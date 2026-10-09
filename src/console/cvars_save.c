#include "cvars_private.h"
#include "qa/cvars_save.h"
#include "qa/console_cvars_prepare.h"
#include "qa/source_save.h"
#include "save_fields.h"
#include <stdlib.h>
#include <string.h>

typedef struct saved_detail {
    const char *binding, *value, *latch;
    uint32_t dialect;
    struct saved_detail *next;
} saved_detail;
typedef struct saved_cvar {
    const char *name, *value, *latch;
    bool metadata, explicit_value, pending_explicit;
    saved_detail *details;
    struct saved_cvar *next;
} saved_cvar;

struct qa_cvars_restore {
    qa_cvars *registry;
    qa_cvars_edit *edit;
    saved_cvar *rows;
    bool canonical_metadata;
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

static bool gameplay_row(const cvar *row)
{
    return !row->view.player_scoped && row->view.save_policy==QA_CVAR_SAVE_GAMEPLAY &&
        (row->catalog_row==QA_CVAR_CATALOG_NO_ROW || !qa_cvar_catalog_rows[row->catalog_row].not_stored);
}

static bool detail_fields(qa_source_save_io *io,saved_detail *detail)
{
    return qac_save_text(io,&detail->binding) && qa_source_save_u32(io,&detail->dialect) &&
        qac_save_text(io,&detail->value) && qac_save_text(io,&detail->latch);
}

static bool write_domains(qa_source_save_io *io,const cvar_values *values,size_t count)
{
    char magic[4]={'C','D','O','M'};
    if (!qa_source_save_bytes(io,magic,4) || !qa_source_save_count(io,&count,SIZE_MAX)) return false;
    for (const cvar *row=values->first;row;row=row->next) {
        if (!gameplay_row(row)) continue;
        const char *name=row->view.name;
        bool explicit_value=row->view.explicit_value,pending_explicit=row->pending_explicit;
        size_t details=0;
        for (const cvar_detail *detail=row->details;detail;detail=detail->next)
            if (detail->value || detail->latched_value) ++details;
        if (!qac_save_text(io,&name) || !qa_source_save_bool(io,&explicit_value) ||
            !qa_source_save_bool(io,&pending_explicit) || !qa_source_save_count(io,&details,SIZE_MAX)) return false;
        for (const cvar_detail *detail=row->details;detail;detail=detail->next) {
            if (!detail->value && !detail->latched_value) continue;
            saved_detail state={.binding=qa_cvar_catalog_string(detail->binding->name),
                .value=detail->value,.latch=detail->latched_value,.dialect=(uint32_t)detail->dialect};
            if (!detail_fields(io,&state)) return false;
        }
    }
    return true;
}

bool qa_cvars_save_capture(const qa_cvars *registry,qa_buffer *out,qa_error *error)
{
    if (!qa_cvars_observer_idle(registry) || registry->store->edit || !out)
        return qac_fail(error,QA_ERROR_ARGUMENT,"cvar capture requires returned canonical values and output");
    for (const qa_cvars *view=registry->store->views;view;view=view->next_view)
        if (!qa_cvars_observer_idle(view)) return qac_fail(error,QA_ERROR_ARGUMENT,"cvar capture requires all returned Source views");
    *out=(qa_buffer){0};
    uint64_t revision=registry->store->revision;
    const cvar_values *values=&registry->store->values;
    size_t count=0;
    for (const cvar *row=values->first;row;row=row->next) if (gameplay_row(row)) ++count;
    uint32_t dialect=(uint32_t)registry->store->active_dialect;
    qa_source_save_io io;
    if (!qa_source_save_writer(&io,NULL,error)) return false;
    bool okay=header(&io,&dialect,&count);
    for (const cvar *row=values->first;okay && row;row=row->next) {
        if (!gameplay_row(row)) continue;
        saved_cvar state={.name=row->view.name,.value=row->view.value,.latch=row->view.latched_value};
        okay=fields(&io,&state);
    }
    if (okay) okay=write_domains(&io,values,count);
    if (okay && revision!=registry->store->revision)
        okay=qac_fail(error,QA_ERROR_ARGUMENT,"canonical cvars changed during capture");
    if (okay) okay=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}

static bool read_domains(qa_source_save_io *io,saved_cvar *rows,size_t expected)
{
    char magic[4]; size_t count=0;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"CDOM",4) ||
        !qa_source_save_count(io,&count,expected) || count!=expected)
        return qac_fail(io->error,QA_ERROR_FORMAT,"invalid canonical cvar domain metadata");
    for (size_t i=0;i<count;++i) {
        const char *name=NULL;
        if (!qac_save_text(io,&name)) return false;
        saved_cvar *row=rows;
        while (row && (!name || !qac_equal(row->name,name))) row=row->next;
        free((char *)name);
        if (!row || row->metadata) return qac_fail(io->error,QA_ERROR_FORMAT,"canonical cvar metadata name differs");
        row->metadata=true;
        size_t details=0;
        if (!qa_source_save_bool(io,&row->explicit_value) || !qa_source_save_bool(io,&row->pending_explicit) ||
            !qa_source_save_count(io,&details,(io->input.size-io->offset)/4)) return false;
        saved_detail **tail=&row->details;
        for (size_t d=0;d<details;++d) {
            saved_detail *detail=calloc(1,sizeof(*detail));
            if (!detail) return qac_fail(io->error,QA_ERROR_MEMORY,"allocating canonical cvar detail");
            *tail=detail; tail=&detail->next;
            if (!detail_fields(io,detail) || !detail->binding ||
                !qac_dialect_valid((qa_console_dialect)detail->dialect))
                return qac_fail(io->error,QA_ERROR_FORMAT,"invalid canonical cvar detail");
            for (const saved_detail *prior=row->details;prior!=detail;prior=prior->next)
                if (prior->dialect==detail->dialect && qac_equal(prior->binding,detail->binding))
                    return qac_fail(io->error,QA_ERROR_FORMAT,"duplicate canonical cvar detail");
        }
    }
    return true;
}

static bool restore_domain(qa_cvars_restore *state,const saved_cvar *saved,qa_error *error)
{
    cvar *row=qac_cvars_find_values(state->registry,&state->edit->values,saved->name);
    if (!row || !qac_equal(row->view.name,saved->name))
        return qac_fail(error,QA_ERROR_FORMAT,"domain metadata needs a canonical cvar name");
    if (row->view.save_policy==QA_CVAR_SAVE_SETTING) return true;
    row->view.explicit_value=saved->explicit_value; row->pending_explicit=saved->pending_explicit;
    while (row->details) { cvar_detail *detail=row->details; row->details=detail->next;
        free(detail->value); free(detail->latched_value); free(detail); }
    cvar_detail **tail=&row->details;
    for (const saved_detail *saved_domain=saved->details;saved_domain;saved_domain=saved_domain->next) {
        const qa_cvar_catalog_binding *binding=NULL;
        for (size_t i=0;i<qa_cvar_catalog_binding_count;++i)
            if (qac_equal(qa_cvar_catalog_string(qa_cvar_catalog_bindings[i].name),saved_domain->binding) &&
                qa_cvar_catalog_bindings[i].row_index==row->catalog_row &&
                qa_cvar_catalog_bindings[i].seat==row->catalog_binding->seat) { binding=&qa_cvar_catalog_bindings[i]; break; }
        if (!binding) return qac_fail(error,QA_ERROR_FORMAT,"canonical cvar detail has a foreign alias");
        cvar_detail *detail=calloc(1,sizeof(*detail));
        if (!detail) return qac_fail(error,QA_ERROR_MEMORY,"restoring canonical cvar detail");
        detail->binding=binding; detail->dialect=(qa_console_dialect)saved_domain->dialect;
        detail->value=saved_domain->value?qac_copy(saved_domain->value,error):NULL;
        detail->latched_value=saved_domain->latch?qac_copy(saved_domain->latch,error):NULL;
        *tail=detail; tail=&detail->next;
        if ((saved_domain->value && !detail->value) || (saved_domain->latch && !detail->latched_value)) return false;
    }
    return true;
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
    if (okay && count > bytes.size - io.offset)
        okay = qac_fail(error, QA_ERROR_FORMAT, "saved gameplay cvar extent differs");
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
    bool canonical_metadata=okay && io.offset<bytes.size;
    state->canonical_metadata=canonical_metadata;
    if (canonical_metadata) okay=read_domains(&io,state->rows,count);
    if (okay) okay = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (okay) okay = qa_cvars_edit_prepare(registry, &state->edit, error);
    for (const saved_cvar *row=state->rows;okay && row;row=row->next) {
        const qa_cvar_view *actual=qa_cvars_edit_canonical_record(state->edit,row->name);
        if (actual && actual->save_policy==QA_CVAR_SAVE_SETTING) continue;
        okay=qac_cvars_restore_row(state->edit,row->name,row->value,row->latch,
            (qa_console_dialect)dialect,canonical_metadata,error);
        actual=okay?qa_cvars_edit_canonical_record(state->edit,row->name):NULL;
        if (okay && actual->save_policy==QA_CVAR_SAVE_UNCLASSIFIED)
            okay=qa_cvars_edit_apply(state->edit,&(qa_cvars_edit_command){
                .kind=QA_CVARS_EDIT_SAVE_POLICY,.name=row->name,.save_policy=QA_CVAR_SAVE_GAMEPLAY},error);
        if (okay && row->metadata) okay=restore_domain(state,row,error);
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

bool qa_cvars_save_matches(qa_cvars *registry,qa_bytes bytes,qa_error *error)
{
    qa_cvars_restore *state=NULL;
    if (!qa_cvars_save_prepare(registry,bytes,&state,error)) return false;
    bool okay=true;
    for (const cvar *row=registry->store->values.first;okay && row;row=row->next) {
        if (!gameplay_row(row)) continue;
        const cvar *prepared=qac_cvars_find_values(registry,&state->edit->values,row->view.name);
        const char *latch=row->view.latched_value;
        okay=prepared && !strcmp(prepared->view.value,row->view.value) &&
            (!state->canonical_metadata || (prepared->view.explicit_value==row->view.explicit_value && prepared->pending_explicit==row->pending_explicit)) &&
            ((latch==NULL && prepared->view.latched_value==NULL) ||
             (latch && prepared->view.latched_value && !strcmp(latch,prepared->view.latched_value)));
        for (const cvar_detail *detail=row->details;state->canonical_metadata && okay && detail;detail=detail->next) {
            const cvar_detail *other=prepared->details;
            while (other && (other->binding!=detail->binding || other->dialect!=detail->dialect)) other=other->next;
            okay=other && ((!detail->value && !other->value) || (detail->value && other->value && !strcmp(detail->value,other->value))) &&
                ((!detail->latched_value && !other->latched_value) || (detail->latched_value && other->latched_value && !strcmp(detail->latched_value,other->latched_value)));
        }
    }
    qa_cvars_save_abort(state);
    return okay || qac_fail(error,QA_ERROR_FORMAT,"canonical gameplay cvars differ from saved state");
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
        while (row->details) { saved_detail *detail=row->details; row->details=detail->next;
            free((char *)detail->binding); free((char *)detail->value); free((char *)detail->latch); free(detail); }
        free((char *)row->name); free((char *)row->value); free((char *)row->latch); free(row);
    }
    free(state);
}
