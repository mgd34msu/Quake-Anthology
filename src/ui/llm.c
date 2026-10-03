#include "internal.h"
#include "qa/llm.h"
#include "qa/ui_assistance_save.h"
#include "qa/source_save.h"
#include <stdio.h>

struct qa_ui_llm {
    qa_ui *ui;
    qa_llm *llm;
    qa_ui_id menu;
    qa_ui_control controls[12];
    const char **models;
    size_t model_capacity;
    const char **efforts;
    size_t effort_capacity;
    char key[16385], base[2049], model[1025], status[512];
    bool rejected;
};
enum { LLM_PROVIDER = 1, LLM_MODEL, LLM_EFFORT, LLM_DISCOVER, LLM_SIGNIN, LLM_CANCEL,
       LLM_SIGNOUT, LLM_KEY, LLM_APPLY_KEY, LLM_BASE, LLM_OTHER_MODEL, LLM_STATUS };
static bool open_menu(void *context, uint32_t seat, qa_error *error)
{
    qa_ui_llm *menu = context; (void)seat;
    qa_llm_snapshot state;
    if (!qa_llm_read(menu->llm, &state, error)) return false;
    snprintf(menu->base, sizeof(menu->base), "%s", state.other_base_url ? state.other_base_url : "");
    snprintf(menu->model, sizeof(menu->model), "%s", state.models[QA_LLM_OTHER_API] ? state.models[QA_LLM_OTHER_API] : "");
    menu->key[0] = 0; menu->rejected = false;
    if (state.configured[state.provider] && !qa_llm_discover_models(menu->llm, state.provider, error)) {
        menu->rejected = true;
        snprintf(menu->status, sizeof(menu->status), "%s", error ? error->message : "Model discovery failed");
    }
    return true;
}
static void close_menu(void *context, uint32_t seat)
{
    qa_ui_llm *menu = context; (void)seat;
    memset(menu->key, 0, sizeof(menu->key));
    qa_llm_cancel_sign_in(menu->llm);
    for (unsigned i = 0; i < 3; ++i) {
        qa_error error = {0};
        if (!qa_llm_cancel_model_discovery(menu->llm, (qa_llm_provider)i, &error))
            fprintf(stderr, "model discovery cancellation: %s\n", error.message);
    }
}
static bool action(void *context, uint32_t seat, qa_ui_id control, const qa_ui_action *event, qa_error *error)
{
    qa_ui_llm *menu = context; (void)seat;
    qa_llm_snapshot state;
    if (!qa_llm_read(menu->llm, &state, error)) return false;
    qa_llm_provider provider = state.provider;
    bool ok = true;
    if (control == LLM_PROVIDER && event->kind == QA_UI_SELECT) {
        if (event->value.row > QA_LLM_OTHER_API) { ok = ui_fail(error, "invalid assistance provider"); goto done; }
        memset(menu->key, 0, sizeof(menu->key));
        menu->rejected = false;
        ok = qa_llm_cancel_model_discovery(menu->llm, provider, error) &&
            qa_llm_select_provider(menu->llm, (qa_llm_provider)event->value.row, error);
        if (ok) {
            ok = qa_llm_read(menu->llm, &state, error);
            if (ok && state.configured[state.provider]) ok = qa_llm_discover_models(menu->llm, state.provider, error);
        }
        goto done;
    }
    if (control == LLM_MODEL && event->kind == QA_UI_SELECT) {
        const qa_llm_model *model = qa_llm_model_at(menu->llm, provider, event->value.row);
        ok = model ? qa_llm_select_model(menu->llm, provider, model->id, error) : ui_fail(error, "model catalog changed");
        goto done;
    }
    if (control == LLM_EFFORT && event->kind == QA_UI_SELECT) {
        const qa_llm_model *model = NULL;
        for (size_t i = 0; i < qa_llm_model_count(menu->llm, provider); ++i) {
            const qa_llm_model *candidate = qa_llm_model_at(menu->llm, provider, i);
            if (state.models[provider] && !strcmp(state.models[provider], candidate->id)) model = candidate;
        }
        if (!model || event->value.row > model->effort_count) { ok = ui_fail(error, "effort catalog changed"); goto done; }
        ok = qa_llm_select_effort(menu->llm, provider, event->value.row ? model->efforts[event->value.row - 1] : NULL, error);
        goto done;
    }
    if (event->kind == QA_UI_CHANGE_TEXT) {
        char *target = control == LLM_KEY ? menu->key : control == LLM_BASE ? menu->base : control == LLM_OTHER_MODEL ? menu->model : NULL;
        size_t size = control == LLM_KEY ? sizeof(menu->key) : control == LLM_OTHER_MODEL ? sizeof(menu->model) : sizeof(menu->base);
        if (target) snprintf(target, size, "%s", event->value.text ? event->value.text : "");
        return true;
    }
    if ((control == LLM_BASE || control == LLM_OTHER_MODEL) && event->kind == QA_UI_SUBMIT)
        ok = qa_llm_set_other_connection(menu->llm, menu->base, menu->model, error);
    else if (event->kind == QA_UI_ACTIVATE) {
        switch (control) {
        case LLM_DISCOVER:
            ok = (provider != QA_LLM_OTHER_API || qa_llm_set_other_connection(menu->llm, menu->base, menu->model, error)) &&
                qa_llm_discover_models(menu->llm, provider, error); break;
        case LLM_SIGNIN: ok = qa_llm_sign_in(menu->llm, error); break;
        case LLM_CANCEL: qa_llm_cancel_sign_in(menu->llm); break;
        case LLM_SIGNOUT: ok = qa_llm_sign_out(menu->llm, error); break;
        case LLM_APPLY_KEY:
            ok = qa_llm_set_api_key(menu->llm, provider, *menu->key ? menu->key : NULL, error);
            if (ok) memset(menu->key, 0, sizeof(menu->key));
            break;
        default: break;
        }
    }
done:
    menu->rejected = !ok;
    if (!ok) snprintf(menu->status, sizeof(menu->status), "%s", error ? error->message : "Assistance settings rejected");
    else menu->status[0] = 0;
    return ok;
}
static bool factory(void *context, uint32_t seat, qa_ui_menu *out, qa_error *error)
{
    qa_ui_llm *menu = context; (void)seat;
    qa_llm_snapshot state;
    if (!qa_llm_read(menu->llm, &state, error)) return false;
    size_t count = qa_llm_model_count(menu->llm, state.provider);
    if (count > menu->model_capacity) {
        if (count > SIZE_MAX / sizeof(*menu->models)) return ui_fail(error, "assistance model list overflow");
        const char **models = realloc(menu->models, count * sizeof(*models));
        if (!models) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating assistance model choices"); return false; }
        menu->models = models; menu->model_capacity = count;
    }
    const qa_llm_model *selected = NULL; size_t selected_index = count;
    for (size_t i = 0; i < count; ++i) {
        const qa_llm_model *model = qa_llm_model_at(menu->llm, state.provider, i);
        menu->models[i] = model->name && *model->name ? model->name : model->id;
        if (state.models[state.provider] && !strcmp(model->id, state.models[state.provider])) { selected = model; selected_index = i; }
    }
    for (size_t i = 0; i < 12; ++i)
        menu->controls[i] = (qa_ui_control){.id = i + 1, .kind = QA_UI_BUTTON,
            .visible = true, .enabled = true, .context = menu, .action = action,
            .rect = {40, 88 + (float)i * 30, 560, 26}};
    static const char *providers[] = {"Subscription", "OpenAI API", "Other API"};
    qa_ui_control *provider = &menu->controls[LLM_PROVIDER - 1];
    provider->kind = QA_UI_CHOICE; provider->label = "Provider";
    provider->value.choice.labels = providers; provider->value.choice.count = 3; provider->value.choice.selected = state.provider;
    qa_ui_control *models = &menu->controls[LLM_MODEL - 1];
    models->kind = QA_UI_CHOICE; models->label = "Model"; models->enabled = count > 0;
    models->value.choice.labels = menu->models; models->value.choice.count = count; models->value.choice.selected = selected_index;
    qa_ui_control *effort = &menu->controls[LLM_EFFORT - 1];
    effort->kind = QA_UI_CHOICE; effort->label = "Reasoning effort"; effort->enabled = selected && selected->effort_count;
    size_t effort_count = selected ? selected->effort_count + 1 : 0;
    if (effort_count > menu->effort_capacity) {
        if (effort_count > SIZE_MAX / sizeof(*menu->efforts)) return ui_fail(error, "assistance effort list overflow");
        const char **labels = realloc(menu->efforts, effort_count * sizeof(*labels));
        if (!labels) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating effort choices"); return false; }
        menu->efforts = labels; menu->effort_capacity = effort_count;
    }
    if (selected) {
        menu->efforts[0] = "Model default";
        for (size_t i = 0; i < selected->effort_count; ++i) menu->efforts[i + 1] = selected->efforts[i];
    }
    effort->value.choice.labels = menu->efforts; effort->value.choice.count = effort_count;
    if (selected) for (size_t i = 0; i < selected->effort_count; ++i)
        if (state.efforts[state.provider] && !strcmp(selected->efforts[i], state.efforts[state.provider])) effort->value.choice.selected = i + 1;
    menu->controls[LLM_DISCOVER - 1].label = "Refresh models";
    menu->controls[LLM_SIGNIN - 1].label = "Sign in";
    menu->controls[LLM_SIGNIN - 1].enabled = state.provider == QA_LLM_SUBSCRIPTION && !state.signing_in;
    menu->controls[LLM_CANCEL - 1].label = "Cancel sign in"; menu->controls[LLM_CANCEL - 1].enabled = state.signing_in;
    menu->controls[LLM_SIGNOUT - 1].label = "Sign out";
    menu->controls[LLM_SIGNOUT - 1].enabled = state.provider == QA_LLM_SUBSCRIPTION && state.configured[QA_LLM_SUBSCRIPTION];
    qa_ui_control *key = &menu->controls[LLM_KEY - 1];
    key->kind = QA_UI_FIELD; key->label = "API key"; key->enabled = state.provider != QA_LLM_SUBSCRIPTION;
    key->value.field.text = menu->key; key->value.field.maximum = 4096; key->value.field.masked = true;
    menu->controls[LLM_APPLY_KEY - 1].label = "Apply key, empty removes it"; menu->controls[LLM_APPLY_KEY - 1].enabled = key->enabled;
    for (size_t i = LLM_BASE - 1; i <= LLM_OTHER_MODEL - 1; ++i) {
        menu->controls[i].kind = QA_UI_FIELD; menu->controls[i].enabled = state.provider == QA_LLM_OTHER_API;
        menu->controls[i].label = i == LLM_BASE - 1 ? "Other API URL" : "Other API model";
        menu->controls[i].value.field.text = i == LLM_BASE - 1 ? menu->base : menu->model;
        menu->controls[i].value.field.maximum = i == LLM_BASE - 1 ? 512 : 256;
    }
    const qa_error *fault = NULL;
    const qa_error *errors[] = {state.catalog_errors[state.provider], state.settings_errors[0],
        state.settings_errors[state.provider == QA_LLM_OTHER_API ? 2 : 1],
        state.provider == QA_LLM_OTHER_API ? state.settings_errors[3] : state.authentication_error};
    for (size_t i = 0; i < sizeof(errors) / sizeof(*errors); ++i)
        if (!fault && errors[i] && errors[i]->code != QA_OK) fault = errors[i];
    if (!menu->rejected) snprintf(menu->status, sizeof(menu->status), "%s", fault ? fault->message : state.signing_in ? "Browser sign in pending" :
        state.catalogs[state.provider] == QA_LLM_CATALOG_LOADING ? "Loading models" : state.configured[state.provider] ? "Configured" : "Provider needs authentication");
    menu->controls[LLM_STATUS - 1].label = menu->status; menu->controls[LLM_STATUS - 1].enabled = false;
    *out = (qa_ui_menu){.id = menu->menu, .title = "Assistance", .controls = menu->controls, .count = 12, .fullscreen = true};
    return true;
}
bool qa_ui_llm_create(qa_ui *ui, qa_llm *llm, qa_ui_id id, qa_ui_llm **out, qa_error *error)
{
    if (!ui || !llm || !id || !out) return ui_fail(error, "invalid assistance menu owner");
    qa_ui_llm *menu = calloc(1, sizeof(*menu));
    if (!menu) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating assistance menu"); return false; }
    menu->ui = ui; menu->llm = llm; menu->menu = id;
    if (!qa_ui_register(ui, &(qa_ui_menu_registration){.id = id, .context = menu,
        .factory = factory, .open = open_menu, .close = close_menu}, error)) { free(menu); return false; }
    *out = menu; return true;
}
bool qa_ui_llm_destroy(qa_ui_llm *menu, double time, qa_error *error)
{
    if (!menu) return true;
    if (!qa_ui_unregister(menu->ui, menu->menu, time, error)) return false;
    memset(menu->key, 0, sizeof(menu->key)); free(menu->models); free(menu->efforts); free(menu); return true;
}
const qa_llm *qa_ui_llm_service(const qa_ui_llm *menu) { return menu ? menu->llm : NULL; }
static void checkpoint_key_clear(qa_ui_llm *menu)
{
    volatile char *key = menu->key;
    for (size_t i = 0; i < sizeof(menu->key); ++i) key[i] = 0;
}
static bool checkpoint_fields(qa_source_save_io *io, qa_ui_llm *saved,
                               const qa_ui_llm *qualified, const qa_ui_assistance_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q','L','U','I'};
    uint32_t seat = qualified->ui->options.seat;
    uint64_t menu = qualified->menu, service = 0;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QLUI", sizeof(magic)) ||
        !qa_source_save_u32(io, &seat) || seat != qualified->ui->options.seat ||
        !qa_source_save_u64(io, &menu) || menu != qualified->menu) return false;
    if (!reading && !refs->service_encode(refs->context, saved->llm, &service, io->error)) return false;
    if (!qa_source_save_u64(io, &service)) return false;
    if (reading) {
        qa_llm *actual = NULL;
        if (!refs->service_decode(refs->context, service, &actual, io->error) ||
            !actual || actual != qualified->llm) return false;
    }
    if (!qa_source_save_bool(io, &saved->rejected)) return false;
    char *texts[] = {saved->key, saved->base, saved->model, saved->status};
    const size_t sizes[] = {sizeof(saved->key), sizeof(saved->base), sizeof(saved->model), sizeof(saved->status)};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i)
        if (!qa_source_save_bytes(io, texts[i], sizes[i]) || !memchr(texts[i], 0, sizes[i])) return false;
    /* Model and effort arrays are factory scratch. Action callbacks query
     * the actual service afresh and never consume these borrowed spans. */
    return true;
}
bool qa_ui_llm_checkpoint(const qa_ui_llm *menu, const qa_ui_assistance_checkpoint_refs *refs,
                           qa_buffer *out, qa_error *error)
{
    if (!menu || !menu->llm || !refs || !refs->service_encode || !out || out->data || out->size ||
        menu->ui->handling || menu->ui->drawing)
        return ui_fail(error, "assistance menu capture requires idle actual owners and empty output");
    qa_ui_llm saved = *menu;
    qa_source_save_io io = {0};
    bool success = qa_source_save_writer(&io, NULL, error) && checkpoint_fields(&io, &saved, menu, refs) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); checkpoint_key_clear(&saved);
    if (!success && error && error->code == QA_OK)
        qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid assistance menu continuation");
    return success;
}
bool qa_ui_llm_restore(qa_ui_llm *menu, const qa_ui_assistance_checkpoint_refs *refs,
                       qa_bytes bytes, qa_error *error)
{
    if (!menu || !menu->llm || !refs || !refs->service_decode || menu->ui->handling || menu->ui->drawing)
        return ui_fail(error, "assistance menu restore requires idle actual owners");
    qa_ui_llm saved = {.ui = menu->ui, .llm = menu->llm, .menu = menu->menu};
    qa_source_save_io io = {0};
    bool success = qa_source_save_reader(&io, NULL, bytes, error) && checkpoint_fields(&io, &saved, menu, refs) &&
        qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!success) {
        checkpoint_key_clear(&saved);
        if (error && error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid assistance menu continuation");
        return false;
    }
    qa_ui_llm displaced = *menu;
    *menu = saved;
    checkpoint_key_clear(&saved); checkpoint_key_clear(&displaced);
    free(displaced.models); free(displaced.efforts);
    return true;
}
