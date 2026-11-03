/* Port of src/app/bootstrap/simulation/mod-client-checkpoint.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/mod_client_checkpoint.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"

static void put(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child)) qa_range_error("Out of memory");
}
static qa_save_value *object(void) {
  qa_save_value *o = qa_save_value_object();
  if (o == NULL) qa_range_error("Out of memory");
  return o;
}
static qa_save_value *number(double value) { return qa_save_value_number(value); }
static qa_save_value *string(const char *value) { return qa_save_value_string(value); }
static qa_save_value *vector(qa_vec3 v) {
  qa_save_value *o = object();
  put(o, "x", number(v.x)); put(o, "y", number(v.y)); put(o, "z", number(v.z));
  return o;
}
static qa_save_value *words(qa_angle_words w) {
  qa_save_value *a = qa_save_value_array();
  if (a == NULL) qa_range_error("Out of memory");
  for (int i = 0; i < 3; i++) if (!qa_save_value_push(a, number(w.values[i]))) qa_range_error("Out of memory");
  return a;
}
static const char *time_kind(qa_source_time_kind kind) { return kind == QA_SOURCE_TIME_SECONDS ? "seconds" : "milliseconds"; }
static qa_save_value *command_value(const qa_user_command *command) {
  qa_save_value *c = object();
  switch (command->kind) {
    case QA_USER_COMMAND_Q1: {
      const qa_q1_user_command *q = &command->as.q1;
      put(c, "kind", string("q1-netquake"));
      put(c, "acknowledgedServerTimeSeconds", number(q->acknowledged_server_time_seconds));
      put(c, "viewAngles", vector(q->view_angles));
      put(c, "forwardMove", number(q->forward_move)); put(c, "sideMove", number(q->side_move));
      put(c, "upMove", number(q->up_move)); put(c, "buttons", number(q->buttons)); put(c, "impulse", number(q->impulse));
      break;
    }
    case QA_USER_COMMAND_QW: {
      const qa_qw_user_command *q = &command->as.qw;
      put(c, "kind", string("q1-quakeworld"));
      put(c, "milliseconds", number(q->milliseconds)); put(c, "angles", vector(q->angles));
      put(c, "forwardMove", number(q->forward_move)); put(c, "sideMove", number(q->side_move));
      put(c, "upMove", number(q->up_move)); put(c, "buttons", number(q->buttons)); put(c, "impulse", number(q->impulse));
      break;
    }
    case QA_USER_COMMAND_Q2: {
      const qa_q2_user_command *q = &command->as.q2;
      put(c, "kind", string("q2-classic"));
      put(c, "milliseconds", number(q->milliseconds)); put(c, "angleShorts", words(q->angle_shorts));
      put(c, "forwardMove", number(q->forward_move)); put(c, "sideMove", number(q->side_move));
      put(c, "upMove", number(q->up_move)); put(c, "buttons", number(q->buttons)); put(c, "impulse", number(q->impulse));
      put(c, "lightLevel", number(q->light_level));
      break;
    }
    case QA_USER_COMMAND_Q2_RERELEASE: {
      const qa_q2_rerelease_user_command *q = &command->as.q2_rerelease;
      put(c, "kind", string("q2-rerelease"));
      put(c, "milliseconds", number(q->milliseconds)); put(c, "angles", vector(q->angles));
      put(c, "forwardMove", number(q->forward_move)); put(c, "sideMove", number(q->side_move));
      put(c, "buttons", number(q->buttons)); put(c, "serverFrame", number(q->server_frame));
      break;
    }
    case QA_USER_COMMAND_Q3: {
      const qa_q3_user_command *q = &command->as.q3;
      put(c, "kind", string("q3"));
      put(c, "serverTimeMilliseconds", number(q->server_time_milliseconds)); put(c, "angleWords", words(q->angle_words));
      put(c, "buttons", number(q->buttons)); put(c, "weapon", number(q->weapon));
      put(c, "forwardMove", number(q->forward_move)); put(c, "rightMove", number(q->right_move)); put(c, "upMove", number(q->up_move));
      break;
    }
  }
  return c;
}

qa_save_value *qa_app_sim_capture_mod_client_command(const qa_mod_client_command_receipt *value, qa_client_id client) {
  const qa_actor_command *input = &value->input;
  qa_save_value *o = object();
  qa_saved_actor_id actor = qa_saved_actor_id_of(input->actor);
  qa_save_value *a = object();
  put(a, "slot", number(actor.slot)); put(a, "generation", number(actor.generation));
  put(o, "actor", a);
  qa_save_value *c = object();
  put(c, "slot", number((double)client.slot)); put(c, "generation", number((double)client.generation));
  put(o, "client", c);
  qa_save_value *t = object();
  put(t, "kind", string(time_kind(value->time.kind))); put(t, "value", number(value->time.value));
  put(o, "time", t);
  qa_save_value *s = object();
  if (input->source.kind == QA_COMMAND_SOURCE_LOCAL_SEAT) {
    put(s, "kind", string("local-seat")); put(s, "seat", number((double)input->source.seat.index));
  } else if (input->source.kind == QA_COMMAND_SOURCE_BOT) {
    put(s, "kind", string("bot")); put(s, "provider", string(input->source.provider));
  } else put(s, "kind", string("remote-client"));
  put(o, "source", s);
  put(o, "sequence", number(input->sequence));
  put(o, "command", command_value(&input->command));
  if (input->arsenal == NULL) put(o, "arsenal", qa_save_value_null());
  else {
    qa_save_value *r = object();
    put(r, "provider", string(input->arsenal->provider));
    put(r, "weapon", input->arsenal->weapon == NULL ? qa_save_value_null() : string(input->arsenal->weapon));
    put(r, "useHoldable", qa_save_value_boolean(input->arsenal->use_holdable));
    if (input->arsenal->has_impulse) put(r, "impulse", number(input->arsenal->impulse));
    put(o, "arsenal", r);
  }
  return o;
}

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
static double integer_field(const qa_save_reader *reader, const char *name, bool has_minimum, double minimum) {
  qa_save_reader f = field(reader, name);
  double value = has_minimum ? qa_save_reader_integer_min(&f, minimum) : qa_save_reader_integer(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double finite_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_vec3 vector_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  qa_vec3 value = qa_read_vector(&f);
  qa_save_reader_destroy(&f);
  return value;
}
/* TS words(reader). */
static qa_angle_words words_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  size_t count = qa_save_reader_list_count(&f);
  qa_angle_words out;
  memset(&out, 0, sizeof out);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&f, i, &item)) qa_range_error("Out of memory");
    double value = qa_save_reader_integer(&item);
    if (i < 3) out.values[i] = value;
    qa_save_reader_destroy(&item);
  }
  if (count != 3) qa_save_reader_fail(&f, "Expected three command angle words");
  qa_save_reader_destroy(&f);
  return out;
}
static qa_user_command read_command(const qa_save_reader *reader) {
  static const char *const kinds[] = {"q1-netquake", "q1-quakeworld", "q2-classic", "q2-rerelease", "q3"};
  qa_save_reader k = field(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&k, kinds, 5);
  qa_save_reader_destroy(&k);
  double buttons = integer_field(reader, "buttons", true, 0), forward = finite_field(reader, "forwardMove");
  qa_user_command out;
  memset(&out, 0, sizeof out);
  if (strcmp(kind, "q3") == 0) {
    out.kind = QA_USER_COMMAND_Q3;
    qa_q3_user_command *q = &out.as.q3;
    q->buttons = buttons; q->forward_move = forward;
    q->server_time_milliseconds = integer_field(reader, "serverTimeMilliseconds", false, 0);
    q->angle_words = words_field(reader, "angleWords");
    q->weapon = integer_field(reader, "weapon", false, 0);
    q->right_move = finite_field(reader, "rightMove");
    q->up_move = finite_field(reader, "upMove");
    return out;
  }
  double side = finite_field(reader, "sideMove");
  if (strcmp(kind, "q2-rerelease") == 0) {
    out.kind = QA_USER_COMMAND_Q2_RERELEASE;
    qa_q2_rerelease_user_command *q = &out.as.q2_rerelease;
    q->buttons = buttons; q->forward_move = forward; q->side_move = side;
    q->milliseconds = finite_field(reader, "milliseconds");
    q->angles = vector_field(reader, "angles");
    q->server_frame = integer_field(reader, "serverFrame", false, 0);
    return out;
  }
  double up = finite_field(reader, "upMove"), impulse = integer_field(reader, "impulse", true, 0);
  if (strcmp(kind, "q1-netquake") == 0) {
    out.kind = QA_USER_COMMAND_Q1;
    qa_q1_user_command *q = &out.as.q1;
    q->buttons = buttons; q->forward_move = forward; q->side_move = side; q->up_move = up; q->impulse = impulse;
    q->acknowledged_server_time_seconds = finite_field(reader, "acknowledgedServerTimeSeconds");
    q->view_angles = vector_field(reader, "viewAngles");
    return out;
  }
  double milliseconds = finite_field(reader, "milliseconds");
  if (strcmp(kind, "q1-quakeworld") == 0) {
    out.kind = QA_USER_COMMAND_QW;
    qa_qw_user_command *q = &out.as.qw;
    q->buttons = buttons; q->forward_move = forward; q->side_move = side; q->up_move = up; q->impulse = impulse;
    q->milliseconds = milliseconds;
    q->angles = vector_field(reader, "angles");
    return out;
  }
  out.kind = QA_USER_COMMAND_Q2;
  qa_q2_user_command *q = &out.as.q2;
  q->buttons = buttons; q->forward_move = forward; q->side_move = side; q->up_move = up; q->impulse = impulse;
  q->milliseconds = milliseconds;
  q->angle_shorts = words_field(reader, "angleShorts");
  q->light_level = integer_field(reader, "lightLevel", true, 0);
  return out;
}

typedef struct read_state { qa_app_sim_mod_client_commands result; } read_state;
static void release_partial(void *arg) { qa_app_sim_mod_client_commands_free(&((read_state *)arg)->result); }

qa_app_sim_mod_client_commands qa_app_sim_read_mod_client_commands(const qa_save_reader *reader,
                                                                   qa_app_sim_mod_client_command_host host) {
  read_state state;
  memset(&state, 0, sizeof state);
  if (qa_save_reader_is_undefined(reader)) return state.result;
  size_t count = qa_save_reader_list_count(reader);
  /* A raise abandons this frame: the cleanup node frees partial storage. */
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, release_partial, &state);
  state.result.commands = calloc(count == 0 ? 1 : count, sizeof *state.result.commands);
  state.result.arsenals = calloc(count == 0 ? 1 : count, sizeof *state.result.arsenals);
  if (state.result.commands == NULL || state.result.arsenals == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry;
    if (!qa_save_reader_item(reader, i, &entry)) qa_range_error("Out of memory");
    qa_save_reader saved_actor = field(&entry, "actor");
    qa_actor_id actor = host.actor(host.ctx, qa_read_saved_actor(&saved_actor));
    qa_save_reader_destroy(&saved_actor);
    qa_client_id client;
    bool has_client = host.client(host.ctx, actor, &client);
    qa_save_reader saved_client = field(&entry, "client");
    (void)integer_field(&saved_client, "generation", true, 0);
    bool seen = false;
    for (size_t j = 0; j < state.result.count; j++)
      if (qa_same_actor(state.result.commands[j].input.actor, actor)) seen = true;
    if (!has_client || (double)client.slot != integer_field(&saved_client, "slot", true, 0) || seen)
      qa_save_reader_fail(&entry, "Saved command requires one live restored client");
    qa_save_reader_destroy(&saved_client);
    qa_mod_client_command_receipt *command = &state.result.commands[i];
    command->input.actor = actor;
    qa_save_reader owner = field(&entry, "source"), k = field(&owner, "kind");
    static const char *const kinds[] = {"local-seat", "remote-client", "bot"};
    const char *kind = qa_save_reader_choice_string(&k, kinds, 3);
    qa_save_reader_destroy(&k);
    qa_command_source source;
    memset(&source, 0, sizeof source);
    if (strcmp(kind, "local-seat") == 0) {
      source.kind = QA_COMMAND_SOURCE_LOCAL_SEAT;
      source.client = client;
      source.seat = qa_identity_owner_seat(host.identity, (int64_t)integer_field(&owner, "seat", true, 0));
    } else if (strcmp(kind, "remote-client") == 0) {
      source.kind = QA_COMMAND_SOURCE_REMOTE_CLIENT;
      source.client = client;
    } else {
      source.kind = QA_COMMAND_SOURCE_BOT;
      qa_save_reader p = field(&owner, "provider");
      source.provider = qa_namespaced(&p);
      qa_save_reader_destroy(&p);
    }
    qa_save_reader_destroy(&owner);
    qa_save_reader arsenal = field(&entry, "arsenal");
    bool has_arsenal = !qa_save_reader_is_null(&arsenal);
    if (has_arsenal) {
      qa_arsenal_intent *intent = &state.result.arsenals[i];
      qa_save_reader p = field(&arsenal, "provider");
      intent->provider = qa_namespaced(&p);
      qa_save_reader_destroy(&p);
      qa_save_reader w = field(&arsenal, "weapon");
      intent->weapon = qa_save_reader_is_null(&w) ? NULL : qa_namespaced(&w);
      qa_save_reader_destroy(&w);
      qa_save_reader u = field(&arsenal, "useHoldable");
      intent->use_holdable = qa_save_reader_boolean(&u);
      qa_save_reader_destroy(&u);
      qa_save_reader im = field(&arsenal, "impulse");
      if (!qa_save_reader_is_undefined(&im)) {
        intent->has_impulse = true;
        intent->impulse = qa_save_reader_integer_min(&im, 0);
      }
      qa_save_reader_destroy(&im);
    }
    qa_save_reader_destroy(&arsenal);
    qa_save_reader t = field(&entry, "time");
    command->time = qa_read_time(&t);
    qa_save_reader_destroy(&t);
    command->input.source = source;
    command->input.sequence = integer_field(&entry, "sequence", true, 0);
    command->input.angle_space = "absolute";
    qa_save_reader c = field(&entry, "command");
    command->input.command = read_command(&c);
    qa_save_reader_destroy(&c);
    command->input.arsenal = has_arsenal ? &state.result.arsenals[i] : NULL;
    state.result.count = i + 1;
    qa_save_reader_destroy(&entry);
  }
  qa_cleanup_pop(&cleanup);
  return state.result;
}
void qa_app_sim_mod_client_commands_free(qa_app_sim_mod_client_commands *commands) {
  free(commands->commands);
  free(commands->arsenals);
  memset(commands, 0, sizeof *commands);
}
