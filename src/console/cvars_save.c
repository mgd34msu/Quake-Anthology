#include "cvars_private.h"
#include "qa/cvars_save.h"
#include "qa/source_save.h"
#include "save_fields.h"
#include <stdlib.h>
#include <string.h>

struct qa_cvars_restore {
    qa_cvars *registry;
    cvar_values values;
    uint64_t revision;
};

bool qac_save_text(qa_source_save_io *io, const char **text)
{
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && *text != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) { if (io->direction == QA_SOURCE_SAVE_READ) *text = NULL; return true; }
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*text) : 0;
    if (!qa_source_save_count(io, &length, SIZE_MAX - 1)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, (void *)*text, length);
    if (io->offset > io->input.size || length > io->input.size - io->offset ||
        memchr(io->input.data + io->offset, 0, length))
        return qac_fail(io->error, QA_ERROR_FORMAT, "invalid cvar continuation text");
    char *copy = qac_copy_n((const char *)io->input.data + io->offset, length, io->error);
    if (!copy) return false;
    *text = copy;
    return qa_source_save_bytes(io, copy, length);
}

static bool field_list(qa_source_save_io *io, const char *const **list, size_t *count)
{
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? *count : 0;
    if (!qa_source_save_count(io, &length, SIZE_MAX / sizeof(char *))) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length > io->input.size - io->offset)
            return qac_fail(io->error, QA_ERROR_FORMAT, "invalid cvar documentation extent");
        char **owned = calloc(length ? length : 1, sizeof(*owned));
        if (!owned) return qac_fail(io->error, QA_ERROR_MEMORY, "allocating saved cvar documentation");
        *list = (const char *const *)owned;
        *count = length;
    } else if (*count && !*list)
        return qac_fail(io->error, QA_ERROR_ARGUMENT, "missing cvar documentation list");
    for (size_t i = 0; i < *count; ++i) {
        const char *text = io->direction == QA_SOURCE_SAVE_WRITE ? (*list)[i] : NULL;
        bool ok = qac_save_text(io, &text);
        if (io->direction == QA_SOURCE_SAVE_READ) ((const char **)*list)[i] = text;
        if (!ok || !text) return false;
    }
    return true;
}

bool qac_save_documentation(qa_source_save_io *io, const qa_console_documentation **out)
{
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && *out != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) { if (io->direction == QA_SOURCE_SAVE_READ) *out = NULL; return true; }
    qa_console_documentation copy = {0};
    qa_console_documentation *doc = &copy;
    if (io->direction == QA_SOURCE_SAVE_WRITE) copy = **out;
    else {
        doc = calloc(1, sizeof(*doc));
        if (!doc) return qac_fail(io->error, QA_ERROR_MEMORY, "allocating saved cvar documentation");
        *out = doc;
    }
    return qac_save_text(io, &doc->usage) && field_list(io, &doc->examples, &doc->example_count) &&
        field_list(io, &doc->allowed_values, &doc->allowed_count) &&
        qa_source_save_bool(io, &doc->has_allowed_values);
}

static bool field_entry(qa_source_save_io *io, cvar *entry)
{
    uint64_t binding_owner = io->direction == QA_SOURCE_SAVE_WRITE ? entry->binding.owner : 0;
    bool ok = qac_save_text(io, &entry->view.name) && qac_save_text(io, &entry->view.value) &&
        qac_save_text(io, &entry->view.reset_value) && qac_save_text(io, &entry->view.latched_value) &&
        qac_save_text(io, &entry->view.description) && qac_save_documentation(io, &entry->view.documentation) &&
        qa_source_save_u32(io, &entry->view.flags) && qa_source_save_u64(io, &entry->view.modification_count) &&
        qa_source_save_u64(io, &entry->view.owner) && qa_source_save_f32(io, &entry->view.number) &&
        qa_source_save_i32(io, &entry->view.integer) && qa_source_save_bool(io, &entry->view.modified) &&
        qa_source_save_bool(io, &entry->view.console_created) &&
        qa_source_save_count(io, &entry->view.handle, SIZE_MAX - 1) && qa_source_save_bool(io, &entry->bound) &&
        qa_source_save_u64(io, &binding_owner);
    if (io->direction == QA_SOURCE_SAVE_READ) entry->binding.owner = binding_owner;
    return ok;
}

static bool header(qa_source_save_io *io, uint32_t *dialect, qa_cvars_restore *state)
{
    char magic[4] = {'Q','A','C','V'};
    bool ok = qa_source_save_bytes(io, magic, sizeof(magic)) &&
        qa_source_save_u32(io, dialect) && qa_source_save_count(io, &state->values.count, SIZE_MAX / sizeof(cvar)) &&
        qa_source_save_count(io, &state->values.next_handle, SIZE_MAX) && qa_source_save_u32(io, &state->values.changed_flags) &&
        qa_source_save_bool(io, &state->values.userinfo_modified) && qa_source_save_bool(io, &state->values.server_active) &&
        qa_source_save_bool(io, &state->values.high_characters) && qa_source_save_bool(io, &state->values.cheats) &&
        qa_source_save_count(io,&state->values.alias_count,SIZE_MAX/sizeof(cvar_alias));
    if (!ok) return false;
    if (memcmp(magic, "QACV", 4) || !qac_dialect_valid((qa_console_dialect)*dialect))
        return qac_fail(io->error, QA_ERROR_FORMAT, "unsupported cvar continuation schema");
    return true;
}

static bool alias_fields(qa_source_save_io *io,cvar_alias *alias)
{
    const char *name=alias->name,*target=alias->target;
    uint32_t conversion=alias->conversion;
    bool ok=qac_save_text(io,&name) && qac_save_text(io,&target) &&
        qa_source_save_u32(io,&conversion) && qa_source_save_bool(io,&alias->vm_bound);
    if (io->direction==QA_SOURCE_SAVE_READ) {
        alias->name=(char *)name; alias->target=(char *)target;
        alias->conversion=(qa_cvar_alias_conversion)conversion;
    }
    if (ok && alias->vm_bound) ok=qa_source_save_count(io,&alias->handle,SIZE_MAX-1);
    return ok;
}

bool qa_cvars_save_capture(const qa_cvars *registry, qa_buffer *out, qa_error *error)
{
    if (!qa_cvars_observer_idle(registry) || !out)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar capture requires a complete publication drain and output");
    *out = (qa_buffer){0};
    uint64_t revision = registry->mutation_revision;
    qa_cvars_restore state = {.values=registry->values};
    uint32_t dialect = (uint32_t)registry->options.dialect;
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, NULL, error)) return false;
    bool ok = header(&io, &dialect, &state);
    size_t count = 0;
    for (const cvar *entry = registry->values.first; ok && entry; entry = entry->next) {
        cvar copy = *entry;
        ok = field_entry(&io, &copy); ++count;
    }
    size_t aliases=0;
    for (const cvar_alias *alias=registry->values.aliases;ok && alias;alias=alias->next) {
        cvar_alias copy=*alias;
        ok=alias_fields(&io,&copy); ++aliases;
    }
    if (ok && (count != registry->values.count || aliases!=registry->values.alias_count ||
        revision != registry->mutation_revision))
        ok = qac_fail(error, QA_ERROR_ARGUMENT, "cvar registry changed during capture");
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

static bool valid_entry(const qa_cvars_restore *state, const cvar *entry, qa_error *error)
{
    const qa_cvar_view *v = &entry->view;
    if (!v->value || !v->reset_value || !v->description || v->handle >= state->values.next_handle ||
        (!entry->bound && entry->binding.owner))
        return qac_fail(error, QA_ERROR_FORMAT, "invalid saved cvar identity or value");
    if (!qa_cvars_name_valid(state->registry->options.dialect, v->name))
        return qac_fail(error, QA_ERROR_FORMAT, "invalid saved cvar name");
    for (const cvar *prior = state->values.first; prior != entry; prior = prior->next)
        if (prior->view.handle == v->handle || qac_cvars_name_equal(state->registry, prior->view.name, v->name))
            return qac_fail(error, QA_ERROR_FORMAT, "duplicate saved cvar name or handle");
    for (const cvar_alias *alias=state->registry->values.aliases;alias;alias=alias->next)
        if (qac_cvars_name_equal(state->registry,alias->name,v->name))
            return qac_fail(error,QA_ERROR_FORMAT,"saved physical cvar conflicts with a declared alias");
    return true;
}

bool qa_cvars_save_prepare(qa_cvars *registry, qa_bytes bytes, qa_cvars_restore **out, qa_error *error)
{
    if (!qa_cvars_observer_idle(registry) || !out)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar restore requires a fully drained candidate registry");
    *out = NULL;
    qa_cvars_restore *state = calloc(1, sizeof(*state));
    if (!state) return qac_fail(error, QA_ERROR_MEMORY, "allocating cvar restore ticket");
    state->registry = registry; state->revision = registry->mutation_revision;
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, NULL, bytes, error)) { free(state); return false; }
    uint32_t dialect = 0;
    bool ok = header(&io, &dialect, state);
    if (ok && (dialect != (uint32_t)registry->options.dialect || state->values.count > bytes.size - io.offset ||
        state->values.alias_count!=registry->values.alias_count))
        ok = qac_fail(error, QA_ERROR_FORMAT, "saved cvar dialect or count differs from its candidate");
    cvar **tail = &state->values.first;
    for (size_t i = 0; ok && i < state->values.count; ++i) {
        cvar *entry = calloc(1, sizeof(*entry));
        if (!entry) { ok = qac_fail(error, QA_ERROR_MEMORY, "allocating restored cvar"); break; }
        *tail = entry; tail = &entry->next;
        ok = field_entry(&io, entry) && valid_entry(state, entry, error);
        if (!ok) break;
        cvar *existing = qac_cvars_find_values(registry,&registry->values,entry->view.name);
        if ((existing && existing->bound) != entry->bound || (entry->bound &&
            (existing->view.owner != entry->view.owner || existing->binding.owner != entry->binding.owner))) {
            ok = qac_fail(error, QA_ERROR_FORMAT, "saved cvar binding has no matching candidate owner"); break;
        }
        if (entry->bound) {
            entry->binding = existing->binding;
            entry->binding_order=existing->binding_order;
            if (entry->binding.validate) {
                ++registry->notifying;
                ok = entry->binding.validate(entry->binding.user, entry->view.value, error) &&
                     entry->binding.validate(entry->binding.user, entry->view.reset_value, error) &&
                     (!entry->view.latched_value || entry->binding.validate(entry->binding.user, entry->view.latched_value, error));
                --registry->notifying;
            }
        }
        if (ok) ok=qac_cvars_index_reserve(registry,&state->values,i+1,error);
        if (ok) qac_cvars_index_entry(registry,&state->values,entry);
    }
    for (cvar *existing = registry->values.first; ok && existing; existing = existing->next) {
        if (!existing->bound) continue;
        cvar *saved = qac_cvars_find_values(registry,&state->values,existing->view.name);
        if (!saved) ok = qac_fail(error, QA_ERROR_FORMAT, "restore would discard an actual cvar binding");
    }
    const cvar_alias *actual=registry->values.aliases;
    for (size_t i=0;ok && i<state->values.alias_count;++i) {
        cvar_alias saved={0};
        ok=alias_fields(&io,&saved);
        if (ok && (!actual || !saved.name || !saved.target || strcmp(saved.name,actual->name) ||
            strcmp(saved.target,actual->target) || saved.conversion!=actual->conversion ||
            (saved.vm_bound && (saved.conversion==QA_CVAR_ALIAS_IDENTITY ||
                saved.handle>=state->values.next_handle || saved.handle>=1024))))
            ok=qac_fail(error,QA_ERROR_FORMAT,"saved alias differs from its actual factory declaration");
        const cvar *target=ok?qac_cvars_find_values(registry,&state->values,saved.target):NULL;
        if (ok && saved.vm_bound && !target)
            ok=qac_fail(error,QA_ERROR_FORMAT,"saved alias lacks its canonical target");
        for (const cvar *entry=state->values.first;ok && saved.vm_bound && entry;entry=entry->next)
            if (entry->view.handle==saved.handle)
                ok=qac_fail(error,QA_ERROR_FORMAT,"saved alias handle conflicts with a physical cvar");
        for (const cvar_alias *prior=state->values.aliases;ok && saved.vm_bound && prior;prior=prior->next)
            if (prior->vm_bound && prior->handle==saved.handle)
                ok=qac_fail(error,QA_ERROR_FORMAT,"duplicate saved alias handle");
        if (ok) ok=qac_cvars_index_reserve(registry,&state->values,state->values.count+i+1,error);
        cvar_alias *copy=ok?qac_cvars_alias_copy(actual,error):NULL;
        if (ok && !copy) ok=false;
        if (copy) {
            copy->vm_bound=saved.vm_bound; copy->handle=saved.handle;
            if (state->values.last_alias) state->values.last_alias->next=copy;
            else state->values.aliases=copy;
            state->values.last_alias=copy;
            qac_cvars_index_alias(registry,&state->values,copy);
        }
        free(saved.name); free(saved.target);
        if (actual) actual=actual->next;
    }
    if (ok) ok = qa_source_save_finish(&io, NULL) && qa_cvars_save_validate(state, error);
    qa_source_save_dispose(&io);
    if (!ok) { qa_cvars_save_abort(state); return false; }
    *out = state; return true;
}

bool qa_cvars_save_validate(const qa_cvars_restore *state, qa_error *error)
{
    if (!state || !qa_cvars_observer_idle(state->registry) ||
        state->registry->mutation_revision != state->revision || state->revision == UINT64_MAX)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar restore ticket is stale");
    return true;
}
const qa_cvar_view *qa_cvars_save_find(const qa_cvars_restore *state, const char *name)
{
    const cvar *entry=state?qac_cvars_find_values(state->registry,&state->values,name):NULL;
    return entry?&entry->view:NULL;
}

bool qa_cvars_save_commit(qa_cvars_restore *state, qa_error *error)
{
    if (!qa_cvars_save_validate(state, error)) return false;
    qa_cvars *registry = state->registry;
    cvar_values previous=registry->values;
    registry->values=state->values; state->values=(cvar_values){0};
    ++registry->mutation_revision;
    qac_cvars_values_free(&previous); free(state); return true;
}
void qa_cvars_save_abort(qa_cvars_restore *state)
{ if (state) { qac_cvars_values_free(&state->values); free(state); } }
