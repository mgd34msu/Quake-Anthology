#include "persistence.h"
#include "internal.h"
#include "capture.h"
#include "component_scene.h"
#include "component_scene_save.h"
#include "unified_graph_save.h"
#include "classic_client_graph_save.h"
#include "q2_client_graph_save.h"
#include "../application/guest_q3_components.h"
#include "../application/save_private.h"
#include "restore_topology.h"
#include "image_inventory.h"
#include "material_inventory.h"
#include "font_inventory.h"
#include "scene_inventory.h"
#include "world_inventory.h"
#include "root_restore.h"
#include "q3_inventory.h"
#include "audio_restore.h"
#include "audio_identity_save.h"
#include "content_refs.h"
#include "content_inventory.h"
#include "event_restore.h"
#include "visual_restore.h"
#include "native_q2_save.h"
#include "native_q2_baseline.h"
#include "player_inventory.h"
#include "seat_inventory.h"
#include "seat_save.h"
#include "tools_restore.h"
#include "source_restore.h"
#include "rankings.h"
#include "remap_save.h"
#include "save_commands.h"
#include "save_private.h"
#include "qa/input_platform_save.h"
#include "qa/scene_frame_save.h"
#include "qa/binary.h"
#include "qa/console_dedicated_save.h"
#include "qa/audio_device_save.h"
#include "qa/frontend_save.h"
#include "qa/http_save.h"
#include "qa/render_save.h"
#include "qa/render_gl_save.h"
#include "qa/display_save.h"
#include "qa/application_profile.h"
#include "qa/application_equipment_content.h"
#include "input_profile.h"
#include "campaign.h"
#include "campaign_cinematic.h"
#include "ui_features.h"
#include "source_prompt.h"
#include "qc_rerelease_events.h"
#include "equipment_media_save.h"
#include "equipment_q3_save.h"
#include "equipment_gear_save.h"
#include "shared_register.h"
#include "shared_resource_policy.h"
#include "native_q3_client.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "remote_q3_modules.h"
#include "network_initial_graph.h"
#include "network_restore.h"
#include "global_settings_storage.h"
#include "source_renderer_runtime.h"
#include "music_sources.h"
#include "view_bindings.h"
#include "view_settings.h"
#include "qa/audio_music_engine.h"
#include "material_movie_inventory.h"
#include "cinematic_roles.h"
#include "unified_media_inventory.h"
#include "remote_unified_material_movies_bridge.h"
#include "material_movie_bindings.h"
#include "root_resources.h"
#include "client_source.h"
#include "network_client_commands.h"
#include "remote_q1_effects.h"
#include "q3_color_policy.h"
#include "q1_sky_save.h"
#include "qc_messages.h"
#include "remote_q3_graph_roster.h"
#include "native_q3_topology.h"
#include "native_composition.h"
#include "native_resource_inventory.h"
#include "qa/audio_acoustics_prepare.h"
#include "renderer_materials.h"
#include "renderer_worlds.h"
#include "renderer_registries_save.h"
#include "restart_binding.h"
#include "config_store.h"
#include "keys.h"
#include "client_registry.h"
#include "remote_config.h"
#include "selected_character_save.h"
#include "selected_effects_save.h"
#include "equipment_events.h"
#include "qa/q3_product_policy.h"
#include "qa/vfs_view_save.h"
#include <SDL.h>

typedef enum frontend_section {
    SECTION_TOPOLOGY, SECTION_EVENTS_TOPOLOGY, SECTION_VISUAL_TOPOLOGY,
    SECTION_NATIVE_TOPOLOGY, SECTION_TOOLS, SECTION_IMAGES, SECTION_NAMESPACE,
    SECTION_IMAGE_OWNERS, SECTION_MATERIALS, SECTION_FONTS, SECTION_MODELS,
    SECTION_ROOTS, SECTION_FRAME, SECTION_EVENTS, SECTION_PARTICLES,
    SECTION_PLAYERS, SECTION_NATIVE, SECTION_SEATS_PRESENTATION, SECTION_SHADERS, SECTION_ALIASES, SECTION_RENDERER,
    SECTION_AUDIO_IDS, SECTION_BANKS, SECTION_ENGINE, SECTION_DEVICE,
    SECTION_SEATS_INPUT, SECTION_PLATFORM, SECTION_TERMINAL, SECTION_SAVE_COMMANDS,
    SECTION_Q3, SECTION_SOURCE, SECTION_INPUT_PROFILE, SECTION_UI_FEATURES, SECTION_QC_DEBUG,
    SECTION_NATIVE_RUNTIME, SECTION_EQUIPMENT_TOPOLOGY, SECTION_SELECTED_Q3_TOPOLOGY,
    SECTION_KEYS, SECTION_CONFIG_STORE, SECTION_CLIENT_REGISTRIES, SECTION_NATIVE_Q3_TOPOLOGY,
    SECTION_CHARACTER_TOPOLOGY, SECTION_EFFECTS_TOPOLOGY, SECTION_GEAR_EVENTS, SECTION_GEAR_TOPOLOGY,
    SECTION_GLOBAL_SETTINGS, SECTION_REMOTE_GRAPH, SECTION_MUSIC_SOURCES, SECTION_VIEW_SETTINGS,
    SECTION_MATERIAL_MOVIES, SECTION_Q1_SKY, SECTION_QC_MESSAGES, SECTION_SOURCE_COLOR,
    SECTION_RENDERER_MATERIALS, SECTION_RESTART, SECTION_RENDERER_WORLDS, SECTION_COMPONENT_SCENES,
    SECTION_UNIFIED_GRAPH, SECTION_CLASSIC_CLIENT_GRAPH, SECTION_Q2_CLIENT_GRAPH, SECTION_RENDERER_REGISTRIES,
    SECTION_RESOURCE_POLICY, SECTION_COUNT
} frontend_section;
typedef struct frontend_section_set {
    qa_buffer owned[SECTION_COUNT];
    qa_bytes bytes[SECTION_COUNT];
} frontend_section_set;
typedef struct frontend_persistence frontend_persistence;
typedef struct frontend_owner_binding {
    frontend_persistence *operation;
    qa_save_owner_kind kind;
    size_t ordinal;
} frontend_owner_binding;
struct frontend_persistence {
    qa_frontend *active, *candidate;
    qa_frontend *constructor;
    qa_frontend **slot;
    const qa_application_persistence_ops *services;
    qa_application_persistence_owner owners[7];
    qa_application_persistence_owner *producer_inventory;
    frontend_owner_binding bindings[7];
    qa_application_persistence_ops ops;
    qa_application_ranking_checkpoint_refs ranking;
    qa_vfs_checkpoint_refs content_files;
    frontend_native_resource_context native_resources;
    qa_application_native_resource_refs native_resource_refs;
    frontend_section_set sections;
    frontend_capture *capture;
    frontend_capture *candidate_capture;
    frontend_scene_inventory *scenes;
    frontend_model_inventory *models;
    frontend_scene_namespace *space;
    const frontend_scene_namespace *canonical;
    frontend_world_inventory *roots;
    frontend_q3_inventory *q3;
    frontend_q3_inventory *renderer_q3;
    frontend_remote_q3_graph_roster *remote_graph;
    frontend_component_scene_restore_set *component_scenes;
    frontend_unified_graph *unified_graph;
    frontend_classic_client_graph *classic_graph;
    frontend_q2_client_graph *q2_graph;
    const qa_save_image *restore_image;
    application_save_console_context client_console_context;
    qa_console_save_resolvers client_console;
    qa_audio_asset_inventory *audio;
    qa_scene_image_set *images;
    frontend_restore_topology *topology;
    frontend_native_q3_topology *native_topology;
    qa_input_platform_restore_guard *input_guard;
    qa_audio_device_restore_guard *device_guard;
    qa_display_restore_guard *display_guard;
    qa_gl_restore_guard *gl_guard;
    frontend_q3_color_ticket *color_ticket;
    const frontend_persistence *restored_from;
    qa_buffer external[7];
    uint32_t restored;
    bool captured, imported, finished, fresh_original, sections_read, network_staged;
};

static qa_bytes section(const frontend_section_set *set, frontend_section id)
{ return set->bytes[id]; }
static bool key_registry_encode(void *context,const qa_cvars *registry,
    qa_application_console_scope *out,qa_error *error)
{
    qa_frontend *f=context;
    if (!f || !f->application || !registry || !out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Key registry requires its actual frontend owner");
    for (size_t i=0;i<qa_application_console_count(f->application);++i) {
        qa_console *console=qa_application_console_at(f->application,i,NULL);
        const frontend_config_source *source=frontend_config_store_source(f->config_store,console);
        if (source && frontend_config_source_cvars(source)==registry &&
            qa_application_console_scope_read(f->application,console,out)) return true;
        frontend_remote_config *client=frontend_config_store_client(f->config_store,console);
        frontend_remote_config_view view;
        if (client && frontend_remote_config_read(client,&view) && view.cvars==registry &&
            view.console==console && qa_application_console_scope_read(f->application,console,out) &&
            out->provider==view.scope.provider && out->kind==view.scope.kind && out->seat==view.scope.seat) return true;
    }
    return frontend_source_registry_scope_read(f,registry,out) ||
        frontend_fail(error,QA_ERROR_FORMAT,"Key registry has no installed physical source scope");
}
static const char *key_registry_instance(void *context,qa_actor_owner owner)
{ return qa_application_provider_instance(((qa_frontend *)context)->application,owner); }
static bool key_registry_resolve(void *context,const char *name,qa_actor_owner *owner,qa_error *error)
{
    qa_frontend *f=context;
    return (f && f->application && name && owner &&
        qa_application_provider_owner(f->application,name,owner)) ||
        frontend_fail(error,QA_ERROR_FORMAT,"Saved key scope lacks its actual provider instance");
}
static bool key_registry_qualify(void *context,const qa_application_console_scope *scope,
    const qa_cvars *registry,qa_error *error)
{
    qa_application_console_scope actual;
    return (scope && key_registry_encode(context,registry,&actual,error) &&
        actual.provider==scope->provider && actual.kind==scope->kind && actual.seat==scope->seat) ||
        frontend_fail(error,QA_ERROR_FORMAT,"Saved key scope differs from its physical registry");
}
static frontend_keys_cvar_refs key_registry_refs(qa_frontend *f)
{
    return (frontend_keys_cvar_refs){f,key_registry_encode,key_registry_instance,
        key_registry_resolve,key_registry_qualify};
}
static bool restore_configuration_prefix(frontend_persistence *operation,qa_error *error)
{
    qa_frontend *f=operation->candidate;
    qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
    qa_q3_product_policy policy={0};
    (void)qa_application_q3_product_policy_read(f->application,&policy);
    frontend_keys_cvar_refs refs=key_registry_refs(f); frontend_keys *decoded=NULL;
    if (!frontend_keys_restore(graph,&policy,&refs,section(&operation->sections,SECTION_KEYS),&decoded,error)) return false;
    if (!frontend_keys_destroy(f->keys,error)) { frontend_keys_destroy(decoded,NULL); return false; }
    f->keys=decoded;
    return frontend_config_store_restore_into(f->config_store,f->application,graph,f->keys,&refs,
        section(&operation->sections,SECTION_CONFIG_STORE),error) &&
        frontend_client_registries_prepare_restore(f,graph,section(&operation->sections,SECTION_CLIENT_REGISTRIES),error);
}
static void section_publish(frontend_section_set *set)
{
    for (size_t i=0;i<SECTION_COUNT;++i)
        set->bytes[i]=(qa_bytes){set->owned[i].data,set->owned[i].size};
}
static void sections_destroy(frontend_section_set *set)
{
    for (size_t i=0;i<SECTION_COUNT;++i) qa_buffer_free(set->owned+i);
    *set=(frontend_section_set){0};
}
static bool copy_bytes(qa_bytes bytes, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || (bytes.size && !bytes.data))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend continuation needs an empty owned output");
    uint8_t *data=bytes.size?malloc(bytes.size):NULL;
    if (bytes.size && !data) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining frontend continuation");
    if (bytes.size) memcpy(data,bytes.data,bytes.size);
    *out=(qa_buffer){data,bytes.size}; return true;
}
static bool blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t size=bytes->size;
    if (!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,size);
    if (size>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size; return true;
}
static bool envelope(qa_source_save_io *io, qa_save_owner_kind expected,
    frontend_section_set *set, const frontend_section *ids, size_t count)
{
    uint8_t magic[4]={'Q','F','E','X'}; uint32_t kind=expected; size_t saved=count;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFEX",4) ||
        !qa_source_save_u32(io,&kind) || kind!=(uint32_t)expected ||
        !qa_source_save_count(io,&saved,count) || saved!=count) return false;
    for (size_t i=0;i<count;++i) {
        uint32_t id=ids[i];
        if (!qa_source_save_u32(io,&id) || id!=(uint32_t)ids[i] || !blob(io,set->bytes+ids[i]) || !set->bytes[ids[i]].size) return false;
    }
    return true;
}
static const frontend_section presentation_sections[]={SECTION_TOPOLOGY,SECTION_EVENTS_TOPOLOGY,
    SECTION_VISUAL_TOPOLOGY,SECTION_NATIVE_TOPOLOGY,SECTION_TOOLS,SECTION_IMAGES,SECTION_NAMESPACE,
    SECTION_IMAGE_OWNERS,SECTION_MATERIALS,SECTION_FONTS,SECTION_MODELS,SECTION_ROOTS,SECTION_FRAME,
    SECTION_EVENTS,SECTION_PARTICLES,SECTION_PLAYERS,SECTION_NATIVE,SECTION_SEATS_PRESENTATION,SECTION_SHADERS,SECTION_ALIASES,SECTION_RENDERER,SECTION_QC_DEBUG,
    SECTION_EQUIPMENT_TOPOLOGY,SECTION_SELECTED_Q3_TOPOLOGY,SECTION_NATIVE_Q3_TOPOLOGY,SECTION_CHARACTER_TOPOLOGY,
    SECTION_EFFECTS_TOPOLOGY,SECTION_GEAR_EVENTS,SECTION_GEAR_TOPOLOGY,SECTION_REMOTE_GRAPH,SECTION_VIEW_SETTINGS,
    SECTION_MATERIAL_MOVIES,SECTION_Q1_SKY,SECTION_QC_MESSAGES,SECTION_SOURCE_COLOR,SECTION_RENDERER_MATERIALS,SECTION_RENDERER_WORLDS,SECTION_COMPONENT_SCENES,SECTION_UNIFIED_GRAPH,SECTION_CLASSIC_CLIENT_GRAPH,SECTION_Q2_CLIENT_GRAPH,SECTION_RENDERER_REGISTRIES,SECTION_RESOURCE_POLICY};
static const frontend_section audio_sections[]={SECTION_AUDIO_IDS,SECTION_BANKS,SECTION_ENGINE,SECTION_DEVICE,SECTION_UI_FEATURES,
    SECTION_MUSIC_SOURCES};
static const frontend_section input_sections[]={SECTION_SEATS_INPUT,SECTION_PLATFORM,SECTION_TERMINAL,SECTION_SAVE_COMMANDS,SECTION_INPUT_PROFILE,
    SECTION_KEYS,SECTION_CONFIG_STORE,SECTION_CLIENT_REGISTRIES,SECTION_GLOBAL_SETTINGS,SECTION_RESTART};
static const frontend_section media_sections[]={SECTION_Q3,SECTION_SOURCE,SECTION_NATIVE_RUNTIME};
static bool owner_envelope(qa_source_save_io *io, qa_save_owner_kind kind, frontend_section_set *set)
{
    switch (kind) {
    case QA_SAVE_PRESENTATION: return envelope(io,kind,set,presentation_sections,sizeof(presentation_sections)/sizeof(*presentation_sections));
    case QA_SAVE_AUDIO: return envelope(io,kind,set,audio_sections,sizeof(audio_sections)/sizeof(*audio_sections));
    case QA_SAVE_INPUT: return envelope(io,kind,set,input_sections,sizeof(input_sections)/sizeof(*input_sections));
    case QA_SAVE_MEDIA: return envelope(io,kind,set,media_sections,sizeof(media_sections)/sizeof(*media_sections));
    default: return frontend_fail(io->error,QA_ERROR_FORMAT,"Unexpected frontend continuation owner");
    }
}
static bool optional_state(qa_source_save_io *io, const char magic[4], bool expected, qa_bytes *state)
{
    uint8_t saved[4]; memcpy(saved,magic,4); bool present=expected;
    return qa_source_save_bytes(io,saved,4) && !memcmp(saved,magic,4) &&
        qa_source_save_bool(io,&present) && present==expected &&
        blob(io,state) && (present?state->size!=0:state->size==0);
}
static bool optional_encode(const char magic[4], bool present, qa_bytes bytes, qa_buffer *out, qa_error *error)
{
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && optional_state(&io,magic,present,&bytes) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
static bool optional_decode(const char magic[4], bool present, qa_bytes bytes, qa_bytes *out, qa_error *error)
{
    qa_source_save_io io={0}; qa_bytes state={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && optional_state(&io,magic,present,&state) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (ok) *out=state;
    return ok || frontend_fail(error,QA_ERROR_FORMAT,"Optional frontend owner differs from its real topology");
}
static bool settings_storage_capture(qa_frontend *f,qa_buffer *out,qa_error *error)
{
    qa_buffer state={0};
    bool ok=!f->global_settings_storage || frontend_global_settings_storage_checkpoint(f->global_settings_storage,
        qa_application_content_graph_read(f->application),&state,error);
    ok=ok && optional_encode("QFGW",f->global_settings_storage!=NULL,
        (qa_bytes){state.data,state.size},out,error);
    qa_buffer_free(&state); return ok;
}
static bool settings_storage_prepare(frontend_persistence *operation,qa_error *error)
{
    qa_frontend *f=operation->candidate; qa_source_save_io io={0};
    uint8_t magic[4]={0}; bool present=false; qa_bytes state={0};
    bool ok=f && !f->global_settings_storage && qa_source_save_reader(&io,NULL,
        section(&operation->sections,SECTION_GLOBAL_SETTINGS),error) &&
        qa_source_save_bytes(&io,magic,4) && !memcmp(magic,"QFGW",4) &&
        qa_source_save_bool(&io,&present) &&
        blob(&io,&state) && (present?state.size!=0:state.size==0) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(ok && present) ok=frontend_global_settings_storage_restore(
        qa_application_content_graph_read(f->application),state,&f->global_settings_storage,error);
    return ok || frontend_fail(error,QA_ERROR_FORMAT,"Global settings storage leaves its saved physical directory owner");
}
static bool input_seat_encode(void *context, const qa_input_seat *seat, uint64_t *key, qa_error *error)
{
    qa_frontend *f=context;
    for (unsigned i=0;f && f->seats && key && i<f->options.seats;++i)
        if (seat && seat==f->seats[i].input && f->seats[i].frontend==f && f->seats[i].id==i) { *key=i+1; return true; }
    return frontend_fail(error,QA_ERROR_FORMAT,"Platform route leaves its actual stable frontend seats");
}
static bool input_seat_decode(void *context, uint64_t key, qa_input_seat **out, qa_error *error)
{
    qa_frontend *f=context;
    if (!f || !f->seats || !key || key>f->options.seats || !out || !f->seats[key-1].input ||
        f->seats[key-1].frontend!=f || f->seats[key-1].id!=key-1)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved platform route has no genuine candidate input seat");
    *out=f->seats[key-1].input; return true;
}
static bool haptic_resource_encode(void *context, const qa_resource *resource, uint64_t *pool, uint64_t *version, qa_error *error)
{
    qa_frontend *f=context;
    return qa_application_content_resource_id(qa_application_content_graph_read(f->application),resource,pool,version) ||
        frontend_fail(error,QA_ERROR_FORMAT,"Haptic pattern leaves its actual retained content pool");
}
static bool haptic_resource_decode(void *context, uint64_t pool, uint64_t version, const qa_resource **out, qa_error *error)
{
    qa_frontend *f=context;
    const qa_resource *resource=qa_application_content_resource(qa_application_content_graph_read(f->application),pool,version);
    if (!out || !resource) return frontend_fail(error,QA_ERROR_FORMAT,"Saved haptic pattern has no candidate resource version");
    *out=resource; return true;
}
static qa_input_platform_checkpoint_refs input_refs(qa_frontend *f)
{
    return (qa_input_platform_checkpoint_refs){.context=f,.seat_encode=input_seat_encode,.seat_decode=input_seat_decode,
        .haptics={f,haptic_resource_encode,haptic_resource_decode}};
}
static bool module_audio_scope(const frontend_remote_q3_modules *modules,uint64_t id,uint64_t *role)
{
    for(size_t i=0;i<frontend_remote_q3_modules_role_count(modules);++i) {
        frontend_remote_q3_module_topology view;
        if(!frontend_remote_q3_modules_role_read(modules,i,&view,NULL)) return false;
        if(view.service_owner==id) { *role=i+1; return true; }
    }
    return false;
}
static bool audio_scope(qa_frontend *f, uint64_t id, uint32_t *domain, uint64_t *ordinal,uint64_t *role)
{
    for(size_t i=0;i<frontend_cinematic_roles_count(f);++i) {
        frontend_cinematic_role_view detached;
        if(!frontend_cinematic_roles_read(f,i,&detached,NULL)) return false;
        if(detached.bus==id) { *domain=20; *ordinal=i+1; *role=0; return true; }
    }
    size_t media_count=0;
    if(!frontend_unified_media_inventory_count(f,&media_count,NULL)) return false;
    for(size_t i=0;i<media_count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(f,i,&media,NULL)) return false;
        for(size_t j=0;j<frontend_unified_media_bank_count(media);++j) {
            frontend_unified_bank_view bank;
            uint32_t seat=0; uint64_t bus=0; bool present=false;
            if(!frontend_unified_media_bank_read(media,j,&bank)) return false;
            if(!bank.movies) continue;
            if(!frontend_unified_material_cinematic_namespace_read(media,j,&seat,&bus,&present,NULL)) return false;
            if(present && bus==id) { *domain=21; *ordinal=i+1; *role=j+1; return true; }
        }
    }
    if(frontend_unified_graph_audio_scope(f,id,domain,ordinal,role)) return true;
    for(size_t i=0;i<frontend_remote_q1_count(f);++i) {
        uint64_t bus=0; qa_audio_music *player=NULL;
        if(remote_q1_effects_music(frontend_remote_q1_at(f,i),&bus,&player) && bus==id) {
            *domain=16; *ordinal=i+1; *role=0; return true;
        }
    }
    if (id==QA_FRONTEND_COMMAND_OWNER) { *domain=1; *ordinal=0; return true; }
    for (size_t i=0;i<frontend_source_group_count(f);++i) {
        frontend_source_group_view group;
        if (!frontend_source_group_read(f,i,&group)) return false;
        if (group.identity==id) { *domain=1; *ordinal=i+1; return true; }
    }
    for (size_t i=0;i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view native; qa_error error={0};
        if (!frontend_native_q3_read(f,i,&native,&error)) return false;
        if (native.identity==id) { *domain=2; *ordinal=i+1; return true; }
    }
    for (size_t i=0;i<frontend_selected_effects_count(f);++i) {
        frontend_selected_effects_view effects; qa_error error={0};
        if (!frontend_selected_effects_at(f,i,&effects,&error)) return false;
        if (effects.identity==id) { *domain=3; *ordinal=i+1; return true; }
    }
    for(size_t i=0;i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3 *row=frontend_remote_q3_at(f,i);
        frontend_remote_q3_resources resources;
        if(!frontend_remote_q3_resources_read(row,&resources,NULL)) return false;
        if(resources.identity==id) { *domain=5; *ordinal=i+1; return true; }
        if(module_audio_scope(frontend_remote_q3_modules_read(row),id,role)) {
            *domain=7; *ordinal=i+1; return true;
        }
    }
    frontend_network_initial_graph_view initial;
    if(!frontend_network_initial_graph_read(f,&initial,NULL)) return false;
    if(initial.present) {
        frontend_remote_q3_initial_view resources;
        if(!initial.parent || !frontend_remote_q3_initial_read(initial.parent,&resources,NULL)) return false;
        if(resources.identity==id) { *domain=6; *ordinal=1; return true; }
        if(module_audio_scope(initial.modules,id,role)) { *domain=8; *ordinal=1; return true; }
    }
    for(size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        if(!frontend_component_scene_metadata_read(f,i,&row,NULL)) return false;
        if(row.identity==id) { *domain=11; *ordinal=i+1; return true; }
    }
    size_t retained=0;
    if(!frontend_renderer_materials_count(f,&retained,NULL)) return false;
    for(size_t i=0;i<retained;++i) {
        frontend_renderer_materials_view row;
        if(!frontend_renderer_materials_read_at(f,i,&row,NULL)) return false;
        if(row.has_cinematics && row.cinematic_bus==id) {
            *domain=18; *ordinal=i+1; *role=0; return true;
        }
    }
    for(unsigned slot=0;slot<2;++slot) {
        uint64_t bus=0;
        if(frontend_music_sources_bus(f->music_sources,(frontend_music_slot)slot,&bus) && bus==id) {
            *domain=9+slot; *ordinal=1; return true;
        }
    }
    return false;
}
static bool audio_scope_decode(qa_frontend *f, uint32_t domain, uint64_t ordinal,uint64_t role,uint64_t *id)
{
    if(domain==20) {
        frontend_cinematic_role_view detached;
        if(!ordinal || ordinal-1>SIZE_MAX || role ||
            !frontend_cinematic_roles_read(f,(size_t)ordinal-1,&detached,NULL)) return false;
        *id=detached.bus; return true;
    }
    if(domain==21) {
        frontend_unified_media *media=NULL; uint32_t seat=0; bool present=false;
        return ordinal && ordinal-1<=SIZE_MAX && role && role-1<=SIZE_MAX &&
            frontend_unified_media_inventory_at(f,(size_t)ordinal-1,&media,NULL) && media &&
            frontend_unified_material_cinematic_namespace_read(media,(size_t)role-1,&seat,id,&present,NULL) && present;
    }
    if((domain>=12 && domain<=15) || domain==17) return frontend_unified_graph_audio_resolve(f,domain,ordinal,role,id);
    if(domain==18) {
        frontend_renderer_materials_view row;
        if(!ordinal || ordinal-1>SIZE_MAX || role ||
            !frontend_renderer_materials_read_at(f,(size_t)ordinal-1,&row,NULL) ||
            !row.has_cinematics || !row.cinematic_bus) return false;
        *id=row.cinematic_bus; return true;
    }
    if(domain==16) {
        qa_audio_music *player=NULL;
        return ordinal && ordinal-1<=SIZE_MAX && !role &&
            remote_q1_effects_music(frontend_remote_q1_at(f,(size_t)ordinal-1),id,&player) && *id;
    }
    if(domain==11) {
        frontend_component_scene_view row;
        if(!ordinal || ordinal-1>SIZE_MAX || role ||
            !frontend_component_scene_metadata_read(f,(size_t)ordinal-1,&row,NULL)) return false;
        *id=row.identity; return true;
    }
    if(domain==9 || domain==10) return ordinal==1 && !role &&
        frontend_music_sources_bus(f->music_sources,(frontend_music_slot)(domain-9),id);
    if(domain==5 || domain==7) {
        if(!ordinal || ordinal-1>SIZE_MAX) return false;
        frontend_remote_q3 *row=frontend_remote_q3_at(f,(size_t)ordinal-1);
        frontend_remote_q3_resources resources;
        if(!row || !frontend_remote_q3_resources_read(row,&resources,NULL)) return false;
        if(domain==5) { if(role) return false; *id=resources.identity; return true; }
        frontend_remote_q3_module_topology view;
        if(!role || role-1>SIZE_MAX || !frontend_remote_q3_modules_role_read(
            frontend_remote_q3_modules_read(row),(size_t)role-1,&view,NULL)) return false;
        *id=view.service_owner; return true;
    }
    if(domain==6 || domain==8) {
        frontend_network_initial_graph_view initial;
        frontend_remote_q3_initial_view resources;
        if(ordinal!=1 || !frontend_network_initial_graph_read(f,&initial,NULL) || !initial.present ||
            !initial.parent || !frontend_remote_q3_initial_read(initial.parent,&resources,NULL)) return false;
        if(domain==6) { if(role) return false; *id=resources.identity; return true; }
        frontend_remote_q3_module_topology view;
        if(!role || role-1>SIZE_MAX || !frontend_remote_q3_modules_role_read(initial.modules,(size_t)role-1,&view,NULL)) return false;
        *id=view.service_owner; return true;
    }
    if(role) return false;
    if (domain==3) {
        frontend_selected_effects_view effects; qa_error error={0};
        if (!ordinal || ordinal-1>SIZE_MAX || !frontend_selected_effects_at(f,(size_t)ordinal-1,&effects,&error)) return false;
        *id=effects.identity; return true;
    }
    if (domain==2) {
        frontend_native_q3_view native; qa_error error={0};
        if (!ordinal || ordinal-1>SIZE_MAX || !frontend_native_q3_read(f,(size_t)ordinal-1,&native,&error)) return false;
        *id=native.identity; return true;
    }
    if (domain!=1) return false;
    if (!ordinal) { *id=QA_FRONTEND_COMMAND_OWNER; return true; }
    frontend_source_group_view group;
    if (ordinal-1>SIZE_MAX || !frontend_source_group_read(f,(size_t)(ordinal-1),&group)) return false;
    *id=group.identity; return true;
}
static bool audio_resource_present(frontend_persistence *operation, uint64_t id)
{
    /* This field is the source's pool-local resource number. The actual asset
     * descriptor independently preserves its pool and immutable version. */
    for (uint64_t i=0;operation->audio && i<UINT64_MAX;++i) {
        qa_audio_asset *asset=qa_audio_asset_inventory_at(operation->audio,i);
        if (!asset) break;
        const qa_resource *resource=qa_audio_asset_resource(asset);
        if (resource && qa_resource_id(resource)==id) return true;
    }
    return false;
}
static bool audio_encode(void *context, qa_audio_reference_kind kind, uint64_t id, qa_buffer *out, qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate?operation->candidate:operation->active;
    if(kind==QA_AUDIO_REFERENCE_ACTOR) {
        uint32_t domain=0; uint64_t owner=0,row=0;
        if(frontend_unified_graph_audio_scope(f,id,&domain,&owner,&row) && domain==15) {
            qa_source_save_io actor={0};
            bool ok=qa_source_save_writer(&actor,NULL,error) && qa_source_save_u32(&actor,&domain) &&
                qa_source_save_u64(&actor,&owner) && qa_source_save_u64(&actor,&row) && qa_source_save_finish(&actor,out);
            qa_source_save_dispose(&actor); return ok;
        }
        return frontend_audio_id_encode(f,id,out,error);
    }
    qa_source_save_io io={0}; uint32_t tag=0; uint64_t key=id,role=0; char *owner=NULL;
    bool valid=false;
    switch (kind) {
    case QA_AUDIO_REFERENCE_OWNER:
        if (audio_scope(f,id,&tag,&key,&role)) valid=tag!=15;
        else if (id<=UINT32_MAX && qa_application_provider_instance(f->application,(qa_actor_owner)id)) {
            owner=(char *)qa_strings_cstr(qa_session_strings(qa_application_session(f->application)),(qa_string_id)id);
            valid=owner!=NULL;
        } else if (id<=UINT32_MAX) {
            qa_application_equipment_content gear;
            if (qa_application_equipment_content_read(f->application,(qa_actor_owner)id,&gear,error) &&
                qa_application_equipment_content_current(f->application,&gear)) {
                owner=(char *)qa_strings_cstr(qa_session_strings(qa_application_session(f->application)),(qa_string_id)id);
                tag=4; valid=owner!=NULL;
            }
        }
        break;
    case QA_AUDIO_REFERENCE_BUS:
        valid=audio_scope(f,id,&tag,&key,&role) && tag!=15; if (tag==1) tag=0; break;
    case QA_AUDIO_REFERENCE_RESOURCE: valid=audio_resource_present(operation,id); break;
    case QA_AUDIO_REFERENCE_KEY:
        valid=frontend_event_static_index(f,id,&key);
        if(!valid) { tag=19; valid=frontend_scene_static_audio_identity_encode(operation->space,id,&key,error); }
        break;
    case QA_AUDIO_REFERENCE_ACTOR: break;
    }
    bool ok=valid && qa_source_save_writer(&io,NULL,error) && qa_source_save_u32(&io,&tag) &&
        (kind==QA_AUDIO_REFERENCE_OWNER && (!tag || tag==4)?frontend_save_text(&io,&owner):qa_source_save_u64(&io,&key)) &&
        ((tag!=7 && tag!=8 && tag!=13 && tag!=14 && tag!=17 && tag!=21) || qa_source_save_u64(&io,&role)) &&
        qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    return ok || frontend_fail(error,QA_ERROR_FORMAT,"Audio reference leaves its actual frontend owner graph");
}
static bool audio_decode(void *context, qa_audio_reference_kind kind, qa_bytes bytes, uint64_t *out, qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    if(kind==QA_AUDIO_REFERENCE_ACTOR) {
        if(bytes.size==8) return frontend_audio_id_decode(f,bytes,out,error);
        qa_source_save_io actor={0}; uint32_t domain=0; uint64_t owner=0,row=0;
        bool ok=qa_source_save_reader(&actor,NULL,bytes,error) && qa_source_save_u32(&actor,&domain) && domain==15 &&
            qa_source_save_u64(&actor,&owner) && qa_source_save_u64(&actor,&row) && qa_source_save_finish(&actor,NULL) &&
            frontend_unified_graph_audio_resolve(f,domain,owner,row,out);
        qa_source_save_dispose(&actor);
        return ok || frontend_fail(error,QA_ERROR_FORMAT,"Saved Unified mixer route leaves its actual replica ledger");
    }
    qa_source_save_io io={0}; uint32_t tag=0; uint64_t key=0,id=0,role=0; char *owner=NULL;
    bool ok=f && out && qa_source_save_reader(&io,NULL,bytes,error) && qa_source_save_u32(&io,&tag) &&
        (kind==QA_AUDIO_REFERENCE_OWNER && (!tag || tag==4)?frontend_save_text(&io,&owner):qa_source_save_u64(&io,&key)) &&
        ((tag!=7 && tag!=8 && tag!=13 && tag!=14 && tag!=17 && tag!=21) || qa_source_save_u64(&io,&role)) &&
        qa_source_save_finish(&io,NULL);
    if (ok) switch (kind) {
    case QA_AUDIO_REFERENCE_OWNER:
        if ((tag>=1 && tag<=3) || (tag>=5 && tag<=14) || tag==16 || tag==17 || tag==18 || tag==20 || tag==21) ok=audio_scope_decode(f,tag,key,role,&id);
        else if (!tag && owner) {
            id=qa_strings_find(qa_session_strings(qa_application_session(f->application)),(qa_bytes){(const uint8_t *)owner,strlen(owner)});
            ok=id && id<=UINT32_MAX && qa_application_provider_instance(f->application,(qa_actor_owner)id);
        } else if (tag==4 && owner) {
            qa_application_equipment_content gear;
            id=qa_strings_find(qa_session_strings(qa_application_session(f->application)),(qa_bytes){(const uint8_t *)owner,strlen(owner)});
            ok=id && id<=UINT32_MAX && qa_application_equipment_content_read(f->application,
                (qa_actor_owner)id,&gear,error) && qa_application_equipment_content_current(f->application,&gear);
        } else ok=false;
        break;
    case QA_AUDIO_REFERENCE_BUS: ok=(tag==0 || tag==2 || tag==3 || (tag>=5 && tag<=14) || tag==16 || tag==17 || tag==18 || tag==20 || tag==21) &&
        audio_scope_decode(f,tag?tag:1,key,role,&id); break;
    case QA_AUDIO_REFERENCE_RESOURCE: ok=!tag && audio_resource_present(operation,key); id=key; break;
    case QA_AUDIO_REFERENCE_KEY:
        ok=tag==19?frontend_scene_static_audio_identity_decode(operation->space,key,&id,error):
            !tag && frontend_event_static_key(f,key,&id); break;
    case QA_AUDIO_REFERENCE_ACTOR: ok=false; break;
    }
    free(owner); qa_source_save_dispose(&io);
    if (ok) *out=id;
    return ok || frontend_fail(error,QA_ERROR_FORMAT,"Saved audio reference has no real candidate owner");
}
static bool asset_encode(void *context, const qa_audio_asset *asset, qa_buffer *out, qa_error *error)
{ return frontend_audio_asset_encode(((frontend_persistence *)context)->audio,asset,out,error); }
static bool asset_decode(void *context, qa_bytes bytes, qa_audio_asset **out, qa_error *error)
{ return frontend_audio_asset_decode(((frontend_persistence *)context)->audio,bytes,out,error); }
static qa_audio_checkpoint_refs audio_refs(frontend_persistence *operation)
{
    return (qa_audio_checkpoint_refs){.context=operation,.encode=audio_encode,.decode=audio_decode,
        .asset_encode=asset_encode,.asset_decode=asset_decode};
}
static frontend_unified_graph_refs unified_refs(frontend_persistence *operation,const qa_audio_checkpoint_refs *audio)
{
    qa_frontend *f=operation->candidate?operation->candidate:operation->active;
    return (frontend_unified_graph_refs){.content=qa_application_content_graph_read(f->application),
        .scene=operation->space,.models=operation->models,.roots=operation->roots,
        .assets=operation->audio,.audio=audio,.components=operation->component_scenes};
}
static bool music_sources_capture(frontend_persistence *operation,qa_buffer *out,qa_error *error)
{
    qa_frontend *f=operation->candidate?operation->candidate:operation->active;
    qa_audio_checkpoint_refs refs=audio_refs(operation); qa_buffer state={0};
    bool ok=f->music_sources && frontend_music_sources_checkpoint(f->music_sources,
        qa_application_content_graph_read(f->application),&refs,&state,error);
    ok=ok && optional_encode("QFMW",true,(qa_bytes){state.data,state.size},out,error);
    qa_buffer_free(&state); return ok;
}
static bool music_sources_prepare(frontend_persistence *operation,qa_error *error)
{
    qa_frontend *f=operation->candidate; qa_source_save_io io={0};
    uint8_t magic[4]={0}; bool present=false; qa_bytes state={0};
    bool ok=f && !f->music_sources && qa_source_save_reader(&io,NULL,
        section(&operation->sections,SECTION_MUSIC_SOURCES),error) &&
        qa_source_save_bytes(&io,magic,4) && !memcmp(magic,"QFMW",4) &&
        qa_source_save_bool(&io,&present) && present &&
        blob(&io,&state) && state.size && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); qa_audio_checkpoint_refs refs=audio_refs(operation);
    if(ok && present) ok=frontend_music_sources_restore_prepare(f,
        qa_application_content_graph_read(f->application),&refs,state,&f->music_sources,error);
    return ok || frontend_fail(error,QA_ERROR_FORMAT,"Music source graph leaves its saved physical content and controls");
}
static qa_scene_frame_checkpoint_refs frame_refs(frontend_scene_namespace *space)
{
    return (qa_scene_frame_checkpoint_refs){.context=space,.image_encode=frontend_scene_image_encode,.image_decode=frontend_scene_image_decode,
        .geometry_encode=frontend_scene_geometry_encode,.geometry_decode=frontend_scene_geometry_decode,
        .material_encode=frontend_scene_material_encode,.material_decode=frontend_scene_material_decode,
        .mesh_identity_encode=frontend_scene_mesh_identity_encode,.mesh_identity_decode=frontend_scene_mesh_identity_decode,
        .light_identity_encode=frontend_scene_light_identity_encode,.light_identity_decode=frontend_scene_light_identity_decode};
}
static bool renderer_resource_capture(void *context,const qa_scene_image *image,
    const qa_scene_geometry *geometry,size_t ordinal,qa_error *error)
{
    frontend_scene_namespace *space=context; uint64_t key=0;
    if ((image!=NULL)==(geometry!=NULL))
        return frontend_fail(error,QA_ERROR_FORMAT,"Renderer resource row requires one actual typed holder");
    return image?frontend_scene_image_encode(space,image,&key,error):
        frontend_scene_namespace_capture_renderer_geometry(space,1,ordinal,geometry,error);
}
static bool renderer_resources_capture(qa_frontend *f,frontend_scene_namespace *space,qa_error *error)
{
    if (f->cpu && f->gl) return frontend_fail(error,QA_ERROR_FORMAT,"Frontend installs two renderer destructor owners");
    return (!f->cpu || qa_cpu_checkpoint_resources(f->cpu,renderer_resource_capture,space,error)) &&
        (!f->gl || qa_gl_checkpoint_resources(f->gl,renderer_resource_capture,space,error));
}
static bool renderer_mesh_capture(void *context,uint64_t identity,uint64_t revision,
    const qa_scene_geometry *geometry,size_t ordinal,qa_error *error)
{
    frontend_scene_namespace *space=context; uint64_t key=0; (void)revision;
    if (!identity || !qa_scene_geometry_active(geometry))
        return frontend_fail(error,QA_ERROR_FORMAT,"Renderer live mesh row has no genuine identity/geometry");
    return frontend_scene_geometry_encode(space,geometry,&key,error) &&
        frontend_scene_namespace_capture_renderer_mesh(space,1,ordinal,identity,error);
}
#define RENDER_SCENE_REFS(name,type) \
static bool renderer_##name##_encode(void *context,const type *value,uint64_t *id,qa_error *error) \
{ return frontend_scene_##name##_encode(((frontend_persistence *)context)->space,value,id,error); } \
static bool renderer_##name##_decode(void *context,uint64_t id,const type **value,qa_error *error) \
{ return frontend_scene_##name##_decode(((frontend_persistence *)context)->space,id,value,error); }
RENDER_SCENE_REFS(image,qa_scene_image)
RENDER_SCENE_REFS(geometry,qa_scene_geometry)
RENDER_SCENE_REFS(material,qa_material)
RENDER_SCENE_REFS(world,qa_scene_world)
#undef RENDER_SCENE_REFS
static bool renderer_mesh_encode(void *context,uint64_t value,uint64_t *id,qa_error *error)
{ return frontend_scene_mesh_identity_encode(((frontend_persistence *)context)->space,value,id,error); }
static bool renderer_mesh_decode(void *context,uint64_t id,uint64_t *value,qa_error *error)
{ return frontend_scene_mesh_identity_decode(((frontend_persistence *)context)->space,id,value,error); }
static bool renderer_assets_encode(void *context,const qa_q3_presentation_assets *assets,uint64_t *id,qa_error *error)
{ return frontend_q3_assets_encode(((frontend_persistence *)context)->q3,assets,id,error); }
static bool renderer_assets_decode(void *context,uint64_t id,qa_q3_presentation_assets **assets,qa_error *error)
{ return frontend_q3_assets_decode(((frontend_persistence *)context)->renderer_q3,id,assets,error); }
static qa_render_checkpoint_refs renderer_refs(frontend_persistence *operation)
{
    return (qa_render_checkpoint_refs){.context=operation,
        .image_encode=renderer_image_encode,.image_decode=renderer_image_decode,
        .geometry_encode=renderer_geometry_encode,.geometry_decode=renderer_geometry_decode,
        .material_encode=renderer_material_encode,.material_decode=renderer_material_decode,
        .world_encode=renderer_world_encode,.world_decode=renderer_world_decode,
        .assets_encode=renderer_assets_encode,.assets_decode=renderer_assets_decode,
        .mesh_identity_encode=renderer_mesh_encode,.mesh_identity_decode=renderer_mesh_decode};
}
static bool renderer_fields(qa_source_save_io *io,uint32_t expected,qa_bytes *display,qa_bytes *renderer)
{
    uint8_t magic[4]={'Q','F','R','D'}; uint32_t kind=expected;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFRD",4) &&
        qa_source_save_u32(io,&kind) && kind==expected &&
        blob(io,display) && blob(io,renderer) &&
        (kind ? display->size!=0 && renderer->size!=0 : display->size==0 && renderer->size==0);
}
static bool renderer_kind(const qa_frontend *f,uint32_t *kind,qa_error *error)
{
    if (!f || !kind || (f->options.dedicated ? f->display || f->cpu || f->gl :
        !f->display || (f->cpu!=NULL)==(f->gl!=NULL) ||
        (f->cpu!=NULL)!=(f->options.display.backend==QA_DISPLAY_CPU)))
        return frontend_fail(error,QA_ERROR_FORMAT,"Frontend renderer differs from its genuine display/backend topology");
    *kind=f->options.dedicated?0:f->cpu?1:2; return true;
}
static bool renderer_checkpoint(frontend_persistence *operation,qa_buffer *out,qa_error *error)
{
    qa_frontend *f=operation->candidate?operation->candidate:operation->active;
    qa_buffer display={0},renderer={0}; uint32_t kind=0;
    qa_render_checkpoint_refs refs=renderer_refs(operation);
    bool ok=renderer_kind(f,&kind,error);
    if (ok && kind) ok=operation->restored_from?
        qa_display_restore_checkpoint(operation->restored_from->display_guard,&display,error):
        qa_display_checkpoint(f->display,&display,error);
    if (ok && kind==1) ok=qa_cpu_checkpoint(f->cpu,&refs,&renderer,error);
    if (ok && kind==2) ok=operation->restored_from?
        qa_gl_restore_checkpoint(operation->restored_from->gl_guard,&refs,&renderer,error):
        qa_gl_checkpoint(f->gl,&refs,&renderer,error);
    qa_source_save_io io={0}; qa_bytes display_bytes={display.data,display.size},renderer_bytes={renderer.data,renderer.size};
    ok=ok && qa_source_save_writer(&io,NULL,error) && renderer_fields(&io,kind,&display_bytes,&renderer_bytes) &&
        qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&display); qa_buffer_free(&renderer); return ok;
}
static bool renderer_resource_qualify(void *context,const qa_scene_image *image,
    const qa_scene_geometry *geometry,size_t ordinal,qa_error *error)
{
    frontend_scene_namespace *space=context; uint64_t key=0;
    if ((image!=NULL)==(geometry!=NULL))
        return frontend_fail(error,QA_ERROR_FORMAT,"Restored renderer requires one actual typed resource holder");
    return image?frontend_scene_image_encode(space,image,&key,error):
        frontend_scene_namespace_qualify_renderer_geometry(space,1,ordinal,geometry,error);
}
static bool renderer_mesh_qualify(void *context,uint64_t identity,uint64_t revision,
    const qa_scene_geometry *geometry,size_t ordinal,qa_error *error)
{
    frontend_scene_namespace *space=context; (void)revision;
    if (!identity || !qa_scene_geometry_active(geometry))
        return frontend_fail(error,QA_ERROR_FORMAT,"Restored renderer mesh has no actual active cache allocation");
    return frontend_scene_namespace_qualify_renderer_geometry(space,1,ordinal,geometry,error) &&
        frontend_scene_namespace_qualify_renderer_mesh(space,1,ordinal,identity,error);
}
static bool renderer_restore(frontend_persistence *operation,qa_error *error)
{
    qa_frontend *f=operation->candidate; uint32_t kind=0; qa_bytes display={0},renderer={0};
    qa_source_save_io io={0};
    bool ok=renderer_kind(operation->active,&kind,error) && !f->display && !f->cpu && !f->gl &&
        qa_source_save_reader(&io,NULL,section(&operation->sections,SECTION_RENDERER),error) &&
        renderer_fields(&io,kind,&display,&renderer) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (ok && kind) ok=qa_display_restore(display,operation->active->display,&f->display,&operation->display_guard,error);
    qa_render_checkpoint_refs refs=renderer_refs(operation);
    if (ok && kind==1) {
        qa_cpu_options options; qa_cpu_options_default(&options);
        options.width=f->width; options.height=f->height; options.owner=QA_FRONTEND_COMMAND_OWNER;
        options.present=qa_display_present_cpu; options.present_context=f->display;
        ok=qa_cpu_restore(renderer,&options,&refs,&f->cpu,error) &&
            qa_cpu_checkpoint_resources(f->cpu,renderer_resource_qualify,operation->space,error);
    }
    if (ok && kind==2) {
        qa_gl_options options; qa_gl_options_default(&options);
        options.display=f->display; options.owner=QA_FRONTEND_COMMAND_OWNER;
        ok=qa_gl_restore(renderer,&options,&refs,operation->active->gl,&f->gl,&operation->gl_guard,error) &&
            qa_gl_checkpoint_resources(f->gl,renderer_resource_qualify,operation->space,error) &&
            qa_gl_checkpoint_meshes(f->gl,renderer_mesh_qualify,operation->space,error);
    }
    return ok || frontend_fail(error,QA_ERROR_FORMAT,"Saved renderer lacks its complete genuine candidate display/resource graph");
}
static bool aliases_fields(qa_source_save_io *io, qa_frontend *f, frontend_scene_namespace *space)
{
    uint8_t magic[4]={'Q','F','A','L'}; uint64_t background=0;
    bool reading=io->direction==QA_SOURCE_SAVE_READ, order=f->frame.material_order!=NULL;
    if (!reading && ((f->frame.material_order && f->frame.material_order!=f->order) ||
        (f->console_background && !frontend_scene_image_encode(space,f->console_background,&background,io->error)))) return false;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFAL",4) ||
        !qa_source_save_u64(io,&background) ||
        !qa_source_save_bool(io,&order) || (order && !f->order) ||
        (f->options.dedicated?(background!=0 || order):background==0)) return false;
    if (reading) {
        if (f->console_background || f->frame.material_order) return false;
        const qa_scene_image *image=NULL;
        if (background && !frontend_scene_image_decode(space,background,&image,io->error)) return false;
        size_t image_index=0;
        const qa_scene_resources *owners[1]={f->ui_images};
        if (image && !qa_scene_image_owner_index(owners,1,image,&image_index)) return false;
        if (order && !qa_scene_frame_material_order(&f->frame,f->order,io->error)) return false;
        if (image) qa_scene_image_retain(image);
        f->console_background=(qa_scene_image *)image;
    }
    return true;
}
static bool aliases_checkpoint(qa_frontend *f, frontend_scene_namespace *space, qa_buffer *out, qa_error *error)
{
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && aliases_fields(&io,f,space) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
static bool aliases_restore(qa_frontend *f, frontend_scene_namespace *space, qa_bytes bytes, qa_error *error)
{
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && aliases_fields(&io,f,space) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    return ok || frontend_fail(error,QA_ERROR_FORMAT,"Frontend aliases differ from genuine imported holders");
}
static void cut_destroy(frontend_persistence *operation)
{
    frontend_q3_source_color_defer_finish(&operation->color_ticket);
    frontend_q3_source_color_defer_abort(&operation->color_ticket);
    frontend_remote_q3_graph_destroy(operation->remote_graph); operation->remote_graph=NULL;
    frontend_unified_graph_destroy(operation->unified_graph); operation->unified_graph=NULL;
    frontend_classic_client_graph_destroy(operation->classic_graph); operation->classic_graph=NULL;
    frontend_q2_client_graph_destroy(operation->q2_graph); operation->q2_graph=NULL;
    frontend_component_scene_restore_set_destroy(operation->component_scenes); operation->component_scenes=NULL;
    frontend_q3_inventory_destroy(operation->q3); operation->q3=NULL;
    frontend_q3_inventory_destroy(operation->renderer_q3); operation->renderer_q3=NULL;
    frontend_world_inventory_destroy(operation->roots); operation->roots=NULL;
    frontend_scene_namespace_destroy(operation->space); operation->space=NULL;
    qa_scene_image_set_destroy(operation->images); operation->images=NULL;
    qa_audio_asset_inventory_destroy(operation->audio); operation->audio=NULL;
    if (operation->scenes) frontend_scene_inventory_destroy(operation->scenes);
    else frontend_models_destroy(operation->models);
    operation->scenes=NULL; operation->models=NULL;
    frontend_topology_destroy(operation->topology); operation->topology=NULL;
    frontend_native_q3_topology_destroy(operation->native_topology); operation->native_topology=NULL;
    qa_input_platform_restore_guard_destroy(operation->input_guard); operation->input_guard=NULL;
    qa_audio_device_restore_guard_destroy(operation->device_guard); operation->device_guard=NULL;
    qa_gl_restore_guard_destroy(operation->gl_guard); operation->gl_guard=NULL;
    qa_display_restore_guard_destroy(operation->display_guard); operation->display_guard=NULL;
    sections_destroy(&operation->sections);
    for (size_t i=0;i<7;++i) qa_buffer_free(operation->external+i);
    frontend_capture_end(operation->capture); operation->capture=NULL;
    frontend_capture_end(operation->candidate_capture); operation->candidate_capture=NULL;
}
static bool module_movies(void *context,const frontend_remote_q3_module_topology *role,
    qa_q3_movie_checkpoint_refs *out,qa_error *error)
{ return frontend_q3_module_movie_refs(((frontend_persistence *)context)->q3,role,out,error); }
static frontend_remote_q3_modules_save_refs module_save_refs(frontend_persistence *operation)
{ return (frontend_remote_q3_modules_save_refs){operation,module_movies}; }
static bool capture_platform(frontend_persistence *operation, qa_buffer *out, qa_error *error)
{
    qa_frontend *f=operation->candidate?operation->candidate:operation->active;
    qa_input_platform_checkpoint_refs refs=input_refs(f); qa_buffer state={0};
    bool ok=!f->input || (operation->restored_from?
        qa_input_platform_restore_checkpoint(operation->restored_from->input_guard,&refs,&state,error):
        qa_input_platform_checkpoint(f->input,&refs,&state,error));
    ok=ok && optional_encode("QFIP",f->input!=NULL,(qa_bytes){state.data,state.size},out,error);
    qa_buffer_free(&state); return ok;
}
static bool capture_device(frontend_persistence *operation, qa_buffer *out, qa_error *error)
{
    qa_frontend *f=operation->candidate?operation->candidate:operation->active;
    qa_buffer state={0};
    bool ok=!f->device || (operation->restored_from?
        qa_audio_device_restore_checkpoint(operation->restored_from->device_guard,&state,error):
        qa_audio_device_checkpoint(f->device,f->audio,&state,error));
    ok=ok && optional_encode("QFAD",f->device!=NULL,(qa_bytes){state.data,state.size},out,error);
    qa_buffer_free(&state); return ok;
}
static bool capture_terminal(qa_frontend *f, qa_buffer *out, qa_error *error)
{
    qa_buffer state={0};
    bool ok=!f->terminal || qa_dedicated_console_checkpoint(f->terminal,&state,error);
    ok=ok && optional_encode("QFIT",f->terminal!=NULL,(qa_bytes){state.data,state.size},out,error);
    qa_buffer_free(&state); return ok;
}
static bool capture_engine(frontend_persistence *operation, qa_buffer *out, qa_error *error)
{
    qa_frontend *f=operation->candidate?operation->candidate:operation->active;
    qa_audio_checkpoint_refs refs=audio_refs(operation); qa_buffer state={0};
    bool ok=!f->audio || (frontend_audio_engine_options_ready(f) &&
        qa_audio_engine_checkpoint(f->audio,&refs,&state,error));
    if (!ok && error && error->code==QA_OK)
        frontend_fail(error,QA_ERROR_ARGUMENT,"Audio observer or allocation clock differs from its actual frontend owner");
    ok=ok && optional_encode("QFAE",f->audio!=NULL,(qa_bytes){state.data,state.size},out,error);
    qa_buffer_free(&state); return ok;
}
static bool capture_components(frontend_persistence *operation, qa_error *error)
{
    qa_frontend *f=operation->candidate?operation->candidate:operation->active;
    if (!f || f->capture!=operation->capture || !qa_application_content_graph_read(f->application))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture lost its actual content and child owner leases");
    frontend_section_set *set=&operation->sections;
    qa_tools_checkpoint_refs tools; qa_llm_checkpoint_refs llm; qa_buffer sky={0};
    frontend_scene_identity_scope event_scope={NULL,1};
    qa_audio_bank_checkpoint_refs banks=frontend_audio_content_refs(qa_application_content_graph_read(f->application));
    bool ok=frontend_scene_inventory_capture(f,&operation->scenes,error) &&
        frontend_scene_namespace_create(&operation->space,error) &&
        frontend_scene_namespace_capture_images(operation->space,f,error) &&
        frontend_materials_capture_namespace(f,operation->space,error) &&
        frontend_scene_inventory_namespace(operation->scenes,operation->space,error) &&
        frontend_materials_capture_world_history(f,operation->space,error);
    operation->models=frontend_scene_inventory_models(operation->scenes);
    event_scope.space=operation->space;
    ok=ok && frontend_event_capture_namespace(f,&event_scope,error) &&
        renderer_resources_capture(f,operation->space,error) &&
        (!f->gl || qa_gl_checkpoint_meshes(f->gl,renderer_mesh_capture,operation->space,error)) &&
        frontend_scene_namespace_capture_frame(operation->space,1,&f->frame,error) &&
        frontend_component_scenes_capture_frames(f,operation->space,error) &&
        frontend_unified_graph_capture_numbers(f,operation->space,error) &&
        frontend_classic_client_graph_capture_numbers(f,operation->space,error) &&
        frontend_scene_namespace_seal(operation->space,error) &&
        (!operation->canonical || frontend_scene_namespace_rebase_capture(operation->space,operation->canonical,error)) &&
        frontend_world_inventory_capture(f,operation->scenes,operation->space,&operation->roots,error) &&
        frontend_audio_banks_checkpoint(f,&banks,&operation->audio,set->owned+SECTION_BANKS,error) &&
        frontend_tools_checkpoint_resolvers(f,&tools,&llm,error);
    qa_scene_frame_checkpoint_refs frame=frame_refs(operation->space);
    qa_audio_checkpoint_refs audio=audio_refs(operation);
    frontend_unified_graph_refs unified=unified_refs(operation,&audio);
    frontend_keys_cvar_refs keys=key_registry_refs(f);
    frontend_q3_refs q3={qa_application_content_graph_read(f->application),operation->space,operation->models,operation->roots,operation->audio};
    frontend_remote_q3_modules_save_refs modules=module_save_refs(operation);
    ok=ok && frontend_shared_resource_policy_live_checkpoint(f,set->owned+SECTION_RESOURCE_POLICY,error) &&
        frontend_q3_inventory_capture(f,&q3,&operation->q3,error) &&
        frontend_component_scenes_checkpoint(f,&(frontend_component_scene_save_refs){
            .content=q3.content,.scene=operation->space,.frame=&frame,.q3=operation->q3},
            set->owned+SECTION_COMPONENT_SCENES,error) &&
        frontend_unified_graph_checkpoint(f,&unified,set->owned+SECTION_UNIFIED_GRAPH,error) &&
        frontend_classic_client_graph_checkpoint(f,&(frontend_remote_q1_restore_refs){
            .content=q3.content,.scene=operation->space,.models=operation->models,.roots=operation->roots,
            .assets=operation->audio,.audio=&audio},set->owned+SECTION_CLASSIC_CLIENT_GRAPH,error) &&
        frontend_q2_client_graph_checkpoint(f,&(frontend_remote_q2_restore_refs){
            .content=q3.content,.scene=operation->space,.models=operation->models,.roots=operation->roots},
            set->owned+SECTION_Q2_CLIENT_GRAPH,error) &&
        frontend_remote_q3_graph_checkpoint(f,&modules,set->owned+SECTION_REMOTE_GRAPH,error) &&
        frontend_topology_checkpoint(f,set->owned+SECTION_TOPOLOGY,error) &&
        frontend_event_topology_checkpoint(f,set->owned+SECTION_EVENTS_TOPOLOGY,error) &&
        frontend_visual_topology_checkpoint(f,set->owned+SECTION_VISUAL_TOPOLOGY,error) &&
        frontend_equipment_topology_checkpoint(f,set->owned+SECTION_EQUIPMENT_TOPOLOGY,error) &&
        frontend_equipment_q3_topology_checkpoint(f,set->owned+SECTION_SELECTED_Q3_TOPOLOGY,error) &&
        frontend_selected_character_topology_checkpoint(f,set->owned+SECTION_CHARACTER_TOPOLOGY,error) &&
        frontend_selected_effects_topology_checkpoint(f,set->owned+SECTION_EFFECTS_TOPOLOGY,error) &&
        frontend_equipment_gear_topology_checkpoint(f,set->owned+SECTION_GEAR_TOPOLOGY,error) &&
        frontend_native_q2_topology_checkpoint(f,set->owned+SECTION_NATIVE_TOPOLOGY,error) &&
        frontend_native_q3_topology_checkpoint(f,set->owned+SECTION_NATIVE_Q3_TOPOLOGY,error) &&
        frontend_tools_checkpoint(f,&tools,&llm,set->owned+SECTION_TOOLS,error) &&
        frontend_images_checkpoint(f,set->owned+SECTION_IMAGES,error) &&
        frontend_scene_namespace_checkpoint(operation->space,set->owned+SECTION_NAMESPACE,error) &&
        frontend_image_owners_checkpoint(f,operation->space,set->owned+SECTION_IMAGE_OWNERS,error) &&
        frontend_materials_checkpoint(f,operation->space,set->owned+SECTION_MATERIALS,error) &&
        frontend_fonts_checkpoint(f,operation->space,set->owned+SECTION_FONTS,error) &&
        frontend_models_checkpoint(operation->models,set->owned+SECTION_MODELS,error) &&
        frontend_world_inventory_checkpoint(operation->roots,set->owned+SECTION_ROOTS,error) &&
        qa_scene_frame_checkpoint(&f->frame,&frame,set->owned+SECTION_FRAME,error) &&
        frontend_event_checkpoint(f,operation->audio,&event_scope,set->owned+SECTION_EVENTS,error) &&
        frontend_particle_checkpoint(f,set->owned+SECTION_PARTICLES,error) &&
        frontend_qc_rerelease_checkpoint(f,set->owned+SECTION_QC_DEBUG,error) &&
        frontend_players_checkpoint(f,set->owned+SECTION_PLAYERS,error) &&
        frontend_native_q2_private_checkpoint(f,set->owned+SECTION_NATIVE,error) &&
        frontend_seats_checkpoint(f,operation->space,set->owned+SECTION_SEATS_INPUT,set->owned+SECTION_SEATS_PRESENTATION,error) &&
        frontend_equipment_events_checkpoint(f->gear_events,set->owned+SECTION_GEAR_EVENTS,error) &&
        frontend_shader_checkpoint(f,set->owned+SECTION_SHADERS,error) &&
        aliases_checkpoint(f,operation->space,set->owned+SECTION_ALIASES,error) &&
        renderer_checkpoint(operation,set->owned+SECTION_RENDERER,error) &&
        frontend_audio_id_checkpoint(f,set->owned+SECTION_AUDIO_IDS,error) &&
        music_sources_capture(operation,set->owned+SECTION_MUSIC_SOURCES,error) &&
        frontend_view_settings_checkpoint(f->view_settings,set->owned+SECTION_VIEW_SETTINGS,error) &&
        frontend_material_movie_inventory_checkpoint(f,operation->space,&frame,&audio,operation->q3,set->owned+SECTION_MATERIAL_MOVIES,error) &&
        (f->options.dedicated || (f->q1_sky && frontend_q1_sky_checkpoint(f->q1_sky,operation->space,&sky,error))) &&
        optional_encode("QFQK",!f->options.dedicated,(qa_bytes){sky.data,sky.size},set->owned+SECTION_Q1_SKY,error) &&
        frontend_qc_messages_checkpoint(f->qc_messages,set->owned+SECTION_QC_MESSAGES,error) &&
        frontend_q3_source_color_checkpoint(f,set->owned+SECTION_SOURCE_COLOR,error) &&
        frontend_renderer_materials_checkpoint(f,set->owned+SECTION_RENDERER_MATERIALS,error) &&
        frontend_renderer_worlds_checkpoint(f,operation->roots,set->owned+SECTION_RENDERER_WORLDS,error) &&
        frontend_renderer_registries_checkpoint(f,operation->q3,operation->roots,set->owned+SECTION_RENDERER_REGISTRIES,error) &&
        capture_engine(operation,set->owned+SECTION_ENGINE,error) &&
        frontend_ui_features_checkpoint(f,operation->audio,set->owned+SECTION_UI_FEATURES,error) &&
        capture_device(operation,set->owned+SECTION_DEVICE,error) &&
        capture_platform(operation,set->owned+SECTION_PLATFORM,error) && capture_terminal(f,set->owned+SECTION_TERMINAL,error) &&
        frontend_save_commands_checkpoint(f,set->owned+SECTION_SAVE_COMMANDS,error) &&
        frontend_restart_binding_checkpoint(f,set->owned+SECTION_RESTART,error) &&
        frontend_keys_checkpoint(f->keys,qa_application_content_graph_read(f->application),&keys,set->owned+SECTION_KEYS,error) &&
        frontend_config_store_checkpoint(f->config_store,qa_application_content_graph_read(f->application),&keys,
            set->owned+SECTION_CONFIG_STORE,error) &&
        frontend_client_registries_checkpoint(f,qa_application_content_graph_read(f->application),
            set->owned+SECTION_CLIENT_REGISTRIES,error) &&
        settings_storage_capture(f,set->owned+SECTION_GLOBAL_SETTINGS,error) &&
        frontend_input_profile_checkpoint(f,qa_application_content_graph_read(f->application),set->owned+SECTION_INPUT_PROFILE,error) &&
        frontend_q3_checkpoint(f,&q3,set->owned+SECTION_Q3,error) && frontend_source_checkpoint(f,set->owned+SECTION_SOURCE,error) &&
        qa_native_runtime_checkpoint(f->native_runtime,set->owned+SECTION_NATIVE_RUNTIME,error);
    qa_buffer_free(&sky);
    if (ok) section_publish(set);
    return ok;
}
static bool capture_owner(void *context, qa_application *application, qa_buffer *out, qa_error *error)
{
    frontend_owner_binding *binding=context; frontend_persistence *operation=binding->operation;
    qa_frontend *f=operation->candidate && operation->candidate->application==application?
        operation->candidate:operation->active;
    if (!f || f->application!=application || !operation->capture)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"External capture lost its actual frontend owner");
    if (!operation->captured) {
        if (!capture_components(operation,error) ||
            !frontend_network_checkpoint(f,operation->external+1,operation->external+2,error)) return false;
        operation->captured=true;
    }
    switch (binding->kind) {
    case QA_SAVE_CAMPAIGN:
        return frontend_campaign_checkpoint(f,qa_application_content_graph_read(application),out,error);
    case QA_SAVE_CONNECTIONS: case QA_SAVE_PREDICTION:
        return copy_bytes((qa_bytes){operation->external[binding->ordinal].data,operation->external[binding->ordinal].size},out,error);
    default: {
        qa_source_save_io io={0};
        bool ok=qa_source_save_writer(&io,NULL,error) && owner_envelope(&io,binding->kind,&operation->sections) && qa_source_save_finish(&io,out);
        qa_source_save_dispose(&io);
        return ok || frontend_fail(error,QA_ERROR_FORMAT,"Frontend owner envelope is incomplete");
    }
    }
}
static bool content_visit(void *context, const qa_application *application,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    frontend_persistence *operation=context;
    qa_frontend *f=operation->candidate && operation->candidate->application==application?
        operation->candidate:operation->active;
    return frontend_content_visit(f,application,visitor,error) &&
        (!operation->services || !operation->services->visit_content ||
        operation->services->visit_content(operation->services->context,application,visitor,error));
}
static bool read_saved_sections(frontend_persistence *operation, const qa_save_image *image, qa_error *error)
{
    if (operation->sections_read) return true;
    for (size_t i=0;i<7;++i) {
        const qa_save_record *record=qa_save_image_find(image,operation->bindings[i].kind,"");
        const qa_save_owner *identity=&operation->owners[i].identity;
        if (!record || strcmp(record->owner.schema,identity->schema) ||
            strcmp(record->owner.backend,identity->backend) || !qa_sha256_equal(&record->owner.content,&identity->content))
            return frontend_fail(error,QA_ERROR_FORMAT,"Saved external owner schema differs from its concrete frontend producer");
        if (!copy_bytes(record->payload,operation->external+i,error)) return false;
        qa_bytes bytes={operation->external[i].data,operation->external[i].size};
        if (i>2) {
            qa_source_save_io io={0};
            bool ok=qa_source_save_reader(&io,NULL,bytes,error) && owner_envelope(&io,operation->bindings[i].kind,&operation->sections) &&
                qa_source_save_finish(&io,NULL);
            qa_source_save_dispose(&io);
            if (!ok) return frontend_fail(error,QA_ERROR_FORMAT,"Saved external section inventory is incomplete");
        }
    }
    operation->sections_read=true; return true;
}
static bool prepare_services(void *context, qa_application *candidate, const qa_save_image *image, qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    if (!f || f->application || !candidate) return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend candidate service prefix was already installed");
    f->application=candidate; operation->restore_image=image;
    qa_frontend *native_source=operation->fresh_original?operation->constructor:operation->active;
    qa_audio_output_format output=native_source->device?
        qa_audio_device_requested_configuration(native_source->device).format:native_source->audio_output_format;
    f->audio_output_format=output;
    bool ok=frontend_equipment_events_create(f,&f->gear_events,error) && read_saved_sections(operation,image,error) &&
        (!native_source->cpu || qa_cpu_gamma_read(native_source->cpu,&f->options.gamma,error)) &&
        (!native_source->gl || qa_gl_gamma_read(native_source->gl,&f->options.gamma,error)) &&
        /* The configuration has not decoded a source tuple yet. Declare the
         * real ENGINE factory; canonical QACV later imports its saved values. */
        frontend_shared_register(qa_application_cvars(candidate),NULL,output,f->options.gamma,error) &&
        frontend_view_bindings_create(f,error) &&
        settings_storage_prepare(operation,error) &&
        restore_configuration_prefix(operation,error) &&
        frontend_topology_decode(candidate,section(&operation->sections,SECTION_TOPOLOGY),&operation->topology,error) &&
        frontend_topology_prepare(f,operation->topology,error) &&
        frontend_renderer_materials_prepare_restored(f,section(&operation->sections,SECTION_RENDERER_MATERIALS),error) &&
        frontend_remote_q3_graph_decode(f,section(&operation->sections,SECTION_REMOTE_GRAPH),
            &operation->remote_graph,error) &&
        frontend_unified_graph_decode(f,section(&operation->sections,SECTION_UNIFIED_GRAPH),
            &operation->unified_graph,error) &&
        frontend_classic_client_graph_decode(f,section(&operation->sections,SECTION_CLASSIC_CLIENT_GRAPH),
            &operation->classic_graph,error) &&
        frontend_q2_client_graph_decode(f,section(&operation->sections,SECTION_Q2_CLIENT_GRAPH),
            &operation->q2_graph,error) &&
        frontend_native_q3_topology_decode(f,section(&operation->sections,SECTION_NATIVE_Q3_TOPOLOGY),
            &operation->native_topology,error) &&
        frontend_event_prepare_restored(f,section(&operation->sections,SECTION_EVENTS_TOPOLOGY),error) &&
        frontend_visual_prepare_restored(f,section(&operation->sections,SECTION_VISUAL_TOPOLOGY),error) &&
        frontend_selected_character_prepare_restored(f,section(&operation->sections,SECTION_CHARACTER_TOPOLOGY),error) &&
        frontend_equipment_q3_prepare_restored(f,section(&operation->sections,SECTION_SELECTED_Q3_TOPOLOGY),error) &&
        frontend_equipment_prepare_restored(f,section(&operation->sections,SECTION_EQUIPMENT_TOPOLOGY),error) &&
        frontend_native_q2_prepare_restored(f,section(&operation->sections,SECTION_NATIVE_TOPOLOGY),error) &&
        frontend_tools_prepare_restored(f,section(&operation->sections,SECTION_TOOLS),error) &&
        frontend_restart_binding_restore(f,section(&operation->sections,SECTION_RESTART),error) &&
        frontend_commands(f,error) && frontend_network_prepare_restored(f,
            (qa_bytes){operation->external[1].data,operation->external[1].size},error);
    if (ok && f->options.dedicated) {
        f->terminal=qa_dedicated_console_create(error); ok=f->terminal!=NULL;
    } else if (ok) {
        qa_input_platform_options options={.cvars=qa_application_cvars(candidate),.user=f,.print=frontend_print};
        f->input=qa_input_platform_create_detached(&options,error); ok=f->input!=NULL;
    }
    return ok && (!operation->services || !operation->services->prepare_services ||
        operation->services->prepare_services(operation->services->context,candidate,image,error));
}
static bool prepare_content(void *context, qa_application *candidate, const qa_launch_snapshot *snapshot,
    const qa_save_image *image, qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    if (!f || f->application!=candidate) return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend provider graph belongs to another candidate");
    return frontend_native_q3_topology_prepare(operation->native_topology,snapshot,error) &&
        frontend_source_complete_groups(f,error) &&
        frontend_selected_effects_prepare_restored(f,section(&operation->sections,SECTION_EFFECTS_TOPOLOGY),error) &&
        frontend_native_q2_topology_ready(f,error) &&
        frontend_event_topology_ready(f,error) && frontend_visual_topology_ready(f,error) && frontend_tools_attach_restored(f,error) &&
        (!operation->services || !operation->services->prepare_content ||
        operation->services->prepare_content(operation->services->context,candidate,snapshot,image,error));
}
static bool equipment_roots_restore(frontend_persistence *operation,qa_error *error)
{
    qa_frontend *f=operation->candidate;
    if (!frontend_equipment_media_bind_restored(f,error)) return false;
    size_t media_count=frontend_equipment_media_count(f);
    for (size_t i=0;i<frontend_world_inventory_model_count(operation->roots);++i) {
        frontend_scene_root_view root;
        if (!frontend_world_inventory_model_at(operation->roots,i,&root)) return false;
        if (root.owner.kind!=FRONTEND_SCENE_OWNER_EQUIPMENT) continue;
        frontend_equipment_media_view media;
        if (!root.owner.owner || root.owner.owner>media_count || root.owner.row!=1 ||
            !frontend_equipment_media_at(f,(size_t)root.owner.owner-1,&media) ||
            !media.declaration || media.declaration->none || root.visual_path ||
            root.source.resource!=media.held_parent.resource || root.source.files!=media.owner.mounts ||
            !frontend_scene_root_owner_ready(operation->roots,i+1,FRONTEND_SCENE_OWNER_EQUIPMENT,root.owner.owner,error))
            return error && error->code!=QA_OK?false:
                frontend_fail(error,QA_ERROR_FORMAT,"Saved held scene leaves its genuine equipment media row");
        if (!frontend_equipment_media_attach_restored(f,(size_t)root.owner.owner-1,root.source.model,
                (qa_scene_model *)root.scene,operation->models,error)) return false;
        frontend_scene_root_adopt(operation->roots,i+1);
    }
    return frontend_equipment_topology_ready(f,error);
}
static bool renderer_registries_roster(frontend_persistence *operation,qa_error *error)
{
    frontend_q3_inventory_destroy(operation->renderer_q3); operation->renderer_q3=NULL;
    qa_frontend *f=operation->candidate;
    frontend_q3_refs refs={qa_application_content_graph_read(f->application),operation->space,
        operation->models,operation->roots,NULL};
    return frontend_q3_inventory_restore_roster(f,&refs,&operation->renderer_q3,error);
}
static bool import_components(frontend_persistence *operation, qa_error *error)
{
    qa_frontend *f=operation->candidate; frontend_section_set *set=&operation->sections;
    qa_application_content_graph *content=qa_application_content_graph_read(f->application);
    qa_audio_bank_checkpoint_refs banks=frontend_audio_content_refs(content);
    qa_tools_checkpoint_refs tools; qa_llm_checkpoint_refs llm;
    qa_audio_checkpoint_refs audio=audio_refs(operation);
    frontend_unified_graph_refs unified=unified_refs(operation,&audio);
    qa_bytes engine={0},device={0},platform={0},terminal={0},sky={0};
    bool ok=frontend_component_scenes_prepare_restored(f,content,section(set,SECTION_COMPONENT_SCENES),
            &operation->component_scenes,error) &&
        application_q3_components_scenes_restore_prepare(f->application,error) &&
        frontend_unified_graph_prepare_components(operation->unified_graph,&unified,error) &&
        frontend_component_scene_restore_set_order(operation->component_scenes,error) &&
        frontend_renderer_worlds_prepare_restored(f,section(set,SECTION_RENDERER_WORLDS),error) &&
        frontend_view_settings_restore(f->view_settings,section(set,SECTION_VIEW_SETTINGS),error) &&
        frontend_equipment_gear_prepare_restored(f,section(set,SECTION_GEAR_TOPOLOGY),error) &&
        frontend_input_profile_restore(f,content,section(set,SECTION_INPUT_PROFILE),error) &&
        frontend_images_restore(f,section(set,SECTION_IMAGES),&operation->images,error) &&
        frontend_scene_namespace_restore(section(set,SECTION_NAMESPACE),operation->images,&operation->space,error) &&
        frontend_scene_namespace_bind_frame(operation->space,1,&f->frame,error) &&
        frontend_component_scenes_bind_frames(f,operation->space,error) &&
        frontend_image_owners_restore(f,operation->space,section(set,SECTION_IMAGE_OWNERS),error) &&
        frontend_materials_restore(f,operation->space,section(set,SECTION_MATERIALS),error) &&
        frontend_fonts_restore(f,operation->space,section(set,SECTION_FONTS),error) &&
        frontend_models_restore(content,section(set,SECTION_MODELS),&operation->models,error) &&
        frontend_world_inventory_restore(f,operation->models,operation->space,section(set,SECTION_ROOTS),&operation->roots,error) &&
        frontend_unified_graph_roots(operation->unified_graph,&(frontend_unified_graph_refs){
            .content=content,.scene=operation->space,.models=operation->models,.roots=operation->roots,
            .audio=&audio,.components=operation->component_scenes},error) &&
        frontend_classic_client_graph_roots(operation->classic_graph,&(frontend_remote_q1_restore_refs){
            .content=content,.scene=operation->space,.models=operation->models,.roots=operation->roots,
            .assets=operation->audio,.audio=&audio},error) &&
        frontend_q2_client_graph_roots(operation->q2_graph,&(frontend_remote_q2_restore_refs){
            .content=content,.scene=operation->space,.models=operation->models,.roots=operation->roots},error) &&
        frontend_renderer_worlds_attach_restored(f,operation->roots,error) &&
        frontend_roots_attach_restored(f,operation->roots,operation->models,error) &&
        frontend_source_roots_attach_restored(f,operation->roots,error) &&
        frontend_remote_roots_attach_restored(f,operation->roots,error) &&
        frontend_q3_inventory_restore_roster(f,&(frontend_q3_refs){content,operation->space,
            operation->models,operation->roots,NULL},&operation->renderer_q3,error) &&
        frontend_renderer_registries_prepare_restored(f,operation->renderer_q3,operation->roots,
            section(set,SECTION_RENDERER_REGISTRIES),error) &&
        renderer_registries_roster(operation,error) &&
        music_sources_prepare(operation,error) &&
        equipment_roots_restore(operation,error) &&
        aliases_restore(f,operation->space,section(set,SECTION_ALIASES),error) && renderer_restore(operation,error) &&
        frontend_source_renderer_runtime_bind(f,error) &&
        frontend_q3_source_color_restore(f,operation->display_guard,section(set,SECTION_SOURCE_COLOR),error) &&
        frontend_remote_q3_graph_prepare_modules(operation->remote_graph,error) &&
        frontend_remote_q3_graph_prepare_runtime(operation->remote_graph,error) &&
        frontend_source_material_bindings_restore(f,error) &&
        frontend_native_q3_material_bindings_restore(f,error) &&
        frontend_remote_q3_material_bindings_restore(f,error) &&
        frontend_remote_q3_initial_material_bindings_restore(f,error) &&
        frontend_component_scenes_bind_restored(f,error) &&
        frontend_renderer_materials_bind_restored(f,error) &&
        frontend_renderer_worlds_bind_restored(f,error) &&
        frontend_audio_id_restore(f,section(set,SECTION_AUDIO_IDS),error) &&
        frontend_audio_banks_restore(f,&banks,section(set,SECTION_BANKS),&operation->audio,error);
    frontend_scene_identity_scope events={operation->space,1};
    qa_scene_frame_checkpoint_refs frame=frame_refs(operation->space);
    unified=unified_refs(operation,&audio);
    ok=ok && frontend_event_restore(f,operation->audio,&events,section(set,SECTION_EVENTS),error) &&
        frontend_tools_checkpoint_resolvers(f,&tools,&llm,error) && frontend_tools_restore(f,&tools,&llm,section(set,SECTION_TOOLS),error) &&
        frontend_save_commands_restore(f,section(set,SECTION_SAVE_COMMANDS),error) &&
        (f->options.dedicated || frontend_seats_create_restored(f,frontend_topology_mods(operation->topology),
            section(set,SECTION_SEATS_PRESENTATION),error)) &&
        frontend_material_movie_inventory_restore_unified(f,operation->space,&frame,&audio,operation->q3,section(set,SECTION_MATERIAL_MOVIES),error) &&
        frontend_unified_graph_audio_prefix(operation->unified_graph,&unified,error) &&
        optional_decode("QFQK",!f->options.dedicated,section(set,SECTION_Q1_SKY),&sky,error) &&
        (f->options.dedicated || frontend_q1_sky_restore(f,operation->space,sky,&f->q1_sky,error)) &&
        optional_decode("QFAE",f->audio!=NULL,section(set,SECTION_ENGINE),&engine,error) &&
        (!f->audio || (qa_audio_engine_restore_into(f->audio,engine,&audio,error) &&
            (!f->music_sources || qa_audio_engine_music_controls_restore_bind(f->audio,
                frontend_music_sources_controls(f->music_sources),error)))) && frontend_event_reconnect_audio(f,error) &&
        optional_decode("QFAD",operation->active->device!=NULL,section(set,SECTION_DEVICE),&device,error) &&
        (!operation->active->device || qa_audio_device_restore(device,operation->active->device,f->audio,
            &f->device,&operation->device_guard,error)) &&
        frontend_native_q2_private_restore(f,section(set,SECTION_NATIVE),error) &&
        frontend_equipment_events_restore(f->gear_events,section(set,SECTION_GEAR_EVENTS),error) &&
        frontend_players_restore(f,section(set,SECTION_PLAYERS),error) &&
        frontend_particle_restore(f,section(set,SECTION_PARTICLES),error) && frontend_shader_restore(f,section(set,SECTION_SHADERS),error) &&
        frontend_qc_rerelease_restore(f,section(set,SECTION_QC_DEBUG),error) &&
        frontend_qc_messages_restore(f,section(set,SECTION_QC_MESSAGES),&f->qc_messages,error) &&
        optional_decode("QFIP",f->input!=NULL,section(set,SECTION_PLATFORM),&platform,error) &&
        (!f->input || qa_input_platform_restore(f->input,operation->active->input,&(qa_input_platform_checkpoint_refs){
            .context=f,.seat_encode=input_seat_encode,.seat_decode=input_seat_decode,
            .haptics={f,haptic_resource_encode,haptic_resource_decode}},platform,&operation->input_guard,error)) &&
        optional_decode("QFIT",f->terminal!=NULL,section(set,SECTION_TERMINAL),&terminal,error) &&
        (!f->terminal || qa_dedicated_console_restore(f->terminal,terminal,error));
    frontend_q3_refs q3={content,operation->space,operation->models,operation->roots,operation->audio};
    frontend_remote_q3_modules_save_refs modules=module_save_refs(operation);
    ok=ok && frontend_q3_prepare(f,&q3,section(set,SECTION_Q3),&operation->q3,error) &&
        qa_scene_frame_restore(&f->frame,section(set,SECTION_FRAME),&frame,error) &&
        frontend_scene_namespace_qualify_frame(operation->space,1,&f->frame,error) &&
        frontend_material_movie_inventory_restore(f,operation->space,&frame,&audio,operation->q3,section(set,SECTION_MATERIAL_MOVIES),error) &&
        frontend_q3_restore(operation->q3,(double)f->time_ns/1000000.0,error) &&
        frontend_component_scenes_restore_continuation(operation->component_scenes,
            &(frontend_component_scene_save_refs){.content=content,.scene=operation->space,
                .frame=&frame,.q3=operation->q3},error) &&
        application_q3_components_scenes_restore_finish(f->application,error) &&
        frontend_component_scenes_finish_restore(f,error) &&
        frontend_unified_graph_finish(operation->unified_graph,&unified,error) &&
        frontend_classic_client_graph_finish(operation->classic_graph,&(frontend_remote_q1_restore_refs){
            .content=content,.scene=operation->space,.models=operation->models,.roots=operation->roots,
            .assets=operation->audio,.audio=&audio},error) &&
        frontend_q2_client_graph_finish(operation->q2_graph,&(frontend_remote_q2_restore_refs){
            .content=content,.scene=operation->space,.models=operation->models,.roots=operation->roots},error) &&
        frontend_seats_recipients_restore(f,error) &&
        frontend_seats_restore(f,operation->space,section(set,SECTION_SEATS_INPUT),section(set,SECTION_SEATS_PRESENTATION),error) &&
        frontend_remote_q3_graph_restore_children(operation->remote_graph,error) &&
        frontend_remote_q3_graph_restore_modules(operation->remote_graph,&modules,error) &&
        frontend_remote_q3_graph_restore_frames(operation->remote_graph,error) &&
        frontend_remote_q3_graph_finish_modules(operation->remote_graph,&modules,error) &&
        frontend_remote_q3_graph_finish_sources(operation->remote_graph,error) &&
        frontend_source_worlds_rebind_restored(f,error) &&
        frontend_world_inventory_ready(operation->roots,error) && frontend_scene_namespace_seal(operation->space,error) &&
        frontend_models_install(operation->models,error) &&
        frontend_ui_features_restore(f,operation->audio,section(set,SECTION_UI_FEATURES),error) &&
        qa_audio_asset_inventory_ready(operation->audio,error) &&
        frontend_shared_resource_policy_live_restore(f,section(set,SECTION_RESOURCE_POLICY),error);
    if (ok) {
        /* The real banks, registry, engine and event rows now own every saved
         * holder. Temporary construction refs must precede final recapture. */
        qa_audio_asset_inventory_destroy(operation->audio); operation->audio=NULL;
        qa_scene_image_set_destroy(operation->images); operation->images=NULL;
        operation->imported=true;
    }
    return ok;
}
static bool restore_owner(void *context, qa_application *candidate, qa_bytes bytes, qa_error *error)
{
    frontend_owner_binding *binding=context; frontend_persistence *operation=binding->operation;
    if (!operation->candidate || operation->candidate->application!=candidate || operation->imported ||
        (operation->restored&(1u<<binding->ordinal)) || bytes.size!=operation->external[binding->ordinal].size ||
        (bytes.size && memcmp(bytes.data,operation->external[binding->ordinal].data,bytes.size)))
        return frontend_fail(error,QA_ERROR_FORMAT,"External import leaves its complete retained owner inventory");
    operation->restored|=1u<<binding->ordinal;
    if(binding->kind==QA_SAVE_CONNECTIONS) {
        qa_audio_checkpoint_refs audio=audio_refs(operation);
        frontend_unified_graph_refs unified=unified_refs(operation,&audio);
        if(!application_save_console_context_from_image(candidate,operation->restore_image,
                &operation->client_console_context,error) ||
            !application_save_console_resolvers(&operation->client_console_context,&operation->client_console,error) ||
            !frontend_unified_graph_stage(operation->unified_graph,unified.content,&operation->client_console,error) ||
            !frontend_classic_client_graph_stage(operation->classic_graph,
                &(frontend_remote_q1_restore_refs){.content=unified.content,.audio=&audio},&operation->client_console,error) ||
            !frontend_q2_client_graph_stage(operation->q2_graph,
                &(frontend_remote_q2_restore_refs){.content=unified.content},&operation->client_console,error) ||
            !frontend_network_restore_connections_prefix(operation->candidate,bytes,&operation->network_staged,error) ||
            !frontend_unified_graph_prepare(operation->unified_graph,&unified,error) ||
            !frontend_classic_client_graph_prepare(operation->classic_graph,error) ||
            !frontend_q2_client_graph_prepare(operation->q2_graph,error) ||
            !frontend_remote_q3_graph_prepare(operation->remote_graph,error)) return false;
    }
    if (binding->kind!=QA_SAVE_MEDIA) return true;
    if (operation->restored!=127u) return frontend_fail(error,QA_ERROR_FORMAT,"MEDIA import preceded a required external owner");
    return import_components(operation,error);
}
static bool reconnect(void *context, qa_application *candidate, const qa_save_image *image, qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    return (f && f->application==candidate && operation->imported && operation->restored==127u &&
        frontend_source_complete_groups(f,error) && frontend_native_q2_topology_ready(f,error) &&
        frontend_event_topology_ready(f,error) && frontend_visual_topology_ready(f,error) &&
        (!operation->services || !operation->services->reconnect ||
        operation->services->reconnect(operation->services->context,candidate,image,error))) ||
        frontend_fail(error,QA_ERROR_FORMAT,"Frontend reconnect requires every real imported external owner");
}

static bool ranking_capture(void *context,qa_application_ranking_effect_fn installed,
    void *binding,qa_buffer *out,qa_error *error)
{
    frontend_persistence *operation=context;
    qa_frontend *f=binding==operation->active?operation->active:
        binding==operation->candidate?operation->candidate:NULL;
    qa_application_ranking_checkpoint_refs refs=frontend_ranking_refs(f);
    return refs.capture(refs.context,installed,binding,out,error);
}
static bool client_commands_capture(void *context,qa_application *application,
    const qa_application_console_scope *scope,qa_console *console,qa_buffer *out,qa_error *error)
{
    frontend_persistence *operation=context;
    qa_frontend *f=operation->candidate && operation->candidate->application==application?operation->candidate:
        operation->active && operation->active->application==application?operation->active:NULL;
    if (!f) return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT command capture has no actual frontend application owner");
    bool generic=frontend_client_source_commands_owned(f,application,scope,console);
    bool network=frontend_network_client_commands_owned(f,application,scope,console);
    if (generic==network)
        return frontend_fail(error,QA_ERROR_FORMAT,"CLIENT command console has no unique physical frontend owner");
    return generic?frontend_client_source_commands_capture(f,application,scope,console,out,error):
        frontend_network_client_commands_capture(f,application,scope,console,out,error);
}
static bool client_commands_restore(void *context,qa_application *application,
    const qa_application_console_scope *scope,qa_console *console,qa_bytes bytes,qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    if (!f || f->application!=application)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT command import has no actual isolated frontend owner");
    bool generic=frontend_client_source_commands_owned(f,application,scope,console);
    bool network=frontend_network_client_commands_owned(f,application,scope,console);
    if (generic==network)
        return frontend_fail(error,QA_ERROR_FORMAT,"CLIENT command import has no unique physical frontend owner");
    return generic?frontend_client_source_commands_restore(f,application,scope,console,bytes,error):
        frontend_network_client_commands_restore(f,application,scope,console,bytes,error);
}
static bool ranking_resolve(void *context,qa_bytes bytes,qa_application_ranking_effect_fn *installed,
    void **binding,qa_error *error)
{
    frontend_persistence *operation=context;
    qa_application_ranking_checkpoint_refs refs=frontend_ranking_refs(operation->candidate);
    return refs.resolve(refs.context,bytes,installed,binding,error);
}
static bool ranking_handoff(void *context,qa_rankings *active,qa_rankings *candidate,
    bool *relinquish,qa_error *error)
{
    frontend_persistence *operation=context;
    return operation->services->rankings_handoff(operation->services->context,active,candidate,relinquish,error);
}
static bool native_baseline(void *context,qa_application *candidate,qa_actor_owner owner,
    qa_application_native_baseline_services *services,qa_error *error)
{
    frontend_persistence *operation=context;
    if (operation->services && operation->services->prepare_native_baseline)
        return operation->services->prepare_native_baseline(operation->services->context,candidate,owner,services,error);
    return frontend_native_q2_baseline_prepare(operation->candidate,candidate,owner,services,error);
}
static bool owners_match(frontend_persistence *operation,const qa_save_image *image,qa_error *error)
{
    qa_frontend *f=operation->candidate?operation->candidate:operation->active;
    for (size_t i=0;i<7;++i) {
        const qa_save_record *record=qa_save_image_find(image,operation->bindings[i].kind,"");
        qa_buffer actual={0};
        bool ok=record && capture_owner(operation->bindings+i,f->application,&actual,error);
        if (ok && (actual.size!=record->payload.size || memcmp(actual.data,record->payload.data,actual.size)))
            ok=frontend_fail(error,QA_ERROR_FORMAT,"Restored frontend owner differs from its saved physical continuation");
        qa_buffer_free(&actual);
        if (!ok) return false;
    }
    return true;
}
static bool native_clients_restore(frontend_persistence *operation,qa_error *error)
{
    qa_frontend *f=operation->candidate;
    for (size_t i=0;i<frontend_native_q3_topology_count(operation->native_topology);++i) {
        frontend_native_q3 *row=NULL; frontend_native_q3_import *saved=NULL;
        q3n_client_refs resources;
        if (!frontend_native_q3_topology_read(operation->native_topology,i,&row,&saved,&resources,error) ||
            !row || !frontend_native_q3_prepare_services(row,error)) return false;
        frontend_native_q3_factory factory={.context=f,.compose=frontend_native_composition_create};
        if (!frontend_native_q3_prepare_composition(row,&factory,error)) return false;
        if (!frontend_native_q3_read(f,i,&saved->owners,error)) return false;
        bool found=false;
        if (!qa_application_character_selection_read(f->application,saved->owners.launch_seat,
                &saved->character,&found,error) || !found)
            return error && error->code!=QA_OK?false:
                frontend_fail(error,QA_ERROR_FORMAT,"Native import lacks its actual restored CHARACTER owner");
        bool held=qa_q3_assets_capture_begin(saved->owners.assets,error);
        bool ok=held && frontend_native_q3_restore(f,saved,&resources,&row,error);
        if (held) qa_q3_assets_capture_end(saved->owners.assets);
        if (saved->character.lifetime) {
            saved->character.release(saved->character.lifetime);
            saved->character=(qa_native_q3_character_selection){0};
        }
        if (!ok) return false;
    }
    return true;
}
static bool validate(void *context,qa_application *application,const qa_save_image *image,qa_error *error)
{
    frontend_persistence *operation=context;
    if (!operation->candidate) {
        return application==operation->active->application && operation->capture && operation->captured &&
            owners_match(operation,image,error) && (!operation->services || !operation->services->validate ||
            operation->services->validate(operation->services->context,application,image,error));
    }
    qa_frontend *f=operation->candidate;
    if (f->application!=application || !operation->imported || operation->finished)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend final validation requires its complete isolated candidate");
    bool ok=native_clients_restore(operation,error) && frontend_network_restore_connections(f,
        (qa_bytes){operation->external[1].data,operation->external[1].size},error) &&
        frontend_network_restore_prediction(f,
        (qa_bytes){operation->external[2].data,operation->external[2].size},error) &&
        frontend_source_restore(f,section(&operation->sections,SECTION_SOURCE),error) &&
        frontend_seats_restore_finish(f,error);
    ok=ok && (!f->music_sources || frontend_music_sources_restore_finish(f->music_sources,error));
    if (ok) {
        ok=frontend_config_store_finish_restore(f->config_store,error) &&
            frontend_client_registries_finish_restore(f,error) && frontend_keys_finish_restore(f->keys,error) &&
            frontend_client_sources_finish_restore(f,error) && frontend_network_client_sources_finish_restore(f,error);
    }
    if(ok) ok=frontend_root_sidecars_bind_restored(f,error);
    if (ok) {
        frontend_native_q2_topology_finish(f);
        frontend_source_finish_groups(f);
        operation->finished=true;
        ok=frontend_campaign_restore(f,qa_application_content_graph_read(application),
            (qa_bytes){operation->external[0].data,operation->external[0].size},error);
    }
    if (ok) ok=frontend_equipment_q3_topology_ready(f,error) && frontend_equipment_gear_topology_ready(f,error) &&
        frontend_selected_character_topology_ready(f,error) &&
        frontend_selected_effects_topology_ready(f,error);
    if (ok && operation->services && operation->services->validate)
        ok=operation->services->validate(operation->services->context,application,image,error);
    /* The dictionary imported into real owner fields retains its saved IDs.
     * A fresh typed capture matches those exact domain/owner/node ordinals;
     * no installed identity or opaque record byte is rewritten. */
    frontend_persistence check={.active=operation->active,.candidate=f,
        .canonical=operation->space,.restored_from=operation};
    for (size_t i=0;i<7;++i)
        check.bindings[i]=(frontend_owner_binding){&check,operation->bindings[i].kind,i};
    if (ok) ok=frontend_capture_begin(f,&check.capture,error) && owners_match(&check,image,error);
    if (ok) {
        /* The application's subsequent content comparison still visits the
         * genuine frontend inventory under this same child-owner lease. */
        operation->candidate_capture=check.capture; check.capture=NULL;
    }
    cut_destroy(&check);
    return ok;
}
static void close_captures(frontend_persistence *operation)
{
    frontend_capture_end(operation->candidate_capture); operation->candidate_capture=NULL;
    frontend_capture_end(operation->capture); operation->capture=NULL;
}
static bool discard_services(void *context,qa_application *candidate,qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    close_captures(operation);
    if (!f || (f->application && f->application!=candidate))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Failed application belongs to another frontend graph");
    if (!f->application) f->application=candidate;
    if (!frontend_q3_source_color_abort(&operation->color_ticket,error)) return false;
    if (f->input && !qa_input_platform_settings_idle(f->input))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Failed candidate retains its native input settings preparation");
    if (!frontend_seat_callbacks_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Failed candidate retains actual seat callbacks or source release history");
    if (!frontend_ui_features_idle(f) || !frontend_remote_q3_idle(f) || !frontend_qc_messages_idle(f->qc_messages))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Failed candidate retains UI preparation or remote CLIENT children");
    if (!frontend_equipment_events_idle(f->gear_events) || !frontend_equipment_gear_idle(f) ||
        !frontend_selected_effects_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Failed candidate retains gear delivery or selected effect callbacks");
    if (!frontend_shared_resource_policy_live_destroy(f,error)) return false;
    if (!frontend_network_close_client(f,error) || !frontend_cinematic_destroy(f,error) ||
        !frontend_selected_effects_retire(f,error) || !frontend_remote_q3_destroy(f,error) ||
        !frontend_remote_q3_initial_destroy_all(f,error) ||
        !frontend_native_q3_destroy(f,error) ||
        !frontend_equipment_events_destroy(f->gear_events,error)) return false;
    f->gear_events=NULL;
    if (!frontend_equipment_gear_retire(f,error)) return false;
    frontend_equipment_gear_destroy(f);
    if (operation->services && operation->services->discard_services &&
        !operation->services->discard_services(operation->services->context,candidate,error)) return false;
    if (!frontend_qc_rerelease_idle(f) || !frontend_equipment_retire(f,error) ||
        !frontend_equipment_q3_retire(f,error) ||
        !frontend_selected_character_retire(f,error) ||
        !frontend_save_commands_destroy(f,error) || !frontend_campaign_destroy(f,error)) return false;
    frontend_equipment_destroy(f);
    frontend_equipment_q3_destroy(f);
    frontend_qc_rerelease_destroy(f);
    for (unsigned i=0;i<f->options.seats;++i)
        if (f->seats[i].input && !qa_input_seat_release(f->seats[i].input,
            (double)f->wall_time_ns/1000000.0,error)) return false;
    /* The application is destroyed after this callback returns. Retire its
     * prompt callbacks while the physical UI and input parents still exist. */
    for (unsigned i=0;i<f->options.seats;++i)
        if (!frontend_source_prompt_destroy(&f->seats[i].source_prompt,error)) return false;
    qa_input_platform_destroy(f->input); f->input=NULL;
    qa_input_console_destroy(f->input_commands); f->input_commands=NULL;
    for (unsigned i=0;i<f->options.seats;++i) {
        if (!qa_ui_llm_destroy(f->seats[i].assistance,(double)f->time_ns/1000000.0,error)) return false;
        f->seats[i].assistance=NULL;
    }
    return frontend_qc_messages_destroy(&f->qc_messages,error) && frontend_q1_sky_destroy(&f->q1_sky,error) &&
        frontend_music_sources_destroy(&f->music_sources,error) &&
        frontend_view_settings_destroy(&f->view_settings,error) &&
        frontend_tools_before_world_change(f,error) &&
        (!f->audio || qa_audio_engine_acoustics_release(f->audio,error)) &&
        frontend_client_sources_destroy(f,error) &&
        qa_application_retire_sources(candidate,error) &&
        frontend_config_store_restore_abort_unbound(f->config_store,candidate,error) &&
        frontend_root_resources_destroy(f,error) &&
        frontend_network_destroy(f,error) && frontend_tools_destroy(f,error);
}
static bool publish_ready(void *context,qa_application *active,qa_application *candidate,qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    qa_frontend *native_source=operation->fresh_original?operation->constructor:operation->active;
    close_captures(operation);
    if (!operation->finished || !operation->slot || *operation->slot!=operation->active ||
        operation->active->application!=active || !f || f->application!=candidate ||
        !frontend_owners_idle(operation->active) || !frontend_owners_idle(f) ||
        !frontend_seat_callbacks_checkpoint_ready(operation->active,error) ||
        !frontend_seat_callbacks_checkpoint_ready(f,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend publication lost its idle active/candidate ownership cut");
    if ((!f->options.dedicated && !frontend_q1_sky_publish_ready(f->q1_sky,error)) ||
        !frontend_campaign_publish_ready(f,error) || !frontend_client_registries_rebind_ready(f,f,error) ||
        !frontend_equipment_q3_topology_ready(f,error) || !frontend_equipment_gear_topology_ready(f,error) ||
        !frontend_selected_character_topology_ready(f,error) ||
        !frontend_selected_character_rebind_ready(f,f,error) || !frontend_selected_effects_topology_ready(f,error) ||
        !frontend_selected_effects_rebind_ready(f,f,error) || !frontend_source_rebind_ready(f,operation->active,error) ||
        !frontend_native_q2_rebind_ready(f,operation->active,error) ||
        !frontend_native_q3_rebind_ready(f,operation->active,error) ||
        !frontend_tools_rebind_ready(f,operation->active,error) ||
        !qa_http_handoff_ready(frontend_tools_http(native_source),frontend_tools_http(f),error) ||
        !(operation->fresh_original?frontend_network_fresh_ready(f,operation->active,operation->constructor,error):
            frontend_network_rebind_ready(f,operation->active,error)) ||
        (operation->input_guard && !qa_input_platform_handoff_ready(operation->input_guard,error))) return false;
    if (operation->services && operation->services->publish_ready &&
        !operation->services->publish_ready(operation->services->context,active,candidate,error)) return false;
    if ((operation->display_guard && !qa_display_handoff_prepare(operation->display_guard,error)) ||
        (operation->gl_guard && !qa_gl_handoff_prepare(operation->gl_guard,error)) ||
        (operation->device_guard && !qa_audio_device_handoff_prepare(operation->device_guard,error))) return false;
    if (f->source_color &&
        (!(operation->color_ticket ? frontend_q3_source_color_ready(operation->color_ticket,error) :
            frontend_q3_source_color_restore_prepare(f,f->display,&operation->color_ticket,error)) ||
        !frontend_q3_source_color_ready_is(operation->color_ticket))) return false;
    return (!operation->display_guard || qa_display_handoff_ready(operation->display_guard,error)) &&
        (!operation->gl_guard || qa_gl_handoff_ready(operation->gl_guard,error)) &&
        (!operation->device_guard || qa_audio_device_handoff_ready(operation->device_guard,error)) &&
        (!operation->color_ticket || frontend_q3_source_color_ready_is(operation->color_ticket));
}
static void publish(void *context,qa_application *active,qa_application *candidate)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    if (operation->services && operation->services->publish)
        operation->services->publish(operation->services->context,active,candidate);
    qa_frontend *native_source=operation->fresh_original?operation->constructor:operation->active;
    if (operation->fresh_original) frontend_network_publish_fresh(operation->active,f,operation->constructor);
    else frontend_network_transport_exchange(operation->active,f);
    qa_http_handoff_publish(frontend_tools_http(native_source),frontend_tools_http(f));
    if (operation->input_guard) qa_input_platform_handoff(operation->input_guard);
    if (operation->display_guard) qa_display_handoff(operation->display_guard);
    if (operation->color_ticket) frontend_q3_source_color_publish(operation->color_ticket);
    if (operation->gl_guard) qa_gl_handoff(operation->gl_guard);
    if (operation->device_guard) qa_audio_device_handoff(operation->device_guard);
    frontend_campaign_publish_restored(f);
    frontend_equipment_events_rebind(f->gear_events,f);
    frontend_view_bindings_restore_published(f);
    operation->active->archive_enabled=false;
    f->archive_enabled=true; f->archive_saved=false;
    f->sdl_subsystems=operation->active->sdl_subsystems; operation->active->sdl_subsystems=0;
    *operation->slot=f;
}
static bool content_archive_open(void *context,const char *path,qa_fs_file **out,
    qa_fs_identity *identity,qa_error *error)
{
    frontend_persistence *operation=context;
    const qa_vfs_checkpoint_refs *refs=operation->services?operation->services->content_files:NULL;
    return refs && refs->archive_open?refs->archive_open(refs->context,path,out,identity,error):
        qa_fs_file_open(path,out,identity,error);
}
static bool content_directory_open(void *context,const char *mount_path,const char *retained_path,
    const qa_fs_identity *identity,qa_fs_root **out,qa_error *error)
{
    frontend_persistence *operation=context;
    const qa_frontend *constructor=operation->constructor?operation->constructor:operation->active;
    qa_fs_root *profile=qa_application_player_profile_root(constructor->application);
    if (!out || *out || !retained_path || !identity)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Content directory admission needs a saved identity and empty owner");
    if (profile) {
        char *path=NULL; qa_fs_entry_kind kind; qa_fs_identity actual;
        if (!qa_fs_root_join(profile,"",&path,error)) return false;
        bool same=!strcmp(path,retained_path); free(path);
        if (same) {
            if (!qa_fs_root_status(profile,"",&kind,&actual,error)) return false;
            if (kind!=QA_FS_DIRECTORY || !qa_fs_root_identity_is(profile,&actual) ||
                !qa_fs_root_identity_is(profile,identity))
                return frontend_fail(error,QA_ERROR_FORMAT,"Saved input profile directory differs from its retained native object");
            qa_fs_root_retain(profile); *out=profile; return true;
        }
    }
    const qa_vfs_checkpoint_refs *refs=operation->services?operation->services->content_files:NULL;
    if (refs && refs->directory_open)
        return refs->directory_open(refs->context,mount_path,retained_path,identity,out,error);
    if (!qa_fs_root_open(retained_path,out,error)) return false;
    qa_fs_entry_kind kind; qa_fs_identity actual;
    return qa_fs_root_status(*out,"",&kind,&actual,error) &&
        ((kind==QA_FS_DIRECTORY && qa_fs_root_identity_is(*out,&actual) &&
            qa_fs_root_identity_is(*out,identity)) ||
         frontend_fail(error,QA_ERROR_FORMAT,"Saved content directory differs from its actual native object"));
}
static bool operation_init(frontend_persistence *operation,qa_frontend *active,
    const qa_application_persistence_ops *services,qa_error *error)
{
    static const qa_save_owner_kind kinds[7]={QA_SAVE_CAMPAIGN,QA_SAVE_CONNECTIONS,QA_SAVE_PREDICTION,
        QA_SAVE_PRESENTATION,QA_SAVE_AUDIO,QA_SAVE_INPUT,QA_SAVE_MEDIA};
    static const char *schemas[7]={"qa.frontend.campaign","qa.frontend.connections","qa.frontend.prediction",
        "qa.frontend.presentation","qa.frontend.audio","qa.frontend.input","qa.frontend.media"};
    if (!active || !active->application || (services && ((services->owner_count && !services->owners) ||
        services->owner_count>QA_SAVE_OWNER_LIMIT-7 || ((services->publish_ready!=NULL)!=(services->publish!=NULL)))))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend persistence requires a real active owner and paired backend publication callbacks");
    operation->active=active; operation->services=services;
    for (size_t i=0;i<7;++i) {
        operation->bindings[i]=(frontend_owner_binding){operation,kinds[i],i};
        operation->owners[i]=(qa_application_persistence_owner){
            .identity={.kind=kinds[i],.instance="",.schema=schemas[i],.backend=""},
            .context=operation->bindings+i,.capture=capture_owner,.restore=restore_owner};
    }
    size_t extra=services?services->owner_count:0;
    operation->producer_inventory=calloc(7+extra,sizeof(*operation->producer_inventory));
    if (!operation->producer_inventory) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining concrete frontend persistence producer inventory");
    memcpy(operation->producer_inventory,operation->owners,sizeof(operation->owners));
    for (size_t i=0;i<extra;++i) {
        const qa_application_persistence_owner *owner=services->owners+i;
        if (owner->identity.kind!=QA_SAVE_PROVIDER || !owner->identity.instance || !owner->identity.instance[0])
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Additional frontend producers must name selected external provider instances");
        operation->producer_inventory[7+i]=*owner;
    }
    operation->ranking=(qa_application_ranking_checkpoint_refs){operation,ranking_capture,ranking_resolve};
    operation->content_files=(qa_vfs_checkpoint_refs){operation,content_archive_open,content_directory_open};
    operation->native_resource_refs=frontend_native_resource_refs(&operation->native_resources);
    operation->ops=(qa_application_persistence_ops){.context=operation,
        .owners=operation->producer_inventory,.owner_count=7+extra,.visit_content=content_visit,
        .content_files=&operation->content_files,.native_resources=services && services->native_resources?
            services->native_resources:&operation->native_resource_refs,
        .rankings=services?services->rankings:NULL,
        .progress=services?services->progress:NULL,.ranking_source=&operation->ranking,
        .rankings_handoff=services && services->rankings_handoff?ranking_handoff:NULL,
        .prepare_services=prepare_services,.prepare_native_baseline=native_baseline,.prepare_content=prepare_content,
        .client_commands_capture=client_commands_capture,.client_commands_restore=client_commands_restore,
        .reconnect=reconnect,.validate=validate,.publish_ready=publish_ready,.publish=publish,.discard_services=discard_services};
    return true;
}
static void native_capture_close(frontend_persistence *operation)
{
    if (!operation->native_resources.captured) return;
    qa_error cleanup={0};
    if (!qa_native_resource_inventory_release(&operation->native_resources.captured,&cleanup)) {
        /* The active frontend was proved to have no previous refused capture
         * before this operation began. Keep this admitted partial graph there. */
        operation->active->native_resource_inventory_pending=operation->native_resources.captured;
        operation->native_resources.captured=NULL;
    }
}
bool frontend_persistence_capture(qa_frontend *f,const qa_application_persistence_ops *services,
    qa_save_purpose purpose,qa_save_image **out,qa_error *error)
{
    if (!f || !out || *out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture needs an empty image output and returned native capture graph");
    if (!qa_save_image_destroy_checked(&f->save_image_pending,error) ||
        !qa_native_resource_inventory_release(&f->native_resource_inventory_pending,error)) return false;
    frontend_persistence operation={0};
    bool ok=operation_init(&operation,f,services,error) && frontend_capture_begin(f,&operation.capture,error) &&
        qa_application_persistence_capture(f->application,&operation.ops,purpose,out,error);
    native_capture_close(&operation); cut_destroy(&operation); free(operation.producer_inventory); return ok;
}
bool frontend_persistence_capture_detached(qa_frontend *f,const qa_application_persistence_ops *services,
    const frontend_persistence_native *native,qa_save_purpose purpose,qa_save_image **out,qa_error *error)
{
    if (!f || !native || !out || *out || (!!f->input!=!!native->input) ||
        (!!f->device!=!!native->device) || (!!f->display!=!!native->display) || (!!f->gl!=!!native->gl))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Detached capture needs each actual owner's qualified native guard");
    frontend_persistence cut={.input_guard=native->input,.device_guard=native->device,
        .display_guard=native->display,.gl_guard=native->gl};
    frontend_persistence operation={.restored_from=&cut};
    if (!qa_save_image_destroy_checked(&f->save_image_pending,error) ||
        !qa_native_resource_inventory_release(&f->native_resource_inventory_pending,error)) return false;
    bool ok=operation_init(&operation,f,services,error) && frontend_capture_begin(f,&operation.capture,error) &&
        qa_application_persistence_capture(f->application,&operation.ops,purpose,out,error);
    native_capture_close(&operation); cut_destroy(&operation); free(operation.producer_inventory); return ok;
}
static bool restore_frontend(qa_frontend **slot,const qa_application_persistence_ops *services,
    qa_frontend *constructor,const qa_save_image *image,qa_frontend **displaced,qa_frontend **retained,qa_error *error)
{
    if (!slot || !*slot || !image || !displaced || !retained || *retained ||
        slot==displaced || slot==retained || displaced==retained)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend restore needs distinct active, displaced and empty retained owner slots");
    frontend_persistence operation={.slot=slot,.constructor=constructor,.fresh_original=constructor!=NULL,
        .native_resources={.image=image}};
    bool ok=operation_init(&operation,*slot,services,error) &&
        frontend_capture_begin(operation.active,&operation.capture,error);
    const qa_frontend *source=constructor?constructor:operation.active;
    if (ok) ok=read_saved_sections(&operation,image,error) &&
        qa_native_runtime_validate(source->native_runtime,section(&operation.sections,SECTION_NATIVE_RUNTIME),error);
    if (ok) {
        operation.candidate=calloc(1,sizeof(*operation.candidate));
        if (!operation.candidate) ok=frontend_fail(error,QA_ERROR_MEMORY,"Allocating stable detached frontend owner");
    }
    if (ok) {
        qa_frontend *f=operation.candidate; f->options=source->options;
        f->seats=calloc(f->options.seats,sizeof(*f->seats));
        if (!f->seats) ok=frontend_fail(error,QA_ERROR_MEMORY,"Allocating stable detached frontend seats");
        else {
            for (unsigned i=0;i<f->options.seats;++i) { f->seats[i].frontend=f; f->seats[i].id=i; }
            qa_scene_frame_init(&f->frame,QA_FRONTEND_COMMAND_OWNER);
            f->native_runtime=source->native_runtime;
            qa_native_runtime_retain(f->native_runtime);
            if (source->default_user_root) {
                f->default_user_root=SDL_strdup(source->default_user_root);
                if (!f->default_user_root) ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining the candidate default user-content path");
                else f->options.application.user_root=f->default_user_root;
            }
            f->options.application.player_profile_root=qa_application_player_profile_root(source->application);
        }
    }
    if (ok) {
        operation.candidate->keys=frontend_keys_create(error);
        ok=operation.candidate->keys!=NULL;
        if (ok) {
            operation.candidate->config_store=frontend_config_store_create(operation.candidate,error);
            ok=operation.candidate->config_store!=NULL;
        }
    }
    qa_application *next=operation.active?operation.active->application:NULL,*old=NULL,*held=NULL;
    if (ok) {
        qa_application_options options=operation.candidate->options.application;
        frontend_application_options(operation.candidate,&options);
        ok=qa_application_persistence_restore(&next,&options,&operation.ops,image,&old,&held,error);
        if (!ok) operation.candidate->application=held;
    }
    cut_destroy(&operation); free(operation.producer_inventory);
    if (ok) {
        /* The concrete frontend heap was published at the same nofail boundary
         * as its application. Both displaced owners still have their original
         * stable callback contexts for separate ordinary retirement. */
        operation.active->application=old; *displaced=operation.active;
    } else if (operation.candidate && !operation.candidate->seats) {
        free(operation.candidate);
    } else if (operation.candidate) {
        qa_error cleanup={0};
        if (held || !qa_frontend_destroy(operation.candidate,&cleanup)) *retained=operation.candidate;
    }
    return ok;
}
bool frontend_persistence_restore(qa_frontend **slot,const qa_application_persistence_ops *services,
    const qa_save_image *image,qa_frontend **displaced,qa_frontend **retained,qa_error *error)
{ return restore_frontend(slot,services,NULL,image,displaced,retained,error); }
bool frontend_persistence_restore_original(qa_frontend **slot,const qa_application_persistence_ops *services,
    qa_frontend *source,const qa_save_image *image,qa_frontend **displaced,qa_frontend **retained,qa_error *error)
{
    if (!slot || !*slot || !source || source==*slot || !source->application ||
        source->stepping || source->preparing || source->capture || source->options.seats!=1)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original publication needs its real finished isolated singleplayer frontend");
    return restore_frontend(slot,services,source,image,displaced,retained,error);
}
bool qa_frontend_persistence_capture(qa_frontend *f,const qa_application_persistence_ops *services,
    qa_save_purpose purpose,qa_save_image **out,qa_error *error)
{ return frontend_persistence_capture(f,services,purpose,out,error); }
bool qa_frontend_persistence_restore(qa_frontend **slot,const qa_application_persistence_ops *services,
    const qa_save_image *image,qa_frontend **displaced,qa_frontend **retained,qa_error *error)
{ return frontend_persistence_restore(slot,services,image,displaced,retained,error); }
