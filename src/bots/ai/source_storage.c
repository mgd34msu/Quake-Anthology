#include "internal.h"
#include "source_storage.h"
#include "source_alias.h"
#include "source_goal_record.h"
#include "source_activation.h"

static bool span(qa_bots *b,bot_ai_state *s,uint32_t offset,uint32_t size,uint8_t **bytes,qa_error *e) {
    if(!s || offset>QA_BOT_STATE_SOURCE_BYTES || size>QA_BOT_STATE_SOURCE_BYTES-offset) {
        bot_ai_fail(e,"BotState field exceeds its actual source allocation");return false;
    }
    if(!s->source_span.data && !bot_ai_source_alias_bind(b,s,e)) return false;
    if(s->source_span.length!=QA_BOT_STATE_SOURCE_BYTES) {
        bot_ai_fail(e,"BotState field has no complete actual byte span");return false;
    }
    *bytes=s->source_span.data+offset;return true;
}

bool bot_ai_storage_text(qa_bots *b,bot_ai_state *s,uint32_t offset,const char **out,qa_error *e) {
    if(!out || !s || s->source_record.length!=QA_BOT_STATE_SOURCE_BYTES ||
       s->source_record.offset>QA_BOT_GAME_MEMORY_BYTES-QA_BOT_STATE_SOURCE_BYTES ||
       offset>=s->source_record.length) {
        bot_ai_fail(e,"BotState text has no actual GAME allocation");return false;
    }
    uint32_t start=s->source_record.offset+offset;
    /* Source CString reads may continue beyond the declared field and record. */
    qa_bot_source_span text;
    if(!qa_bot_source_record_span(&b->services.memory,
        (qa_bot_source_record){start,QA_BOT_GAME_MEMORY_BYTES-start},&text,e)) return false;
    if(!memchr(text.data,0,text.length)) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"BotState string reads beyond its real GAME pool");return false;
    }
    *out=(const char *)text.data;return true;
}

bool bot_ai_storage_i32(qa_bots *b,bot_ai_state *s,uint32_t offset,int32_t *value,bool write,qa_error *e) {
    uint8_t *bytes;if(!value || !span(b,s,offset,4,&bytes,e)) return false;
    if(write) bot_source_i32_write(bytes,*value);else *value=bot_source_i32_read(bytes);
    return true;
}
bool bot_ai_storage_u32(qa_bots *b,bot_ai_state *s,uint32_t offset,uint32_t *value,bool write,qa_error *e) {
    uint8_t *bytes;if(!value || !span(b,s,offset,4,&bytes,e)) return false;
    if(write) bot_source_word_write(bytes,*value);else *value=bot_source_word_read(bytes);
    return true;
}
bool bot_ai_storage_f32(qa_bots *b,bot_ai_state *s,uint32_t offset,float *value,bool write,qa_error *e) {
    uint8_t *bytes;if(!value || !span(b,s,offset,4,&bytes,e)) return false;
    if(write) bot_source_f32_write(bytes,*value);else *value=bot_source_f32_read(bytes);
    return true;
}
bool bot_ai_storage_bool(qa_bots *b,bot_ai_state *s,uint32_t offset,bool *value,bool write,qa_error *e) {
    uint8_t *bytes;if(!value || !span(b,s,offset,4,&bytes,e)) return false;
    if(write) bot_source_word_write(bytes,*value?1u:0u);else *value=bot_source_word_read(bytes)!=0;
    return true;
}
bool bot_ai_storage_vec3(qa_bots *b,bot_ai_state *s,uint32_t offset,qa_vec3 *value,bool write,qa_error *e) {
    uint8_t *bytes;if(!value || !span(b,s,offset,12,&bytes,e)) return false;
    if(write) bot_source_vec3_write(bytes,*value);else *value=bot_source_vec3_read(bytes);
    return true;
}
bool bot_ai_storage_goal(qa_bots *b,bot_ai_state *s,uint32_t offset,qa_bot_goal *value,bool write,qa_error *e) {
    uint8_t *bytes;if(!value || !span(b,s,offset,56,&bytes,e)) return false;
    if(write) {
        bot_source_vec3_write(bytes,value->origin);bot_source_i32_write(bytes+12,value->area);
        bot_source_vec3_write(bytes+16,value->mins);bot_source_vec3_write(bytes+28,value->maxs);
        bot_source_i32_write(bytes+40,value->entity);bot_source_i32_write(bytes+44,value->number);
        bot_source_i32_write(bytes+48,value->flags);bot_source_i32_write(bytes+52,value->item_info);
    } else {
        value->origin=bot_source_vec3_read(bytes);value->area=bot_source_i32_read(bytes+12);
        value->mins=bot_source_vec3_read(bytes+16);value->maxs=bot_source_vec3_read(bytes+28);
        value->entity=bot_source_i32_read(bytes+40);value->number=bot_source_i32_read(bytes+44);
        value->flags=bot_source_i32_read(bytes+48);value->item_info=bot_source_i32_read(bytes+52);
    }
    return true;
}
bool bot_ai_storage_settings(qa_bots *b,bot_ai_state *s,const qa_bot_admission *a,qa_error *e) {
    float skill=a->skill;
    return qa_bot_source_record_text_write(&b->services.memory,s->source_record,
            QA_BOT_SOURCE_CHARACTER_FILE,144,a->character_file,e) &&
        bot_ai_storage_f32(b,s,QA_BOT_SOURCE_SKILL,&skill,true,e) &&
        qa_bot_source_record_text_write(&b->services.memory,s->source_record,
            QA_BOT_SOURCE_TEAM,144,a->team?a->team:"",e);
}
bool bot_ai_storage_publish(qa_bots *b,bot_ai_state *s,qa_error *e) {
    bool inuse=true;int32_t client=s->view.source_client,entity=s->view.entity,count=4;
    float time=b->time;
    return bot_ai_storage_bool(b,s,QA_BOT_SOURCE_INUSE,&inuse,true,e) &&
        bot_ai_storage_i32(b,s,QA_BOT_SOURCE_CLIENT,&client,true,e) &&
        bot_ai_storage_i32(b,s,QA_BOT_SOURCE_ENTITY,&entity,true,e) &&
        bot_ai_storage_i32(b,s,QA_BOT_SOURCE_SETUP_COUNT,&count,true,e) &&
        bot_ai_storage_f32(b,s,QA_BOT_SOURCE_ENTER_TIME,&time,true,e);
}

static uint32_t activation_offset(uint32_t index) {
    return QA_BOT_SOURCE_ACTIVATION_HEAP+index*QA_BOT_SOURCE_ACTIVATION_BYTES;
}
static uint32_t activation_address(const bot_ai_state *s,uint32_t index) {
    return s->source_record.offset+activation_offset(index);
}
static bool activation_index(const bot_ai_state *s,uint32_t address,uint32_t *index,qa_error *e) {
    uint32_t first=activation_address(s,0);
    if(address<first || address-first>=QA_BOT_SOURCE_ACTIVATION_COUNT*QA_BOT_SOURCE_ACTIVATION_BYTES ||
       (address-first)%QA_BOT_SOURCE_ACTIVATION_BYTES) {
        bot_ai_fail(e,"BotState activation link does not name its actual GAME heap row");return false;
    }
    *index=(address-first)/QA_BOT_SOURCE_ACTIVATION_BYTES;return true;
}
bool bot_ai_activation_validate(const bot_ai_state *s,qa_error *e) {
    if(!s || !s->source_span.data || s->source_span.length!=QA_BOT_STATE_SOURCE_BYTES ||
       s->source_record.length!=QA_BOT_STATE_SOURCE_BYTES ||
       s->source_record.offset>QA_BOT_GAME_MEMORY_BYTES-QA_BOT_STATE_SOURCE_BYTES)
        return bot_ai_fail(e,"BotState activation heap has no complete actual GAME allocation");
    for(uint32_t i=0;i<QA_BOT_SOURCE_ACTIVATION_COUNT;++i) {
        const uint8_t *p=s->source_span.data+activation_offset(i);
        int32_t count=bot_source_i32_read(p+232);uint32_t next=bot_source_word_read(p+240),index;
        if(count<0 || count>32) return bot_ai_fail(e,"BotState activation area count exceeds its actual row");
        if(next && !activation_index(s,next,&index,e)) return false;
    }
    bool seen[QA_BOT_SOURCE_ACTIVATION_COUNT]={0};
    uint32_t address=bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_ACTIVATION_STACK);
    while(address) {
        uint32_t index;if(!activation_index(s,address,&index,e)) return false;
        const uint8_t *p=s->source_span.data+activation_offset(index);
        if(seen[index] || !bot_source_word_read(p))
            return bot_ai_fail(e,"BotState activation stack has a cycle or inactive row");
        seen[index]=true;address=bot_source_word_read(p+240);
    }
    return true;
}
bool bot_ai_activation_top(const bot_ai_state *s,uint32_t *index,bool *found,qa_error *e) {
    if(!index || !found || !bot_ai_activation_validate(s,e)) return false;
    uint32_t address=bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_ACTIVATION_STACK);
    *found=address!=0;*index=0;
    return !address || activation_index(s,address,index,e);
}
qa_bot_source_activation bot_ai_activation_read(const bot_ai_state *s,uint32_t index) {
    const uint8_t *p=s->source_span.data+activation_offset(index);
    qa_bot_source_activation row={.inuse=bot_source_word_read(p)!=0,
        .goal=bot_ai_goal_record(s,activation_offset(index)+4),
        .time=bot_source_f32_read(p+60),.start_time=bot_source_f32_read(p+64),
        .just_used_time=bot_source_f32_read(p+68),.shoot=bot_source_word_read(p+72)!=0,
        .weapon=bot_source_i32_read(p+76),.target=bot_source_vec3_read(p+80),
        .origin=bot_source_vec3_read(p+92),.area_count=bot_source_i32_read(p+232),
        .areas_disabled=bot_source_word_read(p+236)!=0,.next=bot_source_word_read(p+240)};
    for(uint32_t i=0;i<32;++i) row.areas[i]=bot_source_i32_read(p+104+i*4);
    return row;
}
void bot_ai_activation_time_set(bot_ai_state *s,uint32_t index,float time) {
    bot_source_f32_write(s->source_span.data+activation_offset(index)+60,time);
}
void bot_ai_activation_weapon_set(bot_ai_state *s,uint32_t index,int32_t weapon) {
    bot_source_i32_write(s->source_span.data+activation_offset(index)+76,weapon);
}
bool bot_ai_activation_contains(const bot_ai_state *s,int32_t entity,float now,bool *found,qa_error *e) {
    if(!found || !bot_ai_activation_validate(s,e)) return false;
    *found=false;
    uint32_t address=bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_ACTIVATION_STACK);
    while(address) {
        uint32_t index;if(!activation_index(s,address,&index,e)) return false;
        qa_bot_source_activation row=bot_ai_activation_read(s,index);
        if(!(row.time<now) && row.goal.entity==entity) {*found=true;return true;}
        address=row.next;
    }
    for(uint32_t i=0;i<QA_BOT_SOURCE_ACTIVATION_COUNT;++i) {
        qa_bot_source_activation row=bot_ai_activation_read(s,i);
        if(!row.inuse && row.goal.entity==entity && row.just_used_time>now-2) {*found=true;break;}
    }
    return true;
}
bool bot_ai_activation_push(bot_ai_state *s,const qa_bot_source_activation *row,float now,bool *pushed,qa_error *e) {
    if(!row || !pushed || !bot_ai_activation_validate(s,e)) return false;
    *pushed=false;
    if(row->area_count<0 || row->area_count>32) return bot_ai_fail(e,"Activation producer area count exceeds actual heap row");
    int32_t best=-1;float best_time=now+9999;
    for(uint32_t i=0;i<QA_BOT_SOURCE_ACTIVATION_COUNT;++i) {
        const uint8_t *p=s->source_span.data+activation_offset(i);
        float used=bot_source_f32_read(p+68);
        if(!bot_source_word_read(p) && used<best_time) {best_time=used;best=(int32_t)i;}
    }
    if(best<0) return true;
    uint32_t index=(uint32_t)best;uint8_t *p=s->source_span.data+activation_offset(index);
    bot_source_word_write(p,1);bot_ai_goal_record_set(s,activation_offset(index)+4,row->goal);
    bot_source_f32_write(p+60,row->time);bot_source_f32_write(p+64,row->start_time);
    bot_source_f32_write(p+68,row->just_used_time);bot_source_word_write(p+72,row->shoot?1u:0u);
    bot_source_i32_write(p+76,row->weapon);bot_source_vec3_write(p+80,row->target);bot_source_vec3_write(p+92,row->origin);
    for(uint32_t i=0;i<32;++i) bot_source_i32_write(p+104+i*4,row->areas[i]);
    bot_source_i32_write(p+232,row->area_count);bot_source_word_write(p+236,row->areas_disabled?1u:0u);
    bot_source_word_write(p+240,bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_ACTIVATION_STACK));
    bot_source_word_write(s->source_span.data+QA_BOT_SOURCE_ACTIVATION_STACK,activation_address(s,index));
    *pushed=true;return true;
}
bool bot_ai_activation_pop(qa_bots *b,bot_ai_state *s,qa_error *e) {
    uint32_t index;bool found;if(!bot_ai_activation_top(s,&index,&found,e)) return false;
    if(!found) return true;
    qa_bot_source_activation row=bot_ai_activation_read(s,index);
    if(row.areas_disabled) {
        qa_bot_navigation *nav=qa_bot_runtime_navigation(b->runtime,(int32_t)s->view.client);
        for(int32_t i=0;i<row.area_count;++i) {
            bool previous;
            if(!qa_bot_navigation_enable(nav,(uint32_t)row.areas[i],true,&previous,e)) return false;
        }
        bot_source_word_write(s->source_span.data+activation_offset(index)+236,0);
    }
    uint8_t *p=s->source_span.data+activation_offset(index);
    bot_source_word_write(p,0);bot_source_f32_write(p+68,b->time);
    bot_source_word_write(s->source_span.data+QA_BOT_SOURCE_ACTIVATION_STACK,bot_source_word_read(p+240));
    return true;
}
bool bot_ai_activation_clear(qa_bots *b,bot_ai_state *s,qa_error *e) {
    uint32_t index;bool found;
    while(true) {
        if(!bot_ai_activation_top(s,&index,&found,e)) return false;
        if(!found) return true;
        if(!bot_ai_activation_pop(b,s,e)) return false;
    }
}
