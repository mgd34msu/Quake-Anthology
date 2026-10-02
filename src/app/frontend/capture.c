#include "capture.h"
#include "seat_save.h"
#include "q3_color_policy.h"
#include "visual_restore.h"
#include "source_restore.h"
#include "native_q2_save.h"
#include "native_q3_client.h"
#include "native_q3_video.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "remote_q3_modules.h"
#include "remote_q1_client.h"
#include "remote_q2_client.h"
#include "unified_media_inventory.h"
#include "source_cinematics.h"
#include "qa/q3_assets_custody.h"
#include "qa/material_source_scratch.h"
#include "qa/render_controls.h"
#include "qa/q3_source_scene_bank.h"
#include "network_restore.h"
#include "network_initial_graph.h"
#include "equipment_media.h"
#include "equipment_q3.h"
#include "equipment_gear.h"
#include "equipment_events.h"
#include "selected_character.h"
#include "selected_character_lifetime.h"
#include "selected_effects.h"
#include "save_commands.h"
#include "qc_rerelease_events.h"
#include "campaign_cinematic.h"
#include "ui_features.h"
#include "global_settings_storage.h"
#include "music_sources.h"
#include "material_movies.h"
#include "q1_sky.h"
#include "qc_messages.h"
#include "client_source.h"
#include "component_scene.h"
#include "renderer_materials.h"
#include "renderer_registries.h"
#include "renderer_worlds.h"
#include "remote_unified.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/material_save.h"
#include "qa/font_save.h"
#include "qa/q3_assets_save.h"
#include "qa/scene_world_save.h"
#include "qa/scene_model_save.h"
#include "qa/application_startup_prepare.h"
#include "startup_server_browser.h"
#include "source_prompt.h"

typedef enum capture_kind { CAPTURE_ASSETS, CAPTURE_IMAGES, CAPTURE_LIBRARY,
    CAPTURE_FONTS, CAPTURE_ORDER, CAPTURE_WORLD, CAPTURE_MODEL } capture_kind;
typedef struct capture_row {
    capture_kind kind;
    const void *owner;
    union {
        qa_scene_resources_capture *images;
        qa_font_library_capture *fonts;
        qa_scene_world_capture *world;
        qa_scene_model_capture *model;
    } token;
    bool held;
} capture_row;
struct frontend_capture {
    qa_frontend *frontend;
    capture_row *rows;
    size_t count, capacity;
    frontend_remote_q3_resources *remote;
    size_t remote_count;
    frontend_remote_q3_initial_view initial;
    bool has_initial;
};
typedef struct resource_row {
    capture_kind kind;
    const void *owner;
} resource_row;
typedef enum resource_scope_kind { RESOURCE_SOURCE, RESOURCE_SOURCE_ROLE, RESOURCE_NATIVE_Q3, RESOURCE_NATIVE_Q2,
    RESOURCE_REMOTE, RESOURCE_INITIAL, RESOURCE_REMOTE_Q1, RESOURCE_REMOTE_Q2,
    RESOURCE_VISUAL, RESOURCE_SELECTED, RESOURCE_CHARACTER, RESOURCE_EFFECTS, RESOURCE_GEAR, RESOURCE_CLIENT,
    RESOURCE_UNIFIED,RESOURCE_COMPONENT } resource_scope_kind;
typedef struct resource_scope {
    resource_scope_kind kind;
    size_t ordinal;
    const void *owner, *descriptor;
    qa_actor_owner provider, receiver;
    uint64_t service_owner;
    uint64_t identity;
    uint32_t seat, launch_seat;
    qa_actor_id actor;
    const qa_vfs *source_files, *mounts;
    unsigned roles[3];
    qa_native_profile native_profile;
    qa_net_client_id client;
    qa_net_seat_id network_seat;
    qa_net_protocol_id protocol;
    qa_product_id product;
    uint64_t epoch, configuration, revision;
    qa_application_client_source client_source;
    bool client_ready;
} resource_scope;
typedef struct resource_model {
    const qa_scene_model *root, *owner, *first, *next;
} resource_model;
typedef struct resource_frame {
    uint64_t sequence, owner;
    const qa_material_order *order;
    const void *commands, *images, *geometries, *groups;
    size_t command_count, image_count, geometry_count, group_count;
} resource_frame;
typedef struct resource_seat {
    const qa_frontend *frontend;
    uint32_t id;
    const void *input, *console, *ui, *hud, *wheel;
    qa_actor_id actor;
} resource_seat;
struct frontend_resource_inventory {
    qa_frontend *frontend;
    qa_application *application;
    const qa_launch_snapshot *launch, *candidate;
    const qa_application_client_preparation *client;
    const frontend_video_guests *video;
    qa_q3_cinematic_handles_stage *cinematics;
    qa_console *root_console;
    qa_cvars *root_cvars;
    bool engine_only;
    uint64_t configuration;
    qa_world *world;
    const qa_collision_geometry *geometry;
    const qa_scene_world *scene_world;
    const qa_resource *map_resource;
    const qa_scene_frame *frame;
    const frontend_seat *seats;
    unsigned seat_count;
    resource_seat *seat_roots;
    resource_frame frame_roots;
    resource_row *rows;
    size_t count, capacity;
    resource_scope *scopes;
    size_t scope_count;
    resource_row *observations;
    size_t observation_count, observation_capacity;
    size_t root_observation_count, child_observation_count;
    resource_model *models;
    size_t model_count;
    frontend_remote_q3_initial_view initial;
    frontend_remote_q3_modules *initial_modules;
    bool initial_observed, initial_present;
    const frontend_resource_inventory *checking;
    size_t observation_cursor, scope_cursor;
    bool sealed, metadata_only;
};
static bool same_scope(const resource_scope *,const resource_scope *);
static void resource_metadata_free(frontend_resource_inventory *inventory)
{
    free(inventory->seat_roots); free(inventory->models); free(inventory->observations);
    free(inventory->scopes); free(inventory->rows);
}

static bool resources_idle(const qa_scene_resources *images)
{ return !images || qa_scene_resources_idle(images); }
static bool library_idle(const qa_material_library *library)
{ return !library || qa_material_library_idle(library); }
static bool fonts_idle(const qa_font_library *fonts)
{ return !fonts || qa_font_library_idle(fonts); }
static bool movies_idle(const qa_frontend *f)
{
    if(!f->application) return !f->material_movie_owners;
    size_t count=0;
    if(!frontend_material_movies_roster_count(f,&count,NULL)) return false;
    for(size_t i=0;i<count;++i) {
        frontend_material_movies *owner=NULL;
        if(!frontend_material_movies_roster_at(f,i,&owner,NULL) || !frontend_material_movies_idle(owner)) return false;
    }
    return true;
}
bool frontend_seat_callbacks_returned(const qa_frontend *f)
{
    if (!f) return false;
    if (!f->seats) return true;
    for (unsigned i=0;i<f->options.seats;++i) {
        const frontend_seat *seat=f->seats+i;
        if ((seat->ui && !qa_ui_idle(seat->ui)) || (seat->hud && !qa_hud_idle(seat->hud)) ||
            (seat->wheel && !qa_hud_wheel_round_ready(seat->wheel)) ||
            !frontend_startup_server_browser_idle(seat->server_browser) ||
            (seat->source_prompt && !frontend_source_prompt_idle(seat->source_prompt))) return false;
    }
    return true;
}
bool frontend_seat_callbacks_idle(const qa_frontend *f)
{
    if (!frontend_seat_callbacks_returned(f)) return false;
    if (f->seats) for (unsigned i=0;i<f->options.seats;++i)
        if (!qa_input_release_idle(f->seats[i].input)) return false;
    return true;
}
bool frontend_seat_callbacks_checkpoint_ready(const qa_frontend *f,qa_error *error)
{
    if(!frontend_seat_callbacks_returned(f)) return false;
    if(f->seats) for(unsigned i=0;i<f->options.seats;++i) {
        frontend_seat *seat=f->seats+i;
        qa_input_checkpoint_refs refs=frontend_seat_input_refs(seat);
        if(seat->input && !qa_input_seat_checkpoint_ready(seat->input,&refs,error)) return false;
    }
    return true;
}
static bool owners_returned(const qa_frontend *f,const frontend_video_guests *video)
{
    if(video && !frontend_video_guests_resources_returned(f,video,NULL)) return false;
    if (!f || f->capture || f->resource_inventory || f->root_resources_pending ||
        !frontend_cinematic_idle(f) || !frontend_qc_rerelease_idle(f) || !frontend_native_q2_children_idle(f) ||
        (!video && (!frontend_native_q3_idle(f) || !frontend_remote_q3_idle(f))) ||
        !frontend_remote_q2_idle(f) || !frontend_remote_unified_idle(f) ||
        !frontend_client_sources_idle(f) ||
        (!video && !frontend_component_scenes_idle(f)) ||
        !frontend_remote_q3_initial_idle_all(f) || !frontend_ui_features_idle(f) ||
        !frontend_equipment_idle(f) || !frontend_equipment_q3_idle(f) || !frontend_equipment_gear_idle(f) ||
        !frontend_equipment_events_idle(f->gear_events) ||
        (f->global_settings_storage && !frontend_global_settings_storage_idle(f->global_settings_storage)) ||
        (f->music_sources && !frontend_music_sources_idle(f->music_sources)) ||
        (f->q1_sky && !frontend_q1_sky_idle(f->q1_sky)) || !movies_idle(f) ||
        (f->source_cinematics && !qa_q3_cinematic_handles_idle(f->source_cinematics)) ||
        !frontend_renderer_materials_idle(f->renderer_materials) || !frontend_renderer_worlds_idle(f->renderer_worlds) ||
        !frontend_renderer_registries_idle(f->renderer_registries) ||
        !frontend_qc_messages_idle(f->qc_messages) ||
        !frontend_selected_character_idle(f) || !frontend_selected_effects_idle(f) || !frontend_sources_idle(f) ||
        !resources_idle(f->images) || !resources_idle(f->ui_images) || !library_idle(f->materials) ||
        !fonts_idle(f->fonts) || (f->order && !qa_material_order_idle(f->order)) ||
        (f->scene_world && !qa_scene_world_idle(f->scene_world)) || !frontend_visuals_idle(f) ||
        (f->audio && !qa_audio_engine_round_ready(f->audio,NULL)) ||
        (f->device && !qa_audio_device_round_ready(f->device,NULL))) return false;
    for(size_t i=0;i<frontend_remote_q1_count(f);++i)
        if(!frontend_remote_q1_idle(frontend_remote_q1_at(f,i))) return false;
    for (size_t i=0;;++i) {
        const qa_scene_resources *images=frontend_event_images_at((qa_frontend *)f,i);
        if (!images) break;
        if (!qa_scene_resources_idle(images)) return false;
    }
    return true;
}
bool frontend_owners_returned(const qa_frontend *f)
{ return owners_returned(f,NULL); }
bool frontend_owners_idle(const qa_frontend *f)
{
    return frontend_owners_returned(f) && (!f->input || qa_input_platform_settings_idle(f->input));
}
static bool add(frontend_capture *capture, capture_kind kind, const void *owner, qa_error *error)
{
    if (!owner) return true;
    for (size_t i=0;i<capture->count;++i)
        if (capture->rows[i].kind==kind && capture->rows[i].owner==owner) return true;
    if (capture->count==capture->capacity) {
        size_t capacity=capture->capacity?capture->capacity*2:16;
        if (capacity<capture->capacity || capacity>SIZE_MAX/sizeof(*capture->rows))
            return frontend_fail(error,QA_ERROR_MEMORY,"Frontend capture owner inventory exceeds address space");
        capture_row *rows=realloc(capture->rows,capacity*sizeof(*rows));
        if (!rows) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual frontend capture owners");
        capture->rows=rows; capture->capacity=capacity;
    }
    capture->rows[capture->count++]=(capture_row){.kind=kind,.owner=owner}; return true;
}
typedef bool (*owner_append)(void *, capture_kind, const void *, qa_error *);
typedef const qa_q3_presentation_assets *(*assets_read)(const void *, size_t);
static bool capture_add(void *context, capture_kind kind, const void *owner, qa_error *error)
{ return add(context,kind,owner,error); }
static const qa_q3_presentation_assets *capture_assets(const void *context,size_t ordinal)
{
    const frontend_capture *capture=context;
    for (size_t i=0;i<capture->count;++i)
        if (capture->rows[i].kind==CAPTURE_ASSETS && !ordinal--) return capture->rows[i].owner;
    return NULL;
}
static bool resource_add(void *context,capture_kind kind,const void *owner,qa_error *error)
{
    frontend_resource_inventory *inventory=context;
    if (!owner) return true;
    if (inventory->checking) {
        const frontend_resource_inventory *saved=inventory->checking;
        if (inventory->observation_cursor>=saved->observation_count) return false;
        const resource_row *row=saved->observations+inventory->observation_cursor++;
        return row->kind==kind && row->owner==owner;
    }
    if (inventory->observation_count==inventory->observation_capacity) {
        size_t capacity=inventory->observation_capacity?inventory->observation_capacity*2:32;
        if (capacity<inventory->observation_capacity || capacity>SIZE_MAX/sizeof(*inventory->observations))
            return frontend_fail(error,QA_ERROR_MEMORY,"Resource metadata observations exceed address space");
        resource_row *rows=realloc(inventory->observations,capacity*sizeof(*rows));
        if (!rows) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual resource observation order");
        inventory->observations=rows; inventory->observation_capacity=capacity;
    }
    inventory->observations[inventory->observation_count++]=(resource_row){kind,owner};
    for (size_t i=0;i<inventory->count;++i)
        if (inventory->rows[i].kind==kind && inventory->rows[i].owner==owner) return true;
    if (inventory->count==inventory->capacity) {
        size_t capacity=inventory->capacity?inventory->capacity*2:16;
        if (capacity<inventory->capacity || capacity>SIZE_MAX/sizeof(*inventory->rows))
            return frontend_fail(error,QA_ERROR_MEMORY,"Resource metadata inventory exceeds address space");
        resource_row *rows=realloc(inventory->rows,capacity*sizeof(*rows));
        if (!rows) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual resource owner metadata");
        inventory->rows=rows; inventory->capacity=capacity;
    }
    inventory->rows[inventory->count++]=(resource_row){kind,owner}; return true;
}
static const void *resource_at(const frontend_resource_inventory *inventory,capture_kind kind,size_t ordinal)
{
    if (!inventory) return NULL;
    for (size_t i=0;i<inventory->count;++i)
        if (inventory->rows[i].kind==kind && !ordinal--) return inventory->rows[i].owner;
    return NULL;
}
static const qa_q3_presentation_assets *resource_assets(const void *context,size_t ordinal)
{ return resource_at(context,CAPTURE_ASSETS,ordinal); }
static bool scope_add(frontend_resource_inventory *inventory,resource_scope scope,qa_error *error)
{
    if (inventory->checking) {
        const frontend_resource_inventory *saved=inventory->checking;
        return inventory->scope_cursor<saved->scope_count &&
            same_scope(saved->scopes+inventory->scope_cursor++,&scope);
    }
    if (inventory->scope_count==SIZE_MAX/sizeof(*inventory->scopes))
        return frontend_fail(error,QA_ERROR_MEMORY,"Resource namespace roster exceeds address space");
    resource_scope *scopes=realloc(inventory->scopes,(inventory->scope_count+1)*sizeof(*scopes));
    if (!scopes) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual resource namespace witnesses");
    inventory->scopes=scopes; scopes[inventory->scope_count++]=scope; return true;
}
static bool resource_initial(frontend_resource_inventory *inventory,
    frontend_remote_q3_initial_view *out,bool *present,qa_error *error)
{
    frontend_network_client_attempt attempt;
    frontend_remote_q3_initial *parent=NULL;
    frontend_remote_q3_modules *modules=NULL;
    frontend_remote_q3_initial_view view={0}; bool found=false;
    if (!frontend_network_initial_metadata_read(inventory->frontend,&attempt,&parent,&modules,&found,error) ||
        (found && (!parent || !frontend_remote_q3_initial_metadata_read(parent,&view,error)))) return false;
    size_t count=frontend_remote_q3_initial_count(inventory->frontend);
    if (count!=(found?1u:0u) || (found && frontend_remote_q3_initial_at(inventory->frontend,0)!=parent))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial metadata retains a separate partial resource parent");
    const frontend_resource_inventory *saved=inventory->checking?inventory->checking:inventory;
    if (saved->initial_observed) {
        if (found!=saved->initial_present || (found && (parent!=saved->initial.owner ||
            modules!=saved->initial_modules || !frontend_remote_q3_initial_metadata_current(&saved->initial)))) return false;
    } else {
        inventory->initial=view; inventory->initial_modules=modules;
        inventory->initial_present=found; inventory->initial_observed=true;
    }
    *out=view; *present=found; return true;
}
static bool resource_scopes(frontend_resource_inventory *inventory,qa_error *error)
{
    qa_frontend *f=inventory->frontend;
    size_t unified_count=0;
    if(!frontend_unified_media_inventory_count(f,&unified_count,error)) return false;
    for(size_t i=0;i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(f,i,&media,error) || !scope_add(inventory,
            (resource_scope){.kind=RESOURCE_UNIFIED,.ordinal=i,.owner=media,
                .descriptor=media?frontend_unified_media_recipe(media):NULL},error)) return false;
    }
    for(size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view view;
        if(!frontend_component_scene_metadata_read(f,i,&view,error) ||
            !frontend_component_scene_metadata_current(f,&view,error) ||
            !scope_add(inventory,(resource_scope){.kind=RESOURCE_COMPONENT,.ordinal=i,.owner=view.assets,
                .descriptor=view.descriptor?(const void *)view.descriptor:(const void *)view.recipe_provider,
                .receiver=view.receiver,.service_owner=view.service_owner,.identity=view.identity,
                .seat=view.physical_seat,.actor=view.viewer,.mounts=view.files,
                .epoch=view.generation,.revision=view.sequence},error)) return false;
    }
    for(size_t i=0;i<frontend_client_source_count(f);++i) {
        frontend_client_source_view view;
        if(!frontend_client_source_metadata_read(frontend_client_source_at(f,i),&view,error) ||
            !scope_add(inventory,(resource_scope){.kind=RESOURCE_CLIENT,.ordinal=i,.owner=view.owner,
                .descriptor=view.source.descriptor,.receiver=view.source.context.receiver,
                .seat=view.source.context.physical_seat,.launch_seat=view.source.context.seat,
                .source_files=view.source.descriptor->content,.client_source=view.source,.client_ready=view.ready},error)) return false;
    }
    for (size_t i=0;i<frontend_source_group_count(f);++i) {
        frontend_source_group_view v;
        if (!(inventory->video?frontend_source_group_video_read(f,i,&v,inventory->video):
            frontend_source_group_read(f,i,&v)) || !scope_add(inventory,(resource_scope){
            .kind=RESOURCE_SOURCE,.ordinal=i,.owner=v.presentation,.provider=v.owner,.identity=v.identity,
            .seat=v.seat,.launch_seat=v.launch_seat,.source_files=v.source_files,.mounts=v.mounts,
            .roles={v.roles[0],v.roles[1],v.roles[2]}},error)) return false;
        size_t roles=(size_t)v.roles[QA_QVM_CGAME]+v.roles[QA_QVM_UI];
        for (size_t j=0;j<roles;++j) {
            frontend_source_role_identity role;
            if (!(inventory->video?frontend_source_group_role_video_read(f,i,j,&role,inventory->video):
                frontend_source_group_role_read(f,i,j,&role)) || !scope_add(inventory,(resource_scope){
                .kind=RESOURCE_SOURCE_ROLE,.ordinal=j,.owner=v.presentation,.provider=v.owner,
                .service_owner=role.service_owner,.identity=v.identity,.seat=v.seat,.launch_seat=v.launch_seat,
                .source_files=v.source_files,.mounts=v.mounts,.roles={role.role,0,0}},error)) return false;
        }
    }
    for (size_t i=0;i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view v;
        if (!(inventory->video?frontend_native_q3_video_read(f,i,&v,error):frontend_native_q3_read(f,i,&v,error)) || !scope_add(inventory,(resource_scope){
            .kind=RESOURCE_NATIVE_Q3,.ordinal=i,.owner=frontend_native_q3_at(f,i),.descriptor=v.source_launch,
            .provider=v.source_owner,.receiver=v.receiver,.service_owner=v.service_owner,.identity=v.identity,
            .seat=v.seat,.launch_seat=v.launch_seat,.actor=v.actor,.source_files=v.source_files,.mounts=v.mounts},error)) return false;
    }
    for (size_t i=0;i<frontend_native_q2_owner_count(f);++i) {
        frontend_native_q2_owner_view v;
        if (!frontend_native_q2_owner_read(f,i,&v) || v.prepared || !scope_add(inventory,(resource_scope){
            .kind=RESOURCE_NATIVE_Q2,.ordinal=i,.provider=v.owner,.identity=v.identity,
            .source_files=v.provider_files,.mounts=v.mounts,.native_profile=v.profile},error)) return false;
    }
    for (size_t i=0;i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3_resources v;
        bool current=inventory->metadata_only ?
            frontend_remote_q3_resources_metadata_read(frontend_remote_q3_at(f,i),&v,error) &&
                frontend_remote_q3_resources_metadata_current(&v) :
            frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&v,error) &&
                frontend_remote_q3_resources_current(&v);
        if (!current || !scope_add(inventory,(resource_scope){
                .kind=RESOURCE_REMOTE,.ordinal=i,.owner=v.owner,.descriptor=v.descriptor,
                .identity=v.identity,.seat=v.physical_seat,.mounts=v.mounts,.source_files=v.domain.content},error)) return false;
    }
    frontend_remote_q3_initial_view initial; bool has_initial=false;
    if (!resource_initial(inventory,&initial,&has_initial,error)) return false;
    if (has_initial && !scope_add(inventory,(resource_scope){
        .kind=RESOURCE_INITIAL,.owner=initial.owner,.descriptor=initial.descriptor,
        .receiver=initial.attempt.source.receiver.receiver,.service_owner=initial.attempt.source.receiver.service_owner,
        .identity=initial.identity,.seat=initial.physical_seat,.launch_seat=initial.attempt.source.receiver.seat,
        .mounts=initial.mounts,.source_files=initial.descriptor->content},error)) return false;
    for(size_t i=0;i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1_view v;
        if(!frontend_remote_q1_metadata_read(frontend_remote_q1_at(f,i),&v,error) ||
            v.domain.application!=f->application || !scope_add(inventory,(resource_scope){
                .kind=RESOURCE_REMOTE_Q1,.ordinal=i,.owner=v.owner,.descriptor=v.content.catalog,
                .mounts=v.content.mounts,.provider=v.domain.actor_owner,.seat=v.domain.physical_seat,
                .client=v.domain.client,.network_seat=v.domain.seat,.protocol=v.domain.protocol,
                .product=v.content.product,.epoch=v.domain.epoch,.configuration=v.domain.configuration_generation,
                .revision=v.revision,.identity=v.map_generation},error)) return false;
    }
    for(size_t i=0;i<frontend_remote_q2_count(f);++i) {
        frontend_remote_q2_view v;
        if(!frontend_remote_q2_metadata_read(frontend_remote_q2_at(f,i),&v,error) ||
            v.domain.application!=f->application || !scope_add(inventory,(resource_scope){
                .kind=RESOURCE_REMOTE_Q2,.ordinal=i,.owner=v.owner,.descriptor=v.content.catalog,
                .mounts=v.content.mounts,.seat=v.domain.physical_seat,.client=v.domain.client,
                .network_seat=v.domain.seat,.protocol=v.domain.protocol,.product=v.content.selected,
                .epoch=v.domain.epoch,.configuration=v.domain.configuration_generation,
                .revision=v.content_generation,.identity=v.identity},error)) return false;
    }
    for (size_t i=0;i<frontend_visual_owner_count(f);++i) {
        frontend_visual_owner_view v;
        if (!frontend_visual_owner_read(f,i,&v) || !scope_add(inventory,(resource_scope){
            .kind=RESOURCE_VISUAL,.ordinal=i,.owner=v.images,.provider=v.owner,.mounts=v.mounts},error)) return false;
    }
    for (size_t i=0;i<frontend_equipment_q3_count(f);++i) {
        frontend_equipment_q3_owner_view v;
        if (!frontend_equipment_q3_at(f,i,&v,error) || !scope_add(inventory,(resource_scope){
            .kind=RESOURCE_SELECTED,.ordinal=i,.owner=v.assets,.provider=v.provider,.mounts=v.content.mounts},error)) return false;
    }
    for (size_t i=0;i<frontend_selected_character_count(f);++i) {
        frontend_selected_character_view v;
        if (!frontend_selected_character_at(f,i,&v,error) || !scope_add(inventory,(resource_scope){
            .kind=RESOURCE_CHARACTER,.ordinal=i,.owner=v.assets,.descriptor=v.appearance_launch,
            .launch_seat=v.launch_seat,.mounts=v.content.mounts},error)) return false;
    }
    for (size_t i=0;i<frontend_selected_effects_count(f);++i) {
        frontend_selected_effects_view v;
        if (!frontend_selected_effects_at(f,i,&v,error) || !scope_add(inventory,(resource_scope){
            .kind=RESOURCE_EFFECTS,.ordinal=i,.owner=v.assets,.provider=v.provider,.identity=v.identity,
            .seat=v.physical_seat,.mounts=v.content.mounts,.source_files=v.source_files},error)) return false;
    }
    for (size_t i=0;i<frontend_equipment_gear_count(f);++i) {
        frontend_equipment_gear_owner_view v;
        if (!frontend_equipment_gear_at(f,i,&v,error) || !scope_add(inventory,(resource_scope){
            .kind=RESOURCE_GEAR,.ordinal=i,.owner=v.assets,.descriptor=v.source.descriptor,
            .provider=v.source.selected_owner,.receiver=v.source.owner,.service_owner=v.source.service_owner,
            .source_files=v.source.files,.mounts=v.content.mounts},error)) return false;
    }
    return true;
}
static bool same_scope(const resource_scope *a,const resource_scope *b)
{
    if(a->kind==RESOURCE_CLIENT || b->kind==RESOURCE_CLIENT) {
        const qa_application_client_source *x=&a->client_source,*y=&b->client_source;
        const qa_command_context *p=&x->context.command,*q=&y->context.command;
        if(a->client_ready!=b->client_ready || x->descriptor!=y->descriptor || x->runtime!=y->runtime ||
            !qa_net_client_id_equal(x->client,y->client) || x->network_seat.owner!=y->network_seat.owner ||
            x->network_seat.index!=y->network_seat.index || x->connection_epoch!=y->connection_epoch ||
            x->configuration_generation!=y->configuration_generation || x->context.session!=y->context.session ||
            x->context.receiver!=y->context.receiver || x->context.entity_owner!=y->context.entity_owner ||
            x->context.console!=y->context.console || x->context.cvars!=y->context.cvars ||
            x->context.lifetime!=y->context.lifetime || p->session!=q->session || p->owner!=q->owner ||
            p->client!=q->client || p->seat!=q->seat || p->dialect!=q->dialect || p->origin!=q->origin ||
            p->direct!=q->direct || p->console_text!=q->console_text || p->registry!=q->registry ||
            p->generation!=q->generation || p->script!=q->script || !qa_actor_id_equal(p->actor,q->actor)) return false;
    }
    return a->kind==b->kind && a->ordinal==b->ordinal && a->owner==b->owner && a->descriptor==b->descriptor &&
        a->provider==b->provider && a->receiver==b->receiver && a->service_owner==b->service_owner &&
        a->identity==b->identity && a->seat==b->seat && a->launch_seat==b->launch_seat &&
        qa_actor_id_equal(a->actor,b->actor) && a->source_files==b->source_files && a->mounts==b->mounts &&
        a->roles[0]==b->roles[0] && a->roles[1]==b->roles[1] && a->roles[2]==b->roles[2] &&
        a->native_profile==b->native_profile && qa_net_client_id_equal(a->client,b->client) &&
        a->network_seat.owner==b->network_seat.owner && a->network_seat.index==b->network_seat.index &&
        a->protocol.kind==b->protocol.kind &&
        a->protocol.flags==b->protocol.flags && a->protocol.revision==b->protocol.revision &&
        a->product==b->product && a->epoch==b->epoch && a->configuration==b->configuration && a->revision==b->revision;
}
static bool hold(capture_row *row, qa_error *error)
{
    bool ok=false;
    switch (row->kind) {
    case CAPTURE_ASSETS: ok=qa_q3_assets_capture_begin((qa_q3_presentation_assets *)row->owner,error); break;
    case CAPTURE_IMAGES: ok=qa_scene_resources_capture_begin(row->owner,&row->token.images,error); break;
    case CAPTURE_LIBRARY: ok=qa_material_library_capture_begin(row->owner,error); break;
    case CAPTURE_FONTS: ok=qa_font_library_capture_begin(row->owner,&row->token.fonts,error); break;
    case CAPTURE_ORDER: ok=qa_material_order_capture_begin(row->owner,error); break;
    case CAPTURE_WORLD: ok=qa_scene_world_capture_begin(row->owner,&row->token.world,error); break;
    case CAPTURE_MODEL: ok=qa_scene_model_capture_begin(row->owner,&row->token.model,error); break;
    }
    row->held=ok; return ok;
}
static bool heaps(qa_frontend *f, bool video, owner_append append, void *context, qa_error *error)
{
    if (!append(context,CAPTURE_IMAGES,f->ui_images,error) || !append(context,CAPTURE_IMAGES,f->images,error) ||
        !append(context,CAPTURE_LIBRARY,f->materials,error) || !append(context,CAPTURE_FONTS,f->fonts,error) ||
        !append(context,CAPTURE_ORDER,f->order,error)) return false;
    for (size_t i=0;i<frontend_source_group_count(f);++i) {
        frontend_source_group_view group;
        if (!(video?frontend_source_group_video_read(f,i,&group,f->video_guests):frontend_source_group_read(f,i,&group)))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture source is not fully constructed");
        if (!append(context,CAPTURE_IMAGES,group.images,error) || !append(context,CAPTURE_LIBRARY,group.materials,error) ||
            !append(context,CAPTURE_FONTS,group.fonts,error) || !append(context,CAPTURE_WORLD,group.world,error)) return false;
    }
    for (size_t i=0;;++i) {
        const qa_scene_resources *images=frontend_event_images_at(f,i);
        if (!images) break;
        if (!append(context,CAPTURE_IMAGES,images,error)) return false;
    }
    for (size_t i=0;i<frontend_visual_owner_count(f);++i) {
        frontend_visual_owner_view owner;
        if (!frontend_visual_owner_read(f,i,&owner))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture appearance owner is incomplete");
        if (!append(context,CAPTURE_IMAGES,owner.images,error) || !append(context,CAPTURE_LIBRARY,owner.materials,error)) return false;
        for (size_t j=0;j<frontend_visual_model_count(f,i);++j) {
            frontend_visual_model_view model;
            if (!frontend_visual_model_read(f,i,j,&model) || !append(context,CAPTURE_MODEL,model.scene,error))
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture appearance model is incomplete");
        }
    }
    for (size_t i=0;i<frontend_native_q2_owner_count(f);++i) {
        frontend_native_q2_owner_view owner;
        if (!frontend_native_q2_owner_read(f,i,&owner) || owner.prepared)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture native source is incomplete");
        if (!append(context,CAPTURE_IMAGES,owner.images,error) || !append(context,CAPTURE_FONTS,owner.fonts,error)) return false;
    }
    for (size_t i=0;i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view owner;
        if (!(video?frontend_native_q3_video_read(f,i,&owner,error):frontend_native_q3_read(f,i,&owner,error)) ||
            (!video && (!owner.assets || !owner.images || !owner.materials || !owner.fonts || !owner.core)))
            return error && error->code!=QA_OK?false:
                frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture native Q3 owner is incomplete");
        if (!append(context,CAPTURE_IMAGES,owner.images,error) || !append(context,CAPTURE_LIBRARY,owner.materials,error) ||
            !append(context,CAPTURE_FONTS,owner.fonts,error)) return false;
    }
    for(size_t i=0;i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1 *row=frontend_remote_q1_at(f,i); frontend_remote_q1_media media;
        if(!frontend_remote_q1_media_read(row,&media,error) ||
            !append(context,CAPTURE_IMAGES,media.images,error) ||
            !append(context,CAPTURE_LIBRARY,media.materials,error) ||
            !append(context,CAPTURE_WORLD,media.world,error)) return false;
        for(size_t j=0;j<frontend_remote_q1_model_count(row);++j) {
            frontend_remote_q1_model_view model;
            if(!frontend_remote_q1_model_at(row,j,&model,error) ||
                !append(context,CAPTURE_MODEL,model.scene,error) ||
                !append(context,CAPTURE_WORLD,model.world,error)) return false;
        }
    }
    for(size_t i=0;i<frontend_remote_q2_count(f);++i) {
        frontend_remote_q2 *row=frontend_remote_q2_at(f,i); frontend_remote_q2_view media;
        if(!frontend_remote_q2_metadata_read(row,&media,error) ||
            !append(context,CAPTURE_IMAGES,media.images,error) ||
            !append(context,CAPTURE_LIBRARY,media.materials,error) ||
            !append(context,CAPTURE_FONTS,frontend_remote_q2_fonts(row),error) ||
            !append(context,CAPTURE_WORLD,media.world,error)) return false;
        for(size_t j=0;j<frontend_remote_q2_model_count(row);++j) {
            frontend_remote_q2_model_view model;
            if(!frontend_remote_q2_model_at(row,j,&model,error) ||
                !append(context,CAPTURE_MODEL,model.scene,error)) return false;
        }
    }
    for (size_t i=0;i<frontend_equipment_media_count(f);++i) {
        frontend_equipment_media_view media;
        if (!frontend_equipment_media_at(f,i,&media) || !media.declaration || !media.held ||
            (!media.declaration->none && (!media.held_scene || !media.held->model)))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture equipment media is incomplete");
        if(media.source_slot && (!append(context,CAPTURE_IMAGES,media.owner.images,error) ||
            !append(context,CAPTURE_LIBRARY,media.owner.materials,error))) return false;
        if (media.declaration->none) continue;
        if (!append(context,CAPTURE_MODEL,media.held_scene,error)) return false;
    }
    for(size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        if(!frontend_component_scene_metadata_read(f,i,&row,error) ||
            !append(context,CAPTURE_IMAGES,row.images,error) || !append(context,CAPTURE_LIBRARY,row.materials,error) ||
            !append(context,CAPTURE_FONTS,row.fonts,error)) return false;
    }
    return append(context,CAPTURE_WORLD,f->scene_world,error);
}
static bool registry_children(owner_append append, void *context, assets_read read, qa_error *error)
{
    for (size_t i=0;;++i) {
        const qa_q3_presentation_assets *assets=read(context,i);
        if (!assets) break;
        qa_q3_presentation_assets *parent=qa_q3_assets_parent(assets);
        if(parent==assets || !append(context,CAPTURE_ASSETS,parent,error)) return false;
        for(size_t j=0;j<qa_q3_assets_provider_count(assets);++j) {
            qa_q3_presentation_provider provider;
            if(!qa_q3_assets_provider_at(assets,j,&provider) ||
                !append(context,CAPTURE_IMAGES,provider.images,error) ||
                !append(context,CAPTURE_LIBRARY,provider.materials,error)) return false;
        }
        for(size_t j=0;j<qa_q3_assets_map_count(assets);++j) {
            qa_q3_asset_map_custody map;
            if(!qa_q3_assets_map_at(assets,j,&map) || !append(context,CAPTURE_WORLD,map.world,error)) return false;
        }
        qa_q3_presentation_asset_options services; qa_scene_world *world=NULL;
        qa_collision_geometry *geometry=NULL; size_t count=0;
        if (!qa_q3_assets_services(assets,&services,&world,&geometry,error) ||
            !qa_q3_assets_model_count(assets,&count,error) || !append(context,CAPTURE_WORLD,world,error)) return false;
        for (size_t j=0;j<count;++j) {
            qa_q3_asset_model_holder model;
            if (!qa_q3_assets_model_holder(assets,j,&model,error)) return false;
            if (!model.present) continue;
            if (!append(context,CAPTURE_WORLD,model.world,error)) return false;
            if (!append(context,CAPTURE_MODEL,model.source_md4_scene,error)) return false;
            for (unsigned k=0;k<3;++k)
                if (!append(context,CAPTURE_MODEL,model.scenes[k],error)) return false;
        }
    }
    return true;
}
static bool registries(qa_frontend *f, bool video, owner_append append, void *context, qa_error *error)
{
    bool ok=true;
    for (size_t i=0;ok && i<frontend_source_group_count(f);++i) {
        frontend_source_group_view group;
        ok=(video?frontend_source_group_video_read(f,i,&group,f->video_guests):frontend_source_group_read(f,i,&group)) &&
            append(context,CAPTURE_ASSETS,group.assets,error);
    }
    for (size_t i=0;ok && i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view owner;
        ok=(video?frontend_native_q3_video_read(f,i,&owner,error):frontend_native_q3_read(f,i,&owner,error)) && (video || owner.assets) &&
            append(context,CAPTURE_ASSETS,owner.assets,error);
    }
    for (size_t i=0;ok && i<frontend_equipment_q3_count(f);++i) {
        frontend_equipment_q3_owner_view owner;
        ok=frontend_equipment_q3_at(f,i,&owner,error) && owner.assets &&
            append(context,CAPTURE_ASSETS,owner.assets,error);
    }
    for (size_t i=0;ok && i<frontend_selected_character_count(f);++i) {
        frontend_selected_character_view owner;
        ok=frontend_selected_character_at(f,i,&owner,error) && owner.assets &&
            append(context,CAPTURE_ASSETS,owner.assets,error);
    }
    for (size_t i=0;ok && i<frontend_selected_effects_count(f);++i) {
        frontend_selected_effects_view owner;
        ok=frontend_selected_effects_at(f,i,&owner,error) && owner.assets &&
            append(context,CAPTURE_ASSETS,owner.assets,error);
    }
    for (size_t i=0;ok && i<frontend_equipment_gear_count(f);++i) {
        frontend_equipment_gear_owner_view owner;
        ok=frontend_equipment_gear_at(f,i,&owner,error) && owner.assets &&
            append(context,CAPTURE_ASSETS,owner.assets,error);
    }
    for(size_t i=0;ok && i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        ok=frontend_component_scene_metadata_read(f,i,&row,error) && append(context,CAPTURE_ASSETS,row.assets,error);
    }
    if(ok && f->cpu && f->gl)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture has two physical Source renderers");
    const qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):
        f->gl?qa_gl_render_controls(f->gl):NULL;
    if(ok && controls) {
        const qa_material_source_scratch *source=qa_render_controls_source_metadata(controls,error);
        const qa_q3_source_scene_bank *bank=NULL;
        if(!source || !qa_material_source_scene_bank_metadata(source,&bank,error)) return false;
        size_t image_count=0;
        if(!qa_render_controls_source_images_metadata(controls,&image_count,error)) return false;
        for(size_t i=0;ok && i<image_count;++i) {
            const qa_scene_image *image=NULL;
            if(!qa_render_controls_source_image_metadata(controls,i,&image,error) || !image) return false;
            qa_scene_resources *images=qa_scene_image_resource_owner(image);
            ok=images && append(context,CAPTURE_IMAGES,images,error);
        }
        size_t texture_count=0;
        if(!qa_render_controls_source_texture_metadata(controls,&texture_count,error)) return false;
        for(size_t i=0;ok && i<texture_count;++i) {
            const qa_scene_image *image=NULL;
            if(!qa_render_controls_source_texture_level_metadata(controls,i,&image,error) || !image) return false;
            qa_scene_resources *images=qa_scene_image_resource_owner(image);
            ok=images && append(context,CAPTURE_IMAGES,images,error);
        }
        for(size_t i=0;ok && i<qa_material_source_library_count(source);++i) {
            const qa_material_library *library=qa_material_source_library_at(source,i);
            ok=library && append(context,CAPTURE_LIBRARY,library,error) &&
                append(context,CAPTURE_IMAGES,qa_material_library_resource_owner(library),error);
        }
        for(size_t i=0;ok && i<qa_material_source_world_count(source);++i) {
            const qa_scene_world *world=qa_material_source_world_at(source,i);
            ok=world && append(context,CAPTURE_WORLD,world,error) &&
                append(context,CAPTURE_IMAGES,qa_scene_world_resource_owner(world),error) &&
                append(context,CAPTURE_LIBRARY,qa_scene_world_material_owner(world),error);
        }
        for(size_t i=0;ok && i<qa_q3_source_scene_bank_registry_count(bank);++i) {
            qa_q3_presentation_assets *assets=qa_q3_source_scene_bank_registry_at(bank,i);
            ok=assets && append(context,CAPTURE_ASSETS,assets,error);
        }
    }
    if(ok && f->source_cinematics) {
        qa_q3_cinematic_handles_options pool;
        ok=frontend_source_cinematics_read(f,&pool,error) && append(context,CAPTURE_IMAGES,pool.images,error);
    }
    return ok;
}
static bool remote_capture_roots(qa_frontend *f,frontend_capture *capture,qa_error *error)
{
    capture->remote_count=frontend_remote_q3_count(f);
    if(capture->remote_count>SIZE_MAX/sizeof(*capture->remote))
        return frontend_fail(error,QA_ERROR_MEMORY,"Remote capture roster exceeds address space");
    capture->remote=capture->remote_count?calloc(capture->remote_count,sizeof(*capture->remote)):NULL;
    if(capture->remote_count && !capture->remote)
        return frontend_fail(error,QA_ERROR_MEMORY,"Retaining remote capture resource parents");
    for(size_t i=0;i<capture->remote_count;++i) {
        frontend_remote_q3_resources *owner=capture->remote+i;
        if(!frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),owner,error) ||
            !frontend_remote_q3_resources_current(owner) || !owner->assets ||
            !add(capture,CAPTURE_ASSETS,owner->assets,error)) return false;
    }
    frontend_network_initial_graph_view initial;
    if(!frontend_network_initial_graph_read(f,&initial,error)) return false;
    size_t initial_count=frontend_remote_q3_initial_count(f);
    for(size_t i=0;i<initial_count;++i) {
        frontend_remote_q3_initial *row=frontend_remote_q3_initial_at(f,i);
        frontend_remote_q3_initial_view owner;
        if(!frontend_remote_q3_initial_read(row,&owner,error) || !frontend_remote_q3_initial_current(&owner) ||
            !initial.present || initial.parent!=row)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial capture retains an uncompleted or uninstalled resource parent");
    }
    if(initial_count!=(initial.present?1u:0u))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Initial capture changed its actual parent custody roster");
    if(initial.present) {
        frontend_remote_q3_initial_view *owner=&capture->initial;
        if(!initial.parent || !frontend_remote_q3_initial_idle(initial.parent) ||
            !frontend_remote_q3_modules_idle(initial.modules) ||
            !frontend_remote_q3_initial_read(initial.parent,owner,error) ||
            !frontend_remote_q3_initial_current(owner) || !owner->assets ||
            !add(capture,CAPTURE_ASSETS,owner->assets,error)) return false;
        capture->has_initial=true;
    }
    return frontend_network_initial_graph_current(f,&initial);
}
static bool unified_heaps(qa_frontend *f,owner_append append,void *context,qa_error *error)
{
    size_t count=0;
    if(!frontend_unified_media_inventory_count(f,&count,error)) return false;
    for(size_t i=0;i<count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(f,i,&media,error)) return false;
        if(!media) continue;
        for(size_t j=0;j<frontend_unified_media_bank_count(media);++j) {
            frontend_unified_bank_view bank;
            if(!frontend_unified_media_bank_read(media,j,&bank) ||
                !append(context,CAPTURE_ASSETS,bank.q3_assets,error) ||
                !append(context,CAPTURE_IMAGES,bank.images,error) ||
                !append(context,CAPTURE_LIBRARY,bank.materials,error) ||
                !append(context,CAPTURE_FONTS,bank.fonts,error)) return false;
        }
        if(!append(context,CAPTURE_WORLD,frontend_unified_media_world(media),error)) return false;
        for(size_t j=0;j<frontend_unified_media_model_count(media);++j) {
            frontend_unified_model_view model;
            if(!frontend_unified_media_model_read(media,j,&model) ||
                !append(context,CAPTURE_MODEL,model.scene,error) ||
                !append(context,CAPTURE_WORLD,model.world,error)) return false;
        }
    }
    return true;
}
static bool remote_capture_heaps(frontend_capture *capture,qa_error *error)
{
    for(size_t i=0;i<capture->remote_count;++i) {
        const frontend_remote_q3_resources *owner=capture->remote+i;
        if(!add(capture,CAPTURE_IMAGES,owner->images,error) ||
            !add(capture,CAPTURE_LIBRARY,owner->materials,error) ||
            !add(capture,CAPTURE_FONTS,owner->fonts,error) ||
            !add(capture,CAPTURE_WORLD,owner->world,error)) return false;
    }
    const frontend_remote_q3_initial_view *owner=&capture->initial;
    if(capture->has_initial && (!add(capture,CAPTURE_IMAGES,owner->images,error) ||
        !add(capture,CAPTURE_LIBRARY,owner->materials,error) || !add(capture,CAPTURE_FONTS,owner->fonts,error))) return false;
    size_t retained_count=0;
    if(!frontend_renderer_materials_count(capture->frontend,&retained_count,error)) return false;
    for(size_t i=0;i<retained_count;++i) {
        frontend_renderer_materials_view retained;
        if(!frontend_renderer_materials_read_at(capture->frontend,i,&retained,error) ||
            !add(capture,CAPTURE_IMAGES,retained.images,error) || !add(capture,CAPTURE_LIBRARY,retained.library,error) ||
            !add(capture,CAPTURE_IMAGES,retained.lightmap_images,error)) return false;
    }
    frontend_renderer_worlds_view world; bool has_world=false;
    return frontend_renderer_worlds_read(capture->frontend,&world,&has_world,error) && (!has_world ||
        (add(capture,CAPTURE_WORLD,world.world,error) && add(capture,CAPTURE_IMAGES,world.images,error) &&
         add(capture,CAPTURE_LIBRARY,world.materials,error))) &&
        unified_heaps(capture->frontend,capture_add,capture,error);
}
static bool model_banks(frontend_resource_inventory *inventory,const qa_scene_model *root,qa_error *error)
{
    if (inventory->checking) {
        const frontend_resource_inventory *saved=inventory->checking;
        bool found=false;
        for (size_t i=0;i<saved->model_count;++i) {
            const resource_model *node=saved->models+i;
            if (node->root!=root) continue;
            found=true;
            if (qa_scene_model_replacement_first(node->owner)!=node->first ||
                qa_scene_model_replacement_next(node->owner)!=node->next ||
                !resource_add(inventory,CAPTURE_IMAGES,qa_scene_model_resource_owner(node->owner),error) ||
                !resource_add(inventory,CAPTURE_LIBRARY,qa_scene_model_material_owner(node->owner),error)) return false;
        }
        return found;
    }
    const qa_scene_model **nodes=malloc(sizeof(*nodes)); size_t count=1;
    if (!nodes) return frontend_fail(error,QA_ERROR_MEMORY,"Observing actual replacement resource owners");
    nodes[0]=root; bool ok=true;
    for (size_t i=0;ok && i<count;++i) {
        if (inventory->model_count==SIZE_MAX/sizeof(*inventory->models)) {
            ok=frontend_fail(error,QA_ERROR_MEMORY,"Replacement metadata exceeds address space"); break;
        }
        resource_model *models=realloc(inventory->models,(inventory->model_count+1)*sizeof(*models));
        if (!models) { ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual replacement links"); break; }
        inventory->models=models;
        models[inventory->model_count++]=(resource_model){root,nodes[i],
            qa_scene_model_replacement_first(nodes[i]),qa_scene_model_replacement_next(nodes[i])};
        ok=resource_add(inventory,CAPTURE_IMAGES,qa_scene_model_resource_owner(nodes[i]),error) &&
            resource_add(inventory,CAPTURE_LIBRARY,qa_scene_model_material_owner(nodes[i]),error);
        for (const qa_scene_model *child=ok?qa_scene_model_replacement_first(nodes[i]):NULL;
            child && ok;child=qa_scene_model_replacement_next(child)) {
            for (size_t j=0;j<count;++j) if (nodes[j]==child) {
                ok=frontend_fail(error,QA_ERROR_FORMAT,"Resource metadata replacement tree aliases or cycles"); break;
            }
            if (!ok) break;
            if (count==SIZE_MAX/sizeof(*nodes)) {
                ok=frontend_fail(error,QA_ERROR_MEMORY,"Replacement resource roster exceeds address space"); break;
            }
            const qa_scene_model **grown=realloc(nodes,(count+1)*sizeof(*nodes));
            if (!grown) { ok=frontend_fail(error,QA_ERROR_MEMORY,"Observing replacement bank dependencies"); break; }
            nodes=grown; nodes[count++]=child;
        }
    }
    free(nodes); return ok;
}
static bool resource_collect(frontend_resource_inventory *inventory,qa_error *error)
{
    qa_frontend *f=inventory->frontend;
    if (!resource_scopes(inventory,error) || (inventory->checking &&
        inventory->scope_cursor!=inventory->checking->scope_count) || !registries(f,inventory->video!=NULL,resource_add,inventory,error) ||
        !heaps(f,inventory->video!=NULL,resource_add,inventory,error)) return false;
    /* Metadata replay uses its own retained owner observations. Save capture
     * separately holds the actual ordinary remote and InitialUI resource cut. */
    for (size_t i=0;i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3_resources owner;
        bool current=inventory->metadata_only ?
            frontend_remote_q3_resources_metadata_read(frontend_remote_q3_at(f,i),&owner,error) &&
                frontend_remote_q3_resources_metadata_current(&owner) :
            frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&owner,error) &&
                frontend_remote_q3_resources_current(&owner);
        if (!current ||
            !resource_add(inventory,CAPTURE_ASSETS,owner.assets,error) ||
            !resource_add(inventory,CAPTURE_IMAGES,owner.images,error) ||
            !resource_add(inventory,CAPTURE_LIBRARY,owner.materials,error) ||
            !resource_add(inventory,CAPTURE_FONTS,owner.fonts,error) ||
            !resource_add(inventory,CAPTURE_WORLD,owner.world,error)) return false;
    }
    frontend_remote_q3_initial_view initial; bool has_initial=false;
    if (!resource_initial(inventory,&initial,&has_initial,error)) return false;
    if (has_initial && (!resource_add(inventory,CAPTURE_ASSETS,initial.assets,error) ||
        !resource_add(inventory,CAPTURE_IMAGES,initial.images,error) ||
        !resource_add(inventory,CAPTURE_LIBRARY,initial.materials,error) ||
        !resource_add(inventory,CAPTURE_FONTS,initial.fonts,error))) return false;
    size_t retained_count=0;
    if(!frontend_renderer_materials_count(f,&retained_count,error)) return false;
    for(size_t i=0;i<retained_count;++i) {
        frontend_renderer_materials_view retained;
        if(!frontend_renderer_materials_read_at(f,i,&retained,error) ||
            !resource_add(inventory,CAPTURE_IMAGES,retained.images,error) ||
            !resource_add(inventory,CAPTURE_LIBRARY,retained.library,error) ||
            !resource_add(inventory,CAPTURE_IMAGES,retained.lightmap_images,error)) return false;
    }
    frontend_renderer_worlds_view retained_world; bool has_world=false;
    if(!frontend_renderer_worlds_read(f,&retained_world,&has_world,error) || (has_world &&
        (!resource_add(inventory,CAPTURE_WORLD,retained_world.world,error) ||
         !resource_add(inventory,CAPTURE_IMAGES,retained_world.images,error) ||
         !resource_add(inventory,CAPTURE_LIBRARY,retained_world.materials,error)))) return false;
    if(!unified_heaps(f,resource_add,inventory,error)) return false;
    for(size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view view;
        if(!frontend_component_scene_metadata_read(f,i,&view,error) ||
            !frontend_component_scene_metadata_current(f,&view,error) ||
            !resource_add(inventory,CAPTURE_ASSETS,view.assets,error) ||
            !resource_add(inventory,CAPTURE_IMAGES,view.images,error) ||
            !resource_add(inventory,CAPTURE_LIBRARY,view.materials,error) ||
            !resource_add(inventory,CAPTURE_FONTS,view.fonts,error)) return false;
    }
    if (inventory->checking) {
        if (inventory->observation_cursor!=inventory->checking->root_observation_count) return false;
    } else inventory->root_observation_count=inventory->observation_count;
    if (!registry_children(resource_add,inventory,resource_assets,error)) return false;
    if (inventory->checking) {
        if (inventory->observation_cursor!=inventory->checking->child_observation_count) return false;
    } else inventory->child_observation_count=inventory->observation_count;
    for (size_t i=0;i<inventory->count;++i) {
        resource_row row=inventory->rows[i];
        switch (row.kind) {
        case CAPTURE_ASSETS: {
            qa_q3_presentation_assets *parent=qa_q3_assets_parent(row.owner);
            if(parent==row.owner || !resource_add(inventory,CAPTURE_ASSETS,parent,error)) return false;
            qa_q3_presentation_asset_options services; qa_scene_world *world; qa_collision_geometry *geometry;
            if (!qa_q3_assets_services(row.owner,&services,&world,&geometry,error) ||
                !resource_add(inventory,CAPTURE_IMAGES,services.provider.images,error) ||
                !resource_add(inventory,CAPTURE_LIBRARY,services.provider.materials,error)) return false;
            break;
        }
        case CAPTURE_LIBRARY:
            if (!resource_add(inventory,CAPTURE_IMAGES,qa_material_library_resource_owner(row.owner),error) ||
                !resource_add(inventory,CAPTURE_ORDER,qa_material_library_order_owner(row.owner),error)) return false;
            break;
        case CAPTURE_FONTS:
            if (!resource_add(inventory,CAPTURE_IMAGES,qa_font_library_resource_owner(row.owner),error)) return false;
            break;
        case CAPTURE_WORLD:
            if (!resource_add(inventory,CAPTURE_IMAGES,qa_scene_world_resource_owner(row.owner),error) ||
                !resource_add(inventory,CAPTURE_LIBRARY,qa_scene_world_material_owner(row.owner),error)) return false;
            break;
        case CAPTURE_MODEL:
            if (!model_banks(inventory,row.owner,error)) return false;
            break;
        case CAPTURE_IMAGES:
            for(size_t j=0;j<qa_scene_resources_parent_count(row.owner);++j) {
                qa_scene_resources *parent=NULL;
                if(!qa_scene_resources_parent_at(row.owner,j,&parent) || !parent ||
                    !resource_add(inventory,CAPTURE_IMAGES,parent,error)) return false;
            }
            break;
        case CAPTURE_ORDER: break;
        }
    }
    return true;
}
static bool resource_parent_matches(const frontend_resource_inventory *inventory,bool consuming)
{
    const qa_frontend *f=inventory?inventory->frontend:NULL;
    bool publishing=consuming && inventory && !inventory->engine_only;
    if (!f || inventory->cinematics || f->resource_inventory!=inventory || f->application!=inventory->application ||
        (f->stepping && !inventory->video) || f->round || f->shutdown || f->capture || f->source_restoring ||
        f->scene_world!=inventory->scene_world || f->map_resource!=inventory->map_resource ||
        &f->frame!=inventory->frame || f->seats!=inventory->seats || f->options.seats!=inventory->seat_count ||
        qa_application_launch(f->application)!=(publishing?inventory->candidate:inventory->launch) ||
        (publishing && (!inventory->candidate || inventory->configuration==UINT64_MAX)) ||
        qa_application_configuration_generation(f->application)!=
            inventory->configuration+(publishing?UINT64_C(1):UINT64_C(0)) ||
        qa_application_world(f->application)!=inventory->world ||
        (inventory->world && qa_world_geometry(inventory->world)!=inventory->geometry)) return false;
    if (inventory->engine_only) {
        qa_console *console=NULL; qa_cvars *cvars=NULL;
        if (!qa_application_startup_root_read(f->application,NULL,&console,&cvars,NULL,NULL) ||
            console!=inventory->root_console || cvars!=inventory->root_cvars) return false;
    }
    const qa_scene_frame *frame=&f->frame;
    const resource_frame *saved=&inventory->frame_roots;
    if (frame->sequence!=saved->sequence || frame->owner!=saved->owner || frame->material_order!=saved->order ||
        frame->commands!=saved->commands || frame->command_count!=saved->command_count ||
        frame->images!=saved->images || frame->image_count!=saved->image_count ||
        frame->geometries!=saved->geometries || frame->geometry_count!=saved->geometry_count ||
        frame->groups!=saved->groups || frame->group_count!=saved->group_count) return false;
    for (unsigned i=0;f->seats && i<inventory->seat_count;++i) {
        const frontend_seat *seat=f->seats+i;
        const resource_seat *root=inventory->seat_roots+i;
        if (seat->frontend!=root->frontend || seat->id!=root->id || seat->input!=root->input ||
            seat->console!=root->console || seat->ui!=root->ui || seat->hud!=root->hud ||
            seat->wheel!=root->wheel || !qa_actor_id_equal(seat->actor,root->actor)) return false;
    }
    return !inventory->candidate || qa_application_startup_candidate(f->application)==inventory->candidate;
}
static bool resource_parent_current(const frontend_resource_inventory *inventory)
{
    if (!resource_parent_matches(inventory,false)) return false;
    const qa_frontend *f=inventory->frontend;
    if(inventory->client) return qa_application_client_prepare_associated(f->application,inventory->client) &&
        qa_application_client_prepare_phase_is(inventory->client,QA_CLIENT_PREPARE_RESOURCES);
    if(inventory->video) return frontend_video_guests_resources_associated(f,inventory->video);
    return inventory->candidate || inventory->engine_only ?
        qa_application_startup_candidate(f->application)==inventory->candidate &&
        qa_application_startup_resource_phase(f->application,inventory->candidate) : !f->preparing;
}
static bool resource_parent_associated(const frontend_resource_inventory *inventory)
{
    if (!resource_parent_matches(inventory,false)) return false;
    const qa_frontend *f=inventory->frontend;
    if(inventory->client) return qa_application_client_prepare_associated(f->application,inventory->client) &&
        qa_application_client_prepare_phase_is(inventory->client,QA_CLIENT_PREPARE_RESOURCES);
    if(inventory->video) return frontend_video_guests_resources_associated(f,inventory->video);
    return inventory->candidate || inventory->engine_only ?
        qa_application_startup_resource_phase_associated(f->application,inventory->candidate) : !f->preparing;
}
static bool resource_inventory_collect(qa_frontend *f,qa_application *app,const qa_launch_snapshot *candidate,
    const qa_application_client_preparation *client,const frontend_video_guests *video,
    frontend_resource_inventory **out,qa_error *error)
{
    if(f && (!frontend_renderer_worlds_prune(f,error) || !frontend_renderer_materials_prune(f,error))) return false;
    bool engine_only=!candidate && !client && !video && app && qa_application_startup_resource_phase(app,NULL);
    if (!f || !app || f->application!=app || !out || *out || f->resource_inventory || (f->stepping && !video) ||
        f->round || f->shutdown || f->capture || f->source_restoring ||
        (client ? (!qa_application_client_prepare_associated(app,client) ||
            !qa_application_client_prepare_phase_is(client,QA_CLIENT_PREPARE_RESOURCES)) :
        video ? !frontend_video_guests_resources_associated(f,video) :
        candidate ? (qa_application_startup_candidate(app)!=candidate ||
            !qa_application_startup_resource_phase(app,candidate)) : (!engine_only && f->preparing)) ||
        !(video?owners_returned(f,video):frontend_owners_idle(f)) || !frontend_seat_callbacks_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Resource metadata requires the actual idle parent or resource-phase candidate");
    frontend_resource_inventory *inventory=calloc(1,sizeof(*inventory));
    if (!inventory) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining the structural resource roster");
    inventory->frontend=f; inventory->application=app; inventory->launch=qa_application_launch(app);
    inventory->candidate=candidate; inventory->configuration=qa_application_configuration_generation(app);
    inventory->client=client; inventory->video=video;
    inventory->engine_only=engine_only;
    if (engine_only && !qa_application_startup_root_read(app,NULL,&inventory->root_console,
        &inventory->root_cvars,NULL,error)) { free(inventory); return false; }
    inventory->world=qa_application_world(app);
    inventory->geometry=inventory->world?qa_world_geometry(inventory->world):NULL;
    inventory->scene_world=f->scene_world; inventory->map_resource=f->map_resource; inventory->frame=&f->frame;
    inventory->seats=f->seats; inventory->seat_count=f->options.seats;
    inventory->frame_roots=(resource_frame){f->frame.sequence,f->frame.owner,f->frame.material_order,
        f->frame.commands,f->frame.images,f->frame.geometries,f->frame.groups,
        f->frame.command_count,f->frame.image_count,f->frame.geometry_count,f->frame.group_count};
    if (f->seats && inventory->seat_count) {
        inventory->seat_roots=calloc(inventory->seat_count,sizeof(*inventory->seat_roots));
        if (!inventory->seat_roots) {
            free(inventory); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual physical seat roots");
        }
        for (unsigned i=0;i<inventory->seat_count;++i) {
            const frontend_seat *seat=f->seats+i;
            inventory->seat_roots[i]=(resource_seat){seat->frontend,seat->id,seat->input,seat->console,
                seat->ui,seat->hud,seat->wheel,seat->actor};
        }
    }
    f->resource_inventory=inventory;
    if (!resource_collect(inventory,error) || !resource_parent_current(inventory)) {
        f->resource_inventory=NULL; resource_metadata_free(inventory); free(inventory);
        if (error && error->code==QA_OK) frontend_fail(error,QA_ERROR_ARGUMENT,"Resource metadata parent changed during collection");
        return false;
    }
    *out=inventory; return true;
}
bool frontend_resource_inventory_collect(qa_frontend *f,qa_application *app,const qa_launch_snapshot *candidate,
    frontend_resource_inventory **out,qa_error *error)
{ return resource_inventory_collect(f,app,candidate,NULL,NULL,out,error); }
bool frontend_resource_inventory_collect_client(qa_frontend *f,const qa_application_client_preparation *client,
    frontend_resource_inventory **out,qa_error *error)
{ return resource_inventory_collect(f,qa_application_client_prepare_application(client),NULL,client,NULL,out,error); }
bool frontend_resource_inventory_collect_video(qa_frontend *f,const frontend_video_guests *video,
    frontend_resource_inventory **out,qa_error *error)
{ return resource_inventory_collect(f,f?f->application:NULL,NULL,NULL,video,out,error); }
static bool resource_cinematics_associated(const frontend_resource_inventory *inventory)
{
    if(!inventory || !inventory->frontend) return false;
    qa_q3_cinematic_handles *pool=inventory->frontend->source_cinematics;
    if(!inventory->cinematics) return !pool || qa_q3_cinematic_handles_idle(pool);
    qa_q3_cinematic_handles_options options;
    if(!pool || qa_q3_cinematic_handles_stage_owner(inventory->cinematics)!=pool ||
        !qa_q3_cinematic_handles_stage_associated(pool) || !qa_q3_cinematic_handles_read(pool,&options) ||
        qa_scene_resource_policy_source(qa_q3_cinematic_handles_stage_bank(inventory->cinematics))!=options.images) return false;
    for(size_t i=0;i<inventory->count;++i)
        if(inventory->rows[i].kind==CAPTURE_IMAGES && inventory->rows[i].owner==options.images) return true;
    return false;
}
bool frontend_resource_inventory_cinematics(frontend_resource_inventory *inventory,
    qa_q3_cinematic_handles_stage *stage,qa_error *error)
{
    if(!inventory || inventory->frontend->resource_inventory!=inventory ||
        (stage && inventory->cinematics && inventory->cinematics!=stage)) return false;
    if(!stage) {
        qa_q3_cinematic_handles *pool=inventory->frontend->source_cinematics;
        if(pool && !qa_q3_cinematic_handles_idle(pool))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Global cinematic child has not returned from checked cleanup");
        inventory->cinematics=NULL; return true;
    }
    qa_q3_cinematic_handles_stage *previous=inventory->cinematics; inventory->cinematics=stage;
    if(resource_cinematics_associated(inventory)) return true;
    inventory->cinematics=previous;
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Global cinematic child differs from the retained pool and bank roster");
}
static bool resource_roster_matches(const frontend_resource_inventory *inventory,bool metadata_only)
{
    frontend_resource_inventory current={.frontend=inventory->frontend,.checking=inventory,
        .rows=inventory->rows,.count=inventory->count,.metadata_only=metadata_only,
        .client=inventory->client,.video=inventory->video}; qa_error error={0};
    return resource_cinematics_associated(inventory) && resource_collect(&current,&error) && current.observation_cursor==inventory->observation_count &&
        current.scope_cursor==inventory->scope_count;
}
bool frontend_resource_inventory_current(const frontend_resource_inventory *inventory)
{
    return resource_parent_current(inventory) && resource_roster_matches(inventory,false) &&
        resource_parent_current(inventory);
}
bool frontend_resource_inventory_seal(frontend_resource_inventory *inventory,qa_error *error)
{
    if (!frontend_resource_inventory_current(inventory))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Resource metadata seal requires its complete actual parent and roster");
    inventory->sealed=true; return true;
}
bool frontend_resource_inventory_ready_is(const frontend_resource_inventory *inventory)
{
    return inventory && inventory->sealed && resource_parent_associated(inventory) &&
        resource_roster_matches(inventory,true) && resource_parent_associated(inventory);
}
static bool resource_parent_consuming(const frontend_resource_inventory *inventory)
{
    if(inventory && inventory->client) return resource_parent_matches(inventory,false) &&
        qa_application_client_prepare_associated(inventory->application,inventory->client) &&
        qa_application_client_prepare_entered(inventory->client,QA_CLIENT_PREPARE_CONSUMING);
    return inventory && (inventory->candidate || inventory->engine_only) &&
        qa_application_startup_publication_consuming(inventory->application,inventory->candidate) &&
        resource_parent_matches(inventory,true);
}
bool frontend_resource_inventory_consume_ready_is(const frontend_resource_inventory *inventory)
{
    return inventory && inventory->sealed && resource_parent_consuming(inventory) &&
        resource_roster_matches(inventory,true) && resource_parent_consuming(inventory);
}
bool frontend_resource_inventory_release(frontend_resource_inventory **address,qa_error *error)
{
    if (!address || !*address) return true;
    frontend_resource_inventory *inventory=*address;
    qa_frontend *f=inventory->frontend;
    if (!f || inventory->cinematics || f->resource_inventory!=inventory || f->application!=inventory->application ||
        (f->stepping && (!inventory->video || !frontend_video_guests_resources_associated(f,inventory->video))) ||
        f->round || f->shutdown || f->capture || f->source_restoring ||
        !frontend_seat_callbacks_returned(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Resource metadata release lost its actual enclosing owner");
    for (size_t i=0;i<inventory->count;++i) {
        const resource_row *row=inventory->rows+i; bool idle=false;
        switch (row->kind) {
        case CAPTURE_ASSETS: idle=qa_q3_assets_idle(row->owner); break;
        case CAPTURE_IMAGES: idle=qa_scene_resources_idle(row->owner); break;
        case CAPTURE_LIBRARY: idle=qa_material_library_idle(row->owner); break;
        case CAPTURE_FONTS: idle=qa_font_library_idle(row->owner); break;
        case CAPTURE_ORDER: idle=qa_material_order_idle(row->owner); break;
        case CAPTURE_WORLD: idle=qa_scene_world_idle(row->owner); break;
        case CAPTURE_MODEL: idle=qa_scene_model_idle(row->owner); break;
        }
        if (!idle) return frontend_fail(error,QA_ERROR_ARGUMENT,"Resource metadata retains a nonterminal child hold");
    }
    f->resource_inventory=NULL; resource_metadata_free(inventory); free(inventory); *address=NULL; return true;
}
static const void *resource_owner_at(const frontend_resource_inventory *inventory,capture_kind kind,size_t ordinal)
{ return resource_parent_associated(inventory)?resource_at(inventory,kind,ordinal):NULL; }
const qa_scene_resources *frontend_resource_inventory_images_at(const frontend_resource_inventory *i,size_t n)
{ return resource_owner_at(i,CAPTURE_IMAGES,n); }
const qa_material_library *frontend_resource_inventory_library_at(const frontend_resource_inventory *i,size_t n)
{ return resource_owner_at(i,CAPTURE_LIBRARY,n); }
const qa_font_library *frontend_resource_inventory_fonts_at(const frontend_resource_inventory *i,size_t n)
{ return resource_owner_at(i,CAPTURE_FONTS,n); }
const qa_material_order *frontend_resource_inventory_order_at(const frontend_resource_inventory *i,size_t n)
{ return resource_owner_at(i,CAPTURE_ORDER,n); }
qa_q3_presentation_assets *frontend_resource_inventory_assets_at(const frontend_resource_inventory *i,size_t n)
{ return (qa_q3_presentation_assets *)resource_owner_at(i,CAPTURE_ASSETS,n); }
const qa_scene_world *frontend_resource_inventory_world_at(const frontend_resource_inventory *i,size_t n)
{ return resource_owner_at(i,CAPTURE_WORLD,n); }
const qa_scene_model *frontend_resource_inventory_model_at(const frontend_resource_inventory *i,size_t n)
{ return resource_owner_at(i,CAPTURE_MODEL,n); }
static bool retain_registry(void *context,capture_kind kind,const void *owner,qa_error *error)
{
    return kind!=CAPTURE_ASSETS || !owner ||
        frontend_renderer_registries_include(context,(qa_q3_presentation_assets *)owner,error);
}
bool frontend_capture_begin(qa_frontend *f, frontend_capture **out, qa_error *error)
{
    if(f && (!frontend_renderer_worlds_prune(f,error) || !frontend_renderer_materials_prune(f,error))) return false;
    if (!f || !out || *out || f->stepping || f->preparing || f->round || f->shutdown || f->source_restoring ||
        !f->application || qa_application_client_prepare_active(f->application) ||
        !frontend_owners_idle(f) || !frontend_seat_callbacks_checkpoint_ready(f,error) ||
        !frontend_save_commands_capture_ready(f) || !frontend_cinematic_capture_ready(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture requires idle actual owners and an empty lease");
    if (!frontend_q3_source_color_publication_finish(f,error) ||
        !frontend_selected_character_refresh(f,error) ||
        !frontend_renderer_registries_refresh(f,error) ||
        !registries(f,false,retain_registry,f,error)) return false;
    frontend_capture *capture=calloc(1,sizeof(*capture));
    if (!capture) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining the frontend capture lease");
    capture->frontend=f; f->capture=capture;
    bool ok=registries(f,false,capture_add,capture,error) && remote_capture_roots(f,capture,error) &&
        registry_children(capture_add,capture,capture_assets,error);
    /* Registry entry preflights strict child idle, so it precedes child tokens. */
    for (size_t i=0;ok && i<capture->count;++i)
        if(capture->rows[i].kind==CAPTURE_ASSETS) ok=hold(capture->rows+i,error);
    ok=ok && heaps(f,false,capture_add,capture,error) && remote_capture_heaps(capture,error) &&
        registry_children(capture_add,capture,capture_assets,error);
    for(size_t i=0;ok && i<capture->count;++i) if(capture->rows[i].kind==CAPTURE_IMAGES) {
        const qa_scene_resources *owner=capture->rows[i].owner;
        for(size_t j=0;ok && j<qa_scene_resources_parent_count(owner);++j) {
            qa_scene_resources *parent=NULL;
            ok=qa_scene_resources_parent_at(owner,j,&parent) && parent && add(capture,CAPTURE_IMAGES,parent,error);
        }
    }
    for (size_t i=0;ok && i<capture->count;++i)
        if(!capture->rows[i].held) ok=hold(capture->rows+i,error);
    if (!ok) {
        frontend_capture_end(capture);
        if (error && error->code==QA_OK) frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture has an unqualified actual owner");
        return false;
    }
    *out=capture; return true;
}
void frontend_capture_end(frontend_capture *capture)
{
    if (!capture) return;
    for(unsigned pass=0;pass<2;++pass) for (size_t i=capture->count;i>0;--i) {
        capture_row *row=capture->rows+i-1;
        if (!row->held || (row->kind==CAPTURE_ASSETS)!=(pass!=0)) continue;
        switch (row->kind) {
        case CAPTURE_ASSETS: qa_q3_assets_capture_end((qa_q3_presentation_assets *)row->owner); break;
        case CAPTURE_IMAGES: qa_scene_resources_capture_end(row->token.images); break;
        case CAPTURE_LIBRARY: qa_material_library_capture_end(row->owner); break;
        case CAPTURE_FONTS: qa_font_library_capture_end(row->token.fonts); break;
        case CAPTURE_ORDER: qa_material_order_capture_end(row->owner); break;
        case CAPTURE_WORLD: qa_scene_world_capture_end(row->token.world); break;
        case CAPTURE_MODEL: qa_scene_model_capture_end(row->token.model); break;
        }
    }
    if (capture->frontend->capture==capture) capture->frontend->capture=NULL;
    free(capture->remote); free(capture->rows); free(capture);
}
static const void *owner_at(const frontend_capture *capture, capture_kind kind, size_t ordinal)
{
    if (!capture || capture->frontend->capture!=capture) return NULL;
    for (size_t i=0;i<capture->count;++i)
        if (capture->rows[i].kind==kind && !ordinal--) return capture->rows[i].owner;
    return NULL;
}
const qa_scene_resources *frontend_capture_images_at(const frontend_capture *c,size_t i)
{ return owner_at(c,CAPTURE_IMAGES,i); }
const qa_material_library *frontend_capture_library_at(const frontend_capture *c,size_t i)
{ return owner_at(c,CAPTURE_LIBRARY,i); }
const qa_font_library *frontend_capture_fonts_at(const frontend_capture *c,size_t i)
{ return owner_at(c,CAPTURE_FONTS,i); }
qa_q3_presentation_assets *frontend_capture_assets_at(const frontend_capture *c,size_t i)
{ return (qa_q3_presentation_assets *)owner_at(c,CAPTURE_ASSETS,i); }
const qa_scene_world *frontend_capture_world_at(const frontend_capture *c,size_t i)
{ return owner_at(c,CAPTURE_WORLD,i); }
const qa_scene_model *frontend_capture_model_at(const frontend_capture *c,size_t i)
{ return owner_at(c,CAPTURE_MODEL,i); }
