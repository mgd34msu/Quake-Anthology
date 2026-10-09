#include "qa/world.h"
#include "qa/binary.h"
#include "qa/text.h"

#include <limits.h>
#include <math.h>
#include <string.h>

qa_entity_vector_field qa_entity_vector_bytes(const void *bytes)
{
    const uint8_t *p=bytes;
    return p?(qa_entity_vector_field){{p,p+4,p+8}}:(qa_entity_vector_field){0};
}

static qa_vec3 vector(qa_entity_vector_field field)
{
    return qa_v3(field.word[0]?qa_load_f32le(field.word[0]):0,
                 field.word[1]?qa_load_f32le(field.word[1]):0,
                 field.word[2]?qa_load_f32le(field.word[2]):0);
}

static uint64_t word(qa_entity_scalar_field field)
{
    switch(field.encoding) {
    case QA_ENTITY_NO_FIELD: return 0;
    case QA_ENTITY_U8: return *field.bytes;
    case QA_ENTITY_U64_LE: return qa_load_u64le(field.bytes);
    case QA_ENTITY_F32_LE:
        return (uint32_t)qa_source_float_to_i32(qa_load_f32le(field.bytes));
    case QA_ENTITY_I32_LE: case QA_ENTITY_U32_LE: return qa_load_u32le(field.bytes);
    }
    return 0;
}

static bool fail(qa_error *error,const char *message)
{ qa_error_set(error,QA_ERROR_FORMAT,0,"%s",message); return false; }

static bool reference(const qa_entity_references *table,qa_entity_scalar_field field,
                      qa_actor_reference *out,qa_error *error)
{
    qa_actor_reference value={0};
    if(field.encoding==QA_ENTITY_NO_FIELD || !table) { *out=value; return true; }
    uint64_t number=word(field);
    if((table->zero_is_none && !number) ||
       (table->has_none_number && number==(uint32_t)table->none_number)) {
        *out=value; return true;
    }
    if(table->has_world_number && number==(uint32_t)table->world_number) {
        *out=table->world; return true;
    }
    uint32_t count=table->count?*table->count:table->capacity;
    if(number<table->base || !table->stride ||
       (number-table->base)%table->stride ||
       (number-table->base)/table->stride>=count) {
        if(table->invalid_is_none) { *out=value; return true; }
        return fail(error,"Entity reference leaves its module slot table");
    }
    uint32_t slot=(uint32_t)((number-table->base)/table->stride);
    value=qa_actor_reference_source(table->owner,slot);
    if(table->slots) {
        const uint8_t *row=table->slots+(size_t)slot*table->slot_stride;
        qa_actor_id actor;
        memcpy(&actor,row+table->actor_offset,sizeof(actor));
        bool borrowed=false;
        if(table->foreign_owner) {
            const qa_actor_record *record=qa_actors_get(table->actors,actor);
            borrowed=record && record->owner!=table->owner;
        } else if(table->kind_encoding!=QA_ENTITY_NO_FIELD) {
            qa_entity_scalar_field kind={row+table->kind_offset,table->kind_encoding};
            borrowed=word(kind)==table->borrowed_kind;
        }
        if(borrowed) value=qa_actor_reference_lifetime(actor);
    }
    *out=value; return true;
}

bool qa_entity_body_read(const qa_entity_body_fields *fields,qa_entity_pose pose,
                         qa_entity_body_components components,
                         qa_body_state *out,qa_error *error)
{
    qa_body_state body={.origin=vector(fields->pose[pose].origin),
        .angles=vector(fields->pose[pose].angles),
        .bounds={vector(fields->minimum),vector(fields->maximum)}};
    if(components==QA_ENTITY_BODY_ALL) {
        body.velocity=vector(fields->velocity);
        if(!reference(fields->references,fields->ground,&body.ground,error)) return false;
    }
    *out=body; return true;
}

static bool flags(qa_entity_scalar_field field,uint32_t *out,qa_error *error)
{
    if(field.encoding==QA_ENTITY_F32_LE) {
        float value=qa_load_f32le(field.bytes);
        if(!isfinite(value) || (double)value<INT32_MIN || (double)value>INT32_MAX)
            return fail(error,"Nonfinite or out-of-range entity flags");
    }
    *out=(uint32_t)word(field); return true;
}

bool qa_entity_collision_read(const qa_entity_collision_fields *fields,bool linking,
                              qa_entity_collision_components components,
                              qa_actor_collision *out,qa_error *error)
{
    float q1_solid=0;
    uint32_t q2_solid=0;
    qa_collision_role role=QA_COLLISION_SOLID;
    switch(fields->family) {
    case QA_COLLISION_Q1:
        q1_solid=fields->solid.encoding==QA_ENTITY_F32_LE?
            qa_load_f32le(fields->solid.bytes):(float)(int32_t)word(fields->solid);
        if(!isfinite(q1_solid)) return fail(error,"Nonfinite entity solid");
        if(q1_solid==1) role=QA_COLLISION_TRIGGER;
        break;
    case QA_COLLISION_Q2:
        q2_solid=(uint32_t)word(fields->solid);
        if(q2_solid>3) return fail(error,"Invalid entity solid");
        if(q2_solid==1) role=QA_COLLISION_TRIGGER;
        break;
    case QA_COLLISION_Q3: break;
    }
    if(components==QA_ENTITY_COLLISION_ROLE) { out->role=role; return true; }
    uint32_t bits=0;
    if(!flags(fields->flags,&bits,error)) return false;
    qa_actor_collision value={.family=fields->family,.shape=QA_SHAPE_BOX,.role=role};
    uint32_t index=(uint32_t)word(fields->model);
    bool pending=false;
    switch(fields->family) {
    case QA_COLLISION_Q1: {
        value.contents=q1_solid==0 || q1_solid==1?0:-2;
        value.monster=(bits&32u)!=0;
        value.q1_corpse=q1_solid==5 && fields->rerelease;
        value.inline_model=q1_solid==4;
        float model=fields->model.encoding==QA_ENTITY_F32_LE?
            qa_load_f32le(fields->model.bytes):(float)(int32_t)index;
        if(value.inline_model && !isfinite(model))
            return fail(error,"Nonfinite entity brush model");
        pending=value.inline_model && model==0 && linking;
        break;
    }
    case QA_COLLISION_Q2:
        value.contents=qa_collision_q2_source_contents(q2_solid,bits,fields->rerelease);
        value.monster=(bits&4u)!=0; value.dead_monster=(bits&2u)!=0;
        value.inline_model=q2_solid==3;
        break;
    case QA_COLLISION_Q3:
        value.model=index;
        value.shape=(bits&1024u)?QA_SHAPE_CAPSULE:QA_SHAPE_BOX;
        value.inline_model=word(fields->brush_model)!=0;
        value.contents=(int32_t)word(fields->contents);
        value.has_q3_owner=true;
        value.q3_entity_number=fields->entity_number;
        value.q3_owner_number=(int32_t)word(fields->owner);
        break;
    }
    if(pending) value.inline_model=false;
    else if(value.inline_model) {
        if(fields->models) {
            if(index>=fields->models->count || !fields->models->entries[index].present)
                return fail(error,"Entity brush model is not precached");
            const qa_entity_model_field *model=fields->models->entries+index;
            value.model=model->model; value.model_geometry=model->geometry;
        } else value.model=index;
    }
    if(!reference(fields->references,fields->owner,&value.owner,error)) return false;
    *out=value; return true;
}
