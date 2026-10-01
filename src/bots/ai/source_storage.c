#include "internal.h"
#include "source_storage.h"

bool bot_ai_storage_i32(qa_bots *b,bot_ai_state *s,uint32_t offset,int32_t *value,bool write,qa_error *e) {
    return qa_bot_source_record_i32(&b->services.memory,s->source_record,offset,value,write,e);
}
bool bot_ai_storage_u32(qa_bots *b,bot_ai_state *s,uint32_t offset,uint32_t *value,bool write,qa_error *e) {
    int32_t bits=0;if(write) memcpy(&bits,value,sizeof(bits));
    if(!bot_ai_storage_i32(b,s,offset,&bits,write,e)) return false;
    if(!write) memcpy(value,&bits,sizeof(bits));return true;
}
bool bot_ai_storage_f32(qa_bots *b,bot_ai_state *s,uint32_t offset,float *value,bool write,qa_error *e) {
    return qa_bot_source_record_f32(&b->services.memory,s->source_record,offset,value,write,e);
}
bool bot_ai_storage_bool(qa_bots *b,bot_ai_state *s,uint32_t offset,bool *value,bool write,qa_error *e) {
    return qa_bot_source_record_bool(&b->services.memory,s->source_record,offset,value,write,e);
}
bool bot_ai_storage_vec3(qa_bots *b,bot_ai_state *s,uint32_t offset,qa_vec3 *value,bool write,qa_error *e) {
    return qa_bot_source_record_vec3(&b->services.memory,s->source_record,offset,value,write,e);
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
