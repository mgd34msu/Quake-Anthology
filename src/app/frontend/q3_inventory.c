#include "q3_inventory.h"
#include "renderer_registries.h"
#include "component_scene.h"
#include "capture.h"
#include "content_refs.h"
#include "material_inventory.h"
#include "save_private.h"
#include "scene_refs.h"
#include "source_restore.h"
#include "native_q3_client.h"
#include "equipment_q3_save.h"
#include "equipment_gear_save.h"
#include "selected_character_save.h"
#include "selected_effects_save.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "network_initial_graph.h"
#include "unified_media_inventory.h"
#include "qa/media_library_save.h"
#include "qa/media_resource.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_presentation_save.h"
#include "qa/q3_presentation_media_save.h"
#include "qa/scene_resource_save.h"
#include "qa/vfs_view_save.h"

typedef enum q3_owner_kind { Q3_OWNER_SOURCE, Q3_OWNER_NATIVE, Q3_OWNER_SELECTED, Q3_OWNER_CHARACTER, Q3_OWNER_EFFECTS,
    Q3_OWNER_GEAR, Q3_OWNER_REMOTE, Q3_OWNER_INITIAL,Q3_OWNER_COMPONENT,Q3_OWNER_REGISTRY } q3_owner_kind;
static bool parent_registry(q3_owner_kind kind)
{ return kind==Q3_OWNER_REMOTE || kind==Q3_OWNER_INITIAL || kind==Q3_OWNER_COMPONENT; }
static bool assets_only(q3_owner_kind kind)
{ return kind==Q3_OWNER_SELECTED || kind==Q3_OWNER_CHARACTER || kind==Q3_OWNER_GEAR; }
static bool private_owner(q3_owner_kind kind)
{ return assets_only(kind) || kind==Q3_OWNER_EFFECTS; }
static frontend_scene_owner_kind scene_owner(q3_owner_kind kind)
{
    switch (kind) {
    case Q3_OWNER_SOURCE: return FRONTEND_SCENE_OWNER_Q3;
    case Q3_OWNER_NATIVE: return FRONTEND_SCENE_OWNER_NATIVE_Q3;
    case Q3_OWNER_SELECTED: return FRONTEND_SCENE_OWNER_SELECTED_Q3;
    case Q3_OWNER_CHARACTER: return FRONTEND_SCENE_OWNER_CHARACTER;
    case Q3_OWNER_EFFECTS: return FRONTEND_SCENE_OWNER_EFFECTS;
    case Q3_OWNER_GEAR: return FRONTEND_SCENE_OWNER_GEAR;
    case Q3_OWNER_REMOTE: return FRONTEND_SCENE_OWNER_REMOTE;
    case Q3_OWNER_INITIAL: return FRONTEND_SCENE_OWNER_INITIAL;
    case Q3_OWNER_COMPONENT: return FRONTEND_SCENE_OWNER_COMPONENT;
    case Q3_OWNER_REGISTRY: return FRONTEND_SCENE_OWNER_REGISTRY;
    }
    return FRONTEND_SCENE_OWNER_FRONTEND;
}
typedef struct q3_component_owner {
    qa_actor_owner owner;
    uint32_t seat, launch_seat;
    uint64_t identity;
    const qa_vfs *source_files, *mounts;
    qa_scene_resources *images;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    qa_media_library *movies;
    qa_material_library *selected_materials;
    qa_audio_bank *sounds;
    qa_scene_world *parent_world;
    qa_collision_geometry *parent_geometry;
} q3_component_owner;
typedef struct q3_group {
    q3_owner_kind kind;
    size_t ordinal;
    q3_component_owner source;
    qa_q3_presentation_binding binding;
    qa_q3_presentation_asset_options services;
    size_t assets, presentation, media;
    uint64_t mounts, source_view;
    qa_bytes selected;
} q3_group;
typedef struct q3_registry { size_t group; qa_bytes state; } q3_registry;
typedef struct q3_media {
    size_t group, count;
    const qa_resource **resources;
    qa_bytes state;
} q3_media;
typedef struct q3_presentation {
    size_t group;
    uint64_t frame, world, collision, pool, resource;
    size_t offset, length;
    bool entities;
    qa_bytes scene, media;
} q3_presentation;
struct frontend_q3_inventory {
    qa_frontend *frontend;
    frontend_q3_refs refs;
    q3_group *groups;
    q3_registry *registries;
    q3_media *media;
    q3_presentation *presentations;
    size_t group_count, registry_count, media_count, presentation_count;
    struct q3_scope *movie_scopes;
    bool prepared, importing, restored;
};
typedef struct q3_scope {
    frontend_q3_inventory *inventory;
    size_t group;
    q3_media *media;
    uint64_t target;
    frontend_remote_q3_modules *module_owner;
    struct q3_scope *next;
} q3_scope;
bool frontend_q3_assets_encode(void *context,const qa_q3_presentation_assets *assets,uint64_t *out,qa_error *error)
{
    frontend_q3_inventory *inventory=context;
    if(!inventory || !out) return false;
    if(!assets) { *out=0; return true; }
    for(size_t i=0;i<inventory->registry_count;++i) {
        size_t group=inventory->registries[i].group;
        if(group>=inventory->group_count) return false;
        if(inventory->groups[group].source.assets==assets) { *out=i+1; return true; }
    }
    size_t media_count=0;
    if(!frontend_unified_media_inventory_count(inventory->frontend,&media_count,error)) return false;
    for(size_t i=0;i<media_count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(inventory->frontend,i,&media,error)) return false;
        for(size_t n=0;media && n<frontend_unified_media_bank_count(media);++n) {
            frontend_unified_bank_view bank; uint64_t key=0;
            if(!frontend_unified_media_bank_read(media,n,&bank)) return false;
            if(bank.q3_assets==assets) {
                if(!frontend_unified_media_bank_key(i,n,&key) || (key>>63))
                    return frontend_fail(error,QA_ERROR_FORMAT,"Unified registry ordinal overflows its actual media namespace");
                *out=key|UINT64_C(0x8000000000000000); return true;
            }
        }
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Raw Source cell registry is outside the genuine Q3 owner roster");
}
bool frontend_q3_assets_decode(void *context,uint64_t id,qa_q3_presentation_assets **out,qa_error *error)
{
    frontend_q3_inventory *inventory=context;
    if(!inventory || !out) return false;
    if(id>>63) {
        size_t media_index=0,bank_index=0; frontend_unified_media *media=NULL; frontend_unified_bank_view bank;
        if(!frontend_unified_media_bank_key_read(id&UINT64_C(0x7fffffffffffffff),&media_index,&bank_index) ||
            !frontend_unified_media_inventory_at(inventory->frontend,media_index,&media,error) || !media ||
            !frontend_unified_media_bank_read(media,bank_index,&bank) || !bank.q3_assets)
            return frontend_fail(error,QA_ERROR_FORMAT,"Saved Unified raw cell registry has no genuine media-bank owner");
        *out=bank.q3_assets; return true;
    }
    if(id>inventory->registry_count) return false;
    if(!id) { *out=NULL; return true; }
    size_t group=inventory->registries[id-1].group;
    if(group>=inventory->group_count || !inventory->groups[group].source.assets)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved raw Source cell registry has no actual prepared owner");
    *out=inventory->groups[group].source.assets; return true;
}

static bool phase(qa_frontend *f,const frontend_q3_refs *refs,bool restoring,qa_error *error)
{
    if (!f || !f->application || !refs || !refs->content || !refs->scene || !refs->models ||
        !refs->worlds || refs->content!=qa_application_content_graph_read(f->application) ||
        f->stepping || f->preparing || f->round || f->source_restoring!=restoring ||
        (restoring?f->capture!=NULL:f->capture==NULL) || !(restoring?frontend_seat_callbacks_returned(f):frontend_seat_callbacks_checkpoint_ready(f,error)) ||
        !frontend_native_q2_callbacks_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 inventory requires its actual capture or isolated candidate owners");
    return !restoring || frontend_source_complete_groups(f,error);
}
void frontend_q3_inventory_destroy(frontend_q3_inventory *inventory)
{
    if (!inventory) return;
    if (inventory->media) for (size_t i=0;i<inventory->media_count;++i) free(inventory->media[i].resources);
    while(inventory->movie_scopes) {
        q3_scope *scope=inventory->movie_scopes;
        inventory->movie_scopes=scope->next; free(scope);
    }
    free(inventory->groups); free(inventory->registries); free(inventory->media);
    free(inventory->presentations); free(inventory);
}
static bool collect(qa_frontend *f,const frontend_q3_refs *refs,bool restoring,bool roster_only,
    frontend_q3_inventory **out,qa_error *error)
{
    if (!out || *out || !phase(f,refs,restoring,error)) return false;
    size_t sources=frontend_source_group_count(f),native=frontend_native_q3_count(f),
        selected=frontend_equipment_q3_count(f),character=frontend_selected_character_count(f),
        effects=frontend_selected_effects_count(f),gear=frontend_equipment_gear_count(f);
    if (native>SIZE_MAX-sources || selected>SIZE_MAX-sources-native ||
        character>SIZE_MAX-sources-native-selected || effects>SIZE_MAX-sources-native-selected-character ||
        gear>SIZE_MAX-sources-native-selected-character-effects)
        return frontend_fail(error,QA_ERROR_MEMORY,"Q3 component owner inventory exceeds address space");
    size_t count=sources+native+selected+character+effects+gear,base=count,remote=frontend_remote_q3_count(f);
    frontend_network_initial_graph_view initial;
    if(remote>SIZE_MAX-count || !frontend_network_initial_graph_read(f,&initial,error)) return false;
    count+=remote;
    if(initial.present) { if(count==SIZE_MAX) return false; ++count; }
    size_t component_base=count,components=frontend_component_scene_count(f);
    if(components>SIZE_MAX-count) return false;
    count+=components;
    size_t ordinary_count=count,retained=frontend_renderer_registries_count(f);
    if(retained>SIZE_MAX-count) return false;
    count+=retained;
    if (count>SIZE_MAX/sizeof(q3_group) || count>SIZE_MAX/sizeof(q3_registry) ||
        count>SIZE_MAX/sizeof(q3_media) || count>SIZE_MAX/sizeof(q3_presentation) || (count && !refs->audio && !roster_only))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 source inventory exceeds its actual holder namespace");
    frontend_q3_inventory *inventory=calloc(1,sizeof(*inventory));
    if (!inventory) return frontend_fail(error,QA_ERROR_MEMORY,"Collecting actual Q3 source owners");
    inventory->frontend=f; inventory->refs=*refs; inventory->group_count=count;
    if (count) {
        inventory->groups=calloc(count,sizeof(*inventory->groups));
        inventory->registries=calloc(count,sizeof(*inventory->registries));
        inventory->media=calloc(count,sizeof(*inventory->media));
        inventory->presentations=calloc(count,sizeof(*inventory->presentations));
    }
    bool ok=!count || (inventory->groups && inventory->registries && inventory->media && inventory->presentations);
    if (!ok) frontend_fail(error,QA_ERROR_MEMORY,"Collecting physical Q3 source rows");
    for (size_t i=0;ok && i<ordinary_count;++i) {
        q3_group *group=inventory->groups+i; qa_scene_world *world=NULL; qa_collision_geometry *collision=NULL;
        group->kind=i<sources?Q3_OWNER_SOURCE:i<sources+native?Q3_OWNER_NATIVE:
            i<sources+native+selected?Q3_OWNER_SELECTED:
            i<sources+native+selected+character?Q3_OWNER_CHARACTER:
            i<sources+native+selected+character+effects?Q3_OWNER_EFFECTS:i<base?Q3_OWNER_GEAR:
            i<base+remote?Q3_OWNER_REMOTE:i<component_base?Q3_OWNER_INITIAL:Q3_OWNER_COMPONENT;
        group->ordinal=i<sources?i:i<sources+native?i-sources:
            i<sources+native+selected?i-sources-native:
            i<sources+native+selected+character?i-sources-native-selected:
            i<sources+native+selected+character+effects?i-sources-native-selected-character:
            i<base?i-sources-native-selected-character-effects:i<base+remote?i-base:i<component_base?0:i-component_base;
        if (group->kind==Q3_OWNER_SOURCE) {
            frontend_source_group_view source;
            ok=frontend_source_group_read(f,group->ordinal,&source);
            if (ok) group->source=(q3_component_owner){.owner=source.owner,.seat=source.seat,
                .launch_seat=source.launch_seat,.identity=source.identity,.source_files=source.source_files,
                .mounts=source.mounts,.images=source.images,.assets=source.assets,
                .presentation=source.presentation,.movies=source.movies};
        } else if (group->kind==Q3_OWNER_NATIVE) {
            frontend_native_q3_view source;
            ok=frontend_native_q3_read(f,group->ordinal,&source,error);
            if (ok) group->source=(q3_component_owner){.owner=source.receiver,.seat=source.seat,
                .launch_seat=source.launch_seat,.identity=source.identity,.source_files=source.source_files,
                .mounts=source.mounts,.images=source.images,.assets=source.assets,
                .presentation=source.presentation,.movies=source.movies};
        } else if (group->kind==Q3_OWNER_SELECTED) {
            frontend_equipment_q3_owner_view source;
            ok=frontend_equipment_q3_at(f,group->ordinal,&source,error);
            if (ok) group->source=(q3_component_owner){.owner=source.provider,
                .source_files=source.content.mounts,.mounts=source.content.mounts,
                .images=source.content.images,.assets=source.assets,.selected_materials=source.content.materials};
        } else if (group->kind==Q3_OWNER_CHARACTER) {
            frontend_selected_character_view source;
            ok=frontend_selected_character_at(f,group->ordinal,&source,error);
            if (ok) group->source=(q3_component_owner){.owner=source.selection.owner,
                .launch_seat=source.launch_seat,.source_files=source.content.mounts,.mounts=source.content.mounts,
                .images=source.content.images,.assets=source.assets,.selected_materials=source.content.materials};
        } else if (group->kind==Q3_OWNER_EFFECTS) {
            frontend_selected_effects_view source;
            ok=frontend_selected_effects_at(f,group->ordinal,&source,error);
            if (ok) group->source=(q3_component_owner){.owner=source.provider,
                .seat=source.physical_seat,.identity=source.identity,.source_files=source.source_files,
                .mounts=source.content.mounts,.images=source.content.images,.assets=source.assets,
                .presentation=source.presentation};
        } else if(group->kind==Q3_OWNER_GEAR) {
            frontend_equipment_gear_owner_view source;
            ok=frontend_equipment_gear_at(f,group->ordinal,&source,error) &&
                qa_application_equipment_content_current(f->application,&source.source);
            if (ok) group->source=(q3_component_owner){.owner=source.source.owner,
                .source_files=source.source.files,.mounts=source.content.mounts,
                .images=source.content.images,.assets=source.assets,.selected_materials=source.content.materials};
        } else if(group->kind==Q3_OWNER_REMOTE) {
            frontend_remote_q3_resources source;
            ok=frontend_remote_q3_resources_read(frontend_remote_q3_at(f,group->ordinal),&source,error);
            if(ok) group->source=(q3_component_owner){.owner=source.domain.source.receiver.receiver,
                .seat=source.physical_seat,.launch_seat=source.domain.source.receiver.seat,.identity=source.identity,
                .source_files=source.domain.content,.mounts=source.mounts,.images=source.images,
                .assets=source.assets,.movies=source.movies,.selected_materials=source.materials,
                .sounds=source.sounds,.parent_world=source.world,.parent_geometry=source.geometry};
        } else if(group->kind==Q3_OWNER_COMPONENT) {
            frontend_component_scene_view source;
            ok=frontend_component_scene_metadata_read(f,group->ordinal,&source,error);
            if(ok) group->source=(q3_component_owner){.owner=source.receiver,.seat=source.physical_seat,
                .identity=source.identity,.source_files=source.descriptor?source.descriptor->content:
                    source.recipe_provider?source.recipe_provider->content:NULL,
                .mounts=source.files,.images=source.images,.assets=source.assets,
                .movies=source.media,.selected_materials=source.materials,.sounds=source.sounds};
        } else {
            frontend_remote_q3_initial_view source;
            ok=initial.parent && frontend_remote_q3_initial_read(initial.parent,&source,error);
            if(ok) group->source=(q3_component_owner){.owner=source.attempt.source.receiver.receiver,
                .seat=source.physical_seat,.launch_seat=source.attempt.source.receiver.seat,.identity=source.identity,
                .source_files=source.descriptor->content,.mounts=source.mounts,.images=source.images,
                .assets=source.assets,.movies=source.movies,.selected_materials=source.materials,.sounds=source.sounds};
        }
        ok=ok && group->source.assets &&
            qa_q3_assets_services(group->source.assets,&group->services,&world,&collision,error);
        if (ok && !assets_only(group->kind) && !parent_registry(group->kind)) ok=
            group->source.presentation && (group->kind==Q3_OWNER_EFFECTS?!group->source.movies:group->source.movies!=NULL) &&
            (restoring?
                qa_q3_presentation_options_read(group->source.presentation,&group->binding.options,error):
                qa_q3_presentation_binding_read(group->source.presentation,&group->binding,error)) &&
            (group->kind==Q3_OWNER_SOURCE?
                frontend_source_group_q3_ready(f,group->ordinal,&group->binding.options,&group->services,error):
                group->kind==Q3_OWNER_NATIVE?
                frontend_native_q3_q3_ready(f,group->ordinal,&group->binding.options,&group->services,error):
                frontend_selected_effects_q3_ready(f,group->ordinal,&group->binding.options,&group->services,error));
        if (ok && assets_only(group->kind)) ok=
            group->services.provider.mounts==group->source.mounts &&
            group->services.provider.images==group->source.images &&
            group->services.provider.materials==group->source.selected_materials &&
            group->services.provider.family==QA_SCENE_Q3 && !world && !collision &&
            !group->services.sounds && !group->services.movies && !group->services.zero_sound &&
            !group->services.context && !group->services.select && !group->services.print;
        if(ok && parent_registry(group->kind)) ok=group->source.movies &&
            group->services.provider.mounts==group->source.mounts &&
            group->services.provider.images==group->source.images &&
            group->services.provider.materials==group->source.selected_materials &&
            group->services.provider.family==QA_SCENE_Q3 &&
            group->services.movies==group->source.movies && group->services.sounds==group->source.sounds &&
            world==group->source.parent_world && collision==(world?group->source.parent_geometry:NULL) &&
            qa_media_library_resource_owner(group->source.movies)==group->source.images;
        if (!ok) break;
        group->mounts=qa_application_content_view_id(refs->content,group->source.mounts);
        group->source_view=qa_application_content_view_id(refs->content,group->source.source_files);
        ok=group->mounts && group->source_view &&
            (assets_only(group->kind)?(group->kind==Q3_OWNER_GEAR?
                qa_vfs_lookup_equal(group->source.mounts,group->source.source_files):group->mounts==group->source_view):
            (group->mounts!=group->source_view && (parent_registry(group->kind) || restoring ||
            (group->binding.world==world && group->binding.geometry==collision)) &&
            (group->kind==Q3_OWNER_EFFECTS || qa_media_library_resource_owner(group->source.movies)==group->source.images))) &&
            qa_vfs_resources(group->source.mounts)==qa_vfs_resources(group->source.source_files) &&
            (!restoring || (qa_q3_assets_idle(group->source.assets) &&
                (assets_only(group->kind) || parent_registry(group->kind) || qa_q3_presentation_idle(group->source.presentation))));
        if (!ok) break;
        size_t a=0,p=0,m=0;
        while (a<inventory->registry_count && inventory->groups[inventory->registries[a].group].source.assets!=group->source.assets) ++a;
        while (p<inventory->presentation_count && inventory->groups[inventory->presentations[p].group].source.presentation!=group->source.presentation) ++p;
        while (m<inventory->media_count && inventory->groups[inventory->media[m].group].source.movies!=group->source.movies) ++m;
        if (a==inventory->registry_count) inventory->registries[inventory->registry_count++].group=i;
        if (group->source.presentation && p==inventory->presentation_count)
            inventory->presentations[inventory->presentation_count++].group=i;
        if (group->source.movies && m==inventory->media_count) inventory->media[inventory->media_count++].group=i;
        group->assets=a; group->presentation=group->source.presentation?p:SIZE_MAX;
        group->media=group->source.movies?m:SIZE_MAX;
    }
    inventory->group_count=ordinary_count;
    for(size_t i=0;ok && i<retained;++i) {
        qa_q3_presentation_assets *assets=NULL;
        ok=frontend_renderer_registries_at(f,i,&assets,error) && assets;
        bool alias=false;
        for(size_t j=0;ok && j<inventory->registry_count;++j)
            if(inventory->groups[inventory->registries[j].group].source.assets==assets) alias=true;
        size_t media_count=0;
        ok=ok && frontend_unified_media_inventory_count(f,&media_count,error);
        for(size_t m=0;ok && !alias && m<media_count;++m) {
            frontend_unified_media *media=NULL;
            ok=frontend_unified_media_inventory_at(f,m,&media,error);
            for(size_t n=0;ok && media && n<frontend_unified_media_bank_count(media);++n) {
                frontend_unified_bank_view bank;
                ok=frontend_unified_media_bank_read(media,n,&bank);
                if(ok && bank.q3_assets==assets) alias=true;
            }
        }
        if(!ok || alias) continue;
        size_t g=inventory->group_count++,a=inventory->registry_count++;
        q3_group *group=inventory->groups+g;
        group->kind=Q3_OWNER_REGISTRY; group->ordinal=i;
        group->source.assets=assets; group->assets=a; group->presentation=group->media=SIZE_MAX;
        ok=qa_q3_assets_services(assets,&group->services,&group->source.parent_world,
            &group->source.parent_geometry,error);
        if(ok) {
            group->source.mounts=group->source.source_files=group->services.provider.mounts;
            group->source.images=group->services.provider.images;
            group->source.selected_materials=group->services.provider.materials;
            group->mounts=group->source_view=qa_application_content_view_id(refs->content,group->source.mounts);
            inventory->registries[a].group=g;
            ok=group->mounts!=0 && (!restoring || qa_q3_assets_idle(assets));
        }
    }
    if (ok && !restoring) {
        size_t actual=0,expected=inventory->registry_count,media_count=0;
        ok=frontend_unified_media_inventory_count(f,&media_count,error);
        for(size_t i=0;ok && i<media_count;++i) {
            frontend_unified_media *media=NULL;
            ok=frontend_unified_media_inventory_at(f,i,&media,error);
            for(size_t n=0;ok && media && n<frontend_unified_media_bank_count(media);++n) {
                frontend_unified_bank_view bank;
                ok=frontend_unified_media_bank_read(media,n,&bank);
                if(ok && bank.q3_assets) { if(expected==SIZE_MAX) ok=false; else ++expected; }
            }
        }
        while(ok && frontend_capture_assets_at(f->capture,actual)) {
            uint64_t key=0;
            ok=frontend_q3_assets_encode(inventory,frontend_capture_assets_at(f->capture,actual),&key,error);
            ++actual;
        }
        ok=ok && actual==expected;
    }
    if (!ok) {
        frontend_q3_inventory_destroy(inventory);
        if (error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Q3 owner rows leave their genuine source services or shared capture");
        return false;
    }
    *out=inventory; return true;
}
bool frontend_q3_inventory_capture(qa_frontend *f,const frontend_q3_refs *refs,
    frontend_q3_inventory **out,qa_error *error)
{ return collect(f,refs,false,false,out,error); }
bool frontend_q3_inventory_restore_roster(qa_frontend *f,const frontend_q3_refs *refs,
    frontend_q3_inventory **out,qa_error *error)
{ return collect(f,refs,true,true,out,error); }
static bool write_blob(qa_source_save_io *io,const qa_buffer *buffer)
{
    size_t count=buffer->size;
    return qa_source_save_count(io,&count,SIZE_MAX) && qa_source_save_bytes(io,buffer->data,count);
}
static bool read_blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t count=0;
    if (!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if (io->offset>io->input.size || count>io->input.size-io->offset)
        return frontend_fail(io->error,QA_ERROR_FORMAT,"Saved Q3 blob exceeds its remaining envelope");
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool equal_count(qa_source_save_io *io,size_t expected)
{
    size_t saved=expected;
    return qa_source_save_count(io,&saved,SIZE_MAX) && saved==expected;
}
static bool header(qa_source_save_io *io,const frontend_q3_inventory *inventory)
{
    uint8_t magic[4]={'Q','F','Q','3'}; return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFQ3",4) &&
        equal_count(io,inventory->group_count) &&
        equal_count(io,inventory->registry_count) && equal_count(io,inventory->presentation_count) &&
        equal_count(io,inventory->media_count);
}
static bool gear_namespace(qa_source_save_io *io,qa_application *application,qa_actor_owner *owner)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    qa_strings *strings=qa_session_strings(io->session);
    char *text=!reading && *owner<=UINT32_MAX?(char *)qa_strings_cstr(strings,(qa_string_id)*owner):NULL;
    if (!reading && (!text || !*text)) return false;
    bool ok=qa_source_save_owned_text(io,&text);
    if (reading) {
        *owner=text?qa_strings_find(strings,(qa_bytes){(const uint8_t *)text,strlen(text)}):0;
        free(text);
    }
    qa_application_equipment_content source;
    return ok && *owner && qa_application_equipment_content_read(application,*owner,&source,io->error) &&
        qa_application_equipment_content_current(application,&source);
}
static bool metadata(qa_source_save_io *io,frontend_q3_inventory *inventory,size_t ordinal)
{
    q3_group *group=inventory->groups+ordinal;
    uint32_t kind=group->kind; size_t row=group->ordinal;
    qa_actor_owner owner=group->source.owner; uint32_t seat=group->source.seat,launch_seat=group->source.launch_seat;
    uint64_t identity=group->source.identity,mounts=group->mounts,source=group->source_view;
    size_t assets=group->assets,presentation=group->presentation,media=group->media;
    if(group->kind==Q3_OWNER_REGISTRY) return qa_source_save_u32(io,&kind) && kind==(uint32_t)group->kind &&
        qa_source_save_count(io,&row,SIZE_MAX) && row==group->ordinal &&
        qa_source_save_u64(io,&mounts) && mounts==group->mounts &&
        qa_source_save_count(io,&assets,SIZE_MAX) && assets==group->assets;
    bool ok=qa_source_save_u32(io,&kind) && kind==(uint32_t)group->kind &&
        qa_source_save_count(io,&row,SIZE_MAX) && row==group->ordinal &&
        (parent_registry(group->kind)?qa_source_save_string(io,&owner):
            group->kind==Q3_OWNER_GEAR?gear_namespace(io,inventory->frontend->application,&owner):
            frontend_save_provider(io,inventory->frontend->application,&owner)) && owner==group->source.owner;
    if (assets_only(group->kind)) return ok &&
        (group->kind!=Q3_OWNER_CHARACTER ||
            (qa_source_save_u32(io,&launch_seat) && launch_seat==group->source.launch_seat)) &&
        qa_source_save_u64(io,&mounts) && mounts==group->mounts &&
        (group->kind!=Q3_OWNER_GEAR || (qa_source_save_u64(io,&source) && source==group->source_view)) &&
        qa_source_save_count(io,&assets,SIZE_MAX) && assets==group->assets;
    return ok &&
        qa_source_save_u32(io,&seat) && seat==group->source.seat &&
        (group->kind==Q3_OWNER_EFFECTS ||
            (qa_source_save_u32(io,&launch_seat) && launch_seat==group->source.launch_seat)) &&
        qa_source_save_u64(io,&identity) && identity==group->source.identity &&
        qa_source_save_u64(io,&mounts) && mounts==group->mounts &&
        qa_source_save_u64(io,&source) && source==group->source_view &&
        qa_source_save_count(io,&assets,SIZE_MAX) && assets==group->assets &&
        qa_source_save_count(io,&presentation,SIZE_MAX) && presentation==group->presentation &&
        qa_source_save_count(io,&media,SIZE_MAX) && media==group->media;
}
static bool resource_encode(void *context,const qa_resource *resource,uint64_t *pool,uint64_t *version,qa_error *error)
{
    q3_scope *scope=context;
    if (!pool || !version || pool==version ||
        !qa_application_content_resource_id(scope->inventory->refs.content,resource,pool,version))
        return frontend_fail(error,QA_ERROR_FORMAT,"Q3 retained resource is outside its actual content graph");
    return true;
}
static bool resource_decode(void *context,uint64_t pool,uint64_t version,const qa_resource **out,qa_error *error)
{
    q3_scope *scope=context;
    const qa_resource *resource=qa_application_content_resource(scope->inventory->refs.content,pool,version);
    if (!out || !resource) return frontend_fail(error,QA_ERROR_FORMAT,"Saved Q3 resource is absent from the genuine restored content pool");
    *out=resource; return true;
}
static bool selected_view_encode(void *context,const qa_vfs *view,uint64_t *key,qa_error *error)
{
    q3_scope *scope=context;
    uint64_t id=qa_application_content_view_id(scope->inventory->refs.content,view);
    if (!key || !id) return frontend_fail(error,QA_ERROR_FORMAT,"Gear view is outside its actual content graph");
    *key=id; return true;
}
static bool selected_view_decode(void *context,uint64_t key,qa_vfs **out,qa_error *error)
{
    q3_scope *scope=context;
    const qa_vfs *view=qa_application_content_view(scope->inventory->refs.content,key);
    if (!out || !view) return frontend_fail(error,QA_ERROR_FORMAT,"Saved gear view has no actual retained graph row");
    *out=(qa_vfs *)view; return true;
}
static q3n_selected_media_refs selected_refs(q3_scope *scope)
{ return (q3n_selected_media_refs){scope,selected_view_encode,selected_view_decode,resource_encode,resource_decode}; }
static bool media_resources(qa_source_save_io *io,frontend_q3_inventory *inventory,q3_media *media)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    q3_group *group=inventory->groups+media->group;
    size_t count=reading?0:qa_media_library_record_count(group->source.movies);
    if (!qa_source_save_count(io,&count,reading?(io->input.size-io->offset)/16:SIZE_MAX/sizeof(*media->resources)) ||
        count>SIZE_MAX/sizeof(*media->resources)) return false;
    media->count=count; media->resources=count?calloc(count,sizeof(*media->resources)):NULL;
    if (count && !media->resources) return frontend_fail(io->error,QA_ERROR_MEMORY,"Collecting exact media cache resource versions");
    q3_scope scope={.inventory=inventory,.group=media->group,.media=media};
    for (size_t i=0;i<count;++i) {
        uint64_t pool=0,version=0;
        const qa_resource *resource=reading?NULL:qa_cinematic_asset_resource(qa_media_library_record_at(group->source.movies,i));
        if ((!reading && !resource_encode(&scope,resource,&pool,&version,io->error)) ||
            !qa_source_save_u64(io,&pool) || !qa_source_save_u64(io,&version) ||
            (reading && !resource_decode(&scope,pool,version,&resource,io->error)) || !resource ||
            qa_resource_pool_find(qa_vfs_resources(group->source.mounts),qa_resource_id(resource))!=resource) return false;
        media->resources[i]=resource;
    }
    return true;
}
static bool library_resource_encode(void *context,const qa_resource *resource,uint64_t *key,qa_error *error)
{
    q3_scope *scope=context;
    for (size_t i=0;key && i<scope->media->count;++i) if (scope->media->resources[i]==resource) { *key=i+1; return true; }
    return frontend_fail(error,QA_ERROR_FORMAT,"Media resource has no actual physical cache row");
}
static bool library_resource_decode(void *context,uint64_t key,const qa_resource **resource,qa_error *error)
{
    q3_scope *scope=context;
    if (!resource || !key || key>scope->media->count)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved media resource row is absent");
    *resource=scope->media->resources[key-1]; return *resource!=NULL;
}
static bool image_encode(void *context,const qa_scene_image *image,uint64_t *key,qa_error *error)
{ return frontend_scene_image_encode(((q3_scope *)context)->inventory->refs.scene,image,key,error); }
static bool image_decode(void *context,uint64_t key,const qa_scene_image **image,qa_error *error)
{ return frontend_scene_image_decode(((q3_scope *)context)->inventory->refs.scene,key,image,error); }
static qa_media_library_checkpoint_refs library_refs(q3_scope *scope)
{
    return (qa_media_library_checkpoint_refs){scope,library_resource_encode,library_resource_decode,image_encode,image_decode};
}
static bool media_asset_encode(void *context,const qa_cinematic_asset *asset,uint64_t *key,qa_error *error)
{
    q3_scope *scope=context;
    qa_media_library *library=scope->inventory->groups[scope->group].source.movies;
    size_t count=qa_media_library_record_count(library);
    for (size_t i=0;key && i<count;++i) if (qa_media_library_record_at(library,i)==asset) { *key=i+1; return true; }
    return frontend_fail(error,QA_ERROR_FORMAT,"Q3 movie asset is outside its actual source media cache");
}
static bool media_path_format(const char *path,qa_cinematic_format format)
{
    const char *extension=path?strrchr(path,'.'):NULL;
    if (!extension || strlen(extension)!=4) return false;
    char folded[4];
    for (size_t i=0;i<4;++i) {
        unsigned c=(unsigned char)extension[i];
        folded[i]=(char)(c>='A' && c<='Z'?c+'a'-'A':c);
    }
    const char *expected=format==QA_CINEMATIC_CIN?".cin":format==QA_CINEMATIC_ROQ?".roq":
        format==QA_CINEMATIC_OGV?".ogv":format==QA_CINEMATIC_IMAGE?".pcx":NULL;
    return expected && !memcmp(folded,expected,4);
}
static bool media_asset_decode(void *context,uint64_t key,const char *path,qa_cinematic_asset **out,qa_error *error)
{
    q3_scope *scope=context; q3_group *group=scope->inventory->groups+scope->group;
    const qa_cinematic_asset *asset=key?qa_media_library_record_at(group->source.movies,key-1):NULL;
    const qa_resource *resource=qa_cinematic_asset_resource(asset);
    qa_cinematic_source source=qa_cinematic_asset_source(asset);
    uint64_t pool=0,version=0;
    if (!out || *out || !asset || !resource || !path || !media_path_format(path,source.format) ||
        !resource_encode(scope,resource,&pool,&version,error) ||
        qa_resource_pool_find(qa_vfs_resources(group->source.mounts),qa_resource_id(resource))!=resource)
        return error && error->code!=QA_OK?false:
            frontend_fail(error,QA_ERROR_FORMAT,"Saved Q3 movie leaves its exact retained cache and content owner");
    /* Cache aliases retain the original winning resource, even when the
     * prepared request has another path. The restored Q3MS source rows own
     * those exact request/path aliases; resolving them again would reload. */
    qa_cinematic_asset_retain((qa_cinematic_asset *)asset); *out=(qa_cinematic_asset *)asset; return true;
}
static bool movie_target_encode(void *context,uint64_t target,qa_buffer *out,qa_error *error)
{
    q3_scope *scope=context;
    uint64_t actual=scope->target?scope->target:scope->inventory->groups[scope->group].source.identity;
    if (target!=actual || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q3 movie material target differs from its actual source bus");
    qa_source_save_io io={0}; uint64_t key=scope->group+1;
    bool ok=qa_source_save_writer(&io,NULL,error) && qa_source_save_u64(&io,&key) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
static bool movie_target_decode(void *context,qa_bytes bytes,uint64_t *target,qa_error *error)
{
    q3_scope *scope=context; qa_source_save_io io={0}; uint64_t key=0;
    bool ok=target && qa_source_save_reader(&io,NULL,bytes,error) && qa_source_save_u64(&io,&key) &&
        key==scope->group+1 && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!ok) return error && error->code!=QA_OK?false:
        frontend_fail(error,QA_ERROR_FORMAT,"Saved Q3 movie target is outside its genuine source bus owner");
    *target=scope->target?scope->target:scope->inventory->groups[scope->group].source.identity; return true;
}
static qa_q3_movie_checkpoint_refs movie_refs(q3_scope *scope)
{
    return (qa_q3_movie_checkpoint_refs){.context=scope,.asset_encode=media_asset_encode,.asset_decode=media_asset_decode,
        .playback={scope,movie_target_encode,movie_target_decode},.publication={scope,image_encode,image_decode}};
}
bool frontend_q3_component_movie_refs(frontend_q3_inventory *inventory,uint64_t identity,
    qa_q3_movie_checkpoint_refs *out,qa_error *error)
{
    if(!inventory || !identity || !out) return false;
    for(size_t i=0;i<inventory->group_count;++i) {
        q3_group *group=inventory->groups+i;
        if(group->kind!=Q3_OWNER_COMPONENT || group->source.identity!=identity) continue;
        frontend_component_scene_view row;
        if(!frontend_component_scene_metadata_read(inventory->frontend,group->ordinal,&row,error) ||
            row.identity!=identity || row.assets!=group->source.assets || row.media!=group->source.movies ||
            row.files!=group->source.mounts || row.images!=group->source.images) return false;
        q3_scope *scope=calloc(1,sizeof(*scope));
        if(!scope) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining genuine component movie dictionary scope");
        scope->inventory=inventory; scope->group=i; scope->target=identity;
        scope->next=inventory->movie_scopes; inventory->movie_scopes=scope;
        *out=movie_refs(scope); return true;
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Component movie cache is outside its genuine Q3 roster");
}
static bool module_system_source(void *context,const frontend_system_cinematic_identity *identity,
    frontend_system_cinematic_source *out,qa_error *error)
{
    q3_scope *scope=context;
    if(!identity || identity->service_owner!=scope->target || identity->audio_bus!=scope->target ||
        !scope->module_owner)
        return frontend_fail(error,QA_ERROR_FORMAT,"System cinematic leaves its actual role movie bus");
    return frontend_remote_q3_modules_cinematic_source_decode(scope->module_owner,identity,out,error);
}
static frontend_system_cinematic_refs module_system_refs(q3_scope *scope)
{
    return (frontend_system_cinematic_refs){.context=scope,.source_decode=module_system_source,
        .asset_encode=media_asset_encode,.asset_decode=media_asset_decode,
        .publication={scope,image_encode,image_decode}};
}
static bool module_system_encode(void *context,const qa_q3_system_movie *movie,uint32_t flags,
    qa_buffer *out,qa_error *error)
{
    q3_scope *scope=context; frontend_system_cinematic_refs refs=module_system_refs(scope);
    return frontend_system_cinematic_checkpoint(scope->inventory->frontend,movie,flags,&refs,out,error);
}
static bool module_system_decode(void *context,qa_bytes bytes,uint32_t flags,
    qa_q3_system_movie *out,qa_error *error)
{
    q3_scope *scope=context; frontend_system_cinematic_refs refs=module_system_refs(scope);
    return frontend_system_cinematic_restore(scope->inventory->frontend,&refs,flags,bytes,out,error);
}
static void module_system_discard(void *context,qa_q3_system_movie *movie)
{ (void)context; frontend_system_cinematic_discard(movie); }
static bool module_movie_refs(frontend_q3_inventory *inventory,
    const frontend_remote_q3_module_topology *role,bool cinematic,qa_q3_movie_checkpoint_refs *out,qa_error *error)
{
    if(!inventory || !role || !out || !role->service_owner || !role->assets || !role->movies ||
        !(cinematic?frontend_remote_q3_modules_cinematics_role_current(role):frontend_remote_q3_modules_role_current(role)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Module movie refs require the actual returned role and shared cache roster");
    frontend_remote_q3 *decoded=frontend_remote_q3_modules_parent(role->owner);
    frontend_remote_q3_initial *initial=frontend_remote_q3_modules_initial_parent(role->owner);
    for(size_t i=0;i<inventory->group_count;++i) {
        q3_group *group=inventory->groups+i;
        if(!parent_registry(group->kind) || group->source.assets!=role->assets ||
            group->source.movies!=role->movies || group->source.mounts!=role->mounts ||
            group->source.owner!=role->source.receiver.receiver || group->source.seat!=role->physical_seat) continue;
        if(group->kind==Q3_OWNER_REMOTE?
            decoded!=frontend_remote_q3_at(inventory->frontend,group->ordinal):
            !initial || frontend_remote_q3_initial_frontend(initial)!=inventory->frontend) continue;
        q3_scope *scope=calloc(1,sizeof(*scope));
        if(!scope) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining the real role movie-cache resolver");
        scope->inventory=inventory; scope->group=i; scope->target=role->service_owner;
        scope->module_owner=(frontend_remote_q3_modules *)role->owner;
        scope->next=inventory->movie_scopes; inventory->movie_scopes=scope;
        *out=movie_refs(scope); out->system_encode=module_system_encode;
        out->system_decode=module_system_decode; out->system_discard=module_system_discard; return true;
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Module movie refs leave their genuine parent cache and physical receiver");
}
bool frontend_q3_module_movie_refs(frontend_q3_inventory *inventory,
    const frontend_remote_q3_module_topology *role,qa_q3_movie_checkpoint_refs *out,qa_error *error)
{ return module_movie_refs(inventory,role,false,out,error); }
bool frontend_q3_module_cinematic_refs(frontend_q3_inventory *inventory,
    const frontend_remote_q3_module_topology *role,qa_q3_movie_checkpoint_refs *out,qa_error *error)
{ return module_movie_refs(inventory,role,true,out,error); }
static bool map_binding(const q3_scope *scope,const qa_scene_world **world,
    qa_collision_geometry **geometry,const qa_resource **map,qa_error *error)
{
    frontend_q3_inventory *inventory=scope->inventory; qa_frontend *f=inventory->frontend;
    q3_group *group=inventory->groups+scope->group;
    if(group->kind==Q3_OWNER_REGISTRY) {
        *world=group->source.parent_world; *geometry=group->source.parent_geometry;
        *map=*geometry?qa_collision_resource(*geometry):NULL; return true;
    }
    if(group->kind==Q3_OWNER_INITIAL) { *world=NULL; *geometry=NULL; *map=NULL; return true; }
    if(group->kind==Q3_OWNER_REMOTE) {
        frontend_remote_q3_resources source;
        if(!frontend_remote_q3_resources_read(frontend_remote_q3_at(f,group->ordinal),&source,error) ||
            source.identity!=group->source.identity || source.mounts!=group->source.mounts)
            return frontend_fail(error,QA_ERROR_FORMAT,"Remote Q3 map leaves its actual retained resource parent");
        *world=source.world; *geometry=source.geometry; *map=source.map; return true;
    }
    if (group->kind==Q3_OWNER_EFFECTS) {
        frontend_selected_effects_parent parent;
        if (!frontend_selected_effects_parent_read(f,group->ordinal,&parent,error)) return false;
        q3_owner_kind kind=parent.kind==FRONTEND_EFFECTS_PARENT_SOURCE?Q3_OWNER_SOURCE:Q3_OWNER_NATIVE;
        for (size_t i=0;i<inventory->group_count;++i)
            if (inventory->groups[i].kind==kind && inventory->groups[i].ordinal==parent.ordinal) {
                q3_scope actual={.inventory=inventory,.group=i};
                return map_binding(&actual,world,geometry,map,error);
            }
        return frontend_fail(error,QA_ERROR_FORMAT,"Selected effects map has no actual retained parent row");
    }
    if (group->kind==Q3_OWNER_SOURCE) {
        frontend_source_group_view source;
        if (!frontend_source_group_read(f,group->ordinal,&source) ||
            source.owner!=group->source.owner || source.identity!=group->source.identity ||
            source.mounts!=group->source.mounts)
            return frontend_fail(error,QA_ERROR_FORMAT,"Q3 map binding leaves its actual source group");
        if (source.private_map) {
            if ((!source.map_resource!=!source.geometry) || (!source.map_resource!=!source.world))
                return frontend_fail(error,QA_ERROR_FORMAT,"Private Q3 map has incomplete retained owners");
            *world=source.world; *geometry=source.geometry; *map=source.map_resource; return true;
        }
    }
    *world=f->scene_world; *geometry=qa_world_geometry(qa_application_world(f->application));
    *map=f->map_resource; return true;
}
static bool collision_encode(void *context,const qa_collision_geometry *geometry,uint64_t *key,qa_error *error)
{
    q3_scope *scope=context; const qa_scene_world *world=NULL; const qa_resource *map=NULL;
    qa_collision_geometry *actual=NULL;
    if (!map_binding(scope,&world,&actual,&map,error)) return false;
    if (!key || !actual || geometry!=actual)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q3 collision binding differs from its real map owner");
    *key=1; return true;
}
static bool collision_decode(void *context,uint64_t key,qa_collision_geometry **geometry,qa_error *error)
{
    q3_scope *scope=context; const qa_scene_world *world=NULL; const qa_resource *map=NULL;
    qa_collision_geometry *actual=NULL;
    if (!map_binding(scope,&world,&actual,&map,error)) return false;
    if (!geometry || key!=1 || !actual)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved Q3 collision reference lacks its actual map owner");
    *geometry=actual; return true;
}
static bool capture_binding(frontend_q3_inventory *inventory,q3_presentation *saved,qa_error *error)
{
    q3_group *group=inventory->groups+saved->group; qa_frontend *f=inventory->frontend;
    qa_q3_presentation_binding *binding=&group->binding; q3_scope scope={.inventory=inventory,.group=saved->group};
    const qa_scene_world *world=NULL; const qa_resource *map=NULL; qa_collision_geometry *geometry=NULL;
    if (!map_binding(&scope,&world,&geometry,&map,error)) return false;
    if (binding->frame && (binding->frame!=&f->frame ||
        !frontend_scene_frame_encode(inventory->refs.scene,binding->frame,&saved->frame,error))) return false;
    if (binding->world && (binding->world!=world ||
        !frontend_world_encode(inventory->refs.worlds,binding->world,&saved->world,error) ||
        !collision_encode(&scope,binding->geometry,&saved->collision,error))) return false;
    saved->entities=binding->entities.data!=NULL;
    if (binding->entities.size && !saved->entities) return false;
    if (!saved->entities) return !binding->entities.size && (!binding->world || binding->geometry!=NULL);
    if (!binding->world || !map) return false;
    qa_bytes source=qa_resource_bytes(map);
    uintptr_t base=(uintptr_t)source.data,span=(uintptr_t)binding->entities.data;
    if (!source.data || span<base || span-base>source.size || binding->entities.size>source.size-(span-base) ||
        !resource_encode(&scope,map,&saved->pool,&saved->resource,error)) return false;
    saved->offset=span-base; saved->length=binding->entities.size; return true;
}
static bool binding_fields(qa_source_save_io *io,q3_presentation *saved)
{
    return qa_source_save_u64(io,&saved->frame) && qa_source_save_u64(io,&saved->world) &&
        qa_source_save_u64(io,&saved->collision) && (!saved->world==!saved->collision) &&
        qa_source_save_bool(io,&saved->entities) && qa_source_save_u64(io,&saved->pool) &&
        qa_source_save_u64(io,&saved->resource) && qa_source_save_count(io,&saved->offset,SIZE_MAX) &&
        qa_source_save_count(io,&saved->length,SIZE_MAX) &&
        (saved->entities?(saved->world && saved->pool && saved->resource):
            (!saved->pool && !saved->resource && !saved->offset && !saved->length));
}
static bool attach_binding(frontend_q3_inventory *inventory,const q3_presentation *saved,qa_error *error)
{
    qa_frontend *f=inventory->frontend; q3_group *group=inventory->groups+saved->group;
    const qa_scene_frame *frame=NULL; qa_scene_world *world=NULL; qa_collision_geometry *collision=NULL;
    qa_bytes entities={0}; q3_scope scope={.inventory=inventory,.group=saved->group};
    const qa_scene_world *expected_world=NULL; const qa_resource *map=NULL; qa_collision_geometry *geometry=NULL;
    if (!map_binding(&scope,&expected_world,&geometry,&map,error)) return false;
    if ((saved->frame && (!frontend_scene_frame_decode(inventory->refs.scene,saved->frame,&frame,error) || frame!=&f->frame)) ||
        (saved->world && (!frontend_world_decode(inventory->refs.worlds,saved->world,&world,error) || world!=expected_world ||
            !collision_decode(&scope,saved->collision,&collision,error)))) return false;
    if (saved->entities) {
        const qa_resource *resource=NULL;
        if (!resource_decode(&scope,saved->pool,saved->resource,&resource,error) || resource!=map) return false;
        qa_bytes source=qa_resource_bytes(resource);
        if (!source.data || saved->offset>source.size || saved->length>source.size-saved->offset) return false;
        entities=(qa_bytes){source.data+saved->offset,saved->length};
    }
    return qa_q3_presentation_prepare_restored(group->source.presentation,(qa_scene_frame *)frame,world,collision,entities,error);
}
bool frontend_q3_registry_alias(frontend_q3_inventory *inventory,const qa_q3_presentation_assets *assets,
    uint64_t *key,bool *present,qa_error *error)
{
    if(!inventory || !assets || !key || !present) return false;
    for(size_t i=0;i<inventory->registry_count;++i) {
        q3_group *group=inventory->groups+inventory->registries[i].group;
        if(group->kind!=Q3_OWNER_REGISTRY && group->source.assets==assets) { *key=i+1; *present=true; return true; }
    }
    uint64_t encoded=0;
    qa_error absent={0};
    if(frontend_q3_assets_encode(inventory,assets,&encoded,&absent) && (encoded>>63)) {
        *key=encoded; *present=true; return true;
    }
    (void)error; *key=0; *present=false; return true;
}
bool frontend_q3_registry_services_key(frontend_q3_inventory *inventory,
    const qa_q3_presentation_asset_options *services,uint64_t *key,qa_error *error)
{
    if(!inventory || !services || !key) return false;
    if(!services->sounds && !services->movies && !services->context && !services->select &&
        !services->model_initialize && !services->print) { *key=0; return true; }
    for(size_t i=0;i<inventory->group_count;++i) {
        q3_group *group=inventory->groups+i;
        const qa_q3_presentation_asset_options *actual=&group->services;
        if(group->kind!=Q3_OWNER_REGISTRY && actual->sounds==services->sounds && actual->movies==services->movies &&
            actual->context==services->context && actual->select==services->select &&
            actual->model_initialize==services->model_initialize && actual->print==services->print) { *key=i+1; return true; }
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Retained registry borrowed services have no actual returned parent");
}
bool frontend_q3_registry_services_read(frontend_q3_inventory *inventory,uint64_t key,
    qa_q3_presentation_asset_options *out,qa_error *error)
{
    if(!inventory || !out || key>inventory->group_count ||
        (key && inventory->groups[key-1].kind==Q3_OWNER_REGISTRY))
        return frontend_fail(error,QA_ERROR_FORMAT,"Retained registry service key has no genuine current owner");
    *out=key?inventory->groups[key-1].services:(qa_q3_presentation_asset_options){0};
    out->zero_sound=NULL; return true;
}
static bool services_encode(void *context,const qa_q3_presentation_asset_options *services,uint64_t *key,qa_error *error)
{
    q3_scope *scope=context; q3_group *group=scope->inventory->groups+scope->group;
    if (!key || services->provider.mounts!=group->services.provider.mounts ||
        services->provider.images!=group->services.provider.images || services->provider.materials!=group->services.provider.materials ||
        services->provider.family!=group->services.provider.family || services->sounds!=group->services.sounds ||
        services->movies!=group->services.movies || services->zero_sound!=group->services.zero_sound ||
        services->context!=group->services.context || services->select!=group->services.select || services->print!=group->services.print ||
        services->model_initialize!=group->services.model_initialize)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q3 asset services differ from their genuine preobserved source owner");
    *key=scope->group+1; return true;
}
static bool services_qualify(void *context,uint64_t key,const qa_q3_presentation_asset_options *services,qa_error *error)
{
    uint64_t actual=0; q3_scope *scope=context;
    return key==scope->group+1 && services_encode(context,services,&actual,error) && key==actual;
}
static bool provider_encode(void *context,const qa_q3_presentation_provider *provider,uint64_t *key,qa_error *error)
{ return frontend_material_provider_encode(((q3_scope *)context)->inventory->frontend,provider,key,error); }
static bool provider_decode(void *context,uint64_t key,qa_q3_presentation_provider *provider,qa_error *error)
{ return frontend_material_provider_decode(((q3_scope *)context)->inventory->frontend,key,provider,error); }
static bool model_encode(void *context,const qa_model *model,uint64_t *key,qa_error *error)
{ return frontend_model_encode(((q3_scope *)context)->inventory->refs.models,model,key,error); }
static bool model_decode(void *context,uint64_t key,const qa_resource *resource,const qa_model **model,qa_error *error)
{
    frontend_model_inventory *inventory=((q3_scope *)context)->inventory->refs.models;
    frontend_model_source source;
    if (key>SIZE_MAX || !frontend_model_source_at(inventory,(size_t)key,&source) || source.resource!=resource)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q3 model refers to another canonical content resource");
    return frontend_model_decode(inventory,key,model,error);
}
static bool model_retain(void *context,const qa_model *model,qa_q3_asset_model_lease *lease,qa_error *error)
{
    q3_scope *scope=context; frontend_scene_model_scope model_scope={{scope->inventory->refs.scene,0},scope->inventory->refs.models};
    return frontend_scene_q3_model_retain(&model_scope,model,lease,error);
}
static bool scene_encode(void *context,const qa_scene_model *model,uint64_t *key,qa_error *error)
{ return frontend_scene_root_encode(((q3_scope *)context)->inventory->refs.worlds,model,key,error); }
static bool scene_decode(void *context,uint64_t key,qa_scene_model **model,qa_error *error)
{ return frontend_scene_root_decode(((q3_scope *)context)->inventory->refs.worlds,key,model,error); }
static bool world_encode(void *context,const qa_scene_world *world,uint64_t *key,qa_error *error)
{ return frontend_world_encode(((q3_scope *)context)->inventory->refs.worlds,world,key,error); }
static bool world_decode(void *context,uint64_t key,qa_scene_world **world,qa_error *error)
{ return frontend_world_decode(((q3_scope *)context)->inventory->refs.worlds,key,world,error); }
static bool asset_material_encode(void *context,const qa_material *material,uint64_t *key,qa_error *error)
{ return frontend_scene_material_encode(((q3_scope *)context)->inventory->refs.scene,material,key,error); }
static bool asset_material_decode(void *context,uint64_t key,const qa_material **material,qa_error *error)
{ return frontend_scene_material_decode(((q3_scope *)context)->inventory->refs.scene,key,material,error); }
static bool audio_encode(void *context,const qa_audio_asset *asset,uint64_t *key,qa_error *error)
{
    q3_scope *scope=context;
    return qa_audio_asset_inventory_index(scope->inventory->refs.audio,asset,key) ||
        frontend_fail(error,QA_ERROR_FORMAT,"Q3 audio reference is outside its actual asset inventory");
}
static bool audio_decode(void *context,uint64_t key,qa_audio_asset **asset,qa_error *error)
{
    q3_scope *scope=context; qa_audio_asset *actual=qa_audio_asset_inventory_at(scope->inventory->refs.audio,key);
    if (!asset || !actual) return frontend_fail(error,QA_ERROR_FORMAT,"Saved Q3 audio has no genuine imported asset");
    *asset=actual; return true;
}
static bool scene_owned_ready(void *context,uint64_t key,size_t model_row,qa_error *error)
{
    q3_scope *scope=context; frontend_scene_root_view view;
    if (!key || key-1>SIZE_MAX || !frontend_world_inventory_model_at(scope->inventory->refs.worlds,(size_t)(key-1),&view) ||
        view.owner.row!=model_row+1)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q3 model row differs from its genuine scene destructor edge");
    const q3_group *group=scope->inventory->groups+scope->group;
    return frontend_scene_root_owner_ready(scope->inventory->refs.worlds,key,
        scene_owner(group->kind),group->ordinal+1,error);
}
static bool world_owned_ready(void *context,uint64_t key,size_t model_row,qa_error *error)
{
    q3_scope *scope=context; frontend_world_source source; frontend_scene_owner owner;
    if (!key || key-1>SIZE_MAX || !frontend_world_inventory_world_at(scope->inventory->refs.worlds,(size_t)(key-1),&source,&owner) ||
        owner.row!=model_row+1)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q3 preview row differs from its genuine world destructor edge");
    const q3_group *group=scope->inventory->groups+scope->group;
    return frontend_world_owner_ready(scope->inventory->refs.worlds,key,
        scene_owner(group->kind),group->ordinal+1,error);
}
static void scene_adopt(void *context,uint64_t key)
{ frontend_scene_root_adopt(((q3_scope *)context)->inventory->refs.worlds,key); }
static void world_adopt(void *context,uint64_t key)
{ frontend_world_adopt(((q3_scope *)context)->inventory->refs.worlds,key); }
static bool registry_encode(void *context,const qa_q3_presentation_assets *assets,uint64_t *key,qa_error *error)
{ return frontend_q3_assets_encode(((q3_scope *)context)->inventory,assets,key,error); }
static bool registry_decode(void *context,uint64_t key,qa_q3_presentation_assets **assets,qa_error *error)
{ return frontend_q3_assets_decode(((q3_scope *)context)->inventory,key,assets,error); }
static qa_q3_asset_owner_refs asset_refs(q3_scope *scope)
{
    return (qa_q3_asset_owner_refs){.context=scope,.services_encode=services_encode,.services_qualify=services_qualify,
        .provider_encode=provider_encode,.provider_decode=provider_decode,.resource_encode=resource_encode,.resource_decode=resource_decode,
        .model_encode=model_encode,.model_decode=model_decode,.model_retain=model_retain,.scene_encode=scene_encode,.scene_decode=scene_decode,
        .world_encode=world_encode,.world_decode=world_decode,.collision_encode=collision_encode,.collision_decode=collision_decode,
        .material_encode=asset_material_encode,.material_decode=asset_material_decode,.audio_encode=audio_encode,.audio_decode=audio_decode,
        .scene_owned_ready=scene_owned_ready,.world_owned_ready=world_owned_ready,.scene_adopt=scene_adopt,.world_adopt=world_adopt,
        .registry_encode=registry_encode,.registry_decode=registry_decode};
}
bool frontend_q3_checkpoint(qa_frontend *f,const frontend_q3_refs *refs,qa_buffer *out,qa_error *error)
{
    if (!out || out->data || out->size) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 checkpoint requires empty output");
    frontend_q3_inventory *inventory=NULL; qa_source_save_io io={0};
    bool ok=collect(f,refs,false,false,&inventory,error) && qa_source_save_writer(&io,qa_application_session(f->application),error) && header(&io,inventory);
    for (size_t i=0;ok && i<inventory->group_count;++i) ok=metadata(&io,inventory,i);
    for (size_t i=0;ok && i<inventory->group_count;++i) if (private_owner(inventory->groups[i].kind)) {
        qa_buffer private={0};
        q3_group *group=inventory->groups+i;
        q3_scope scope={.inventory=inventory,.group=i}; q3n_selected_media_refs selected=selected_refs(&scope);
        ok=(group->kind==Q3_OWNER_SELECTED?frontend_equipment_q3_checkpoint(f,group->ordinal,&private,error):
            group->kind==Q3_OWNER_CHARACTER?frontend_selected_character_checkpoint(f,group->ordinal,&private,error):
            group->kind==Q3_OWNER_GEAR?frontend_equipment_gear_checkpoint(f,group->ordinal,&selected,&private,error):
            frontend_selected_effects_checkpoint(f,group->ordinal,&private,error)) && write_blob(&io,&private);
        qa_buffer_free(&private);
    }
    for (size_t i=0;ok && i<inventory->media_count;++i) {
        q3_media *media=inventory->media+i; q3_scope scope={.inventory=inventory,.group=media->group,.media=media};
        qa_media_library_checkpoint_refs library=library_refs(&scope); qa_buffer state={0};
        ok=media_resources(&io,inventory,media) && qa_media_library_checkpoint(inventory->groups[media->group].source.movies,&library,&state,error) && write_blob(&io,&state);
        qa_buffer_free(&state);
    }
    for (size_t i=0;ok && i<inventory->registry_count;++i) {
        q3_scope scope={.inventory=inventory,.group=inventory->registries[i].group}; qa_q3_asset_owner_refs assets=asset_refs(&scope); qa_buffer state={0};
        ok=qa_q3_assets_owner_checkpoint(inventory->groups[scope.group].source.assets,qa_application_session(f->application),&assets,&state,error) && write_blob(&io,&state);
        qa_buffer_free(&state);
    }
    for (size_t i=0;ok && i<inventory->presentation_count;++i) {
        q3_presentation *saved=inventory->presentations+i; q3_scope scope={.inventory=inventory,.group=saved->group};
        qa_q3_movie_checkpoint_refs movies=movie_refs(&scope); qa_buffer scene={0},media={0};
        qa_q3_presentation *presentation=inventory->groups[saved->group].source.presentation;
        ok=capture_binding(inventory,saved,error) && binding_fields(&io,saved) &&
            qa_q3_presentation_scene_checkpoint(presentation,&scene,error) &&
            qa_q3_presentation_media_checkpoint(presentation,&movies,&media,error) && write_blob(&io,&scene) && write_blob(&io,&media);
        qa_buffer_free(&scene); qa_buffer_free(&media);
    }
    ok=ok && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); frontend_q3_inventory_destroy(inventory);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Q3 component inventory has invalid actual owner edges");
    return ok;
}
bool frontend_q3_prepare(qa_frontend *f,const frontend_q3_refs *refs,qa_bytes bytes,frontend_q3_inventory **out,qa_error *error)
{
    if (!out || *out) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 preparation requires empty inventory output");
    frontend_q3_inventory *inventory=NULL; qa_source_save_io io={0};
    bool ok=collect(f,refs,true,false,&inventory,error) && qa_source_save_reader(&io,qa_application_session(f->application),bytes,error) && header(&io,inventory);
    for (size_t i=0;ok && i<inventory->group_count;++i) ok=metadata(&io,inventory,i);
    for (size_t i=0;ok && i<inventory->group_count;++i) if (private_owner(inventory->groups[i].kind))
        ok=read_blob(&io,&inventory->groups[i].selected);
    for (size_t i=0;ok && i<inventory->media_count;++i)
        ok=media_resources(&io,inventory,inventory->media+i) && read_blob(&io,&inventory->media[i].state);
    for (size_t i=0;ok && i<inventory->registry_count;++i) ok=read_blob(&io,&inventory->registries[i].state);
    for (size_t i=0;ok && i<inventory->presentation_count;++i)
        ok=binding_fields(&io,inventory->presentations+i) && read_blob(&io,&inventory->presentations[i].scene) && read_blob(&io,&inventory->presentations[i].media);
    ok=ok && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    for (size_t i=0;ok && i<inventory->media_count;++i)
        if (qa_media_library_record_count(inventory->groups[inventory->media[i].group].source.movies))
            ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 media import requires every genuine cache owner to be empty");
    for (size_t i=0;ok && i<inventory->media_count;++i) {
        q3_media *media=inventory->media+i; q3_scope scope={.inventory=inventory,.group=media->group,.media=media}; qa_media_library_checkpoint_refs library=library_refs(&scope);
        qa_media_library *movies=inventory->groups[media->group].source.movies;
        ok=qa_media_library_restore(movies,media->state,&library,error) && qa_media_library_record_count(movies)==media->count;
        for (size_t j=0;ok && j<media->count;++j)
            ok=qa_cinematic_asset_resource(qa_media_library_record_at(movies,j))==media->resources[j];
    }
    if (!ok) {
        frontend_q3_inventory_destroy(inventory);
        if (error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Saved Q3 envelope leaves its genuine prepared source topology");
        return false;
    }
    inventory->prepared=true; *out=inventory; return true;
}
static bool registry_restore_order(frontend_q3_inventory *inventory,size_t **out,qa_error *error)
{
    size_t count=inventory->registry_count;
    if(count>SIZE_MAX/sizeof(size_t)) return frontend_fail(error,QA_ERROR_MEMORY,"Registry parent order exceeds its real roster");
    size_t *order=count?malloc(count*sizeof(*order)):NULL;
    size_t *parents=count?malloc(count*sizeof(*parents)):NULL;
    bool *completed=count?calloc(count,sizeof(*completed)):NULL;
    bool ok=!count || (order && parents && completed);
    if(!ok) frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual registry parent dependencies");
    for(size_t i=0;ok && i<count;++i) {
        uint64_t key=0; qa_q3_presentation_assets *parent=NULL;
        parents[i]=SIZE_MAX;
        ok=qa_q3_assets_owner_parent_key(inventory->registries[i].state,&key,error);
        if(ok && key) ok=frontend_q3_assets_decode(inventory,key,&parent,error) && parent;
        if(!ok || !key) continue;
        for(size_t j=0;j<count;++j)
            if(inventory->groups[inventory->registries[j].group].source.assets==parent) {
                parents[i]=j; break;
            }
        if(parents[i]==i) ok=frontend_fail(error,QA_ERROR_FORMAT,"Saved registry names itself as its retained parent");
    }
    for(size_t i=0;ok && i<count;++i) {
        size_t next=0;
        while(next<count && (completed[next] || (parents[next]!=SIZE_MAX && !completed[parents[next]]))) ++next;
        if(next==count) { ok=frontend_fail(error,QA_ERROR_FORMAT,"Saved registry parent graph contains a cycle"); break; }
        completed[next]=true; order[i]=next;
    }
    free(parents); free(completed);
    if(!ok) { free(order); return false; }
    *out=order; return true;
}
bool frontend_q3_restore(frontend_q3_inventory *inventory,double wall_milliseconds,qa_error *error)
{
    if (!inventory || !inventory->prepared || inventory->importing || inventory->restored || !isfinite(wall_milliseconds) ||
        !phase(inventory->frontend,&inventory->refs,true,error))
        return error && error->code!=QA_OK?false:frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 late import requires its untouched prepared candidate");
    size_t *registry_order=NULL;
    if(!registry_restore_order(inventory,&registry_order,error)) return false;
    inventory->importing=true; bool ok=true; qa_frontend *f=inventory->frontend;
    for (size_t i=0;ok && i<inventory->presentation_count;++i) ok=attach_binding(inventory,inventory->presentations+i,error);
    for (size_t i=0;ok && i<inventory->registry_count;++i) {
        q3_registry *registry=inventory->registries+registry_order[i]; q3_scope scope={.inventory=inventory,.group=registry->group}; qa_q3_asset_owner_refs assets=asset_refs(&scope);
        ok=qa_q3_assets_owner_restore(inventory->groups[registry->group].source.assets,qa_application_session(f->application),&assets,registry->state,error);
    }
    free(registry_order);
    for (size_t i=0;ok && i<inventory->group_count;++i) if (assets_only(inventory->groups[i].kind)) {
        q3_group *group=inventory->groups+i;
        ok=qa_q3_assets_capture_begin(group->source.assets,error);
        if (ok) {
            q3_scope scope={.inventory=inventory,.group=i}; q3n_selected_media_refs selected=selected_refs(&scope);
            ok=group->kind==Q3_OWNER_SELECTED?frontend_equipment_q3_restore(f,group->ordinal,group->selected,error):
                group->kind==Q3_OWNER_GEAR?frontend_equipment_gear_restore(f,group->ordinal,&selected,group->selected,error):
                frontend_selected_character_restore(f,group->ordinal,group->selected,error);
            qa_q3_assets_capture_end(group->source.assets);
        }
    }
    for (size_t i=0;ok && i<inventory->presentation_count;++i) {
        q3_presentation *saved=inventory->presentations+i; q3_scope scope={.inventory=inventory,.group=saved->group}; qa_q3_movie_checkpoint_refs movies=movie_refs(&scope);
        q3_group *group=inventory->groups+saved->group;
        ok=qa_q3_presentation_scene_restore(group->source.presentation,saved->scene,error) &&
            qa_q3_presentation_media_restore(group->source.presentation,&movies,group->source.identity,wall_milliseconds,saved->media,error) &&
            qa_q3_presentation_binding_read(group->source.presentation,&group->binding,error);
    }
    /* Effects cull rows and pools refer to the actual restored collecting
     * packet, so both its registry and backend state precede private import. */
    for (size_t i=0;ok && i<inventory->group_count;++i) if (inventory->groups[i].kind==Q3_OWNER_EFFECTS) {
        q3_group *group=inventory->groups+i;
        ok=qa_q3_assets_capture_begin(group->source.assets,error);
        if (ok) {
            ok=frontend_selected_effects_restore(f,group->ordinal,group->selected,error);
            qa_q3_assets_capture_end(group->source.assets);
        }
    }
    /* A partial import has real adopted children. It is retired as one isolated
     * candidate, never retried through this operation or replayed. */
    inventory->restored=ok; return ok;
}
