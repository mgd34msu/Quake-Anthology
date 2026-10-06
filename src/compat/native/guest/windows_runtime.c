#include "internal.h"
#include "windows_runtime.h"
#include "windows_kernel.h"
#include "windows_crt.h"
#include "windows_msvc.h"
#include "pe_bind.h"
#include <ctype.h>
#include <stdio.h>
#include <math.h>

enum { WINDOWS_STORAGE_TAG = 0x57494e, WINDOWS_HEAP_TAG = 0x57484c };
static bool call_policy(qa_native_guest_backend execution, size_t budget)
{
    return execution == QA_NATIVE_GUEST_EMULATED ? budget != 0 :
        execution == QA_NATIVE_GUEST_HOST_X86_64 && budget == 0;
}
static const uint8_t cfg_dispatch_code[] = {0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x48,0x83,0xec,0x20,
    0x48,0x89,0xc1,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x83,0xc4,0x20,
    0x41,0x59,0x41,0x58,0x5a,0x59,0x58,0xff,0xe0};

static char *copy_text(const char *value, qa_error *error)
{
    size_t bytes = strlen(value) + 1;
    char *out = malloc(bytes);
    if (!out) { guest_fail(error, QA_ERROR_MEMORY, 0, "retaining Windows runtime text"); return NULL; }
    memcpy(out, value, bytes); return out;
}

static char *canonical(const char *value, qa_error *error)
{
    const char *base = value;
    for (const char *at = value; *at; ++at) if (*at == '/' || *at == '\\') base = at + 1;
    size_t length = strlen(base);
    if (length > SIZE_MAX - 5) { guest_fail(error, QA_ERROR_MEMORY, 0, "Windows library name overflows"); return NULL; }
    char *out = malloc(length + 5);
    if (!out) { guest_fail(error, QA_ERROR_MEMORY, 0, "retaining Windows library name"); return NULL; }
    for (size_t i = 0; i < length; ++i) out[i] = (char)tolower((unsigned char)base[i]);
    if (length < 4 || memcmp(out + length - 4, ".dll", 4)) { memcpy(out + length, ".dll", 4); length += 4; }
    out[length] = 0; return out;
}

bool windows_read(guest_windows *owner, uint64_t address, size_t bytes, uint64_t *out, qa_error *error)
{
    uint8_t data[8];
    if (!owner || !out || !bytes || bytes > 8 || !qa_native_guest_read(owner->guest, address, data, bytes, error)) return false;
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; ++i) value |= (uint64_t)data[i] << (i * 8);
    *out = value; return true;
}

bool windows_write(guest_windows *owner, uint64_t address, size_t bytes, uint64_t value, qa_error *error)
{
    uint8_t data[8];
    if (!owner || !bytes || bytes > 8) return guest_fail(error, QA_ERROR_ARGUMENT, address, "invalid Windows scalar store");
    for (size_t i = 0; i < bytes; ++i) data[i] = (uint8_t)(value >> (i * 8));
    return qa_native_guest_write(owner->guest, address, (qa_bytes){data, bytes}, error);
}

bool windows_zero(guest_windows *owner, uint64_t address, size_t bytes, qa_error *error)
{
    uint8_t zero[4096] = {0};
    while (bytes) {
        size_t amount = bytes < sizeof(zero) ? bytes : sizeof(zero);
        if (!qa_native_guest_write(owner->guest, address, (qa_bytes){zero, amount}, error)) return false;
        bytes -= amount; address += amount;
    }
    return true;
}

bool windows_storage(guest_windows *owner, size_t bytes, uint64_t *out, qa_error *error)
{ return qa_native_guest_allocate(owner->guest, bytes, WINDOWS_STORAGE_TAG, out, error); }

bool windows_string(guest_windows *owner, uint64_t address, bool wide,
    uint16_t **out, size_t *length, qa_error *error)
{
    if (!address || !out || *out || !length) return guest_fail(error, QA_ERROR_ARGUMENT, address, "Windows string requires real storage and empty output");
    size_t count = 0, capacity = 0; uint16_t *text = NULL;
    for (; count < 1048576; ++count) {
        uint64_t value;
        if (!windows_read(owner, address + count * (wide ? 2 : 1), wide ? 2 : 1, &value, error) ||
            !guest_grow((void **)&text, &capacity, count + 1, sizeof(*text), error)) { free(text); return false; }
        text[count] = (uint16_t)value;
        if (!value) { *out = text; *length = count; return true; }
    }
    free(text); return guest_fail(error, QA_ERROR_FORMAT, address, "Windows string exceeds source read limit");
}

bool windows_store_string(guest_windows *owner, const uint16_t *text, size_t length,
    bool wide, uint64_t *out, qa_error *error)
{
    size_t unit = wide ? 2 : 1;
    if (length > SIZE_MAX / unit - 1 || (length && !text)) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows stored string extent is invalid");
    size_t bytes = (length + 1) * unit;
    uint8_t *data = calloc(1, bytes);
    if (!data) return guest_fail(error, QA_ERROR_MEMORY, 0, "encoding Windows process string");
    for (size_t i = 0; i < length; ++i) {
        if (wide) qa_store_u16le(data + i * 2, text[i]); else data[i] = (uint8_t)text[i];
    }
    bool okay = windows_storage(owner, bytes, out, error) && qa_native_guest_write(owner->guest, *out, (qa_bytes){data, bytes}, error);
    free(data); return okay;
}

bool windows_last_error(guest_windows *owner, uint32_t value, qa_error *error)
{ return windows_write(owner, owner->teb + (owner->target.pointer_bytes == 4 ? 0x34 : 0x68), 4, value, error); }

bool windows_allocate(guest_windows *owner, size_t bytes, uint64_t heap, uint64_t *out, qa_error *error)
{
    *out = 0;
    if (bytes > 0x10000000) return windows_last_error(owner, 8, error);
    if (!guest_grow((void **)&owner->allocations, &owner->allocation_capacity,
        owner->allocation_count + 1, sizeof(*owner->allocations), error)) return false;
    uint64_t address;
    if (!qa_native_guest_allocate(owner->guest, bytes ? bytes : 1, WINDOWS_HEAP_TAG, &address, error)) return false;
    owner->allocations[owner->allocation_count++] = (windows_heap_allocation){address, heap, bytes ? bytes : 1};
    *out = address; return true;
}

bool windows_allocation_size(guest_windows *owner, uint64_t address, uint64_t heap, uint64_t *out)
{
    for (size_t i = 0; i < owner->allocation_count; ++i)
        if (owner->allocations[i].address == address && owner->allocations[i].heap == heap) {
            *out = owner->allocations[i].requested; return true;
        }
    return false;
}

bool windows_free(guest_windows *owner, uint64_t address, uint64_t heap, qa_error *error)
{
    if (!address) return true;
    for (size_t i = 0; i < owner->allocation_count; ++i)
        if (owner->allocations[i].address == address && owner->allocations[i].heap == heap) {
            if (!qa_native_guest_free(owner->guest, address, error)) return false;
            memmove(owner->allocations + i, owner->allocations + i + 1,
                (owner->allocation_count - i - 1) * sizeof(*owner->allocations));
            --owner->allocation_count; return true;
        }
    windows_last_error(owner, 87, error);
    return guest_fail(error, QA_ERROR_NOT_FOUND, address, "Windows free does not name an allocation from this heap");
}

bool windows_destroy_heap(guest_windows *owner, uint64_t heap, qa_error *error)
{
    for (size_t i = 0; i < owner->allocation_count; ) {
        if (owner->allocations[i].heap == heap) {
            if (!windows_free(owner, owner->allocations[i].address, heap, error)) return false;
        } else ++i;
    }
    return true;
}

static guest_abi_layout layout(guest_windows *owner, qa_native_value_type type)
{
    size_t bytes = type == QA_NATIVE_VOID ? 0 : type == QA_NATIVE_I8 || type == QA_NATIVE_U8 ? 1 :
        type == QA_NATIVE_I16 || type == QA_NATIVE_U16 ? 2 :
        type == QA_NATIVE_ADDRESS ? owner->target.pointer_bytes :
        type == QA_NATIVE_I32 || type == QA_NATIVE_U32 || type == QA_NATIVE_F32 ? 4 : 8;
    return (guest_abi_layout){.kind = type, .bytes = bytes, .alignment = bytes ? bytes : 1};
}

bool windows_invoke(guest_windows *owner, uint64_t target, const qa_native_value_type *types,
    size_t count, qa_native_value_type result_type, const qa_native_value *arguments,
    bool system, qa_native_value *result, qa_error *error)
{
    if (!owner || !owner->constructed || owner->detached || owner->publication_pending || owner->retired || owner->retiring || count > 16 || !target || !owner->return_trap)
        return guest_fail(error, QA_ERROR_ARGUMENT, target, "Windows invocation requires attached process, target and actual return trap");
    guest_abi_layout parameters[16];
    for (size_t i = 0; i < count; ++i) parameters[i] = layout(owner, types[i]);
    guest_abi_signature signature = {.abi = owner->target.abi,
        .convention = system && owner->target.pointer_bytes == 4 ? GUEST_ABI_STDCALL : GUEST_ABI_DEFAULT,
        .parameters = parameters, .parameter_count = count, .result = layout(owner, result_type)};
    guest_abi_plan *plan = NULL;
    if (!guest_abi_plan_create(&signature, NULL, 0, &plan, error)) return false;
    ++owner->busy;
    bool okay = owner->execution == QA_NATIVE_GUEST_HOST_X86_64 ?
        guest_abi_invoke_native(plan, owner->guest, target, owner->return_trap,
            arguments, count, result, error) :
        guest_abi_invoke(plan, owner->guest, target, owner->return_trap,
            arguments, count, result, owner->instruction_budget, error);
    --owner->busy; guest_abi_plan_destroy(plan); return okay;
}

static bool executable(guest_windows *owner, uint64_t target, qa_error *error)
{
    for (size_t i = 0; i < qa_native_guest_mapping_count(owner->guest); ++i) {
        qa_native_guest_mapping mapping;
        if (!qa_native_guest_mapping_at(owner->guest, i, &mapping, error)) return false;
        if (target >= mapping.base && target - mapping.base < mapping.bytes)
            return (mapping.permissions & QA_NATIVE_GUEST_EXECUTE) != 0 ||
                guest_fail(error, QA_ERROR_ARGUMENT, target, "Windows indirect target has no execute permission");
    }
    return guest_fail(error, QA_ERROR_ARGUMENT, target, "Windows indirect target is unmapped");
}

static bool service_invoke(void *context, qa_native_guest *guest,
    const qa_native_value *args, size_t count, qa_native_value *result, qa_error *error)
{
    windows_service *service = context; guest_windows *owner = service->owner;
    if (!owner || !owner->constructed || owner->guest != guest || owner->detached || owner->publication_pending || owner->retired || owner->retiring)
        return guest_fail(error, QA_ERROR_ARGUMENT, service->function.id, "Windows import belongs to a different process");
    ++owner->busy;
    bool okay;
    if (service->family == 1) okay = windows_kernel_invoke(service, args, count, result, error);
    else if (service->family == 2) okay = windows_crt_invoke(service, args, count, result, error);
    else if (service->family == 3) okay = windows_msvc_invoke(service, args, count, result, error);
    else if (service->family == 4) {
        uint64_t target; qa_native_guest_cpu cpu;
        okay = service->operation != 2 || owner->target.pointer_bytes == 8;
        if (!okay) guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "i386 CFG dispatch convention is not implemented");
        target = 0;
        if (okay && service->operation == 2) target = args[0].as.address;
        else if (okay) { okay = qa_native_guest_cpu_read(guest, &cpu, error); target = cpu.registers[owner->target.pointer_bytes == 4 ? QA_NATIVE_RCX : QA_NATIVE_RAX]; }
        bool valid = false;
        if (okay) {
            for (size_t i = 0; i < owner->cfg_count; ++i) if (owner->cfg_targets[i] == target) valid = true;
            for (size_t i = 0; i < guest->callback_count; ++i)
                if (guest->callbacks[i].address == target) valid = true;
            if (okay && !valid) okay = guest_fail(error, QA_ERROR_UNSUPPORTED, target, "invalid Windows control-flow-guard target");
            if (okay) okay = executable(owner, target, error);
        }
        result->type = QA_NATIVE_VOID;
    } else {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Unsupported Windows guest import %s!%s", service->library, service->name);
        okay = false;
    }
    --owner->busy; return okay;
}

static bool trap(guest_windows *owner, uint64_t *out, qa_error *error)
{
    if (owner->trap_cursor == owner->trap_end) {
        uint64_t base;
        if (!windows_storage(owner, QA_NATIVE_GUEST_PAGE, &base, error) ||
            !qa_native_guest_free(owner->guest, base, error) ||
            !guest_runtime_imports_traps(owner->imports, base, QA_NATIVE_GUEST_PAGE, error)) return false;
        owner->trap_cursor = base; owner->trap_end = base + QA_NATIVE_GUEST_PAGE;
    }
    *out = owner->trap_cursor; owner->trap_cursor += 16; return true;
}

static windows_library *library_record(guest_windows *owner, const char *name)
{
    for (size_t i = 0; i < owner->library_count; ++i) if (!strcmp(owner->libraries[i].name, name)) return owner->libraries + i;
    return NULL;
}

static bool ensure_library(guest_windows *owner, const char *name, qa_error *error)
{
    if (library_record(owner, name)) return true;
    if (!guest_grow((void **)&owner->libraries, &owner->library_capacity,
        owner->library_count + 1, sizeof(*owner->libraries), error)) return false;
    char *text = copy_text(name, error); uint64_t handle;
    if (!text || !windows_storage(owner, 16, &handle, error)) { free(text); return false; }
    owner->libraries[owner->library_count++] = (windows_library){text, handle, 0}; return true;
}

bool windows_service_add(guest_windows *owner, uint64_t id, uint8_t family, uint32_t operation,
    const char *library, const char *name, const qa_native_value_type *types, size_t count,
    qa_native_value_type result, guest_abi_convention convention, bool bind, qa_error *error)
{
    if (id < GUEST_WINDOWS_CALLBACK_MINIMUM || count > 16)
        return guest_fail(error, QA_ERROR_ARGUMENT, id, "Windows descriptor differs from its owned callback namespace or argument storage");
    for (size_t i = 0; i < owner->service_count; ++i)
        if (owner->services[i]->function.id == id) return guest_fail(error, QA_ERROR_ARGUMENT, id, "Windows service identity repeats");
    windows_service *service = calloc(1, sizeof(*service));
    if (!service) return guest_fail(error, QA_ERROR_MEMORY, id, "allocating Windows import descriptor");
    service->owner = owner; service->family = family; service->operation = operation; service->supported = family != 0;
    service->library = canonical(library, error); service->name = copy_text(name, error);
    if (!service->library || !service->name || !guest_grow((void **)&owner->services,
        &owner->service_capacity, owner->service_count + 1, sizeof(*owner->services), error)) {
        free(service->library); free(service->name); free(service); return false;
    }
    for (size_t i = 0; i < count; ++i) service->parameters[i] = layout(owner, types[i]);
    service->function = (guest_runtime_function){.id = id,
        .signature = {.abi = owner->target.abi, .convention = convention,
            .parameters = service->parameters, .parameter_count = count, .result = layout(owner, result)},
        .invoke = service_invoke, .context = service};
    owner->services[owner->service_count++] = service;
    if (!bind) return true;
    guest_runtime_import_key key = {.library = service->library, .kind = GUEST_RUNTIME_SYMBOL_NAME, .name = service->name};
    return trap(owner, &service->function.address, error) && ensure_library(owner, service->library, error) &&
        guest_runtime_imports_function(owner->imports, &key, &service->function, error);
}

const windows_image_record *windows_image_at(const guest_windows *owner, uint64_t id)
{
    for (size_t i = 0; i < owner->image_count; ++i) if (owner->images[i].id == id) return owner->images + i;
    return NULL;
}

bool windows_library_handle(guest_windows *owner, const char *library, uint64_t *out, qa_error *error)
{
    *out = 0; char *name = canonical(library, error);
    if (!name) return false;
    bool okay = true;
    for (size_t i = 0; i < owner->image_count; ++i) {
        char *image_name = canonical(owner->images[i].path, error);
        if (!image_name) { okay = false; break; }
        bool match = !strcmp(image_name, name); free(image_name);
        if (match) { *out = owner->images[i].base; break; }
    }
    if (okay && !*out) {
        bool supported = false;
        for (size_t i = 0; i < guest_runtime_imports_count(owner->imports); ++i) {
            guest_runtime_import_view view;
            if (!guest_runtime_imports_at(owner->imports, i, &view, error)) { okay = false; break; }
            if (strcmp(view.key.library, name)) continue;
            if (view.kind == GUEST_RUNTIME_IMPORT_DATA) supported = true;
            for (size_t j = 0; j < owner->service_count; ++j)
                if (owner->services[j]->function.id == view.id && owner->services[j]->supported) supported = true;
        }
        windows_library *record = library_record(owner, name);
        if (supported && record) *out = record->handle;
    }
    free(name); return okay;
}

const char *windows_library_name(const guest_windows *owner, uint64_t handle)
{
    for (size_t i = 0; i < owner->library_count; ++i) if (owner->libraries[i].handle == handle) return owner->libraries[i].name;
    for (size_t i = 0; i < owner->image_count; ++i) if (owner->images[i].base == handle) return owner->images[i].path;
    return NULL;
}

bool windows_load_library(guest_windows *owner, const char *library, uint64_t *out, qa_error *error)
{
    if (!windows_library_handle(owner, library, out, error)) return false;
    if (!*out) return windows_last_error(owner, 126, error);
    for (size_t i = 0; i < owner->library_count; ++i) if (owner->libraries[i].handle == *out) {
        if (owner->libraries[i].references == UINT64_MAX) return guest_fail(error, QA_ERROR_ARGUMENT, *out, "Windows DLL reference count overflows");
        ++owner->libraries[i].references; return true;
    }
    for (size_t i = 0; i < owner->image_count; ++i) if (owner->images[i].base == *out) {
        if (owner->images[i].references == UINT64_MAX) return guest_fail(error, QA_ERROR_ARGUMENT, *out, "Windows DLL reference count overflows");
        ++owner->images[i].references; return true;
    }
    return guest_fail(error, QA_ERROR_ARGUMENT, *out, "Windows library handle has no actual reference owner");
}

bool windows_free_library(guest_windows *owner, uint64_t handle, bool *out, qa_error *error)
{
    *out = false;
    for (size_t i = 0; i < owner->library_count; ++i) if (owner->libraries[i].handle == handle && owner->libraries[i].references) {
        --owner->libraries[i].references; *out = true; return true;
    }
    for (size_t i = 0; i < owner->image_count; ++i) if (owner->images[i].base == handle && owner->images[i].references) {
        --owner->images[i].references; *out = true; return true;
    }
    return windows_last_error(owner, 6, error);
}

bool guest_windows_resolve(guest_windows *owner, const char *library, const char *symbol,
    uint64_t *out, qa_error *error)
{
    if (!owner || !library || !symbol || !out)
        return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows symbol resolution requires its actual owner and key");
    *out = 0; char *name = canonical(library, error), *entry = copy_text(symbol, error);
    char *seen[128] = {0}; size_t seen_count = 0; bool okay = name && entry;
    for (size_t depth = 0; okay && depth < 128; ++depth) {
        size_t bytes = strlen(name) + strlen(entry) + 2;
        char *key_text = malloc(bytes);
        if (!key_text) { okay = guest_fail(error, QA_ERROR_MEMORY, 0, "retaining Windows forwarder path"); break; }
        snprintf(key_text, bytes, "%s!%s", name, entry);
        bool repeats = false;
        for (size_t i = 0; i < seen_count; ++i) if (!strcmp(seen[i], key_text)) repeats = true;
        if (repeats) { free(key_text); break; }
        seen[seen_count++] = key_text;
        const guest_pe_view *image = NULL;
        for (size_t i = 0; i < owner->image_count; ++i) {
            char *candidate = canonical(owner->images[i].path, error);
            if (!candidate) { okay = false; break; }
            bool match = !strcmp(candidate, name); free(candidate);
            if (match) { image = guest_pe_describe(owner->images[i].image); break; }
        }
        if (!okay) break;
        if (!image) {
            guest_runtime_import_key key = {.library = name, .kind = GUEST_RUNTIME_SYMBOL_NAME, .name = entry};
            guest_runtime_import_view imported; qa_error absent = {0};
            if (guest_runtime_imports_find(owner->imports, &key, &imported, &absent)) {
                bool supported = imported.kind == GUEST_RUNTIME_IMPORT_DATA;
                for (size_t i = 0; i < owner->service_count; ++i)
                    if (owner->services[i]->function.id == imported.id && owner->services[i]->supported) supported = true;
                if (supported) *out = imported.address;
            } else if (absent.code != QA_ERROR_NOT_FOUND) { if (error) *error = absent; okay = false; }
            break;
        }
        const guest_pe_export *exported = NULL;
        uint64_t ordinal = 0; bool by_ordinal = entry[0] == '#';
        if (by_ordinal) {
            const char *at = entry + 1;
            if (!*at) break;
            for (; *at; ++at) { if (*at < '0' || *at > '9' || ordinal > (UINT32_MAX - (uint32_t)(*at - '0')) / 10) break; ordinal = ordinal * 10 + (uint32_t)(*at - '0'); }
            if (*at) break;
        }
        for (size_t i = 0; i < image->export_count; ++i)
            if (by_ordinal ? image->exports[i].ordinal == ordinal : image->exports[i].name && !strcmp(image->exports[i].name, entry)) { exported = image->exports + i; break; }
        if (!exported) break;
        if (!exported->forwarder) { *out = image->base + exported->rva; break; }
        const char *dot = strrchr(exported->forwarder, '.');
        if (!dot) break;
        size_t length = (size_t)(dot - exported->forwarder);
        char *forward_library = malloc(length + 1);
        if (!forward_library) { okay = guest_fail(error, QA_ERROR_MEMORY, 0, "retaining forwarded Windows library"); break; }
        memcpy(forward_library, exported->forwarder, length); forward_library[length] = 0;
        char *next_name = canonical(forward_library, error), *next_entry = copy_text(dot + 1, error);
        free(forward_library); free(name); free(entry); name = next_name; entry = next_entry; okay = name && entry;
    }
    for (size_t i = 0; i < seen_count; ++i) free(seen[i]);
    free(name); free(entry); return okay;
}

bool guest_windows_idle(const guest_windows *owner)
{
    return owner && owner->constructed && !owner->busy && !owner->detached && !owner->publication_pending && !owner->retired && !owner->retiring && qa_native_guest_idle(owner->guest) &&
        guest_runtime_imports_idle(owner->imports) && guest_runtime_resources_idle(owner->resources);
}

guest_runtime_imports *guest_windows_imports(guest_windows *owner) { return owner ? owner->imports : NULL; }
guest_runtime_resources *guest_windows_resources(guest_windows *owner) { return owner ? owner->resources : NULL; }

static bool coverage_row(const guest_windows *owner, const guest_runtime_import_view *view)
{
    if (view->kind == GUEST_RUNTIME_IMPORT_DATA) return false;
    if (view->reached) return true;
    char ordinal[24];
    if (view->key.kind == GUEST_RUNTIME_SYMBOL_ORDINAL) snprintf(ordinal,sizeof(ordinal),"#%u",view->key.ordinal);
    const char *name = view->key.kind == GUEST_RUNTIME_SYMBOL_ORDINAL ? ordinal : view->key.name;
    size_t library_bytes = strlen(view->key.library), name_bytes = strlen(name);
    for (size_t i = 0; i < owner->requested_count; ++i) {
        const char *request = owner->requested[i];
        if (strlen(request) == library_bytes + name_bytes + 1 &&
            !memcmp(request,view->key.library,library_bytes) && request[library_bytes] == '!' &&
            !memcmp(request + library_bytes + 1,name,name_bytes)) return true;
    }
    return false;
}

bool guest_windows_coverage_count(const guest_windows *owner, size_t *out, qa_error *error)
{
    if (!owner || !owner->imports || !out) return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows coverage requires its retained import owner");
    size_t count = 0;
    for (size_t i = 0; i < guest_runtime_imports_count(owner->imports); ++i) {
        guest_runtime_import_view view;
        if (!guest_runtime_imports_at(owner->imports,i,&view,error)) return false;
        if (coverage_row(owner,&view)) ++count;
    }
    *out = count; return true;
}

bool guest_windows_coverage_at(const guest_windows *owner, size_t index,
    guest_runtime_import_view *out, bool *supported, qa_error *error)
{
    if (!owner || !owner->imports || !out || !supported) return guest_fail(error,QA_ERROR_ARGUMENT,index,"Windows coverage requires actual outputs");
    for (size_t i = 0; i < guest_runtime_imports_count(owner->imports); ++i) {
        guest_runtime_import_view view;
        if (!guest_runtime_imports_at(owner->imports,i,&view,error)) return false;
        if (!coverage_row(owner,&view)) continue;
        if (index) { --index; continue; }
        *out = view; *supported = false;
        for (size_t j = 0; j < owner->service_count; ++j)
            if (owner->services[j]->function.id == view.id) *supported = owner->services[j]->supported;
        return true;
    }
    return guest_fail(error,QA_ERROR_NOT_FOUND,index,"Windows coverage ordinal is absent");
}

static void dispose(guest_windows *owner)
{
    for (size_t i = 0; i < owner->service_count; ++i) {
        free(owner->services[i]->library); free(owner->services[i]->name); free(owner->services[i]);
    }
    if (owner->libraries) for (size_t i = 0; i < owner->library_count; ++i) free(owner->libraries[i].name);
    if (owner->images) for (size_t i = 0; i < owner->image_count; ++i) free(owner->images[i].path);
    if (owner->requested) for (size_t i = 0; i < owner->requested_count; ++i) free(owner->requested[i]);
    free(owner->prepared_ids); free(owner->initialized_ids);
    free(owner->requested); free(owner->services); free(owner->libraries); free(owner->images); free(owner->allocations); free(owner->cfg_targets);
    windows_kernel_dispose(owner->kernel);
    if (owner->crt) {
        for (size_t i = 0; i < owner->crt->pending_file_count; ++i) {
            free(owner->crt->pending_files[i]->name); free(owner->crt->pending_files[i]);
        }
        free(owner->crt->pending_files); free(owner->crt->files); free(owner->crt);
    }
    free(owner->msvc); free(owner);
}

void guest_windows_abandon(guest_windows **pointer)
{
    if (!pointer || !*pointer) return;
    guest_windows *owner = *pointer;
    if (!owner->files_closed && !owner->detached && !owner->publication_pending) return;
    guest_runtime_imports_abandon(&owner->imports); guest_runtime_resources_abandon(&owner->resources);
    dispose(owner); *pointer = NULL;
}

bool guest_windows_close_files(guest_windows *owner, qa_error *error)
{
    if (!owner || owner->busy || owner->detached || owner->publication_pending || !owner->guest ||
        owner->guest->run || owner->guest->callback_depth || owner->guest->publication_depth ||
        owner->guest->stepping || owner->guest->restoring || owner->guest->faulting)
        return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows file retirement requires its genuinely stopped retained lower owner");
    owner->retiring=true;
    if (owner->files_closed) return true;
    /* Flush and close capabilities may call back into their retained runtime.
     * Keep retirement owned until the complete close attempt has returned. */
    ++owner->busy;
    bool closed = windows_stdio_close_files(owner,error) && windows_kernel_close_pending(owner,error) &&
        guest_runtime_resources_destroy(&owner->resources,error);
    --owner->busy;
    if (!closed) return false;
    owner->files_closed=true; return true;
}

bool guest_windows_destroy(guest_windows **pointer, qa_error *error)
{
    if (!pointer || !*pointer) return true;
    guest_windows *owner = *pointer;
    if (owner->busy || owner->detached || !qa_native_guest_idle(owner->guest) ||
        (!owner->retiring && owner->imports && !guest_runtime_imports_idle(owner->imports)) ||
        (!owner->retiring && !owner->retired && owner->resources && !guest_runtime_resources_idle(owner->resources)))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows process retirement requires idle lower and child owners");
    if (!guest_windows_close_files(owner,error) || !guest_runtime_imports_destroy(&owner->imports,error)) return false;
    dispose(owner); *pointer = NULL; return true;
}

bool guest_windows_create(const guest_windows_options *options, guest_windows **out, qa_error *error)
{
    if (!options || !out || *out || !qa_native_guest_idle(options->guest) ||
        options->guest->options.image.target.os != QA_NATIVE_OS_WINDOWS || !options->primary_image || !options->capabilities.id ||
        !options->capabilities.entropy || !options->capabilities.milliseconds || !options->capabilities.performance ||
        !options->capabilities.calendar || options->capabilities.performance_frequency <= 0 ||
        !qa_native_windows_locale_profile_valid(&options->capabilities.locale) ||
        (options->capabilities.locale.source == 2) != (options->capabilities.compare_string != NULL) ||
        !call_policy(qa_native_guest_execution(options->guest), options->instruction_budget))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows construction requires the genuine process, clock, entropy and stack capabilities");
    guest_windows *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "allocating Windows process owner");
    owner->guest = options->guest; owner->target = options->guest->options.image.target;
    owner->execution = qa_native_guest_execution(options->guest);
    owner->primary_image = options->primary_image;
    owner->capabilities = options->capabilities; owner->capability_id = options->capabilities.id;
    owner->process_id = options->has_process_id ? options->process_id : 1;
    owner->thread_id = options->has_thread_id ? options->thread_id : 1;
    owner->instruction_budget = options->instruction_budget; owner->stack_base = options->stack_base; owner->stack_bytes = options->stack_bytes;
    owner->kernel = calloc(1, sizeof(*owner->kernel)); owner->crt = calloc(1, sizeof(*owner->crt)); owner->msvc = calloc(1, sizeof(*owner->msvc));
    *out = owner; /* Retain actual partial state for whole-guest failure cleanup. */
    if (!owner->kernel || !owner->crt || !owner->msvc) return guest_fail(error, QA_ERROR_MEMORY, 0, "allocating Windows service state");
    qa_native_guest_cpu cpu;
    if (!qa_native_guest_cpu_read(owner->guest, &cpu, error) || !owner->stack_base || !owner->stack_bytes ||
        owner->stack_bytes > UINT64_MAX - owner->stack_base || cpu.registers[QA_NATIVE_RSP] <= owner->stack_base ||
        cpu.registers[QA_NATIVE_RSP] - owner->stack_base > owner->stack_bytes)
        return guest_fail(error, QA_ERROR_ARGUMENT, owner->stack_base, "Windows thread requires its actual mapped stack");
    bool stack_found = false;
    for (size_t i = 0; i < qa_native_guest_mapping_count(owner->guest); ++i) {
        qa_native_guest_mapping mapping;
        if (!qa_native_guest_mapping_at(owner->guest, i, &mapping, error)) return false;
        if (mapping.base == owner->stack_base && mapping.bytes == owner->stack_bytes &&
            (mapping.permissions & (QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE)) == (QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE)) stack_found = true;
    }
    if (!stack_found) return guest_fail(error, QA_ERROR_ARGUMENT, owner->stack_base, "Windows stack extent differs from real lower mapping");
    size_t width = owner->target.pointer_bytes;
    if (!guest_runtime_imports_create(owner->guest, &owner->imports, error) ||
        !guest_runtime_resources_create(&owner->resources, error) || !windows_storage(owner, 0x2000, &owner->teb, error) ||
        !windows_storage(owner, 0x1000, &owner->peb, error) || !windows_storage(owner, width * 1024, &owner->static_tls, error) ||
        !windows_write(owner, owner->teb + (width == 4 ? 0x18 : 0x30), width, owner->teb, error) ||
        !windows_write(owner, owner->teb + (width == 4 ? 0x30 : 0x60), width, owner->peb, error) ||
        !windows_write(owner, owner->teb + (width == 4 ? 0x2c : 0x58), width, owner->static_tls, error) ||
        !windows_write(owner, owner->teb, width, UINT64_MAX, error) ||
        !windows_write(owner, owner->teb + (width == 4 ? 0x20 : 0x40), width, owner->process_id, error) ||
        !windows_write(owner, owner->teb + (width == 4 ? 0x24 : 0x48), width, owner->thread_id, error) ||
        !windows_write(owner, owner->teb + width, width, owner->stack_base + owner->stack_bytes, error) ||
        !windows_write(owner, owner->teb + width * 2, width, owner->stack_base, error) || !trap(owner, &owner->return_trap, error)) return false;
    cpu.segments[width == 4 ? 4 : 5].base = owner->teb;
    if (!qa_native_guest_cpu_write(owner->guest, &cpu, error) || !windows_kernel_initialize(owner, options, error) ||
        !windows_crt_initialize(owner, error) || !windows_kernel_descriptors(owner, true, error) ||
        !windows_crt_descriptors(owner, true, error) || !windows_msvc_descriptors(owner, true, error) ||
        !windows_msvc_initialize(owner, error)) return false;
    owner->constructed = true;
    return true;
}

static bool image_library(void *context, const char *library, const guest_pe *requesting,
    const guest_pe **out, qa_error *error)
{
    (void)requesting;
    guest_windows *owner = context; char *name = canonical(library, error);
    if (!name) return false;
    *out = NULL;
    for (size_t i = 0; i < owner->image_count; ++i) {
        char *candidate = canonical(owner->images[i].path, error);
        if (!candidate) { free(name); return false; }
        bool matches = !strcmp(candidate, name); free(candidate);
        if (matches) { *out = owner->images[i].image; break; }
    }
    free(name); return true;
}

static bool resolve_import(void *context, const guest_pe_view *requesting,
    const guest_pe_import *imported, guest_pe_resolution *out, qa_error *error)
{
    guest_windows *owner = context; char *library = canonical(imported->library, error);
    if (!library) return false;
    char ordinal[24];
    if (imported->by_ordinal) snprintf(ordinal, sizeof(ordinal), "#%u", imported->ordinal);
    const char *name = imported->by_ordinal ? ordinal : imported->name;
    guest_runtime_import_key key = {.library = library,
        .kind = imported->by_ordinal ? GUEST_RUNTIME_SYMBOL_ORDINAL : GUEST_RUNTIME_SYMBOL_NAME,
        .name = imported->by_ordinal ? NULL : name, .ordinal = imported->ordinal};
    size_t request_bytes = strlen(library) + strlen(name) + 2;
    char *request = malloc(request_bytes);
    if (!request) { free(library); return guest_fail(error, QA_ERROR_MEMORY, 0, "retaining Windows import request"); }
    snprintf(request, request_bytes, "%s!%s", library, name);
    bool requested = false;
    for (size_t i = 0; i < owner->requested_count; ++i) if (!strcmp(owner->requested[i], request)) requested = true;
    if (!requested) {
        if (!guest_grow((void **)&owner->requested, &owner->requested_capacity,
            owner->requested_count + 1, sizeof(*owner->requested), error)) { free(request); free(library); return false; }
        owner->requested[owner->requested_count++] = request;
    } else free(request);
    guest_runtime_import_view view; qa_error missing = {0};
    bool found = guest_runtime_imports_find(owner->imports, &key, &view, &missing);
    bool okay = true; uint64_t address = 0;
    if (!found && missing.code != QA_ERROR_NOT_FOUND) { if (error) *error = missing; okay = false; }
    if (okay && found && view.kind == GUEST_RUNTIME_IMPORT_DATA) address = view.address;
    const guest_pe *image = NULL;
    if (okay && !address) okay = image_library(owner, library, NULL, &image, error);
    if (okay && !address && image && guest_pe_describe(image) != requesting) {
        const guest_pe *resolved = NULL;
        okay = guest_pe_resolve_export(image, owner->guest, imported->name, imported->ordinal,
            imported->by_ordinal, image_library, owner, &resolved, &address, error);
    }
    if (okay && !address && found) address = view.address;
    if (okay && !address) {
        uint64_t slot;
        if (!trap(owner, &slot, error) || !ensure_library(owner, library, error)) okay = false;
        else {
            /* Missing strong imports have no inferred argument descriptor. */
            size_t registry_index = guest_runtime_imports_count(owner->imports);
            uint64_t base = UINT64_C(0x57494e7f00000000);
            okay = registry_index <= UINT64_MAX - base || guest_fail(error,QA_ERROR_MEMORY,slot,"Windows unresolved import identity overflows");
            if (okay) okay = guest_runtime_imports_unresolved(owner->imports, &key, base + registry_index, slot, error);
            if (okay) address = slot;
        }
    }
    if (okay) {
        *out = (guest_pe_resolution){.address = address};
        /* Eager binding is not a delay-helper DLL notification. */
    }
    free(library); return okay;
}

static bool patch_pointer(guest_windows *owner, uint64_t slot, uint64_t value, qa_error *error)
{
    size_t width = owner->target.pointer_bytes;
    if (slot > UINT64_MAX - width || !guest_range(owner->guest,slot,width,QA_NATIVE_GUEST_READ,error)) return false;
    uint64_t pages[2] = {slot & ~UINT64_C(4095),(slot + width - 1) & ~UINT64_C(4095)};
    uint32_t permissions[2]; size_t count = pages[0] == pages[1] ? 1 : 2;
    for (size_t i = 0; i < count; ++i) {
        qa_native_guest_mapping *mapping = guest_mapping(owner->guest,pages[i]);
        if (!mapping) return guest_fail(error,QA_ERROR_FORMAT,pages[i],"unmapped Windows CFG slot page");
        permissions[i] = mapping->permissions;
    }
    bool changed = false, okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        okay = qa_native_guest_protect_range(owner->guest,pages[i],4096,QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE,error);
        if (okay) changed = true;
    }
    if (okay) okay = windows_write(owner,slot,width,value,error);
    for (size_t i = 0; okay && i < count; ++i)
        okay = qa_native_guest_protect_range(owner->guest,pages[i],4096,permissions[i],error);
    /* Failed mutation retains the exact failed process for whole destruction. */
    if (!okay && changed) owner->guest->failed = true;
    return okay;
}

static bool prepare_cfg(guest_windows *owner, const guest_pe_view *image, qa_error *error)
{
    if (!image->load_configuration.size || !(image->dll_characteristics & 0x4000) || !(image->guard_flags & 0x100)) return true;
    size_t width = owner->target.pointer_bytes, offset = width == 4 ? 80 : 128;
    if (image->load_configuration.size < offset + width * 2) return guest_fail(error, QA_ERROR_FORMAT, image->base, "truncated Windows CFG load configuration");
    uint64_t table, count; uint64_t config = image->base + image->directories[10].rva;
    if (!windows_read(owner, config + offset, width, &table, error) || !windows_read(owner, config + offset + width, width, &count, error)) return false;
    uint32_t extra = image->guard_flags >> 28, stride = extra + 4;
    if (count > image->image.image_bytes / stride || (count && !table)) return guest_fail(error, QA_ERROR_FORMAT, table, "invalid Windows CFG function table");
    uint32_t previous = 0; bool seen = false;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t rva, flags = 0;
        if (!windows_read(owner, table + i * stride, 4, &rva, error) ||
            (extra && !windows_read(owner, table + i * stride + 4, 1, &flags, error))) return false;
        if ((seen && rva <= previous) || rva >= image->image.image_bytes || (flags & ~UINT64_C(3)))
            return guest_fail(error, QA_ERROR_FORMAT, table + i * stride, "invalid Windows CFG target metadata");
        for (uint32_t j = 1; j < extra; ++j) {
            uint64_t byte;
            if (!windows_read(owner, table + i * stride + 4 + j, 1, &byte, error)) return false;
            if (byte) return guest_fail(error, QA_ERROR_UNSUPPORTED, table + i * stride, "unsupported Windows CFG extra metadata");
        }
        previous = (uint32_t)rva; seen = true;
        if ((flags & 1) || ((flags & 2) && (image->guard_flags & 0x8000))) continue;
        uint64_t target = image->base + rva; bool exists = false;
        if (!executable(owner, target, error)) return false;
        for (size_t j = 0; j < owner->cfg_count; ++j) if (owner->cfg_targets[j] == target) exists = true;
        if (!exists) {
            if (!guest_grow((void **)&owner->cfg_targets, &owner->cfg_capacity, owner->cfg_count + 1, sizeof(*owner->cfg_targets), error)) return false;
            owner->cfg_targets[owner->cfg_count++] = target;
        }
    }
    if (!owner->cfg_check) {
        if (!windows_service_add(owner, UINT64_C(0x57494e0400000001), 4, 1, "quake-runtime.dll", "guard-check", NULL, 0,
            QA_NATIVE_VOID, width == 4 ? GUEST_ABI_STDCALL : GUEST_ABI_DEFAULT, true, error)) return false;
        owner->cfg_check = owner->services[owner->service_count - 1]->function.address;
        qa_native_value_type pointer_type = QA_NATIVE_ADDRESS;
        if (!windows_service_add(owner, UINT64_C(0x57494e0400000002), 4, 2, "quake-runtime.dll", "guard-dispatch-check",
            width == 8 ? &pointer_type : NULL, width == 8 ? 1 : 0, QA_NATIVE_VOID,
            width == 4 ? GUEST_ABI_STDCALL : GUEST_ABI_DEFAULT, true, error)) return false;
        uint64_t checker = owner->services[owner->service_count - 1]->function.address;
        if (width == 4) owner->cfg_dispatch = checker;
        else {
            uint8_t code[sizeof(cfg_dispatch_code)]; memcpy(code,cfg_dispatch_code,sizeof(code));
            qa_store_u64le(code + 16, checker);
            if (!windows_storage(owner, sizeof(code), &owner->cfg_dispatch, error) ||
                !qa_native_guest_write(owner->guest, owner->cfg_dispatch, (qa_bytes){code, sizeof(code)}, error) ||
                !qa_native_guest_protect_range(owner->guest, owner->cfg_dispatch, 4096, QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_EXECUTE, error)) return false;
        }
    }
    return (!image->guard_check || patch_pointer(owner, image->base + image->guard_check, owner->cfg_check, error)) &&
        (!image->guard_dispatch || patch_pointer(owner, image->base + image->guard_dispatch, owner->cfg_dispatch, error));
}

static bool lifecycle_failure(guest_windows *owner)
{
    owner->guest->failed = true;
    return false;
}

static bool image_identity(const qa_native_image_info *a, const qa_native_image_info *b)
{
    return a->format == b->format && a->target.os == b->target.os &&
        a->target.arch == b->target.arch && a->target.abi == b->target.abi &&
        a->target.pointer_bytes == b->target.pointer_bytes &&
        a->preferred_base == b->preferred_base && a->image_bytes == b->image_bytes;
}

bool guest_windows_inventory(guest_windows *owner, const guest_windows_image *inputs,
    size_t count, qa_error *error)
{
    if (!guest_windows_idle(owner) || owner->image_count || !inputs || !count ||
        count > SIZE_MAX / sizeof(*owner->images))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows inventory requires a fresh idle process and complete actual graph");
    windows_image_record *records = calloc(count, sizeof(*records));
    if (!records) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning complete Windows image inventory");
    bool okay = true, primary = false;
    for (size_t i = 0; okay && i < count; ++i) {
        const guest_windows_image *input = inputs + i;
        const guest_pe_view *image = guest_pe_describe(input->image);
        if (!input->id || !image || !input->path || !*input->path ||
            image->image.target.os != owner->target.os || image->image.target.arch != owner->target.arch ||
            image->image.target.abi != owner->target.abi || image->image.target.pointer_bytes != owner->target.pointer_bytes) {
            okay = guest_fail(error, QA_ERROR_ARGUMENT, i, "Windows inventory image differs from its actual process target");
            break;
        }
        for (size_t j = 0; j < i; ++j)
            if (records[j].id == input->id || records[j].base == image->base) {
                okay = guest_fail(error, QA_ERROR_ARGUMENT, input->id, "Windows inventory repeats an image identity or physical base");
                break;
            }
        if (!okay) break;
        records[i] = (windows_image_record){.id = input->id, .base = image->base,
            .preferred_base = image->image.preferred_base, .image_bytes = image->image.image_bytes, .image = input->image};
        records[i].path = copy_text(input->path, error);
        okay = records[i].path != NULL;
        if (input->id == owner->primary_image) {
            primary = image_identity(&image->image, &owner->guest->options.image);
            if (!primary) okay = guest_fail(error, QA_ERROR_ARGUMENT, input->id, "Windows primary artifact differs from the actual lower image witness");
        }
    }
    if (okay && !primary) okay = guest_fail(error, QA_ERROR_ARGUMENT, owner->primary_image, "Windows inventory has no actual primary image");
    if (!okay) {
        for (size_t i = 0; i < count; ++i) free(records[i].path);
        free(records); return false;
    }
    owner->images = records; owner->image_count = owner->image_capacity = count;
    return true;
}

bool guest_windows_prepare(guest_windows *owner, const guest_windows_image *input, qa_error *error)
{
    if (!guest_windows_idle(owner) || !input || !input->id || !input->image || !input->path)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows prepare requires idle process and genuine attached image identity");
    const guest_pe_view *image = guest_pe_describe(input->image);
    if (image->image.target.os != owner->target.os || image->image.target.arch != owner->target.arch ||
        image->image.target.abi != owner->target.abi || image->image.target.pointer_bytes != owner->target.pointer_bytes)
        return guest_fail(error, QA_ERROR_ARGUMENT, input->id, "Windows image ABI differs from its process");
    windows_image_record *record = NULL;
    for (size_t i = 0; i < owner->image_count; ++i) {
        if (owner->images[i].id == input->id) record = owner->images + i;
        else if (owner->images[i].base == image->base) return guest_fail(error, QA_ERROR_ARGUMENT, image->base, "Windows images share a physical base");
    }
    if (!record) return guest_fail(error, QA_ERROR_ARGUMENT, input->id, "Windows image was not admitted in its complete inventory");
    if (record->image != input->image || strcmp(record->path, input->path)) return guest_fail(error, QA_ERROR_ARGUMENT, input->id, "Windows image identity was replaced");
    if (record->prepared) return true;
    if (!guest_pe_bind_imports(input->image, owner->guest, resolve_import, owner, error)) return lifecycle_failure(owner);
    if (image->tls.present && !record->tls_block) {
        if (owner->static_tls_count >= 1024 || image->tls.initialized.size > SIZE_MAX - image->tls.zero_bytes) {
            guest_fail(error, QA_ERROR_FORMAT, input->id, "Windows static TLS index or template extent is invalid");
            return lifecycle_failure(owner);
        }
        size_t bytes = image->tls.initialized.size + image->tls.zero_bytes;
        size_t alignment = image->tls.alignment < 16 ? 16 : image->tls.alignment;
        if (!qa_native_guest_allocate_aligned(owner->guest, bytes ? bytes : 1, alignment,
            QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, WINDOWS_STORAGE_TAG, &record->tls_block, error) ||
            !qa_native_guest_write(owner->guest, record->tls_block, image->tls.initialized, error) ||
            !windows_write(owner, image->base + image->tls.index, 4, owner->static_tls_count, error) ||
            !windows_write(owner, owner->static_tls + (uint64_t)owner->static_tls_count * owner->target.pointer_bytes,
                owner->target.pointer_bytes, record->tls_block, error)) return lifecycle_failure(owner);
        record->tls_index = owner->static_tls_count++;
    } else if (image->tls.present) {
        size_t bytes = image->tls.initialized.size + image->tls.zero_bytes;
        if (!windows_zero(owner, record->tls_block, bytes, error) ||
            !qa_native_guest_write(owner->guest, record->tls_block, image->tls.initialized, error) ||
            !windows_write(owner, image->base + image->tls.index, 4, record->tls_index, error) ||
            !windows_write(owner, owner->static_tls + (uint64_t)record->tls_index * owner->target.pointer_bytes,
                owner->target.pointer_bytes, record->tls_block, error)) return lifecycle_failure(owner);
    }
    if (!prepare_cfg(owner, image, error)) return lifecycle_failure(owner);
    if (!guest_grow((void **)&owner->prepared_ids, &owner->prepared_capacity,
        owner->prepared_count + 1, sizeof(*owner->prepared_ids), error)) return lifecycle_failure(owner);
    owner->prepared_ids[owner->prepared_count++] = record->id;
    record->prepared = true; return true;
}

bool guest_windows_reload_begin(guest_windows *owner, uint64_t id, qa_error *error)
{
    if (!guest_windows_idle(owner))
        return guest_fail(error, QA_ERROR_ARGUMENT, id, "Windows module reload requires its returned process");
    windows_image_record *record = (windows_image_record *)windows_image_at(owner, id);
    if (!record || !record->prepared || record->initialized)
        return guest_fail(error, QA_ERROR_ARGUMENT, id, "Windows module reload requires its finalized attached DLL");
    const guest_pe_view *image = guest_pe_describe(record->image);
    if (!windows_crt_finalize_image(owner, image->base, image->image.image_bytes, error)) return lifecycle_failure(owner);
    for (size_t i = owner->prepared_count; i; --i)
        if (owner->prepared_ids[i - 1] == id) {
            memmove(owner->prepared_ids + i - 1, owner->prepared_ids + i,
                (owner->prepared_count - i) * sizeof(*owner->prepared_ids));
            --owner->prepared_count;
        }
    for (size_t i = owner->cfg_count; i; --i)
        if (owner->cfg_targets[i - 1] >= image->base &&
            owner->cfg_targets[i - 1] - image->base < image->image.image_bytes) {
            memmove(owner->cfg_targets + i - 1, owner->cfg_targets + i,
                (owner->cfg_count - i) * sizeof(*owner->cfg_targets));
            --owner->cfg_count;
        }
    record->prepared = false;
    return true;
}

static bool notification(guest_windows *owner, const windows_image_record *record,
    uint64_t target, uint32_t reason, qa_native_value_type type, qa_native_value *result, qa_error *error)
{
    const qa_native_value_type types[] = {QA_NATIVE_ADDRESS, QA_NATIVE_U32, QA_NATIVE_ADDRESS};
    const qa_native_value args[] = {{.type = QA_NATIVE_ADDRESS, .as.address = record->base},
        {.type = QA_NATIVE_U32, .as.u32 = reason}, {.type = QA_NATIVE_ADDRESS, .as.address = 0}};
    return windows_invoke(owner, target, types, 3, type, args, true, result, error);
}

bool guest_windows_initialize(guest_windows *owner, uint64_t id, size_t budget, qa_error *error)
{
    if (!guest_windows_idle(owner) || !call_policy(owner->execution, budget))
        return guest_fail(error, QA_ERROR_ARGUMENT, id, "Windows DLL initialization requires its actual idle execution policy");
    windows_image_record *record = (windows_image_record *)windows_image_at(owner, id);
    if (!record) return guest_fail(error, QA_ERROR_NOT_FOUND, id, "Windows DLL was not attached");
    if (record->initialized) return true;
    if (!record->prepared) {
        guest_windows_image input = {record->id, record->image, record->path};
        if (!guest_windows_prepare(owner, &input, error)) return false;
    }
    if (!guest_grow((void **)&owner->initialized_ids, &owner->initialized_capacity,
        owner->initialized_count + 1, sizeof(*owner->initialized_ids), error)) return false;
    owner->instruction_budget = budget; const guest_pe_view *image = guest_pe_describe(record->image); qa_native_value result;
    for (size_t i = 0; i < image->tls.callback_count; ++i)
        if (!notification(owner, record, image->tls.callbacks[i], 1, QA_NATIVE_VOID, &result, error)) return lifecycle_failure(owner);
    if (image->entry) {
        if (!notification(owner, record, image->base + image->entry, 1, QA_NATIVE_I32, &result, error)) return lifecycle_failure(owner);
        if (!result.as.i32) {
            guest_fail(error, QA_ERROR_ARGUMENT, id, "Windows DLL_PROCESS_ATTACH returned false");
            return lifecycle_failure(owner);
        }
    }
    owner->initialized_ids[owner->initialized_count++] = record->id;
    record->initialized = true; return true;
}

bool guest_windows_finalize(guest_windows *owner, uint64_t id, size_t budget, qa_error *error)
{
    if (!guest_windows_idle(owner) || !call_policy(owner->execution, budget))
        return guest_fail(error, QA_ERROR_ARGUMENT, id, "Windows DLL finalization requires its actual idle execution policy");
    windows_image_record *record = (windows_image_record *)windows_image_at(owner, id);
    if (!record) return guest_fail(error, QA_ERROR_NOT_FOUND, id, "Windows DLL was not attached");
    if (!record->initialized) return true;
    owner->instruction_budget = budget; const guest_pe_view *image = guest_pe_describe(record->image); qa_native_value result;
    if (image->entry && !notification(owner, record, image->base + image->entry, 0, QA_NATIVE_I32, &result, error)) return lifecycle_failure(owner);
    for (size_t i = 0; i < image->tls.callback_count; ++i)
        if (!notification(owner, record, image->tls.callbacks[i], 0, QA_NATIVE_VOID, &result, error)) return lifecycle_failure(owner);
    for (size_t i = 0; i < owner->initialized_count; ++i) if (owner->initialized_ids[i] == id) {
        memmove(owner->initialized_ids + i, owner->initialized_ids + i + 1,
            (owner->initialized_count - i - 1) * sizeof(*owner->initialized_ids)); --owner->initialized_count; break;
    }
    record->initialized = false; return true;
}

bool windows_validate_storage(guest_windows *owner, uint64_t address, size_t bytes,
    int32_t tag, qa_error *error)
{
    qa_native_allocation_info allocation;
    if (!address || !qa_native_guest_allocation(owner->guest, address, &allocation, error)) return false;
    return (allocation.base == address && allocation.bytes == bytes && allocation.tag == tag) ||
        guest_fail(error, QA_ERROR_FORMAT, address, "Windows owner address differs from its genuine lower allocation");
}

typedef struct windows_codec {
    qa_buffer output; qa_bytes input; size_t offset, capacity;
    qa_error *error; bool reading;
} windows_codec;

static bool codec_bytes(windows_codec *io, void *data, size_t count)
{
    if (io->reading) {
        if (count > io->input.size - io->offset) {
            guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Windows continuation is truncated");
            return false;
        }
        if (count) memcpy(data, io->input.data + io->offset, count);
        io->offset += count; return true;
    }
    if (count > SIZE_MAX - io->output.size || !guest_grow((void **)&io->output.data,
        &io->capacity, io->output.size + count, 1, io->error)) return false;
    if (count) memcpy(io->output.data + io->output.size, data, count);
    io->output.size += count; return true;
}

static bool codec_u64(windows_codec *io, uint64_t *value)
{
    uint8_t bytes[8]; if (!io->reading) qa_store_u64le(bytes, *value);
    if (!codec_bytes(io, bytes, 8)) return false;
    if (io->reading) *value = qa_load_u64le(bytes);
    return true;
}

static bool codec_u32(windows_codec *io, uint32_t *value)
{
    uint8_t bytes[4]; if (!io->reading) qa_store_u32le(bytes, *value);
    if (!codec_bytes(io, bytes, 4)) return false;
    if (io->reading) *value = qa_load_u32le(bytes);
    return true;
}

static bool codec_bool(windows_codec *io, bool *value)
{
    uint8_t byte = io->reading ? 0 : *value;
    if (!codec_bytes(io, &byte, 1)) return false;
    if (byte > 1) return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Windows continuation boolean is invalid");
    *value = byte != 0; return true;
}

static bool codec_locale(windows_codec *io, qa_native_windows_locale_profile *profile)
{
    if (!codec_u32(io,&profile->source) || !codec_u32(io,&profile->ansi_code_page) ||
        !codec_u32(io,&profile->oem_code_page)) return false;
    qa_native_windows_locale *locales[] = {&profile->user,&profile->system};
    for (size_t i = 0; i < 2; ++i) {
        qa_native_windows_locale *locale = locales[i];
        if (!codec_u32(io,&locale->lcid) || !codec_u32(io,&locale->language_id) || !codec_u32(io,&locale->ansi_code_page) ||
            !codec_u32(io,&locale->oem_code_page) || !codec_u32(io,&locale->sort_version) ||
            !codec_u32(io,&locale->sort_defined_version) || !codec_u32(io,&locale->sort_effective_id) ||
            !codec_bytes(io,locale->sort_custom_version,sizeof(locale->sort_custom_version))) return false;
        for (size_t j = 0; j < QA_NATIVE_WINDOWS_LOCALE_NAME_UNITS; ++j) {
            uint8_t bytes[2]; if (!io->reading) qa_store_u16le(bytes,locale->collation_name[j]);
            if (!codec_bytes(io,bytes,2)) return false;
            if (io->reading) locale->collation_name[j] = qa_load_u16le(bytes);
        }
        uint16_t *fields[] = {locale->decimal,locale->thousands,locale->grouping,
            locale->default_decimal,locale->default_thousands,locale->default_grouping};
        for (size_t j = 0; j < sizeof(fields)/sizeof(*fields); ++j)
            for (size_t k = 0; k < QA_NATIVE_WINDOWS_LOCALE_UNITS; ++k) {
                uint8_t bytes[2]; if (!io->reading) qa_store_u16le(bytes,fields[j][k]);
                if (!codec_bytes(io,bytes,2)) return false;
                if (io->reading) fields[j][k] = qa_load_u16le(bytes);
            }
    }
    return qa_native_windows_locale_profile_valid(profile) ||
        guest_fail(io->error,QA_ERROR_FORMAT,io->offset,"Windows continuation locale snapshot is invalid");
}

static bool codec_count(windows_codec *io, size_t *count, size_t minimum)
{
    uint64_t value = *count;
    if (!codec_u64(io, &value)) return false;
    if (io->reading) {
        if (value > SIZE_MAX || value > (io->input.size - io->offset) / minimum)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Windows continuation table exceeds remaining source bytes");
        *count = (size_t)value;
    }
    return true;
}

static bool codec_text(windows_codec *io, char **text)
{
    size_t bytes = *text ? strlen(*text) : 0;
    if (!codec_count(io, &bytes, 1)) return false;
    if (bytes == SIZE_MAX) return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Windows identity length overflows its terminator");
    if (io->reading) {
        *text = malloc(bytes + 1);
        if (!*text) return guest_fail(io->error, QA_ERROR_MEMORY, 0, "decoding Windows continuation text");
    }
    if (!codec_bytes(io, *text, bytes)) return false;
    if (io->reading) {
        if (memchr(*text, 0, bytes)) return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Windows identity contains an embedded terminator");
        (*text)[bytes] = 0;
    }
    return true;
}

static bool codec_vector(windows_codec *io, uint64_t **values, size_t *count, size_t *capacity)
{
    if (!codec_count(io, count, 8)) return false;
    if (io->reading && !guest_grow((void **)values, capacity, *count, sizeof(**values), io->error)) return false;
    for (size_t i = 0; i < *count; ++i) if (!codec_u64(io, *values + i)) return false;
    return true;
}

static bool codec_kernel(windows_codec *io, guest_windows_kernel *kernel)
{
    uint64_t *addresses[] = {&kernel->command_line_a,&kernel->command_line_w,&kernel->environment_a,&kernel->environment_w,
        &kernel->pointer_secret,&kernel->process_heap,&kernel->dynamic_tls,&kernel->exception_filter};
    for (size_t i = 0; i < sizeof(addresses)/sizeof(*addresses); ++i) if (!codec_u64(io, addresses[i])) return false;
    if (!codec_vector(io,&kernel->heaps,&kernel->heap_count,&kernel->heap_capacity) ||
        !codec_vector(io,&kernel->locks,&kernel->lock_count,&kernel->lock_capacity) ||
        !codec_count(io,&kernel->reservation_count,17)) return false;
    if (io->reading && !guest_grow((void **)&kernel->reservations,&kernel->reservation_capacity,
        kernel->reservation_count,sizeof(*kernel->reservations),io->error)) return false;
    for (size_t i = 0; i < kernel->reservation_count; ++i) {
        windows_reservation *record = kernel->reservations + i;
        if (!codec_u64(io,&record->base) || !codec_u64(io,&record->bytes) || !codec_bool(io,&record->allocated)) return false;
    }
    for (size_t i = 0; i < 1088; ++i) if (!codec_bool(io,kernel->tls + i)) return false;
    for (size_t i = 0; i < 128; ++i) {
        windows_fls *record = kernel->fls + i;
        if (!codec_bool(io,&record->allocated) || !codec_u64(io,&record->callback) || !codec_u64(io,&record->value)) return false;
    }
    if (!codec_count(io,&kernel->standard_count,12)) return false;
    if (io->reading && !guest_grow((void **)&kernel->standards,&kernel->standard_capacity,
        kernel->standard_count,sizeof(*kernel->standards),io->error)) return false;
    for (size_t i = 0; i < kernel->standard_count; ++i) {
        uint32_t id = io->reading ? 0 : (uint32_t)kernel->standards[i].id;
        if (!codec_u32(io,&id) || !codec_u64(io,&kernel->standards[i].handle)) return false;
        memcpy(&kernel->standards[i].id,&id,4);
    }
    if (!codec_count(io,&kernel->handle_count,20)) return false;
    if (io->reading && !guest_grow((void **)&kernel->handles,&kernel->handle_capacity,
        kernel->handle_count,sizeof(*kernel->handles),io->error)) return false;
    for (size_t i = 0; i < kernel->handle_count; ++i) {
        uint32_t stream = io->reading ? 0 : (uint32_t)kernel->handles[i].stream;
        if (!codec_u64(io,&kernel->handles[i].handle) || !codec_u32(io,&stream) ||
            !codec_u64(io,&kernel->handles[i].stream_offset)) return false;
        memcpy(&kernel->handles[i].stream,&stream,4);
    }
    return true;
}

static bool codec_crt(windows_codec *io, guest_windows_crt *crt)
{
    uint64_t *values[] = {&crt->onexit,&crt->error_number,&crt->time_buffer,&crt->locale,&crt->empty,&crt->decimal};
    for (size_t i = 0; i < sizeof(values)/sizeof(*values); ++i) if (!codec_u64(io,values[i])) return false;
    if (!codec_bool(io,&crt->has_file_opener) || !codec_u32(io,&crt->next_file) ||
        !codec_count(io,&crt->file_count,44)) return false;
    if (io->reading && !guest_grow((void **)&crt->files,&crt->file_capacity,
        crt->file_count,sizeof(*crt->files),io->error)) return false;
    for (size_t i = 0; i < crt->file_count; ++i) {
        windows_stdio_file *f = crt->files + i;
        if (!codec_u64(io,&f->address) || !codec_u64(io,&f->handle) || !codec_u64(io,&f->capability) ||
            !codec_u32(io,&f->mode) || !codec_u32(io,&f->descriptor) || !codec_u32(io,&f->flags) ||
            !codec_bytes(io,&f->pushback,1) || !codec_bytes(io,&f->pending,1) ||
            !codec_bool(io,&f->legacy) || !codec_bool(io,&f->binary) || !codec_bool(io,&f->append) ||
            !codec_bool(io,&f->closing) || !codec_bool(io,&f->has_pushback) || !codec_bool(io,&f->has_pending)) return false;
    }
    return true;
}

static bool codec_msvc(windows_codec *io, guest_windows_msvc *msvc)
{
    uint64_t *values[] = {&msvc->locks,&msvc->facet_vtable,&msvc->global_locale,&msvc->true_text,&msvc->false_text,
        &msvc->ios_vtable,&msvc->ostream_vtable,&msvc->iostream_vtable,&msvc->buffer_vtable,
        &msvc->ostream_vbase,&msvc->iostream_vbase,&msvc->locale_id_count,&msvc->numpunct_id};
    for (size_t i = 0; i < sizeof(values)/sizeof(*values); ++i) if (!codec_u64(io,values[i])) return false;
    return true;
}

static bool codec_owner(windows_codec *io, guest_windows *owner,
    guest_windows_image_resolve_fn resolve, void *context)
{
    (void)resolve; (void)context;
    uint32_t os = owner->target.os, arch = owner->target.arch, abi = owner->target.abi;
    if (!codec_u32(io,&os) || !codec_u32(io,&arch) || !codec_u32(io,&abi) || !codec_bytes(io,&owner->target.pointer_bytes,1)) return false;
    owner->target.os = (qa_native_os)os; owner->target.arch = (qa_native_arch)arch; owner->target.abi = (qa_native_abi)abi;
    uint32_t execution = owner->execution;
    if (!codec_u32(io, &execution) || execution > QA_NATIVE_GUEST_HOST_X86_64)
        return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Windows continuation backend is invalid");
    owner->execution = (qa_native_guest_backend)execution;
    if (os != QA_NATIVE_OS_WINDOWS || !((arch == QA_NATIVE_ARCH_I386 && abi == QA_NATIVE_ABI_CDECL_I386 && owner->target.pointer_bytes == 4) ||
        (arch == QA_NATIVE_ARCH_X86_64 && abi == QA_NATIVE_ABI_MICROSOFT_X64 && owner->target.pointer_bytes == 8)))
        return guest_fail(io->error,QA_ERROR_FORMAT,io->offset,"Windows continuation ABI is unsupported");
    uint64_t *addresses[] = {&owner->capability_id,&owner->teb,&owner->peb,&owner->static_tls,&owner->return_trap,
        &owner->trap_cursor,&owner->trap_end,&owner->stack_base,&owner->stack_bytes,&owner->cfg_check,&owner->cfg_dispatch,&owner->primary_image};
    for (size_t i = 0; i < sizeof(addresses)/sizeof(*addresses); ++i) if (!codec_u64(io,addresses[i])) return false;
    for (size_t i = 0; i < 3; ++i) if (!codec_u64(io,owner->stream_ids + i)) return false;
    uint64_t budget = owner->instruction_budget, frequency = (uint64_t)owner->capabilities.performance_frequency;
    if (!codec_u64(io,&budget) || !codec_u64(io,&frequency) || !codec_u32(io,&owner->process_id) ||
        !codec_u32(io,&owner->thread_id) || !codec_u32(io,&owner->static_tls_count)) return false;
    if (budget > SIZE_MAX || !call_policy(owner->execution, (size_t)budget) ||
        (owner->execution == QA_NATIVE_GUEST_HOST_X86_64 &&
         (arch != QA_NATIVE_ARCH_X86_64 || abi != QA_NATIVE_ABI_MICROSOFT_X64 || owner->target.pointer_bytes != 8)) ||
        frequency > INT64_MAX || !frequency)
        return guest_fail(io->error,QA_ERROR_FORMAT,io->offset,"Windows continuation budget or clock capability is invalid");
    owner->instruction_budget = (size_t)budget; owner->capabilities.performance_frequency = (int64_t)frequency;
    if (!codec_locale(io,&owner->capabilities.locale)) return false;
    if (io->reading && (!windows_kernel_descriptors(owner,false,io->error) ||
        !windows_crt_descriptors(owner,false,io->error) || !windows_msvc_descriptors(owner,false,io->error))) return false;
    size_t services = owner->service_count;
    if (!codec_count(io,&services,16)) return false;
    size_t immutable_count = owner->service_count;
    if (io->reading && services != immutable_count && services != immutable_count + 2)
        return guest_fail(io->error,QA_ERROR_FORMAT,io->offset,"Windows continuation descriptor inventory differs");
    if (io->reading && services == immutable_count + 2) {
        qa_native_value_type pointer = QA_NATIVE_ADDRESS; size_t width = owner->target.pointer_bytes;
        if (!windows_service_add(owner,UINT64_C(0x57494e0400000001),4,1,"quake-runtime.dll","guard-check",NULL,0,
            QA_NATIVE_VOID,width == 4 ? GUEST_ABI_STDCALL : GUEST_ABI_DEFAULT,false,io->error) ||
            !windows_service_add(owner,UINT64_C(0x57494e0400000002),4,2,"quake-runtime.dll","guard-dispatch-check",
                width == 8 ? &pointer : NULL,width == 8 ? 1 : 0,QA_NATIVE_VOID,
                width == 4 ? GUEST_ABI_STDCALL : GUEST_ABI_DEFAULT,false,io->error)) return false;
    }
    for (size_t i = 0; i < services; ++i) {
        windows_service *service = owner->services[i]; uint64_t id = service->function.id;
        if (!codec_u64(io,&id) || !codec_u64(io,&service->function.address)) return false;
        if (id != service->function.id) return guest_fail(io->error,QA_ERROR_FORMAT,io->offset,"Windows immutable descriptor identity/order differs");
    }
    if (!codec_count(io,&owner->library_count,24)) return false;
    if (io->reading && !guest_grow((void **)&owner->libraries,&owner->library_capacity,
        owner->library_count,sizeof(*owner->libraries),io->error)) return false;
    if (io->reading && owner->library_count) memset(owner->libraries,0,owner->library_count*sizeof(*owner->libraries));
    for (size_t i = 0; i < owner->library_count; ++i) {
        windows_library *record = owner->libraries + i;
        if (!codec_text(io,&record->name) || !codec_u64(io,&record->handle) || !codec_u64(io,&record->references)) return false;
    }
    if (!codec_count(io,&owner->requested_count,8)) return false;
    if (io->reading && !guest_grow((void **)&owner->requested,&owner->requested_capacity,
        owner->requested_count,sizeof(*owner->requested),io->error)) return false;
    if (io->reading && owner->requested_count) memset(owner->requested,0,owner->requested_count*sizeof(*owner->requested));
    for (size_t i = 0; i < owner->requested_count; ++i) if (!codec_text(io,owner->requested + i)) return false;
    if (!codec_count(io,&owner->image_count,62)) return false;
    if (io->reading && !guest_grow((void **)&owner->images,&owner->image_capacity,
        owner->image_count,sizeof(*owner->images),io->error)) return false;
    if (io->reading && owner->image_count) memset(owner->images,0,owner->image_count*sizeof(*owner->images));
    for (size_t i = 0; i < owner->image_count; ++i) {
        windows_image_record *record = owner->images + i;
        if (!codec_u64(io,&record->id) || !codec_u64(io,&record->base) ||
            !codec_u64(io,&record->preferred_base) || !codec_u64(io,&record->image_bytes) ||
            !codec_text(io,&record->path) || !codec_u64(io,&record->tls_block) || !codec_u32(io,&record->tls_index) ||
            !codec_u64(io,&record->references) || !codec_bool(io,&record->prepared) || !codec_bool(io,&record->initialized)) return false;
    }
    if (!codec_count(io,&owner->allocation_count,24)) return false;
    if (io->reading && !guest_grow((void **)&owner->allocations,&owner->allocation_capacity,
        owner->allocation_count,sizeof(*owner->allocations),io->error)) return false;
    for (size_t i = 0; i < owner->allocation_count; ++i) {
        windows_heap_allocation *record = owner->allocations + i;
        if (!codec_u64(io,&record->address) || !codec_u64(io,&record->heap) || !codec_u64(io,&record->requested)) return false;
    }
    return codec_vector(io,&owner->cfg_targets,&owner->cfg_count,&owner->cfg_capacity) &&
        codec_vector(io,&owner->prepared_ids,&owner->prepared_count,&owner->prepared_capacity) &&
        codec_vector(io,&owner->initialized_ids,&owner->initialized_count,&owner->initialized_capacity) &&
        codec_kernel(io,owner->kernel) && codec_crt(io,owner->crt) && codec_msvc(io,owner->msvc);
}

static bool function_resolve(void *context, uint64_t id, guest_runtime_function *out, qa_error *error)
{
    guest_windows *owner = context;
    for (size_t i = 0; i < owner->service_count; ++i) if (owner->services[i]->function.id == id) { *out = owner->services[i]->function; return true; }
    return guest_fail(error,QA_ERROR_NOT_FOUND,id,"Windows saved callback has no immutable descriptor");
}

static bool pointer_value(const guest_windows *owner, uint64_t address)
{ return owner->target.pointer_bytes == 8 || address <= UINT32_MAX; }

static bool unique_values(const uint64_t *values, size_t count)
{
    for (size_t i = 0; i < count; ++i) for (size_t j = 0; j < i; ++j) if (values[i] == values[j]) return false;
    return true;
}

static bool has_value(const uint64_t *values, size_t count, uint64_t value)
{ for (size_t i = 0; i < count; ++i) if (values[i] == value) return true; return false; }

static bool records_valid(guest_windows *owner, qa_error *error)
{
    guest_windows_kernel *kernel = owner->kernel;
    if (!call_policy(owner->execution, owner->instruction_budget) ||
        !qa_native_windows_locale_profile_valid(&owner->capabilities.locale) ||
        (owner->guest && qa_native_guest_execution(owner->guest) != owner->execution))
        return guest_fail(error, QA_ERROR_FORMAT, 0, "Windows continuation differs from its actual execution policy");
    if (!owner->primary_image || !windows_image_at(owner, owner->primary_image))
        return guest_fail(error, QA_ERROR_FORMAT, owner->primary_image, "Windows continuation has no actual primary image");
    const windows_image_record *primary = windows_image_at(owner, owner->primary_image);
    if (owner->guest && (primary->preferred_base != owner->guest->options.image.preferred_base ||
        primary->image_bytes != owner->guest->options.image.image_bytes))
        return guest_fail(error, QA_ERROR_FORMAT, owner->primary_image, "Windows primary differs from its retained lower artifact witness");
    /* Open acquisition failures retain close owners, but the corresponding
     * source callback makes the CPU terminal. They have no healthy cold cut. */
    if (kernel->pending_count || owner->crt->pending_file_count || owner->files_closed)
        return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows failure-only file acquisition or retirement cannot be checkpointed");
    if (!owner->capability_id || !owner->teb || !owner->peb || !owner->static_tls || !owner->return_trap ||
        !owner->stack_base || !owner->stack_bytes || owner->stack_bytes > UINT64_MAX - owner->stack_base ||
        owner->static_tls_count > 1024 || !owner->trap_end || owner->trap_end % 4096 || owner->trap_cursor > owner->trap_end ||
        owner->trap_end - owner->trap_cursor > 4096 || owner->trap_cursor % 16 ||
        !unique_values(owner->prepared_ids,owner->prepared_count) || !unique_values(owner->initialized_ids,owner->initialized_count) ||
        !unique_values(owner->cfg_targets,owner->cfg_count) || !unique_values(kernel->heaps,kernel->heap_count) ||
        !unique_values(kernel->locks,kernel->lock_count) || !has_value(kernel->heaps,kernel->heap_count,kernel->process_heap) ||
        !(kernel->pointer_secret & 1) || !pointer_value(owner,kernel->pointer_secret))
        return guest_fail(error,QA_ERROR_FORMAT,0,"Windows continuation owner identities or ordered sets are invalid");
    uint64_t addresses[] = {owner->teb,owner->peb,owner->static_tls,owner->return_trap,owner->trap_cursor,
        owner->trap_end - 1,owner->stack_base,owner->stack_base + owner->stack_bytes - 1,owner->cfg_check,owner->cfg_dispatch,
        kernel->command_line_a,kernel->command_line_w,kernel->environment_a,kernel->environment_w,kernel->process_heap,kernel->dynamic_tls,kernel->exception_filter,
        owner->crt->onexit,owner->crt->error_number,owner->crt->time_buffer,owner->crt->locale,owner->crt->empty,owner->crt->decimal};
    for (size_t i = 0; i < sizeof(addresses)/sizeof(*addresses); ++i) if (!pointer_value(owner,addresses[i])) return guest_fail(error,QA_ERROR_FORMAT,addresses[i],"Windows pointer exceeds the actual process width");
    for (size_t i = 0; i < owner->service_count; ++i) {
        uint64_t address = owner->services[i]->function.address;
        if (!address || address % 16 || !pointer_value(owner,address) || address == owner->return_trap)
            return guest_fail(error,QA_ERROR_FORMAT,address,"Windows callback trap identity is invalid");
        for (size_t j = 0; j < i; ++j) if (owner->services[j]->function.address == address)
            return guest_fail(error,QA_ERROR_FORMAT,address,"Windows physical service traps repeat");
    }
    for (size_t i = 0; i < owner->library_count; ++i) {
        windows_library *record = owner->libraries + i; char *normalized = canonical(record->name,error);
        if (!normalized) return false;
        bool valid = !strcmp(normalized,record->name) && *record->name && record->handle && pointer_value(owner,record->handle); free(normalized);
        if (!valid) return guest_fail(error,QA_ERROR_FORMAT,i,"Windows built-in library identity is invalid");
        for (size_t j = 0; j < i; ++j) if (!strcmp(record->name,owner->libraries[j].name) || record->handle == owner->libraries[j].handle)
            return guest_fail(error,QA_ERROR_FORMAT,i,"Windows built-in library identity repeats");
    }
    for (size_t i = 0; i < owner->requested_count; ++i) {
        const char *request = owner->requested[i], *separator = strchr(request,'!');
        if (!separator || separator == request || !separator[1]) return guest_fail(error,QA_ERROR_FORMAT,i,"Windows requested import has no actual symbol");
        for (size_t j = 0; j < i; ++j) if (!strcmp(request,owner->requested[j])) return guest_fail(error,QA_ERROR_FORMAT,i,"Windows requested import repeats");
    }
    size_t tls_count = 0;
    for (size_t i = 0; i < owner->image_count; ++i) {
        windows_image_record *record = owner->images + i;
        if (!record->id || !record->base || record->base % 65536 || !*record->path || !pointer_value(owner,record->base) ||
            record->prepared != has_value(owner->prepared_ids,owner->prepared_count,record->id) ||
            record->initialized != has_value(owner->initialized_ids,owner->initialized_count,record->id) || (record->initialized && !record->prepared))
            return guest_fail(error,QA_ERROR_FORMAT,record->id,"Windows image lifecycle receipt is invalid");
        for (size_t j = 0; j < i; ++j) if (record->id == owner->images[j].id || record->base == owner->images[j].base)
            return guest_fail(error,QA_ERROR_FORMAT,record->id,"Windows image identity repeats");
        if (record->tls_block) {
            ++tls_count;
            if (!pointer_value(owner,record->tls_block) || record->tls_index >= owner->static_tls_count)
                return guest_fail(error,QA_ERROR_FORMAT,record->id,"Windows static TLS slot is invalid");
            for (size_t j = 0; j < i; ++j) if (owner->images[j].tls_block &&
                (owner->images[j].tls_index == record->tls_index || owner->images[j].tls_block == record->tls_block))
                return guest_fail(error,QA_ERROR_FORMAT,record->id,"Windows static TLS slot repeats");
        } else if (record->tls_index) return guest_fail(error,QA_ERROR_FORMAT,record->id,"Windows image without TLS has a slot index");
    }
    if (tls_count != owner->static_tls_count) return guest_fail(error,QA_ERROR_FORMAT,0,"Windows static TLS vector count differs from real image blocks");
    for (size_t i = 0; i < owner->prepared_count; ++i) if (!windows_image_at(owner,owner->prepared_ids[i])) return guest_fail(error,QA_ERROR_FORMAT,i,"Windows prepared order names no actual image");
    for (size_t i = 0; i < owner->initialized_count; ++i) if (!windows_image_at(owner,owner->initialized_ids[i])) return guest_fail(error,QA_ERROR_FORMAT,i,"Windows initialized order names no actual image");
    for (size_t i = 0; i < owner->allocation_count; ++i) {
        windows_heap_allocation *record = owner->allocations + i;
        if (!record->address || !record->requested || record->requested > 0x10000000 || !pointer_value(owner,record->address) ||
            record->requested - 1 > UINT64_MAX - record->address || !pointer_value(owner,record->address + record->requested - 1) ||
            (record->heap && !has_value(kernel->heaps,kernel->heap_count,record->heap)))
            return guest_fail(error,QA_ERROR_FORMAT,record->address,"Windows requested heap allocation is invalid");
        for (size_t j = 0; j < i; ++j) if (record->address == owner->allocations[j].address)
            return guest_fail(error,QA_ERROR_FORMAT,record->address,"Windows requested heap allocation repeats");
    }
    for (size_t i = 0; i < kernel->reservation_count; ++i) {
        windows_reservation *record = kernel->reservations + i;
        if (!record->base || record->base % 65536 || !record->bytes || record->bytes % 4096 ||
            record->bytes > UINT64_MAX - record->base || !pointer_value(owner,record->base + record->bytes - 1))
            return guest_fail(error,QA_ERROR_FORMAT,record->base,"Windows reservation extent is invalid");
        for (size_t j = 0; j < i; ++j) if (record->base < kernel->reservations[j].base + kernel->reservations[j].bytes &&
            kernel->reservations[j].base < record->base + record->bytes)
            return guest_fail(error,QA_ERROR_FORMAT,record->base,"Windows reservations overlap");
    }
    for (size_t i = 0; i < 128; ++i) if ((!kernel->fls[i].allocated && (kernel->fls[i].callback || kernel->fls[i].value)) ||
        !pointer_value(owner,kernel->fls[i].callback) || !pointer_value(owner,kernel->fls[i].value))
        return guest_fail(error,QA_ERROR_FORMAT,i,"Windows FLS record is invalid");
    for (size_t i = 0; i < kernel->standard_count; ++i) {
        if (!kernel->standards[i].handle || !pointer_value(owner,kernel->standards[i].handle)) return guest_fail(error,QA_ERROR_FORMAT,i,"Windows standard handle is invalid");
        for (size_t j = 0; j < i; ++j) if (kernel->standards[i].id == kernel->standards[j].id) return guest_fail(error,QA_ERROR_FORMAT,i,"Windows standard handle key repeats");
    }
    for (size_t i = 0; i < kernel->handle_count; ++i) {
        windows_file_handle *record = kernel->handles + i;
        if (!record->handle || !pointer_value(owner,record->handle) || record->stream < -1 || record->stream > 2 ||
            (record->stream < 0 && record->stream_offset != 0)) return guest_fail(error,QA_ERROR_FORMAT,i,"Windows file handle receipt is invalid");
        for (size_t j = 0; j < i; ++j) if (record->handle == kernel->handles[j].handle) return guest_fail(error,QA_ERROR_FORMAT,i,"Windows file handle repeats");
    }
    return true;
}

static bool resolve_images(guest_windows *owner, guest_windows_image_resolve_fn resolve, void *context, qa_error *error)
{
    for (size_t i = 0; i < owner->image_count; ++i) {
        windows_image_record *record = owner->images + i; guest_windows_image actual = {0};
        if (!resolve || !resolve(context,record->id,record->base,&actual,error)) return false;
        const guest_pe_view *pe = guest_pe_describe(actual.image);
        if (!pe || actual.id != record->id || !actual.path || strcmp(actual.path,record->path) || pe->base != record->base ||
            pe->image.preferred_base != record->preferred_base || pe->image.image_bytes != record->image_bytes || pe->image.target.os != owner->target.os || pe->image.target.arch != owner->target.arch ||
            pe->image.target.abi != owner->target.abi || pe->image.target.pointer_bytes != owner->target.pointer_bytes ||
            (record->tls_block && !pe->tls.present) || (record->prepared && pe->tls.present && !record->tls_block))
            return guest_fail(error,QA_ERROR_FORMAT,record->id,"Windows image resolver returned a different actual immutable owner");
        record->image = actual.image;
    }
    return true;
}

static bool blob(windows_codec *io, qa_buffer *bytes)
{
    size_t count = bytes->size;
    if (!codec_count(io,&count,1)) return false;
    if (io->reading) { bytes->data = count ? malloc(count) : NULL; bytes->size = count; if (count && !bytes->data) return guest_fail(io->error,QA_ERROR_MEMORY,0,"decoding Windows child continuation"); }
    return codec_bytes(io,bytes->data,count);
}

static bool registry_valid(const guest_windows *owner, qa_error *error)
{
    for (size_t i = 0; i < owner->service_count; ++i) {
        const windows_service *service = owner->services[i];
        guest_runtime_import_key key = {.library=service->library,
            .kind=GUEST_RUNTIME_SYMBOL_NAME,.name=service->name};
        guest_runtime_import_view view;
        if (!guest_runtime_imports_find(owner->imports,&key,&view,error)) return false;
        if (view.kind != GUEST_RUNTIME_IMPORT_FUNCTION || view.id != service->function.id ||
            view.address != service->function.address)
            return guest_fail(error,QA_ERROR_FORMAT,service->function.id,
                "Windows service key lost its actual immutable callback identity");
    }
    return true;
}

bool guest_windows_checkpoint(const guest_windows *source, qa_buffer *out, qa_error *error)
{
    if (!guest_windows_idle(source) || !out || out->data || out->size)
        return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows checkpoint requires idle real owner and empty output");
    windows_codec io = {.error=error}; guest_windows *owner = (guest_windows *)source;
    char magic[4] = {'Q','A','W','N'}; qa_buffer imports = {0}, resources = {0};
    bool okay = records_valid(owner,error) && windows_stdio_lower_valid(owner,error) && registry_valid(owner,error) && codec_bytes(&io,magic,sizeof(magic)) && codec_owner(&io,owner,NULL,NULL) &&
        guest_runtime_imports_checkpoint(owner->imports,&imports,error) &&
        guest_runtime_resources_checkpoint(owner->resources,&resources,error) && blob(&io,&imports) && blob(&io,&resources);
    qa_buffer_free(&imports); qa_buffer_free(&resources);
    if (!okay) { qa_buffer_free(&io.output); return false; }
    *out = io.output; return true;
}

bool guest_windows_decode(qa_bytes bytes, guest_windows_image_resolve_fn resolve,
    void *context, guest_windows **out, qa_error *error)
{
    if (!out || *out || !bytes.data) return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows decode requires source bytes and empty output");
    guest_windows *owner = calloc(1,sizeof(*owner));
    if (!owner) return guest_fail(error,QA_ERROR_MEMORY,0,"allocating detached Windows candidate");
    owner->detached = true; owner->kernel = calloc(1,sizeof(*owner->kernel)); owner->crt = calloc(1,sizeof(*owner->crt)); owner->msvc = calloc(1,sizeof(*owner->msvc));
    if (!owner->kernel || !owner->crt || !owner->msvc) { dispose(owner); return guest_fail(error,QA_ERROR_MEMORY,0,"allocating detached Windows services"); }
    windows_codec io = {.input=bytes,.error=error,.reading=true}; char magic[4]; qa_buffer imports = {0}, resources = {0};
    bool okay = codec_bytes(&io,magic,sizeof(magic)) && !memcmp(magic,"QAWN",sizeof(magic));
    if (!okay) guest_fail(error,QA_ERROR_FORMAT,0,"Windows continuation signature differs");
    if (okay) okay = codec_owner(&io,owner,resolve,context) && blob(&io,&imports) && blob(&io,&resources);
    if (okay && io.offset != bytes.size) okay = guest_fail(error,QA_ERROR_FORMAT,io.offset,"Windows continuation has trailing bytes");
    if (okay) okay = records_valid(owner,error) &&
        guest_runtime_imports_decode((qa_bytes){imports.data,imports.size},&owner->target,function_resolve,owner,&owner->imports,error) &&
        guest_runtime_resources_decode((qa_bytes){resources.data,resources.size},&owner->resources,error) &&
        windows_stdio_valid(owner,error) &&
        registry_valid(owner,error) &&
        resolve_images(owner,resolve,context,error);
    qa_buffer_free(&imports); qa_buffer_free(&resources);
    if (!okay) { guest_runtime_imports_abandon(&owner->imports); guest_runtime_resources_abandon(&owner->resources); dispose(owner); return false; }
    owner->constructed = true;
    *out = owner; return true;
}

bool guest_windows_callback(void *context, uint64_t id, uint64_t address,
    qa_native_guest_callback *out, qa_error *error)
{
    guest_windows *owner = context;
    if (!owner || !owner->detached || !owner->imports) return guest_fail(error,QA_ERROR_ARGUMENT,id,"Windows callback resolver requires its detached complete candidate");
    return guest_runtime_imports_callback(owner->imports,id,address,out,error);
}

static bool restored_storage(guest_windows *owner, qa_error *error)
{
    size_t width = owner->target.pointer_bytes;
    if (!windows_validate_storage(owner,owner->teb,0x2000,WINDOWS_STORAGE_TAG,error) ||
        !windows_validate_storage(owner,owner->peb,0x1000,WINDOWS_STORAGE_TAG,error) ||
        !windows_validate_storage(owner,owner->static_tls,width*1024,WINDOWS_STORAGE_TAG,error)) return false;
    for (size_t i = 0; i < owner->library_count; ++i)
        if (!windows_validate_storage(owner,owner->libraries[i].handle,16,WINDOWS_STORAGE_TAG,error)) return false;
    for (size_t i = 0; i < owner->allocation_count; ++i)
        if (!windows_validate_storage(owner,owner->allocations[i].address,(size_t)owner->allocations[i].requested,WINDOWS_HEAP_TAG,error)) return false;
    for (size_t i = 0; i < owner->image_count; ++i) {
        windows_image_record *record = owner->images + i; const guest_pe_view *pe = guest_pe_describe(record->image);
        if (record->tls_block) {
            if (pe->tls.initialized.size > SIZE_MAX - pe->tls.zero_bytes) return guest_fail(error,QA_ERROR_FORMAT,record->id,"restored Windows TLS extent overflows");
            size_t bytes = pe->tls.initialized.size + pe->tls.zero_bytes;
            if (!windows_validate_storage(owner,record->tls_block,bytes ? bytes : 1,WINDOWS_STORAGE_TAG,error)) return false;
        }
    }
    if (!owner->service_count || owner->return_trap > UINT64_MAX - 16 || owner->services[0]->function.address != owner->return_trap + 16 ||
        !executable(owner,owner->return_trap,error)) return guest_fail(error,QA_ERROR_FORMAT,owner->return_trap,"Windows return trap lost its actual import-page owner");
    uint64_t trap_byte;
    if (!windows_read(owner,owner->return_trap,1,&trap_byte,error) || trap_byte != 0xcc)
        return guest_fail(error,QA_ERROR_FORMAT,owner->return_trap,"Windows return trap differs from its retained physical byte");
    uint64_t last_trap = owner->return_trap;
    for (size_t i = 0; i < guest_runtime_imports_count(owner->imports); ++i) {
        guest_runtime_import_view view;
        if (!guest_runtime_imports_at(owner->imports,i,&view,error)) return false;
        if (view.kind != GUEST_RUNTIME_IMPORT_DATA && view.address > last_trap) last_trap = view.address;
    }
    if (last_trap > UINT64_MAX - 16 || owner->trap_cursor != last_trap + 16 ||
        !owner->trap_cursor || owner->trap_end != ((owner->trap_cursor - 1) | UINT64_C(4095)) + 1)
        return guest_fail(error,QA_ERROR_FORMAT,owner->trap_cursor,"Windows trap allocation cursor differs from its physical registered slots");
    uint64_t check = 0, dispatch_check = 0;
    for (size_t i = 0; i < owner->service_count; ++i) {
        windows_service *service = owner->services[i];
        if (service->family == 4 && service->operation == 1) check = service->function.address;
        if (service->family == 4 && service->operation == 2) dispatch_check = service->function.address;
    }
    if (owner->cfg_check != check || (check == 0) != (owner->cfg_dispatch == 0) || (check == 0) != (dispatch_check == 0))
        return guest_fail(error,QA_ERROR_FORMAT,0,"Windows CFG helper tuple differs from its actual callback identities");
    if (check && width == 4 && owner->cfg_dispatch != dispatch_check)
        return guest_fail(error,QA_ERROR_FORMAT,owner->cfg_dispatch,"Windows i386 CFG dispatch lost its actual callback");
    if (check && width == 8) {
        uint8_t expected[sizeof(cfg_dispatch_code)], actual[sizeof(cfg_dispatch_code)];
        memcpy(expected,cfg_dispatch_code,sizeof(expected)); qa_store_u64le(expected + 16,dispatch_check);
        if (!windows_validate_storage(owner,owner->cfg_dispatch,sizeof(expected),WINDOWS_STORAGE_TAG,error) ||
            !executable(owner,owner->cfg_dispatch,error) ||
            !qa_native_guest_read(owner->guest,owner->cfg_dispatch,actual,sizeof(actual),error)) return false;
        if (memcmp(expected,actual,sizeof(expected))) return guest_fail(error,QA_ERROR_FORMAT,owner->cfg_dispatch,"Windows CFG dispatch instructions differ from their real checker identity");
    }
    for (size_t i = 0; i < owner->cfg_count; ++i) if (!pointer_value(owner,owner->cfg_targets[i]) || !executable(owner,owner->cfg_targets[i],error)) return false;
    return windows_kernel_validate(owner,error) && windows_crt_validate(owner,error) && windows_msvc_validate(owner,error);
}

bool guest_windows_attach(guest_windows *owner, qa_native_guest *guest,
    const guest_windows_capabilities *capabilities, qa_error *error)
{
    if (!owner || !owner->detached || owner->guest || !qa_native_guest_idle(guest) || !capabilities ||
        qa_native_guest_execution(guest) != owner->execution ||
        capabilities->id != owner->capability_id || !capabilities->milliseconds || !capabilities->performance || !capabilities->calendar ||
        (capabilities->open_file != NULL) != owner->crt->has_file_opener ||
        capabilities->standard_input.id != owner->stream_ids[0] || capabilities->standard_output.id != owner->stream_ids[1] ||
        capabilities->standard_error.id != owner->stream_ids[2] ||
        (owner->stream_ids[0] && !capabilities->standard_input.read) ||
        (owner->stream_ids[1] && !capabilities->standard_output.write) || (owner->stream_ids[2] && !capabilities->standard_error.write) ||
        capabilities->performance_frequency != owner->capabilities.performance_frequency ||
        (capabilities->locale.source == 2) != (capabilities->compare_string != NULL) ||
        !qa_native_windows_locale_profile_equal(&capabilities->locale,&owner->capabilities.locale) ||
        guest->options.image.target.os != owner->target.os || guest->options.image.target.arch != owner->target.arch ||
        guest->options.image.target.abi != owner->target.abi || guest->options.image.target.pointer_bytes != owner->target.pointer_bytes)
        return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows cold attach requires its exact completed guest and retained capability identity");
    owner->guest = guest; owner->capabilities = *capabilities;
    /* No processor or RAM write occurs. Source-modified IAT/TEB/TLS values stay
     * authoritative; owner allocations and immutable callback identities prove
     * the retained process. */
    if (!records_valid(owner,error) || !restored_storage(owner,error) ||
        !guest_runtime_resources_rebind(owner->resources,capabilities->resolve_file,capabilities->context,error) ||
        !guest_runtime_imports_attach(owner->imports,guest,error)) return false;
    owner->detached = false; owner->publication_pending = true; return true;
}

bool guest_windows_adopt(guest_windows *owner, guest_windows *previous, qa_error *error)
{
    if (!owner || owner->detached || !owner->publication_pending || owner->busy || !qa_native_guest_idle(owner->guest) ||
        !guest_runtime_imports_idle(owner->imports) || (previous && !guest_windows_idle(previous)))
        return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows adoption requires actual quiescent candidate and previous owner");
    if (!guest_runtime_resources_adopt(owner->resources,previous ? previous->resources : NULL,error)) return false;
    owner->publication_pending = false;
    if (previous) previous->retired = true;
    return true;
}

bool guest_windows_discard(guest_windows **pointer, qa_error *error)
{
    if (!pointer || !*pointer) return true;
    guest_windows *owner = *pointer;
    if (!owner->detached || owner->guest || owner->busy) return guest_fail(error,QA_ERROR_ARGUMENT,0,"only an unentered detached Windows candidate may be discarded");
    if (!guest_runtime_imports_destroy(&owner->imports,error)) return false;
    guest_runtime_resources_abandon(&owner->resources); dispose(owner); *pointer = NULL; return true;
}
