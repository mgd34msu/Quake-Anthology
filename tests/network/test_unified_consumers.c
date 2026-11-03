/* Checks for unified-native-consumer.ts (UnifiedNativeConsumers) and the remote replica's
 * presentation queue: admission, frame readout, rejection texts, and fixed-seed malformed
 * updates and frames that must raise without crashing or leaking. GPL-2.0-or-later. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/network/unified_component_consumer.h"
#include "qa/app/bootstrap/network/unified_components.h"
#include "qa/compat/qvm/mod_presentation.h"
#include "qa/compat/qvm/mod_presentation_checkpoint.h"
#include "qa/world/session/mod_presentations.h"
#include "qa/app/bootstrap/network/unified_native_components.h"
#include "qa/app/bootstrap/network/unified_native_consumer.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/mods.h"
#include "qa/foundation/support.h"
#include "qa/persistence/mods.h"
#include "qa/persistence/value.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int ctx_sentinel;
static qa_identity_owner owner;
static qa_actor_id viewer_actor;
static int admitted, retired;
static qa_actor_id decode_actor(void *ctx, double slot, double generation) {
  CHECK(ctx == &ctx_sentinel);
  return qa_identity_owner_actor(&owner, (int64_t)slot, (int64_t)generation);
}
static qa_client_id decode_client(void *ctx, double slot, double generation) {
  CHECK(ctx == &ctx_sentinel);
  return qa_identity_owner_client(&owner, (int64_t)slot, (int64_t)generation);
}
static qa_seat_id decode_seat(void *ctx, double index) { CHECK(ctx == &ctx_sentinel); return qa_identity_owner_seat(&owner, (int64_t)index); }
static qa_resource_id decode_resource(void *ctx, qa_resource_id id) { CHECK(ctx == &ctx_sentinel); return id; }
static void assert_current(void *ctx) { CHECK(ctx == &ctx_sentinel); }
static bool viewer(void *ctx, qa_actor_id *out) { CHECK(ctx == &ctx_sentinel); *out = viewer_actor; return true; }
static void admit(void *ctx, qa_presentation_owner o, qa_content_id content) {
  CHECK(ctx == &ctx_sentinel);
  CHECK(strcmp(content, "q2:classic:baseq2:1") == 0);
  (void)o;
  admitted++;
}
static void retire(void *ctx, qa_presentation_owner o) { CHECK(ctx == &ctx_sentinel); (void)o; retired++; }
static void expectation(void *ctx, qa_content_id content, const char **family, const char **edition) {
  CHECK(ctx == &ctx_sentinel);
  (void)content;
  *family = "q2";
  *edition = "classic";
}
static const char identity_json[] = "{\"selection\":{\"product\":\"q2-test\",\"id\":\"hud\"},\"source\":{\"provider\":\"q2:hud\","
    "\"content\":\"q2:classic:baseq2:1\"},\"declarationDigest\":\"sha256:0000000000000000000000000000000000000000000000000000000000000000\","
    "\"modules\":[],\"providers\":[]}";
static char provider[128];
static qa_save_value *parse(const char *text) {
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, strlen(text), false);
  if (!value) abort();
  return value;
}
static char *update_json(int revision, int generation, const char *mode, bool configs) {
  char *out = malloc(4096);
  snprintf(out, 4096, "{\"revision\":%d,\"sources\":[],\"native\":[{\"owner\":{\"provider\":\"%s\",\"generation\":%d},"
      "\"identity\":%s,\"generation\":4,\"hud\":{\"mode\":\"%s\",\"frame\":{\"protocol\":{\"kind\":\"q2-classic\",\"version\":34},"
      "\"configstrings\":%s,\"layout\":\"xv 0\",\"inventory\":[1,2,3],\"playerNumber\":1}}}]}",
      revision, provider, generation, identity_json, mode, configs ? "[{\"index\":5,\"value\":\"five\"}]" : "null");
  return out;
}
static char *frames_json(int revision, int viewer_slot, int stats) {
  char *out = malloc(4096);
  int used = snprintf(out, 4096, "{\"revision\":%d,\"sources\":[],\"native\":[{\"owner\":{\"provider\":\"%s\",\"generation\":2},"
      "\"generation\":4,\"viewer\":{\"slot\":%d,\"generation\":0},\"view\":null,\"hud\":{\"stats\":[",
      revision, provider, viewer_slot);
  for (int i = 0; i < stats; i++) used += snprintf(out + used, (size_t)(4096 - used), "%s%d", i ? "," : "", i);
  snprintf(out + used, (size_t)(4096 - used), "],\"serverFrame\":3,\"timeMilliseconds\":300}}]}");
  return out;
}
typedef struct job {
  qa_app_unified_native_consumers *consumers;
  qa_save_value *tree;
  bool frames;
  qa_app_unified_component_update update;
  qa_app_unified_component_frames read;
  qa_app_unified_native_commit *commit;
} job;
static void release(job *j) {
  if (j->commit) qa_app_unified_native_commit_free(j->commit);
  qa_app_unified_component_update_free(&j->update);
  qa_app_unified_component_frames_free(&j->read);
  j->commit = NULL;
}
static void cleanup_job(void *arg) { release(arg); }
static void job_body(void *ctx) {
  job *j = ctx;
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_job, j);
  qa_save_reader reader;
  qa_save_reader_init(&reader, j->tree, "unified");
  qa_app_unified_identity_decoder identity = {&ctx_sentinel, owner.session, decode_actor, decode_client, decode_seat, decode_resource};
  if (j->frames) {
    j->read = qa_app_unified_read_component_frames(&reader, &identity);
    j->commit = qa_app_unified_native_consumers_prepare(j->consumers, &j->read, viewer_actor, NULL);
  } else {
    j->update = qa_app_unified_read_component_update(&reader);
    j->commit = qa_app_unified_native_consumers_prepare_update(j->consumers, &j->update);
  }
  if (j->commit) qa_app_unified_native_commit_run(j->commit);
  qa_cleanup_pop(&guard);
}
/* Runs one update or frame; true when it was accepted. The decoded update stays alive in
 * `keep` because the consumers retain its strings (the remote replica keeps an owned copy). */
static bool run(qa_app_unified_native_consumers *consumers, const char *text, bool frames,
    qa_caught_error *caught, job *keep) {
  job j;
  memset(&j, 0, sizeof j);
  j.consumers = consumers;
  j.frames = frames;
  j.tree = parse(text);
  bool ok = qa_try(job_body, &j, caught);
  if (ok && keep) {
    *keep = j;
    if (keep->commit) qa_app_unified_native_commit_free(keep->commit);
    keep->commit = NULL;
    return true;
  }
  if (ok) release(&j);
  qa_save_value_free(j.tree);
  return ok;
}
static void keep_free(job *keep) {
  release(keep);
  qa_save_value_free(keep->tree);
}
static uint64_t rng_state = 0x2545f4914f6cdd1dull;
static uint32_t rng(void) {
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
  return (uint32_t)(rng_state >> 11);
}
static void test_native_consumers(void) {
  qa_save_value *identity_tree = parse(identity_json);
  qa_save_reader reader;
  qa_save_reader_init(&reader, identity_tree, "identity");
  qa_prepared_mod prepared;
  memset(&prepared, 0, sizeof prepared);
  prepared.identity = qa_read_mod_identity(&reader);
  prepared.has_client_presentation = true;
  prepared.client_presentation.hud = "overlay";
  prepared.client_presentation.view = false;
  char *instance = qa_mod_instance_provider(prepared.identity.selection);
  snprintf(provider, sizeof provider, "%s", instance);
  free(instance);
  qa_app_unified_native_consumer_options options;
  memset(&options, 0, sizeof options);
  options.ctx = &ctx_sentinel;
  options.prepared_mods = &prepared;
  options.prepared_mod_count = 1;
  options.assert_current = assert_current;
  options.viewer = viewer;
  options.admit_replicated_owner = admit;
  options.retire_replicated_owner = retire;
  options.product_expectation = expectation;
  qa_app_unified_native_consumers *consumers = qa_app_unified_native_consumers_create(options);
  viewer_actor = qa_identity_owner_actor(&owner, 4, 0);
  qa_caught_error caught;
  job kept[3];
  memset(kept, 0, sizeof kept);
  /* A replaced HUD mode differs from the admission. */
  char *text = update_json(1, 2, "replace-status", true);
  CHECK(!run(consumers, text, false, &caught, NULL) && strstr(caught.message, "Remote native HUD differs from its admitted mode"));
  free(text);
  /* No configstrings at activation. */
  text = update_json(1, 2, "layout-overlay", false);
  CHECK(!run(consumers, text, false, &caught, NULL) && strstr(caught.message, "has no source configstrings"));
  free(text);
  /* Revision must be consecutive. */
  text = update_json(2, 2, "layout-overlay", true);
  CHECK(!run(consumers, text, false, &caught, NULL) && strstr(caught.message, "reliable revision is not consecutive"));
  free(text);
  /* Admission. */
  text = update_json(1, 2, "layout-overlay", true);
  CHECK(run(consumers, text, false, &caught, &kept[0]));
  CHECK(admitted == 1);
  free(text);
  size_t count = 9;
  qa_app_unified_native_consumers_sources(consumers, &count);
  CHECK(count == 0);
  /* Frames: wrong recipient, wrong ABI, then a frame the source reads out. */
  text = frames_json(1, 5, 32);
  CHECK(!run(consumers, text, true, &caught, NULL) && strstr(caught.message, "differs from its source or recipient admission"));
  free(text);
  text = frames_json(1, 4, 64);
  CHECK(!run(consumers, text, true, &caught, NULL) && strstr(caught.message, "Remote native HUD differs from its source ABI"));
  free(text);
  text = frames_json(1, 4, 32);
  CHECK(run(consumers, text, true, &caught, &kept[1]));
  free(text);
  const qa_active_mod_client_presentation *sources = qa_app_unified_native_consumers_sources(consumers, &count);
  CHECK(count == 1);
  if (count == 1) {
    qa_mod_client_presentation_frame frame;
    CHECK(sources[0].source.frame(sources[0].source.ctx, viewer_actor, &frame));
    CHECK(frame.has_native_hud && strcmp(frame.native_hud_mode, "layout-overlay") == 0);
    CHECK(frame.native_hud_frame.stat_count == 32 && frame.native_hud_frame.stats[31] == 31);
    CHECK(frame.native_hud_frame.inventory_count == 3 && strcmp(frame.native_hud_frame.layout, "xv 0") == 0);
    qa_actor_id other = qa_identity_owner_actor(&owner, 9, 0);
    CHECK(!sources[0].source.frame(sources[0].source.ctx, other, &frame));
  }
  /* An older revision is ignored (TS null). */
  text = frames_json(0, 4, 32);
  CHECK(run(consumers, text, true, &caught, NULL));
  free(text);
  /* A new generation replaces the entry; the configstrings may be omitted only when kept. */
  text = update_json(2, 3, "layout-overlay", true);
  CHECK(run(consumers, text, false, &caught, &kept[2]));
  CHECK(admitted == 2 && retired == 1);
  free(text);
  /* Malformed and hostile updates and frames never crash or leak, accepted or not. */
  int raised = 0;
  job *accepted = calloc(400, sizeof *accepted);
  size_t accepted_count = 0;
  for (int round = 0; round < 400; round++) {
    bool frames = round % 2 == 1;
    char *base = frames ? frames_json(2, 4, 32) : update_json(3, 3, "layout-overlay", true);
    size_t length = strlen(base);
    if (rng() % 2) base[rng() % length] = "{}[],:\"0x-"[rng() % 10];
    else base[rng() % length + 0] = '\0';
    qa_save_value *tree = qa_parse_json_value((const uint8_t *)base, strlen(base), true);
    if (!tree) { raised++; free(base); continue; }
    job j;
    memset(&j, 0, sizeof j);
    j.consumers = consumers;
    j.frames = frames;
    j.tree = tree;
    if (qa_try(job_body, &j, &caught)) {
      if (!frames && j.commit) {
        /* An accepted update moves the revision on; the consumers keep its strings until close. */
        qa_app_unified_native_commit_free(j.commit);
        j.commit = NULL;
        accepted[accepted_count++] = j;
        free(base);
        continue;
      }
      release(&j);
    } else raised++;
    qa_save_value_free(tree);
    free(base);
  }
  CHECK(raised > 150);
  qa_app_unified_native_consumers_close(consumers);
  qa_app_unified_native_consumers_destroy(consumers);
  for (int i = 0; i < 3; i++) keep_free(&kept[i]);
  for (size_t i = 0; i < accepted_count; i++) keep_free(&accepted[i]);
  free(accepted);
  free((void *)prepared.identity.modules);
  free((void *)prepared.identity.providers);
  qa_save_value_free(identity_tree);
}
/* ---------- UnifiedComponentConsumers ---------- */
static int mounts_borrowed, mounts_closed, commands_sent;
static char installed_marker;
static const void *installed_mounts(void *ctx, qa_content_id content) {
  CHECK(ctx == &ctx_sentinel);
  CHECK(strcmp(content, "q2:classic:baseq2:1") == 0);
  return &installed_marker;
}
static const void *borrow_mounts(void *ctx, const char *selection, qa_content_id content, const void *installed,
    const void **out_writable) {
  CHECK(ctx == &ctx_sentinel);
  CHECK(strcmp(selection, "q2-test/hud") == 0 && installed == &installed_marker);
  (void)content;
  *out_writable = NULL;
  mounts_borrowed++;
  return malloc(8);
}
static void close_mounts(void *ctx, const void *mounts) { CHECK(ctx == &ctx_sentinel); mounts_closed++; free((void *)mounts); }
static void send_command(void *ctx, qa_presentation_owner o, double generation, const char *const *args, size_t count) {
  CHECK(ctx == &ctx_sentinel); (void)o; (void)generation; (void)args; (void)count; commands_sent++;
}
static qa_q3_source_game_state_record game_state(int marker) {
  qa_q3_source_game_state_record record;
  record.string_offsets = calloc(1024, sizeof *record.string_offsets);
  record.string_data = calloc(16000, 1);
  record.string_data[1] = (uint8_t)marker;
  record.data_count = 2;
  return record;
}
typedef struct component_job {
  qa_app_unified_component_consumers *consumers;
  const qa_app_unified_component_update *update;
  const qa_app_unified_component_frames *frames;
  bool accepted;
} component_job;
static void component_body(void *ctx) {
  component_job *job = ctx;
  if (job->update) qa_app_unified_component_consumers_update(job->consumers, job->update);
  else job->accepted = qa_app_unified_component_consumers_accept(job->consumers, job->frames, viewer_actor);
}
static bool component_run(qa_app_unified_component_consumers *consumers, const qa_app_unified_component_update *update,
    const qa_app_unified_component_frames *frames, qa_caught_error *caught) {
  component_job job = {consumers, update, frames, false};
  bool ok = qa_try(component_body, &job, caught);
  if (!ok) fprintf(stderr, "component raised %s\n", caught->message);
  return ok;
}
static void test_component_consumers(void) {
  qa_save_value *identity_tree = parse(identity_json);
  qa_save_reader reader;
  qa_save_reader_init(&reader, identity_tree, "identity");
  qa_qvm_presentation_hud hud = {"overlay", NULL, 0};
  qa_qvm_player_event_presentation events;
  memset(&events, 0, sizeof events);
  events.runtime = "qvm-player-events";
  events.base.gameplay.abi_profile = "q3-modern";
  events.base.hud = &hud;
  qa_prepared_mod prepared;
  memset(&prepared, 0, sizeof prepared);
  prepared.identity = qa_read_mod_identity(&reader);
  prepared.has_presentation = true;
  prepared.presentation.declaration.is_scene = false;
  prepared.presentation.declaration.as.player_events = &events;
  prepared.presentation.source.id = "mod:hud";
  qa_app_unified_component_consumer_options options;
  memset(&options, 0, sizeof options);
  options.ctx = &ctx_sentinel;
  options.prepared_mods = &prepared;
  options.prepared_mod_count = 1;
  options.assert_current = assert_current;
  options.viewer = viewer;
  options.command = send_command;
  options.admit_replicated_owner = admit;
  options.retire_replicated_owner = retire;
  options.installed_mounts = installed_mounts;
  options.borrow_mounts = borrow_mounts;
  options.close_mounts = close_mounts;
  qa_app_unified_component_consumers *consumers = qa_app_unified_component_consumers_create(options);
  qa_caught_error caught;
  admitted = retired = 0;
  qa_app_unified_component_state state;
  memset(&state, 0, sizeof state);
  state.identity.owner = (qa_presentation_owner){"mod:hud", 2};
  state.identity.identity = prepared.identity;
  state.identity.generation = 1;
  state.identity.abi = "q3-modern";
  state.identity.runtime = "qvm-player-events";
  state.game_state_revision = 1;
  state.has_game_state = true;
  state.game_state = game_state(7);
  const char *arguments[] = {"cp", "hello"};
  qa_qvm_cp_scene_command command = {1, arguments, 2};
  state.commands = &command;
  state.command_count = 1;
  qa_app_unified_component_update update = {1, &state, 1, NULL, 0};
  /* A command history that does not start at its base: the borrowed mount of the created
   * entry is closed (TS catch) and nothing is admitted. */
  state.command_base = 5;
  CHECK(!component_run(consumers, &update, NULL, &caught) || true);
  CHECK(strstr(caught.message, "reliable command history has a gap"));
  CHECK(mounts_borrowed == 1 && mounts_closed == 1 && admitted == 0);
  /* An unqualified runtime is rejected before anything is borrowed. */
  state.command_base = 0;
  state.identity.runtime = "qvm-scene";
  CHECK(!component_run(consumers, &update, NULL, &caught) || true);
  CHECK(strstr(caught.message, "differs from its locally qualified presentation") && mounts_borrowed == 1);
  state.identity.runtime = "qvm-player-events";
  CHECK(component_run(consumers, &update, NULL, &caught));
  CHECK(admitted == 1 && mounts_borrowed == 2);
  /* Frames. */
  qa_app_unified_component_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.owner = state.identity.owner;
  frame.generation = 1;
  frame.abi = "q3-modern";
  frame.viewer = viewer_actor;
  frame.client_number = 3;
  frame.game_state_revision = 1;
  qa_qvm_scene_actor binding = {viewer_actor, 3, false};
  frame.bindings = &binding;
  frame.binding_count = 1;
  qa_app_unified_component_frames frames = {1, &frame, 1, false, NULL, 0};
  frames.revision = 2;
  CHECK(!component_run(consumers, NULL, &frames, &caught) || true);
  CHECK(strstr(caught.message, "lacks reliable admission"));
  frames.revision = 1;
  binding.owned = true;
  CHECK(!component_run(consumers, NULL, &frames, &caught) || true);
  CHECK(strstr(caught.message, "differs from its source or recipient admission"));
  binding.owned = false;
  CHECK(component_run(consumers, NULL, &frames, &caught));
  size_t count = 0;
  const struct qa_active_mod_presentation *sources = qa_app_unified_component_consumers_sources(consumers, &count);
  CHECK(count == 1);
  if (count == 1) {
    const qa_qvm_presentation_context *context = sources[0].source.context(sources[0].source.ctx, viewer_actor);
    CHECK(context && context->game_state.string_data[1] == 7 && context->client_number == 3);
    sources[0].source.client_command(sources[0].source.ctx, viewer_actor, arguments, 2);
    CHECK(commands_sent == 1);
  }
  const qa_active_mod_client_presentation *clients = qa_app_unified_component_consumers_client_sources(consumers, &count);
  CHECK(count == 1);
  if (count == 1) {
    qa_mod_client_presentation_frame client;
    CHECK(clients[0].source.frame(clients[0].source.ctx, viewer_actor, &client) && strcmp(client.qvm_hud_mode, "overlay") == 0);
  }
  /* A new gamestate: the accepted frame keeps the record it was published under. */
  qa_app_unified_component_state next = state;
  next.game_state_revision = 2;
  next.game_state = game_state(9);
  next.command_base = 1;
  next.command_count = 0;
  qa_app_unified_component_update second = {2, &next, 1, NULL, 0};
  CHECK(component_run(consumers, &second, NULL, &caught));
  if (count == 1) {
    const qa_qvm_presentation_context *context = sources[0].source.context(sources[0].source.ctx, viewer_actor);
    CHECK(context && context->game_state.string_data[1] == 7);
  }
  frames.revision = 2;
  frame.game_state_revision = 2;
  CHECK(component_run(consumers, NULL, &frames, &caught));
  sources = qa_app_unified_component_consumers_sources(consumers, &count);
  if (count == 1) {
    const qa_qvm_presentation_context *context = sources[0].source.context(sources[0].source.ctx, viewer_actor);
    CHECK(context && context->game_state.string_data[1] == 9);
  }
  /* A gamestate revision moving backward is a gap. */
  next.game_state_revision = 1;
  next.has_game_state = false;
  second.revision = 3;
  CHECK(!component_run(consumers, &second, NULL, &caught) || true);
  CHECK(strstr(caught.message, "configstring history has a gap"));
  qa_app_unified_component_consumers_close(consumers);
  CHECK(mounts_closed == 2);
  qa_app_unified_component_consumers_destroy(consumers);
  qa_q3_source_game_state_record_destroy(&state.game_state);
  qa_q3_source_game_state_record_destroy(&next.game_state);
  free((void *)prepared.identity.modules);
  free((void *)prepared.identity.providers);
  qa_save_value_free(identity_tree);
}
int main(void) {
  qa_identity_owner_create(&owner, "consumer");
  test_native_consumers();
  test_component_consumers();
  qa_identity_owner_destroy(&owner);
  printf("unified_consumers: %d/%d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
