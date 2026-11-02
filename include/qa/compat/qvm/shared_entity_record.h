/*
 * Port of src/compat/qvm/shared-entity-record.ts. The TS borrowed
 * object with getters/setters becomes a view handle plus get/set
 * functions operating on the same live bytes. The nested `s` is the
 * landed qa_qvm_entity_state (entity_record.h, TS QvmEntityState)
 * borrowed over the same live bytes.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_SHARED_ENTITY_RECORD_H
#define QA_COMPAT_QVM_SHARED_ENTITY_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/compat/qvm/entity_record.h"
#include "qa/compat/qvm/memory_writes.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/math.h"

#define QA_QVM_SHARED_ENTITY_BYTES 516

/* TS QvmEntityCollisionModel: kind "inline" | "box" | "capsule". */
typedef struct qa_qvm_entity_collision_model {
  const char *kind;
  int32_t index; /* when inline */
} qa_qvm_entity_collision_model;

/* TS QvmSharedEntity borrowed handle. */
typedef struct qa_qvm_shared_entity {
  qa_qvm_entity_state s;
  qa_qvm_writable_view view;
  qa_qvm_abi_profile profile;
} qa_qvm_shared_entity;

int qa_qvm_shared_entity_bytes(qa_qvm_abi_profile profile);
void qa_qvm_borrow_shared_entity(const qa_qvm_writable_view *view, qa_qvm_abi_profile profile,
                                 qa_qvm_shared_entity *out);

bool qa_qvm_shared_entity_get_linked(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_linked(qa_qvm_shared_entity *entity, bool value);
int32_t qa_qvm_shared_entity_get_linkcount(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_linkcount(qa_qvm_shared_entity *entity, int32_t value);
int32_t qa_qvm_shared_entity_get_sv_flags(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_sv_flags(qa_qvm_shared_entity *entity, int32_t value);
int32_t qa_qvm_shared_entity_get_single_client(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_single_client(qa_qvm_shared_entity *entity, int32_t value);
qa_vec3 qa_qvm_shared_entity_get_mins(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_mins(qa_qvm_shared_entity *entity, qa_vec3 value);
qa_vec3 qa_qvm_shared_entity_get_maxs(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_maxs(qa_qvm_shared_entity *entity, qa_vec3 value);
int32_t qa_qvm_shared_entity_get_contents(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_contents(qa_qvm_shared_entity *entity, int32_t value);
qa_vec3 qa_qvm_shared_entity_get_absmin(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_absmin(qa_qvm_shared_entity *entity, qa_vec3 value);
qa_vec3 qa_qvm_shared_entity_get_absmax(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_absmax(qa_qvm_shared_entity *entity, qa_vec3 value);
qa_vec3 qa_qvm_shared_entity_get_current_origin(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_current_origin(qa_qvm_shared_entity *entity, qa_vec3 value);
qa_vec3 qa_qvm_shared_entity_get_current_angles(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_current_angles(qa_qvm_shared_entity *entity, qa_vec3 value);
int32_t qa_qvm_shared_entity_get_owner_num(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_owner_num(qa_qvm_shared_entity *entity, int32_t value);
qa_qvm_entity_collision_model qa_qvm_shared_entity_get_model(qa_qvm_shared_entity *entity);
void qa_qvm_shared_entity_set_model(qa_qvm_shared_entity *entity, qa_qvm_entity_collision_model value);

#endif
