#ifndef QA_GAME_Q1_BOTS_H
#define QA_GAME_Q1_BOTS_H
#include "qa/game_q1.h"

typedef struct qa_q1_bot_entity {
    qa_string_id model,classname;
    float max_health;
    int32_t frame;
    bool present,worldspawn;
} qa_q1_bot_entity;
bool qa_q1_bot_entity_read(const qa_q1_game *,qa_actor_id,qa_q1_bot_entity *,qa_error *);
bool qa_q1_bot_clock_read(const qa_q1_game *,double *,bool *,double *,qa_error *);
bool qa_q1_bot_max_clients(const qa_q1_game *,uint32_t *,qa_error *);
qa_actor_id qa_q1_bot_world_actor(const qa_q1_game *);
typedef struct qa_q1_source_client_view {
    qa_actor_id actor;
    uint32_t slot;
    const char *name;
    float frags,team;
    uint8_t shirt,pants;
    bool observer,no_target,god_mode;
    int32_t impulse;
    bool use,death_recorded;
    double respawn_requested_at;
} qa_q1_source_client_view;
typedef struct qa_q1_source_client_services {
    void *context;
    bool (*publish)(void *,const qa_q1_source_client_view *,qa_error *);
    bool (*observer)(void *,qa_actor_id,bool,qa_error *);
} qa_q1_source_client_services;
bool qa_q1_source_clients_configure(qa_q1_game *,const qa_q1_source_client_services *,qa_error *);
bool qa_q1_source_client_read(const qa_q1_game *,qa_actor_id,qa_q1_source_client_view *);
bool qa_q1_source_client_userinfo(qa_q1_game *,qa_actor_id,const char *,qa_error *);
bool qa_q1_source_client_info(const qa_q1_game *,qa_actor_id,const char *,const char **);
bool qa_q1_source_client_add_score(qa_q1_game *,qa_actor_id,double delta,qa_error *);
bool qa_q1_source_client_set_score(qa_q1_game *,qa_actor_id,float score,qa_error *);
bool qa_q1_source_client_observer(qa_q1_game *,qa_actor_id,bool,qa_error *);
bool qa_q1_source_client_spawned(qa_q1_game *,qa_actor_id,qa_error *);
bool qa_q1_bot_exit_level(qa_q1_game *,double seconds,bool same_level,qa_error *);
#endif
