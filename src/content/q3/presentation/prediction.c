/* Port of src/content/q3/presentation/prediction.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/prediction.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/shared/jump_pad.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/content/q3/presentation/entities.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/world/collision/q3/model.h"
#include "qa/foundation/js.h"

enum { SOLID_BMODEL = 0xffffff, CONTENTS_BODY = 0x2000000, MASK_PLAYERSOLID = 1 | 0x10000 | CONTENTS_BODY };

static double f32(double value) { return (double)(float)value; }
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double truncated = trunc(fmod(fabs(value), 4294967296.0));
  uint32_t bits = qa_js_to_uint32(truncated);
  if (value < 0) bits = ~bits + 1u;
  int32_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}
static int32_t imul(double a, double b) {
  uint32_t product = (uint32_t)to_int32(a) * (uint32_t)to_int32(b);
  int32_t result;
  memcpy(&result, &product, sizeof result);
  return result;
}
/* TS Number -> string for warn messages (integer text when integral). */
static void fmt_num(char *buffer, size_t size, double value) {
  qa_net_js_number(value, buffer, size);
}
static qa_q3_base_trajectory base_trajectory(const qa_q3_wire_trajectory *wire) {
  qa_q3_base_trajectory result = {wire->type, wire->time, wire->duration, wire->base, wire->delta};
  return result;
}

static qa_q3_base_user_command empty_command(void) {
  qa_q3_base_user_command command;
  memset(&command, 0, sizeof command);
  command.weapon = QA_Q3_WP_NONE;
  return command;
}

void qa_q3_pres_client_command_history_init(qa_q3_pres_client_command_history *history) {
  for (int index = 0; index < 64; index++) history->commands[index] = empty_command();
  history->number = 0;
}
double qa_q3_pres_client_command_history_current_number(const qa_q3_pres_client_command_history *history) {
  return history->number;
}
double qa_q3_pres_client_command_history_append(qa_q3_pres_client_command_history *history,
    qa_q3_base_user_command command) {
  history->number = to_int32(history->number + 1);
  history->commands[to_int32(history->number) & 63] = command;
  return history->number;
}
bool qa_q3_pres_client_command_history_read(qa_q3_pres_client_command_history *history, double number,
    qa_q3_base_user_command *out) {
  if (number != trunc(number)) qa_range_error("Invalid user command number");
  if (number > history->number) {
    char wanted[32], current[32];
    fmt_num(wanted, sizeof wanted, number);
    fmt_num(current, sizeof current, history->number);
    qa_range_error("CL_GetUserCmd: %s >= %s", wanted, current);
  }
  if (number <= (double)to_int32(history->number - 64)) return false;
  *out = history->commands[to_int32(number) & 63];
  return true;
}
static double history_current_number(void *ctx) {
  return qa_q3_pres_client_command_history_current_number(ctx);
}
static bool history_read(void *ctx, double number, qa_q3_base_user_command *out) {
  return qa_q3_pres_client_command_history_read(ctx, number, out);
}
qa_q3_pres_command_source qa_q3_pres_client_command_history_source(qa_q3_pres_client_command_history *history) {
  qa_q3_pres_command_source source = {history, history_current_number, history_read};
  return source;
}

typedef qa_q3_pres_prediction_runtime runtime_t;
static void warn(runtime_t *runtime, const char *message) {
  runtime->host.warn(runtime->host.ctx, message);
}
static double slot_get(const qa_q3_base_player_state_slots *slots, double index) {
  return qa_q3_base_player_state_slots_get(slots, index);
}

static double inventory_ammo(void *ctx, qa_q3_weapon weapon) {
  return slot_get(&((const qa_q3_base_source_player_state *)ctx)->ammo, weapon);
}
static double inventory_powerup(void *ctx, qa_q3_powerup powerup) {
  return slot_get(&((const qa_q3_base_source_player_state *)ctx)->powerups, powerup);
}
static qa_q3_base_player_inventory inventory_of(const qa_q3_base_source_player_state *ps) {
  qa_q3_base_product product = qa_q3_base_player_state_record_product(ps);
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(product);
  qa_q3_base_player_inventory inventory;
  memset(&inventory, 0, sizeof inventory);
  inventory.ctx = (void *)ps;
  inventory.product = product;
  inventory.health = qa_q3_base_player_state_record_health(ps);
  inventory.armor = slot_get(&ps->stats, schema->armor);
  inventory.max_health = slot_get(&ps->stats, schema->max_health);
  inventory.holdable_item = slot_get(&ps->stats, schema->holdable_item);
  inventory.team = slot_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM);
  inventory.ammo = inventory_ammo;
  inventory.powerup = inventory_powerup;
  if (product != QA_Q3_BASE_PRODUCT_BASEQ3) {
    if (!schema->has_persistent_powerup) {
      qa_range_error("Missionpack prediction requires its stat schema");
    }
    inventory.has_persistent_powerup_index = true;
    inventory.persistent_powerup_index = slot_get(&ps->stats, schema->persistent_powerup);
  }
  return inventory;
}

static qa_vec3 interpolate_vector(qa_vec3 a, qa_vec3 b, double fraction) {
  return qa_math_vec3(f32(a.x + f32(fraction * f32(b.x - a.x))),
                      f32(a.y + f32(fraction * f32(b.y - a.y))),
                      f32(a.z + f32(fraction * f32(b.z - a.z))));
}

/* state.predictedPlayerState = source.copy() (fresh owned slots). */
static bool replace_predicted(qa_q3_pres_client_game_state *state, const qa_q3_base_source_player_state *source) {
  qa_q3_base_source_player_state copy;
  if (!qa_q3_base_player_state_record_copy(&copy, source)) return false;
  qa_q3_base_player_state_record_destroy(&state->predicted_player_state);
  state->predicted_player_state = copy;
  return true;
}
static qa_q3_base_user_command required_command(runtime_t *runtime, double number) {
  qa_q3_base_user_command command;
  if (!runtime->host.commands.read(runtime->host.commands.ctx, number, &command)) {
    qa_range_error("Prediction command source violated its CMD_BACKUP window");
  }
  return command;
}

void qa_q3_pres_prediction_runtime_init(runtime_t *runtime, qa_q3_pres_client_game_state *state,
    qa_q3_pres_collision_world collision, qa_q3_pres_prediction_host host) {
  runtime->state = state;
  runtime->collision = collision;
  runtime->host = host;
  runtime->command = empty_command();
}

qa_q3_pres_movement_trace qa_q3_pres_prediction_runtime_trace(runtime_t *runtime, qa_vec3 start, qa_vec3 end,
    qa_bounds bounds, double skip_number, double mask) {
  qa_q3_pres_collision_world *collision = &runtime->collision;
  qa_q3_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start;
  query.end = end;
  query.shape.kind = "box";
  query.shape.mins = bounds.min;
  query.shape.maxs = bounds.max;
  query.mask = mask;
  qa_q3_trace_result world_trace = collision->trace(collision->ctx, query);
  qa_q3_pres_movement_trace result;
  result.result = world_trace;
  result.entity_num = world_trace.fraction != 1 ? QA_Q3_BASE_ENTITYNUM_WORLD : QA_Q3_BASE_ENTITYNUM_NONE;
  qa_q3_pres_client_entity_list *solids = &runtime->state->solid_entities;
  for (size_t index = 0; index < solids->count; index++) {
    qa_q3_pres_client_entity *cent = solids->items[index];
    const qa_q3_base_entity_state *entity = &cent->current_state;
    if (entity->number == skip_number) continue;
    int32_t solid = qa_js_to_int32(entity->solid);
    double x = solid & 255;
    double zd = (solid >> 8) & 255;
    double zu = ((solid >> 16) & 255) - 32;
    qa_q3_trace_result trace;
    if (entity->solid == SOLID_BMODEL) {
      qa_q3_trace_query modeled = query;
      modeled.has_model_index = true;
      modeled.model_index = entity->modelindex;
      trace = collision->transformed_trace(collision->ctx, modeled,
          qa_q3_base_evaluate_trajectory(base_trajectory(&entity->pos), runtime->state->physics_time),
          cent->lerp_angles);
    } else {
      qa_bounds box = {{-x, -x, -zd}, {x, x, zu}};
      qa_temporary_collision_model *model = qa_create_box_model(box, collision->counters, NULL);
      trace = qa_temporary_model_transformed_trace(model, query, cent->lerp_origin, qa_math_vec3(0, 0, 0));
      qa_temporary_collision_model_destroy(model);
    }
    if (strcmp(trace.solidity, "all-solid") == 0 || trace.fraction < result.result.fraction) {
      result.result = trace;
      result.entity_num = entity->number;
    } else if (strcmp(trace.solidity, "clear") != 0 && strcmp(result.result.solidity, "all-solid") != 0) {
      result.result.solidity = "start-solid";
    }
    if (strcmp(result.result.solidity, "all-solid") == 0) return result;
  }
  return result;
}

double qa_q3_pres_prediction_runtime_point_contents(runtime_t *runtime, qa_vec3 point, double pass_entity) {
  qa_q3_pres_collision_world *collision = &runtime->collision;
  double contents = collision->point_contents(collision->ctx, point, 0);
  qa_q3_pres_client_entity_list *solids = &runtime->state->solid_entities;
  for (size_t index = 0; index < solids->count; index++) {
    const qa_q3_base_entity_state *entity = &solids->items[index]->current_state;
    if (entity->number == pass_entity || entity->solid != SOLID_BMODEL || entity->modelindex == 0) continue;
    double extra = collision->transformed_point_contents(collision->ctx, point, entity->modelindex,
        entity->origin, entity->angles);
    contents = (double)(to_int32(contents) | to_int32(extra));
  }
  return contents;
}

bool qa_q3_pres_prediction_runtime_interpolate_player_state(runtime_t *runtime, bool grab_angles) {
  qa_q3_pres_client_game_state *state = runtime->state;
  qa_q3_pres_retail_snapshot *previous = state->snap;
  if (previous == NULL) qa_range_error("CG_InterpolatePlayerState requires cg.snap");
  if (!replace_predicted(state, &previous->player_state)) return false;
  qa_q3_base_source_player_state *out = &state->predicted_player_state;
  if (grab_angles) {
    qa_q3_base_user_command command =
        required_command(runtime, runtime->host.commands.current_number(runtime->host.commands.ctx));
    runtime->host.movement.update_view_angles(runtime->host.movement.ctx, out, command);
  }
  qa_q3_pres_retail_snapshot *next = state->next_snap;
  if (state->next_frame_teleport || next == NULL || next->server_time <= previous->server_time) return true;
  double fraction = f32(f32((double)to_int32(state->time - (double)previous->server_time)) /
                        f32((double)to_int32((double)next->server_time - (double)previous->server_time)));
  const qa_q3_base_source_player_state *a = &previous->player_state;
  const qa_q3_base_source_player_state *b = &next->player_state;
  double cycle = b->bob_cycle < a->bob_cycle ? (double)to_int32(b->bob_cycle + 256) : b->bob_cycle;
  out->bob_cycle = qa_qvm_float_to_int(f32(f32(a->bob_cycle) +
      f32(fraction * f32((double)to_int32(cycle - a->bob_cycle)))));
  qa_vec3 a3 = qa_q3_base_player_state_record_origin(a);
  qa_vec3 b3 = qa_q3_base_player_state_record_origin(b);
  qa_q3_base_player_state_record_set_origin(out, interpolate_vector(
      a3, b3, fraction));
  qa_vec3 a2 = qa_q3_base_player_state_record_velocity(a);
  qa_vec3 b2 = qa_q3_base_player_state_record_velocity(b);
  qa_q3_base_player_state_record_set_velocity(out, interpolate_vector(
      a2, b2, fraction));
  if (!grab_angles) {
    out->viewangles = qa_math_vec3(
        qa_lerp_angle_with_rule(a->viewangles.x, b->viewangles.x, fraction, QA_LERP_ANGLE_FLOAT),
        qa_lerp_angle_with_rule(a->viewangles.y, b->viewangles.y, fraction, QA_LERP_ANGLE_FLOAT),
        qa_lerp_angle_with_rule(a->viewangles.z, b->viewangles.z, fraction, QA_LERP_ANGLE_FLOAT));
  }
  return true;
}

static void source_touch_item(runtime_t *runtime, qa_q3_pres_client_entity *cent,
    qa_q3_pres_prediction_settings settings) {
  qa_vec3 player_origin;
  qa_q3_base_trajectory item_position;
  qa_q3_pres_client_game_state *state = runtime->state;
  qa_q3_base_source_player_state *ps = &state->predicted_player_state;
  qa_q3_base_entity_state *entity = &cent->current_state;
  if (!settings.predict_items ||
      !(player_origin = qa_q3_base_player_state_record_origin(ps), item_position = base_trajectory(&entity->pos), qa_q3_base_player_touches_item(player_origin,
          item_position, state->time)) ||
      cent->misc_time == state->time) {
    return;
  }
  qa_q3_base_pickup_entity pickup = {entity->modelindex, entity->modelindex2, entity->generic1};
  qa_q3_base_player_inventory inventory = inventory_of(ps);
  if (!qa_q3_base_can_item_be_grabbed(settings.game_type, pickup, &inventory)) return;
  const qa_q3_base_item_definition *item = qa_q3_base_item_at(state->product, entity->modelindex);
  if (state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && settings.game_type == QA_Q3_BASE_GT_1FCTF &&
      item->tag != QA_Q3_PW_NEUTRALFLAG) {
    return;
  }
  if (settings.game_type == QA_Q3_BASE_GT_CTF ||
      (state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && settings.game_type == QA_Q3_BASE_GT_HARVESTER)) {
    double team = slot_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM);
    if ((team == QA_Q3_BASE_TEAM_RED && item->tag == QA_Q3_PW_REDFLAG) ||
        (team == QA_Q3_BASE_TEAM_BLUE && item->tag == QA_Q3_PW_BLUEFLAG)) {
      return;
    }
  }
  (void)qa_q3_base_player_state_record_add_event(ps, QA_Q3_EV_ITEM_PICKUP, entity->modelindex);
  entity->e_flags = (double)(qa_js_to_int32(entity->e_flags) | 0x80);
  cent->misc_time = state->time;
  if (item->type == QA_Q3_BASE_IT_WEAPON) {
    double slot = qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(ps))->weapons;
    int32_t bit = (int32_t)(1u << ((uint32_t)to_int32(item->tag) & 31u));
    qa_q3_base_player_state_slots_set(&ps->stats, slot,
        (double)(to_int32(slot_get(&ps->stats, slot)) | bit));
    if (slot_get(&ps->ammo, item->tag) == 0) qa_q3_base_player_state_slots_set(&ps->ammo, item->tag, 1);
  }
}
typedef struct touch_source { runtime_t *runtime; qa_q3_pres_client_entity *entity; qa_q3_pres_prediction_settings settings; } touch_source;
static void touch_source_run(void *ctx) {
  const touch_source *source = ctx;
  source_touch_item(source->runtime, source->entity, source->settings);
}
void qa_q3_pres_prediction_runtime_touch_item(runtime_t *runtime, qa_q3_pres_client_entity *entity,
    qa_q3_pres_prediction_settings settings) {
  touch_source source = {runtime, entity, settings};
  runtime->host.predict_item(runtime->host.ctx, entity, touch_source_run, &source);
}

void qa_q3_pres_prediction_runtime_touch_trigger_prediction(runtime_t *runtime, qa_bounds bounds,
    qa_q3_pres_prediction_settings settings) {
  qa_q3_pres_client_game_state *state = runtime->state;
  qa_q3_base_source_player_state *ps = &state->predicted_player_state;
  if (qa_q3_base_player_state_record_health(ps) <= 0) return;
  bool spectator = ps->pm_type == QA_Q3_PM_SPECTATOR;
  if (ps->pm_type != QA_Q3_PM_NORMAL && !spectator) return;
  qa_q3_pres_client_entity_list *triggers = &state->trigger_entities;
  for (size_t index = 0; index < triggers->count; index++) {
    qa_q3_pres_client_entity *cent = triggers->items[index];
    qa_q3_base_entity_state *entity = &cent->current_state;
    if (entity->e_type == QA_Q3_BASE_ET_ITEM && !spectator) {
      qa_q3_pres_prediction_runtime_touch_item(runtime, cent, settings);
      continue;
    }
    if (entity->solid != SOLID_BMODEL || entity->modelindex == 0) continue;
    qa_vec3 origin = qa_q3_base_player_state_record_origin(ps);
    qa_q3_trace_query query;
    memset(&query, 0, sizeof query);
    query.start = origin;
    query.end = origin;
    query.shape.kind = "box";
    query.shape.mins = bounds.min;
    query.shape.maxs = bounds.max;
    query.has_model_index = true;
    query.model_index = entity->modelindex;
    query.mask = -1;
    qa_q3_trace_result trace = runtime->collision.trace(runtime->collision.ctx, query);
    if (strcmp(trace.solidity, "clear") == 0) continue;
    if (entity->e_type == QA_Q3_BASE_ET_TELEPORT_TRIGGER) {
      state->hyperspace = true;
    } else if (entity->e_type == QA_Q3_BASE_ET_PUSH_TRIGGER) {
      qa_q3_base_touch_jump_pad(ps, entity);
    }
  }
  if (ps->jumppad_frame != ps->pmove_framecount) {
    ps->jumppad_frame = 0;
    ps->jumppad_ent = 0;
  }
}

static qa_q3_pres_movement_trace movement_trace_thunk(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
    double skip_number, double mask) {
  return qa_q3_pres_prediction_runtime_trace(ctx, start, end, bounds, skip_number, mask);
}
static double movement_point_contents_thunk(void *ctx, qa_vec3 point, double pass_entity) {
  return qa_q3_pres_prediction_runtime_point_contents(ctx, point, pass_entity);
}

bool qa_q3_pres_prediction_runtime_predict_player_state(runtime_t *runtime) {
  qa_q3_pres_client_game_state *state = runtime->state;
  qa_q3_pres_retail_snapshot *snapshot = state->snap;
  qa_q3_pres_prediction_settings settings = runtime->host.settings(runtime->host.ctx);
  if (snapshot == NULL) qa_range_error("CG_PredictPlayerState requires cg.snap");
  state->hyperspace = false;
  if (!state->valid_pps) {
    state->valid_pps = true;
    if (!replace_predicted(state, &snapshot->player_state)) return false;
  }
  if (settings.demo_playback || (to_int32(snapshot->player_state.pm_flags) & QA_Q3_FLAG_FOLLOW) != 0) {
    return qa_q3_pres_prediction_runtime_interpolate_player_state(runtime, false);
  }
  if (settings.no_predict || settings.synchronous_clients) {
    return qa_q3_pres_prediction_runtime_interpolate_player_state(runtime, true);
  }
  int32_t mask = MASK_PLAYERSOLID;
  if (state->predicted_player_state.pm_type == QA_Q3_PM_DEAD ||
      slot_get(&snapshot->player_state.persistant, QA_Q3_BASE_PERS_TEAM) == QA_Q3_BASE_TEAM_SPECTATOR) {
    mask &= ~CONTENTS_BODY;
  }
  qa_q3_base_source_player_state old;
  if (!qa_q3_base_player_state_record_copy(&old, &state->predicted_player_state)) return false;
  double current = runtime->host.commands.current_number(runtime->host.commands.ctx);
  qa_q3_base_user_command oldest = required_command(runtime, (double)to_int32(current - 63));
  if (oldest.server_time > snapshot->player_state.command_time && oldest.server_time < state->time) {
    if (settings.show_miss != 0) warn(runtime, "exceeded PACKET_BACKUP on commands\n");
    qa_q3_base_player_state_record_destroy(&old);
    return true;
  }
  qa_q3_base_user_command latest = required_command(runtime, current);
  qa_q3_pres_retail_snapshot *selected =
      state->next_snap != NULL && !state->next_frame_teleport && !state->this_frame_teleport
          ? state->next_snap : snapshot;
  if (!replace_predicted(state, &selected->player_state)) {
    qa_q3_base_player_state_record_destroy(&old);
    return false;
  }
  qa_q3_base_player_state_record_set_event_debug(&state->predicted_player_state, runtime->host.event_debug);
  state->physics_time = (double)selected->server_time;
  qa_q3_base_source_player_state *ps = &state->predicted_player_state;
  if (settings.pmove_msec < 8) {
    runtime->host.set_pmove_msec(runtime->host.ctx, 8);
  } else if (settings.pmove_msec > 33) {
    runtime->host.set_pmove_msec(runtime->host.ctx, 33);
  }
  bool moved = false;
  for (double number = (double)to_int32(current - 63); number <= current; number += 1) {
    qa_q3_base_user_command read_command;
    if (runtime->host.commands.read(runtime->host.commands.ctx, number, &read_command)) {
      runtime->command = read_command;
    }
    if (settings.pmove_fixed) {
      runtime->host.movement.update_view_angles(runtime->host.movement.ctx, ps, runtime->command);
    }
    if (runtime->command.server_time <= ps->command_time || runtime->command.server_time > latest.server_time) {
      continue;
    }
    if (ps->command_time == old.command_time) {
      if (state->this_frame_teleport) {
        state->predicted_error = qa_math_vec3(0, 0, 0);
        if (settings.show_miss != 0) warn(runtime, "PredictionTeleport\n");
        state->this_frame_teleport = false;
      } else {
        qa_vec3 old_origin = qa_q3_base_player_state_record_origin(&old);
        qa_vec3 adjusted = qa_q3_pres_adjust_position_for_mover(state,
            qa_q3_base_player_state_record_origin(ps), ps->ground_entity_num, state->physics_time, state->old_time);
        qa_vec3 delta = qa_sub3(old_origin, adjusted);
        double length = f32(qa_length3(delta));
        if (settings.show_miss != 0 &&
            (old_origin.x != adjusted.x || old_origin.y != adjusted.y || old_origin.z != adjusted.z)) {
          warn(runtime, "prediction error\n");
        }
        if (length > f32(0.1)) {
          if (settings.show_miss != 0) {
            char message[QA_JS_TO_FIXED_CAPACITY + 32];
            char fixed[QA_JS_TO_FIXED_CAPACITY]; /* length.toFixed(6): exact ties round up */
            qa_js_to_fixed(length, 6, fixed, sizeof fixed);
            snprintf(message, sizeof message, "Prediction miss: %s\n", fixed);
            warn(runtime, message);
          }
          if (settings.error_decay_integer != 0) {
            int32_t elapsed = to_int32(state->time - state->predicted_error_time);
            double fraction = f32(f32(f32(settings.error_decay_value) - f32((double)elapsed)) /
                                  f32(settings.error_decay_value));
            if (fraction < 0) fraction = 0;
            if (fraction > 0 && settings.show_miss != 0) {
              char message[QA_JS_TO_FIXED_CAPACITY + 32];
              char fixed[QA_JS_TO_FIXED_CAPACITY]; /* fraction.toFixed(6) */
              qa_js_to_fixed(fraction, 6, fixed, sizeof fixed);
              snprintf(message, sizeof message, "Double prediction decay: %s\n", fixed);
              warn(runtime, message);
            }
            state->predicted_error = qa_scale3(state->predicted_error, fraction);
          } else {
            state->predicted_error = qa_math_vec3(0, 0, 0);
          }
          state->predicted_error = qa_add3(delta, state->predicted_error);
          state->predicted_error_time = state->old_time;
        }
      }
    }
    double original_server_time = runtime->command.server_time;
    if (settings.pmove_fixed && runtime->host.movement.command_timing != NULL &&
        strcmp(runtime->host.movement.command_timing, "q3") == 0) {
      runtime->command.server_time = imul(
          trunc((double)to_int32(runtime->command.server_time + settings.pmove_msec - 1) / settings.pmove_msec),
          settings.pmove_msec);
    }
    qa_q3_pres_movement_options options;
    memset(&options, 0, sizeof options);
    options.has_original_server_time = true;
    options.original_server_time = original_server_time;
    options.trace_ctx = runtime;
    options.trace = movement_trace_thunk;
    options.point_contents = movement_point_contents_thunk;
    options.trace_mask = mask;
    options.has_fixed_msec = settings.pmove_fixed;
    options.fixed_msec = settings.pmove_msec;
    options.no_footsteps = (to_int32(settings.dm_flags) & 32) != 0;
    options.ladders_disabled = settings.ladders_disabled;
    options.gauntlet_hit = false;
    qa_bounds result = runtime->host.movement.move_player(runtime->host.movement.ctx, ps, runtime->command, options);
    moved = true;
    qa_q3_pres_prediction_runtime_touch_trigger_prediction(runtime, result, settings);
  }
  if (settings.show_miss > 1) {
    char server_time[32], frame_time[32], message[96];
    fmt_num(server_time, sizeof server_time, runtime->command.server_time);
    fmt_num(frame_time, sizeof frame_time, state->time);
    snprintf(message, sizeof message, "[%s : %s] ", server_time, frame_time);
    warn(runtime, message);
  }
  if (!moved) {
    /* A local authority can acknowledge every command before the next frame. */
    runtime->host.transition_player_state(runtime->host.ctx, ps, &old);
    if (settings.show_miss != 0) warn(runtime, "not moved\n");
    qa_q3_base_player_state_record_destroy(&old);
    return true;
  }
  qa_q3_base_player_state_record_set_origin(ps, qa_q3_pres_adjust_position_for_mover(state,
      qa_q3_base_player_state_record_origin(ps), ps->ground_entity_num, state->physics_time, state->time));
  if (settings.show_miss != 0 && ps->event_sequence > (double)to_int32(old.event_sequence + 2)) {
    warn(runtime, "WARNING: dropped event\n");
  }
  runtime->host.transition_player_state(runtime->host.ctx, ps, &old);
  if (settings.show_miss != 0 && state->event_sequence > ps->event_sequence) {
    warn(runtime, "WARNING: double event\n");
    state->event_sequence = ps->event_sequence;
  }
  qa_q3_base_player_state_record_destroy(&old);
  return true;
}

bool qa_q3_pres_build_solid_list(qa_q3_pres_client_game_state *state) {
  state->solid_entities.count = 0;
  state->trigger_entities.count = 0;
  qa_q3_pres_retail_snapshot *snapshot =
      state->next_snap != NULL && !state->next_frame_teleport && !state->this_frame_teleport
          ? state->next_snap : state->snap;
  if (snapshot == NULL) qa_range_error("CG_BuildSolidList requires a snapshot");
  for (size_t index = 0; index < snapshot->entity_count; index++) {
    qa_q3_pres_client_entity *entity =
        qa_q3_pres_client_game_state_entity_at(state, snapshot->entities[index].number);
    int32_t type = qa_js_to_int32(entity->current_state.e_type);
    if (type == QA_Q3_BASE_ET_ITEM || type == QA_Q3_BASE_ET_PUSH_TRIGGER || type == QA_Q3_BASE_ET_TELEPORT_TRIGGER) {
      if (!qa_q3_pres_client_entity_list_push(&state->trigger_entities, entity)) return false;
    } else if (entity->next_state.solid != 0) {
      if (!qa_q3_pres_client_entity_list_push(&state->solid_entities, entity)) return false;
    }
  }
  return true;
}
