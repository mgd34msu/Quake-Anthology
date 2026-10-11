#include "native_q2_appearance.h"
#include "guest_native_q2_private.h"

enum { Q2_CLIENT_MODEL = 255 };

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
        !engine->calls && engine->hud_names.configured ? engine : NULL;
}

static bool media_probe(void *context, qa_string_id name, qa_q2_player_media_kind kind,
    bool *found, qa_error *error)
{
    struct application_native_q2 *engine = context;
    qa_vfs *files = engine->provider->launch->content;
    uint64_t generation = qa_vfs_lookup_generation(files);
    if (engine->media_files != files || engine->media_generation != generation) {
        qa_id_index_clear(&engine->media_probes);
        engine->media_files = files;
        engine->media_generation = generation;
    }
    size_t cached;
    qa_id_index_cursor cursor = qa_id_index_begin(&engine->media_probes, name, 0);
    if (qa_id_index_next(&engine->media_probes, &cursor, &cached)) {
        *found = cached != 0;
        return true;
    }
    if (!qa_id_index_reserve(&engine->media_probes, 1, error)) return false;
    const char *path = qa_strings_cstr(qa_session_strings(engine->provider->application->session), name);
    char picture[4096];
    if(kind==QA_Q2_PLAYER_MEDIA_ICON) {
        if(!qa_scene_q2_picture_path(path,engine->hud_names.layout.extended,picture,sizeof(picture),error)) return false;
        path=picture;
    }
    uint64_t size;
    if (!qa_vfs_probe(files, path, found, &size, error)) return false;
    qa_id_index_insert(&engine->media_probes, name, 0, *found);
    return true;
}
static bool player_paths(struct application_native_q2 *engine, uint32_t slot, bool body, qa_error *error)
{
    qa_strings *strings = qa_session_strings(engine->provider->application->session);
    const qa_cvar_view *setting = qa_cvars_read(engine->cvars, engine->appearance_cvar);
    return qa_q2_player_media_resolve(strings, engine->hud_names.players + slot, engine->hud_names.weapons.names,
        engine->hud_names.weapons.count, engine->hud_names.layout.extended, setting ? setting->number : 0, false, body, media_probe, engine,
        engine->player_media + slot, error);
}
bool application_native_q2_config_prepare(struct application_native_q2 *engine, qa_error *error)
{
    engine->appearance_cvar = qa_cvars_resolve(engine->cvars, "cl_noskins");
    for (uint32_t slot=0;slot<256;++slot) {
        if (!player_paths(engine,slot,true,error)) return false;
    }
    return true;
}
bool application_native_q2_config_update(struct application_native_q2 *engine,
    uint32_t index, qa_string_id name, qa_error *error)
{
    qa_string_id previous=engine->hud_names.values[index];
    if (previous==name) return true;
    bool weapon_changed = false;
    bool ok = qa_hud_q2_names_set(&engine->hud_names,index,name,&weapon_changed,error);
    if (ok && index>=engine->hud_names.layout.player_skins && index-engine->hud_names.layout.player_skins<256) {
        uint32_t slot=index-engine->hud_names.layout.player_skins;
        ok=player_paths(engine,slot,true,error);
    } else if (weapon_changed) {
        for (uint32_t slot=0;ok&&slot<256;++slot) ok=player_paths(engine,slot,false,error);
    }
    if (!ok) return false;
    qa_q2_config_changed(&engine->config_changes,index);
    return true;
}
bool application_native_q2_config_clear(struct application_native_q2 *engine, qa_error *error)
{
    for (uint32_t i=0;i<engine->hud_names.layout.max_configs;++i) {
        if (engine->hud_names.values[i]) qa_q2_config_changed(&engine->config_changes,i);
    }
    qa_hud_q2_names_clear(&engine->hud_names);
    return qa_hud_q2_weapons_bind(engine->hud_names.strings,&engine->hud_names.layout,
        engine->hud_names.values,&engine->hud_names.weapons,error) &&
        application_native_q2_config_prepare(engine,error);
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
        .config_revision = engine->config_changes.revision, .source_slot = slot};
    if (!qa_native_host_q2_wire_entity((qa_native_host *)source->source.original.host,
            slot, &value.entity, error)) return false;
    const qa_q2_entity *state = &value.entity.state;
    uint32_t indexes[] = {state->modelindex, state->modelindex2, state->modelindex3, state->modelindex4};
    value.skin = state->skinnum;
    bool ok = true;
    const qa_q2_player_media *media=engine->player_media+(state->skinnum&255u);
    uint32_t ordinal=(state->skinnum>>8)&255u;
    qa_string_id held=ordinal<engine->hud_names.weapons.count?media->weapons[ordinal]:media->weapons[0];
    for (unsigned i=0;i<4;++i) {
        if (indexes[i]==Q2_CLIENT_MODEL) {
            value.models[i]=i?held:media->model;
            if (!i) {value.skin=0;value.skin_path=media->skin;}
        } else if (indexes[i] && indexes[i]<engine->hud_names.layout.max_models)
            value.models[i]=engine->hud_names.values[engine->hud_names.layout.models+indexes[i]];
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
    return engine && engine->config_changes.revision == view->config_revision &&
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
    *view = (application_native_q2_appearance){0};
}
