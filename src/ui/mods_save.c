#include "mods_internal.h"
#include "qa/ui_menu_save.h"
#include "qa/source_save.h"
#include <stdio.h>

#define FIELD(type, name) do { if (!qa_source_save_##type(io, &saved->name)) return false; } while (0)
static bool blob(qa_source_save_io *io, qa_buffer *value)
{
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX;
    if (!qa_source_save_count(io, &value->size, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && value->size) {
        value->data = malloc(value->size);
        if (!value->data) return ui_fail(io->error, "Allocating retained mod menu bytes");
    }
    return qa_source_save_bytes(io, value->data, value->size);
}
static bool text(qa_source_save_io *io, const char **value, char **owned)
{
    bool present = *value != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) { if (io->direction == QA_SOURCE_SAVE_READ) *value = NULL; return true; }
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*value) : 0;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX - 1;
    if (!qa_source_save_count(io, &length, maximum) || length == SIZE_MAX) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)*value, length);
    *owned = malloc(length + 1);
    if (!*owned) return ui_fail(io->error, "Allocating retained mod menu string");
    if (!qa_source_save_bytes(io, *owned, length)) return false;
    if (memchr(*owned, 0, length)) {
        qa_error_set(io->error, QA_ERROR_FORMAT, 0, "Retained mod menu text contains NUL"); return false;
    }
    (*owned)[length] = 0; *value = *owned; return true;
}
static void release(qa_ui_mods *saved)
{
    qa_launch_draft_destroy(saved->draft); qa_catalog_release(saved->catalog);
    qa_buffer_free(&saved->query_lower); ui_mods_cache_release(saved);
    free(saved->rows); free(saved->selections);
}
static bool same_text(const char *a, const char *b) { return (!a || !b) ? a == b : !strcmp(a, b); }
static bool row_matches(const qa_ui_mods *mods, const qa_catalog_mod *mod,
    const qa_launch_mod_selection *selection, size_t *index, qa_error *error)
{
    const char *key = mod ? mod->key : selection->component;
    const char *title = mod ? mod->title : key;
    const qa_product *product = mod ? qa_catalog_product(mods->catalog, mod->product) : NULL;
    bool matched;
    if (!ui_search(title, key, product ? product->title : "",
        (qa_bytes){mods->query_lower.data, mods->query_lower.size}, &matched, error)) return false;
    if (!matched) return true;
    if (*index >= mods->count) return false;
    const mod_row *row = &mods->selections[*index];
    const qa_ui_row *view = &mods->rows[*index];
    char state[96];
    snprintf(state, sizeof(state), "%s%s%s", selection && selection->enabled ? "Enabled" :
        mod && !mod->unavailable ? "Disabled" : "Unavailable", selection ? " / " : "", selection ? selection->instance : "");
    if (!same_text(row->component, key) || !same_text(row->instance, selection ? selection->instance : NULL) ||
        row->enabled != (selection && selection->enabled) || strcmp(row->state, state) ||
        !same_text(view->key, selection ? selection->instance : key) || !same_text(view->label, title) ||
        view->detail != row->state || !view->enabled || view->image) return false;
    ++*index; return true;
}
static bool cache_matches(const qa_ui_mods *mods, qa_error *error)
{
    if (mods->dirty) return true;
    const qa_launch_choices *choices = qa_launch_draft_choices(mods->draft);
    size_t index = 0;
    for (size_t i = 0; i < choices->mod_count; ++i) {
        const qa_launch_mod_selection *selection = &choices->mods[i];
        const qa_catalog_mod *mod = qa_catalog_mod_find(mods->catalog, selection->component);
        if ((!mod || mod->purpose == QA_MOD_ADDITION) && !row_matches(mods, mod, selection, &index, error)) return false;
    }
    for (size_t i = 0; i < qa_catalog_mod_count(mods->catalog); ++i) {
        const qa_catalog_mod *mod = qa_catalog_mod_at(mods->catalog, i);
        if (mod->purpose != QA_MOD_ADDITION) continue;
        bool present = false;
        for (size_t j = 0; j < choices->mod_count; ++j) present |= !strcmp(choices->mods[j].component, mod->key);
        if (!present && !row_matches(mods, mod, NULL, &index, error)) return false;
    }
    return index == mods->count;
}
static bool fields(qa_source_save_io *io, qa_ui_mods *saved, const qa_ui_mods *qualified,
    const qa_ui_menu_checkpoint_refs *refs)
{
    uint8_t magic[4] = {'Q','M','O','D'}; uint32_t seat = qualified->ui->options.seat;
    uint64_t menu = qualified->menu, catalog = 0;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QMOD", 4) ||
        !qa_source_save_u32(io, &seat) || seat != qualified->ui->options.seat ||
        !qa_source_save_u64(io, &menu) || menu != qualified->menu) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE &&
        !refs->catalog_encode(refs->context, saved->catalog, &catalog, io->error)) return false;
    if (!qa_source_save_u64(io, &catalog)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        qa_catalog *actual = NULL;
        if (!refs->catalog_decode(refs->context, catalog, &actual, io->error) || !actual) return false;
        qa_catalog_retain(actual); saved->catalog = actual;
    }
    qa_buffer draft = {0};
    const qa_actor_registry *actors = qa_session_actors(qa_application_session(qualified->application));
    bool ok = io->direction == QA_SOURCE_SAVE_READ || qa_launch_draft_checkpoint(saved->draft, actors, &draft, io->error);
    if (ok) ok = blob(io, &draft);
    if (ok && io->direction == QA_SOURCE_SAVE_READ)
        ok = qa_launch_draft_restore(saved->catalog, actors, (qa_bytes){draft.data, draft.size}, &saved->draft, io->error);
    qa_buffer_free(&draft);
    if (!ok) return false;
    FIELD(bool, dirty); FIELD(u64, revision); FIELD(u64, configuration_generation);
    if (!qa_source_save_count(io, &saved->selected, SIZE_MAX) ||
        !qa_source_save_bytes(io, saved->query, sizeof(saved->query)) || !memchr(saved->query, 0, sizeof(saved->query)) ||
        !qa_source_save_bytes(io, saved->status, sizeof(saved->status)) || !memchr(saved->status, 0, sizeof(saved->status)) ||
        !qa_source_save_bytes(io, saved->display, sizeof(saved->display)) || !memchr(saved->display, 0, sizeof(saved->display)) ||
        !blob(io, &saved->query_lower)) return false;
    /* Dirty caches can borrow a discarded draft/catalog. Every UI action and
     * draw reacquires the factory, which resets these rows before consumption. */
    if (saved->dirty) { if (io->direction == QA_SOURCE_SAVE_READ) saved->count = 0; return true; }
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / 100 : SIZE_MAX;
    size_t count = saved->count;
    if (maximum > SIZE_MAX / sizeof(*saved->selections)) maximum = SIZE_MAX / sizeof(*saved->selections);
    if (maximum > SIZE_MAX / sizeof(*saved->rows)) maximum = SIZE_MAX / sizeof(*saved->rows);
    if (!qa_source_save_count(io, &count, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && count) {
        saved->rows = calloc(count, sizeof(*saved->rows));
        saved->selections = calloc(count, sizeof(*saved->selections));
        if (!saved->rows || !saved->selections) {
            return ui_fail(io->error, "Allocating retained mod menu rows");
        }
        saved->capacity = saved->selection_capacity = count;
    }
    if (io->direction == QA_SOURCE_SAVE_READ) saved->count = count;
    for (size_t i = 0; i < saved->count; ++i) {
        mod_row row = io->direction == QA_SOURCE_SAVE_WRITE ? saved->selections[i] : (mod_row){0};
        qa_ui_row view = io->direction == QA_SOURCE_SAVE_WRITE ? saved->rows[i] : (qa_ui_row){0};
        if (io->direction == QA_SOURCE_SAVE_READ) saved->selections[i] = row;
        mod_row *target = io->direction == QA_SOURCE_SAVE_READ ? &saved->selections[i] : &row;
        if (!text(io, &target->component, &target->retained_component) || !target->component || !*target->component ||
            !text(io, &target->instance, &target->retained_instance) ||
            !text(io, &view.label, &target->retained_label) || !view.label ||
            !qa_source_save_bool(io, &target->enabled) || !qa_source_save_bytes(io, target->state, sizeof(target->state)) ||
            !memchr(target->state, 0, sizeof(target->state)) || !qa_source_save_bool(io, &view.enabled)) return false;
        if (io->direction == QA_SOURCE_SAVE_WRITE) {
            const char *key = row.instance ? row.instance : row.component;
            if (!view.key || strcmp(view.key, key) || view.detail != saved->selections[i].state || view.image) return false;
        } else {
            view.key = target->instance ? target->instance : target->component;
            view.detail = target->state; saved->rows[i] = view;
        }
    }
    return cache_matches(saved, io->error);
}
bool qa_ui_mods_checkpoint(const qa_ui_mods *mods, const qa_ui_menu_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!mods || !refs || !refs->catalog_encode || !out || out->data || out->size ||
        mods->ui->handling || mods->ui->drawing || !mods->draft || !mods->catalog ||
        qa_launch_draft_catalog(mods->draft) != mods->catalog)
        return ui_fail(error, "Mod menu capture requires idle actual draft and catalog owners");
    qa_ui_mods saved = *mods; qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, &saved, mods, refs) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK)
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Mod menu continuation leaves its actual source field domains");
    return ok;
}
bool qa_ui_mods_restore(qa_ui_mods *mods, const qa_ui_menu_checkpoint_refs *refs, qa_bytes bytes, qa_error *error)
{
    if (!mods || !refs || !refs->catalog_decode || mods->ui->handling || mods->ui->drawing)
        return ui_fail(error, "Mod menu restore requires idle actual controller and catalog resolver");
    qa_ui_mods saved = {.ui = mods->ui, .application = mods->application, .menu = mods->menu};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, &saved, mods, refs) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) {
        release(&saved);
        if (error && error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, 0, "Unqualified mod menu continuation");
        return false;
    }
    qa_ui_mods displaced = *mods; *mods = saved; release(&displaced); return true;
}
