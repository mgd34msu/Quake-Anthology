#ifndef QA_APPLICATION_NATIVE_Q2_ATTACK_H
#define QA_APPLICATION_NATIVE_Q2_ATTACK_H
#include "qa/native_host.h"
struct application_native_q2;
struct application_native_q2_attack;
struct application_native_q2_attack_restore;
struct qa_q2_wire_movement;
bool application_native_q2_attack_input_fields(struct application_native_q2 *,uint32_t,
    qa_actor_id,struct qa_q2_wire_movement *,qa_error *);
bool application_native_q2_attack_prepare(struct application_native_q2 *, qa_error *);
bool application_native_q2_attack_activate(struct application_native_q2 *, qa_error *);
bool application_native_q2_attack_suspend(struct application_native_q2 *, qa_error *);
bool application_native_q2_attack_close(struct application_native_q2 *, qa_error *);
void application_native_q2_attack_released(struct application_native_q2 *, qa_actor_id);
bool application_native_q2_attack_read(struct application_native_q2 *, qa_actor_id attacker,
    qa_actor_id inflictor, qa_actor_id target, bool weapon_damage, qa_attack *, qa_error *);
/* Readonly loan from the already prepared original descriptor roster. */
bool application_native_q2_attack_weapon_read(struct application_native_q2 *, uint32_t source_slot,
    qa_actor_id, qa_item_id *, qa_error *);
bool application_native_q2_attack_item_read(struct application_native_q2 *, uint32_t source_slot,
    qa_actor_id, qa_native_address descriptor, qa_item_id *, qa_error *);
bool application_native_q2_attack_descriptor_item(struct application_native_q2 *,
    qa_native_address descriptor,qa_item_id *,qa_error *);
bool application_native_q2_attack_weapon_contains(struct application_native_q2 *,
    qa_item_id, bool *, qa_error *);
const qa_json_document *application_native_q2_attack_declaration_read(const struct application_native_q2 *);
bool application_native_q2_attack_capture(struct application_native_q2 *, qa_buffer *, qa_error *);
bool application_native_q2_attack_restore_prepare(struct application_native_q2 *, qa_bytes,
    struct application_native_q2_attack_restore **, qa_error *);
void application_native_q2_attack_restore_commit(struct application_native_q2 *,
    struct application_native_q2_attack_restore *);
void application_native_q2_attack_restore_abort(struct application_native_q2_attack_restore *);
#endif
