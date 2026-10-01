#ifndef QA_BOT_CATALOG_INTERNAL_H
#define QA_BOT_CATALOG_INTERNAL_H
#include "qa/bots_catalog.h"
#include "qa/common_parse.h"
#include "qa/text.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BOT_CATALOG_INFOS=1024,BOT_CATALOG_TEXT=8192,BOT_CATALOG_INFO=1024,BOT_CATALOG_QUEUE=16 };
typedef struct bot_catalog_infos {
    qa_bot_source_record records[BOT_CATALOG_INFOS];
    uint32_t count,extent;
} bot_catalog_infos;
struct qa_bot_catalog {
    qa_bot_catalog_services services;
    bot_catalog_infos bots,arenas;
    struct {int32_t client;double time;} queue[BOT_CATALOG_QUEUE];
    qa_common_parser parser;
    char minimum_value[256];
    size_t minimum_length;
    double minimum_numeric,check_minimum_time;
    int32_t minimum_integer;
    uint64_t minimum_modification;
    bool minimum_registered;
    size_t calls;
};
bool bot_catalog_fail(qa_error *,qa_status,const char *);
bool bot_catalog_enter(qa_bot_catalog *,qa_error *);
bool bot_catalog_equal(const char *,const char *);
bool bot_catalog_info_value(const char *,const char *,char *,size_t,qa_error *);
bool bot_catalog_info_set(qa_bot_catalog *,char [1024],const char *,const char *,qa_error *);
bool bot_catalog_text(qa_bot_catalog *,qa_bot_source_record,qa_buffer *,qa_error *);
bool bot_catalog_write(qa_bot_catalog *,qa_bot_source_record,const char *,qa_error *);
bool bot_catalog_lookup(qa_bot_catalog *,bot_catalog_infos *,const char *,const char *,qa_buffer *,bool *,qa_error *);
bool bot_catalog_cvar(qa_bot_catalog *,const char *,char *,size_t,int32_t *,qa_error *);
bool bot_catalog_minimum_update(qa_bot_catalog *,qa_error *);
bool bot_catalog_spawn_list(qa_bot_catalog *,const char *,int32_t,qa_error *);
bool bot_catalog_minimum_check(qa_bot_catalog *,qa_error *);
bool bot_catalog_latin1(const char *,char *,size_t,qa_error *);
bool bot_catalog_utf8(const char *,qa_buffer *,qa_error *);
float bot_catalog_atof(const char *);
int32_t bot_catalog_atoi(const char *);
int32_t bot_catalog_word(uint32_t);
#endif
