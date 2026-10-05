#include "cvars_private.h"
#include "qa/cvars_save.h"
#include "qa/console_cvars_prepare.h"
#include "qa/source_save.h"
#include "save_fields.h"
#include <stdlib.h>
#include <string.h>

typedef struct saved_cvar {
    const char *name, *value, *latch;
    struct saved_cvar *next;
} saved_cvar;

struct qa_cvars_restore {
    qa_cvars *registry;
    qa_cvars_edit *edit;
    saved_cvar *rows;
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

static bool fields(qa_source_save_io *io, saved_cvar *row)
{
    return qac_save_text(io, &row->name) && qac_save_text(io, &row->value) &&
        qac_save_text(io, &row->latch);
}

static bool header(qa_source_save_io *io, uint32_t *dialect, size_t *count)
{
    char magic[4] = {'Q','A','C','V'};
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) ||
        !qa_source_save_u32(io, dialect) ||
        !qa_source_save_count(io, count, SIZE_MAX / sizeof(saved_cvar))) return false;
    return (!memcmp(magic, "QACV", 4) && qac_dialect_valid((qa_console_dialect)*dialect)) ||
        qac_fail(io->error, QA_ERROR_FORMAT, "invalid gameplay cvar state");
}

bool qa_cvars_save_capture(const qa_cvars *registry, qa_buffer *out, qa_error *error)
{
    if (!qa_cvars_observer_idle(registry) || !out)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar capture requires a drained registry and output");
    *out = (qa_buffer){0};
    uint64_t revision = registry->mutation_revision;
    size_t count = 0;
    for (const cvar *row = registry->values.first; row; row = row->next)
        if (row->view.save_policy == QA_CVAR_SAVE_GAMEPLAY) ++count;
    uint32_t dialect = (uint32_t)registry->options.dialect;
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, NULL, error)) return false;
    bool okay = header(&io, &dialect, &count);
    for (const cvar *row = registry->values.first; okay && row; row = row->next) {
        if (row->view.save_policy != QA_CVAR_SAVE_GAMEPLAY) continue;
        saved_cvar state = {row->view.name, row->view.value, row->view.latched_value, NULL};
        okay = fields(&io, &state);
    }
    if (okay && revision != registry->mutation_revision)
        okay = qac_fail(error, QA_ERROR_ARGUMENT, "cvar registry changed during capture");
    if (okay) okay = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return okay;
}

bool qa_cvars_save_prepare(qa_cvars *registry, qa_bytes bytes, qa_cvars_restore **out, qa_error *error)
{
    if (!qa_cvars_observer_idle(registry) || !out)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar restore requires a drained current factory registry");
    *out = NULL;
    qa_cvars_restore *state = calloc(1, sizeof(*state));
    if (!state) return qac_fail(error, QA_ERROR_MEMORY, "allocating gameplay cvar restore");
    state->registry = registry;
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, NULL, bytes, error)) { free(state); return false; }
    uint32_t dialect = 0;
    size_t count = 0;
    bool okay = header(&io, &dialect, &count);
    if (okay && (dialect != (uint32_t)registry->options.dialect || count > bytes.size - io.offset))
        okay = qac_fail(error, QA_ERROR_FORMAT, "saved gameplay cvar dialect or extent differs");
    saved_cvar **tail = &state->rows;
    for (size_t i = 0; okay && i < count; ++i) {
        saved_cvar *row = calloc(1, sizeof(*row));
        if (!row) { okay = qac_fail(error, QA_ERROR_MEMORY, "allocating saved gameplay cvar"); break; }
        *tail = row; tail = &row->next;
        okay = fields(&io, row);
        if (okay && (!row->value || !qa_cvars_name_valid(registry->options.dialect, row->name)))
            okay = qac_fail(error, QA_ERROR_FORMAT, "invalid saved gameplay cvar name or value");
        for (const saved_cvar *prior = state->rows; okay && prior != row; prior = prior->next)
            if (qac_cvars_name_equal(registry, prior->name, row->name))
                okay = qac_fail(error, QA_ERROR_FORMAT, "duplicate saved gameplay cvar");
    }
    if (okay) okay = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (okay) okay = qa_cvars_edit_prepare(registry, &state->edit, error);
    for (const saved_cvar *row = state->rows; okay && row; row = row->next) {
        const qa_cvar_view *actual = qa_cvars_edit_canonical_record(state->edit, row->name);
        /* Removed declarations are absent; current settings retain their current
         * values even when a name formerly belonged to gameplay. */
        if (!actual || !qac_cvars_name_equal(registry, actual->name, row->name) || actual->save_policy != QA_CVAR_SAVE_GAMEPLAY) continue;
        okay = qa_cvars_edit_apply(state->edit, &(qa_cvars_edit_command){
            .kind = QA_CVARS_EDIT_SET, .name = row->name, .value = row->value, .force = true}, error);
        actual = okay ? qa_cvars_edit_canonical_record(state->edit, row->name) : NULL;
        if (okay && (row->latch || actual->latched_value))
            okay = qa_cvars_edit_apply(state->edit, &(qa_cvars_edit_command){
                .kind = QA_CVARS_EDIT_STAGE, .name = row->name,
                .value = row->latch ? row->latch : row->value}, error);
    }
    if (okay) okay = qa_cvars_edit_ready(state->edit, error);
    if (!okay) { qa_cvars_save_abort(state); return false; }
    *out = state;
    return true;
}

bool qa_cvars_save_validate(const qa_cvars_restore *state, qa_error *error)
{
    return (state && qa_cvars_edit_ready_is(state->edit)) ||
        qac_fail(error, QA_ERROR_ARGUMENT, "gameplay cvar restore lost its current prepared registry");
}

const qa_cvar_view *qa_cvars_save_find(const qa_cvars_restore *state, const char *name)
{ return state && state->edit ? qa_cvars_edit_find(state->edit, name) : NULL; }

bool qa_cvars_save_commit(qa_cvars_restore *state, qa_error *error)
{
    if (!qa_cvars_save_validate(state, error)) return false;
    qa_cvars_edit_publish(state->edit);
    state->edit = NULL;
    if (!qa_cvars_edit_finish(state->registry, error)) return false;
    qa_cvars_save_abort(state);
    return true;
}

void qa_cvars_save_abort(qa_cvars_restore *state)
{
    if (!state) return;
    qa_cvars_edit_abort(state->edit);
    while (state->rows) {
        saved_cvar *row = state->rows;
        state->rows = row->next;
        free((char *)row->name); free((char *)row->value); free((char *)row->latch); free(row);
    }
    free(state);
}
