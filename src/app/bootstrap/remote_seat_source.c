/* Port of src/app/bootstrap/remote-seat-source.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/remote_seat_source.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/app/bootstrap/network/q3_client_content.h"
#include "qa/app/bootstrap/network/q3_client_downloads.h"
#include "qa/app/bootstrap/network/q3_downloads.h"
#include "qa/app/bootstrap/network/qw_downloads.h"
#include "qa/app/bootstrap/remote_access.h"
#include "qa/app/bootstrap/server_map.h"
#include "qa/content/catalog/index.h"
#include "qa/foundation/support.h"
#include "qa/app/bootstrap/startup_selection.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q1/checksum.h"
#include "qa/network/q3/admission.h"
#include "qa/network/q3/pak_references.h"
#include "qa/network/q3/pure.h"
#include "qa/world/collision/q3/settings.h"

/* ---- RemoteSeatPresentation / RemoteSeatNetwork dispatch ----------------- */
qa_client_id qa_app_remote_seat_presentation_client(const qa_app_remote_seat_presentation *remote) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return qa_app_unified_remote_client(remote->unified);
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_remote_client(remote->q1);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_remote_client(remote->qw);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_remote_client(remote->q2);
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_remote_client(remote->q3);
  }
  qa_range_error("Unknown remote family");
}
bool qa_app_remote_seat_presentation_player(const qa_app_remote_seat_presentation *remote, qa_actor_id *out) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: {
      qa_client_id client;
      double entity = 0;
      return qa_app_unified_remote_player(remote->unified, out, &client, &entity);
    }
    case QA_APP_REMOTE_FAMILY_Q1: {
      qa_app_q1_player player;
      if (!qa_app_q1_remote_player(remote->q1, &player)) return false;
      *out = player.actor;
      return true;
    }
    case QA_APP_REMOTE_FAMILY_QW: {
      qa_app_q1_player player;
      if (!qa_app_qw_remote_player(remote->qw, &player)) return false;
      *out = player.actor;
      return true;
    }
    case QA_APP_REMOTE_FAMILY_Q2: {
      qa_app_q2_player player;
      if (!qa_app_q2_remote_player_of(remote->q2, &player)) return false;
      *out = player.actor;
      return true;
    }
    case QA_APP_REMOTE_FAMILY_Q3: {
      qa_app_q3_player player;
      if (!qa_app_q3_remote_player(remote->q3, &player)) return false;
      *out = player.actor;
      return true;
    }
  }
  qa_range_error("Unknown remote family");
}
qa_shared_scene_queries *qa_app_remote_seat_presentation_scene(const qa_app_remote_seat_presentation *remote) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return qa_app_unified_remote_scene(remote->unified);
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_remote_scene(remote->q1);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_remote_scene(remote->qw);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_remote_scene(remote->q2);
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_remote_scene(remote->q3);
  }
  qa_range_error("Unknown remote family");
}
bool qa_app_remote_seat_presentation_is_player(const qa_app_remote_seat_presentation *remote, qa_actor_id actor) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return qa_app_unified_remote_is_player(remote->unified, actor);
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_remote_is_player(remote->q1, actor);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_remote_is_player(remote->qw, actor);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_remote_is_player(remote->q2, actor);
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_remote_is_player(remote->q3, actor);
  }
  qa_range_error("Unknown remote family");
}
const qa_simulation_output *qa_app_remote_seat_presentation_output(const qa_app_remote_seat_presentation *remote) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return qa_app_unified_remote_output(remote->unified);
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_remote_output(remote->q1);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_remote_output(remote->qw);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_remote_output(remote->q2);
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_remote_output(remote->q3);
  }
  qa_range_error("Unknown remote family");
}
const qa_app_simulation_presentation *qa_app_remote_seat_presentation_presentations(
    const qa_app_remote_seat_presentation *remote, size_t *out_count) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return qa_app_unified_remote_presentations(remote->unified, out_count);
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_remote_presentations(remote->q1, out_count);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_remote_presentations(remote->qw, out_count);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_remote_presentations(remote->q2, out_count);
    /* TS Q3RemotePresentation.presentations() is [] */
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_remote_presentations(remote->q3, out_count);
  }
  qa_range_error("Unknown remote family");
}
const qa_q3_character_view *qa_app_remote_seat_presentation_character_views(
    const qa_app_remote_seat_presentation *remote, size_t *out_count) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return qa_app_unified_remote_character_views(remote->unified, out_count);
    /* TS Q1/QW/Q3 characterViews() are [] */
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_remote_character_views(remote->q1, out_count);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_remote_character_views(remote->qw, out_count);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_remote_character_views(remote->q2, out_count);
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_remote_character_views(remote->q3, out_count);
  }
  qa_range_error("Unknown remote family");
}
const qa_app_sim_presentation_event *qa_app_remote_seat_presentation_drain_events(
    const qa_app_remote_seat_presentation *remote, size_t *out_count) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return qa_app_unified_remote_drain_presentation_events(remote->unified, out_count);
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_remote_drain_events(remote->q1, out_count);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_remote_drain_events(remote->qw, out_count);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_remote_drain_events(remote->q2, out_count);
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_remote_drain_events(remote->q3, out_count);
  }
  qa_range_error("Unknown remote family");
}
qa_app_sim_player_view qa_app_remote_seat_presentation_player_view(const qa_app_remote_seat_presentation *remote,
    qa_actor_id actor) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return qa_app_unified_remote_player_view(remote->unified, actor);
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_remote_player_view(remote->q1, actor);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_remote_player_view(remote->qw, actor);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_remote_player_view(remote->q2, actor);
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_remote_player_view(remote->q3, actor);
  }
  qa_range_error("Unknown remote family");
}
const qa_world_text *qa_app_remote_seat_presentation_world_text(const qa_app_remote_seat_presentation *remote,
    size_t *out_count) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return qa_app_unified_remote_world_text(remote->unified, out_count);
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_remote_world_text(remote->q1, out_count);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_remote_world_text(remote->qw, out_count);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_remote_world_text(remote->q2, out_count);
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_remote_world_text(remote->q3, out_count);
  }
  qa_range_error("Unknown remote family");
}
void qa_app_remote_seat_presentation_disconnected(const qa_app_remote_seat_presentation *remote, const char *reason) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: qa_app_unified_remote_disconnected(remote->unified, reason); return;
    case QA_APP_REMOTE_FAMILY_Q1: qa_app_q1_remote_disconnected(remote->q1, reason); return;
    case QA_APP_REMOTE_FAMILY_QW: qa_app_qw_remote_disconnected(remote->qw, reason); return;
    case QA_APP_REMOTE_FAMILY_Q2: qa_app_q2_remote_disconnected(remote->q2, reason); return;
    case QA_APP_REMOTE_FAMILY_Q3: qa_app_q3_remote_disconnected(remote->q3, reason); return;
  }
}
void qa_app_remote_seat_presentation_destroy(qa_app_remote_seat_presentation *remote) {
  qa_app_unified_remote_destroy(remote->unified);
  qa_app_q1_remote_destroy(remote->q1);
  qa_app_qw_remote_destroy(remote->qw);
  qa_app_q2_remote_destroy(remote->q2);
  qa_app_q3_remote_destroy(remote->q3);
  remote->unified = NULL; remote->q1 = NULL; remote->qw = NULL; remote->q2 = NULL; remote->q3 = NULL;
}

qa_app_network_phase qa_app_remote_seat_network_phase(const qa_app_remote_seat_network *network) {
  switch (network->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return qa_app_unified_client_network_phase(network->unified);
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_client_network_phase(network->q1);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_client_network_phase(network->qw);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_client_network_phase(network->q2);
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_client_network_phase(network->q3);
  }
  qa_range_error("Unknown remote family");
}
void qa_app_remote_seat_network_poll(const qa_app_remote_seat_network *network, double now) {
  size_t count = 0;
  switch (network->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: (void)qa_app_unified_client_network_poll(network->unified, now, &count); return;
    case QA_APP_REMOTE_FAMILY_Q1: (void)qa_app_q1_client_network_poll(network->q1, now, &count); return;
    case QA_APP_REMOTE_FAMILY_QW: (void)qa_app_qw_client_network_poll(network->qw, now, &count); return;
    case QA_APP_REMOTE_FAMILY_Q2: (void)qa_app_q2_client_network_poll(network->q2, now, &count); return;
    case QA_APP_REMOTE_FAMILY_Q3: qa_app_q3_client_network_poll(network->q3, now); return;
  }
}
void qa_app_remote_seat_network_command(const qa_app_remote_seat_network *network, const char *text) {
  switch (network->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: qa_app_unified_client_network_command(network->unified, text); return;
    case QA_APP_REMOTE_FAMILY_Q1: qa_app_q1_client_network_command(network->q1, text); return;
    case QA_APP_REMOTE_FAMILY_QW: qa_app_qw_client_network_command(network->qw, text); return;
    case QA_APP_REMOTE_FAMILY_Q2: qa_app_q2_client_network_command(network->q2, text); return;
    case QA_APP_REMOTE_FAMILY_Q3: qa_app_q3_client_network_command(network->q3, text); return;
  }
}
void qa_app_remote_seat_network_submit(const qa_app_remote_seat_network *network, const qa_actor_command *commands,
    size_t count, double now) {
  if (network->family == QA_APP_REMOTE_FAMILY_UNIFIED) {
    qa_app_unified_client_network_submit(network->unified, commands, count, now);
    return;
  }
  const void **rows = calloc(count ? count : 1, sizeof *rows);
  if (rows == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) rows[i] = &commands[i];
  switch (network->family) {
    case QA_APP_REMOTE_FAMILY_Q1: qa_app_q1_client_network_submit(network->q1, rows, count, now); break;
    case QA_APP_REMOTE_FAMILY_QW: qa_app_qw_client_network_submit(network->qw, rows, count, now); break;
    case QA_APP_REMOTE_FAMILY_Q2: qa_app_q2_client_network_submit(network->q2, rows, count, now); break;
    case QA_APP_REMOTE_FAMILY_Q3: qa_app_q3_client_network_submit(network->q3, rows, count, now); break;
    case QA_APP_REMOTE_FAMILY_UNIFIED: break;
  }
  free(rows);
}
void qa_app_remote_seat_network_close(const qa_app_remote_seat_network *network) {
  switch (network->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: qa_app_unified_client_network_close(network->unified); return;
    case QA_APP_REMOTE_FAMILY_Q1: qa_app_q1_client_network_close(network->q1); return;
    case QA_APP_REMOTE_FAMILY_QW: qa_app_qw_client_network_close(network->qw); return;
    case QA_APP_REMOTE_FAMILY_Q2: qa_app_q2_client_network_close(network->q2); return;
    case QA_APP_REMOTE_FAMILY_Q3: qa_app_q3_client_network_close(network->q3); return;
  }
}
void qa_app_remote_seat_network_destroy(qa_app_remote_seat_network *network) {
  qa_app_unified_client_network_destroy(network->unified);
  qa_app_q1_client_network_destroy(network->q1);
  qa_app_qw_client_network_destroy(network->qw);
  qa_app_q2_client_network_destroy(network->q2);
  qa_app_q3_client_network_destroy(network->q3);
  network->unified = NULL; network->q1 = NULL; network->qw = NULL; network->q2 = NULL; network->q3 = NULL;
}

/* ---- RemoteSeatSource ----------------------------------------------------- */
typedef struct qw_binding { qa_app_remote_seat_source *source; double generation; char *game_root, *skin_root; } qw_binding;
typedef struct q3_binding {
  qa_app_remote_seat_source *source;
  qa_q3_client_connection *connection;
  int64_t generation;
  qa_app_q3_client_downloads *downloads;
  qa_app_q3_download_mounts mounts; /* strings owned */
} q3_binding;

struct qa_app_remote_seat_source {
  qa_app_remote_access access; /* first: the ctx of the presentation's accessors */
  qa_app_remote_seat_source_options owner;
  qa_app_remote_seat_presentation remote;
  qa_app_remote_seat_network network;
  qa_datagram_transport datagram;
  qa_app_qw_download_receiver *qw_downloads; qw_binding *qw_binding;
  qa_app_q3_client_downloads *q3_downloads; q3_binding *q3_binding;
  qa_app_q3_client_content *q3_content;
  double download_generation;
  bool closed, closing;
  char *all_skins;
  qa_collision_map_settings collision;
  char *userinfo; char *seat_name;
  qa_app_client_download_progress *progress; size_t progress_capacity;
};

static void selection_release(void *selection) { qa_remote_content_selection_free(selection); }
static void *xcalloc(size_t count, size_t size) {
  void *out = calloc(count ? count : 1, size);
  if (out == NULL) qa_range_error("Out of memory");
  return out;
}
static char *xdup(const char *text) {
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, length + 1);
  return out;
}
static qa_cvar_registry *cvars(qa_app_remote_seat_source *s) { return s->owner.cvars(s->owner.cvars_ctx); }
static const char *keep_userinfo(qa_app_remote_seat_source *s, char *text) {
  if (text == NULL) qa_range_error("Out of memory");
  free(s->userinfo);
  s->userinfo = text;
  return text;
}
static void assert_open(qa_app_remote_seat_source *s) {
  if (s->closed) qa_raise("Error", 0, 0, "Remote seat source is retired");
}

/* common: { presentationTime, publish, print, sendCommand, disconnected } */
static double c_presentation_time(void *ctx) {
  qa_app_remote_seat_source *s = ctx;
  return qa_app_presentation_time_milliseconds(s->owner.clock);
}
static void c_publish(void *ctx, const qa_simulation_output *output) {
  qa_app_remote_seat_source *s = ctx;
  s->owner.hooks.publish(s->owner.hooks.ctx, output);
}
static void c_print(void *ctx, const char *text) {
  qa_app_remote_seat_source *s = ctx;
  s->owner.hooks.print(s->owner.hooks.ctx, text);
}
static void c_send_command(void *ctx, const char *text) {
  qa_app_remote_seat_source *s = ctx;
  qa_app_remote_seat_network_command(&s->network, text);
}
static void c_disconnected(void *ctx, const char *reason) {
  qa_app_remote_seat_source *s = ctx;
  s->owner.hooks.disconnected(s->owner.hooks.ctx, reason);
}
static const char *c_userinfo(void *ctx) {
  qa_app_remote_seat_source *s = ctx;
  return keep_userinfo(s, qa_cvar_registry_info_string(cvars(s), QA_CVAR_FLAG_USER_INFO, -1));
}
static qa_app_loaded_content *remember(qa_app_remote_seat_source *s, qa_app_loaded_content *content) {
  s->access.loaded = content;
  return content;
}

/* ---- unified ---- */
static void *u_load_content(void *ctx, const qa_app_unified_control *offer, qa_app_unified_operation_fn assert_current,
    void *assert_ctx) {
  qa_app_remote_seat_source *s = ctx;
  return remember(s, s->owner.unified.load_content(s->owner.unified.ctx, offer, assert_current, assert_ctx));
}
static qa_decoded_model u_model(void *ctx, const qa_app_unified_resource_key *key, bool has_brush, double brush) {
  qa_app_remote_seat_source *s = ctx;
  return s->owner.unified.model(s->owner.unified.ctx, key, has_brush, brush);
}
static void u_publish_components(void *ctx, qa_app_unified_operation_fn operation, void *operation_ctx) {
  qa_app_remote_seat_source *s = ctx;
  s->owner.unified.publish_components(s->owner.unified.ctx, operation, operation_ctx);
}
static void u_send_command(void *ctx, const char *name, const char *const *args, size_t count) {
  qa_app_remote_seat_source *s = ctx;
  if (s->network.family == QA_APP_REMOTE_FAMILY_UNIFIED && s->network.unified != NULL)
    qa_app_unified_client_network_player_command(s->network.unified, name, args, count);
}
static void u_send_component_command(void *ctx, qa_presentation_owner owner, double generation,
    const char *const *args, size_t count) {
  qa_app_remote_seat_source *s = ctx;
  if (s->network.family != QA_APP_REMOTE_FAMILY_UNIFIED || s->network.unified == NULL)
    qa_raise("Error", 0, 0, "Component has no unified channel");
  qa_app_unified_client_network_component_command(s->network.unified, owner, generation, args, count);
}
static const char *u_userinfo(void *ctx) { return c_userinfo(ctx); }

/* ---- QW ---- */
static void qw_close_downloads(qa_app_remote_seat_source *s) {
  s->download_generation++;
  if (s->qw_downloads != NULL) {
    qa_app_qw_download_receiver_close(s->qw_downloads);
    qa_app_qw_download_receiver_destroy(s->qw_downloads);
  }
  s->qw_downloads = NULL;
  if (s->qw_binding != NULL) {
    free(s->qw_binding->game_root);
    free(s->qw_binding->skin_root);
    free(s->qw_binding);
  }
  s->qw_binding = NULL;
}
static qa_app_qw_download_receiver *qw_require(qa_app_remote_seat_source *s) {
  if (s->qw_downloads == NULL) qa_raise("Error", 0, 0, "QW download before serverdata");
  return s->qw_downloads;
}
static qa_app_qw_download_request_result qw_request(void *ctx, const char *path, qa_app_qw_download_category category) {
  return qa_app_qw_download_receiver_request(qw_require(ctx), path, category);
}
static qa_app_qw_download_receive_result qw_receive(void *ctx, const void *result) {
  return qa_app_qw_download_receiver_receive(qw_require(ctx), *(const qa_app_qw_download_result *)result);
}
static void qw_close(void *ctx) { qw_close_downloads(ctx); }
static bool qw_current(const qw_binding *b) { return !b->source->closed && b->generation == b->source->download_generation; }
static const char *qw_assert_current(void *ctx) {
  return qw_current(ctx) ? NULL : "QW directory selection was cancelled";
}
static void qw_assert_raise(void *ctx) {
  const char *error = qw_assert_current(ctx);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
typedef struct qw_exists_state { qw_binding *binding; const char *path; bool found; } qw_exists_state;
static void qw_exists_body(void *ctx) {
  qw_exists_state *state = ctx;
  const qa_resolved_resource_reference *resolved = NULL;
  state->found = qa_content_mounted_resolve(state->binding->source->owner.hooks.mounts(state->binding->source->owner.hooks.ctx),
      state->path, &resolved);
}
static bool qw_exists(void *ctx, const char *path, qa_app_qw_download_category category) {
  qw_binding *b = ctx;
  if (!qw_current(b)) return false;
  const char *root = category == QA_APP_QW_DL_SKIN ? b->skin_root : b->game_root;
  size_t size = strlen(root) + strlen(path) + 2;
  char *joined = xcalloc(size, 1);
  snprintf(joined, size, "%s/%s", root, path);
  struct stat info;
  bool exists = stat(joined, &info) == 0;
  free(joined);
  if (exists) return true;
  /* TS: try { ... mounts.resolve(path) } catch (error) { if (!current()) return false; throw error; } */
  qw_exists_state state = {b, path, false};
  qa_caught_error caught;
  if (!qa_try(qw_exists_body, &state, &caught)) {
    if (!qw_current(b)) return false;
    qa_rethrow(&caught);
  }
  return state.found;
}
static void qw_send_command(void *ctx, const char *text) {
  qw_binding *b = ctx;
  qw_assert_raise(b);
  qa_app_remote_seat_network_command(&b->source->network, text);
}
static void qw_print(void *ctx, const char *text) {
  qw_binding *b = ctx;
  b->source->owner.hooks.print(b->source->owner.hooks.ctx, text);
}
static double qw_noskins(void *ctx) {
  qw_binding *b = ctx;
  return qa_cvar_registry_variable_value(cvars(b->source), "noskins");
}
static bool qw_demo_recording(void *ctx) {
  qw_binding *b = ctx;
  return b->source->owner.hooks.recording(b->source->owner.hooks.ctx);
}
static bool qw_demo_playback(void *ctx) { (void)ctx; return false; }
/* TS private prepareQwDownloads(data). */
static void qw_prepare_server_data(void *ctx, const qa_quakeworld_message *data) {
  qa_app_remote_seat_source *s = ctx;
  qw_close_downloads(s);
  qw_binding *binding = xcalloc(1, sizeof *binding);
  binding->source = s;
  binding->generation = s->download_generation;
  qa_remote_content_selection selection;
  if (!qa_remote_content_selection_make("q1-quakeworld", data->game_directory ? data->game_directory : "", &selection))
    qa_range_error("Out of memory");
  qa_app_remote_content_mounts *prepared = NULL;
  /* the selection and binding are released whatever the hook raises */
  qa_cleanup selection_guard, binding_guard;
  qa_cleanup_push(&selection_guard, selection_release, &selection);
  qa_cleanup_push(&binding_guard, free, binding);
  prepared = s->owner.hooks.select_content(s->owner.hooks.ctx, &selection, qw_assert_raise, binding);
  qw_assert_raise(binding);
  qa_cleanup_pop(&binding_guard);
  qa_cleanup_pop(&selection_guard);
  qa_remote_content_selection_free(&selection);
  binding->game_root = xdup(prepared->write_root);
  binding->skin_root = xdup(prepared->base_write_root);
  qa_app_qw_download_options options;
  memset(&options, 0, sizeof options);
  options.ctx = binding;
  options.game_root = binding->game_root;
  options.skin_root = binding->skin_root;
  options.exists = qw_exists;
  options.send_command = qw_send_command;
  options.print = qw_print;
  options.noskins = qw_noskins;
  options.demo_recording = qw_demo_recording;
  options.demo_playback = qw_demo_playback;
  s->qw_binding = binding;
  s->qw_downloads = qa_app_qw_download_receiver_create(options);
  if (s->qw_downloads == NULL) qa_range_error("Out of memory");
}
static double qw_hightrack(void *ctx) { return qa_cvar_registry_variable_value(cvars(ctx), "cl_hightrack"); }
static double qw_chasecam(void *ctx) { return qa_cvar_registry_variable_value(cvars(ctx), "cl_chasecam"); }
static bool qw_skin_read(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length) {
  qa_app_remote_seat_source *s = ctx;
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(s->owner.hooks.mounts(s->owner.hooks.ctx), path, NULL, NULL, &opened)) return false;
  *out_bytes = opened.bytes;
  *out_length = opened.byte_length;
  return true;
}
static double qw_skin_noskins(void *ctx) { return qa_cvar_registry_variable_value(cvars(ctx), "noskins"); }
static const char *qw_skin_baseskin(void *ctx) {
  qa_app_remote_seat_source *s = ctx;
  return keep_userinfo(s, qa_cvar_registry_variable_string(cvars(s), "baseskin"));
}
static const char *qw_skin_allskins(void *ctx) {
  qa_app_remote_seat_source *s = ctx;
  return s->all_skins;
}
static void *qw_load_content(void *ctx, qa_app_q1_remote_world_info world, qa_app_q1_remote_assert_fn assert_current,
    void *assert_ctx) {
  (void)assert_current; (void)assert_ctx; /* TS loadContent: world => hooks.loadQ1World(world, true) */
  qa_app_remote_seat_source *s = ctx;
  return remember(s, s->owner.hooks.load_q1_world(s->owner.hooks.ctx, &world, true));
}
static double qw_map_checksum(void *ctx, qa_app_q1_remote_world_info world, const char *game_directory) {
  (void)game_directory; /* TS mapChecksum: async world => quakeWorldMapChecksum2(await hooks.content().mounts.read(world.map)) */
  qa_app_remote_seat_source *s = ctx;
  qa_app_loaded_content *content = s->owner.hooks.content(s->owner.hooks.ctx);
  size_t length = 0;
  uint8_t *bytes = qa_content_mounted_read_path((qa_content_mounted *)qa_app_loaded_content_mounts(content), world.map,
      &length);
  int32_t checksum = qa_quake_world_map_checksum2(bytes, length);
  free(bytes);
  return checksum;
}
static const char *qw_userinfo(void *ctx) {
  qa_app_remote_seat_source *s = ctx;
  return keep_userinfo(s, qa_cvar_registry_propagated_info(cvars(s), QA_CVAR_INFO_CLIENT_USERINFO));
}

/* ---- Q1 ---- */
static void *q1_load_content(void *ctx, qa_app_q1_remote_world_info world, qa_app_q1_remote_assert_fn assert_current,
    void *assert_ctx) {
  (void)assert_current; (void)assert_ctx; /* TS loadContent: world => hooks.loadQ1World(world, false) */
  qa_app_remote_seat_source *s = ctx;
  return remember(s, s->owner.hooks.load_q1_world(s->owner.hooks.ctx, &world, false));
}

/* ---- Q3 ---- */
static double q3_timescale(void *ctx) { return qa_cvar_registry_variable_value(cvars(ctx), "timescale"); }
static double q3_time_nudge(void *ctx) {
  qa_cvar_read read;
  if (!qa_cvar_registry_get(cvars(ctx), "cl_timeNudge", &read)) return 0;
  double value = read.integer_value;
  qa_cvar_read_free(&read);
  return value;
}
static void q3_initialize(void *ctx, qa_q3_client_connection *connection) {
  qa_app_remote_seat_source *s = ctx;
  s->owner.hooks.initialize_q3(s->owner.hooks.ctx, connection);
}
static void q3_shutdown(void *ctx) {
  qa_app_remote_seat_source *s = ctx;
  s->owner.hooks.shutdown_q3(s->owner.hooks.ctx);
}
static char *game_state_text(qa_q3_client_connection *connection, int64_t index) {
  char *text = NULL;
  if (!qa_q3_client_game_state_storage_get(qa_q3_client_connection_game_state(connection), index, &text) || text == NULL)
    return xdup("");
  return text;
}
static void q3_close_downloads(qa_app_remote_seat_source *s) {
  if (s->q3_downloads != NULL) {
    qa_app_q3_client_downloads_close(s->q3_downloads);
    qa_app_q3_client_downloads_destroy(s->q3_downloads);
  }
  s->q3_downloads = NULL;
  if (s->q3_binding != NULL) {
    free((char *)s->q3_binding->mounts.selection_directory);
    free((char *)s->q3_binding->mounts.write_root);
    free((char *)s->q3_binding->mounts.base_write_root);
    free(s->q3_binding);
  }
  s->q3_binding = NULL;
}
static qa_app_q3_client_downloads *q3_require(qa_app_remote_seat_source *s) {
  if (s->q3_downloads == NULL) qa_raise("Error", 0, 0, "Q3 download has no content owner");
  return s->q3_downloads;
}
typedef struct q3_current { qa_app_remote_seat_source *source; qa_q3_client_connection *connection; int64_t generation; } q3_current;
static void q3_assert_current(void *ctx) {
  q3_current *current = ctx;
  assert_open(current->source);
  const qa_app_remote_seat_network *network = &current->source->network;
  if (network->family != QA_APP_REMOTE_FAMILY_Q3 || network->q3 == NULL
      || qa_app_q3_client_network_native(network->q3) != current->connection
      || current->generation != qa_q3_client_connection_generation(current->connection))
    qa_raise("Error", 0, 0, "Q3 package download belongs to a retired connection");
}
static void q3_binding_assert(void *ctx) {
  q3_binding *b = ctx;
  q3_current current = {b->source, b->connection, b->generation};
  q3_assert_current(&current);
  if (b->source->q3_downloads != b->downloads) qa_raise("Error", 0, 0, "Q3 package download was replaced");
}
static void q3_binding_reliable(void *ctx, const char *text) {
  q3_binding *b = ctx;
  qa_q3_reliable_command added;
  if (!qa_q3_client_reliable_commands_add(qa_q3_client_connection_reliable(b->connection), text, &added))
    qa_range_error("Out of memory");
}
static void q3_binding_send_packet(void *ctx) {
  q3_binding *b = ctx;
  q3_current current = {b->source, b->connection, b->generation};
  q3_assert_current(&current);
  if (b->source->network.family == QA_APP_REMOTE_FAMILY_Q3) qa_app_q3_client_network_send_packet(b->source->network.q3);
}
static void q3_binding_progress(void *ctx, const char *name, double count, double size) {
  q3_binding *b = ctx;
  if (count == 0 || count == size) {
    char count_text[40], size_text[40];
    qa_net_js_number(count, count_text, sizeof count_text);
    qa_net_js_number(size, size_text, sizeof size_text);
    size_t length = strlen(name) + strlen(count_text) + strlen(size_text) + 32;
    char *line = xcalloc(length, 1);
    snprintf(line, length, "Downloading %s: %s/%s bytes\n", name, count_text, size_text);
    b->source->owner.hooks.print(b->source->owner.hooks.ctx, line);
    free(line);
  }
}
static void q3_refresh_assert(void *ctx) {
  q3_binding *b = ctx;
  q3_current current = {b->source, b->connection, b->generation};
  q3_assert_current(&current);
  if (b->source->q3_downloads != b->downloads) qa_raise("Error", 0, 0, "Q3 package refresh was cancelled");
}
static void q3_binding_reload(void *ctx) {
  q3_binding *b = ctx;
  b->source->owner.hooks.refresh_content(b->source->owner.hooks.ctx, q3_refresh_assert, b);
}
typedef struct q3_exists_ctx { const char *root; const qa_app_q3_download_mounts *seed; bool mapped; } q3_exists_ctx;
static bool q3_exists(void *ctx, const char *path) {
  q3_exists_ctx *e = ctx;
  char *mapped = e->mapped ? qa_app_q3_download_path(path, e->seed) : xdup(path);
  if (mapped == NULL) qa_range_error("Out of memory");
  size_t size = strlen(e->root) + strlen(mapped) + 2;
  char *joined = xcalloc(size, 1);
  snprintf(joined, size, "%s/%s", e->root, mapped);
  struct stat info;
  bool exists = stat(joined, &info) == 0;
  free(joined);
  free(mapped);
  return exists;
}
static void make_directories(const char *root) {
  char *path = xdup(root);
  for (char *at = path + 1; *at != '\0'; at++) {
    if (*at != '/') continue;
    *at = '\0';
    if (mkdir(path, 0777) != 0 && errno != EEXIST) {
      int code = errno;
      free(path);
      qa_raise("Error", 0, 0, "%s", strerror(code));
    }
    *at = '/';
  }
  if (mkdir(path, 0777) != 0 && errno != EEXIST) {
    int code = errno;
    free(path);
    qa_raise("Error", 0, 0, "%s", strerror(code));
  }
  free(path);
}
static char *dirname_of(const char *path) {
  char *copy = xdup(path);
  size_t length = strlen(copy);
  while (length > 1 && copy[length - 1] == '/') copy[--length] = '\0';
  char *slash = strrchr(copy, '/');
  if (slash == NULL) { free(copy); return xdup("."); }
  if (slash == copy) { copy[1] = '\0'; return copy; }
  *slash = '\0';
  return copy;
}
typedef struct q3_prepare_state {
  qa_q3_server_pak_set *referenced; qa_q3_server_pak *snapshot; char *info, *root, *missing;
  qa_app_q3_packages *packages; double *loaded; int64_t *loaded64; qa_app_q3_package_mount *mounts;
  qa_remote_content_selection selection; bool has_selection;
} q3_prepare_state;
static void q3_prepare_release(void *ctx) {
  q3_prepare_state *p = ctx;
  if (p->referenced) qa_q3_server_pak_set_destroy(p->referenced);
  free(p->snapshot); free(p->info); free(p->root); free(p->missing);
  if (p->packages) qa_app_q3_packages_destroy(p->packages);
  free(p->loaded); free(p->loaded64); free(p->mounts);
  if (p->has_selection) qa_remote_content_selection_free(&p->selection);
  memset(p, 0, sizeof *p);
}
/* TS private prepareQ3Downloads(connection). */
static bool q3_prepare(void *ctx, qa_q3_client_connection *connection) {
  qa_app_remote_seat_source *s = ctx;
  q3_current current = {s, connection, qa_q3_client_connection_generation(connection)};
  q3_assert_current(&current);
  q3_close_downloads(s);
  const qa_app_remote_seat_source_hooks *hooks = &s->owner.hooks;
  q3_prepare_state p;
  memset(&p, 0, sizeof p);
  qa_cleanup guard;
  qa_cleanup_push(&guard, q3_prepare_release, &p);
  p.info = game_state_text(connection, 1);
  char *game = qa_q3_info_value(p.info, "fs_game");
  if (game == NULL) qa_range_error("Out of memory");
  bool made = qa_remote_content_selection_make("q3-baseq3", game[0] != '\0' ? game : "baseq3", &p.selection);
  free(game);
  if (!made) qa_range_error("Out of memory");
  p.has_selection = true;
  qa_app_remote_content_mounts *seed = hooks->select_content(hooks->ctx, &p.selection, q3_assert_current, &current);
  p.root = dirname_of(seed->write_root);
  size_t mount_count = 0;
  p.mounts = qa_app_q3_package_mounts_of(seed->catalog, seed->mounts, &mount_count);
  p.packages = qa_app_q3_packages_open(p.mounts, mount_count, (double)qa_q3_client_connection_checksum_feed(connection));
  if (p.packages == NULL) qa_range_error("Out of memory");
  q3_assert_current(&current);
  if (hooks->current_content(hooks->ctx) != seed) qa_raise("Error", 0, 0, "Q3 package content selection was replaced");
  if (!qa_q3_server_pak_set_init(&p.referenced)) qa_range_error("Out of memory");
  char *sums = qa_q3_info_value(p.info, "sv_referencedPaks"), *names = qa_q3_info_value(p.info, "sv_referencedPakNames");
  if (sums == NULL || names == NULL) { free(sums); free(names); qa_range_error("Out of memory"); }
  (void)qa_q3_server_pak_set_set_checksums(p.referenced, sums);
  qa_q3_server_pak_set_set_names(p.referenced, names, -1);
  free(sums); free(names);
  q3_binding *binding = xcalloc(1, sizeof *binding);
  binding->source = s;
  binding->connection = connection;
  binding->generation = current.generation;
  binding->mounts.selection_base = seed->selection.base;
  binding->mounts.selection_directory = xdup(seed->selection.directory);
  binding->mounts.write_root = xdup(seed->write_root);
  binding->mounts.base_write_root = xdup(seed->base_write_root);
  qa_app_q3_download_bindings bindings;
  memset(&bindings, 0, sizeof bindings);
  bindings.ctx = binding;
  bindings.permission = s->owner.download_permission;
  bindings.assert_current = q3_binding_assert;
  bindings.reliable = q3_binding_reliable;
  bindings.send_packet = q3_binding_send_packet;
  bindings.progress = q3_binding_progress;
  bindings.reload_packages = q3_binding_reload;
  qa_app_q3_client_downloads *downloads = qa_app_q3_client_downloads_create(p.root, bindings, &binding->mounts);
  if (downloads == NULL) qa_range_error("Out of memory");
  binding->downloads = downloads;
  s->q3_downloads = downloads;
  s->q3_binding = binding;
  size_t pack_count = 0;
  const qa_q3_mounted_pak *packs = qa_app_q3_packages_packs(p.packages, &pack_count);
  p.loaded = xcalloc(pack_count, sizeof *p.loaded);
  p.loaded64 = xcalloc(pack_count, sizeof *p.loaded64);
  for (size_t i = 0; i < pack_count; i++) {
    p.loaded[i] = (double)packs[i].pack.checksum;
    p.loaded64[i] = packs[i].pack.checksum;
  }
  size_t referenced_count = 0;
  if (!qa_q3_server_pak_set_snapshot(p.referenced, &p.snapshot, &referenced_count)) qa_range_error("Out of memory");
  q3_exists_ctx plain = {p.root, &binding->mounts, false}, mapped = {p.root, &binding->mounts, true};
  bool permitted = s->owner.download_permission != NULL
      && qa_app_client_download_permitted(s->owner.download_permission, QA_APP_DL_NATIVE, QA_APP_DL_PACKAGE);
  if (!permitted) {
    p.missing = qa_q3_compare_packages(p.snapshot, referenced_count, p.loaded64, pack_count, q3_exists, &mapped, false,
        1024);
    if (p.missing == NULL) qa_range_error("Out of memory");
    if (p.missing[0] != '\0') {
      size_t length = strlen(p.missing) + 160;
      char *line = xcalloc(length, 1);
      snprintf(line, length,
          "Missing server packages: %s\nDownloads are disabled. Enable cl_allowDownload to download server packages.\n",
          p.missing);
      hooks->print(hooks->ctx, line);
      free(line);
    }
    qa_cleanup_pop(&guard);
    q3_prepare_release(&p);
    return false;
  }
  bool pending = qa_app_q3_client_downloads_begin(downloads, p.snapshot, referenced_count, p.loaded, pack_count,
      q3_exists, &plain);
  if (pending) {
    make_directories(p.root);
    q3_assert_current(&current);
    if (s->q3_downloads != downloads) qa_raise("Error", 0, 0, "Q3 download setup was cancelled");
  }
  qa_cleanup_pop(&guard);
  q3_prepare_release(&p);
  return pending;
}
static double q3_publish_size(void *ctx, double size) {
  return qa_app_q3_client_downloads_publish_size(q3_require(ctx), size);
}
static void q3_receive(void *ctx, const qa_q3_download_block *block) {
  qa_app_q3_client_downloads_receive(q3_require(ctx), block);
}
static void q3_downloads_close(void *ctx) { q3_close_downloads(ctx); }

/* Q3ClientContent.open host over the seed content (catalog + mounts) and the options. */
typedef struct q3_content_host {
  qa_app_remote_seat_source *source;
  qa_app_options options; /* { ...hooks.options(), map: serverMapPath(world.map) } (owned) */
  qa_app_q3_package_mount *catalog_mounts, *loaded_mounts;
} q3_content_host;
static void *q3c_load(void *ctx, const qa_pure_mount_policy *policy) {
  q3_content_host *host = ctx;
  qa_app_options options;
  qa_app_startup_options_copy(&options, &host->options);
  qa_app_loaded_content *content = qa_app_load_application_content_native((qa_app_content_host){0}, options, NULL,
      false, policy, NULL, NULL);
  if (content == NULL) qa_range_error("Out of memory");
  return content;
}
static const qa_app_q3_package_mount *q3c_loaded_mounts(void *ctx, void *loaded, size_t *out_count) {
  q3_content_host *host = ctx;
  free(host->loaded_mounts);
  host->loaded_mounts = qa_app_q3_package_mounts_of(qa_app_loaded_content_catalog(loaded),
      qa_app_loaded_content_mounts(loaded), out_count);
  return host->loaded_mounts;
}
static void q3c_collect(void *ctx, void *loaded,
    void (*visit)(void *visit_ctx, const qa_resolved_resource_reference *reference, bool is_q3), void *visit_ctx) {
  (void)ctx;
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(loaded);
  /* the reader fills up to capacity without reporting a total: grow until it fits */
  size_t capacity = 16, count = 0;
  const void **opened = NULL;
  for (;;) {
    free(opened);
    opened = xcalloc(capacity, sizeof *opened);
    count = qa_app_loaded_content_opened_mounts(loaded, opened, capacity);
    if (count < capacity) break;
    capacity *= 2;
  }
  for (size_t i = 0; i < count; i++) {
    size_t resources = 0;
    const qa_resolved_resource_reference *rows = qa_content_mounted_opened_resources(opened[i], &resources);
    for (size_t j = 0; j < resources; j++) {
      const qa_resolved_resource_reference *reference = &rows[j];
      qa_content_id content = reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE
          ? reference->provenance.archive_mount->identity.content : reference->provenance.loose_mount->identity.content;
      visit(visit_ctx, reference, qa_content_catalog_product(catalog, content)->expectation.family == QA_GAME_FAMILY_Q3);
    }
  }
  free(opened);
}
static void q3c_close(void *ctx, void *loaded) {
  (void)ctx;
  qa_app_loaded_content_close(loaded);
  qa_app_loaded_content_destroy(loaded);
}
static void q3_content_host_free(q3_content_host *host) {
  qa_app_options_destroy(&host->options);
  free(host->catalog_mounts);
  free(host->loaded_mounts);
  free(host);
}
static void q3_content_host_release(void *host) { q3_content_host_free(host); }
typedef struct q3_world_state {
  qa_app_remote_seat_source *source; qa_q3_client_connection *connection; int64_t generation;
  qa_app_remote_content_mounts *seed;
  qa_app_q1_remote_world_info world;
  q3_content_host *host; qa_app_q3_client_content *prepared; char *info;
  qa_app_loaded_content *content;
} q3_world_state;
static void q3_world_assert(q3_world_state *state) {
  assert_open(state->source);
  const qa_app_remote_seat_source_hooks *hooks = &state->source->owner.hooks;
  if (state->generation != qa_q3_client_connection_generation(state->connection)
      || hooks->current_content(hooks->ctx) != state->seed)
    qa_raise("Error", 0, 0, "Remote Q3 content loading was cancelled");
}
static void q3_world_body(void *ctx) {
  q3_world_state *state = ctx;
  qa_app_remote_seat_source *s = state->source;
  q3_world_assert(state);
  state->content = s->owner.hooks.load_q3_world(s->owner.hooks.ctx, &state->world, state->connection,
      qa_app_q3_client_content_loaded(state->prepared));
  q3_world_assert(state);
}
/* TS private loadQ3World(world, connection). */
static void *q3_load_content(void *ctx, const char *map, const char *const *models, size_t model_count,
    const char *const *sounds, size_t sound_count, qa_q3_client_connection *connection) {
  qa_app_remote_seat_source *s = ctx;
  const qa_app_remote_seat_source_hooks *hooks = &s->owner.hooks;
  q3_world_state state;
  memset(&state, 0, sizeof state);
  state.source = s;
  state.connection = connection;
  state.generation = qa_q3_client_connection_generation(connection);
  state.seed = hooks->current_content(hooks->ctx);
  state.world = (qa_app_q1_remote_world_info){map, models, model_count, sounds, sound_count};
  if (state.seed == NULL) qa_raise("Error", 0, 0, "Q3 world loading requires its selected content owner");
  q3_world_assert(&state);
  q3_content_host *host = xcalloc(1, sizeof *host);
  host->source = s;
  qa_app_startup_options_copy(&host->options, hooks->options(hooks->ctx));
  char *map_path = qa_app_server_map_path(map);
  free(host->options.map);
  host->options.map = map_path;
  const qa_catalog_discovered_product *product = qa_content_catalog_require(state.seed->catalog, host->options.product);
  size_t catalog_count = 0;
  host->catalog_mounts = qa_app_q3_package_mounts_of(state.seed->catalog, state.seed->mounts, &catalog_count);
  qa_app_q3_content_host content_host;
  memset(&content_host, 0, sizeof content_host);
  content_host.ctx = host;
  content_host.product_family = qa_game_family_name(product->expectation.family);
  content_host.product_content_directory = product->expectation.content_directory;
  content_host.catalog_mounts = host->catalog_mounts;
  content_host.catalog_mount_count = catalog_count;
  content_host.load_content = q3c_load;
  content_host.loaded_mounts = q3c_loaded_mounts;
  content_host.collect_opened = q3c_collect;
  content_host.close_content = q3c_close;
  state.info = game_state_text(connection, 1);
  qa_cleanup host_guard;
  qa_cleanup_push(&host_guard, q3_content_host_release, host);
  state.prepared = qa_app_q3_client_content_open(content_host, state.info,
      (double)qa_q3_client_connection_checksum_feed(connection));
  free(state.info);
  state.info = NULL;
  if (state.prepared == NULL) qa_range_error("Out of memory");
  qa_cleanup_pop(&host_guard);
  qa_caught_error caught;
  if (!qa_try(q3_world_body, &state, &caught)) {
    qa_app_q3_client_content_close(state.prepared);
    qa_app_q3_client_content_destroy(state.prepared);
    q3_content_host_free(host);
    qa_rethrow(&caught);
  }
  qa_app_q3_client_content *previous = s->q3_content;
  s->q3_content = state.prepared;
  qa_app_loaded_content_keep(state.content, host, q3_content_host_release);
  if (previous != NULL) {
    qa_app_q3_client_content_close(previous);
    qa_app_q3_client_content_destroy(previous);
  }
  return remember(s, state.content);
}

/* ---- Q2 ---- */
static void q2_cinematic_start(void *ctx, const char *name, qa_app_q2_cinematic_ended_fn ended, void *ended_ctx) {
  qa_app_remote_seat_source *s = ctx;
  s->owner.hooks.cinematic_start(s->owner.hooks.ctx, name, ended, ended_ctx);
}
static void q2_cinematic_stop(void *ctx) {
  qa_app_remote_seat_source *s = ctx;
  s->owner.hooks.cinematic_stop(s->owner.hooks.ctx);
}
static const qa_app_q2_remote_mounts *q2_prepare_server_data(void *ctx, const void *data_value,
    qa_app_q2_remote_assert_fn assert_current, void *assert_ctx) {
  qa_app_remote_seat_source *s = ctx;
  const qa_q2_server_data_result *data = data_value;
  const char *kind = qa_app_q2_remote_protocol(s->remote.q2).kind;
  bool rerelease = strcmp(kind, "q2-rerelease") == 0 || strcmp(kind, "q2-kex") == 0 || strcmp(kind, "q2-kex-demo") == 0;
  qa_remote_content_selection selection;
  if (!qa_remote_content_selection_make(rerelease ? "q2-rerelease-baseq2" : "q2-classic-baseq2", data->gamedir, &selection))
    qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, selection_release, &selection);
  qa_app_remote_content_mounts *content = s->owner.hooks.select_content(s->owner.hooks.ctx, &selection, assert_current,
      assert_ctx);
  qa_cleanup_pop(&guard);
  qa_remote_content_selection_free(&selection);
  return qa_app_remote_access_q2_mounts(&s->access, content);
}
static void *q2_load_content(void *ctx, const qa_app_q2_game_state *state) {
  qa_app_remote_seat_source *s = ctx;
  return remember(s, s->owner.hooks.load_q2_world(s->owner.hooks.ctx, state));
}
static const qa_app_q2_remote_mounts *q2_refresh_downloads(void *ctx, qa_app_q2_remote_assert_fn assert_current,
    void *assert_ctx) {
  qa_app_remote_seat_source *s = ctx;
  qa_app_remote_content_mounts *content = s->owner.hooks.refresh_content(s->owner.hooks.ctx, assert_current, assert_ctx);
  return qa_app_remote_access_q2_mounts(&s->access, content);
}

static void fill_common(qa_app_remote_seat_source *s, void **ctx, qa_app_remote_world_host *world_host,
    qa_session_id *session, qa_client_id *client) {
  *ctx = s;
  *world_host = qa_app_remote_access_world_host();
  *session = s->access.session;
  *client = qa_session_client_id(qa_session_seat_client(s->owner.seat));
}
static void construct(qa_app_remote_seat_source *s) {
  const qa_app_remote_seat_source_options *owner = &s->owner;
  qa_seat_id seat = qa_session_seat_id(owner->seat);
  if (owner->family == QA_APP_REMOTE_FAMILY_UNIFIED) {
    qa_app_unified_remote_options o;
    memset(&o, 0, sizeof o);
    fill_common(s, &o.ctx, &o.world_host, &o.session, &o.client);
    o.seat = seat;
    o.next_generation = qa_app_remote_access_next_generation;
    o.identity_actor = qa_app_remote_access_identity_actor;
    o.identity_client = qa_app_remote_access_identity_client;
    o.owned_actor = qa_app_remote_access_owned_actor;
    o.load_content = u_load_content;
    o.prepared_mods = qa_app_remote_access_prepared_mods;
    o.mod_has_presentation = qa_app_remote_access_mod_has_presentation;
    o.recipe = qa_app_remote_access_content_recipe;
    o.map_entities_provider = qa_app_remote_access_map_entities_provider;
    o.resolve_resource = qa_app_remote_access_resolve_unified_resource;
    o.model = u_model;
    o.world_resource = qa_app_remote_access_world_resource;
    o.world_geometry = qa_app_remote_access_content_world;
    o.publish = c_publish;
    o.publish_components = owner->unified.publish_components != NULL ? u_publish_components : NULL;
    o.send_command = u_send_command;
    o.has_mod_files = true; /* TS modFiles: new ModUserFiles(options.userContentRoot ?? defaultUserContentRoot()) */
    o.send_component_command = u_send_component_command;
    o.scene_link = qa_app_remote_access_scene_link;
    o.scene_unlink = qa_app_remote_access_scene_unlink;
    o.scene_actor_is_model = qa_app_remote_access_scene_actor_is_model;
    o.disconnected = c_disconnected;
    o.print = c_print;
    s->remote.family = QA_APP_REMOTE_FAMILY_UNIFIED;
    s->remote.unified = qa_app_unified_remote_create(o);
    if (s->remote.unified == NULL) qa_range_error("Out of memory");
    qa_app_unified_client_options n;
    memset(&n, 0, sizeof n);
    n.transport = &s->datagram;
    n.remote = owner->address;
    n.host = s->remote.unified;
    n.userinfo_ctx = s;
    n.userinfo = u_userinfo;
    s->network.family = QA_APP_REMOTE_FAMILY_UNIFIED;
    s->network.unified = qa_app_unified_client_network_create(n);
    if (s->network.unified == NULL) qa_range_error("Out of memory");
  } else if (owner->family == QA_APP_REMOTE_FAMILY_QW) {
    if (owner->address.kind == QA_NET_IPX) qa_raise("Error", 0, 0, "QuakeWorld requires UDP");
    qa_app_qw_remote_options o;
    memset(&o, 0, sizeof o);
    fill_common(s, &o.shared.ctx, &o.shared.world_host, &o.shared.session, &o.shared.client);
    o.shared.presentation_time = c_presentation_time;
    o.shared.next_generation = qa_app_remote_access_next_generation;
    o.shared.identity_actor = qa_app_remote_access_identity_actor;
    o.shared.load_content = qw_load_content;
    o.shared.content_recipe = qa_app_remote_access_content_recipe;
    o.shared.content_world = qa_app_remote_access_content_world;
    o.shared.send_command = c_send_command;
    o.shared.print = c_print;
    o.shared.publish = c_publish;
    o.shared.disconnected = c_disconnected;
    o.seat = seat;
    o.has_camera_options = true;
    o.camera_options = (qa_app_qw_camera_options){s, qw_hightrack, qw_chasecam};
    o.skin_options = (qa_app_qw_skin_options){s, qw_skin_read, qw_skin_noskins, qw_skin_baseskin, qw_skin_allskins};
    o.has_downloads = true;
    o.downloads = (qa_app_qw_downloads){s, qw_request, qw_receive, qw_close};
    o.camera_trace = qa_app_remote_access_qw_camera_trace;
    o.prepare_server_data = qw_prepare_server_data;
    o.map_checksum = qw_map_checksum;
    o.resolve_sound = qa_app_remote_access_resolve_sound;
    o.owned_actor = qa_app_remote_access_owned_actor;
    o.scene_link = qa_app_remote_access_scene_link;
    o.scene_unlink = qa_app_remote_access_scene_unlink;
    o.scene_model_bounds = qa_app_remote_access_scene_model_bounds;
    o.now_milliseconds = qa_app_remote_access_now_milliseconds;
    s->remote.family = QA_APP_REMOTE_FAMILY_QW;
    s->remote.qw = qa_app_qw_remote_create(o);
    if (s->remote.qw == NULL) qa_range_error("Out of memory");
    qa_app_qw_client_network_options n;
    memset(&n, 0, sizeof n);
    n.transport = &qa_app_application_transport_udp_socket(owner->transport)->transport;
    n.remote = owner->address;
    n.host = qa_app_qw_remote_as_client_host(s->remote.qw);
    n.qport = owner->qport;
    n.userinfo_ctx = s;
    n.userinfo = qw_userinfo;
    s->network.family = QA_APP_REMOTE_FAMILY_QW;
    s->network.qw = qa_app_qw_client_network_create(n);
    if (s->network.qw == NULL) qa_range_error("Out of memory");
  } else if (owner->family == QA_APP_REMOTE_FAMILY_Q1) {
    qa_app_q1_remote_options o;
    memset(&o, 0, sizeof o);
    fill_common(s, &o.ctx, &o.world_host, &o.session, &o.client);
    o.presentation_time = c_presentation_time;
    o.next_generation = qa_app_remote_access_next_generation;
    o.identity_actor = qa_app_remote_access_identity_actor;
    o.load_content = q1_load_content;
    o.content_recipe = qa_app_remote_access_content_recipe;
    o.content_world = qa_app_remote_access_content_world;
    o.send_command = c_send_command;
    o.print = c_print;
    o.publish = c_publish;
    o.disconnected = c_disconnected;
    s->remote.family = QA_APP_REMOTE_FAMILY_Q1;
    s->remote.q1 = qa_app_q1_remote_create(o);
    if (s->remote.q1 == NULL) qa_range_error("Out of memory");
    char *name = qa_cvar_registry_variable_string(cvars(s), "name");
    if (name == NULL) qa_range_error("Out of memory");
    if (name[0] == '\0') { free(name); name = xdup("Player"); }
    s->seat_name = name;
    qa_cvar_read color;
    double color_value = 0;
    if (qa_cvar_registry_get(cvars(s), "color", &color)) {
      color_value = color.integer_value;
      qa_cvar_read_free(&color);
    }
    qa_app_q1_client_network_options n;
    memset(&n, 0, sizeof n);
    n.transport = s->datagram;
    n.remote = owner->address;
    n.host = qa_app_q1_remote_as_client_host(s->remote.q1);
    n.seat = (qa_netquake_seat_identity){s->seat_name, color_value, "", false, 0};
    s->network.family = QA_APP_REMOTE_FAMILY_Q1;
    s->network.q1 = qa_app_q1_client_network_create(n);
    if (s->network.q1 == NULL) qa_range_error("Out of memory");
  } else if (owner->family == QA_APP_REMOTE_FAMILY_Q3) {
    if (owner->address.kind != QA_NET_IPV4 && owner->address.kind != QA_NET_IPX)
      qa_raise("Error", 0, 0, "Native Q3 remote requires IPv4");
    qa_app_q3_remote_options o;
    memset(&o, 0, sizeof o);
    fill_common(s, &o.ctx, &o.world_host, &o.session, &o.client);
    o.next_generation = qa_app_remote_access_next_generation;
    o.identity_actor = qa_app_remote_access_identity_actor;
    o.owned_actor = qa_app_remote_access_owned_actor;
    o.userinfo = c_userinfo;
    o.time_nudge = q3_time_nudge;
    o.timescale = q3_timescale;
    o.presentation_time = c_presentation_time;
    o.load_content = q3_load_content;
    o.content_recipe = qa_app_remote_access_content_recipe;
    o.content_world = qa_app_remote_access_content_world;
    o.has_downloads = true;
    o.downloads = (qa_app_q3_remote_downloads){s, q3_prepare, q3_publish_size, q3_receive, q3_downloads_close};
    o.shutdown = q3_shutdown;
    o.initialize = q3_initialize;
    o.send_command = c_send_command;
    o.disconnected = c_disconnected;
    o.publish = c_publish;
    o.print = c_print;
    s->remote.family = QA_APP_REMOTE_FAMILY_Q3;
    s->remote.q3 = qa_app_q3_remote_create(o);
    if (s->remote.q3 == NULL) qa_range_error("Out of memory");
    qa_collision_map_settings_init(&s->collision, cvars(s));
    qa_app_q3_remote_bind_collision_settings(s->remote.q3, &s->collision);
    qa_app_q3_client_network_options n;
    memset(&n, 0, sizeof n);
    n.transport = s->datagram;
    if (!qa_q3_address_from_net(&owner->address, &n.remote)) qa_raise("Error", 0, 0, "Native Q3 remote requires IPv4");
    n.host = qa_app_q3_remote_as_client_host(s->remote.q3);
    n.qport = owner->qport;
    n.cvars = cvars(s);
    n.has_authorization = true;
    n.authorization = owner->authorization;
    s->network.family = QA_APP_REMOTE_FAMILY_Q3;
    s->network.q3 = qa_app_q3_client_network_create(n);
    if (s->network.q3 == NULL) qa_range_error("Out of memory");
  } else {
    if (owner->download_permission == NULL) qa_raise("Error", 0, 0, "Q2 remote client has no download policy");
    qa_app_q2_remote_options o;
    memset(&o, 0, sizeof o);
    fill_common(s, &o.ctx, &o.world_host, &o.session, &o.client);
    o.seat = seat;
    o.next_generation = qa_app_remote_access_next_generation;
    o.identity_actor = qa_app_remote_access_identity_actor;
    o.owned_actor = qa_app_remote_access_owned_actor;
    o.protocol = owner->protocol;
    o.userinfo = c_userinfo;
    o.download_permission = owner->download_permission;
    o.has_cinematic = true;
    o.cinematic = (qa_app_q2_remote_cinematic){s, q2_cinematic_start, q2_cinematic_stop};
    o.publish = c_publish;
    o.disconnected = c_disconnected;
    o.presentation_time = c_presentation_time;
    o.print = c_print;
    o.send_command = c_send_command;
    o.load_content = q2_load_content;
    o.prepare_server_data = q2_prepare_server_data;
    o.refresh_downloads = q2_refresh_downloads;
    o.content_recipe = qa_app_remote_access_content_recipe;
    o.content_world = qa_app_remote_access_content_world;
    o.content_read = qa_app_remote_access_content_read;
    o.rebind_download_content = qa_app_remote_access_rebind_q2_mounts;
    s->remote.family = QA_APP_REMOTE_FAMILY_Q2;
    s->remote.q2 = qa_app_q2_remote_create(o);
    if (s->remote.q2 == NULL) qa_range_error("Out of memory");
    qa_app_q2_client_network_options n;
    memset(&n, 0, sizeof n);
    n.transport = &s->datagram;
    n.remote = &s->owner.address;
    n.host = qa_app_q2_remote_as_client_host(s->remote.q2);
    n.qport = owner->qport;
    s->network.family = QA_APP_REMOTE_FAMILY_Q2;
    s->network.q2 = qa_app_q2_client_network_create(n);
    if (s->network.q2 == NULL) qa_range_error("Out of memory");
  }
}
static void release(qa_app_remote_seat_source *s) {
  qw_close_downloads(s);
  q3_close_downloads(s);
  if (s->q3_content != NULL) {
    qa_app_q3_client_content_close(s->q3_content);
    qa_app_q3_client_content_destroy(s->q3_content);
  }
  s->q3_content = NULL;
  qa_app_remote_seat_network_destroy(&s->network);
  qa_app_remote_seat_presentation_destroy(&s->remote);
  qa_app_remote_access_release(&s->access);
  qa_net_address_destroy(&s->owner.address);
  free(s->all_skins); free(s->userinfo); free(s->seat_name); free(s->progress);
  free(s);
}
static void construct_body(void *ctx) { construct(ctx); }
qa_app_remote_seat_source *qa_app_remote_seat_source_create(const qa_app_remote_seat_source_options *owner) {
  qa_app_remote_seat_source *s = calloc(1, sizeof *s);
  if (s == NULL) return NULL;
  qa_app_remote_access_init(&s->access, owner->identity);
  s->owner = *owner;
  if (!qa_net_address_clone(&owner->address, &s->owner.address)) { free(s); return NULL; }
  s->datagram = qa_app_application_transport_as_transport(owner->transport);
  s->all_skins = xdup("");
  qa_caught_error caught;
  if (!qa_try(construct_body, s, &caught)) {
    release(s);
    qa_rethrow(&caught);
  }
  return s;
}
void qa_app_remote_seat_source_destroy(qa_app_remote_seat_source *source) {
  if (source == NULL) return;
  qa_app_remote_seat_source_close(source);
  release(source);
}
const qa_app_remote_seat_presentation *qa_app_remote_seat_source_remote(const qa_app_remote_seat_source *source) {
  return &source->remote;
}
const qa_app_remote_seat_network *qa_app_remote_seat_source_network(const qa_app_remote_seat_source *source) {
  return &source->network;
}
double qa_app_remote_seat_source_qport(const qa_app_remote_seat_source *source) { return source->owner.qport; }
qa_app_application_transport *qa_app_remote_seat_source_transport(const qa_app_remote_seat_source *source) {
  return source->owner.transport;
}
void qa_app_remote_seat_source_set_all_skins(qa_app_remote_seat_source *source, const char *value) {
  char *copy = xdup(value);
  free(source->all_skins);
  source->all_skins = copy;
}
char *qa_app_remote_seat_source_pure_command(qa_app_remote_seat_source *source, double server_id) {
  if (source->q3_content == NULL) return NULL;
  return qa_app_q3_client_content_referenced_pure_command(source->q3_content, server_id);
}
static void progress_reserve(qa_app_remote_seat_source *s, size_t count) {
  if (count <= s->progress_capacity) return;
  qa_app_client_download_progress *grown = realloc(s->progress, count * sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  s->progress = grown;
  s->progress_capacity = count;
}
const qa_app_client_download_progress *qa_app_remote_seat_source_download_progress(qa_app_remote_seat_source *source,
    size_t *out_count) {
  size_t count = 0;
  if (source->remote.family == QA_APP_REMOTE_FAMILY_Q2) {
    count = qa_app_q2_remote_download_progress(source->remote.q2, NULL, 0);
    progress_reserve(source, count);
    count = qa_app_q2_remote_download_progress(source->remote.q2, source->progress, count);
  } else if (source->qw_downloads != NULL) {
    progress_reserve(source, 1);
    count = qa_app_qw_download_receiver_progress(source->qw_downloads, source->progress);
  } else if (source->q3_downloads != NULL) {
    count = qa_app_q3_client_downloads_progress(source->q3_downloads, NULL, 0);
    progress_reserve(source, count);
    count = qa_app_q3_client_downloads_progress(source->q3_downloads, source->progress, count);
  }
  *out_count = count;
  return source->progress;
}
void qa_app_remote_seat_source_cancel_downloads(qa_app_remote_seat_source *source) {
  if (source->remote.family == QA_APP_REMOTE_FAMILY_Q2) qa_app_q2_remote_cancel_downloads(source->remote.q2);
  if (source->qw_downloads != NULL) qa_app_qw_download_receiver_cancel(source->qw_downloads);
  if (source->q3_downloads != NULL) qa_app_q3_client_downloads_cancel(source->q3_downloads);
}
void qa_app_remote_seat_source_retry_downloads(qa_app_remote_seat_source *source) {
  if (source->remote.family == QA_APP_REMOTE_FAMILY_Q2) qa_app_q2_remote_retry_downloads(source->remote.q2);
  if (source->qw_downloads != NULL) (void)qa_app_qw_download_receiver_retry(source->qw_downloads);
  if (source->q3_downloads != NULL) (void)qa_app_q3_client_downloads_retry(source->q3_downloads);
}
static void close_network(void *ctx) {
  qa_app_remote_seat_source *s = ctx;
  qa_app_remote_seat_network_close(&s->network);
}
void qa_app_remote_seat_source_close(qa_app_remote_seat_source *source) {
  if (source->closing) return;
  source->closed = true;
  qw_close_downloads(source);
  q3_close_downloads(source);
  source->closing = true;
  /* TS try { await network.close() } finally { await q3Content?.close(); q3Content = null } */
  qa_caught_error caught;
  bool closed = qa_try(close_network, source, &caught);
  if (source->q3_content != NULL) {
    qa_app_q3_client_content_close(source->q3_content);
    qa_app_q3_client_content_destroy(source->q3_content);
  }
  source->q3_content = NULL;
  if (!closed) qa_rethrow(&caught);
}
