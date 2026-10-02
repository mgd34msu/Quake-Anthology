#include "guest_q3_components_private.h"
#include "guest_q3_component_private.h"
#include "unified_q3_events.h"
#include <limits.h>
#include <time.h>

static qa_collision_geometry *geometry(void *context)
{ component_game_row *row=context; return qa_world_geometry(row->roster->options.world); }
static bool load_map(void *context,const char *path,qa_error *e)
{
    component_game_row *row=context; const qa_launch_choices *choices=qa_launch_snapshot_choices(row->roster->options.snapshot);
    char *normalized=qa_vfs_normalize_path(path,e),*actual=qa_vfs_normalize_path(choices->world.map,e);
    if(!normalized||!actual) { free(normalized); free(actual); return false; }
    const char *requested=!strncmp(normalized,"maps/",5)?normalized+5:normalized;
    const char *selected=!strncmp(actual,"maps/",5)?actual+5:actual;
    size_t a=strlen(requested),b=strlen(selected); if(a>=4&&!strcmp(requested+a-4,".bsp")) a-=4; if(b>=4&&!strcmp(selected+b-4,".bsp")) b-=4;
    bool ok=a==b&&!memcmp(requested,selected,a)&&geometry(row)!=NULL;
    free(normalized); free(actual); return ok||application_fail(e,QA_ERROR_ARGUMENT,"Component collision load differs from its actual shared map");
}
static bool point(void *context,const float origin[3],int32_t *area,int32_t *cluster,qa_error *e)
{
    qa_collision_leaf leaf;
    if(!qa_collision_point_leaf(geometry(context),qa_v3(origin[0],origin[1],origin[2]),&leaf,e)) return false;
    if(leaf.area<INT32_MIN||leaf.area>INT32_MAX||leaf.cluster<INT32_MIN||leaf.cluster>INT32_MAX) return application_fail(e,QA_ERROR_FORMAT,"Component PVS leaf exceeds its real Q3 ABI");
    *area=(int32_t)leaf.area; *cluster=(int32_t)leaf.cluster; return true;
}
static bool areas(void *context,int32_t area,uint8_t accumulator[32],size_t *count,qa_error *e)
{
    uint8_t bits[32]; if(!qa_collision_area_bits(geometry(context),area,bits,32,count,e)) return false;
    for(size_t i=0;i<*count;++i) accumulator[i]|=bits[i];
    return true;
}
static bool connected(void *context,int32_t a,int32_t b)
{
    component_game_row *row=context; bool result=false;
    return qa_collision_areas_connected(geometry(row),a,b,&result,&row->roster->visibility_failure)&&result;
}
static bool visible(void *context,int32_t a,int32_t b)
{
    component_game_row *row=context; bool result=false;
    return qa_collision_cluster_visible(geometry(row),a,b,false,&result,&row->roster->visibility_failure)&&result;
}
static void print(void *context,const char *text)
{
    component_game_row *row=context;
    qa_command_context command={.owner=row->publication.owner,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_SERVER};
    application_console_print(row->roster->options.application,&command,text);
}
static int32_t calendar(void *context,qa_q3_host_calendar *out)
{
    (void)context; time_t now=time(NULL); const struct tm *value=localtime(&now);
    if(!value) return -1;
    if(out) *out=(qa_q3_host_calendar){value->tm_sec,value->tm_min,value->tm_hour,value->tm_mday,value->tm_mon,value->tm_year,value->tm_wday,value->tm_yday,value->tm_isdst};
    uint32_t bits=(uint32_t)now; int32_t result; memcpy(&result,&bits,4); return result;
}
static bool match_read(void *context,qa_actor_id actor,qa_string_id *team,double *score,qa_error *e)
{
    component_game_row *row=context; application_q3_components_options *options=&row->roster->options;
    return options->match_read&&options->match_read(options->context,actor,team,score,e);
}
static bool match_write(void *context,qa_actor_id actor,bool team,qa_string_id id,double score,qa_error *e)
{
    component_game_row *row=context; application_q3_components_options *options=&row->roster->options;
    return options->match_write&&options->match_write(options->context,actor,team,id,score,e);
}
static bool damage_context(void *context,qa_damage_request *request,qa_error *e)
{
    component_game_row *row=context; qa_application *app=row->roster->options.application;
    qa_clock_state clock;
    if(!q3components_current(row)||!qa_session_clock(app->session,row->publication.owner,&clock))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component damage lost its genuine Source clock");
    request->attack.time_ns=clock.frame.time_ns;
    request->attack.weapon_provider=row->publication.owner;
    qa_actor_id source=request->attack.attacker.registry?request->attack.attacker:request->target;
    application_provider *combat=application_provider_for(app,request->target,QA_ROLE_COMBAT,"");
    application_provider *inventory=application_provider_for(app,source,QA_ROLE_INVENTORY,"");
    application_provider *movement=application_provider_for(app,request->target,QA_ROLE_MOVEMENT,"");
    request->attack.combat_provider=combat?combat->owner:0;
    request->attack.inventory_provider=inventory?inventory->owner:0;
    request->attack.movement_provider=movement?movement->owner:0;
    return true;
}
static bool source_event(void *context,qa_actor_id recipient,const char *text,int32_t time,qa_error *e)
{
    component_game_row *row=context;
    return application_unified_q3_component_command(row->roster->options.application,&row->publication,recipient,text,time,e);
}
bool q3components_create_game(component_game_row *row,qa_error *e)
{
    application_q3_components_options *options=&row->roster->options;
    qa_json_document *d=NULL; if(!qa_json_parse(qa_resource_bytes(row->declaration),&d,e)) return false;
    qa_json_id abi=qa_json_get(d,qa_json_root(d),"abiProfile"); qa_qvm_abi layout=QA_QVM_Q3_MODERN;
    qa_json_id runtime=qa_json_get(d,qa_json_get(d,qa_json_root(d),"presentation"),"runtime");
    if(runtime!=QA_JSON_NONE) {
        qa_buffer name={0};
        if(!qa_json_string(d,runtime,&name,e)) { qa_json_destroy(d); return false; }
        if(memchr(name.data,0,name.size)) { qa_buffer_free(&name); qa_json_destroy(d); return application_fail(e,QA_ERROR_FORMAT,"Component presentation runtime contains NUL"); }
        row->presentation_runtime=(char *)name.data; row->publication.presentation_runtime=row->presentation_runtime;
    }
    bool valid=qa_json_string_equal(d,abi,"q3-modern");
    if(qa_json_string_equal(d,abi,"q3-1.16n-base")) { layout=QA_QVM_Q3_116N; valid=true; }
    qa_json_destroy(d);
    if(!valid) return application_fail(e,QA_ERROR_FORMAT,"Component GAME declaration has no actual admitted ABI");
    row->publication.abi=layout;
    application_q3_component_options create={.program=row->program,.declaration=row->declaration,
        .program_path=row->publication.metadata->program_path,.program_digest=row->publication.metadata->program_digest,
        .declaration_digest=row->publication.metadata->declaration_digest,.image=row->image,.abi=layout,
        .map_path=qa_launch_snapshot_choices(options->snapshot)->world.map,
        .host={.role=QA_QVM_GAME,.abi=layout,.session=options->application->session,.world=options->world,
            .owner=row->publication.owner,.service_owner=row->services,.mounts=row->publication.content,
            .command_context={.owner=row->publication.owner,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_SERVER},
            .common={.context=row,.print=print,.calendar=calendar},
            .collision={.context=row,.geometry=geometry,.load_map=load_map},.entity_text=options->entity_text},
        .combat=options->application->combat,.inventory=options->application->inventory,
        .application=options->application,.equipment=options->equipment,
        .visibility={.context=row,.point=point,.area_bits=areas,.areas_connected=connected,.cluster_visible=visible},
        .generation=row->publication.generation,.context=row,.current=q3components_current,.storage_current=q3components_storage,
        .match_read=match_read,.match_write=match_write,.clients=options->clients,
        .actor_operations=options->application->mod_operations,.damage_context=damage_context,.source_command_event=source_event};
    create.host.write_view.root=qa_catalog_product_write_root(row->provider->product_catalog,row->provider->launch->selection.product);
    for(size_t i=0;i<qa_vfs_mount_count(create.host.mounts);++i) {
        qa_vfs_mount_info mount;
        if(qa_vfs_mount_at(create.host.mounts,i,&mount)&&mount.writable&&!mount.is_archive&&
            qa_fs_root_same_object(create.host.write_view.root,qa_vfs_mount_root(create.host.mounts,mount.id))) { create.host.writable_mount=mount.id; break; }
    }
    if(!application_q3_mod_operations_read(options->application->mod_operations,create.operations,e)||
        !application_q3_component_create(&create,options->restoring,&row->publication.game,e)) return false;
    row->publication.source=application_q3_component_source_read(row->publication.game); return true;
}
