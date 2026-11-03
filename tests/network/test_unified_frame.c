/* Port of tests/network/unified/frame.test.ts and tests/network/unified/content.test.ts, the
 * owns() checks of tests/network/unified-events.test.ts, and fixed-seed malformed-input checks
 * for every unified client reader: frame codec, prediction, native states/frames, component
 * frames, composition and the remote replica's receive paths. GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "qa/app/bootstrap/network/remote_unified.h"
#include "qa/app/bootstrap/network/unified_components.h"
#include "qa/app/bootstrap/network/unified_content.h"
#include "qa/app/bootstrap/network/unified_event_codec.h"
#include "qa/app/bootstrap/network/unified_frame_codec.h"
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include "qa/app/bootstrap/network/unified_native_components.h"
#include "qa/app/bootstrap/network/unified_prediction.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
#include "unified_content_corpus.h"
#include "unified_frame_corpus.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int ctx_sentinel;
static qa_identity_owner server, client;

/* ---------- zlib helpers ---------- */
static uint8_t *deflate_bytes(const uint8_t *data, size_t length, size_t *out_length) {
  z_stream s;
  memset(&s, 0, sizeof s);
  deflateInit2(&s, 1, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
  size_t bound = deflateBound(&s, (uLong)length);
  uint8_t *out = malloc(bound);
  s.next_in = (Bytef *)data; s.avail_in = (uInt)length; s.next_out = out; s.avail_out = (uInt)bound;
  deflate(&s, Z_FINISH);
  *out_length = s.total_out;
  deflateEnd(&s);
  return out;
}
static char *inflate_text(const uint8_t *data, size_t length) {
  z_stream s;
  memset(&s, 0, sizeof s);
  inflateInit2(&s, -15);
  size_t cap = 1 << 20;
  char *out = malloc(cap + 1);
  s.next_in = (Bytef *)data; s.avail_in = (uInt)length; s.next_out = (Bytef *)out; s.avail_out = (uInt)cap;
  inflate(&s, Z_FINISH);
  out[s.total_out] = '\0';
  inflateEnd(&s);
  return out;
}
/* ---------- identity ---------- */
static qa_app_unified_actor_reference encode_actor(void *ctx, qa_actor_id actor) {
  CHECK(ctx == &ctx_sentinel);
  return (qa_app_unified_actor_reference){(double)actor.slot, (double)actor.generation};
}
static bool session_matches(void *ctx, qa_session_id session) { CHECK(ctx == &ctx_sentinel); (void)session; return true; }
static const qa_app_unified_identity_encoder encoder = {&ctx_sentinel, {0}, session_matches, encode_actor};
static qa_identity_owner *decoding_owner = &client;
static qa_actor_id decode_actor(void *ctx, double slot, double generation) {
  CHECK(ctx == &ctx_sentinel);
  return qa_identity_owner_actor(decoding_owner, (int64_t)slot, (int64_t)generation);
}
static qa_client_id decode_client(void *ctx, double slot, double generation) {
  CHECK(ctx == &ctx_sentinel);
  return qa_identity_owner_client(decoding_owner, (int64_t)slot, (int64_t)generation);
}
static qa_seat_id decode_seat(void *ctx, double index) {
  CHECK(ctx == &ctx_sentinel);
  return qa_identity_owner_seat(decoding_owner, (int64_t)index);
}
static qa_resource_id decode_resource(void *ctx, qa_resource_id id) { CHECK(ctx == &ctx_sentinel); return id; }
static qa_app_unified_identity_decoder identity_decoder(void) {
  qa_app_unified_identity_decoder d = {&ctx_sentinel, client.session, decode_actor, decode_client, decode_seat, decode_resource};
  return d;
}
/* ---------- fixture (frame.test.ts) ---------- */
static const qa_vec3 origin = {1, 2, 3};
static const qa_vec4 white = {1, 1, 1, 1};
static qa_loose_mount server_mount, local_mount;
static qa_resolved_resource_reference server_resource, local_resource;
static qa_q3_mesh_model mesh;
static qa_decoded_model model;
static double resource_byte_length_override = -1;
static int resource_calls, model_calls;
static qa_resolved_resource_reference frame_resource(void *ctx, const qa_app_unified_resource_key *key) {
  CHECK(ctx == &ctx_sentinel);
  (void)key;
  resource_calls++;
  qa_resolved_resource_reference out = local_resource;
  if (resource_byte_length_override >= 0) out.byte_length = (int64_t)resource_byte_length_override;
  return out;
}
static qa_decoded_model frame_model(void *ctx, const qa_app_unified_resource_key *key, bool has_brush, double brush) {
  CHECK(ctx == &ctx_sentinel);
  CHECK(strcmp(key->path, "models/test.md3") == 0);
  CHECK(!has_brush);
  (void)brush;
  model_calls++;
  return model;
}
static qa_app_unified_frame_decoder frame_decoder(void) {
  qa_app_unified_frame_decoder d;
  memset(&d, 0, sizeof d);
  d.ctx = &ctx_sentinel;
  d.identity = identity_decoder();
  d.world = NULL;
  d.resource = frame_resource;
  d.model = frame_model;
  return d;
}
static void init_resources(void) {
  server_mount.identity = (qa_mount_identity){"mount:test:base", "q3:base:baseq3:1", 0};
  server_mount.root_path = "/server-private";
  local_mount = server_mount;
  local_mount.root_path = "/local-only";
  server_resource.id = "resource:test";
  server_resource.requested_path = "models/test.md3";
  server_resource.byte_length = 42;
  server_resource.digest = "sha256:0000000000000000000000000000000000000000000000000000000000000000";
  server_resource.provenance.kind = QA_CONTENT_MOUNT_LOOSE;
  server_resource.provenance.loose_mount = &server_mount;
  server_resource.provenance.member_path = "models/test.md3";
  server_resource.resolution.kind = QA_RESOURCE_RESOLUTION_DEFAULT_ORDER;
  server_resource.resolution.plan = "mount-plan:test:base";
  local_resource = server_resource;
  local_resource.provenance.loose_mount = &local_mount;
  memset(&model, 0, sizeof model);
  model.kind = QA_DECODED_MODEL_Q3_MESH;
  model.as.q3_mesh = &mesh;
}
typedef struct fixture {
  qa_app_unified_presentation_frame_t frame;
  qa_actor_id id, ground;
  qa_actor_observation actors[1];
  qa_body_snapshot bodies[1];
  qa_inventory_entry entries[1];
  qa_inventory_snapshot inventories[1];
  qa_provider_reference weapons[1];
  qa_actor_configuration configurations[1];
  qa_skeleton_joint_pose joints[1];
  qa_scene_entity entities[1], attached;
  qa_scene_entity_attachment attachments[1];
  qa_scene_light lights[1];
  qa_scene_particle particles[2];
  qa_scene_light_style styles[2];
  uint8_t area[2]; qa_byte_view area_view;
  qa_save_value *event_wire, *event;
  uint8_t skin_pixels[2];
  qa_app_simulation_presentation models[1];
  qa_q3_character_view characters[1];
  qa_world_text texts[1];
  qa_active_powerup_timer powerups[1];
  qa_weapon_hud_status status;
  qa_inventory_entry ammo[1];
  qa_save_value *oracle;
} fixture;
static qa_save_value *parse_json(const char *text) {
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, strlen(text), false);
  if (!value) abort();
  return value;
}
static void build_fixture(fixture *f) {
  memset(f, 0, sizeof *f);
  f->oracle = parse_json(ts_frame_canonical);
  qa_app_unified_presentation_frame_t *frame = &f->frame;
  f->id = qa_identity_owner_actor(&server, 4, 2);
  f->ground = f->id;
  frame->epoch = 3;
  frame->acknowledged_input = 9;
  qa_app_unified_prediction_projection *p = &frame->prediction;
  p->actor = f->id;
  p->snapshot.sequence = 9;
  p->snapshot.command_time_milliseconds = 1000;
  p->snapshot.state.kind = QA_MOVEMENT_STATE_Q2;
  p->snapshot.state.as.q2.origin_eighths.values[0] = 8;
  p->snapshot.state.as.q2.origin_eighths.values[1] = 16;
  p->snapshot.state.as.q2.origin_eighths.values[2] = 24;
  p->snapshot.state.as.q2.gravity = 800;
  p->profile.kind = "q2-classic";
  p->profile.id = "q2:movement";
  p->profile.clock.kind = QA_CLOCK_Q2_CLASSIC;
  p->profile.clock.frame_milliseconds = 100;
  p->profile.numeric.id = "q2:binary32";
  p->profile.numeric.arithmetic.kind = QA_ARITHMETIC_BINARY32;
  p->profile.numeric.float_to_int = QA_FLOAT_TO_INT_CHECKED_C_TRUNCATION;
  p->snapshot.arsenal.provider = "q1:arsenal";
  p->snapshot.arsenal.active_weapon = "q1:shotgun";
  f->ammo[0] = (qa_inventory_entry){"q2:cells", 17, 200, false, {0}};
  p->snapshot.arsenal.ammo = f->ammo;
  p->snapshot.arsenal.ammo_count = 1;
  p->snapshot.arsenal.state.kind = "q1";
  p->snapshot.arsenal.state.source_weapon = 1;
  p->snapshot.animation.provider = "q3:character";
  p->snapshot.animation.state.kind = "q3";
  p->snapshot.animation.state.legs = 1;
  p->snapshot.animation.state.torso = 2;
  p->snapshot.animation.state.legs_timer_milliseconds = 100;
  p->snapshot.animation.state.torso_timer_milliseconds = 200;
  qa_bounds bounds = {{-16, -16, -24}, {16, 16, 32}};
  p->standing_bounds = bounds;
  p->standing_view_height = 26;
  p->snapshot.bounds = bounds;
  p->snapshot.view_angles = origin;
  p->snapshot.view_height = 26;
  p->snapshot.view_offset = (qa_vec3){0, 0, 26};
  p->snapshot.environment.health = 85;
  p->snapshot.environment.gravity_multiplier = 1;
  qa_world_snapshot *s = &frame->output.snapshot;
  s->session = server.session;
  s->frame = (qa_frame_context){7, {QA_SOURCE_TIME_SECONDS, 1}, {QA_SOURCE_TIME_SECONDS, 0.1}, QA_FRAME_PHASE_FRAME_EXIT};
  f->actors[0] = (qa_actor_observation){0};
  f->actors[0].id = f->id; f->actors[0].owner = "q1:game"; f->actors[0].definition = "q1:player";
  s->actors = f->actors; s->actor_count = 1;
  f->bodies[0].actor = f->id;
  f->bodies[0].body.origin = origin; f->bodies[0].body.angles = origin; f->bodies[0].body.velocity = origin;
  f->bodies[0].body.bounds = (qa_bounds){origin, origin};
  f->bodies[0].body.ground = &f->ground;
  s->bodies = f->bodies; s->body_count = 1;
  f->entries[0] = f->ammo[0];
  f->inventories[0].actor = f->id; f->inventories[0].entries = f->entries; f->inventories[0].entry_count = 1;
  s->inventories = f->inventories; s->inventory_count = 1;
  f->weapons[0] = (qa_provider_reference){"q1:arsenal", "q1:registered:id1:1"};
  qa_actor_configuration *c = &f->configurations[0];
  c->actor = f->id;
  c->movement = (qa_provider_reference){"q2:movement", "q2:classic:baseq2:1"};
  c->character.definition = (qa_provider_reference){"q3:character", "q3:base:baseq3:1"};
  c->character.appearance = (qa_provider_reference){"q3:sarge", "q3:base:baseq3:1"};
  c->weapons.values = f->weapons; c->weapons.count = 1;
  c->inventory = (qa_provider_reference){"q2:inventory", "q2:classic:baseq2:1"};
  s->configurations = f->configurations; s->configuration_count = 1;
  s->scene.session = server.session;
  s->scene.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, 1};
  f->joints[0] = (qa_skeleton_joint_pose){origin, white, 1};
  qa_scene_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.actor = &f->id;
  entity.resource = server_resource;
  entity.model = model;
  entity.transform.origin = origin;
  entity.transform.axis.vectors[0] = (qa_vec3){1, 0, 0};
  entity.transform.axis.vectors[1] = (qa_vec3){0, 1, 0};
  entity.transform.axis.vectors[2] = (qa_vec3){0, 0, 1};
  entity.transform.scale = origin;
  entity.previous_origin = origin;
  entity.pose.joints = f->joints; entity.pose.joint_count = 1;
  entity.skin = 2;
  entity.color = white;
  entity.shader_time = (qa_source_time){QA_SOURCE_TIME_SECONDS, 1};
  entity.flags_kind = "q3";
  entity.lighting_origin = origin;
  f->attached = entity;
  f->attachments[0] = (qa_scene_entity_attachment){"tag_weapon", &f->attached};
  f->entities[0] = entity;
  f->entities[0].attachments = f->attachments; f->entities[0].attachment_count = 1;
  s->scene.entities = f->entities; s->scene.entity_count = 1;
  f->lights[0] = (qa_scene_light){origin, origin, 10, true, {"q2", 2, true, origin, 0.4, true, 64}};
  s->scene.lights = f->lights; s->scene.light_count = 1;
  f->particles[0] = (qa_scene_particle){false, origin, 10, 0.5, 1, 0, {0, 0, 0, 0}};
  f->particles[1] = (qa_scene_particle){true, origin, 0, 0, 2, 0.4, white};
  s->scene.particles = f->particles; s->scene.particle_count = 2;
  f->styles[0] = (qa_scene_light_style){false, 0, 1, {0, 0, 0}, 0};
  f->styles[1] = (qa_scene_light_style){true, 2, 0, origin, 1};
  s->scene.light_styles = f->styles; s->scene.light_style_count = 2;
  f->area[0] = 1; f->area[1] = 4;
  f->area_view = (qa_byte_view){f->area, 2};
  s->scene.area_bits = &f->area_view;
  /* The event rides as the decoded tree, built from the TS wire row with server identities. */
  const qa_save_value *output = qa_save_value_get(f->oracle, "output");
  f->event_wire = (qa_save_value *)qa_save_value_get(output, "events")->items[0];
  decoding_owner = &server;
  qa_app_unified_identity_decoder server_identity = identity_decoder();
  f->event = qa_app_unified_read_simulation_event(f->event_wire, &server_identity);
  decoding_owner = &client;
  frame->events = &f->event; frame->event_count = 1;
  qa_app_simulation_presentation *m = &f->models[0];
  m->actor = f->id; m->content = "q2:classic:baseq2:1"; m->family = QA_GAME_FAMILY_Q2; m->path = "players/male/tris.md2";
  m->frame = 1; m->has_back_lerp = true; m->back_lerp = 0.25;
  f->skin_pixels[0] = 7; f->skin_pixels[1] = 8;
  m->has_indexed_skin = true;
  m->indexed_skin.name = "translated"; m->indexed_skin.width = 2; m->indexed_skin.height = 1;
  m->indexed_skin.pixels.bytes = f->skin_pixels; m->indexed_skin.pixels.length = 2;
  m->effects = 1; m->origin = origin; m->has_previous_origin = true; m->previous_origin = origin;
  m->angles = origin; m->scale = 1; m->visible = true; m->has_alpha = true; m->alpha = 0.5;
  m->has_q3_weapon = true; m->q3_weapon_time_ms = 1000; m->q3_weapon_torso_animation = 1; m->q3_weapon_weapon = 2;
  frame->models = f->models; frame->model_count = 1;
  qa_q3_character_view *v = &f->characters[0];
  v->actor = f->id; v->origin = origin; v->angles = origin; v->velocity = origin;
  v->animation.kind = "q3"; v->animation.legs = 1; v->animation.torso = 2;
  v->animation.legs_timer_milliseconds = 100; v->animation.torso_timer_milliseconds = 200;
  v->powerups = 4; v->team = "red"; v->color = white; v->has_opacity = true; v->opacity = 0.7;
  frame->characters = f->characters; frame->character_count = 1;
  qa_world_text *t = &f->texts[0];
  t->content = "q1:registered:id1:1"; t->text = "shared"; t->origin = origin; t->color = white; t->cell_size = 8;
  t->orientation_fixed = true; t->angles = origin; t->depth_test = true; t->font_selected = true;
  t->has_distance_cull_factor = true; t->distance_cull_factor = 0.2;
  frame->world_text = f->texts; frame->world_text_count = 1;
  frame->player_actor = f->id;
  qa_app_sim_player_view *view = &frame->player_view;
  view->origin = origin; view->angles = origin; view->view_height = 22;
  view->has_blend = true; view->blend = white; view->has_kick_angles = true; view->kick_angles = origin;
  view->has_field_of_view = true; view->field_of_view = 110;
  view->has_pitch_drift = true; view->pitch_drift_grounded = true; view->pitch_drift_ideal_pitch = 5;
  qa_app_sim_player_ui *ui = &frame->player_ui;
  const qa_save_value *player = qa_save_value_get(f->oracle, "player");
  ui->native_inventory = qa_save_value_get(qa_save_value_get(player, "ui"), "nativeInventory");
  ui->health = 85;
  ui->armor.regular.kind = QA_REGULAR_ARMOR_Q3; ui->armor.regular.points = 25; ui->armor.regular.protection = 0.66;
  ui->armor.powered.kind = "shield"; ui->armor.powered.cells = 17;
  ui->active_weapon = "q1:shotgun";
  ui->has_ammo = true; ui->ammo_item = "q2:cells"; ui->ammo_count = 17;
  ui->inventory = f->entries; ui->inventory_count = 1;
  f->powerups[0] = (qa_active_powerup_timer){"q3:quad", "Quad", 10};
  ui->powerups = f->powerups; ui->powerup_count = 1;
  f->status.source = (qa_provider_reference){"q1:arsenal", "q1:registered:id1:1"};
  f->status.item = "q1:shotgun"; f->status.label = "Shotgun";
  f->status.ammo_is_finite = true; f->status.ammo_item = "q2:cells"; f->status.ammo_count = 17;
  f->status.has_ammo_to_start = true;
  ui->weapon_status = &f->status;
  ui->arsenal_warning = "none";
}
static void free_fixture(fixture *f) {
  qa_save_value_free(f->event);
  qa_save_value_free(f->oracle);
}
static uint8_t *encode(const qa_app_unified_presentation_frame_t *frame, size_t *length) {
  return qa_app_unified_encode_frame(frame, &encoder, length);
}
/* ---------- qa_try plumbing ---------- */
typedef struct decode_job {
  const uint8_t *bytes; size_t length;
  qa_app_unified_frame_decoder decoder;
  qa_app_unified_presentation_frame_t frame;
} decode_job;
static void decode_body(void *ctx) {
  decode_job *job = ctx;
  job->frame = qa_app_unified_decode_frame(job->bytes, job->length, &job->decoder);
}
static bool try_decode(const uint8_t *bytes, size_t length, qa_caught_error *caught, decode_job *job) {
  memset(job, 0, sizeof *job);
  job->bytes = bytes; job->length = length; job->decoder = frame_decoder();
  return qa_try(decode_body, job, caught);
}
static bool same_actor(qa_actor_id a, qa_actor_id b) { return qa_same_actor(a, b); }
static void test_frame_roundtrip(void) {
  fixture f;
  build_fixture(&f);
  size_t length = 0;
  uint8_t *bytes = encode(&f.frame, &length);
  char *text = inflate_text(bytes, length);
  /* Byte-exact against the TS canonical encoding of the same fixture. */
  CHECK(strcmp(text, ts_frame_canonical) == 0);
  if (strcmp(text, ts_frame_canonical) != 0) {
    size_t i = 0;
    while (text[i] && text[i] == ts_frame_canonical[i]) i++;
    fprintf(stderr, "first difference at %zu:\n C: %.160s\nTS: %.160s\n", i, text + (i > 40 ? i - 40 : 0),
        ts_frame_canonical + (i > 40 ? i - 40 : 0));
  }
  CHECK(strstr(text, "\"version\":10") != NULL);
  CHECK(strstr(text, "/server-private") == NULL);
  CHECK(strstr(text, "local-model") == NULL);
  free(text);
  qa_caught_error caught;
  decode_job job;
  resource_calls = model_calls = 0;
  bool ok = try_decode(bytes, length, &caught, &job);
  CHECK(ok);
  if (!ok) fprintf(stderr, "decode raised %s: %s\n", caught.name, caught.message);
  if (ok) {
    qa_app_unified_presentation_frame_t *r = &job.frame;
    const qa_world_snapshot *s = &r->output.snapshot;
    CHECK(s->session.token == client.session.token);
    qa_actor_id local = qa_identity_owner_actor(&client, 4, 2);
    CHECK(same_actor(r->player_actor, local));
    CHECK(!same_actor(r->player_actor, f.id));
    CHECK(s->bodies[0].body.ground && same_actor(*s->bodies[0].body.ground, r->player_actor));
    CHECK(resource_calls == 2 && model_calls == 2);
    CHECK(s->scene.entities[0].resource.provenance.loose_mount == &local_mount);
    CHECK(s->scene.entities[0].model.as.q3_mesh == &mesh);
    const qa_scene_entity *child = s->scene.entities[0].attachments[0].entity;
    CHECK(child->actor && same_actor(*child->actor, r->player_actor));
    CHECK(!s->scene.entities[0].pose.is_frame && s->scene.entities[0].pose.joint_count == 1
        && s->scene.entities[0].pose.joints[0].scale == 1);
    CHECK(memcmp(&s->scene.lights[0], &f.lights[0], sizeof f.lights[0]) == 0
        || (s->scene.lights[0].profile.shadow_resolution == 64 && s->scene.lights[0].profile.cone_cos_half_angle == 0.4));
    CHECK(s->scene.particle_count == 2 && s->scene.particles[0].palette_index == 10 && s->scene.particles[1].rotation == 0.4);
    CHECK(s->scene.light_style_count == 2 && s->scene.light_styles[1].white == 1 && s->scene.light_styles[0].value == 1);
    CHECK(s->scene.area_bits && s->scene.area_bits->length == 2 && s->scene.area_bits->bytes[1] == 4);
    CHECK(strcmp(s->configurations[0].movement.provider, "q2:movement") == 0);
    CHECK(r->models[0].indexed_skin.pixels.length == 2 && r->models[0].indexed_skin.pixels.bytes[0] == 7);
    CHECK(r->characters[0].animation.legs == 1 && r->characters[0].animation.torso_timer_milliseconds == 200);
    CHECK(r->epoch == 3 && r->acknowledged_input == 9);
    CHECK(strcmp(qa_save_value_get(qa_save_value_get(r->events[0], "audience"), "kind")->text, "client") == 0);
    CHECK(same_actor(r->prediction.actor, local) && r->prediction.snapshot.sequence == 9);
    CHECK(r->player_view.has_pitch_drift && r->player_view.field_of_view == 110);
    CHECK(r->player_ui.armor.powered.cells == 17 && r->player_ui.weapon_status
        && strcmp(r->player_ui.weapon_status->label, "Shotgun") == 0);
    /* Re-encoding the decoded frame reproduces the same canonical bytes (player.ui/view, worldText equal). */
    size_t again_length = 0;
    uint8_t *again = encode(r, &again_length);
    char *again_text = inflate_text(again, again_length);
    CHECK(strcmp(again_text, ts_frame_canonical) == 0);
    free(again_text);
    free(again);
    qa_app_unified_presentation_frame_free(r);
  }
  /* Mismatched local resource and unsafe server path. */
  resource_byte_length_override = 43;
  CHECK(!try_decode(bytes, length, &caught, &job) && strstr(caught.message, "differs"));
  resource_byte_length_override = -1;
  f.entities[0].resource.requested_path = "../outside.md3";
  free(bytes);
  bytes = encode(&f.frame, &length);
  CHECK(!try_decode(bytes, length, &caught, &job) && strstr(caught.message, "relative resource path"));
  f.entities[0].resource.requested_path = "models/test.md3";
  free(bytes);
  /* Wrong schema before resolving resources. */
  const char *wrong = "{\"schema\":\"wrong\",\"version\":1,\"epoch\":3}";
  uint8_t *wrong_bytes = deflate_bytes((const uint8_t *)wrong, strlen(wrong), &length);
  resource_calls = 0;
  CHECK(!try_decode(wrong_bytes, length, &caught, &job) && strstr(caught.message, "qts-unified-frame") && resource_calls == 0);
  free(wrong_bytes);
  /* zlib failures keep the TS (node:zlib) text. */
  static const uint8_t garbage[] = {0xff, 0xff, 0xff, 0xff};
  CHECK(!try_decode(garbage, 4, &caught, &job) && strcmp(caught.message, "invalid block type") == 0);
  CHECK(!try_decode(garbage, 0, &caught, &job) && strcmp(caught.message, "unexpected end of file") == 0);
  /* Source armor is carried without assigning an absorption formula. */
  f.frame.player_ui.armor.regular.kind = QA_REGULAR_ARMOR_SOURCE;
  f.frame.player_ui.armor.regular.points = 39;
  f.frame.player_ui.armor.regular.item = "mod:armor";
  bytes = encode(&f.frame, &length);
  ok = try_decode(bytes, length, &caught, &job);
  CHECK(ok);
  if (ok) {
    const qa_armor_state *armor = &job.frame.player_ui.armor;
    CHECK(armor->regular.kind == QA_REGULAR_ARMOR_SOURCE && armor->regular.points == 39
        && strcmp(armor->regular.item, "mod:armor") == 0 && armor->powered.cells == 17);
    qa_app_unified_presentation_frame_free(&job.frame);
  }
  free(bytes);
  free_fixture(&f);
}
static void test_grapple_and_native_camera(void) {
  fixture f;
  build_fixture(&f);
  qa_app_sim_q3_grapple_cable cable = {f.id, origin, origin, 26, true, true,
      "models/grapple/flight.md3", "models/grapple/pull.md3", "models/grapple/hold.md3", 14};
  qa_qvm_grapple_view_anchor anchor = {"models/weapons2/shotgun/shotgun_hand.md3", "tag_weapon", {5, 0, -1}, 90, -0.2};
  f.models[0].replaces_body = true;
  f.models[0].render_owner = "source-client";
  f.models[0].model_anchor = &anchor;
  f.models[0].q3_grapple_cable = &cable;
  size_t length = 0;
  uint8_t *bytes = encode(&f.frame, &length);
  qa_caught_error caught;
  decode_job job;
  bool ok = try_decode(bytes, length, &caught, &job);
  CHECK(ok);
  if (ok) {
    const qa_app_sim_q3_grapple_cable *decoded = job.frame.models[0].q3_grapple_cable;
    CHECK(decoded && same_actor(decoded->owner, qa_identity_owner_actor(&client, 4, 2)));
    CHECK(decoded && !same_actor(decoded->owner, f.id));
    CHECK(decoded && decoded->segment_length == 14 && strcmp(decoded->pull, "models/grapple/pull.md3") == 0);
    CHECK(job.frame.models[0].replaces_body);
    CHECK(strcmp(job.frame.models[0].render_owner, "source-client") == 0);
    const qa_qvm_grapple_view_anchor *got = job.frame.models[0].model_anchor;
    CHECK(got && got->fov_offset_scale == -0.2 && got->fov_offset_above == 90);
    CHECK(got && strcmp(got->path, "models/weapons2/shotgun/shotgun_hand.md3") == 0 && strcmp(got->tag, "tag_weapon") == 0);
    CHECK(got && got->offset.x == 5 && got->offset.y == 0 && got->offset.z == -1);
    qa_app_unified_presentation_frame_free(&job.frame);
  }
  free(bytes);
  f.models[0].q3_grapple_cable = NULL;
  f.models[0].model_anchor = NULL;
  /* Native camera: owner, identity, prediction policy and edition-specific damage blend. */
  qa_save_value *identity = parse_json("{\"selection\":{\"product\":\"q2-test\",\"id\":\"camera\"},\"source\":"
      "{\"provider\":\"q2:camera\",\"content\":\"q2:rerelease:baseq2:1\"},\"declarationDigest\":"
      "\"sha256:0000000000000000000000000000000000000000000000000000000000000000\",\"modules\":[],\"providers\":[]}");
  qa_app_unified_native_camera_frame *camera = &f.frame.native_camera;
  f.frame.has_native_camera = true;
  camera->owner = (qa_presentation_owner){"mod:q2-test%2Fcamera", 8};
  camera->identity_wire = identity;
  camera->generation = 3;
  camera->view.view.origin = origin; camera->view.view.angles = origin; camera->view.view.view_height = 12;
  camera->view.view.has_kick_angles = true; camera->view.view.kick_angles = origin;
  camera->view.view.has_field_of_view = true; camera->view.view.field_of_view = 95;
  camera->view.view.has_blend = true; camera->view.view.blend = white;
  camera->view.view.has_damage_blend = true; camera->view.view.damage_blend = (qa_vec4){1, 0, 0, 0.5};
  camera->view.edition = "rerelease";
  camera->view.movement_origin = origin;
  camera->view.render_flags = 2;
  bytes = encode(&f.frame, &length);
  ok = try_decode(bytes, length, &caught, &job);
  CHECK(ok);
  if (ok) {
    const qa_app_unified_native_camera_frame *got = &job.frame.native_camera;
    CHECK(job.frame.has_native_camera && strcmp(got->owner.provider, "mod:q2-test%2Fcamera") == 0
        && got->owner.generation == 8 && got->generation == 3);
    CHECK(strcmp(got->identity.selection.product, "q2-test") == 0 && strcmp(got->identity.source.provider, "q2:camera") == 0);
    CHECK(strcmp(got->view.edition, "rerelease") == 0 && got->view.render_flags == 2
        && !got->view.position_prediction && !got->view.weapon_visible);
    CHECK(got->view.view.has_damage_blend && got->view.view.damage_blend.w == 0.5 && got->view.view.field_of_view == 95);
    qa_app_unified_presentation_frame_free(&job.frame);
  }
  free(bytes);
  camera->view.edition = "classic";
  bytes = encode(&f.frame, &length);
  CHECK(!try_decode(bytes, length, &caught, &job) && strstr(caught.message, "Classic camera"));
  free(bytes);
  camera->view.edition = "rerelease";
  /* Native component frames: hud stats and the viewer identity cross the frame. */
  f.frame.has_native_camera = false;
  double stats[64] = {0};
  stats[1] = 47;
  qa_app_unified_native_frame native;
  memset(&native, 0, sizeof native);
  native.owner = camera->owner; native.generation = 3; native.viewer = f.id;
  native.has_view = true; native.view = camera->view;
  native.has_hud = true; native.hud.stats = stats; native.hud.stat_count = 64;
  native.hud.server_frame = 31; native.hud.time_milliseconds = 775;
  f.frame.has_components = true;
  f.frame.components.revision = 1;
  f.frame.components.has_native = true;
  f.frame.components.native = &native;
  f.frame.components.native_count = 1;
  bytes = encode(&f.frame, &length);
  ok = try_decode(bytes, length, &caught, &job);
  CHECK(ok);
  if (ok) {
    CHECK(job.frame.has_components && job.frame.components.native_count == 1);
    const qa_app_unified_native_frame *got = &job.frame.components.native[0];
    CHECK(got->hud.stat_count == 64 && got->hud.stats[1] == 47 && got->hud.server_frame == 31 && got->hud.time_milliseconds == 775);
    CHECK(got->has_view && got->view.view.field_of_view == 95);
    CHECK(same_actor(got->viewer, qa_identity_owner_actor(&client, 4, 2)));
    qa_app_unified_presentation_frame_free(&job.frame);
  }
  free(bytes);
  native.hud.stat_count = 2;
  bytes = encode(&f.frame, &length);
  CHECK(!try_decode(bytes, length, &caught, &job) && strstr(caught.message, "stat count"));
  free(bytes);
  f.frame.has_components = false;
  memset(&f.frame.components, 0, sizeof f.frame.components);
  qa_save_value_free(identity);
  free_fixture(&f);
}
/* ---------- fixed-seed mutation ---------- */
static uint64_t rng_state = 0x9e3779b97f4a7c15ull;
static uint32_t rng(void) {
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
  return (uint32_t)(rng_state >> 11);
}
static const char *const tokens[] = {"null", "-1", "1e309", "\"\"", "[]", "{}", "true", "9007199254740993", "\"../x\"", "0.5"};
/* Truncates, flips a byte, or swaps a JSON value for a hostile token. */
static char *mutate_text(const char *text, size_t *out_length) {
  size_t length = strlen(text);
  char *out = malloc(length + 64);
  uint32_t mode = rng() % 3;
  if (mode == 0) {
    size_t cut = rng() % (length + 1);
    memcpy(out, text, cut);
    *out_length = cut;
  } else if (mode == 1) {
    memcpy(out, text, length);
    out[rng() % length] ^= (char)(1u << (rng() % 7));
    *out_length = length;
  } else {
    size_t at = rng() % length;
    while (at < length && text[at] != ':') at++;
    if (at >= length) { memcpy(out, text, length); *out_length = length; return out; }
    size_t end = at + 1;
    int depth = 0;
    bool string = false;
    for (; end < length; end++) {
      char ch = text[end];
      if (string) { if (ch == '\\') end++; else if (ch == '"') string = false; continue; }
      if (ch == '"') string = true;
      else if (ch == '{' || ch == '[') depth++;
      else if (ch == '}' || ch == ']') { if (depth == 0) break; depth--; }
      else if (ch == ',' && depth == 0) break;
    }
    const char *token = tokens[rng() % (sizeof tokens / sizeof *tokens)];
    size_t token_length = strlen(token);
    memcpy(out, text, at + 1);
    memcpy(out + at + 1, token, token_length);
    memcpy(out + at + 1 + token_length, text + end, length - end);
    *out_length = at + 1 + token_length + length - end;
  }
  return out;
}
static void test_frame_mutations(void) {
  int caught_count = 0, decoded = 0;
  for (int round = 0; round < 600; round++) {
    size_t mutated_length = 0;
    char *mutated = mutate_text(ts_frame_canonical, &mutated_length);
    size_t length = 0;
    uint8_t *bytes = deflate_bytes((const uint8_t *)mutated, mutated_length, &length);
    if (round % 5 == 4) { bytes[rng() % length] ^= 0x5a; length = length - rng() % (length / 2 + 1); }
    qa_caught_error caught;
    decode_job job;
    if (try_decode(bytes, length, &caught, &job)) {
      decoded++;
      qa_app_unified_presentation_frame_free(&job.frame);
    } else caught_count++;
    free(bytes);
    free(mutated);
  }
  CHECK(caught_count > 300);
  printf("frame mutations: %d raised, %d decoded\n", caught_count, decoded);
}
typedef struct prediction_job { const uint8_t *bytes; size_t length; qa_app_unified_prediction_projection out; } prediction_job;
static void prediction_body(void *ctx) {
  prediction_job *job = ctx;
  qa_app_unified_identity_decoder identity = identity_decoder();
  job->out = qa_app_unified_decode_prediction(job->bytes, job->length, &identity);
}
static void test_prediction(void) {
  fixture f;
  build_fixture(&f);
  qa_app_unified_prediction_projection *p = &f.frame.prediction;
  qa_spatial_actor collision;
  memset(&collision, 0, sizeof collision);
  qa_actor_id owner = qa_identity_owner_actor(&server, 6, 1);
  collision.body.actor = f.id;
  collision.body.state.ground = &f.ground;
  collision.body.link_count = 2;
  collision.collision.family = "q3";
  collision.collision.shape_kind = "model";
  collision.collision.shape_model = 3;
  collision.collision.owner = &owner;
  collision.collision.role = "solid";
  collision.collision.has_q3_owner = true;
  collision.collision.q3_entity_number = 5;
  p->collisions = &collision;
  p->collision_count = 1;
  p->snapshot.has_contact = true;
  p->snapshot.contact_ground.kind = QA_TRACE_HIT_ACTOR;
  p->snapshot.contact_ground.actor = f.id;
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_prediction(p, &length);
  prediction_job job = {.bytes = bytes, .length = length};
  qa_caught_error caught;
  bool ok = qa_try(prediction_body, &job, &caught);
  CHECK(ok);
  if (ok) {
    CHECK(strcmp(job.out.profile.id, "q2:movement") == 0 && job.out.snapshot.arsenal.ammo_count == 1
        && job.out.snapshot.arsenal.ammo[0].capacity == 200);
    CHECK(job.out.collision_count == 1 && job.out.collisions[0].collision.owner
        && same_actor(*job.out.collisions[0].collision.owner, qa_identity_owner_actor(&client, 6, 1)));
    CHECK(job.out.snapshot.has_contact && same_actor(job.out.snapshot.contact_ground.actor, qa_identity_owner_actor(&client, 4, 2)));
    qa_app_unified_prediction_projection_free(&job.out);
  }
  char *text = malloc(length + 1);
  memcpy(text, bytes, length);
  text[length] = '\0';
  int raised = 0;
  for (int round = 0; round < 400; round++) {
    size_t mutated_length = 0;
    char *mutated = mutate_text(text, &mutated_length);
    prediction_job m = {.bytes = (const uint8_t *)mutated, .length = mutated_length};
    if (qa_try(prediction_body, &m, &caught)) qa_app_unified_prediction_projection_free(&m.out);
    else raised++;
    free(mutated);
  }
  CHECK(raised > 200);
  /* A state that differs from its profile is rejected. */
  p->snapshot.state.kind = QA_MOVEMENT_STATE_Q2_RERELEASE;
  free(bytes);
  bytes = qa_app_unified_encode_prediction(p, &length);
  job = (prediction_job){.bytes = bytes, .length = length};
  CHECK(!qa_try(prediction_body, &job, &caught) && strstr(caught.message, "movement state differs from profile"));
  free(bytes);
  free(text);
  free_fixture(&f);
}
typedef struct reader_job { qa_save_value *tree; int kind; size_t count; void *out; qa_app_unified_component_frames frames; } reader_job;
static void reader_body(void *ctx) {
  reader_job *job = ctx;
  qa_save_reader reader;
  qa_save_reader_init(&reader, job->tree, "native");
  qa_app_unified_identity_decoder identity = identity_decoder();
  if (job->kind == 0) {
    qa_app_unified_native_state *states = NULL;
    job->count = qa_app_unified_read_native_states(&reader, &states);
    job->out = states;
  } else if (job->kind == 1) {
    qa_app_unified_native_frame *frames = NULL;
    job->count = qa_app_unified_read_native_frames(&reader, &identity, &frames);
    job->out = frames;
  } else {
    job->frames = qa_app_unified_read_component_frames(&reader, &identity);
  }
}
static const char native_states_wire[] =
    "[{\"owner\":{\"provider\":\"mod:hud\",\"generation\":2},\"identity\":{\"selection\":{\"product\":\"q2-test\",\"id\":\"hud\"},"
    "\"source\":{\"provider\":\"q2:hud\",\"content\":\"q2:rerelease:baseq2:1\"},\"declarationDigest\":"
    "\"sha256:0000000000000000000000000000000000000000000000000000000000000000\",\"modules\":[],\"providers\":[]},"
    "\"generation\":4,\"hud\":{\"mode\":\"layout-overlay\",\"frame\":{\"protocol\":{\"kind\":\"q2-classic\",\"version\":34},"
    "\"configstrings\":[{\"index\":5,\"value\":\"five\"},{\"index\":9,\"value\":\"nine\"}],\"layout\":\"xv 0\","
    "\"inventory\":[1,2,3],\"playerNumber\":1}}}]";
static const char native_frames_wire[] =
    "[{\"owner\":{\"provider\":\"mod:hud\",\"generation\":2},\"generation\":4,\"viewer\":{\"slot\":4,\"generation\":2},"
    "\"view\":{\"origin\":{\"x\":1,\"y\":2,\"z\":3},\"angles\":{\"x\":1,\"y\":2,\"z\":3},\"viewHeight\":22,"
    "\"native\":{\"edition\":\"classic\",\"movementOrigin\":{\"x\":1,\"y\":2,\"z\":3},\"renderFlags\":0,"
    "\"positionPrediction\":true,\"angularPrediction\":true,\"weaponVisible\":true}},"
    "\"hud\":{\"stats\":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31],"
    "\"serverFrame\":3,\"timeMilliseconds\":300,\"frameTimeMilliseconds\":100}}]";
static void reader_release(reader_job *job) {
  if (job->kind == 0) qa_app_unified_native_states_free(job->out, job->count);
  else if (job->kind == 1) qa_app_unified_native_frames_free(job->out, job->count);
  else qa_app_unified_component_frames_free(&job->frames);
}
static void test_native_readers(void) {
  const char *wires[3];
  wires[0] = native_states_wire;
  wires[1] = native_frames_wire;
  char *components = malloc(strlen(native_frames_wire) + 64);
  sprintf(components, "{\"revision\":1,\"sources\":[],\"native\":%s}", native_frames_wire);
  wires[2] = components;
  qa_caught_error caught;
  for (int kind = 0; kind < 3; kind++) {
    reader_job job;
    memset(&job, 0, sizeof job);
    job.kind = kind;
    job.tree = parse_json(wires[kind]);
    bool ok = qa_try(reader_body, &job, &caught);
    CHECK(ok);
    if (!ok) fprintf(stderr, "reader %d raised %s\n", kind, caught.message);
    if (ok && kind == 0) {
      const qa_app_unified_native_state *state = job.out;
      CHECK(job.count == 1 && state->hud.configstring_count == 2 && strcmp(state->hud.configstrings[1].value, "nine") == 0);
    }
    if (ok && kind == 1) {
      const qa_app_unified_native_frame *frame = job.out;
      CHECK(job.count == 1 && frame->hud.stat_count == 32 && frame->hud.frame_time_milliseconds == 100);
    }
    if (ok && kind == 2) CHECK(job.frames.has_native && job.frames.native_count == 1 && job.frames.revision == 1);
    if (ok) reader_release(&job);
    qa_save_value_free(job.tree);
    int raised = 0;
    for (int round = 0; round < 300; round++) {
      size_t mutated_length = 0;
      char *mutated = mutate_text(wires[kind], &mutated_length);
      qa_save_value *tree = qa_parse_json_value((const uint8_t *)mutated, mutated_length, true);
      free(mutated);
      if (!tree) { raised++; continue; }
      memset(&job, 0, sizeof job);
      job.kind = kind;
      job.tree = tree;
      if (qa_try(reader_body, &job, &caught)) reader_release(&job);
      else raised++;
      qa_save_value_free(tree);
    }
    CHECK(raised > 100);
  }
  /* Duplicate configstrings are found after every entry is read. */
  reader_job job;
  memset(&job, 0, sizeof job);
  job.tree = parse_json("[{\"owner\":{\"provider\":\"mod:hud\",\"generation\":2},\"identity\":{\"selection\":{\"product\":\"q2-test\","
      "\"id\":\"hud\"},\"source\":{\"provider\":\"q2:hud\",\"content\":\"q2:rerelease:baseq2:1\"},\"declarationDigest\":"
      "\"sha256:0000000000000000000000000000000000000000000000000000000000000000\",\"modules\":[],\"providers\":[]},"
      "\"generation\":4,\"hud\":{\"mode\":\"layout-overlay\",\"frame\":{\"protocol\":{\"kind\":\"q2-classic\",\"version\":34},"
      "\"configstrings\":[{\"index\":5,\"value\":\"a\"},{\"index\":5,\"value\":\"b\"}],\"layout\":\"\",\"inventory\":[],\"playerNumber\":0}}}]");
  CHECK(!qa_try(reader_body, &job, &caught) && strstr(caught.message, "duplicate native configstrings"));
  qa_save_value_free(job.tree);
  /* More native owners than the provider list can hold raise instead of overrunning it. */
  size_t big = 300 * 400 + 64;
  char *many = malloc(big);
  size_t used = (size_t)sprintf(many, "{\"revision\":1,\"sources\":[],\"native\":[");
  for (int i = 0; i < 300; i++)
    used += (size_t)sprintf(many + used, "%s{\"owner\":{\"provider\":\"mod:n%d\",\"generation\":1},\"generation\":0,"
        "\"viewer\":{\"slot\":1,\"generation\":0},\"view\":null,\"hud\":null}", i ? "," : "", i);
  sprintf(many + used, "]}");
  memset(&job, 0, sizeof job);
  job.kind = 2;
  job.tree = parse_json(many);
  CHECK(!qa_try(reader_body, &job, &caught) && strstr(caught.message, "invalid component owner set"));
  qa_save_value_free(job.tree);
  free(many);
  free(components);
}
/* ---------- content.test.ts ---------- */
typedef struct content_job { const qa_save_value *value; const uint8_t *bytes; size_t length; qa_app_unified_composition *out; } content_job;
static void read_body(void *ctx) { content_job *job = ctx; job->out = qa_app_unified_read_composition(job->value); }
static void decode_composition_body(void *ctx) {
  content_job *job = ctx;
  job->out = qa_app_unified_decode_composition(job->bytes, job->length);
}
static qa_save_value *decoded_tree(const char *text) {
  qa_save_value *value = qa_decode_checkpoint_value((const uint8_t *)text, strlen(text));
  if (!value) abort();
  return value;
}
static bool same_unordered(const qa_save_value *a, const qa_save_value *b) {
  if (a->kind != b->kind) return false;
  switch (a->kind) {
    case QA_SAVE_BOOLEAN: return a->boolean == b->boolean;
    case QA_SAVE_NUMBER: return a->number == b->number;
    case QA_SAVE_STRING: case QA_SAVE_BIGINT: return strcmp(a->text, b->text) == 0;
    case QA_SAVE_BYTES: return a->byte_length == b->byte_length && memcmp(a->bytes, b->bytes, a->byte_length) == 0;
    case QA_SAVE_ARRAY:
      if (a->item_count != b->item_count) return false;
      for (size_t i = 0; i < a->item_count; i++) if (!same_unordered(a->items[i], b->items[i])) return false;
      return true;
    case QA_SAVE_OBJECT:
      if (a->entry_count != b->entry_count) return false;
      for (size_t i = 0; i < a->entry_count; i++) {
        const qa_save_value *other = qa_save_value_get(b, a->entries[i].key);
        if (!other || !same_unordered(a->entries[i].value, other)) return false;
      }
      return true;
    default: return true;
  }
}
typedef struct id_job { qa_app_unified_resource_key key; char *out; } id_job;
static void id_body(void *ctx) { id_job *job = ctx; job->out = qa_app_unified_resource_id_from_key(&job->key); }
static void test_content(void) {
  qa_save_value *original = decoded_tree(ts_content_original), *relocated = decoded_tree(ts_content_relocated);
  qa_app_unified_composition *identity = qa_app_unified_create_composition(original, NULL, 0);
  qa_app_unified_composition *moved = qa_app_unified_create_composition(relocated, NULL, 0);
  CHECK(strcmp(identity->digest, ts_content_digest) == 0);
  CHECK(strcmp(moved->digest, identity->digest) == 0);
  qa_save_value *value = qa_app_unified_composition_value(identity);
  size_t length = 0;
  uint8_t *encoded = qa_app_unified_encode_composition(value, &length);
  /* Same value as the TS encoding; TS orders keys as readRecipe builds them, C keeps wire order. */
  qa_save_value *ts_encoded = decoded_tree(ts_content_encoded);
  qa_save_value *c_encoded = qa_decode_checkpoint_value(encoded, length);
  CHECK(c_encoded && same_unordered(c_encoded, ts_encoded));
  qa_save_value_free(c_encoded);
  qa_save_value_free(ts_encoded);
  CHECK(memmem(encoded, length, "/fixture", 8) == NULL && memmem(encoded, length, "/different", 10) == NULL);
  content_job job = {NULL, encoded, length, NULL};
  qa_caught_error caught;
  CHECK(qa_try(decode_composition_body, &job, &caught) && strcmp(job.out->digest, identity->digest) == 0);
  qa_app_unified_composition_destroy(job.out);
  /* Inventory provider participates in the digest. */
  qa_save_value *inventory = (qa_save_value *)qa_save_value_get(original, "inventory");
  qa_save_value *provider = (qa_save_value *)qa_save_value_get(inventory, "provider");
  char *saved = provider->text;
  provider->text = (char *)"q1:other";
  qa_app_unified_composition *other = qa_app_unified_create_composition(original, NULL, 0);
  provider->text = saved;
  CHECK(strcmp(other->digest, ts_content_other) == 0);
  qa_app_unified_composition_destroy(other);
  /* Resource ids ignore machine mounts. */
  const qa_save_value *geometry = qa_save_value_get(qa_save_value_get(original, "map"), "geometry");
  qa_app_unified_resource_key key = {"q1:classic:id1:fixture", "maps/start.bsp",
      qa_save_value_get(geometry, "digest")->text, 123};
  char *id = qa_app_unified_resource_id_from_key(&key);
  CHECK(strcmp(id, ts_content_resourceId) == 0);
  free(id);
  /* Malformed identities. */
  qa_save_value *bad = qa_app_unified_composition_value(identity);
  qa_save_value *digest = (qa_save_value *)qa_save_value_get(bad, "digest");
  saved = digest->text;
  digest->text = (char *)"sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
  job = (content_job){bad, NULL, 0, NULL};
  CHECK(!qa_try(read_body, &job, &caught));
  digest->text = saved;
  qa_save_value *composition = (qa_save_value *)qa_save_value_get(bad, "composition");
  for (size_t i = 0; i < composition->entry_count; i++)
    if (strcmp(composition->entries[i].key, "recipe") == 0) {
      qa_save_value_free(composition->entries[i].value);
      composition->entries[i].value = decoded_tree(ts_content_original);
    }
  CHECK(!qa_try(read_body, &job, &caught) && strstr(caught.message, "noncanonical"));
  qa_save_value_free(bad);
  const char *schemas[] = {"other:snapshot", "qts:snapshot-v6"};
  for (int i = 0; i < 2; i++) {
    bad = qa_app_unified_composition_value(identity);
    composition = (qa_save_value *)qa_save_value_get(bad, "composition");
    qa_save_value *schema = (qa_save_value *)qa_save_value_get(composition, "snapshotSchema");
    saved = schema->text;
    schema->text = (char *)schemas[i];
    job = (content_job){bad, NULL, 0, NULL};
    CHECK(!qa_try(read_body, &job, &caught));
    schema->text = saved;
    qa_save_value_free(bad);
  }
  id_job escape = {{"q1:classic:id1:fixture", "../escape", key.digest, 1}, NULL};
  CHECK(!qa_try(id_body, &escape, &caught));
  /* Sidecars: presence and bytes take part; repeats are rejected. */
  qa_app_unified_map_sidecar sidecars[2];
  memset(sidecars, 0, sizeof sidecars);
  sidecars[0].content = "q1:classic:id1:fixture";
  sidecars[0].path = "maps/start.ent";
  qa_app_unified_composition *missing = qa_app_unified_create_composition(original, sidecars, 1);
  CHECK(strcmp(missing->digest, ts_content_missing) == 0);
  sidecars[0].has_resource = true;
  sidecars[0].resource = (qa_app_unified_resource_key){"q1:classic:id1:fixture", "maps/start.ent",
      "sha256:1111111111111111111111111111111111111111111111111111111111111111", 10};
  qa_app_unified_composition *present = qa_app_unified_create_composition(original, sidecars, 1);
  CHECK(strcmp(present->digest, ts_content_present) == 0);
  qa_app_unified_composition *pair[2] = {missing, present};
  for (int i = 0; i < 2; i++) {
    qa_save_value *round = qa_app_unified_composition_value(pair[i]);
    uint8_t *round_bytes = qa_app_unified_encode_composition(round, &length);
    job = (content_job){NULL, round_bytes, length, NULL};
    CHECK(qa_try(decode_composition_body, &job, &caught) && strcmp(job.out->digest, pair[i]->digest) == 0
        && job.out->sidecar_count == 1 && job.out->sidecars[0].has_resource == (i == 1));
    if (job.out) qa_app_unified_composition_destroy(job.out);
    free(round_bytes);
    qa_save_value_free(round);
  }
  sidecars[0].has_resource = false;
  sidecars[1] = sidecars[0];
  typedef struct { const qa_save_value *recipe; const qa_app_unified_map_sidecar *sidecars; qa_app_unified_composition *out; } create_job;
  create_job repeat = {original, sidecars, NULL};
  void create_body(void *ctx);
  CHECK(!qa_try(create_body, &repeat, &caught) && strstr(caught.message, "Repeated"));
  qa_app_unified_composition_destroy(missing);
  qa_app_unified_composition_destroy(present);
  /* Mutated offers raise or decode; never crash or leak. */
  int raised = 0;
  for (int round = 0; round < 300; round++) {
    size_t mutated_length = 0;
    char *mutated = mutate_text(ts_content_encoded, &mutated_length);
    job = (content_job){NULL, (const uint8_t *)mutated, mutated_length, NULL};
    if (qa_try(decode_composition_body, &job, &caught)) qa_app_unified_composition_destroy(job.out);
    else raised++;
    free(mutated);
  }
  CHECK(raised > 200);
  free(encoded);
  qa_save_value_free(value);
  qa_app_unified_composition_destroy(identity);
  qa_app_unified_composition_destroy(moved);
  qa_save_value_free(original);
  qa_save_value_free(relocated);
}
void create_body(void *ctx) {
  struct { const qa_save_value *recipe; const qa_app_unified_map_sidecar *sidecars; qa_app_unified_composition *out; } *job = ctx;
  job->out = qa_app_unified_create_composition(job->recipe, job->sidecars, 2);
}
/* ---------- unified-events.test.ts owns() ---------- */
static void test_event_owns(void) {
  qa_save_value *wire = parse_json("{\"sequence\":3,\"time\":{\"kind\":\"milliseconds\",\"value\":1500},\"audience\":{\"kind\":\"client\","
      "\"client\":{\"slot\":1,\"generation\":2}},\"payload\":{\"kind\":\"sound\",\"resource\":\"resource:unified:"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"actor\":{\"slot\":3,\"generation\":7},"
      "\"origin\":{\"x\":1,\"y\":2,\"z\":3},\"channel\":1,\"volume\":0.7,\"attenuation\":1}}");
  qa_app_unified_identity_decoder identity = identity_decoder();
  qa_save_value *sound = qa_app_unified_read_simulation_event(wire, &identity);
  qa_client_id id;
  CHECK(qa_app_unified_event_client_id(qa_save_value_get(qa_save_value_get(sound, "audience"), "client"), &id));
  CHECK(qa_identity_owner_owns_client(&client, id));
  CHECK(!qa_identity_owner_owns_client(&server, id));
  qa_actor_id actor;
  CHECK(qa_app_unified_event_actor_id(qa_save_value_get(qa_save_value_get(sound, "payload"), "actor"), &actor));
  CHECK(qa_identity_owner_owns_actor(&client, actor) && !qa_identity_owner_owns_actor(&server, actor));
  CHECK(same_actor(actor, qa_identity_owner_actor(&client, 3, 7)));
  qa_save_value_free(sound);
  qa_save_value_free(wire);
}
/* ---------- remote replica receive paths ---------- */
static double next_generation(void *ctx, double slot) { CHECK(ctx == &ctx_sentinel); (void)slot; return 0; }
static qa_actor_id remote_actor(void *ctx, double slot, double generation) {
  CHECK(ctx == &ctx_sentinel);
  return qa_identity_owner_actor(&client, (int64_t)slot, (int64_t)generation);
}
static qa_client_id remote_client(void *ctx, double slot, double generation) {
  CHECK(ctx == &ctx_sentinel);
  return qa_identity_owner_client(&client, (int64_t)slot, (int64_t)generation);
}
typedef struct remote_job { qa_app_unified_remote *remote; const uint8_t *payload; size_t payload_length;
  const uint8_t *simulation; size_t simulation_length; bool frame; double acknowledged; } remote_job;
static void remote_body(void *ctx) {
  remote_job *job = ctx;
  if (job->frame) qa_app_unified_remote_receive_frame(job->remote, job->payload, job->payload_length, &job->acknowledged);
  else qa_app_unified_remote_receive_events(job->remote, 0, 1, job->payload, job->payload_length,
      job->simulation, job->simulation_length);
}
static void test_remote_receive(void) {
  qa_app_unified_remote_options options;
  memset(&options, 0, sizeof options);
  options.ctx = &ctx_sentinel;
  options.session = client.session;
  options.next_generation = next_generation;
  options.identity_actor = remote_actor;
  options.identity_client = remote_client;
  qa_app_unified_remote *remote = qa_app_unified_remote_create(options);
  CHECK(remote != NULL);
  const char *presentation = "[]";
  const char *simulation = "[{\"sequence\":3,\"time\":{\"kind\":\"seconds\",\"value\":1},\"audience\":{\"kind\":\"client\",\"client\":{\"slot\":70000,\"generation\":1}},"
      "\"payload\":{\"kind\":\"sound\",\"resource\":\"resource:unified:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
      "\"actor\":{\"slot\":70000,\"generation\":2},\"origin\":{\"x\":1,\"y\":2,\"z\":3},\"channel\":1,\"volume\":1,\"attenuation\":0.5}}]";
  remote_job job = {remote, (const uint8_t *)presentation, 2, (const uint8_t *)simulation, strlen(simulation), false, 0};
  qa_caught_error caught;
  /* A slot beyond the identity range raises (TS RangeError) instead of growing the ledger. */
  bool remote_ok = qa_try(remote_body, &job, &caught);
  CHECK(!remote_ok && strcmp(caught.name, "RangeError") == 0 && strstr(caught.message, "Unified client slot is out of range"));
  if (remote_ok || strcmp(caught.name, "RangeError") != 0) fprintf(stderr, "remote raised %s: %s\n", caught.name, caught.message);
  int raised = 0;
  for (int round = 0; round < 300; round++) {
    size_t mutated_length = 0;
    char *mutated = mutate_text(simulation, &mutated_length);
    job = (remote_job){remote, (const uint8_t *)presentation, 2, (const uint8_t *)mutated, mutated_length, false, 0};
    if (!qa_try(remote_body, &job, &caught)) raised++;
    free(mutated);
  }
  CHECK(raised > 100);
  /* Frames before admission read only the envelope; malformed envelopes raise without leaking. */
  fixture f;
  build_fixture(&f);
  size_t length = 0;
  uint8_t *bytes = encode(&f.frame, &length);
  job = (remote_job){remote, bytes, length, NULL, 0, true, 0};
  CHECK(qa_try(remote_body, &job, &caught));
  bytes[length / 2] ^= 0x33;
  job.payload_length = length - 3;
  CHECK(!qa_try(remote_body, &job, &caught));
  free(bytes);
  free_fixture(&f);
  qa_app_unified_remote_destroy(remote);
}
int main(void) {
  qa_identity_owner_create(&server, "server");
  qa_identity_owner_create(&client, "client");
  init_resources();
  test_frame_roundtrip();
  test_grapple_and_native_camera();
  test_frame_mutations();
  test_prediction();
  test_native_readers();
  test_content();
  test_event_owns();
  test_remote_receive();
  qa_identity_owner_destroy(&server);
  qa_identity_owner_destroy(&client);
  printf("%d/%d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
