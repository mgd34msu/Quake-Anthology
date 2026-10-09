#ifndef QA_BOTS_CATALOG_H
#define QA_BOTS_CATALOG_H
#include "qa/bots_memory.h"
#include "qa/vfs.h"

typedef struct qa_bot_catalog qa_bot_catalog;
typedef struct qa_bot_catalog_clock {
    int32_t time,start_time,intermission_time,game_type,max_clients;
} qa_bot_catalog_clock;
typedef struct qa_bot_catalog_client {
    const char *name;
    int32_t team;
    bool has_player,connected,bot;
} qa_bot_catalog_client;
typedef struct qa_bot_catalog_add_request {
    const char *definition_name,*public_name,*team;
    float skill;
    int32_t delay_ms;
} qa_bot_catalog_add_request;
/* Source text uses Latin-1 bytes at this internal game boundary. Public UTF-8
 * names enter through lookup_utf8; engine adapters perform their own encoding. */
typedef struct qa_bot_catalog_services {
    void *context;
    qa_session *session;
    qa_vfs *files;
    qa_bot_source_memory memory;
    bool (*print)(void *,const char *,qa_error *);
    qa_cvars *cvars;
    bool (*register_cvar)(void *,const char *,const char *,uint32_t,qa_error *);
    bool (*set_cvar)(void *,const char *,const char *,qa_error *);
    bool (*server_info)(void *,char *,size_t,qa_error *);
    bool (*clock)(void *,qa_bot_catalog_clock *,qa_error *);
    bool (*client)(void *,int32_t,qa_bot_catalog_client *,qa_error *);
    bool (*allocate_client)(void *,int32_t *,qa_error *);
    bool (*choose_team)(void *,int32_t,int32_t *,qa_error *);
    bool (*activate)(void *,int32_t,qa_error *);
    bool (*userinfo)(void *,int32_t,char *,size_t,qa_error *);
    bool (*set_userinfo)(void *,int32_t,const char *,qa_error *);
    bool (*connect)(void *,int32_t,bool,bool,bool *,qa_error *);
    bool (*begin)(void *,int32_t,qa_error *);
    bool (*reset_podium)(void *,qa_error *);
    bool (*insert_command)(void *,const char *,qa_error *);
    bool (*append_command)(void *,const char *,qa_error *);
    bool (*server_command)(void *,int32_t,const char *,qa_error *);
    bool (*random)(void *,float *,qa_error *);
} qa_bot_catalog_services;
bool qa_bot_catalog_create(const qa_bot_catalog_services *,qa_bot_catalog **,qa_error *);
bool qa_bot_catalog_destroy(qa_bot_catalog *,qa_error *);
bool qa_bot_catalog_can_destroy(const qa_bot_catalog *);
bool qa_bot_catalog_initialize(qa_bot_catalog *,bool restart,qa_error *);
uint32_t qa_bot_catalog_bot_count(const qa_bot_catalog *);
uint32_t qa_bot_catalog_arena_count(const qa_bot_catalog *);
/* Owned NUL-terminated info bytes; empty output and found=false for a miss. */
bool qa_bot_catalog_bot_number(qa_bot_catalog *,int32_t,qa_buffer *,bool *,qa_error *);
bool qa_bot_catalog_bot_name(qa_bot_catalog *,const char *,qa_buffer *,bool *,qa_error *);
bool qa_bot_catalog_bot_name_utf8(qa_bot_catalog *,const char *,qa_buffer *,bool *,qa_error *);
bool qa_bot_catalog_arena_map(qa_bot_catalog *,const char *,qa_buffer *,bool *,qa_error *);
bool qa_bot_catalog_console(qa_bot_catalog *,const char *const *,size_t,qa_error *);
/* Direct G_AddBot entry. Empty public_name uses the definition's funname/name.
 * The first entry takes source bytes; the second decodes public UTF-8 text. */
bool qa_bot_catalog_add(qa_bot_catalog *,const qa_bot_catalog_add_request *,qa_error *);
bool qa_bot_catalog_add_utf8(qa_bot_catalog *,const qa_bot_catalog_add_request *,qa_error *);
bool qa_bot_catalog_check_spawn(qa_bot_catalog *,qa_error *);
bool qa_bot_catalog_remove_queued_begin(qa_bot_catalog *,int32_t,qa_error *);
bool qa_bot_catalog_capture(qa_bot_catalog *,qa_buffer *,qa_error *);
bool qa_bot_catalog_restore(qa_bot_catalog *,qa_bytes,qa_error *);
#endif
