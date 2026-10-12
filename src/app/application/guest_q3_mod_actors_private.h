#ifndef QA_APPLICATION_GUEST_Q3_MOD_ACTORS_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_MOD_ACTORS_PRIVATE_H
#include "guest_q3_mod_actors.h"
#include "guest_q3_mod_private.h"
#include "qa/pool.h"

enum { MOD_ACTOR_TOUCH,MOD_ACTOR_USE,MOD_ACTOR_PAIN,MOD_ACTOR_DIE,MOD_ACTOR_DAMAGE,MOD_ACTOR_CALLS };
typedef struct mod_actor_call {
    uint32_t roles[8];
    int32_t words[62];
    size_t count,role_count;
} mod_actor_call;
typedef struct mod_actor_row {
    struct mod_actor_row *next,*previous,*retired_next;
    struct application_q3_mod_actors *owner;
    qa_actor_id actor;
    uint32_t pointer;
    uint64_t serial;
    size_t storage_slot;
    bool bound,retired;
} mod_actor_row;
typedef struct mod_actor_damage_frame {
    struct mod_actor_damage_frame *previous;
    qa_damage_request request;
    qa_damage_observer *observer;
    qa_damage_result result;
    qa_combat_state before;
    qa_vec3 velocity;
    bool has_velocity,finished;
} mod_actor_damage_frame;
typedef struct mod_actor_incoming {
    struct mod_actor_incoming *previous;
    mod_actor_row *row;
    const qa_damage_request *request;
    qa_damage_observer *observer;
    qa_damage_result *result;
    const qa_qvm_call *call;
    bool started,entered;
} mod_actor_incoming;
struct application_q3_mod_actors {
    application_q3_mod_actors_options options;
    size_t entity_record,client_record;
    uint32_t callback_fields[4];
    bool callbacks[4],present,combat,legacy,has_client,has_team_field;
    uint32_t damage_entry,health,takedamage,flags,godmode,no_knockback;
    uint32_t client_pointer,client_health,client_armor,client_team;
    double protection;
    enum { MOD_ACTOR_MASS_CONSTANT,MOD_ACTOR_MASS_INT32,MOD_ACTOR_MASS_FLOAT32 } mass_kind;
    double mass;
    uint32_t mass_field;
    uint32_t damage_flags[5];
    qa_team_source_value *teams;
    size_t team_count;
    mod_actor_call calls[MOD_ACTOR_CALLS];
    application_q3_mod_call *globals;
    qa_actor_index actor_index;
    qa_unified_frame_lease *actor_storage;
    qa_pool actor_pool;
    size_t actor_chunk;
    mod_actor_row *actors,*actors_tail,*retired;
    mod_actor_damage_frame *frames;
    mod_actor_incoming *incoming;
    uint64_t sequence;
    size_t depth;
    bool restoring,closing;
};
bool q3mod_actors_profile(application_q3_mod_actors *,qa_error *);
bool q3mod_actors_current(application_q3_mod_actors *,qa_error *);
mod_actor_row *q3mod_actors_find(application_q3_mod_actors *,qa_actor_id);
mod_actor_row *q3mod_actors_row_create(application_q3_mod_actors *,qa_error *);
void q3mod_actors_collect(application_q3_mod_actors *);
bool q3mod_actors_word(application_q3_mod_actors *,uint32_t,int32_t *,qa_error *);
bool q3mod_actors_write(application_q3_mod_actors *,uint32_t,int32_t,qa_error *);
bool q3mod_actors_state(void *,qa_combat_state *,qa_error *);
bool q3mod_actors_health(void *,double,qa_error *);
bool q3mod_actors_armor(void *,const qa_armor *,qa_error *);
bool q3mod_actors_armor_valid(void *,const qa_armor *,qa_error *);
bool q3mod_actors_invoke(application_q3_mod_actors *,uint32_t,const int32_t *,size_t,int32_t *,qa_error *);
bool q3mod_actors_invoke_started(application_q3_mod_actors *,uint32_t,const int32_t *,size_t,int32_t *,bool *,qa_error *);
bool q3mod_actors_entry(mod_actor_row *,size_t,uint32_t *,qa_error *);
bool q3mod_actors_source_damage(void *,qa_combat *,const qa_damage_request *,qa_damage_observer *,qa_damage_result *,qa_error *);
bool q3mod_actors_damage_hook(application_q3_mod_actors *,const qa_qvm_call *,
    application_q3_mod_actor_proceed,void *,int32_t *,qa_error *);
bool q3mod_actors_flush(application_q3_mod_actors *,mod_actor_damage_frame *,qa_error *);
bool q3mod_actors_reaction(application_q3_mod_actors *,qa_actor_id,size_t,double,qa_error *);
#endif
