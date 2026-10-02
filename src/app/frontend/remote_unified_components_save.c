#include "remote_unified_components_save.h"
#include "remote_unified_components_private.h"
#include "save_private.h"
#include "qa/cvars_save.h"
#include "qa/console_save.h"
#include "qa/binary.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_source_save_io *io,const char *text)
{ return q3remote_component_fail(io->error,QA_ERROR_FORMAT,text); }
static bool blob(qa_source_save_io *io,qa_buffer *value)
{
    size_t size=value->size;
    if(!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        value->data=size?malloc(size):NULL; value->size=size;
        if(size&&!value->data) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining remote component continuation");
    }
    return qa_source_save_bytes(io,value->data,size);
}
static bool document(qa_source_save_io *io,qa_unified_document **value,qa_unified_document_kind kind)
{
    qa_buffer bytes={0};
    if(io->direction==QA_SOURCE_SAVE_WRITE) {
        if(!*value||qa_unified_document_type(*value)!=kind) return fail(io,"Component checkpoint document has another semantic domain");
        qa_bytes source=qa_json_source(qa_unified_document_json(*value),qa_unified_document_root(*value));
        bytes=(qa_buffer){(uint8_t *)source.data,source.size};
    }
    bool ok=blob(io,&bytes);
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(ok) ok=qa_unified_document_create(kind,(qa_bytes){bytes.data,bytes.size},value,io->error);
        qa_buffer_free(&bytes);
    }
    return ok;
}
static bool actor(qa_source_save_io *io,frontend_unified_components *owner,qa_actor_id *value)
{
    bool present=io->direction==QA_SOURCE_SAVE_WRITE&&value->registry;
    qa_saved_actor_id wire={0};
    if(present&&!frontend_remote_unified_wire_actor(owner->replica,*value,&wire))
        return fail(io,"Component checkpoint actor has no actual replica wire identity");
    if(!qa_source_save_bool(io,&present)||!qa_source_save_u32(io,&wire.slot)||!qa_source_save_u64(io,&wire.generation)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(!present) {
            if(wire.slot||wire.generation) return fail(io,"Absent component actor contains wire identity");
            *value=(qa_actor_id){0};
        } else if(!frontend_remote_unified_actor_retained(owner->replica,wire.slot,wire.generation,value,io->error)) return false;
    }
    return true;
}
static bool write_record(void *context,size_t at,qa_bytes bytes,qa_error *e)
{ (void)e; if(bytes.size) memcpy((uint8_t *)context+at,bytes.data,bytes.size); return true; }
static bool game_state(qa_source_save_io *io,qa_qvm_abi abi,qa_q3_gamestate *value)
{
    uint8_t bytes[20100]={0};
    qa_q3_abi_record record={.abi=abi,.bytes={bytes,sizeof(bytes)},.context=bytes,.write=write_record};
    bool ok=io->direction==QA_SOURCE_SAVE_READ||qa_q3_abi_write_gamestate(&record,0,true,value,io->error);
    if(ok) ok=qa_source_save_bytes(io,bytes,sizeof(bytes));
    if(ok&&io->direction==QA_SOURCE_SAVE_READ) {
        uint32_t count=qa_load_u32le(bytes+20096);
        if(count>16000||bytes[4096]) return fail(io,"Saved component gameState has invalid Source strings");
        for(size_t i=0;i<1024;++i) {
            uint32_t offset=qa_load_u32le(bytes+4*i);
            if(offset>=(count?count:1)||(offset&&!memchr(bytes+4096+offset,0,count-offset)))
                return fail(io,"Saved component configstring has no exact Source terminator");
            value->config_offsets[i]=(uint16_t)offset;
        }
        value->string_bytes=count; memcpy(value->strings,bytes+4096,16000);
    }
    return ok;
}
static bool arguments(qa_source_save_io *io,qa_command_tokens *value)
{
    size_t count=value->count;
    if(!qa_source_save_count(io,&count,128)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        value->values=count?calloc(count,sizeof(*value->values)):NULL; value->count=count;
        if(count&&!value->values) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining checkpoint component argv");
    }
    for(size_t i=0;i<count;++i) {
        const char *text=io->direction==QA_SOURCE_SAVE_WRITE?value->values[i]:NULL;
        if(!qa_source_save_text(io,&text)||!text||strlen(text)>8192) return fail(io,"Saved component argv exceeds its source extent");
        if(io->direction==QA_SOURCE_SAVE_READ) value->values[i]=(char *)text;
    }
    const char *tail=io->direction==QA_SOURCE_SAVE_WRITE?value->args_text:NULL;
    if(!qa_source_save_text(io,&tail)||!tail) return fail(io,"Saved component command has no literal args");
    if(io->direction==QA_SOURCE_SAVE_READ) value->args_text=(char *)tail;
    return true;
}
static bool source(qa_source_save_io *io,remote_component_state *state)
{
    if(!qa_source_save_u64(io,&state->game_state_revision)||state->game_state_revision>QA_UNIFIED_SAFE_INTEGER||
        !qa_source_save_i32(io,&state->command_sequence)||state->command_sequence<0||
        !game_state(io,state->abi,&state->game_state)||!qa_source_save_count(io,&state->command_count,64)) return false;
    if(state->command_count>(size_t)state->command_sequence) return fail(io,"Saved component command history exceeds its reached sequence");
    int32_t first=state->command_sequence-(int32_t)state->command_count+1;
    for(size_t i=0;i<state->command_count;++i) {
        application_q3_scene_command *command=state->commands+i;
        if(!qa_source_save_i32(io,&command->sequence)||command->sequence!=first+(int32_t)i||
            !qa_source_save_bool(io,&command->addressed)||!frontend_save_text(io,(char **)&command->text)||
            !command->text||!arguments(io,state->arguments+i)) return fail(io,"Saved component reliable history is not contiguous");
        command->arguments=state->arguments+i;
    }
    return true;
}
static bool state(qa_source_save_io *io,frontend_unified_components *owner,remote_component_state *value)
{
    uint32_t abi=(uint32_t)value->abi;
    if(!frontend_save_text(io,&value->provider)||!value->provider||
        !qa_source_save_u64(io,&value->owner_generation)||!value->owner_generation||value->owner_generation>QA_UNIFIED_SAFE_INTEGER||
        !qa_source_save_u64(io,&value->generation)||value->generation>QA_UNIFIED_SAFE_INTEGER||
        !qa_source_save_u32(io,&abi)||(abi!=QA_QVM_Q3_MODERN&&abi!=QA_QVM_Q3_116N)||
        !document(io,&value->identity,QA_UNIFIED_CHECKPOINT)||!document(io,&value->presentation_owner,QA_UNIFIED_CHECKPOINT)) return false;
    value->abi=(qa_qvm_abi)abi;
    if(!source(io,value)) return false;
    return io->direction==QA_SOURCE_SAVE_WRITE||q3remote_component_state_qualify(owner,value,io->error);
}
static bool snapshot(qa_source_save_io *io,qa_qvm_abi abi,qa_q3_snapshot *value)
{
    size_t size=qa_qvm_snapshot_bytes(abi);
    uint8_t *bytes=calloc(1,size);
    if(!bytes) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining component Source snapshot bytes");
    qa_q3_abi_record record={.abi=abi,.bytes={bytes,size},.context=bytes,.write=write_record};
    bool ok=io->direction==QA_SOURCE_SAVE_READ||qa_q3_abi_write_snapshot(&record,0,true,value,0,io->error);
    if(ok) ok=qa_source_save_bytes(io,bytes,size);
    if(ok&&io->direction==QA_SOURCE_SAVE_READ) {
        size_t ps=qa_qvm_player_bytes(abi),es=qa_qvm_entity_bytes(abi);
        int32_t count=qa_load_i32le(bytes+44+ps);
        if(count<0||count>256) ok=fail(io,"Saved component snapshot exceeds the actual Source entity window");
        if(ok) {
            qa_q3_entity *entities=count?calloc((size_t)count,sizeof(*entities)):NULL;
            if(count&&!entities) ok=q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining component snapshot entities");
            *value=(qa_q3_snapshot){.valid=true,.flags=bytes[0],.server_time=qa_load_i32le(bytes+8),
                .area_bytes=32,.entity_count=(size_t)count,.entities=entities,.server_command_number=qa_load_i32le(bytes+size-4)};
            memcpy(value->area_mask,bytes+12,32);
            if(ok) ok=qa_q3_abi_read_player(&record,44,true,&value->player,io->error);
            for(size_t i=0;ok&&i<(size_t)count;++i) ok=qa_q3_abi_read_entity(&record,48+ps+i*es,true,entities+i,io->error);
        }
    }
    free(bytes); return ok;
}
static bool frame(qa_source_save_io *io,remote_component *row,remote_component_frame **slot)
{
    bool present=io->direction==QA_SOURCE_SAVE_WRITE&&*slot;
    if(!qa_source_save_bool(io,&present)||!present) return !io->failed;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        *slot=calloc(1,sizeof(**slot));
        if(!*slot) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining received component Source frame");
    }
    remote_component_frame *value=*slot; application_q3_scene_context *c=&value->context;
    if(!actor(io,row->parent,&value->viewer)||!value->viewer.registry||
        !qa_source_save_u64(io,&c->generation)||c->generation!=row->state.generation||
        !qa_source_save_i64(io,&c->revision)||c->revision<0||(uint64_t)c->revision>QA_UNIFIED_SAFE_INTEGER||
        !qa_source_save_i64(io,&c->game_state_revision)||c->game_state_revision<0||
        !qa_source_save_i32(io,&c->time_ms)||c->time_ms<0||
        !qa_source_save_i32(io,&c->frame_ms)||c->frame_ms<0||
        !qa_source_save_i32(io,&c->client_number)||c->client_number<0||c->client_number>1023||
        !qa_source_save_vec3(io,&c->origin)||!qa_vec_finite(c->origin)) return false;
    for(size_t i=0;i<3;++i) if(!qa_source_save_vec3(io,c->axis+i)||!qa_vec_finite(c->axis[i])) return false;
    if(!qa_source_save_bool(io,&c->has_weapon_presented)||!qa_source_save_bool(io,&c->weapon_presented)||
        !qa_source_save_bool(io,&value->has_scene)||!value->has_scene||
        !qa_source_save_count(io,&c->actor_count,1024)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        value->actors=c->actor_count?calloc(c->actor_count,sizeof(*value->actors)):NULL;
        if(c->actor_count&&!value->actors) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining component binding map");
    }
    bool viewer=false;
    for(size_t i=0;i<c->actor_count;++i) {
        application_q3_scene_actor *binding=value->actors+i;
        if(!qa_source_save_u32(io,&binding->slot)||binding->slot>1023||
            !actor(io,row->parent,&binding->actor)||!binding->actor.registry||!qa_source_save_bool(io,&binding->owned)) return false;
        for(size_t k=0;k<i;++k) if(value->actors[k].slot==binding->slot) return fail(io,"Saved component repeats a source binding slot");
        viewer|=!binding->owned&&binding->slot==(uint32_t)c->client_number&&qa_actor_id_equal(binding->actor,value->viewer);
    }
    value->source.abi=row->state.abi;
    if(!viewer||!source(io,&value->source)||!snapshot(io,row->state.abi,&value->snapshot)||
        value->snapshot.server_time!=c->time_ms||value->snapshot.server_command_number!=value->source.command_sequence||
        value->source.game_state_revision!=(uint64_t)c->game_state_revision||
        value->source.command_sequence>row->state.command_sequence||value->source.game_state_revision>row->state.game_state_revision)
        return fail(io,"Saved component frame lost its Source history");
    c->game_state=&value->source.game_state; c->commands=value->source.commands; c->command_count=value->source.command_count;
    c->snapshot=&value->snapshot; c->actors=value->actors; return true;
}
static bool resource(qa_source_save_io *io,qa_vfs *files,const frontend_unified_components_refs *refs,
    qa_resource **value,qa_vfs_acquisition *opening)
{
    uint64_t pool=0,id=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE&&!qa_application_content_resource_id(refs->content,*value,&pool,&id))
        return fail(io,"Component program is outside the actual captured content holders");
    if(!qa_source_save_u64(io,&pool)||!pool||!qa_source_save_u64(io,&id)||!id||
        !qa_source_save_u64(io,&opening->mount)||!qa_source_save_u64(io,&opening->resource_id)||
        !frontend_save_text(io,&opening->path)||!frontend_save_text(io,&opening->lookup_path)||
        !frontend_save_text(io,&opening->link_source)||!frontend_save_text(io,&opening->link_target)||
        !qa_vfs_acquisition_opening_codec(io,files,opening)||!opening->opening_present||
        !qa_vfs_acquisition_retained(files,opening,io->error)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        const qa_resource *actual=qa_application_content_resource(refs->content,pool,id);
        if(!actual||qa_resource_id(actual)!=opening->resource_id) return fail(io,"Saved component program receipt has another held resource");
        *value=(qa_resource *)actual; qa_resource_retain(*value);
    }
    return true;
}
static bool profile(remote_component *row,qa_error *e)
{
    qa_json_document *d=NULL; if(!qa_json_parse(row->state.mod->declaration,&d,e)) return false;
    qa_json_id declaration=qa_json_get(d,qa_json_root(d),"presentation"),cgame=qa_json_get(d,declaration,"cgame");
    bool ok=qa_json_string_equal(d,qa_json_get(d,declaration,"runtime"),"qvm-scene")&&
        qa_json_string_equal(d,qa_json_get(d,cgame,"path"),row->acquisition.path)&&
        !strcmp(row->gameplay_acquisition.path,row->state.mod->program_path)&&
        qa_sha256_equal(qa_resource_digest(row->gameplay),&row->state.mod->program_digest)&&
        qa_qvm_image_load(qa_resource_bytes(row->artifact),&row->image,e)&&
        qa_qvm_image_load(qa_resource_bytes(row->gameplay),&row->gameplay_image,e)&&
        application_q3_scene_profile_create(row->image,row->state.abi,row->acquisition.path,row->gameplay_image,
            row->state.mod->program_path,qa_json_source(d,declaration),&row->profile,e);
    qa_json_destroy(d); return ok||(e&&e->code!=QA_OK?false:q3remote_component_fail(e,QA_ERROR_FORMAT,"Saved component has another genuine CG declaration"));
}
static bool admissions(qa_source_save_io *io,remote_component *row)
{
    if(!qa_source_save_u64(io,&row->submission_cycle)||!qa_source_save_bool(io,&row->admissions_ready)||
        !qa_source_save_count(io,&row->admission_count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if(!row->submission_cycle&&(row->admission_count||row->admissions_ready||row->submitted))
        return fail(io,"Component admission continuation lacks its real scene cycle");
    if(row->submitted&&!row->admissions_ready) return fail(io,"Submitted component has no completed admission receipt");
    if(io->direction==QA_SOURCE_SAVE_READ) {
        row->admissions=row->admission_count?calloc(row->admission_count,sizeof(*row->admissions)):NULL;
        if(row->admission_count&&!row->admissions) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining saved component admissions");
    }
    for(size_t i=0;i<row->admission_count;++i) {
        remote_component_packet_admission *receipt=row->admissions+i;
        if(!qa_source_save_i32(io,&receipt->source_time)||!qa_source_save_u32(io,&receipt->first_entity)||
            receipt->first_entity>QA_Q3_SOURCE_ENTITY_LIMIT||!qa_source_save_bool(io,&receipt->entity_started)||
            !qa_source_save_count(io,&receipt->entity_scanned,SIZE_MAX)||
            !qa_source_save_count(io,&receipt->entity_count,QA_Q3_SOURCE_ENTITY_LIMIT-receipt->first_entity)||
            !qa_source_save_count(io,&receipt->entity_emitted,receipt->entity_count)||receipt->entity_count>receipt->entity_scanned||
            (!receipt->entity_started&&(receipt->first_entity||receipt->entity_scanned||receipt->entity_count||receipt->entity_emitted))||
            !qa_source_save_count(io,&receipt->polygon_count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
        if(io->direction==QA_SOURCE_SAVE_READ) {
            receipt->polygons=receipt->polygon_count?calloc(receipt->polygon_count,sizeof(*receipt->polygons)):NULL;
            if(receipt->polygon_count&&!receipt->polygons) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining saved component polygon receipts");
        }
        for(size_t j=0;j<receipt->polygon_count;++j) {
            remote_component_polygon_admission *polygon=receipt->polygons+j;
            if(!qa_source_save_u32(io,&polygon->ordinal)||!qa_source_save_bool(io,&polygon->reached)||
                !qa_source_save_bool(io,&polygon->admitted)||!qa_source_save_bool(io,&polygon->emitted)||
                (!polygon->reached&&(polygon->ordinal||polygon->admitted||polygon->emitted))||
                (polygon->emitted&&!polygon->admitted)||(row->admissions_ready&&!polygon->reached))
                return fail(io,"Saved component polygon has no actual reached admission");
        }
        if(!qa_source_save_count(io,&receipt->light_count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
        if(io->direction==QA_SOURCE_SAVE_READ) {
            receipt->lights=receipt->light_count?calloc(receipt->light_count,sizeof(*receipt->lights)):NULL;
            if(receipt->light_count&&!receipt->lights) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining saved component light receipts");
        }
        for(size_t j=0;j<receipt->light_count;++j) {
            remote_component_light_admission *light=receipt->lights+j;
            if(!qa_source_save_u32(io,&light->ordinal)||!qa_source_save_bool(io,&light->reached)||!qa_source_save_bool(io,&light->admitted)||
                (!light->reached&&(light->ordinal||light->admitted))||(light->admitted&&light->ordinal>=QA_Q3_SOURCE_LIGHT_CAPACITY))
                return fail(io,"Saved component light has no actual reached admission");
        }
    }
    return true;
}
static bool row_fields(qa_source_save_io *io,remote_component *row,const frontend_unified_components_refs *refs)
{
    if(!state(io,row->parent,&row->state)) return false;
    bool same=io->direction==QA_SOURCE_SAVE_WRITE&&row->frame&&row->frame==row->baseline;
    if(!frame(io,row,&row->baseline)||!qa_source_save_bool(io,&same)) return false;
    if(same) {
        if(!row->baseline) return fail(io,"Saved component aliases an absent baseline");
        row->frame=row->baseline;
    } else if(!frame(io,row,&row->frame)) return false;
    bool initialized=io->direction==QA_SOURCE_SAVE_WRITE&&row->initialized;
    bool physical=io->direction==QA_SOURCE_SAVE_WRITE&&row->frontend.owner;
    if(!qa_source_save_bool(io,&row->retired)||!qa_source_save_bool(io,&initialized)||
        !qa_source_save_bool(io,&physical)||(row->retired&&initialized)) return false;
    if(!physical) return (!initialized&&!row->scene)||fail(io,"Component physical constructor is absent from its continuation");
    if(!initialized&&!row->retired) return fail(io,"Uninitialized component retains an uncaptured physical constructor");
    if(!row->frame||!row->baseline) return fail(io,"Initialized component has no accepted Source frame");
    if(io->direction==QA_SOURCE_SAVE_WRITE&&row->submission_cycle) {
        uint64_t cycle=0;
        if(!qa_q3_source_scene_bank_cycle(row->submission_bank,&cycle)||cycle!=row->submission_cycle)
            q3remote_component_admissions_clear(row);
    }
    qa_vfs *files=NULL;
    const qa_product *product=qa_catalog_product(qa_executable_recipe_catalog(row->parent->recipe),row->state.mod->product);
    if(!product||!qa_executable_recipe_content(row->parent->recipe,product->identity,&files,&product,io->error)) return false;
    if(!resource(io,files,refs,&row->artifact,&row->acquisition)||!resource(io,files,refs,&row->gameplay,&row->gameplay_acquisition)||
        !qa_source_save_u64(io,&row->frontend_identity)||!row->frontend_identity||!qa_source_save_i32(io,&row->renderer_time)||row->renderer_time<0||
        !qa_source_save_bool(io,&row->scene_time_present)||!qa_source_save_f64(io,&row->scene_time_offset)||!isfinite(row->scene_time_offset)||
        (!row->scene_time_present&&row->scene_time_offset!=0)||
        !qa_source_save_bool(io,&row->previous_frame_present)||!qa_source_save_i32(io,&row->previous_frame_time)||row->previous_frame_time<0||
        (!row->previous_frame_present&&row->previous_frame_time!=0)||
        (row->previous_frame_present&&!row->scene_time_present)||
        !qa_source_save_u64(io,&row->draw_sequence)||!qa_source_save_bool(io,&row->advanced)||!qa_source_save_bool(io,&row->submitted)||
        !qa_source_save_bool(io,&row->pictures_present)||!qa_source_save_u64(io,&row->picture_sequence)||
        !qa_source_save_count(io,&row->pictures_submitted,SIZE_MAX)||!admissions(io,row)) return false;
    if(!row->pictures_present&&(row->picture_sequence||row->pictures_submitted))
        return fail(io,"Absent component picture continuation contains a reached cursor");
    if(io->direction==QA_SOURCE_SAVE_READ) {
        row->restore_pending=true;
        if(!profile(row,io->error)) return false;
    }
    if(io->direction==QA_SOURCE_SAVE_WRITE) {
        if(!refs->scene_current||!refs->scene_current(refs->context,row->frontend.owner,row->frontend_identity,io->error))
            return io->error&&io->error->code!=QA_OK?false:q3remote_component_fail(io->error,QA_ERROR_ARGUMENT,"Component capture lost its actual shared private renderer graph");
        if((!row->retired&&!application_q3_scene_checkpoint(row->scene,&row->saved_scene,io->error))||
            !qa_cvars_save_capture(row->cvars,&row->saved_cvars,io->error)||
            !qa_console_save_capture(row->console,io->session,&row->saved_console,io->error)) return false;
    }
    return blob(io,&row->saved_scene)&&(row->retired?!row->saved_scene.size:row->saved_scene.size!=0)&&blob(io,&row->saved_cvars)&&row->saved_cvars.size&&
        blob(io,&row->saved_console)&&row->saved_console.size;
}
static bool fields(qa_source_save_io *io,frontend_unified_components *owner,const frontend_unified_components_refs *refs)
{
    uint8_t magic[4]={'Q','U','C','P'}; uint32_t version=6,epoch=frontend_remote_unified_epoch(owner->replica);
    if(!qa_source_save_bytes(io,magic,4)||memcmp(magic,"QUCP",4)||!qa_source_save_u32(io,&version)||version!=6||
        !qa_source_save_u32(io,&epoch)||epoch!=frontend_remote_unified_epoch(owner->replica)||
        !qa_source_save_u64(io,&owner->revision)||owner->revision>QA_UNIFIED_SAFE_INTEGER||
        !qa_source_save_count(io,&owner->count,256)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        owner->rows=owner->count?calloc(owner->count,sizeof(*owner->rows)):NULL;
        if(owner->count&&!owner->rows) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining component collection topology");
    }
    for(size_t i=0;i<owner->count;++i) {
        if(io->direction==QA_SOURCE_SAVE_READ) {
            owner->rows[i]=calloc(1,sizeof(*owner->rows[i]));
            if(!owner->rows[i]) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining component activation owner");
            owner->rows[i]->parent=owner;
        }
        if(!row_fields(io,owner->rows[i],refs)) return false;
        if(owner->rows[i]->retired) return fail(io,"Active component roster contains a retired physical row");
        for(size_t k=0;k<i;++k) if(!strcmp(owner->rows[k]->state.provider,owner->rows[i]->state.provider))
            return fail(io,"Saved components duplicate an admitted provider");
    }
    size_t retired=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE) for(remote_component *row=owner->retired;row;row=row->retired_next) ++retired;
    if(!qa_source_save_count(io,&retired,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    remote_component **tail=&owner->retired;
    for(size_t i=0;i<retired;++i) {
        if(io->direction==QA_SOURCE_SAVE_READ) {
            *tail=calloc(1,sizeof(**tail));
            if(!*tail) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining actual inactive component registry owner");
            (*tail)->parent=owner;
        }
        if(!*tail||!row_fields(io,*tail,refs)||!(*tail)->retired) return fail(io,"Saved inactive component has no physical retirement witness");
        tail=&(*tail)->retired_next;
    }
    size_t physical=q3remote_component_physical_count(owner);
    for(size_t i=0;i<physical;++i) {
        const remote_component *row=q3remote_component_physical_at(owner,i);
        for(size_t k=0;k<i;++k) {
            const remote_component *prior=q3remote_component_physical_at(owner,k);
            if(row->frontend_identity&&row->frontend_identity==prior->frontend_identity)
                return fail(io,"Saved component physical namespaces alias an existing activation");
        }
    }
    bool prepared=io->direction==QA_SOURCE_SAVE_WRITE&&owner->prepared;
    if(!qa_source_save_bool(io,&prepared)||!prepared) return !io->failed;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        owner->prepared=calloc(1,sizeof(*owner->prepared));
        if(!owner->prepared) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining actual prepared component frame token");
        owner->prepared->owner=owner; owner->prepared->count=owner->count;
        owner->prepared->rows=owner->count?calloc(owner->count,sizeof(*owner->prepared->rows)):NULL;
        if(owner->count&&!owner->prepared->rows) return q3remote_component_fail(io->error,QA_ERROR_MEMORY,"Retaining actual prepared component frame rows");
    }
    qa_unified_document *input=io->direction==QA_SOURCE_SAVE_WRITE?(qa_unified_document *)owner->prepared->input:NULL;
    if(!document(io,&input,QA_UNIFIED_FRAME_DOCUMENT)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) { owner->prepared->owned_input=input; owner->prepared->input=input; }
    for(size_t i=0;i<owner->count;++i) if(!frame(io,owner->rows[i],owner->prepared->rows+i)||!owner->prepared->rows[i]) return false;
    return true;
}
bool frontend_unified_components_checkpoint(frontend_unified_components *owner,const frontend_unified_components_refs *refs,
    qa_buffer *out,qa_error *e)
{
    if(!owner||!refs||!refs->content||!out||out->data||out->size||owner->busy||owner->restoring||!frontend_unified_components_retained_current(owner))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component capture requires its actual returned replica and content graph");
    for(size_t i=0;i<q3remote_component_physical_count(owner);++i) {
        remote_component *row=q3remote_component_physical_at(owner,i);
        if(row->acquired||(row->scene&&!application_q3_scene_idle(row->scene))||
            (row->frontend.owner&&!row->frontend.idle(row->frontend.owner)))
            return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component capture retains genuine entered output");
    }
    if(owner->prepared&&!frontend_unified_components_frame_ready(owner->prepared,owner->prepared->input,e)) return false;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(owner->replica);
    qa_source_save_io io={0};
    bool ok=domain&&qa_source_save_writer(&io,qa_application_session(domain->application),e)&&fields(&io,owner,refs)&&qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    for(size_t i=0;i<q3remote_component_physical_count(owner);++i) {
        remote_component *row=q3remote_component_physical_at(owner,i);
        qa_buffer_free(&row->saved_scene); qa_buffer_free(&row->saved_cvars); qa_buffer_free(&row->saved_console);
    }
    return ok;
}
static bool console_identity(void *context,qa_console_save_identity kind,uint64_t saved,uint64_t *out,qa_error *e)
{
    remote_component *row=context;
    if((kind==QA_CONSOLE_SAVE_OWNER&&saved==row->services)||(kind==QA_CONSOLE_SAVE_CLIENT&&!saved)) { *out=saved; return true; }
    return q3remote_component_fail(e,QA_ERROR_FORMAT,"Component console identity differs from its genuine receiver");
}
static bool console_context(void *context,uint64_t registry,const qa_command_context *saved,qa_command_context *out,qa_error *e)
{
    remote_component *row=context;
    if(!registry||saved->session||saved->client||saved->owner!=row->services||saved->dialect!=QA_CONSOLE_Q3)
        return q3remote_component_fail(e,QA_ERROR_FORMAT,"Component console command lost its actual saved receiver");
    *out=*saved;
    if(saved->registry==registry) out->registry=qa_actors_identity(qa_session_actors(row->host.session));
    else if(saved->registry) out->registry=0;
    return true;
}
bool frontend_unified_components_restore_prepare(qa_frontend *frontend,frontend_remote_unified *replica,frontend_unified_media *media,
    const frontend_unified_components_refs *refs,qa_bytes bytes,frontend_unified_components **out,qa_error *e)
{
    if(!frontend||!replica||!media||!refs||!refs->content||!out||*out)
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component import requires its genuine detached replica and content graph");
    frontend_unified_components *owner=calloc(1,sizeof(*owner));
    if(!owner) return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining component import parent");
    owner->frontend=frontend; owner->replica=replica; owner->media=media; owner->recipe=frontend_remote_unified_recipe(replica);
    owner->restoring=true; *out=owner;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(replica); qa_source_save_io io={0};
    bool ok=domain&&owner->recipe==frontend_unified_media_recipe(media)&&qa_source_save_reader(&io,qa_application_session(domain->application),bytes,e)&&
        fields(&io,owner,refs)&&qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    for(size_t i=0;ok&&i<q3remote_component_physical_count(owner);++i) {
        remote_component *row=q3remote_component_physical_at(owner,i); if(!row->restore_pending) continue;
        ok=q3remote_component_open(row,e);
        qa_cvars_restore *cvars=NULL;
        if(ok) ok=qa_cvars_save_prepare(row->cvars,(qa_bytes){row->saved_cvars.data,row->saved_cvars.size},&cvars,e)&&qa_cvars_save_validate(cvars,e);
        if(ok) { ok=qa_cvars_save_commit(cvars,e); if(ok) cvars=NULL; }
        qa_cvars_save_abort(cvars);
        qa_console_save_resolvers console={row,console_identity,console_context};
        if(ok) ok=qa_console_save_restore(row->console,row->host.session,&console,(qa_bytes){row->saved_console.data,row->saved_console.size},e);
        if(ok) row->restore_consoles=true;
    }
    return ok;
}
bool frontend_unified_components_restore_finish(frontend_unified_components *owner,const frontend_unified_components_refs *refs,qa_error *e)
{
    if(!owner||!owner->restoring||!refs||owner->busy||!owner->events||
        !frontend_unified_components_retained_current(owner)||!frontend_unified_media_current(owner->media))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component activation requires its actual imported private media");
    if(!frontend_unified_components_events_bind(owner,owner->events,e)) return false;
    for(size_t i=0;i<q3remote_component_physical_count(owner);++i) {
        remote_component *row=q3remote_component_physical_at(owner,i); if(!row->restore_pending) continue;
        if(!row->restore_consoles||!refs->scene_current)
            return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component activation lacks its real private renderer graph association");
        if(!row->restore_frontend) {
            if(!refs->scene_current(refs->context,row->frontend.owner,row->frontend_identity,e)) return false;
            if(row->pictures_present) {
                size_t pictures=0;
                if(!row->advanced||row->picture_sequence!=row->draw_sequence||
                    !frontend_component_scene_picture_count(owner->frontend,row->frontend_identity,row->draw_sequence,&pictures,e)||
                    row->pictures_submitted>pictures)
                    return e&&e->code!=QA_OK?false:q3remote_component_fail(e,QA_ERROR_FORMAT,"Saved component picture cursor exceeds its actual imported output");
            }
            row->restore_frontend=true;
        }
        if(row->retired) {
            if(!q3remote_component_retire(row,e)) return false;
            row->restore_imported=true; row->restore_pending=false;
            qa_buffer_free(&row->saved_cvars); qa_buffer_free(&row->saved_console); continue;
        }
        if(!row->restore_imported) {
            if(!application_q3_scene_restore(row->scene,(qa_bytes){row->saved_scene.data,row->saved_scene.size},e)) return false;
            row->restore_imported=true;
        }
        if(!application_q3_scene_finish_restore(row->scene,e)) return false;
        row->initialized=true; row->restore_pending=false;
        qa_buffer_free(&row->saved_scene); qa_buffer_free(&row->saved_cvars);
        qa_buffer_free(&row->saved_console);
    }
    owner->restoring=false; return frontend_unified_components_retained_current(owner)||
        q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Restored component collection lost its actual admitted replica");
}
bool frontend_unified_components_prepared(frontend_unified_components *owner,frontend_unified_component_frame **out,
    const qa_unified_document **input,qa_error *e)
{
    if(!owner||!out||*out||!input||owner->restoring||
        !(frontend_unified_components_current(owner)||frontend_unified_components_retained_current(owner)))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component token read requires its actual installed continuation");
    *out=owner->prepared; *input=owner->prepared?owner->prepared->input:NULL; return true;
}
