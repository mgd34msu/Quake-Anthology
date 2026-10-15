#include "runtime_import.h"
#include "internal.h"
#include "qa/id_index.h"

typedef struct import_function {
    guest_runtime_function descriptor;
    guest_abi_plan *plan;
    uint64_t reached, failed;
    qa_status last_failure;
    char last_failure_detail[256];
    bool bound, unresolved;
} import_function;
typedef struct import_row {
    guest_runtime_import_key key;
    guest_runtime_import_kind kind;
    uint64_t id, address, bytes;
} import_row;
struct guest_runtime_imports {
    qa_native_guest *guest;
    qa_native_target target;
    import_function *functions;
    import_row *rows;
    qa_native_guest_mapping *traps;
    qa_id_index identities;
    size_t function_count, function_capacity, count, capacity, trap_count, trap_capacity;
    unsigned calls;
    bool detached, closing, restore_leased;
};
enum { IMPORT_FUNCTION_ID, IMPORT_FUNCTION_ADDRESS, IMPORT_FIRST_ROW };

static bool same_target(const qa_native_target *a, const qa_native_target *b)
{ return a->abi == b->abi && a->arch == b->arch && a->os == b->os && a->pointer_bytes == b->pointer_bytes; }
static bool target_valid(const qa_native_target *t)
{
    return t && ((t->os == QA_NATIVE_OS_WINDOWS &&
        ((t->arch == QA_NATIVE_ARCH_I386 && t->pointer_bytes == 4 && t->abi == QA_NATIVE_ABI_CDECL_I386) ||
         (t->arch == QA_NATIVE_ARCH_X86_64 && t->pointer_bytes == 8 && t->abi == QA_NATIVE_ABI_MICROSOFT_X64))) ||
        (t->os == QA_NATIVE_OS_LINUX &&
        ((t->arch == QA_NATIVE_ARCH_I386 && t->pointer_bytes == 4 && t->abi == QA_NATIVE_ABI_SYSTEM_V_I386) ||
         (t->arch == QA_NATIVE_ARCH_X86_64 && t->pointer_bytes == 8 && t->abi == QA_NATIVE_ABI_SYSTEM_V_X64))));
}
static bool same_text(const char *a, const char *b)
{ return a && b ? !strcmp(a, b) : a == b; }
static bool key_valid(const guest_runtime_import_key *k)
{
    return k && k->library && ((k->kind == GUEST_RUNTIME_SYMBOL_NAME && k->name && *k->name && !k->ordinal) ||
        (k->kind == GUEST_RUNTIME_SYMBOL_ORDINAL && !k->name));
}
static bool same_key(const guest_runtime_import_key *a, const guest_runtime_import_key *b)
{
    return a->scope == b->scope && a->kind == b->kind && a->ordinal == b->ordinal &&
        same_text(a->library, b->library) && same_text(a->name, b->name) && same_text(a->version, b->version);
}
static void key_free(guest_runtime_import_key *k)
{ free((void *)k->library); free((void *)k->name); free((void *)k->version); memset(k, 0, sizeof(*k)); }
static bool text_copy(const char *source, const char **out, qa_error *e)
{
    if (!source) { *out = NULL; return true; }
    size_t bytes = strlen(source);
    if (bytes == SIZE_MAX) return guest_fail(e, QA_ERROR_MEMORY, 0, "import string extent overflows");
    char *copy = malloc(bytes + 1);
    if (!copy) return guest_fail(e, QA_ERROR_MEMORY, 0, "owning import symbol text");
    memcpy(copy, source, bytes + 1); *out = copy; return true;
}
static bool key_copy(const guest_runtime_import_key *source, guest_runtime_import_key *out, qa_error *e)
{
    *out = *source; out->library = out->name = out->version = NULL;
    if (text_copy(source->library, &out->library, e) && text_copy(source->name, &out->name, e) &&
        text_copy(source->version, &out->version, e)) return true;
    key_free(out); return false;
}
static void signature_free(guest_abi_signature *s)
{
    for (size_t i = 0; s->parameters && i < s->parameter_count; ++i) free((void *)s->parameters[i].fields);
    free((void *)s->parameters); free((void *)s->result.fields); memset(s, 0, sizeof(*s));
}
static bool layout_copy(const guest_abi_layout *source, guest_abi_layout *out, qa_error *e)
{
    *out = *source; out->fields = NULL;
    if (source->field_count) {
        if (!source->fields || source->field_count > SIZE_MAX / sizeof(*source->fields))
            return guest_fail(e, QA_ERROR_ARGUMENT, 0, "import layout field extent is invalid");
        guest_abi_field *fields = malloc(source->field_count * sizeof(*fields));
        if (!fields) return guest_fail(e, QA_ERROR_MEMORY, 0, "owning import layout fields");
        memcpy(fields, source->fields, source->field_count * sizeof(*fields)); out->fields = fields;
    }
    return true;
}
static bool signature_copy(const guest_abi_signature *source, guest_abi_signature *out, qa_error *e)
{
    *out = *source; out->parameters = NULL; out->parameter_count = 0; out->result.fields = NULL;
    if (source->parameter_count > SIZE_MAX / sizeof(*source->parameters) ||
        (source->parameter_count && !source->parameters))
        return guest_fail(e, QA_ERROR_ARGUMENT, 0, "import parameter extent is invalid");
    guest_abi_layout *parameters = source->parameter_count ? calloc(source->parameter_count, sizeof(*parameters)) : NULL;
    if (source->parameter_count && !parameters) return guest_fail(e, QA_ERROR_MEMORY, 0, "owning import parameters");
    out->parameters = parameters;
    bool okay = layout_copy(&source->result, &out->result, e);
    for (size_t i = 0; okay && i < source->parameter_count; ++i) {
        okay = layout_copy(&source->parameters[i], &parameters[i], e);
        if (okay) ++out->parameter_count;
    }
    if (!okay) { signature_free(out); return false; }
    return true;
}
static bool same_layout(const guest_abi_layout *a, const guest_abi_layout *b)
{
    if (a->kind != b->kind || a->bytes != b->bytes || a->alignment != b->alignment ||
        a->stack_only != b->stack_only || a->field_count != b->field_count || (b->field_count && !b->fields)) return false;
    for (size_t i = 0; i < a->field_count; ++i)
        if (a->fields[i].kind != b->fields[i].kind || a->fields[i].offset != b->fields[i].offset ||
            a->fields[i].count != b->fields[i].count) return false;
    return true;
}
static bool same_signature(const guest_abi_signature *a, const guest_abi_signature *b)
{
    if (a->abi != b->abi || a->convention != b->convention || a->variadic != b->variadic ||
        a->parameter_count != b->parameter_count || (b->parameter_count && !b->parameters) ||
        !same_layout(&a->result, &b->result)) return false;
    for (size_t i = 0; i < a->parameter_count; ++i) if (!same_layout(&a->parameters[i], &b->parameters[i])) return false;
    return true;
}
static import_function *function_at(const guest_runtime_imports *o, uint64_t id)
{
    if (!o) return NULL;
    qa_id_index_cursor cursor = qa_id_index_begin(&o->identities, IMPORT_FUNCTION_ID, id);
    size_t row;
    return qa_id_index_next(&o->identities, &cursor, &row) ? &o->functions[row] : NULL;
}
static import_function *function_address(const guest_runtime_imports *o, uint64_t address)
{
    qa_id_index_cursor cursor = qa_id_index_begin(&o->identities, IMPORT_FUNCTION_ADDRESS, address);
    size_t row;
    return qa_id_index_next(&o->identities, &cursor, &row) ? &o->functions[row] : NULL;
}
static void sync_function(const guest_runtime_imports *o, import_function *f)
{
    if (!o->guest || !o->guest->child || f->descriptor.readonly == GUEST_READONLY_NONE) return;
    uint64_t reached = guest_host_child_reached(o->guest->child, f->descriptor.id);
    if (reached > f->reached) f->reached = reached;
}
static void sync_reached(const guest_runtime_imports *o)
{
    if (!o || !o->guest || !o->guest->child) return;
    for (size_t i = 0; i < o->function_count; ++i)
        if (o->functions[i].descriptor.readonly != GUEST_READONLY_NONE) sync_function(o, o->functions + i);
}
static import_row *first_row(const guest_runtime_imports *o, uint64_t id)
{
    qa_id_index_cursor cursor = qa_id_index_begin(&o->identities, IMPORT_FIRST_ROW, id);
    size_t row;
    return qa_id_index_next(&o->identities, &cursor, &row) ? &o->rows[row] : NULL;
}
static void function_append(guest_runtime_imports *o, import_function f)
{
    size_t row = o->function_count++;
    o->functions[row] = f;
    qa_id_index_insert(&o->identities, IMPORT_FUNCTION_ID, f.descriptor.id, row);
    qa_id_index_insert(&o->identities, IMPORT_FUNCTION_ADDRESS, f.descriptor.address, row);
}
static void row_append(guest_runtime_imports *o, import_row r)
{
    size_t row = o->count++;
    o->rows[row] = r;
    if (r.id && !first_row(o, r.id))
        qa_id_index_insert(&o->identities, IMPORT_FIRST_ROW, r.id, row);
}
static import_row *key_at(const guest_runtime_imports *o, const guest_runtime_import_key *key)
{
    for (size_t i = 0; o && i < o->count; ++i) if (same_key(&o->rows[i].key, key)) return &o->rows[i];
    return NULL;
}
static bool trap_contains(const guest_runtime_imports *o, uint64_t address)
{
    if (!address || address % 16) return false;
    for (size_t i = 0; i < o->trap_count; ++i)
        if (address >= o->traps[i].base && address - o->traps[i].base <= o->traps[i].bytes - 16) return true;
    return false;
}
static bool trap_bytes(const qa_native_guest *guest, uint64_t address, size_t bytes, qa_error *e)
{
    uint8_t data[QA_NATIVE_GUEST_PAGE];
    while (bytes) {
        size_t amount = bytes < sizeof(data) ? bytes : sizeof(data);
        if (!qa_native_guest_read(guest, address, data, amount, e)) return false;
        for (size_t i = 0; i < amount; ++i)
            if (data[i] != 0xcc) return guest_fail(e, QA_ERROR_FORMAT, address + i, "owned import trap bytes changed");
        address += amount; bytes -= amount;
    }
    return true;
}

bool guest_runtime_imports_idle(const guest_runtime_imports *o)
{ return o && !o->calls && !o->closing && !o->detached && qa_native_guest_idle(o->guest); }
static bool declarations_mutable(const guest_runtime_imports *o, qa_error *e)
{
    if (!o || o->closing || o->detached || !guest_mutable(o->guest, e)) return false;
    return qa_native_guest_idle(o->guest) ||
        (o->guest->run && o->guest->callback_depth && !o->guest->publication_depth);
}
size_t guest_runtime_imports_count(const guest_runtime_imports *o)
{ return o ? o->count : 0; }

static bool dispatch(void *context, qa_native_guest *guest, uint64_t id, qa_error *e)
{
    guest_runtime_imports *o = context;
    sync_reached(o);
    import_function *f = function_at(o, id);
    qa_native_guest_cpu cpu;
    if (!o || guest != o->guest || o->detached || o->closing || !f || !f->bound ||
        (!f->unresolved && !f->descriptor.invoke) ||
        o->calls == UINT_MAX || f->reached == UINT64_MAX || !qa_native_guest_cpu_read(guest, &cpu, e) ||
        cpu.instruction != f->descriptor.address || !trap_bytes(guest, f->descriptor.address, 16, e))
        return guest_fail(e, QA_ERROR_ARGUMENT, id, "import dispatch differs from its actual bound trap owner");
    ++o->calls; ++f->reached;
    if (f->unresolved) {
        ++f->failed; f->last_failure = QA_ERROR_UNSUPPORTED; --o->calls;
        import_row *row = first_row(o, id);
        if (row) {
            const guest_runtime_import_key *key = &row->key;
            qa_error_set(e, QA_ERROR_UNSUPPORTED, (size_t)id,
                "unresolved native import scope %llu library %s symbol %s ordinal %u version %s",
                (unsigned long long)key->scope, key->library, key->name ? key->name : "",
                key->ordinal, key->version ? key->version : "<unversioned>");
            qa_error detail = {0};
            qa_error_set(&detail, QA_ERROR_UNSUPPORTED, (size_t)id,
                "unresolved native import scope %llu library %s symbol %s ordinal %u version %s",
                (unsigned long long)key->scope, key->library, key->name ? key->name : "",
                key->ordinal, key->version ? key->version : "<unversioned>");
            memcpy(f->last_failure_detail, detail.message, sizeof(f->last_failure_detail));
            return false;
        }
        return guest_fail(e, QA_ERROR_FORMAT, id, "unresolved import has lost its exact symbol row");
    }
    /* Lazy source services append functions during the callbacks below. The
     * physical table can move; owned signature/plan pointees remain retained
     * because unbind and retirement require idle ownership. */
    guest_runtime_function descriptor = f->descriptor;
    guest_abi_plan *plan = f->plan;
    size_t count = guest_abi_argument_count(plan);
    qa_arena scratch; qa_arena_init_pool(&scratch, &guest->scratch_pages);
    qa_native_value *args = count ? qa_arena_alloc(&scratch, count * sizeof(*args), _Alignof(qa_native_value), e) : NULL;
    qa_buffer storage = {0}; guest_abi_plan *expanded = NULL;
    bool okay = !count || args;
    if (!okay) guest_fail(e, QA_ERROR_MEMORY, id, "owning actual import arguments");
    if (okay) okay = guest_abi_decode(plan, guest, args, count, &scratch, &storage, e);
    if (okay && descriptor.signature.variadic) {
        const guest_abi_layout *extras = NULL; size_t extra_count = 0;
        okay = descriptor.variadic(descriptor.context, guest, args, count, &extras, &extra_count, e);
        f = function_at(o, id);
        if (okay && (!f || f->plan != plan || !f->bound))
            okay = guest_fail(e, QA_ERROR_FORMAT, id, "variadic selector lost its retained import declaration");
        if (okay) okay = guest_abi_plan_create(&descriptor.signature, extras, extra_count, &scratch, &expanded, e);
        if (okay) {
            storage = (qa_buffer){0}; args = NULL;
            count = guest_abi_argument_count(expanded);
            if (count > SIZE_MAX / sizeof(*args)) okay = guest_fail(e, QA_ERROR_MEMORY, id, "expanded import arguments overflow");
            else args = count ? qa_arena_alloc(&scratch, count * sizeof(*args), _Alignof(qa_native_value), e) : NULL;
            if (okay && count && !args) okay = guest_fail(e, QA_ERROR_MEMORY, id, "owning expanded import arguments");
            if (okay) okay = guest_abi_decode(expanded, guest, args, count, &scratch, &storage, e);
        }
    }
    qa_native_value result = {.type = QA_NATIVE_VOID};
    if (okay) {
        okay = descriptor.invoke(descriptor.context, guest, args, count, &result, e);
        f = function_at(o, id);
        if (okay && (!f || f->plan != plan || !f->bound))
            okay = guest_fail(e, QA_ERROR_FORMAT, id, "service callback lost its retained import declaration");
    }
    if (okay) okay = guest_abi_return(expanded ? expanded : plan, guest, &result, e);
    f = function_at(o, id);
    if (!f) okay = guest_fail(e, QA_ERROR_FORMAT, id, "import dispatch lost its stable service identity");
    if (!okay && f) {
        ++f->failed;
        f->last_failure = e && e->code != QA_OK ? e->code : QA_ERROR_FORMAT;
        if (!e || e->code == QA_OK) guest_fail(e, f->last_failure, id, "actual import callback rejected its continuation");
        size_t length = 0;
        if (e) while (length + 1 < sizeof(f->last_failure_detail) && e->message[length]) ++length;
        if (length) memcpy(f->last_failure_detail, e->message, length);
        f->last_failure_detail[length] = 0;
        if (!e) {
            static const char message[] = "actual import callback rejected its continuation";
            memcpy(f->last_failure_detail, message, sizeof(message));
        }
    }
    guest_abi_plan_destroy(expanded); qa_arena_destroy(&scratch); --o->calls;
    return okay;
}

static bool native_admit(qa_native_guest *guest, const import_function *f, qa_error *error)
{
    guest_abi_scalar_call scalar;
    guest_readonly_kind kind = f->descriptor.readonly;
    if (!guest->child || kind == GUEST_READONLY_NONE || !guest_abi_scalar_plan(f->plan, &scalar)) return true;
    if (!((kind == GUEST_READONLY_COMPARE && scalar.count == 3 && scalar.result_bytes == 4) ||
        (kind == GUEST_READONLY_LENGTH && scalar.count == 1 && scalar.result_bytes == 8))) return true;
    return guest_host_child_bind(guest->child, f->descriptor.id, f->descriptor.address,
        kind, &scalar, f->reached, error);
}
static bool bind_function(guest_runtime_imports *o, const import_function *f, qa_error *error)
{
    qa_native_guest_callback callback = {f->descriptor.id, f->descriptor.address, dispatch, o};
    return qa_native_guest_bind(o->guest, &callback, error) && native_admit(o->guest, f, error);
}

bool guest_runtime_imports_create(qa_native_guest *guest, guest_runtime_imports **out, qa_error *e)
{
    if (!qa_native_guest_idle(guest) || !out || *out)
        return guest_fail(e, QA_ERROR_ARGUMENT, 0, "import registry requires its actual idle guest and empty output");
    guest_runtime_imports *o = calloc(1, sizeof(*o));
    if (!o) return guest_fail(e, QA_ERROR_MEMORY, 0, "owning import registry");
    o->guest = guest; o->target = guest->options.image.target; *out = o; return true;
}

bool guest_runtime_imports_traps(guest_runtime_imports *o, uint64_t base, size_t bytes, qa_error *e)
{
    if (!guest_runtime_imports_idle(o) || !bytes || bytes % QA_NATIVE_GUEST_PAGE)
        return guest_fail(e, QA_ERROR_ARGUMENT, base, "import trap reservation requires idle physical page storage");
    if (!guest_grow((void **)&o->traps, &o->trap_capacity, o->trap_count + 1, sizeof(*o->traps), e)) return false;
    uint8_t *code = malloc(bytes);
    if (!code) return guest_fail(e, QA_ERROR_MEMORY, base, "owning actual import trap bytes");
    memset(code, 0xcc, bytes); qa_native_guest_mapping mapping;
    bool okay = qa_native_guest_map(o->guest, base, bytes, QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_EXECUTE,
        (qa_bytes){code, bytes}, &mapping, e);
    free(code);
    if (okay) o->traps[o->trap_count++] = mapping;
    return okay;
}

static bool add_row(guest_runtime_imports *o, const guest_runtime_import_key *key,
    import_row *row, qa_error *e)
{
    if (!key_valid(key) || key_at(o, key)) return guest_fail(e, QA_ERROR_ARGUMENT, 0, "import symbol key is invalid or already registered");
    if (!guest_grow((void **)&o->rows, &o->capacity, o->count + 1, sizeof(*o->rows), e) ||
        !qa_id_index_reserve(&o->identities, 1, e)) return false;
    return key_copy(key, &row->key, e);
}

bool guest_runtime_imports_function(guest_runtime_imports *o, const guest_runtime_import_key *key,
    const guest_runtime_function *d, qa_error *e)
{
    if (!declarations_mutable(o, e) || !d || !d->id || !d->invoke || d->signature.abi != o->target.abi ||
        (d->signature.variadic ? !d->variadic || !d->variadic_id : d->variadic || d->variadic_id) ||
        !trap_contains(o, d->address) || !key_valid(key))
        return guest_fail(e, QA_ERROR_ARGUMENT, 0, "import function needs its actual signature, service identity and owned executable trap");
    import_function *previous = function_at(o, d->id);
    import_function *occupied = function_address(o, d->address);
    if (occupied && occupied->unresolved) {
        import_row *row = key_at(o, key);
        if (!guest_runtime_imports_idle(o) || (previous && previous != occupied) ||
            !row || row->kind != GUEST_RUNTIME_IMPORT_UNRESOLVED ||
            row->id != occupied->descriptor.id || row->address != d->address)
            return guest_fail(e, QA_ERROR_ARGUMENT, d->id, "import promotion changed its unresolved key or physical trap");
        import_function replacement = *occupied;
        replacement.descriptor = *d;
        memset(&replacement.descriptor.signature, 0, sizeof(replacement.descriptor.signature));
        replacement.plan = NULL; replacement.unresolved = false; replacement.bound = true;
        bool okay = signature_copy(&d->signature, &replacement.descriptor.signature, e) &&
            guest_abi_plan_create(&replacement.descriptor.signature, NULL, 0, NULL, &replacement.plan, e);
        if (okay && occupied->bound) {
            okay = qa_native_guest_unbind(o->guest, occupied->descriptor.id, e);
            if (okay) occupied->bound = false;
        }
        if (okay) okay = bind_function(o, &replacement, e);
        if (!okay) {
            signature_free(&replacement.descriptor.signature); guest_abi_plan_destroy(replacement.plan);
            return false;
        }
        uint64_t old_id = occupied->descriptor.id;
        size_t function_index = (size_t)(occupied - o->functions);
        size_t first_index = (size_t)(first_row(o, old_id) - o->rows);
        signature_free(&occupied->descriptor.signature); guest_abi_plan_destroy(occupied->plan);
        *occupied = replacement;
        qa_id_index_remove(&o->identities, IMPORT_FUNCTION_ID, old_id);
        qa_id_index_remove(&o->identities, IMPORT_FIRST_ROW, old_id);
        qa_id_index_insert(&o->identities, IMPORT_FUNCTION_ID, d->id, function_index);
        qa_id_index_insert(&o->identities, IMPORT_FIRST_ROW, d->id, first_index);
        for (size_t i = 0; i < o->count; ++i) if (o->rows[i].id == old_id) {
            o->rows[i].id = d->id;
            o->rows[i].kind = GUEST_RUNTIME_IMPORT_FUNCTION;
        }
        return true;
    }
    if (previous) {
        import_row *row = key_at(o, key);
        if (previous->bound || previous->unresolved || !row || row->kind != GUEST_RUNTIME_IMPORT_FUNCTION || row->id != d->id ||
            previous->descriptor.address != d->address || previous->descriptor.variadic_id != d->variadic_id ||
            !same_signature(&previous->descriptor.signature, &d->signature))
            return guest_fail(e, QA_ERROR_ARGUMENT, d->id, "import rebind changed its retained declaration");
        import_function binding = *previous; binding.descriptor.readonly = d->readonly;
        if (!bind_function(o, &binding, e)) return false;
        previous->descriptor.invoke = d->invoke; previous->descriptor.variadic = d->variadic;
        previous->descriptor.context = d->context; previous->descriptor.readonly = d->readonly;
        previous->bound = true; return true;
    }
    if (occupied)
        return guest_fail(e, QA_ERROR_ARGUMENT, d->address, "import trap slot already belongs to another function");
    import_row row = {.kind = GUEST_RUNTIME_IMPORT_FUNCTION, .id = d->id, .address = d->address, .bytes = 16};
    if (!add_row(o, key, &row, e)) return false;
    import_function f = {.descriptor = *d, .bound = true}; memset(&f.descriptor.signature, 0, sizeof(f.descriptor.signature));
    bool okay = signature_copy(&d->signature, &f.descriptor.signature, e) &&
        guest_abi_plan_create(&f.descriptor.signature, NULL, 0, NULL, &f.plan, e) &&
        guest_grow((void **)&o->functions, &o->function_capacity, o->function_count + 1, sizeof(*o->functions), e) &&
        qa_id_index_reserve(&o->identities, 3, e);
    if (okay) okay = bind_function(o, &f, e);
    if (!okay) { key_free(&row.key); signature_free(&f.descriptor.signature); guest_abi_plan_destroy(f.plan); return false; }
    function_append(o, f); row_append(o, row); return true;
}

bool guest_runtime_imports_unresolved(guest_runtime_imports *o,
    const guest_runtime_import_key *key, uint64_t id, uint64_t address, qa_error *e)
{
    if (!declarations_mutable(o, e) || !id || !trap_contains(o, address))
        return guest_fail(e, QA_ERROR_ARGUMENT, id, "unresolved import needs a real idle or stopped-callback owned trap");
    import_function *prior = function_at(o, id);
    if (prior) {
        import_row *row = key_valid(key) ? key_at(o, key) : NULL;
        if (!prior->unresolved || prior->bound || prior->descriptor.address != address || !row || row->id != id)
            return guest_fail(e, QA_ERROR_ARGUMENT, id, "unresolved rebind differs from its original symbol");
        qa_native_guest_callback callback = {id, address, dispatch, o};
        if (!qa_native_guest_bind(o->guest, &callback, e)) return false;
        prior->bound = true; return true;
    }
    if (function_address(o, address))
        return guest_fail(e, QA_ERROR_ARGUMENT, address, "unresolved trap already has a physical owner");
    import_row row = {.kind = GUEST_RUNTIME_IMPORT_UNRESOLVED, .id = id, .address = address, .bytes = 16};
    if (!add_row(o, key, &row, e)) return false;
    if (!guest_grow((void **)&o->functions, &o->function_capacity, o->function_count + 1, sizeof(*o->functions), e) ||
        !qa_id_index_reserve(&o->identities, 3, e)) {
        key_free(&row.key); return false;
    }
    qa_native_guest_callback callback = {id, address, dispatch, o};
    if (!qa_native_guest_bind(o->guest, &callback, e)) { key_free(&row.key); return false; }
    import_function f = {.descriptor = {.id = id, .address = address}, .bound = true, .unresolved = true};
    function_append(o, f); row_append(o, row); return true;
}

bool guest_runtime_imports_data(guest_runtime_imports *o, const guest_runtime_import_key *key,
    uint64_t address, size_t bytes, qa_error *e)
{
    if (!o || o->detached || o->closing || !guest_mutable(o->guest, e) || !bytes ||
        !guest_range(o->guest, address, bytes, QA_NATIVE_GUEST_READ, e))
        return guest_fail(e, QA_ERROR_ARGUMENT, address, "import data requires its genuine readable extent");
    import_row row = {.kind = GUEST_RUNTIME_IMPORT_DATA, .address = address, .bytes = bytes};
    if (!add_row(o, key, &row, e)) return false;
    row_append(o, row); return true;
}

bool guest_runtime_imports_alias(guest_runtime_imports *o, const guest_runtime_import_key *key,
    uint64_t id, qa_error *e)
{
    import_function *f = function_at(o, id);
    if (!declarations_mutable(o, e) || !f) return guest_fail(e, QA_ERROR_ARGUMENT, id, "import alias requires its actual physical function owner");
    if (!key_valid(key)) return guest_fail(e, QA_ERROR_ARGUMENT, id, "import alias key is invalid");
    import_row *first = first_row(o, id);
    if (first && first->key.scope != key->scope)
        return guest_fail(e, QA_ERROR_ARGUMENT, id, "import alias cannot change its provider scope");
    import_row row = {.kind = f->unresolved ? GUEST_RUNTIME_IMPORT_UNRESOLVED : GUEST_RUNTIME_IMPORT_FUNCTION,
        .id = id, .address = f->descriptor.address, .bytes = 16};
    if (!add_row(o, key, &row, e)) return false;
    row_append(o, row); return true;
}

bool guest_runtime_imports_unbind(guest_runtime_imports *o, uint64_t id, qa_error *e)
{
    import_function *f = function_at(o, id);
    if (!guest_runtime_imports_idle(o) || !f) return guest_fail(e, QA_ERROR_ARGUMENT, id, "import unbind requires its idle retained function");
    sync_function(o, f);
    if (f->bound && !qa_native_guest_unbind(o->guest, id, e)) return false;
    f->bound = false; f->descriptor.invoke = NULL; f->descriptor.context = NULL; f->descriptor.variadic = NULL; return true;
}

static void view(const guest_runtime_imports *o, const import_row *row, guest_runtime_import_view *out)
{
    *out = (guest_runtime_import_view){row->key, row->kind, row->id, row->address, row->bytes, 0, 0, true, QA_OK, NULL};
    if (row->kind != GUEST_RUNTIME_IMPORT_DATA) {
        import_function *f = function_at(o, row->id);
        sync_function(o, f);
        out->reached = f->reached; out->failed = f->failed; out->bound = f->bound; out->last_failure = f->last_failure;
        out->last_failure_detail = f->failed ? f->last_failure_detail : NULL;
    }
}
bool guest_runtime_imports_find(const guest_runtime_imports *o, const guest_runtime_import_key *key,
    guest_runtime_import_view *out, qa_error *e)
{
    if (!o || !out || !key_valid(key)) return guest_fail(e, QA_ERROR_ARGUMENT, 0, "exact import lookup key and output are required");
    import_row *row = key_at(o, key);
    if (!row) return guest_fail(e, QA_ERROR_NOT_FOUND, 0, "exact import symbol has no registered provider");
    view(o, row, out); return true;
}
bool guest_runtime_imports_at(const guest_runtime_imports *o, size_t index,
    guest_runtime_import_view *out, qa_error *e)
{
    if (!o || !out || index >= o->count) return guest_fail(e, QA_ERROR_ARGUMENT, index, "import physical row is unavailable");
    view(o, &o->rows[index], out); return true;
}

void guest_runtime_imports_abandon(guest_runtime_imports **owner)
{
    if (!owner || !*owner || (*owner)->calls) return;
    guest_runtime_imports *o = *owner;
    for (size_t i = 0; i < o->count; ++i) key_free(&o->rows[i].key);
    for (size_t i = 0; i < o->function_count; ++i) {
        signature_free(&o->functions[i].descriptor.signature); guest_abi_plan_destroy(o->functions[i].plan);
    }
    qa_id_index_destroy(&o->identities);
    free(o->functions); free(o->rows); free(o->traps); free(o); *owner = NULL;
}
bool guest_runtime_imports_destroy(guest_runtime_imports **owner, qa_error *e)
{
    if (!owner) return guest_fail(e, QA_ERROR_ARGUMENT, 0, "import destruction owner is required");
    guest_runtime_imports *o = *owner;
    if (!o) return true;
    if (o->detached) {
        if (o->restore_leased) return guest_fail(e, QA_ERROR_ARGUMENT, 0, "destroy the actual lower restore candidate before abandoning import callback contexts");
        guest_runtime_imports_abandon(owner); return true;
    }
    if (o->calls || !qa_native_guest_idle(o->guest)) return guest_fail(e, QA_ERROR_ARGUMENT, 0, "import destruction requires idle lower ownership");
    sync_reached(o);
    o->closing = true;
    for (size_t i = 0; i < o->function_count; ++i) if (o->functions[i].bound) {
        if (!qa_native_guest_unbind(o->guest, o->functions[i].descriptor.id, e)) return false;
        o->functions[i].bound = false;
    }
    while (o->trap_count) {
        if (!qa_native_guest_unmap(o->guest, o->traps[0].id, e)) return false;
        memmove(o->traps, o->traps + 1, (--o->trap_count) * sizeof(*o->traps));
    }
    guest_runtime_imports_abandon(owner); return true;
}

typedef struct codec { qa_bytes in; qa_buffer out; size_t at, capacity; bool read; qa_error *e; } codec;
static bool blob(codec *c, void *data, size_t bytes)
{
    if (c->read) {
        if (bytes > c->in.size - c->at) {
            guest_fail(c->e, QA_ERROR_FORMAT, c->at, "import checkpoint is truncated");
            return false;
        }
        if (bytes) memcpy(data, c->in.data + c->at, bytes);
        c->at += bytes; return true;
    }
    if (bytes > SIZE_MAX - c->out.size) return guest_fail(c->e, QA_ERROR_MEMORY, 0, "import checkpoint extent overflows");
    if (!guest_grow((void **)&c->out.data, &c->capacity, c->out.size + bytes, 1, c->e)) return false;
    if (bytes) memcpy(c->out.data + c->out.size, data, bytes);
    c->out.size += bytes; return true;
}
static bool u64(codec *c, uint64_t *value)
{
    uint8_t data[8]; if (!c->read) qa_store_u64le(data, *value);
    if (!blob(c, data, 8)) return false;
    if (c->read) *value = qa_load_u64le(data);
    return true;
}
static bool size(codec *c, size_t *value, size_t minimum)
{
    uint64_t n = *value;
    if (!u64(c, &n)) return false;
    if (c->read) {
        if (n > SIZE_MAX || (minimum && n > (c->in.size - c->at) / minimum))
            return guest_fail(c->e, QA_ERROR_FORMAT, c->at, "import checkpoint count exceeds actual bytes");
        *value = (size_t)n;
    }
    return true;
}
static bool enumeration(codec *c, unsigned *value, unsigned maximum)
{
    uint64_t n = *value;
    if (!u64(c, &n)) return false;
    if (n > maximum) return guest_fail(c->e, QA_ERROR_FORMAT, c->at, "import checkpoint enum is invalid");
    *value = (unsigned)n; return true;
}
static bool text(codec *c, const char **value)
{
    uint64_t n = *value ? strlen(*value) : UINT64_MAX;
    if (!u64(c, &n)) return false;
    if (n == UINT64_MAX) { if (c->read) *value = NULL; return true; }
    if (!c->read) return blob(c, (void *)*value, (size_t)n);
    if (n >= SIZE_MAX || n > c->in.size - c->at || memchr(c->in.data + c->at, 0, (size_t)n))
        return guest_fail(c->e, QA_ERROR_FORMAT, c->at, "import checkpoint string extent is invalid");
    char *p = malloc((size_t)n + 1);
    if (!p) return guest_fail(c->e, QA_ERROR_MEMORY, c->at, "owning restored import string");
    if (!blob(c, p, (size_t)n)) { free(p); return false; }
    p[n] = 0; *value = p; return true;
}
static bool layout(codec *c, guest_abi_layout *l)
{
    unsigned kind = l->kind, stack_only = l->stack_only;
    if (!enumeration(c, &kind, QA_NATIVE_BYTES) || !size(c, &l->bytes, 0) ||
        !size(c, &l->alignment, 0) || !enumeration(c, &stack_only, 1) || !size(c, &l->field_count, 24)) return false;
    l->kind = (qa_native_value_type)kind;
    l->stack_only = stack_only != 0;
    if (c->read && l->field_count) {
        if (l->field_count > SIZE_MAX / sizeof(*l->fields)) return guest_fail(c->e, QA_ERROR_FORMAT, c->at, "import field table overflows");
        l->fields = calloc(l->field_count, sizeof(*l->fields));
        if (!l->fields) return guest_fail(c->e, QA_ERROR_MEMORY, c->at, "owning restored import fields");
    }
    for (size_t i = 0; i < l->field_count; ++i) {
        guest_abi_field *f = (guest_abi_field *)&l->fields[i]; kind = f->kind;
        if (!enumeration(c, &kind, QA_NATIVE_BYTES) || !size(c, &f->offset, 0) || !size(c, &f->count, 0)) return false;
        f->kind = (qa_native_value_type)kind;
    }
    return true;
}
static bool signature(codec *c, guest_abi_signature *s)
{
    unsigned abi = s->abi, convention = s->convention, variadic = s->variadic;
    if (!enumeration(c, &abi, QA_NATIVE_ABI_AAPCS64) || !enumeration(c, &convention, QA_NATIVE_CALL_THISCALL) ||
        !enumeration(c, &variadic, 1) || !size(c, &s->parameter_count, 32) || !layout(c, &s->result)) return false;
    s->abi = (qa_native_abi)abi; s->convention = (qa_native_call_convention)convention; s->variadic = variadic != 0;
    if (c->read && s->parameter_count) {
        if (s->parameter_count > SIZE_MAX / sizeof(*s->parameters)) return guest_fail(c->e, QA_ERROR_FORMAT, c->at, "import parameter table overflows");
        s->parameters = calloc(s->parameter_count, sizeof(*s->parameters));
        if (!s->parameters) return guest_fail(c->e, QA_ERROR_MEMORY, c->at, "owning restored import parameters");
    }
    for (size_t i = 0; i < s->parameter_count; ++i) if (!layout(c, (guest_abi_layout *)&s->parameters[i])) return false;
    return true;
}
static bool mapping(codec *c, qa_native_guest_mapping *m)
{
    unsigned permissions = m->permissions;
    if (!u64(c, &m->id) || !u64(c, &m->base) || !u64(c, &m->bytes) || !u64(c, &m->backing) ||
        !u64(c, &m->backing_offset) || !enumeration(c, &permissions, 7)) return false;
    m->permissions = permissions; return true;
}
static bool same_mapping(const qa_native_guest_mapping *a, const qa_native_guest_mapping *b)
{
    return a->id == b->id && a->base == b->base && a->bytes == b->bytes && a->backing == b->backing &&
        a->backing_offset == b->backing_offset && a->permissions == b->permissions;
}
static bool current(const guest_runtime_imports *o, const qa_native_guest *guest, qa_error *e)
{
    if (!same_target(&o->target, &guest->options.image.target)) return guest_fail(e, QA_ERROR_FORMAT, 0, "import owner target differs from actual guest");
    for (size_t i = 0; i < o->trap_count; ++i) {
        bool found = false;
        for (size_t j = 0; j < guest->mapping_count; ++j) if (same_mapping(&o->traps[i], &guest->mappings[j])) { found = true; break; }
        if (!found || o->traps[i].bytes > SIZE_MAX)
            return guest_fail(e, QA_ERROR_FORMAT, i, "import trap mapping has lost its real lower owner");
        if (!trap_bytes(guest, o->traps[i].base, (size_t)o->traps[i].bytes, e)) return false;
    }
    for (size_t i = 0; i < o->function_count; ++i) {
        const import_function *f = &o->functions[i];
        const qa_native_guest_callback *b = guest_callback_id(guest, f->descriptor.id);
        const qa_native_guest_callback *address = guest_callback_address(guest, f->descriptor.address);
        bool found = b || address;
        if (b && address && b != address)
            return guest_fail(e, QA_ERROR_FORMAT, f->descriptor.id, "import callback physical binding differs");
        if (!b) b = address;
        if (b && (!f->bound || b->id != f->descriptor.id || b->address != f->descriptor.address ||
            b->invoke != dispatch || b->context != o))
            return guest_fail(e, QA_ERROR_FORMAT, f->descriptor.id, "import callback physical binding differs");
        if (found != f->bound) return guest_fail(e, QA_ERROR_FORMAT, f->descriptor.id, "import bound receipt differs from lower callback registry");
    }
    for (size_t i = 0; i < o->count; ++i)
        if (o->rows[i].kind == GUEST_RUNTIME_IMPORT_DATA &&
            (o->rows[i].bytes > SIZE_MAX || !guest_range(guest, o->rows[i].address,
                (size_t)o->rows[i].bytes, 0, e))) return false;
    return true;
}

bool guest_runtime_imports_checkpoint(const guest_runtime_imports *owner, qa_buffer *out, qa_error *e)
{
    if (!guest_runtime_imports_idle(owner) || !out || out->data || out->size || !current(owner, owner->guest, e))
        return guest_fail(e, QA_ERROR_ARGUMENT, 0, "import checkpoint requires its idle current lower owner and empty output");
    guest_runtime_imports *o = (guest_runtime_imports *)owner;
    sync_reached(o);
    codec c = {.e = e}; uint8_t magic[4] = {'Q','G','I','M'};
    unsigned os = o->target.os, arch = o->target.arch, abi = o->target.abi, word = o->target.pointer_bytes;
    bool okay = blob(&c, magic, 4) && enumeration(&c, &os, QA_NATIVE_OS_LINUX) &&
        enumeration(&c, &arch, QA_NATIVE_ARCH_AARCH64) && enumeration(&c, &abi, QA_NATIVE_ABI_AAPCS64) &&
        enumeration(&c, &word, 8) && size(&c, &o->trap_count, 48);
    for (size_t i = 0; okay && i < o->trap_count; ++i) okay = mapping(&c, &o->traps[i]);
    if (okay) okay = size(&c, &o->function_count, 72);
    for (size_t i = 0; okay && i < o->function_count; ++i) {
        import_function *f = &o->functions[i]; unsigned bound = f->bound, last = f->last_failure, unresolved = f->unresolved;
        okay = u64(&c, &f->descriptor.id) && u64(&c, &f->descriptor.address) && u64(&c, &f->descriptor.variadic_id) &&
            enumeration(&c, &bound, 1) && u64(&c, &f->reached) && u64(&c, &f->failed) &&
            enumeration(&c, &last, QA_ERROR_NOT_FOUND) && enumeration(&c, &unresolved, 1);
        const char *detail = f->last_failure_detail;
        if (okay) okay = text(&c, &detail);
        if (okay && !f->unresolved) okay = signature(&c, &f->descriptor.signature);
    }
    if (okay) okay = size(&c, &o->count, 56);
    for (size_t i = 0; okay && i < o->count; ++i) {
        import_row *r = &o->rows[i]; unsigned symbol = r->key.kind, kind = r->kind; uint64_t ordinal = r->key.ordinal;
        okay = u64(&c, &r->key.scope) && enumeration(&c, &symbol, GUEST_RUNTIME_SYMBOL_ORDINAL) &&
            u64(&c, &ordinal) && text(&c, &r->key.library) && text(&c, &r->key.name) && text(&c, &r->key.version) &&
            enumeration(&c, &kind, GUEST_RUNTIME_IMPORT_UNRESOLVED) && u64(&c, &r->id) && u64(&c, &r->address) && u64(&c, &r->bytes);
    }
    if (okay) *out = c.out; else qa_buffer_free(&c.out);
    return okay;
}

bool guest_runtime_imports_decode(qa_bytes bytes, const qa_native_target *target,
    guest_runtime_function_resolve_fn resolve, void *context, guest_runtime_imports **out, qa_error *e)
{
    if (!target_valid(target) || !out || *out || !bytes.data)
        return guest_fail(e, QA_ERROR_ARGUMENT, 0, "detached import decode needs actual supported target and empty output");
    guest_runtime_imports *o = calloc(1, sizeof(*o));
    if (!o) return guest_fail(e, QA_ERROR_MEMORY, 0, "owning detached import candidate");
    o->detached = true;
    codec c = {.in = bytes, .read = true, .e = e}; uint8_t magic[4];
    unsigned os = 0, arch = 0, abi = 0, word = 0;
    bool okay = blob(&c, magic, 4) && !memcmp(magic, "QGIM", 4) &&
        enumeration(&c, &os, QA_NATIVE_OS_LINUX) && enumeration(&c, &arch, QA_NATIVE_ARCH_AARCH64) &&
        enumeration(&c, &abi, QA_NATIVE_ABI_AAPCS64) && enumeration(&c, &word, 8);
    o->target = (qa_native_target){(qa_native_os)os, (qa_native_arch)arch, (qa_native_abi)abi, (uint8_t)word};
    if (okay) okay = same_target(&o->target, target);
    size_t count = 0;
    if (okay) okay = size(&c, &count, 48) && guest_grow((void **)&o->traps, &o->trap_capacity, count, sizeof(*o->traps), e);
    for (size_t i = 0; okay && i < count; ++i) {
        qa_native_guest_mapping m = {0}; okay = mapping(&c, &m);
        if (okay) okay = m.id && m.backing && m.bytes && !(m.bytes % QA_NATIVE_GUEST_PAGE) &&
            m.base && !(m.base % QA_NATIVE_GUEST_PAGE) && m.bytes <= UINT64_MAX - m.base &&
            m.permissions == (QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_EXECUTE) &&
            (target->pointer_bytes != 4 || (m.base <= UINT32_MAX && m.bytes <= UINT64_C(0x100000000) - m.base));
        for (size_t j = 0; okay && j < o->trap_count; ++j)
            if (o->traps[j].id == m.id || (m.base < o->traps[j].base + o->traps[j].bytes &&
                o->traps[j].base < m.base + m.bytes)) okay = false;
        if (okay) o->traps[o->trap_count++] = m;
    }
    count = 0;
    if (okay) okay = size(&c, &count, 72) && guest_grow((void **)&o->functions, &o->function_capacity, count, sizeof(*o->functions), e);
    for (size_t i = 0; okay && i < count; ++i) {
        import_function f = {0}; unsigned bound = 0, last = 0, unresolved = 0;
        okay = u64(&c, &f.descriptor.id) && u64(&c, &f.descriptor.address) && u64(&c, &f.descriptor.variadic_id) &&
            enumeration(&c, &bound, 1) && u64(&c, &f.reached) && u64(&c, &f.failed) &&
            enumeration(&c, &last, QA_ERROR_NOT_FOUND) && enumeration(&c, &unresolved, 1);
        f.bound = bound != 0; f.last_failure = (qa_status)last; f.unresolved = unresolved != 0;
        const char *detail = NULL;
        if (okay) okay = text(&c, &detail) && detail && strlen(detail) < sizeof(f.last_failure_detail);
        if (okay) memcpy(f.last_failure_detail, detail, strlen(detail) + 1);
        free((void *)detail);
        if (okay && !f.unresolved) okay = signature(&c, &f.descriptor.signature);
        /* Every false import terminates the lower guest, so an idle saved
         * continuation cannot contain a failed service receipt. */
        if (okay) okay = f.descriptor.id && trap_contains(o, f.descriptor.address) && !f.failed &&
            last == QA_OK && !f.last_failure_detail[0] && (f.unresolved ? !f.descriptor.variadic_id :
                f.descriptor.signature.abi == target->abi &&
                (f.descriptor.signature.variadic == (f.descriptor.variadic_id != 0)));
        if (okay) okay = !function_at(o, f.descriptor.id) && !function_address(o, f.descriptor.address) &&
            qa_id_index_reserve(&o->identities, 2, e);
        if (okay) function_append(o, f);
        else signature_free(&f.descriptor.signature);
    }
    count = 0;
    if (okay) okay = size(&c, &count, 56) && guest_grow((void **)&o->rows, &o->capacity, count, sizeof(*o->rows), e);
    for (size_t i = 0; okay && i < count; ++i) {
        import_row r = {0}; unsigned symbol = 0, kind = 0; uint64_t ordinal = 0;
        okay = u64(&c, &r.key.scope) && enumeration(&c, &symbol, GUEST_RUNTIME_SYMBOL_ORDINAL) &&
            u64(&c, &ordinal) && ordinal <= UINT32_MAX && text(&c, &r.key.library) && text(&c, &r.key.name) &&
            text(&c, &r.key.version) && enumeration(&c, &kind, GUEST_RUNTIME_IMPORT_UNRESOLVED) &&
            u64(&c, &r.id) && u64(&c, &r.address) && u64(&c, &r.bytes);
        r.key.kind = (guest_runtime_symbol_kind)symbol; r.key.ordinal = (uint32_t)ordinal;
        r.kind = (guest_runtime_import_kind)kind;
        if (okay) okay = key_valid(&r.key) && !key_at(o, &r.key) && r.address && r.bytes && r.bytes <= UINT64_MAX - r.address &&
            (target->pointer_bytes != 4 || (r.address <= UINT32_MAX && r.bytes <= UINT64_C(0x100000000) - r.address));
        if (okay && r.kind != GUEST_RUNTIME_IMPORT_DATA) {
            import_function *f = function_at(o, r.id);
            okay = f && r.address == f->descriptor.address && r.bytes == 16 &&
                f->unresolved == (r.kind == GUEST_RUNTIME_IMPORT_UNRESOLVED);
            import_row *first = first_row(o, r.id);
            if (first && first->key.scope != r.key.scope) okay = false;
        } else if (okay) okay = !r.id && r.bytes <= SIZE_MAX;
        if (okay && r.id && !first_row(o, r.id)) okay = qa_id_index_reserve(&o->identities, 1, e);
        if (okay) row_append(o, r); else key_free(&r.key);
    }
    if (okay) okay = c.at == bytes.size;
    /* All encoded rows and aliases are qualified before any plan/resolver work. */
    for (size_t i = 0; okay && i < o->function_count; ++i) {
        import_function *f = &o->functions[i]; bool has_row = first_row(o, f->descriptor.id) != NULL;
        if (f->unresolved) { okay = has_row; continue; }
        guest_runtime_function d = {0};
        okay = has_row && resolve && resolve(context, f->descriptor.id, &d, e) && d.id == f->descriptor.id &&
            d.address == f->descriptor.address && d.variadic_id == f->descriptor.variadic_id && d.invoke &&
            (f->descriptor.signature.variadic ? d.variadic != NULL : d.variadic == NULL) &&
            same_signature(&f->descriptor.signature, &d.signature);
        if (okay) okay = guest_abi_plan_create(&f->descriptor.signature, NULL, 0, NULL, &f->plan, e);
        if (okay) f->descriptor.readonly = d.readonly;
        if (okay && f->bound) {
            f->descriptor.invoke = d.invoke; f->descriptor.variadic = d.variadic; f->descriptor.context = d.context;
        }
    }
    if (!okay) {
        guest_runtime_imports_abandon(&o);
        if (!e || e->code == QA_OK) guest_fail(e, QA_ERROR_FORMAT, c.at, "import checkpoint identity, layout or descriptor is invalid");
        return false;
    }
    *out = o; return true;
}

bool guest_runtime_imports_callback(void *context, uint64_t id, uint64_t address,
    qa_native_guest_callback *out, qa_error *e)
{
    guest_runtime_imports *o = context; import_function *f = function_at(o, id);
    if (!o || !o->detached || !out)
        return guest_fail(e, QA_ERROR_FORMAT, id, "saved lower callback has no detached import owner");
    if (!f)
        return guest_fail(e, QA_ERROR_NOT_FOUND, id, "saved lower callback is not owned by this import registry");
    if (!f->bound || f->descriptor.address != address ||
        (!f->unresolved && !f->descriptor.invoke))
        return guest_fail(e, QA_ERROR_FORMAT, id, "saved lower callback has no exact detached import descriptor");
    o->restore_leased = true;
    *out = (qa_native_guest_callback){id, address, dispatch, o}; return true;
}

bool guest_runtime_imports_attach(guest_runtime_imports *o, qa_native_guest *guest, qa_error *e)
{
    if (!o || !o->detached || o->guest || o->calls || !qa_native_guest_idle(guest) || !current(o, guest, e))
        return guest_fail(e, QA_ERROR_ARGUMENT, 0, "import attach requires the same completed lower candidate and actual saved bindings");
    for (size_t i = 0; i < o->function_count; ++i)
        if (o->functions[i].bound && !o->functions[i].unresolved &&
            !native_admit(guest, o->functions + i, e)) return false;
    o->guest = guest; o->detached = false; return true;
}
