#include "internal.h"
#include "source_storage.h"
#include "source_alias.h"

static bool span(qa_bots *b,bot_ai_state *s,uint32_t offset,uint32_t size,uint8_t **bytes,qa_error *e) {
    if(!s || offset>QA_BOT_STATE_SOURCE_BYTES || size>QA_BOT_STATE_SOURCE_BYTES-offset)
        return bot_ai_fail(e,"BotState field exceeds its actual source allocation");
    if(!s->source_span.data && !bot_ai_source_alias_bind(b,s,e)) return false;
    if(s->source_span.length!=QA_BOT_STATE_SOURCE_BYTES)
        return bot_ai_fail(e,"BotState field has no complete actual byte span");
    *bytes=s->source_span.data+offset;return true;
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
