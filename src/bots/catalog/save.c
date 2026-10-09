#include "internal.h"

static bool signature(qa_source_save_io *io) {
    static const uint8_t expected[8]={'Q','A','B','C','A','T',0,0};
    uint8_t magic[8];memcpy(magic,expected,sizeof(magic));
    return qa_source_save_bytes(io,magic,sizeof(magic)) &&
        (!memcmp(magic,expected,sizeof(magic))?true:
            bot_catalog_fail(io->error,QA_ERROR_FORMAT,"unsupported source game bot catalogue continuation"));
}
static bool infos_fields(qa_source_save_io *io,bot_catalog_infos *infos) {
    if(!qa_source_save_u32(io,&infos->count) || !qa_source_save_u32(io,&infos->extent) ||
       infos->count>infos->extent || infos->extent>BOT_CATALOG_INFOS) return false;
    for(uint32_t i=0;i<infos->extent;++i) {
        qa_bot_source_record *record=&infos->records[i];
        if(!qa_source_save_u32(io,&record->offset) || !qa_source_save_u32(io,&record->length) ||
           record->offset>QA_BOT_GAME_MEMORY_BYTES || record->length>QA_BOT_GAME_MEMORY_BYTES-record->offset) return false;
    }
    return true;
}
static bool parser_fields(qa_source_save_io *io,qa_common_parser *parser) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t token=parser->token_length,name=parser->name_length;
    if(!qa_source_save_count(io,&token,QA_COMMON_TOKEN_CAPACITY) ||
       !qa_source_save_bytes(io,parser->token,token) ||
       !qa_source_save_count(io,&name,QA_COMMON_TOKEN_CAPACITY) ||
       !qa_source_save_bytes(io,parser->name,name) || !qa_source_save_i32(io,&parser->line)) return false;
    if(reading) {
        qa_common_parser_state state={.token={(uint8_t *)parser->token,token},
            .name={(uint8_t *)parser->name,name},.line=parser->line};
        if(!qa_common_parser_restore(parser,&state,io->error)) return false;
    }
    return true;
}
static bool fields(qa_source_save_io *io,qa_bot_catalog *c) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!infos_fields(io,&c->bots) || !infos_fields(io,&c->arenas)) return false;
    for(size_t i=0;i<BOT_CATALOG_QUEUE;++i)
        if(!qa_source_save_i32(io,&c->queue[i].client) || !qa_source_save_f64(io,&c->queue[i].time) ||
           (reading && !isfinite(c->queue[i].time))) return false;
    if(!qa_source_save_bool(io,&c->minimum_registered)) return false;
    if(c->minimum_registered) {
        size_t length=reading?0:c->minimum_length;
        if(!qa_source_save_count(io,&length,255) || !qa_source_save_bytes(io,c->minimum_value,length) ||
           !qa_source_save_f64(io,&c->minimum_numeric) ||
           !qa_source_save_i32(io,&c->minimum_integer) || !qa_source_save_u64(io,&c->minimum_modification) ||
           (reading && !isfinite(c->minimum_numeric))) return false;
        if(reading) {c->minimum_value[length]=0;c->minimum_length=length;}
    }
    return qa_source_save_f64(io,&c->check_minimum_time) &&
        (!reading || isfinite(c->check_minimum_time)) && parser_fields(io,&c->parser);
}
bool qa_bot_catalog_capture(qa_bot_catalog *c,qa_buffer *out,qa_error *e) {
    if(!c || !out || out->data || c->calls) return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"source bot catalogue capture requires its idle actual owner");
    qa_source_save_io io={0};++c->calls;
    bool okay=qa_source_save_writer(&io,c->services.session,e) && signature(&io) && fields(&io,c) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);--c->calls;return okay;
}
bool qa_bot_catalog_restore(qa_bot_catalog *c,qa_bytes bytes,qa_error *e) {
    if(!c || c->calls) return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"source bot catalogue restore requires its idle actual owner");
    qa_bot_catalog *scratch=calloc(1,sizeof(*scratch));
    if(!scratch) return bot_catalog_fail(e,QA_ERROR_MEMORY,"preparing source bot catalogue continuation");
    scratch->services=c->services;qa_source_save_io io={0};++c->calls;
    bool okay=qa_source_save_reader(&io,c->services.session,bytes,e) && signature(&io) && fields(&io,scratch) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);--c->calls;
    if(okay) {bot_catalog_bind_controls(scratch);*c=*scratch;}
    free(scratch);return okay;
}
