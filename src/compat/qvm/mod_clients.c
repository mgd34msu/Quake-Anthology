/* See include/qa/compat/qvm/mod_clients.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/mod_clients.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/world/session/mod_client_input.h"

typedef struct client_slot {
  qa_actor_id actor;
  int64_t slot;
  bool admitted;
  bool has_client;
  qa_client_id client;
} client_slot;

static client_slot *find_entry(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor) {
  client_slot *entries = bindings->entries;
  for (size_t i = 0; i < bindings->entry_count; i++)
    if (qa_same_actor(entries[i].actor, actor)) return &entries[i];
  return NULL;
}

static client_slot *require_entry(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor) {
  client_slot *entry = find_entry(bindings, actor);
  qa_mod_client_services *services = bindings->operations.services;
  qa_client_id client;
  bool has_client = services->for_actor(services->ctx, actor, &client);
  qa_actor_id resolved;
  bool resolves = has_client && services->actor(services->ctx, client, &resolved) &&
                  qa_same_actor(resolved, actor);
  if (entry == NULL || !has_client ||
      (entry->has_client && !qa_same_client(entry->client, client)) || !resolves)
    qa_range_error("QVM component client identity is no longer live");
  if (!entry->has_client) {
    entry->has_client = true;
    entry->client = client;
  }
  return entry;
}

static client_slot *at_slot(qa_qvm_mod_client_bindings *bindings, int64_t slot) {
  client_slot *entries = bindings->entries;
  for (size_t i = 0; i < bindings->entry_count; i++)
    if (entries[i].slot == slot) return require_entry(bindings, entries[i].actor);
  return NULL;
}

void qa_qvm_mod_client_bindings_init(qa_qvm_mod_client_bindings *bindings,
                                     const qa_qvm_mod_client_operations *operations) {
  memset(bindings, 0, sizeof *bindings);
  bindings->operations = *operations;
}

void qa_qvm_mod_client_bindings_destroy(qa_qvm_mod_client_bindings *bindings) {
  free(bindings->entries);
  free(bindings->applications);
  memset(bindings, 0, sizeof *bindings);
}

bool qa_qvm_mod_client_bindings_has(const qa_qvm_mod_client_bindings *bindings, qa_actor_id actor) {
  const client_slot *entries = bindings->entries;
  for (size_t i = 0; i < bindings->entry_count; i++)
    if (qa_same_actor(entries[i].actor, actor)) return true;
  return false;
}

bool qa_qvm_mod_client_bindings_live(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor) {
  client_slot *entry = find_entry(bindings, actor);
  qa_mod_client_services *services = bindings->operations.services;
  qa_client_id client;
  if (entry == NULL || !services->for_actor(services->ctx, actor, &client)) return false;
  if (entry->has_client && !qa_same_client(entry->client, client)) return false;
  qa_actor_id resolved;
  return services->actor(services->ctx, client, &resolved) && qa_same_actor(resolved, actor);
}

bool qa_qvm_mod_client_bindings_admitted(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor) {
  if (!qa_qvm_mod_client_bindings_has(bindings, actor)) return false;
  return require_entry(bindings, actor)->admitted;
}

bool qa_qvm_mod_client_bindings_slot(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor, int64_t *out) {
  qa_mod_client_services *services = bindings->operations.services;
  qa_client_id client;
  if (!services->for_actor(services->ctx, actor, &client)) {
    client_slot *entry = find_entry(bindings, actor);
    if (entry == NULL) return false;
    *out = entry->slot;
    return true;
  }
  client_slot *previous = find_entry(bindings, actor);
  if (previous != NULL) {
    *out = require_entry(bindings, actor)->slot;
    return true;
  }
  int64_t reserved[128];
  size_t reserved_count = 0;
  if (bindings->operations.reserved_slots != NULL)
    reserved_count = bindings->operations.reserved_slots(bindings->operations.ctx, reserved, 128);
  int64_t slot = 0;
  for (;;) {
    bool used = false;
    client_slot *entries = bindings->entries;
    for (size_t i = 0; i < bindings->entry_count; i++) if (entries[i].slot == slot) used = true;
    for (size_t i = 0; i < reserved_count; i++) if (reserved[i] == slot) used = true;
    if (!used) break;
    slot++;
  }
  if (slot >= (int64_t)bindings->operations.declaration->maximum)
    qa_range_error("QVM component source client capacity exceeded");
  if (bindings->entry_count == bindings->entry_capacity) {
    bindings->entry_capacity = bindings->entry_capacity == 0 ? 8 : bindings->entry_capacity * 2;
    bindings->entries = realloc(bindings->entries, bindings->entry_capacity * sizeof(client_slot));
    if (bindings->entries == NULL) qa_range_error("QVM memory allocation failed");
  }
  client_slot *created = &((client_slot *)bindings->entries)[bindings->entry_count++];
  created->actor = actor;
  created->slot = slot;
  created->admitted = false;
  created->has_client = true;
  created->client = client;
  *out = slot;
  return true;
}

static void run_calls(qa_qvm_mod_client_bindings *bindings, const qa_qvm_mod_source_call *calls,
                      size_t count, qa_actor_id actor) {
  for (size_t i = 0; i < count; i++)
    bindings->operations.invoke(bindings->operations.ctx, &calls[i], actor, NULL);
}

static void admit(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor) {
  int64_t slot = 0;
  if (!qa_qvm_mod_client_bindings_slot(bindings, actor, &slot))
    qa_range_error("QVM component admission requires a live client");
  client_slot *entry = require_entry(bindings, actor);
  bindings->operations.project(bindings->operations.ctx, actor);
  if (!entry->admitted) {
    entry->admitted = true;
    run_calls(bindings, bindings->operations.declaration->admit,
              bindings->operations.declaration->admit_count, actor);
  }
  if (bindings->operations.admitted != NULL)
    bindings->operations.admitted(bindings->operations.ctx, actor);
}

static void client_event(void *ctx, qa_mod_client_event event) {
  qa_qvm_mod_client_bindings *bindings = ctx;
  qa_actor_id actor = event.identity.actor;
  if (strcmp(event.kind, "admitted") == 0) {
    admit(bindings, actor);
  } else if (strcmp(event.kind, "userinfo") == 0) {
    admit(bindings, actor);
    run_calls(bindings, bindings->operations.declaration->userinfo,
              bindings->operations.declaration->userinfo_count, actor);
  } else if (qa_qvm_mod_client_bindings_has(bindings, actor)) {
    (void)require_entry(bindings, actor);
    run_calls(bindings, bindings->operations.declaration->disconnect,
              bindings->operations.declaration->disconnect_count, actor);
    bindings->operations.release(bindings->operations.ctx, actor);
    qa_qvm_mod_client_bindings_forget(bindings, actor);
  }
}

typedef struct open_cleanup {
  qa_qvm_mod_client_bindings *bindings;
  qa_mod_client_application *application;
  void (*close)(void *ctx);
  void *close_ctx;
} open_cleanup;

static void remove_application(qa_qvm_mod_client_bindings *bindings, qa_mod_client_application *application) {
  for (size_t i = 0; i < bindings->application_count; i++) {
    if (bindings->applications[i] == application) {
      memmove(&bindings->applications[i], &bindings->applications[i + 1],
              (bindings->application_count - i - 1) * sizeof *bindings->applications);
      bindings->application_count--;
      return;
    }
  }
}

static void run_open_cleanup(void *ctx) {
  open_cleanup *cleanup = ctx;
  if (cleanup->close != NULL) cleanup->close(cleanup->close_ctx);
  remove_application(cleanup->bindings, cleanup->application);
  free(cleanup);
}

static void input_open(void *ctx, qa_mod_client_application *application,
                       void (**cleanup)(void *cleanup_ctx), void **cleanup_ctx) {
  qa_qvm_mod_client_bindings *bindings = ctx;
  client_slot *entry = require_entry(bindings, application->identity.actor);
  if (!entry->admitted) qa_range_error("QVM input callback requires admitted source client state");
  if (bindings->application_count == bindings->application_capacity) {
    bindings->application_capacity = bindings->application_capacity == 0 ? 8 : bindings->application_capacity * 2;
    bindings->applications = realloc(bindings->applications, bindings->application_capacity * sizeof *bindings->applications);
    if (bindings->applications == NULL) qa_range_error("QVM memory allocation failed");
  }
  bindings->applications[bindings->application_count++] = application;
  void (*close)(void *) = NULL;
  void *close_ctx = NULL;
  if (bindings->operations.open_input != NULL)
    bindings->operations.open_input(bindings->operations.ctx, application, &close, &close_ctx);
  open_cleanup *state = malloc(sizeof *state);
  if (state == NULL) qa_range_error("QVM memory allocation failed");
  state->bindings = bindings;
  state->application = application;
  state->close = close;
  state->close_ctx = close_ctx;
  *cleanup = run_open_cleanup;
  *cleanup_ctx = state;
}

static void input_invoke(void *ctx, const void *call, qa_mod_client_application *application) {
  qa_qvm_mod_client_bindings *bindings = ctx;
  (void)require_entry(bindings, application->identity.actor);
  bindings->operations.invoke(bindings->operations.ctx, call, application->identity.actor, application);
}

static void input_output(void *ctx, const void *outputs, size_t output_count,
                         size_t output_elem_size, qa_mod_client_application *application,
                         void (*run)(void *run_ctx), void *run_ctx,
                         qa_mod_client_input_output **out_outputs, size_t *out_count) {
  (void)output_elem_size;
  qa_qvm_mod_client_bindings *bindings = ctx;
  bindings->operations.output(bindings->operations.ctx, outputs, output_count, application,
                              run, run_ctx, out_outputs, out_count);
}

void qa_qvm_mod_client_bindings_start(qa_qvm_mod_client_bindings *bindings) {
  if (bindings->has_unsubscribe) return;
  qa_mod_client_services *services = bindings->operations.services;
  bindings->unsubscribe = services->subscribe(services->ctx, client_event, bindings);
  bindings->has_unsubscribe = true;
  size_t count = 0;
  const qa_mod_client_identity *clients = services->clients(services->ctx, &count);
  for (size_t i = 0; i < count; i++) admit(bindings, clients[i].actor);
  qa_mod_client_input_operations operations;
  memset(&operations, 0, sizeof operations);
  operations.ctx = bindings;
  operations.open = input_open;
  operations.invoke = input_invoke;
  operations.output = bindings->operations.output != NULL ? input_output : NULL;
  bindings->unsubscribe_input = qa_subscribe_mod_client_input(
      services, bindings->operations.declaration->input,
      bindings->operations.declaration->input_count, operations);
  bindings->has_unsubscribe_input = true;
}

void qa_qvm_mod_client_bindings_restore(qa_qvm_mod_client_bindings *bindings,
                                        const qa_qvm_mod_client_slot *entries, size_t count) {
  bindings->entry_count = 0;
  qa_mod_client_services *services = bindings->operations.services;
  for (size_t i = 0; i < count; i++) {
    if (bindings->entry_count == bindings->entry_capacity) {
      bindings->entry_capacity = bindings->entry_capacity == 0 ? 8 : bindings->entry_capacity * 2;
      bindings->entries = realloc(bindings->entries, bindings->entry_capacity * sizeof(client_slot));
      if (bindings->entries == NULL) qa_range_error("QVM memory allocation failed");
    }
    client_slot *created = &((client_slot *)bindings->entries)[bindings->entry_count++];
    created->actor = entries[i].actor;
    created->slot = entries[i].slot;
    created->admitted = entries[i].admitted;
    created->has_client = services->for_actor(services->ctx, entries[i].actor, &created->client);
  }
}

static int compare_slot(const void *left, const void *right) {
  int64_t a = ((const client_slot *)left)->slot;
  int64_t b = ((const client_slot *)right)->slot;
  return a < b ? -1 : a > b ? 1 : 0;
}

void qa_qvm_mod_client_bindings_frame(qa_qvm_mod_client_bindings *bindings,
                                      void (*invoke)(void *ctx, const qa_qvm_mod_source_call *call, qa_actor_id actor),
                                      void *invoke_ctx) {
  const qa_qvm_mod_source_call *calls = bindings->operations.declaration->frame;
  size_t call_count = bindings->operations.declaration->frame_count;
  if (call_count == 0) return;
  client_slot *clients = malloc((bindings->entry_count == 0 ? 1 : bindings->entry_count) * sizeof *clients);
  if (clients == NULL) qa_range_error("QVM memory allocation failed");
  size_t count = 0;
  client_slot *entries = bindings->entries;
  for (size_t i = 0; i < bindings->entry_count; i++) {
    if (entries[i].admitted && qa_qvm_mod_client_bindings_live(bindings, entries[i].actor))
      clients[count++] = *require_entry(bindings, entries[i].actor);
  }
  qsort(clients, count, sizeof *clients, compare_slot);
  for (size_t i = 0; i < count; i++) {
    for (size_t c = 0; c < call_count; c++) {
      client_slot *current = find_entry(bindings, clients[i].actor);
      if (current == NULL || current->slot != clients[i].slot ||
          !qa_qvm_mod_client_bindings_live(bindings, clients[i].actor))
        break;
      invoke(invoke_ctx, &calls[c], clients[i].actor);
    }
  }
  free(clients);
}

size_t qa_qvm_mod_client_bindings_players(qa_qvm_mod_client_bindings *bindings,
                                          qa_qvm_mod_client_slot *out, size_t capacity) {
  client_slot *entries = bindings->entries;
  for (size_t i = 0; i < bindings->entry_count && i < capacity; i++) {
    client_slot *required = require_entry(bindings, entries[i].actor);
    out[i].actor = entries[i].actor;
    out[i].slot = required->slot;
    out[i].admitted = required->admitted;
  }
  return bindings->entry_count;
}

size_t qa_qvm_mod_client_bindings_checkpoint(qa_qvm_mod_client_bindings *bindings,
                                             qa_qvm_mod_client_slot *out, size_t capacity) {
  if (bindings->application_count != 0)
    qa_range_error("Cannot save during QVM component input application");
  client_slot *entries = bindings->entries;
  for (size_t i = 0; i < bindings->entry_count && i < capacity; i++) {
    out[i].actor = entries[i].actor;
    out[i].slot = entries[i].slot;
    out[i].admitted = entries[i].admitted;
  }
  return bindings->entry_count;
}

void qa_qvm_mod_client_bindings_forget(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor) {
  client_slot *entries = bindings->entries;
  for (size_t i = 0; i < bindings->entry_count; i++) {
    if (qa_same_actor(entries[i].actor, actor)) {
      memmove(&entries[i], &entries[i + 1], (bindings->entry_count - i - 1) * sizeof *entries);
      bindings->entry_count--;
      return;
    }
  }
}

void qa_qvm_mod_client_bindings_close(qa_qvm_mod_client_bindings *bindings) {
  if (bindings->has_unsubscribe_input) bindings->unsubscribe_input.invoke(bindings->unsubscribe_input.ctx);
  bindings->has_unsubscribe_input = false;
  if (bindings->has_unsubscribe) bindings->unsubscribe.invoke(bindings->unsubscribe.ctx);
  bindings->has_unsubscribe = false;
  bindings->application_count = 0;
  bindings->entry_count = 0;
}

const char *qa_qvm_mod_client_bindings_get_userinfo(qa_qvm_mod_client_bindings *bindings, int64_t slot) {
  client_slot *entry = at_slot(bindings, slot);
  if (entry == NULL || !entry->has_client) return "";
  qa_mod_client_services *services = bindings->operations.services;
  return services->userinfo(services->ctx, entry->client);
}

void qa_qvm_mod_client_bindings_set_userinfo(qa_qvm_mod_client_bindings *bindings, int64_t slot, const char *value) {
  client_slot *entry = at_slot(bindings, slot);
  if (entry == NULL || !entry->has_client)
    qa_range_error("QVM component cannot update an unbound source client");
  qa_mod_client_services *services = bindings->operations.services;
  services->set_userinfo(services->ctx, entry->client, value);
}

void qa_qvm_mod_client_bindings_drop_client(qa_qvm_mod_client_bindings *bindings, int64_t slot, const char *reason) {
  client_slot *entry = at_slot(bindings, slot);
  if (entry != NULL && entry->has_client) {
    qa_mod_client_services *services = bindings->operations.services;
    services->drop(services->ctx, entry->client, reason, bindings->operations.content);
  }
}

void qa_qvm_mod_client_bindings_send_server_command(qa_qvm_mod_client_bindings *bindings, int64_t slot, const char *text) {
  if (slot == -1) {
    bindings->operations.send(bindings->operations.ctx, text, NULL);
    return;
  }
  client_slot *entry = at_slot(bindings, slot);
  if (entry != NULL) bindings->operations.send(bindings->operations.ctx, text, &entry->actor);
}

/* Mirrored q3CommandForControls intermediate (angles as a vector). */
typedef struct controls_command {
  double server_time;
  qa_vec3 angles;
  double buttons;
  double weapon;
  double forwardmove;
  double rightmove;
  double upmove;
} controls_command;

static int32_t command_buttons(const qa_user_command *command) {
  switch (command->kind) {
    case QA_USER_COMMAND_Q1: return (int32_t)command->as.q1.buttons;
    case QA_USER_COMMAND_QW: return (int32_t)command->as.qw.buttons;
    case QA_USER_COMMAND_Q2: return (int32_t)command->as.q2.buttons;
    case QA_USER_COMMAND_Q2_RERELEASE: return (int32_t)command->as.q2_rerelease.buttons;
    case QA_USER_COMMAND_Q3: return (int32_t)command->as.q3.buttons;
  }
  return 0;
}

/* Mirrored from TS src/app/bootstrap/simulation/q3-commands.ts. */
static controls_command q3_command_for_controls(const qa_user_command *command, double milliseconds,
                                                double requested_weapon, bool use_holdable) {
  int32_t buttons = command_buttons(command);
  int32_t mapped = (command->kind == QA_USER_COMMAND_Q3 ? buttons & ~4 : buttons & 1) | (use_holdable ? 4 : 0);
  controls_command result;
  memset(&result, 0, sizeof result);
  result.buttons = mapped;
  result.weapon = requested_weapon;
  if (command->kind == QA_USER_COMMAND_Q3) {
    result.server_time = command->as.q3.server_time_milliseconds;
    result.angles.x = command->as.q3.angle_words.values[0];
    result.angles.y = command->as.q3.angle_words.values[1];
    result.angles.z = command->as.q3.angle_words.values[2];
    result.forwardmove = command->as.q3.forward_move;
    result.rightmove = command->as.q3.right_move;
    result.upmove = command->as.q3.up_move;
    return result;
  }
  double forward = 0, side = 0, up = 0;
  if (command->kind == QA_USER_COMMAND_Q2) {
    result.angles.x = command->as.q2.angle_shorts.values[0];
    result.angles.y = command->as.q2.angle_shorts.values[1];
    result.angles.z = command->as.q2.angle_shorts.values[2];
    forward = command->as.q2.forward_move;
    side = command->as.q2.side_move;
    up = command->as.q2.up_move;
  } else {
    qa_vec3 value = command->kind == QA_USER_COMMAND_Q1 ? command->as.q1.view_angles
                    : command->kind == QA_USER_COMMAND_QW ? command->as.qw.angles
                                                          : command->as.q2_rerelease.angles;
    result.angles.x = (double)((int64_t)trunc(value.x * 65536.0 / 360.0) & 65535);
    result.angles.y = (double)((int64_t)trunc(value.y * 65536.0 / 360.0) & 65535);
    result.angles.z = (double)((int64_t)trunc(value.z * 65536.0 / 360.0) & 65535);
    if (command->kind == QA_USER_COMMAND_Q1) {
      forward = command->as.q1.forward_move; side = command->as.q1.side_move; up = command->as.q1.up_move;
    } else if (command->kind == QA_USER_COMMAND_QW) {
      forward = command->as.qw.forward_move; side = command->as.qw.side_move; up = command->as.qw.up_move;
    } else {
      forward = command->as.q2_rerelease.forward_move; side = command->as.q2_rerelease.side_move;
    }
  }
  double scale = (command->kind == QA_USER_COMMAND_Q1 || command->kind == QA_USER_COMMAND_QW) ? 320.0 : 200.0;
  result.server_time = trunc(milliseconds);
  result.forwardmove = trunc(fmax(-127.0, fmin(127.0, forward * 127.0 / scale)));
  result.rightmove = trunc(fmax(-127.0, fmin(127.0, side * 127.0 / scale)));
  if (command->kind == QA_USER_COMMAND_Q2_RERELEASE)
    result.upmove = (buttons & 8) != 0 ? 127 : (buttons & 16) != 0 ? -127 : 0;
  else if ((command->kind == QA_USER_COMMAND_Q1 || command->kind == QA_USER_COMMAND_QW) && (buttons & 2) != 0)
    result.upmove = 127;
  else
    result.upmove = trunc(fmax(-127.0, fmin(127.0, up * 127.0 / scale)));
  return result;
}

/* Mirrored from TS src/app/bootstrap/simulation/q3-commands.ts. */
static controls_command relative_q3_source_command(qa_command_source_kind source, qa_user_command_kind dialect,
                                                   controls_command command, qa_vec3 delta,
                                                   const char *angle_space) {
  bool absolute = (angle_space != NULL && strcmp(angle_space, "absolute") == 0) ||
      (angle_space == NULL && (source == QA_COMMAND_SOURCE_LOCAL_SEAT || source == QA_COMMAND_SOURCE_BOT) &&
       dialect != QA_USER_COMMAND_Q3);
  if (!absolute) return command;
  command.angles.x -= delta.x;
  command.angles.y -= delta.y;
  command.angles.z -= delta.z;
  return command;
}

static qa_mod_client_application *applied(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor) {
  for (size_t i = bindings->application_count; i-- > 0;) {
    if (qa_same_actor(bindings->applications[i]->identity.actor, actor))
      return bindings->applications[i];
  }
  return NULL;
}

static double requested_weapon(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor, double fallback) {
  double value = 0;
  if (bindings->operations.requested_weapon != NULL &&
      bindings->operations.requested_weapon(bindings->operations.ctx, actor, &value))
    return value;
  return fallback;
}

qa_q3_wire_user_command qa_qvm_mod_client_bindings_get_user_command(qa_qvm_mod_client_bindings *bindings, int64_t slot) {
  client_slot *entry = at_slot(bindings, slot);
  qa_mod_client_application *application = entry == NULL ? NULL : applied(bindings, entry->actor);
  double storage[64];
  qa_q3_player_state ps;
  memset(&ps, 0, sizeof ps);
  ps.stats.values = storage;
  ps.persistent.values = storage + 16;
  ps.powerups.values = storage + 32;
  ps.ammo.values = storage + 48;
  if (application != NULL && entry != NULL) {
    bindings->operations.player_state(bindings->operations.ctx, entry->actor, &ps);
    qa_source_time time = application->frame.time;
    double milliseconds = time.kind == QA_SOURCE_TIME_SECONDS ? time.value * 1000 : time.value;
    bool use_holdable = application->command.kind == QA_USER_COMMAND_Q3 &&
                        ((int32_t)application->command.as.q3.buttons & 4) != 0;
    controls_command command = q3_command_for_controls(&application->command, milliseconds,
        requested_weapon(bindings, entry->actor, ps.weapon), use_holdable);
    qa_q3_wire_user_command result;
    result.server_time = (int32_t)command.server_time;
    result.buttons = (int32_t)command.buttons;
    result.weapon = (int32_t)command.weapon;
    result.forwardmove = (int32_t)command.forwardmove;
    result.rightmove = (int32_t)command.rightmove;
    result.upmove = (int32_t)command.upmove;
    result.angles[0] = (int32_t)((int64_t)trunc(application->absolute_aim.x * 65536.0 / 360.0) & 65535) - (int32_t)ps.delta_angle_words.values[0];
    result.angles[1] = (int32_t)((int64_t)trunc(application->absolute_aim.y * 65536.0 / 360.0) & 65535) - (int32_t)ps.delta_angle_words.values[1];
    result.angles[2] = (int32_t)((int64_t)trunc(application->absolute_aim.z * 65536.0 / 360.0) & 65535) - (int32_t)ps.delta_angle_words.values[2];
    return result;
  }
  qa_mod_client_services *services = bindings->operations.services;
  const qa_mod_client_command_receipt *accepted =
      entry == NULL || !entry->has_client ? NULL : services->command(services->ctx, entry->client);
  if (entry == NULL || accepted == NULL)
    qa_range_error("QVM component usercmd requires an accepted destination client command");
  const qa_actor_command *input = &accepted->input;
  double milliseconds = accepted->time.kind == QA_SOURCE_TIME_SECONDS ? accepted->time.value * 1000 : accepted->time.value;
  bindings->operations.player_state(bindings->operations.ctx, entry->actor, &ps);
  bool use_holdable;
  if (input->arsenal != NULL) use_holdable = input->arsenal->use_holdable;
  else use_holdable = input->command.kind == QA_USER_COMMAND_Q3 && ((int32_t)input->command.as.q3.buttons & 4) != 0;
  controls_command converted = q3_command_for_controls(&input->command, milliseconds,
      requested_weapon(bindings, entry->actor, ps.weapon), use_holdable);
  qa_vec3 delta = {ps.delta_angle_words.values[0], ps.delta_angle_words.values[1], ps.delta_angle_words.values[2]};
  controls_command command = relative_q3_source_command(input->source.kind, input->command.kind,
                                                        converted, delta, input->angle_space);
  qa_q3_wire_user_command result;
  result.server_time = (int32_t)command.server_time;
  result.angles[0] = (int32_t)command.angles.x;
  result.angles[1] = (int32_t)command.angles.y;
  result.angles[2] = (int32_t)command.angles.z;
  result.buttons = (int32_t)command.buttons;
  result.weapon = (int32_t)command.weapon;
  result.forwardmove = (int32_t)command.forwardmove;
  result.rightmove = (int32_t)command.rightmove;
  result.upmove = (int32_t)command.upmove;
  return result;
}
