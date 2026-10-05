#include "mods_internal.h"
#include <stdio.h>

void ui_mods_cache_release(qa_ui_mods *mods) {
    for (size_t i = 0; i < mods->count; ++i) {
        free(mods->selections[i].retained_component);
        free(mods->selections[i].retained_instance);
        free(mods->selections[i].retained_label);
    }
    mods->count = 0;
}

enum { MOD_SEARCH = 1, MOD_LIST, MOD_TOGGLE, MOD_APPLY, MOD_CANCEL, MOD_REFRESH, MOD_STATUS };
bool qa_ui_mods_cancel(qa_ui_mods *mods, qa_error *error) {
    if (!mods || mods->ui->drawing) return ui_fail(error, "invalid mod menu edit");
    const qa_launch_snapshot *snapshot = qa_application_launch(mods->application);
    qa_launch_draft *draft = NULL;
    if (!snapshot || !qa_launch_snapshot_draft_copy(snapshot, &draft, error))
        return snapshot ? false : ui_fail(error, "select a game before editing mods");
    qa_catalog *catalog = qa_launch_draft_catalog(draft);
    qa_catalog_retain(catalog);
    qa_launch_draft_destroy(mods->draft);
    qa_catalog_release(mods->catalog);
    mods->draft = draft;
    mods->catalog = catalog;
    mods->status[0] = 0;
    mods->dirty = true;
    mods->configuration_generation = qa_application_configuration_generation(mods->application);
    return true;
}
bool qa_ui_mods_apply(qa_ui_mods *mods, qa_error *error) {
    if (!mods || mods->ui->drawing || !mods->draft) return ui_fail(error, "missing staged mod selection");
    if (mods->configuration_generation != qa_application_configuration_generation(mods->application)) {
        snprintf(mods->status, sizeof(mods->status), "Configuration changed; Cancel reloads current choices");
        return ui_fail(error, mods->status);
    }
    if (!qa_application_apply(mods->application, mods->draft, error)) {
        snprintf(mods->status, sizeof(mods->status), "%s", error ? error->message : "Mod selection rejected");
        return false;
    }
    mods->configuration_generation = qa_application_configuration_generation(mods->application);
    snprintf(mods->status, sizeof(mods->status), "Mod selection applied");
    return true;
}
bool qa_ui_mods_refresh(qa_ui_mods *mods, qa_error *error) {
    if (!mods || mods->ui->drawing || !mods->draft) return ui_fail(error, "missing mod refresh draft");
    if (mods->configuration_generation != qa_application_configuration_generation(mods->application))
        return ui_fail(error, "Configuration changed; Cancel reloads current choices before refresh");
    if (!qa_application_rediscover(mods->application, true, error)) return false;
    qa_catalog *catalog = qa_application_catalog(mods->application);
    qa_launch_draft *draft;
    if (!qa_launch_draft_rebase(mods->draft, catalog, &draft, error)) return false;
    qa_catalog_retain(catalog);
    qa_launch_draft_destroy(mods->draft);
    qa_catalog_release(mods->catalog);
    mods->draft = draft;
    mods->catalog = catalog;
    mods->dirty = true;
    snprintf(mods->status, sizeof(mods->status), "Content refreshed; staged choices retained");
    return true;
}
static bool row_add(qa_ui_mods *mods, const qa_catalog_mod *mod,
                     const qa_launch_mod_selection *selection, qa_error *error) {
    const char *key = mod ? mod->key : selection->component;
    const char *title = mod ? mod->title : key;
    const qa_product *product = mod ? qa_catalog_product(mods->catalog, mod->product) : NULL;
    bool matched;
    if (!ui_search(title, key, product ? product->title : "",
        (qa_bytes){mods->query_lower.data, mods->query_lower.size}, &matched, error)) return false;
    if (!matched) return true;
    if (!ui_reserve((void **)&mods->rows, &mods->capacity, mods->count + 1, sizeof(*mods->rows), error) ||
        !ui_reserve((void **)&mods->selections, &mods->selection_capacity, mods->count + 1,
                    sizeof(*mods->selections), error)) return false;
    size_t i = mods->count++;
    mods->selections[i] = (mod_row){.component = key,
        .instance = selection ? selection->instance : NULL, .enabled = selection && selection->enabled};
    snprintf(mods->selections[i].state, sizeof(mods->selections[i].state), "%s%s%s",
        selection && selection->enabled ? "Enabled" : mod && !mod->unavailable ? "Disabled" : "Unavailable",
        selection ? " / " : "", selection ? selection->instance : "");
    mods->rows[i] = (qa_ui_row){.key = selection ? selection->instance : key, .label = title,
        .detail = mods->selections[i].state, .enabled = true};
    return true;
}
static bool toggle(qa_ui_mods *mods, size_t index, qa_error *error) {
    if (index >= mods->count) return true;
    mod_row row = mods->selections[index];
    const qa_catalog_mod *mod = qa_catalog_mod_find(mods->catalog, row.component);
    if (!row.enabled && (!mod || mod->unavailable)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "%s", mod && mod->unavailable ? mod->unavailable : "Mod is unavailable");
        return false;
    }
    char *instance = NULL;
    if (!row.instance) {
        size_t length = strlen(row.component);
        if (length > SIZE_MAX - 10) return ui_fail(error, "mod key too long");
        instance = malloc(length + 10);
        if (!instance) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating mod instance key"); return false; }
        snprintf(instance, length + 10, "ui:addon:%s", row.component);
    }
    qa_launch_mod_selection selection = {.instance = row.instance ? row.instance : instance,
        .component = row.component, .enabled = !row.enabled};
    bool ok = qa_launch_set_mod(mods->draft, &selection, error);
    free(instance);
    if (ok) { mods->dirty = true; snprintf(mods->status, sizeof(mods->status), "Staged changes; Apply or Cancel"); }
    return ok;
}
static bool action(void *context, uint32_t seat, qa_ui_id control,
                    const qa_ui_action *event, qa_error *error) {
    qa_ui_mods *mods = context;
    (void)seat;
    if (control == MOD_SEARCH && (event->kind == QA_UI_CHANGE_TEXT || event->kind == QA_UI_SUBMIT)) {
        const char *text = event->value.text ? event->value.text : "";
        qa_buffer lower = {0};
        if (!qa_utf8_lower((qa_bytes){(const uint8_t *)text, strlen(text)}, &lower, error)) return false;
        snprintf(mods->query, sizeof(mods->query), "%s", text);
        qa_buffer_free(&mods->query_lower); mods->query_lower = lower;
        mods->dirty = true;
        mods->selected = 0;
        return true;
    }
    if (control == MOD_LIST) {
        if (event->kind == QA_UI_SELECT) { mods->selected = event->value.row; return true; }
        if (event->kind == QA_UI_ROW_ACTIVATE) return toggle(mods, event->value.row, error);
    }
    if (event->kind != QA_UI_ACTIVATE) return true;
    switch (control) {
    case MOD_TOGGLE: return toggle(mods, mods->selected, error);
    case MOD_APPLY: return qa_ui_mods_apply(mods, error);
    case MOD_CANCEL: return qa_ui_mods_cancel(mods, error);
    case MOD_REFRESH: return qa_ui_mods_refresh(mods, error);
    default: return true;
    }
}
static bool factory(void *context, uint32_t seat, qa_ui_menu *out, qa_error *error) {
    qa_ui_mods *mods = context;
    (void)seat;
    if (mods->dirty) {
        ui_mods_cache_release(mods);
        const qa_launch_choices *choices = qa_launch_draft_choices(mods->draft);
        for (size_t i = 0; i < choices->mod_count; ++i) {
            const qa_launch_mod_selection *selection = &choices->mods[i];
            const qa_catalog_mod *mod = qa_catalog_mod_find(mods->catalog, selection->component);
            if ((!mod || mod->purpose == QA_MOD_ADDITION) && !row_add(mods, mod, selection, error)) return false;
        }
        for (size_t i = 0; i < qa_catalog_mod_count(mods->catalog); ++i) {
            const qa_catalog_mod *mod = qa_catalog_mod_at(mods->catalog, i);
            if (mod->purpose != QA_MOD_ADDITION) continue;
            bool present = false;
            for (size_t j = 0; j < choices->mod_count; ++j)
                present |= !strcmp(choices->mods[j].component, mod->key);
            if (!present && !row_add(mods, mod, NULL, error)) return false;
        }
        /* Reallocation may move selection state buffers referenced by rows. */
        for (size_t i = 0; i < mods->count; ++i) mods->rows[i].detail = mods->selections[i].state;
        ++mods->revision;
        mods->dirty = false;
    }
    if (mods->selected >= mods->count) mods->selected = mods->count ? mods->count - 1 : 0;
    for (size_t i = 0; i < 7; ++i) mods->controls[i] = (qa_ui_control){.id = i + 1,
        .kind = QA_UI_BUTTON, .enabled = true, .visible = true, .context = mods, .action = action};
    mods->controls[0].kind = QA_UI_FIELD;
    mods->controls[0].label = "Search mods";
    mods->controls[0].rect = (qa_scene_rect_f){48, 88, 544, 28};
    mods->controls[0].value.field.text = mods->query;
    mods->controls[0].value.field.maximum = 80;
    mods->controls[1].kind = QA_UI_LIST;
    mods->controls[1].label = "Mods";
    mods->controls[1].rect = (qa_scene_rect_f){48, 124, 544, 220};
    mods->controls[1].enabled = mods->count != 0;
    mods->controls[1].value.list.rows = mods->rows;
    mods->controls[1].value.list.count = mods->count;
    mods->controls[1].value.list.selected = mods->selected;
    mods->controls[1].value.list.row_height = 28;
    mods->controls[1].value.list.revision = mods->revision;
    const char *labels[] = {mods->count && mods->selections[mods->selected].enabled ? "Disable" : "Enable",
                            "Apply", "Cancel", "Refresh", mods->status};
    for (size_t i = 0; i < 4; ++i) {
        mods->controls[i + 2].label = labels[i];
        mods->controls[i + 2].rect = (qa_scene_rect_f){48 + (float)i * 138, 356, 130, 28};
    }
    const qa_catalog_mod *current = mods->count ? qa_catalog_mod_find(mods->catalog, mods->selections[mods->selected].component) : NULL;
    mods->controls[2].enabled = mods->count && (mods->selections[mods->selected].enabled || (current && !current->unavailable));
    snprintf(mods->display, sizeof(mods->display), "%s%s%s", mods->status,
        current && current->unavailable && *mods->status ? " / " : "",
        current && current->unavailable ? current->unavailable : "");
    mods->controls[6].label = mods->display;
    mods->controls[6].rect = (qa_scene_rect_f){48, 400, 544, 48};
    mods->controls[6].enabled = false;
    *out = (qa_ui_menu){.id = mods->menu, .title = "Mods", .controls = mods->controls,
                        .count = 7, .fullscreen = true};
    return true;
}
bool qa_ui_mods_create(qa_ui *ui, qa_application *application, qa_ui_id menu,
                       qa_ui_mods **out, qa_error *error) {
    if (!ui || !application || !menu || !out) return ui_fail(error, "invalid mod menu owner");
    qa_ui_mods *mods = calloc(1, sizeof(*mods));
    if (!mods) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating mod menu"); return false; }
    mods->ui = ui; mods->application = application; mods->menu = menu;
    if (!qa_ui_mods_cancel(mods, error) || !qa_ui_register(ui,
        &(qa_ui_menu_registration){.id = menu, .context = mods, .factory = factory}, error)) {
        qa_launch_draft_destroy(mods->draft); qa_catalog_release(mods->catalog); free(mods); return false;
    }
    *out = mods;
    return true;
}


bool qa_ui_mods_destroy(qa_ui_mods *mods, double time, qa_error *error) {
    if (!mods) return true;
    if (mods->ui->handling) return ui_fail(error, "mod menu callback is active");
    if (!qa_ui_unregister(mods->ui, mods->menu, time, error)) return false;
    qa_launch_draft_destroy(mods->draft); qa_catalog_release(mods->catalog);
    qa_buffer_free(&mods->query_lower);
    ui_mods_cache_release(mods);
    free(mods->rows); free(mods->selections); free(mods);
    return true;
}
