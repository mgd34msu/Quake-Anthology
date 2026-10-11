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
        !engine->calls && engine->configstrings ? engine : NULL;
}

static bool player_paths(struct application_native_q2 *engine, uint32_t slot,
    bool body, qa_error *error)
{
    qa_strings *strings = qa_session_strings(engine->provider->application->session);
    const qa_hud_q2_player_ids *names = engine->player_names + slot;
    qa_bytes model = qa_strings_text(strings,names->model);
    application_native_q2_player_media value = engine->player_media[slot];
    char path[4096];
    if (body) {
        if (!qa_hud_q2_player_model_path(model,(qa_bytes){(const uint8_t *)"tris.md2",8},"",path,sizeof(path),error) ||
            !qa_strings_intern_cstr(strings,path,&value.model,error) ||
            !qa_hud_q2_player_model_path(model,qa_strings_text(strings,names->skin),".pcx",path,sizeof(path),error) ||
            !qa_strings_intern_cstr(strings,path,&value.skin,error)) return false;
    }
    memset(value.weapons,0,sizeof(value.weapons));
    for (size_t i=0;i<engine->weapons.count;++i) {
        if (!qa_hud_q2_player_model_path(model,qa_strings_text(strings,engine->weapons.names[i]),"",path,sizeof(path),error) ||
            !qa_strings_intern_cstr(strings,path,value.weapons+i,error)) return false;
    }
    engine->player_media[slot]=value;
    return true;
}
bool application_native_q2_config_prepare(struct application_native_q2 *engine, qa_error *error)
{
    qa_strings *strings=qa_session_strings(engine->provider->application->session);
    if (!qa_hud_q2_weapons_bind(strings,&engine->config_layout,engine->configstrings,&engine->weapons,error)) return false;
    for (uint32_t slot=0;slot<256;++slot) {
        const char *info=qa_strings_cstr(strings,engine->configstrings[engine->config_layout.player_skins+slot]);
        if (!qa_hud_q2_player_ids_bind(strings,info?info:"",engine->config_layout.extended,engine->player_names+slot,error) ||
            !player_paths(engine,slot,true,error)) return false;
    }
    return true;
}
bool application_native_q2_config_update(struct application_native_q2 *engine,
    uint32_t index, qa_string_id name, qa_error *error)
{
    qa_string_id previous=engine->configstrings[index];
    if (previous==name) return true;
    qa_strings *strings=qa_session_strings(engine->provider->application->session);
    engine->configstrings[index]=name;
    qa_hud_q2_weapons_warning(strings,&engine->config_layout,engine->configstrings,index,&engine->weapons);
    bool ok=true;
    if (index>=engine->config_layout.player_skins && index-engine->config_layout.player_skins<256) {
        uint32_t slot=index-engine->config_layout.player_skins;
        const char *info=qa_strings_cstr(strings,name);
        ok=qa_hud_q2_player_ids_bind(strings,info?info:"",engine->config_layout.extended,engine->player_names+slot,error) &&
            player_paths(engine,slot,true,error);
    } else if (index>=engine->config_layout.models && index-engine->config_layout.models<engine->config_layout.max_models) {
        const char *old=qa_strings_cstr(strings,previous),*next=qa_strings_cstr(strings,name);
        if ((old&&old[0]=='#') || (next&&next[0]=='#')) {
            ok=qa_hud_q2_weapons_bind(strings,&engine->config_layout,engine->configstrings,&engine->weapons,error);
            for (uint32_t slot=0;ok&&slot<256;++slot) ok=player_paths(engine,slot,false,error);
        }
    }
    if (!ok) {engine->configstrings[index]=previous;return false;}
    qa_q2_config_changed(&engine->config_changes,index);
    return true;
}
bool application_native_q2_config_clear(struct application_native_q2 *engine, qa_error *error)
{
    for (uint32_t i=0;i<engine->config_layout.max_configs;++i) {
        if (engine->configstrings[i]) qa_q2_config_changed(&engine->config_changes,i);
        engine->configstrings[i]=QA_STRING_NONE;
    }
    return application_native_q2_config_prepare(engine,error);
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
    const application_native_q2_player_media *media=engine->player_media+(state->skinnum&255u);
    uint32_t ordinal=(state->skinnum>>8)&255u;
    qa_string_id held=ordinal<engine->weapons.count?media->weapons[ordinal]:media->weapons[0];
    for (unsigned i=0;i<4;++i) {
        if (indexes[i]==Q2_CLIENT_MODEL) {
            value.models[i]=i?held:media->model;
            if (!i) {value.skin=0;value.skin_path=media->skin;}
        } else if (indexes[i] && indexes[i]<engine->config_layout.max_models)
            value.models[i]=engine->configstrings[engine->config_layout.models+indexes[i]];
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
