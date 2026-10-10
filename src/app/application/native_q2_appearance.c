#include "native_q2_appearance.h"
#include "guest_native_q2_private.h"

enum { Q2_CLIENT_MODEL = 255, Q2_CLASSIC_PLAYERSKINS = 1312,
    Q2_RERELEASE_PLAYERSKINS = 11582, Q2_CLASSIC_WEAPON_MODELS = 20 };

static struct application_native_q2 *engine_read(qa_application *app,
    const qa_application_native_q2_presentation *source)
{
    if (!source || source->kind != QA_APPLICATION_NATIVE_Q2_ORIGINAL ||
        !qa_application_native_q2_presentation_current(app, source)) return NULL;
    application_provider *provider = application_world_provider(app, QA_ROLE_ENTITIES, "");
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    return provider && provider->kind == APPLICATION_PROVIDER_NATIVE &&
        provider->owner == source->source_owner && provider->launch == source->launch &&
        provider->state.native.host == source->source.original.host && engine &&
        engine->provider == provider && engine->profile == source->source.original.profile &&
        !engine->calls && engine->configstrings ? engine : NULL;
}

static bool copy(char **out, const char *text, qa_error *error)
{
    size_t size = strlen(text) + 1;
    *out = malloc(size);
    if (!*out) return application_fail(error, QA_ERROR_MEMORY, "Copying original Q2 appearance path");
    memcpy(*out, text, size);
    return true;
}

static const char *resource(const struct application_native_q2 *engine, uint32_t index)
{
    if (!index || engine->resource_base[0] >= engine->configstring_count || index >= engine->resource_limit[0] ||
        index >= engine->configstring_count - engine->resource_base[0]) return "";
    const char *value = qa_strings_cstr(qa_session_strings(engine->provider->application->session), engine->configstrings[engine->resource_base[0] + index]);
    return value ? value : "";
}

static bool player_path(char **out, const char *model, size_t model_size,
    const char *name, const char *suffix, qa_error *error)
{
    size_t name_size = strlen(name), suffix_size = strlen(suffix);
    if (model_size > SIZE_MAX - name_size - suffix_size - 10)
        return application_fail(error, QA_ERROR_FORMAT, "Original Q2 player appearance path exceeds its extent");
    size_t size = 8 + model_size + name_size + suffix_size + 2;
    char *path = malloc(size);
    if (!path) return application_fail(error, QA_ERROR_MEMORY, "Resolving original Q2 player appearance");
    memcpy(path, "players/", 8);
    memcpy(path + 8, model, model_size);
    path[8 + model_size] = '/';
    memcpy(path + 9 + model_size, name, name_size);
    memcpy(path + 9 + model_size + name_size, suffix, suffix_size + 1);
    *out = path;
    return true;
}

static const char *weapon(const struct application_native_q2 *engine,
    qa_q2_edition edition, uint32_t ordinal)
{
    if (!ordinal) return "weapon.md2";
    uint32_t count = 1;
    for (uint32_t i = 1; i < engine->resource_limit[0]; ++i) {
        const char *name = resource(engine, i);
        if (name[0] != '#') continue;
        if (edition == QA_Q2_CLASSIC && count >= Q2_CLASSIC_WEAPON_MODELS) break;
        if (count++ == ordinal) return name + 1;
    }
    return "weapon.md2";
}

bool application_native_q2_appearance_read(qa_application *app,
    const qa_application_native_q2_presentation *source, uint32_t slot,
    application_native_q2_appearance *out, qa_error *error)
{
    struct application_native_q2 *engine = engine_read(app, source);
    if (!out || !engine || out->models[0] || out->models[1] || out->models[2] ||
        out->models[3] || out->skin_path)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 appearance requires its returned physical Source");
    application_native_q2_appearance value = {.source = *source,
        .config_revision = engine->config_revision, .source_slot = slot};
    if (!qa_native_host_q2_wire_entity((qa_native_host *)source->source.original.host,
            slot, &value.entity, error)) return false;
    const qa_q2_entity *state = &value.entity.state;
    uint32_t indexes[] = {state->modelindex, state->modelindex2, state->modelindex3, state->modelindex4};
    value.skin = state->skinnum;
    bool custom = false;
    for (unsigned i = 0; i < 4; ++i) custom |= indexes[i] == Q2_CLIENT_MODEL;
    bool ok = true;
    const char *model = "male", *skin = "grunt", *held = "weapon.md2";
    size_t model_size = 4;
    if (custom) {
        uint32_t base = source->edition == QA_Q2_CLASSIC ? Q2_CLASSIC_PLAYERSKINS : Q2_RERELEASE_PLAYERSKINS;
        uint32_t index = base + (state->skinnum & 255u);
        if (index >= engine->configstring_count) ok = application_fail(error, QA_ERROR_FORMAT,
            "Original Q2 player skin exceeds its actual configstring table");
        else {
            const char *info = qa_strings_cstr(qa_session_strings(engine->provider->application->session), engine->configstrings[index]);
            if (!info) info = "player\\male/grunt";
            const char *appearance = strchr(info, '\\');
            appearance = appearance ? appearance + 1 : info;
            const char *slash = strchr(appearance, '/');
            if (slash) { model = appearance; model_size = (size_t)(slash - appearance); skin = slash + 1; }
            held = weapon(engine, source->edition, (state->skinnum >> 8) & 255u);
        }
    }
    for (unsigned i = 0; ok && i < 4; ++i) {
        if (indexes[i] == Q2_CLIENT_MODEL) {
            ok = player_path(value.models + i, model, model_size, i ? held : "tris.md2", "", error);
            if (ok && !i) {
                value.skin = 0;
                ok = player_path(&value.skin_path, model, model_size, skin, ".pcx", error);
            }
        } else ok = copy(value.models + i, resource(engine, indexes[i]), error);
    }
    if (ok && !application_native_q2_appearance_current(app, &value))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 appearance changed its physical Source or configstrings");
    if (!ok) { application_native_q2_appearance_dispose(&value); return false; }
    *out = value;
    return true;
}

bool application_native_q2_appearance_current(qa_application *app,
    const application_native_q2_appearance *view)
{
    struct application_native_q2 *engine = view ? engine_read(app, &view->source) : NULL;
    qa_native_host_q2_entity row;
    return engine && engine->config_revision == view->config_revision &&
        qa_native_host_q2_wire_entity((qa_native_host *)view->source.source.original.host,
            view->source_slot, &row, NULL) &&
        qa_actor_id_equal(row.binding.actor, view->entity.binding.actor) &&
        row.binding.kind == view->entity.binding.kind && row.in_use == view->entity.in_use &&
        row.server_flags == view->entity.server_flags &&
        row.state.modelindex == view->entity.state.modelindex &&
        row.state.modelindex2 == view->entity.state.modelindex2 &&
        row.state.modelindex3 == view->entity.state.modelindex3 &&
        row.state.modelindex4 == view->entity.state.modelindex4 &&
        row.state.skinnum == view->entity.state.skinnum;
}

void application_native_q2_appearance_dispose(application_native_q2_appearance *view)
{
    if (!view) return;
    for (unsigned i = 0; i < 4; ++i) free(view->models[i]);
    free(view->skin_path);
    *view = (application_native_q2_appearance){0};
}
