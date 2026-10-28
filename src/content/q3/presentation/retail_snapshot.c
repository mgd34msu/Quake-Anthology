/* Port of src/content/q3/presentation/retail-snapshot.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/retail_snapshot.h"

#include <stdlib.h>
#include <string.h>

static void copy_slots(qa_q3_base_player_state_slots *destination, const int32_t *values, size_t length) {
  for (size_t index = 0; index < length; index++) {
    qa_q3_base_player_state_slots_set(destination, (double)index, (double)values[index]);
  }
}

bool qa_q3_pres_retail_snapshot_from_wire(qa_q3_pres_retail_snapshot *out, const qa_q3_wire_snapshot *source) {
  memset(out, 0, sizeof *out);
  const qa_q3_player_state_record *input = &source->player_state;
  qa_q3_base_product product = input->product == QA_Q3_PRODUCT_MISSIONPACK
      ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
  /* new PlayerStateRecord(product, 0, 0, 0), then copyFrom({...input, slots}). */
  if (!qa_q3_base_player_state_record_init(&out->player_state, product, 0, 0, 0, NULL)) return false;
  qa_q3_base_source_player_state *player_state = &out->player_state;
  player_state->command_time = input->command_time;
  player_state->pm_type = input->pm_type;
  player_state->bob_cycle = input->bob_cycle;
  player_state->pm_flags = input->pm_flags;
  player_state->pm_time = input->pm_time;
  qa_q3_base_player_state_record_set_origin(player_state, input->origin);
  qa_q3_base_player_state_record_set_velocity(player_state, input->velocity);
  player_state->weapon_time = input->weapon_time;
  player_state->gravity = input->gravity;
  player_state->speed = input->speed;
  player_state->delta_angles = input->delta_angles;
  player_state->ground_entity_num = input->ground_entity_num;
  player_state->legs_timer = input->legs_timer;
  player_state->legs_anim = input->legs_anim;
  player_state->torso_timer = input->torso_timer;
  player_state->torso_anim = input->torso_anim;
  player_state->movement_dir = input->movement_dir;
  player_state->grapple_point = input->grapple_point;
  player_state->e_flags = input->e_flags;
  player_state->event_sequence = input->event_sequence;
  copy_slots(&player_state->events, input->events, 2);
  copy_slots(&player_state->event_parms, input->event_parms, 2);
  player_state->external_event = input->external_event;
  player_state->external_event_parm = input->external_event_parm;
  player_state->external_event_time = input->external_event_time;
  player_state->client_num = input->client_num;
  player_state->weapon = input->weapon;
  player_state->weapon_state = input->weapon_state;
  player_state->viewangles = input->viewangles;
  player_state->viewheight = input->viewheight;
  player_state->damage_event = input->damage_event;
  player_state->damage_yaw = input->damage_yaw;
  player_state->damage_pitch = input->damage_pitch;
  player_state->damage_count = input->damage_count;
  copy_slots(&player_state->stats, input->stats, 16);
  copy_slots(&player_state->persistant, input->persistant, 16);
  copy_slots(&player_state->powerups, input->powerups, 16);
  copy_slots(&player_state->ammo, input->ammo, 16);
  player_state->generic1 = input->generic1;
  player_state->loop_sound = input->loop_sound;
  player_state->jumppad_ent = input->jumppad_ent;
  player_state->ping = input->ping;
  player_state->pmove_framecount = input->pmove_framecount;
  player_state->jumppad_frame = input->jumppad_frame;
  player_state->entity_event_sequence = input->entity_event_sequence;

  if (source->entity_count > 0) {
    out->entities = malloc(source->entity_count * sizeof *out->entities);
    if (out->entities == NULL) {
      qa_q3_pres_retail_snapshot_destroy(out);
      return false;
    }
    out->entity_count = source->entity_count;
    for (size_t index = 0; index < source->entity_count; index++) {
      /* retailEntityState: new EntityState() + copyFrom(every field). */
      qa_q3_entity_state_init(&out->entities[index]);
      qa_q3_entity_state_record_copy_from(&out->entities[index], &source->entities[index]);
    }
  }
  out->message_number = source->message_number;
  out->server_time = source->server_time;
  out->delta_number = source->delta_number;
  out->flags = source->flags;
  out->server_command_number = source->server_command_number;
  out->parse_entities_number = source->parse_entities_number;
  out->area_mask_length = source->area_mask_len <= 32 ? source->area_mask_len : 32;
  memcpy(out->area_mask, source->area_mask, out->area_mask_length);
  return true;
}

void qa_q3_pres_retail_snapshot_destroy(qa_q3_pres_retail_snapshot *snapshot) {
  qa_q3_base_player_state_record_destroy(&snapshot->player_state);
  free(snapshot->entities);
  memset(snapshot, 0, sizeof *snapshot);
}
