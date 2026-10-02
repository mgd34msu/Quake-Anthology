#include "bots_save_private.h"
#include "map_players_private.h"
#include "guest_q3_private.h"
#include "qa/bot_runtime_save.h"
#include "qa/bots_population_save.h"
#include "qa/navigation_graph_save.h"
#include "qa/persistence_navigation.h"
#include "qa/persistence_fields.h"
#include "../../bots/save_fields.h"
#include "bot_world.h"
#include "bots_transport.h"
#include "bots_catalog.h"
#include "bots_guests.h"
#include "bots_knowledge.h"
#include <limits.h>

typedef struct bot_app_record {
    qa_bytes requirements,runtime,population,shared_world,transport,catalogue;
} bot_app_record;

static const uint8_t bots_magic[8]={'Q','A','B','A','P','P',0,0};
static const uint8_t nav_magic[8]={'Q','A','N','A','P','P',0,0};

static bool app_signature(qa_source_save_io *io) {
    uint8_t magic[8];memcpy(magic,bots_magic,sizeof(magic));uint32_t version=9;
    return qa_source_save_bytes(io,magic,sizeof(magic)) && qa_source_save_u32(io,&version) &&
        (!memcmp(magic,bots_magic,sizeof(magic)) && version==9?true:
            bot_save_fail(io,QA_ERROR_FORMAT,"Unsupported application bot continuation schema"));
}

static bool section(qa_source_save_io *io,qa_bytes *value) {
    size_t size=value->size;
    if(!qa_source_save_count(io,&size,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(size>io->input.size-io->offset) return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated application bot section");
        *value=(qa_bytes){io->input.data+io->offset,size};io->offset+=size;return true;
    }
    return qa_source_save_bytes(io,(void *)value->data,size);
}
static const qa_launch_snapshot *launch(qa_application *app) {
    return app->routing_snapshot?app->routing_snapshot:qa_application_launch(app);
}
static application_provider *provider(qa_application *app,qa_actor_owner owner) {
    application_provider **table=app->routing_providers?app->routing_providers:app->providers;
    size_t count=app->routing_providers?app->routing_provider_count:app->provider_count;
    for(size_t i=0;i<count;++i) if(table[i] && table[i]->owner==owner) return table[i];
    return NULL;
}
static bool provider_field(qa_source_save_io *io,qa_application *app,application_provider **value,bool required) {
    qa_actor_owner owner=*value?(*value)->owner:0;
    if(!qa_source_save_string(io,&owner)) return false;
    application_provider *actual=owner?provider(app,owner):NULL;
    if((owner && !actual) || (required && !actual)) return bot_save_fail(io,QA_ERROR_FORMAT,"Application bot provider identity is absent");
    if(io->direction==QA_SOURCE_SAVE_READ) *value=actual;
    else if(actual!=*value) return bot_save_fail(io,QA_ERROR_FORMAT,"Application bot provider is outside selected inventory");
    return true;
}
static bool knowledge_field(qa_source_save_io *io,application_bots *bots) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    application_bot_knowledge_owner *owner=bots->knowledge_owner;
    size_t count=reading?0:owner?owner->count:0;
    if(!qa_source_save_count(io,&count,SIZE_MAX/sizeof(application_bot_knowledge_actor))) return false;
    if(reading && count) {
        if(count>(io->input.size-io->offset)/17)
            return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated weapon-handle knowledge map");
        owner=calloc(1,sizeof(*owner));
        if(!owner) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring actual weapon-handle knowledge owner");
        bots->knowledge_owner=owner;
        owner->actors=calloc(count,sizeof(*owner->actors));
        if(!owner->actors) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring retained weapon-handle actor map");
        owner->count=owner->capacity=count;
    }
    if(count && (!owner || !owner->actors || owner->count>owner->capacity))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Weapon-handle knowledge map has no actual allocation");
    for(size_t i=0;i<count;++i) {
        application_bot_knowledge_actor *entry=owner->actors+i;
        if(!qa_source_save_u32(io,&entry->handle) || entry->handle>INT32_MAX ||
           !qa_source_save_actor(io,&entry->actor) || !entry->actor.registry ||
           !provider_field(io,bots->application,&entry->provider,true) ||
           (entry->provider->kind!=APPLICATION_PROVIDER_Q1 && entry->provider->kind!=APPLICATION_PROVIDER_Q2 &&
            entry->provider->kind!=APPLICATION_PROVIDER_Q3))
            return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid actual selected-arsenal knowledge identity");
        for(size_t j=0;j<i;++j) if(owner->actors[j].handle==entry->handle)
            return bot_save_fail(io,QA_ERROR_FORMAT,"Duplicate retained weapon-handle knowledge key");
    }
    return true;
}
/* Bot file views are constructor clones with read-only script consumers. Their
 * origin selects the complete pinned policy; numeric mount IDs are remapped
 * by real search-order ordinal, and archive content must remain identical. */
static bool files_field(qa_source_save_io *io,application_bots *bots) {
    qa_application *app=bots->application;
    if(!qa_source_save_bool(io,&bots->files_launch) || !qa_source_save_string(io,&bots->files_product) ||
        (bots->files_launch && bots->files_product) || (!bots->files_launch && !bots->files_product))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid application bot file origin");
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(bots->files_launch) bots->files=qa_vfs_clone(qa_launch_snapshot_mounts(launch(app)),io->error);
        else {
            const char *id=qa_strings_cstr(qa_session_strings(app->session),bots->files_product);
            const qa_product *product=NULL;
            for(size_t i=0;id && i<qa_catalog_count(app->catalog);++i) {
                const qa_product *candidate=qa_catalog_at(app->catalog,i);
                if(!strcmp(candidate->key,id)) {product=candidate;break;}
            }
            if(!product || product->family!=QA_GAME_Q3 || product->availability!=QA_CONTENT_INSTALLED)
                return bot_save_fail(io,QA_ERROR_FORMAT,"Saved bot content product is unavailable");
            if(!qa_catalog_open(app->catalog,product->id,&bots->files,io->error)) return false;
        }
        if(!bots->files) return false;
    }
    size_t actual=qa_vfs_mount_count(bots->files),count=actual;
    if(!qa_source_save_count(io,&count,actual) || count!=actual) return bot_save_fail(io,QA_ERROR_FORMAT,"Bot file mount inventory differs");
    if(io->direction==QA_SOURCE_SAVE_READ && count) {
        bots->saved_file_references=calloc(count,sizeof(*bots->saved_file_references));
        if(!bots->saved_file_references) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring bot file reference accounting");
        bots->saved_file_reference_count=count;
    }
    for(size_t i=0;i<count;++i) {
        qa_vfs_mount_info info;
        if(!qa_vfs_mount_at(bots->files,i,&info)) return bot_save_fail(io,QA_ERROR_FORMAT,"Bot file mount is absent");
        bool archive=info.is_archive,writable=info.writable,overlay=info.user_overlay,referenced=info.referenced;
        uint32_t format=info.format,comparison=info.comparison;
        qa_sha256_digest digest={0};if(info.digest) digest=*info.digest;
        if(!qa_source_save_bool(io,&archive) || !qa_source_save_u32(io,&format) ||
            !qa_source_save_u32(io,&comparison) || !qa_source_save_bool(io,&writable) ||
            !qa_source_save_bool(io,&overlay) || !qa_source_save_bool(io,&referenced) ||
            !qa_source_save_bytes(io,digest.bytes,sizeof(digest.bytes)) ||
            archive!=info.is_archive || format!=(uint32_t)info.format || comparison!=(uint32_t)info.comparison ||
            writable!=info.writable || overlay!=info.user_overlay ||
            (info.digest? !qa_sha256_equal(&digest,info.digest):memcmp(digest.bytes,(uint8_t[32]){0},32)))
            return bot_save_fail(io,QA_ERROR_FORMAT,"Bot file mount policy or content differs");
        if(io->direction==QA_SOURCE_SAVE_READ) bots->saved_file_references[i]=referenced;
    }
    actual=qa_vfs_prefix_count(bots->files);count=actual;
    if(!qa_source_save_count(io,&count,actual) || count!=actual) return bot_save_fail(io,QA_ERROR_FORMAT,"Bot file prefix inventory differs");
    for(size_t i=0;i<count;++i) {
        const char *prefix;const qa_mount_id *order;size_t extent;
        if(!qa_vfs_prefix_at(bots->files,i,&prefix,&order,&extent)) return false;
        const char *saved=io->direction==QA_SOURCE_SAVE_WRITE?prefix:NULL;
        bool ok=bot_save_text(io,&saved) && saved && !strcmp(saved,prefix);
        if(io->direction==QA_SOURCE_SAVE_READ) free((void *)saved);
        size_t saved_extent=extent;
        if(!ok || !qa_source_save_count(io,&saved_extent,extent) || saved_extent!=extent)
            return bot_save_fail(io,QA_ERROR_FORMAT,"Bot file prefix policy differs");
        for(size_t j=0;j<extent;++j) {
            size_t ordinal=0;qa_vfs_mount_info info;
            while(ordinal<qa_vfs_mount_count(bots->files) &&
                (!qa_vfs_mount_at(bots->files,ordinal,&info) || info.id!=order[j])) ++ordinal;
            size_t expected=ordinal;
            if(ordinal==qa_vfs_mount_count(bots->files) || !qa_source_save_count(io,&ordinal,expected) || ordinal!=expected)
                return bot_save_fail(io,QA_ERROR_FORMAT,"Bot file prefix mount order differs");
        }
    }
    return true;
}
static bool map_field(qa_source_save_io *io,application_bots *bots) {
    qa_application *app=bots->application;
    qa_string_id name=app->current_map;
    const char *path=io->direction==QA_SOURCE_SAVE_WRITE?qa_resource_path(bots->map_resource):NULL;
    qa_sha256_digest digest={0};
    if(io->direction==QA_SOURCE_SAVE_WRITE) digest=*qa_resource_digest(bots->map_resource);
    bool ok=qa_source_save_string(io,&name) && name==app->current_map && bot_save_text(io,&path) && path &&
        qa_source_save_bytes(io,digest.bytes,32) && app->map_resource &&
        !strcmp(path,qa_resource_path(app->map_resource)) && qa_sha256_equal(&digest,qa_resource_digest(app->map_resource));
    if(io->direction==QA_SOURCE_SAVE_READ) free((void *)path);
    if(!ok) return bot_save_fail(io,QA_ERROR_FORMAT,"Application bot pinned map identity differs");
    if(io->direction==QA_SOURCE_SAVE_READ) {
        bots->map_resource=app->map_resource;qa_resource_retain(bots->map_resource);
        if(!qa_bsp_open(qa_resource_bytes(bots->map_resource),&bots->geometry,io->error) ||
            !qa_entities_parse(bots->geometry.lumps[QA_BSP_ENTITIES].bytes,
                bots->geometry.family==QA_BSP_Q3?QA_ENTITY_Q3:QA_ENTITY_Q1,&bots->entities,io->error)) return false;
    }
    return true;
}
static bool snapshot_field(qa_source_save_io *io,application_bots *bots) {
    qa_builtin_actor_snapshot *s=&bots->pickup_snapshot;
    size_t maximum=qa_actors_capacity(qa_session_actors(bots->application->session));
    if(!qa_source_save_count(io,&s->capacity,maximum) || !qa_source_save_count(io,&s->count,s->capacity)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ && s->capacity) {
        if(s->count>(io->input.size-io->offset)/13 || s->capacity>SIZE_MAX/(2*sizeof(*s->ids)))
            return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated application pickup snapshot");
        s->ids=calloc(s->capacity,2*sizeof(*s->ids));
        if(!s->ids) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring application pickup snapshot");
        s->sort=s->ids+s->capacity;
    }
    if(s->count && !s->ids) return bot_save_fail(io,QA_ERROR_FORMAT,"Application pickup snapshot has no storage");
    if(io->direction==QA_SOURCE_SAVE_WRITE && s->capacity &&
        (!s->ids || s->sort!=s->ids+s->capacity))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Application pickup snapshot allocation differs");
    for(size_t i=0;i<s->count;++i) if(!qa_source_save_actor(io,s->ids+i)) return false;
    if(!qa_source_save_count(io,&bots->train_capacity,maximum)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ && bots->train_capacity) {
        if(bots->train_capacity>SIZE_MAX/sizeof(*bots->train_stops)) return false;
        bots->train_stops=calloc(bots->train_capacity,sizeof(*bots->train_stops));
        if(!bots->train_stops) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring application mover scratch");
    }
    return true;
}
static bool fields(qa_source_save_io *io,application_bots *bots,bot_app_record *record) {
    qa_application *app=bots->application;
    if(!provider_field(io,app,&bots->source,true) || !map_field(io,bots) || !files_field(io,bots) || !qa_source_save_u32(io,&bots->capacity) ||
        bots->capacity>INT32_MAX || bots->capacity>SIZE_MAX/sizeof(*bots->seats)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ && bots->capacity) {
        if(bots->capacity>(io->input.size-io->offset)/14) return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated application bot seats");
        bots->seats=calloc(bots->capacity,sizeof(*bots->seats));
        if(!bots->seats) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring application bot seats");
    }
    for(uint32_t i=0;i<bots->capacity;++i) {
        application_bot_seat *s=bots->seats+i;
        if(!qa_source_save_actor(io,&s->actor) || !qa_source_save_u32(io,&s->seat) ||
            !qa_source_save_u32(io,&s->library_client) || !qa_source_save_bool(io,&s->retired)) return false;
    }
    size_t count=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE) for(application_bot_guest *g=bots->guests;g;g=g->next) ++count;
    size_t maximum=qa_launch_snapshot_instance_count(launch(app));
    if(!qa_source_save_count(io,&count,maximum)) return false;
    application_bot_guest **tail=&bots->guests,*guest=bots->guests;
    for(size_t i=0;i<count;++i) {
        if(io->direction==QA_SOURCE_SAVE_READ) {
            guest=calloc(1,sizeof(*guest));
            if(!guest) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring application guest bot namespace");
            *tail=guest;tail=&guest->next;
        }
        if(!provider_field(io,app,&guest->provider,true) || !qa_source_save_u32(io,&guest->client_base) ||
            !qa_source_save_u32(io,&guest->entity_base) || guest->client_base>INT32_MAX-64u ||
            guest->entity_base>INT32_MAX-1024u || !application_bots_guest_fields(io,guest)) return false;
        for(application_bot_guest *previous=bots->guests;previous!=guest;previous=previous->next)
            if(previous->provider==guest->provider ||
                (guest->client_base<previous->client_base+64 && previous->client_base<guest->client_base+64) ||
                (guest->entity_base<previous->entity_base+1024 && previous->entity_base<guest->entity_base+1024))
                return bot_save_fail(io,QA_ERROR_FORMAT,"Duplicate application guest bot namespace");
        guest=guest->next;
    }
    bool shared=io->direction==QA_SOURCE_SAVE_WRITE?bots->shared_world!=NULL:false;
    bool catalogue=io->direction==QA_SOURCE_SAVE_WRITE?bots->catalogue!=NULL:false;
    bool native_shared=bots->source->kind==APPLICATION_PROVIDER_Q1 || bots->source->kind==APPLICATION_PROVIDER_Q2;
    return qa_source_save_bool(io,&shared) && (!shared || native_shared) &&
        qa_source_save_bool(io,&catalogue) && qa_source_save_bool(io,&bots->catalogue_ready) &&
        (!bots->catalogue_ready || catalogue) && (!catalogue || shared || bots->source->kind==APPLICATION_PROVIDER_Q3) &&
        snapshot_field(io,bots) && knowledge_field(io,bots) && section(io,&record->requirements) && record->requirements.size &&
        section(io,&record->runtime) && record->runtime.size && section(io,&record->population) &&
        section(io,&record->shared_world) && section(io,&record->transport) &&
        section(io,&record->catalogue) && (record->catalogue.size!=0)==catalogue &&
        (record->shared_world.size!=0)==shared && (record->transport.size!=0)==shared;
}
bool application_bots_save_capture(qa_application *app,qa_buffer *out,qa_error *error) {
    if(!app || !out || !application_bots_can_destroy(app) || (app->bots &&
       (app->bots->restoring || app->bots->round_phase!=APPLICATION_BOT_ROUND_ACTIVE ||
        app->bots->round || app->bots->original || app->bots->producing)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Application bots are borrowed or restoring");
    qa_buffer requirements={0},runtime={0},population={0},shared_world={0},transport={0},catalogue={0};application_bots *bots=app->bots;
    bool present=bots!=NULL,ok=true;
    if(present) ok=qa_bots_save_requirements_capture(bots->runtime,bots->population,&requirements,error) &&
        qa_bot_runtime_save_capture(app->session,bots->runtime,&runtime,error) &&
        (!bots->population || qa_bots_population_capture(bots->population,&population,error));
    if(ok && present && bots->shared_world) {
        qa_source_save_io transport_io={0};
        ok=application_bot_world_capture(bots->shared_world,&shared_world,error) &&
            qa_source_save_writer(&transport_io,app->session,error) &&
            application_bot_transport_fields(&transport_io,bots->transport) && qa_source_save_finish(&transport_io,&transport);
        qa_source_save_dispose(&transport_io);
    }
    if(ok && present && bots->catalogue) ok=qa_bot_catalog_capture(bots->catalogue,&catalogue,error);
    bot_app_record record={.requirements={requirements.data,requirements.size},.runtime={runtime.data,runtime.size},
        .population={population.data,population.size},.shared_world={shared_world.data,shared_world.size},
        .transport={transport.data,transport.size},.catalogue={catalogue.data,catalogue.size}};
    qa_source_save_io io={0};
    ok=ok && qa_source_save_writer(&io,app->session,error) && app_signature(&io) &&
        qa_source_save_bool(&io,&present) && (!present || fields(&io,bots,&record)) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);qa_buffer_free(&requirements);qa_buffer_free(&runtime);qa_buffer_free(&population);
    qa_buffer_free(&shared_world);qa_buffer_free(&transport);qa_buffer_free(&catalogue);return ok;
}
static application_bot_graph *graph_at(application_bots *bots,size_t ordinal) {
    application_bot_graph *g=bots->graphs;
    while(g && ordinal) {g=g->next;--ordinal;} return g;
}
static size_t graph_count(application_bots *bots) {
    size_t count=0;for(application_bot_graph *g=bots->graphs;g;g=g->next) ++count;return count;
}
static bool binding(qa_source_save_io *io,application_bots *bots,qa_actor_id actor,qa_bot_navigation **value) {
    size_t count=graph_count(bots),index=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE && *value) {
        if(!qa_actor_id_equal(actor,qa_bot_navigation_actor(*value)))
            return bot_save_fail(io,QA_ERROR_FORMAT,"Bot navigation actor binding differs");
        application_bot_graph *g=bots->graphs;
        while(g && g->navigation!=qa_bot_navigation_runtime(*value)) {g=g->next;++index;}
        if(!g) return bot_save_fail(io,QA_ERROR_FORMAT,"Bot navigation graph owner is absent");
        ++index;
    }
    if(!qa_source_save_count(io,&index,count)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ && index) {
        application_bot_graph *g=graph_at(bots,index-1);
        return application_bot_navigation_restore_binding(bots,g->navigation,actor,value,io->error);
    }
    return true;
}
static bool asset_field(qa_source_save_io *io,application_bots *bots,application_bot_graph *g,bool prepare) {
    bool present=g->asset_resource!=NULL;
    if(!qa_source_save_bool(io,&present)) return false;
    if(!present) return !g->asset_resource || bot_save_fail(io,QA_ERROR_FORMAT,"Navigation asset presence differs");
    const char *path=io->direction==QA_SOURCE_SAVE_WRITE?qa_resource_path(g->asset_resource):NULL;
    qa_sha256_digest digest={0};if(g->asset_resource) digest=*qa_resource_digest(g->asset_resource);
    size_t ordinal=g->asset_mount_ordinal;
    qa_vfs *files=qa_launch_snapshot_mounts(launch(bots->application));
    bool ok=bot_save_text(io,&path) && path && qa_source_save_bytes(io,digest.bytes,32) &&
        qa_source_save_count(io,&ordinal,qa_vfs_mount_count(files));
    if(ok && io->direction==QA_SOURCE_SAVE_READ && prepare) {
        qa_vfs_mount_info mount;
        ok=qa_vfs_mount_at(files,ordinal,&mount) &&
            qa_vfs_acquire_from(files,mount.id,path,&g->asset_resource,io->error) &&
            qa_sha256_equal(&digest,qa_resource_digest(g->asset_resource));
        g->asset_mount_ordinal=ordinal;
    } else if(ok && io->direction==QA_SOURCE_SAVE_READ) {
        ok=g->asset_resource && ordinal==g->asset_mount_ordinal &&
            !strcmp(path,qa_resource_path(g->asset_resource)) &&
            qa_sha256_equal(&digest,qa_resource_digest(g->asset_resource));
    }
    if(io->direction==QA_SOURCE_SAVE_READ) free((void *)path);
    return ok?true:bot_save_fail(io,QA_ERROR_FORMAT,"Pinned navigation asset selection differs");
}
static bool same_profile(qa_movement_profile a,qa_movement_profile b,qa_error *error) {
    qa_source_save_io left={0},right={0};qa_buffer x={0},y={0};
    bool ok=qa_source_save_writer(&left,NULL,error) && qa_persistence_movement_profile(&left,&a) &&
        qa_source_save_finish(&left,&x) && qa_source_save_writer(&right,NULL,error) &&
        qa_persistence_movement_profile(&right,&b) && qa_source_save_finish(&right,&y) &&
        x.size==y.size && !memcmp(x.data,y.data,x.size);
    qa_source_save_dispose(&left);qa_source_save_dispose(&right);qa_buffer_free(&x);qa_buffer_free(&y);return ok;
}
static bool graph_policy(application_bot_graph *g,qa_error *error) {
    const qa_nav_graph_view *view=qa_nav_graph_read(g->graph);
    if(!view || !same_profile(g->profile,view->profile.movement,error) ||
        view->profile.shape.kind!=QA_SHAPE_BOX || memcmp(&g->bounds,&view->profile.shape.bounds,sizeof(g->bounds)))
        return application_fail(error,QA_ERROR_FORMAT,"Application navigation cache policy differs from its actual graph");
    return true;
}
static bool navigation_fields(qa_source_save_io *io,application_bots *bots,bool prepare,bool restore) {
    size_t count=graph_count(bots);
    if(!qa_source_save_count(io,&count,SIZE_MAX/sizeof(application_bot_graph))) return false;
    if(io->direction==QA_SOURCE_SAVE_READ && prepare && count>(io->input.size-io->offset)/50)
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated application navigation graph inventory");
    application_bot_graph **tail=&bots->graphs,*g=bots->graphs;
    qa_nav_map map={.name=bots->application->current_map,.format=bots->geometry.format};
    qa_sha256_digest digest;qa_sha256(bots->geometry.source,&digest);memcpy(map.digest,digest.bytes,32);
    for(size_t i=0;i<count;++i) {
        if(io->direction==QA_SOURCE_SAVE_READ && prepare) {
            g=calloc(1,sizeof(*g));
            if(!g) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring application navigation graph owner");
            *tail=g;tail=&g->next;
        }
        if(!g) return bot_save_fail(io,QA_ERROR_FORMAT,"Application navigation graph inventory differs");
        if(!provider_field(io,bots->application,&g->movement,false) ||
            !qa_persistence_movement_profile(io,&g->profile) || !qa_persistence_bounds(io,&g->bounds) ||
            !asset_field(io,bots,g,prepare)) return false;
        qa_buffer graph={0},state={0};qa_bytes graph_bytes={0},state_bytes={0};bool ok=true;
        if(io->direction==QA_SOURCE_SAVE_WRITE) {
            ok=qa_navigation_graph_save_capture(bots->application->session,g->graph,&graph,io->error) &&
                qa_persistence_navigation_capture(bots->application->session,g->navigation,&state,io->error);
            graph_bytes=(qa_bytes){graph.data,graph.size};state_bytes=(qa_bytes){state.data,state.size};
        }
        ok=ok && section(io,&graph_bytes) && graph_bytes.size && section(io,&state_bytes) && state_bytes.size;
        if(ok && io->direction==QA_SOURCE_SAVE_READ && prepare) {
            qa_nav_asset *asset=NULL;
            if(g->asset_resource) {
                int32_t checksum=(int32_t)qa_block_checksum(bots->geometry.source);
                /* Native NAV2/NAV3 ignore the optional AAS checksum argument. */
                ok=qa_nav_asset_read(qa_resource_bytes(g->asset_resource),&checksum,&asset,io->error);
            }
            if(ok) ok=qa_navigation_graph_save_restore(bots->application->session,graph_bytes,&map,asset,&g->graph,io->error);
            qa_nav_asset_release(asset);
            qa_navigation_services services=application_bot_navigation_services(bots);
            if(ok) ok=qa_navigation_create(g->graph,&services,&g->navigation,io->error);
        } else if(ok && io->direction==QA_SOURCE_SAVE_READ && restore) {
            ok=qa_navigation_graph_save_capture(bots->application->session,g->graph,&graph,io->error) &&
                graph.size==graph_bytes.size && !memcmp(graph.data,graph_bytes.data,graph.size) &&
                qa_persistence_navigation_restore(bots->application->session,g->navigation,state_bytes,io->error);
        }
        qa_buffer_free(&graph);qa_buffer_free(&state);
        if(!ok || !graph_policy(g,io->error)) return false;
        g=g->next;
    }
    if(io->direction==QA_SOURCE_SAVE_READ && g) return bot_save_fail(io,QA_ERROR_FORMAT,"Extra application navigation graph owner");
    if(io->direction==QA_SOURCE_SAVE_READ && restore) {
        /* All remaining binding records were already decoded at prepare. The
         * complete unchanged record check precedes this second import pass. */
        io->offset=io->input.size; return true;
    }
    if(!binding(io,bots,(qa_actor_id){0},&bots->map_navigation)) return false;
    size_t seats=bots->capacity;
    if(!qa_source_save_count(io,&seats,bots->capacity) || seats!=bots->capacity) return false;
    for(uint32_t i=0;i<bots->capacity;++i)
        if(!binding(io,bots,bots->seats[i].actor,&bots->seats[i].navigation)) return false;
    count=0;if(io->direction==QA_SOURCE_SAVE_WRITE) for(application_bot_target *t=bots->targets;t;t=t->next) ++count;
    if(!qa_source_save_count(io,&count,SIZE_MAX/sizeof(application_bot_target))) return false;
    if(io->direction==QA_SOURCE_SAVE_READ && count>(io->input.size-io->offset)/54)
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated application target navigation inventory");
    application_bot_target **link=&bots->targets,*target=bots->targets;
    for(size_t i=0;i<count;++i) {
        if(io->direction==QA_SOURCE_SAVE_READ) {
            target=calloc(1,sizeof(*target));
            if(!target) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring application target navigation owner");
            *link=target;link=&target->next;
        }
        if(target->predicting || !qa_source_save_actor(io,&target->actor) || !target->actor.registry ||
            !provider_field(io,bots->application,&target->movement,false) ||
            !qa_persistence_movement_profile(io,&target->profile) || !qa_persistence_bounds(io,&target->bounds) ||
            !binding(io,bots,target->actor,&target->navigation) || !target->navigation) return false;
        target=target->next;
    }
    return true;
}
bool application_navigation_save_capture(qa_application *app,qa_buffer *out,qa_error *error) {
    if(!app || !out || !application_bots_can_destroy(app) || (app->bots &&
       (app->bots->restoring || app->bots->round_phase!=APPLICATION_BOT_ROUND_ACTIVE ||
        app->bots->round || app->bots->original || app->bots->producing)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Application navigation is borrowed or restoring");
    bool present=app->bots!=NULL;qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,app->session,error) && bot_save_signature(&io,nav_magic) &&
        qa_source_save_bool(&io,&present) && (!present || navigation_fields(&io,app->bots,false,false)) &&
        qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);return ok;
}
bool application_bots_save_prepare(qa_application *app,qa_bytes bytes,qa_bytes nav,qa_error *error) {
    if(!app || app->bots || !app->world || !app->physics || !app->modes || !app->map_resource || !launch(app))
        return application_fail(error,QA_ERROR_ARGUMENT,"Bot candidate requires pinned map and stable shared services");
    qa_source_save_io io={0};bool present=false;
    bool ok=qa_source_save_reader(&io,app->session,bytes,error) && app_signature(&io) &&
        qa_source_save_bool(&io,&present);
    application_bots *bots=NULL;bot_app_record record={0};
    if(ok && present) {
        bots=calloc(1,sizeof(*bots));
        if(!bots) ok=bot_save_fail(&io,QA_ERROR_MEMORY,"Allocating restored application bot owner");
        else {
            bots->application=app;bots->restoring=true;app->bots=bots;
            ok=fields(&io,bots,&record);
        }
    }
    ok=ok && qa_source_save_finish(&io,NULL);qa_source_save_dispose(&io);
    if(ok && present) {
        ok=qa_bots_save_requirements_read(record.requirements,&bots->saved_requirements,error) &&
            bots->saved_requirements.population==(record.population.size!=0);
        bots->saved_bot_record=bytes;bots->saved_navigation_record=nav;
        bots->saved_runtime=record.runtime;bots->saved_population=record.population;
        bots->saved_shared_world=record.shared_world;bots->saved_transport=record.transport;
        bots->saved_catalogue=record.catalogue;
    }
    bool nav_present=false;
    if(ok) ok=qa_source_save_reader(&io,app->session,nav,error) && bot_save_signature(&io,nav_magic) &&
        qa_source_save_bool(&io,&nav_present) && nav_present==present &&
        (!present || navigation_fields(&io,bots,true,false)) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    return ok && (!present ||
        (qa_vfs_restore_references(bots->files,bots->saved_file_references,bots->saved_file_reference_count,error) &&
         application_bots_construct_restored(bots,error)));
}
bool application_navigation_save_restore(qa_application *app,qa_bytes bytes,qa_error *error) {
    application_bots *bots=app?app->bots:NULL;
    if(bots && (!bots->restoring || bots->navigation_restored || bytes.size!=bots->saved_navigation_record.size ||
        memcmp(bytes.data,bots->saved_navigation_record.data,bytes.size)))
        return application_fail(error,QA_ERROR_FORMAT,"Navigation import differs from prepared candidate");
    qa_source_save_io io={0};bool present=false;
    bool ok=app && qa_source_save_reader(&io,app->session,bytes,error) && bot_save_signature(&io,nav_magic) &&
        qa_source_save_bool(&io,&present) && present==(bots!=NULL) &&
        (!present || navigation_fields(&io,bots,false,true)) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);if(ok && bots) bots->navigation_restored=true;return ok;
}
bool application_bots_save_restore(qa_application *app,qa_bytes bytes,qa_error *error) {
    if(!app) return application_fail(error,QA_ERROR_ARGUMENT,"Missing bot candidate application");
    application_bots *bots=app->bots;
    if(!bots) {
        qa_source_save_io io={0};bool present=true;
        bool ok=qa_source_save_reader(&io,app->session,bytes,error) && app_signature(&io) &&
            qa_source_save_bool(&io,&present) && !present && qa_source_save_finish(&io,NULL);
        qa_source_save_dispose(&io);return ok;
    }
    if(!bots->restoring || !bots->navigation_restored || bots->runtime_restored ||
        bytes.size!=bots->saved_bot_record.size || memcmp(bytes.data,bots->saved_bot_record.data,bytes.size))
        return application_fail(error,QA_ERROR_FORMAT,"Bot import differs from prepared candidate or navigation is not restored");
    qa_bot_runtime_saved_map saved={0};
    if(!qa_bot_runtime_save_map_read(bots->saved_runtime,&saved,error)) return false;
    const char *name=qa_strings_cstr(qa_session_strings(app->session),app->current_map);
    qa_bot_runtime_map map={.name=name,.entities=saved.entities?&bots->entities:NULL,
        .source_entities=saved.source?bots->geometry.lumps[QA_BSP_ENTITIES].bytes:(qa_bytes){0},
        .navigation=saved.navigation?bots->map_navigation:NULL};
    bool ok=!bots->saved_shared_world.size || application_bots_shared_construct(bots,true,error);
    if(ok && bots->shared_world) {
        qa_source_save_io transport_io={0};
        ok=application_bot_world_restore(bots->shared_world,bots->saved_shared_world,error) &&
            qa_source_save_reader(&transport_io,app->session,bots->saved_transport,error) &&
            application_bot_transport_fields(&transport_io,bots->transport) && qa_source_save_finish(&transport_io,NULL);
        qa_source_save_dispose(&transport_io);
    }
    ok=ok && (!saved.name || (name && !strcmp(saved.name,name))) &&
        qa_bot_runtime_save_restore(app->session,bots->runtime,bots->saved_runtime,saved.name?&map:NULL,error) &&
        application_bots_guests_restore(bots,error) &&
        (!bots->population || qa_bots_population_restore(bots->population,bots->saved_population,error));
    if(ok && bots->saved_catalogue.size)
        ok=application_bots_catalog_create(bots,error) && qa_bot_catalog_restore(bots->catalogue,bots->saved_catalogue,error);
    qa_bot_runtime_saved_map_free(&saved);if(ok) bots->runtime_restored=true;return ok;
}
static application_bot_graph *binding_graph(application_bots *bots,qa_bot_navigation *navigation) {
    if(!navigation) return NULL;
    qa_navigation *runtime=qa_bot_navigation_runtime(navigation);
    for(application_bot_graph *g=bots->graphs;g;g=g->next) if(g->navigation==runtime) return g;
    return NULL;
}
static bool guest_selected(application_provider *p) {
    uint64_t presentation=QA_ROLE_BIT(QA_ROLE_HUD)|QA_ROLE_BIT(QA_ROLE_MENU)|QA_ROLE_BIT(QA_ROLE_EFFECTS)|
        QA_ROLE_BIT(QA_ROLE_AUDIO)|QA_ROLE_BIT(QA_ROLE_MUSIC);
    return p && p->attached && p->constructed && p->product && p->product->family==QA_GAME_Q3 &&
        (p->kind==APPLICATION_PROVIDER_QVM || p->kind==APPLICATION_PROVIDER_NATIVE) &&
        p->launch && (p->launch->roles & ~presentation)!=0;
}
static bool agreement(application_bots *bots,qa_error *error) {
    qa_application *app=bots->application;
    if(bots->map_resource!=app->map_resource || !bots->runtime ||
       bots->saved_requirements.runtime.maximum_states!=64)
        return application_fail(error,QA_ERROR_FORMAT,"Application bot map or actual source handle capacity differs");
    size_t guests=0;for(application_bot_guest *g=bots->guests;g;g=g->next) ++guests;
    if(guests>UINT32_MAX/64 || bots->saved_requirements.runtime.observations!=QA_BOT_OBSERVATION_NATIVE)
        return application_fail(error,QA_ERROR_FORMAT,"Application guest bot namespace capacity differs");
    uint32_t base=bots->saved_requirements.runtime.minimum_clients;
    uint32_t entity_base=base>1024u?base:1024u;
    if(base>qa_actors_capacity(qa_session_actors(app->session)))
        return application_fail(error,QA_ERROR_FORMAT,"Application guest bot namespace origin exceeds restored actor extent");
    for(application_bot_guest *g=bots->guests;g;g=g->next) {
        if(!g->runtime || g->runtime==bots->runtime || !guest_selected(g->provider) || g->client_base<base || (g->client_base-base)%64 ||
            (g->client_base-base)/64>=guests ||
            (uint64_t)entity_base+(uint64_t)((g->client_base-base)/64)*1024!=g->entity_base)
            return application_fail(error,QA_ERROR_FORMAT,"Application guest bot namespace differs from its source owner");
    }
    application_provider **table=app->routing_providers?app->routing_providers:app->providers;
    size_t count=app->routing_providers?app->routing_provider_count:app->provider_count;
    for(size_t i=0;i<count;++i) if(guest_selected(table[i])) {
        bool found=false;for(application_bot_guest *g=bots->guests;g;g=g->next) found=found || g->provider==table[i];
        if(!found) return application_fail(error,QA_ERROR_FORMAT,"Selected guest bot provider has no saved namespace");
    }
    application_bot_graph *map=binding_graph(bots,bots->map_navigation);
    if(!map || map->movement || qa_bot_navigation_actor(bots->map_navigation).registry)
        return application_fail(error,QA_ERROR_FORMAT,"Application map navigation binding differs");
    for(application_bot_graph *g=bots->graphs;g;g=g->next) if(!graph_policy(g,error)) return false;
    for(uint32_t i=0;i<bots->capacity;++i) {
        application_bot_seat *s=bots->seats+i;
        if(!s->actor.registry) {
            if(s->navigation) return application_fail(error,QA_ERROR_FORMAT,"Absent bot seat owns navigation");
            continue;
        }
        application_bot_graph *g=binding_graph(bots,s->navigation);
        if(!g || !qa_actor_id_equal(s->actor,qa_bot_navigation_actor(s->navigation)))
            return application_fail(error,QA_ERROR_FORMAT,"Application bot seat navigation binding differs");
        for(uint32_t j=0;j<i;++j) {
            const application_bot_seat *previous=bots->seats+j;
            if(previous->actor.registry &&
                (qa_actor_id_equal(previous->actor,s->actor) ||
                 (!previous->retired && !s->retired &&
                  (previous->seat==s->seat || previous->library_client==s->library_client))))
                return application_fail(error,QA_ERROR_FORMAT,"Application bot seats alias a saved actor or active roster seat");
        }
        if(s->retired) continue;
        application_player_record *record=NULL;
        if(app->players) for(size_t j=0;j<app->players->count;++j)
            if(!app->players->records[j].retiring && app->players->records[j].seat==s->seat &&
                qa_actor_id_equal(app->players->records[j].actor,s->actor)) {record=app->players->records+j;break;}
        if(!record || !record->bot || !qa_actors_get(qa_session_actors(app->session),s->actor))
            return application_fail(error,QA_ERROR_FORMAT,"Application live bot seat differs from restored roster");
        if(bots->population && !qa_bot_runtime_closed(bots->runtime)) {
            qa_bot_view view;
            if(!qa_bots_read(bots->population,s->actor,&view,error) || view.client!=s->library_client)
                return application_fail(error,QA_ERROR_FORMAT,"Application bot seat differs from restored population");
        }
    }
    for(application_bot_target *t=bots->targets;t;t=t->next) {
        application_bot_graph *g=binding_graph(bots,t->navigation);
        if(!g || t->predicting || !qa_actor_id_equal(t->actor,qa_bot_navigation_actor(t->navigation)) ||
            t->movement!=g->movement || !same_profile(t->profile,g->profile,error) ||
            memcmp(&t->bounds,&g->bounds,sizeof(t->bounds)))
            return application_fail(error,QA_ERROR_FORMAT,"Application target navigation differs from saved cache policy");
    }
    return true;
}
bool application_bots_save_finish(qa_application *app,qa_error *error) {
    if(!app) return application_fail(error,QA_ERROR_ARGUMENT,"Missing application bot completion owner");
    application_bots *bots=app->bots;if(!bots) return true;
    if(!bots->restoring || !bots->navigation_restored || !bots->runtime_restored ||
        !application_bots_can_destroy(app) || !agreement(bots,error)) return false;
    /* Recapture proves complete source continuation and admitted identity after
     * all shared stores and guest continuations have finished reconnecting. */
    qa_buffer actual={0},nav={0};bots->restoring=false;
    bool ok=(!bots->population || qa_bots_source_memory_bind(bots->population,error)) &&
        application_bots_save_capture(app,&actual,error) &&
        actual.size==bots->saved_bot_record.size && !memcmp(actual.data,bots->saved_bot_record.data,actual.size) &&
        application_navigation_save_capture(app,&nav,error) && nav.size==bots->saved_navigation_record.size &&
        !memcmp(nav.data,bots->saved_navigation_record.data,nav.size);
    qa_buffer_free(&actual);qa_buffer_free(&nav);
    if(!ok) {
        bots->restoring=true;
        if(!error || error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Application bot continuation changed during candidate restoration");
        return false;
    }
    bots->saved_bot_record=(qa_bytes){0};bots->saved_navigation_record=(qa_bytes){0};
    bots->saved_runtime=(qa_bytes){0};bots->saved_population=(qa_bytes){0};
    bots->saved_shared_world=(qa_bytes){0};bots->saved_transport=(qa_bytes){0};bots->saved_catalogue=(qa_bytes){0};
    application_bots_guests_finish(bots);return true;
}
