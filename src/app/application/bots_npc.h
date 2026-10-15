#ifndef QA_APPLICATION_BOTS_NPC_H
#define QA_APPLICATION_BOTS_NPC_H
#include "qa/game_q1.h"
#include "qa/persistence_content.h"
#include "qa/native_host.h"

struct application_provider;
struct application_bots_npc;
struct qa_application;
bool application_navigation_hazard(struct qa_application *,qa_world *,qa_bounds,bool *,qa_error *);
bool application_bots_npc_native_hazard(struct application_provider *,qa_bounds,bool *,qa_error *);
bool application_bots_npc_walk(void *,qa_actor_id,qa_vec3,float,qa_q1_path_result *,qa_error *);
bool application_bots_npc_path(void *,const qa_native_host_q2_path_request *,
    qa_native_host_q2_path_info *,qa_error *);
void application_bots_npc_released(void *,qa_actor_id);
bool application_bots_npc_clone(void *,qa_actor_id,qa_actor_id,qa_error *);
void application_bots_npc_destroy(struct application_provider *);
bool application_bots_npc_idle(const struct application_provider *);
bool application_bots_npc_horde(void *);
bool application_bots_npc_capture(struct application_provider *,qa_buffer *,qa_error *);
bool application_bots_npc_restore(struct application_provider *,qa_bytes,qa_error *);
bool application_bots_npc_content_visit(const struct application_provider *,
    const qa_application_content_visitor *,qa_error *);
#endif
