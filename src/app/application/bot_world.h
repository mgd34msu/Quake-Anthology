#ifndef QA_APPLICATION_BOT_WORLD_H
#define QA_APPLICATION_BOT_WORLD_H

#include "qa/bots.h"
#include "qa/source_save.h"

enum { APPLICATION_BOT_WORLD_FIRST=64, APPLICATION_BOT_WORLD_LAST=1021,
    APPLICATION_BOT_WORLD_CAPACITY=958, APPLICATION_BOT_MEMORY_BYTES=262144 };
typedef struct application_bot_world application_bot_world;
typedef enum application_bot_world_source {
    APPLICATION_BOT_WORLD_Q1, APPLICATION_BOT_WORLD_Q2
} application_bot_world_source;
typedef struct application_bot_memory_alias { uint32_t offset,length; } application_bot_memory_alias;
typedef struct application_bot_memory_view { uint8_t *data; size_t size; } application_bot_memory_view;
typedef struct application_bot_world_metadata {
    const char *model;
    qa_string_id classname;
    int32_t frame,max_health;
    bool hidden,worldspawn;
} application_bot_world_metadata;
typedef struct application_bot_world_movement {
    int32_t source_client,view_height;
    qa_vec3 view_angles;
    bool grounded;
} application_bot_world_movement;
typedef struct application_bot_world_client {
    const char *name,*skin;
    int32_t score;
    bool spectator;
} application_bot_world_client;
typedef struct application_bot_world_combat { int32_t health,armor; } application_bot_world_combat;
typedef struct application_bot_world_connect {
    bool allowed;
    const char *userinfo,*reason;
} application_bot_world_connect;
typedef struct application_bot_world_services {
    void *context;
    qa_session *session;
    qa_world *world;
    application_bot_world_source source;
    uint32_t max_clients,base_model_count;
    bool (*metadata)(void *,qa_actor_id,application_bot_world_metadata *,bool *,qa_error *);
    bool (*movement)(void *,qa_actor_id,application_bot_world_movement *,bool *,qa_error *);
    bool (*client)(void *,qa_actor_id,application_bot_world_client *,bool *,qa_error *);
    bool (*combat)(void *,qa_actor_id,application_bot_world_combat *,bool *,qa_error *);
    bool (*brush)(void *,qa_actor_id,bool *,qa_error *);
    bool (*weapon)(void *,qa_actor_id,int32_t *,int32_t *,qa_error *);
    qa_actor_id (*world_actor)(void *);
    qa_actor_id (*bot_actor)(void *,int32_t);
    bool (*clock)(void *,int32_t *,int32_t *,qa_error *);
    bool (*print)(void *,const char *,qa_error *);
    bool (*memory_debug)(void *,int32_t *,qa_error *);
    bool (*q2_connect)(void *,const char *,bool,application_bot_world_connect *,qa_error *);
    bool (*userinfo_changed)(void *,qa_actor_id,const char *,qa_error *);
    bool (*bot_connect)(void *,uint32_t,bool,bool *,qa_error *);
    bool (*bot_begin)(void *,uint32_t,qa_error *);
    bool (*bot_drop)(void *,uint32_t,qa_error *);
    bool (*q2_activate)(void *,qa_actor_id,qa_error *);
    bool (*exit_level)(void *,qa_error *);
    bool (*console)(void *,const char *,qa_error *);
    bool (*message)(void *,uint32_t,const char *,qa_error *);
    bool (*random)(void *,float *,qa_error *);
} application_bot_world_services;
typedef struct application_bot_world_entity {
    qa_actor_id actor;
    uint64_t generation;
    qa_q3_entity state;
    qa_q3_player player;
    qa_vec3 origin,angles;
    qa_bounds bounds;
    qa_string_id classname;
    const char *name;
    int32_t contents,team;
    double inline_model;
    bool present,linked,hidden,bot,has_player,connected,has_inline_model;
} application_bot_world_entity;

/* restoring constructs an empty holder without refreshing source observations. */
bool application_bot_world_create(const application_bot_world_services *,bool restoring,
    application_bot_world **,qa_error *);
bool application_bot_world_destroy(application_bot_world *,qa_error *);
bool application_bot_world_can_destroy(const application_bot_world *);
bool application_bot_world_refresh(application_bot_world *,qa_error *);
bool application_bot_world_entity_id(application_bot_world *,qa_actor_id,int32_t *,qa_error *);
bool application_bot_world_actor(application_bot_world *,int32_t,qa_actor_id *,qa_error *);
uint32_t application_bot_world_entity_count(const application_bot_world *);
bool application_bot_world_read(application_bot_world *,int32_t,application_bot_world_entity *,qa_error *);
bool application_bot_world_clock(application_bot_world *,int32_t *,int32_t *,qa_error *);
uint32_t application_bot_world_max_clients(const application_bot_world *);
bool application_bot_world_begin(application_bot_world *,uint32_t,qa_error *);
bool application_bot_world_connect_client(application_bot_world *,uint32_t,bool,bool,
    const char **rejection,qa_error *);
bool application_bot_world_userinfo_changed(application_bot_world *,uint32_t,qa_error *);
bool application_bot_world_activate(application_bot_world *,uint32_t,qa_error *);
bool application_bot_world_drop(application_bot_world *,uint32_t,const char *,qa_error *);
bool application_bot_world_drop_admitted(const application_bot_world *);
bool application_bot_world_exit_level(application_bot_world *,qa_error *);
bool application_bot_world_console(application_bot_world *,const char *,qa_error *);
bool application_bot_world_message(application_bot_world *,uint32_t,const char *,qa_error *);
bool application_bot_world_random(application_bot_world *,bool centered,float *,qa_error *);
const char *application_bot_world_userinfo(const application_bot_world *,uint32_t);
bool application_bot_world_userinfo_set(application_bot_world *,uint32_t,const char *,qa_error *);
bool application_bot_world_configstring(application_bot_world *,uint32_t,char *,size_t,qa_error *);
bool application_bot_world_configstring_set(application_bot_world *,uint32_t,const char *,qa_error *);
bool application_bot_world_model_index(application_bot_world *,const char *,int32_t *,qa_error *);

/* Aliases refer to the entire fixed source pool, including retained tails and
 * overlapping restored views. Rewinding does not clear former allocation bytes. */
bool application_bot_world_memory_allocate(application_bot_world *,uint32_t,
    application_bot_memory_alias *,qa_error *);
bool application_bot_world_memory_alias(application_bot_world *,application_bot_memory_alias,
    application_bot_memory_view *,qa_error *);
bool application_bot_world_memory_string(application_bot_world *,application_bot_memory_alias,
    const char **,qa_error *);
bool application_bot_world_memory_write_string(application_bot_world *,application_bot_memory_alias,
    const char *,qa_error *);
bool application_bot_world_memory_rewind(application_bot_world *,qa_error *);
bool application_bot_world_memory_status(application_bot_world *,qa_error *);
bool application_bot_world_capture(application_bot_world *,qa_buffer *,qa_error *);
bool application_bot_world_restore(application_bot_world *,qa_bytes,qa_error *);

#endif
