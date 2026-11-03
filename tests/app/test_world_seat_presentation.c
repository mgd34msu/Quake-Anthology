/* C checks for src/app/bootstrap/presentation-scene.ts (ApplicationWorldScene,
 * seatModelVisible), held-weapon.ts (ForeignHeldWeapons),
 * native-held-weapon.ts (NativeHeldWeapons) and q3-selected-weapon.ts
 * (SelectedQ3WeaponPresenter) over retail content (QTS_TEST_CORPUS or
 * ~/qfiles). Scene operations and held passes are compared line by line with
 * the TS modules under bun (tests/app/oracles/world_scene_oracle.ts ->
 * world_scene.txt). TS tests reproduced: console/q1-client-settings
 * (seatModelVisible), first-playable/held-weapon (authored declarations,
 * none, digest and unknown-model errors; native attachment), and the
 * first-playable/rerelease-presentation Q1 actor opacity case. Every
 * callback checks its sentinel ctx. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/held_weapon.h"
#include "qa/app/bootstrap/native_held_weapon.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/presentation.h"
#include "qa/app/bootstrap/presentation_scene.h"
#include "qa/settings/config.h"
#include "qa/text/atlas.h"
#include "qa/ui/common/skin.h"
#include "qa/world/collision/index.h"
#include "qa/world/session/session.h"
#include "qa/app/bootstrap/q3_selected_weapon.h"
#include "qa/content/q1/foundation/held_weapons.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q2/foundation/weapon_attachments.h"
#include "qa/content/q3/foundation/held_weapons.h"
#include "qa/core/math.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/render/scene/models/attachment.h"
#include "qa/render/scene/models/grip.h"
#include "qa/render/scene/models/transform.h"
#include "qa/render/scene/view.h"

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- output compared with the TS oracle ---- */
static char **lines; static size_t line_count, line_capacity;
static void out(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  if (line_count == line_capacity) {
    line_capacity = line_capacity == 0 ? 64 : line_capacity * 2;
    lines = realloc(lines, line_capacity * sizeof *lines);
  }
  lines[line_count++] = strdup(buffer);
}
static const char *fx(double value, char *buffer) {
  qa_js_to_fixed(value == 0 ? 0 : value, 3, buffer, 160);
  return buffer;
}
#define F(value) fx((value), (char[160]){0})

static void dump_batch(const char *prefix, const qa_draw_batch *batch) {
  double p = 0, c = 0, t = 0;
  for (size_t i = 0; i < batch->vertex_count; i++) {
    const qa_render_vertex *v = &batch->vertices[i];
    p += v->position.x + v->position.y + v->position.z + v->position.w;
    c += v->color.x + v->color.y + v->color.z + v->color.w;
    t += v->tex_coord.x + v->tex_coord.y;
  }
  size_t count = batch->texturing_is_pair ? batch->pair_vertex_count : batch->vertex_count;
  out("%s v=%zu i=%zu p=%s c=%s t=%s depthWrite=%s pair=%s", prefix, count, batch->indices.count, F(p), F(c), F(t),
      batch->state.depth_write ? "true" : "false", batch->texturing_is_pair ? "true" : "false");
}
static const char *render_kind(qa_render_operation_kind kind) {
  switch (kind) {
    case QA_RENDER_OP_DRAW: return "draw";
    case QA_RENDER_OP_OBJECT_OPACITY: return "object-opacity";
    default: return "other";
  }
}
static void dump_render(const char *label, size_t index, size_t r, const qa_render_operation *render) {
  char opacity[200] = "";
  if (render->kind == QA_RENDER_OP_OBJECT_OPACITY) snprintf(opacity, sizeof opacity, " opacity=%s", F(render->opacity));
  out("%s %zu.%zu kind=%s%s batches=%zu", label, index, r, render_kind(render->kind), opacity, render->batch_count);
  for (size_t b = 0; b < render->batch_count; b++) {
    char prefix[256];
    snprintf(prefix, sizeof prefix, "%s %zu.%zu.%zu", label, index, r, b);
    dump_batch(prefix, &render->batches[b]);
  }
}
static void dump_operations(const char *label, const qa_scene_operation *operations, size_t count) {
  out("%s count=%zu", label, count);
  for (size_t i = 0; i < count; i++) {
    const qa_scene_operation *operation = &operations[i];
    if (!operation->is_group) { out("%s %zu op=%s", label, i, render_kind(operation->operation.kind)); continue; }
    const qa_scene_group_order *order = &operation->group.order;
    char key[256];
    if (order->kind == QA_GROUP_SEQUENCE) snprintf(key, sizeof key, "sequence/%s", qa_sequence_phase_name(order->phase));
    else if (order->kind == QA_GROUP_SOURCE)
      snprintf(key, sizeof key, "source/%s:%s:%s", order->source.entity.is_refentity ? "refentity" : "world",
               QA_JS_NUMBER(order->source.surface), QA_JS_NUMBER(order->source.fog));
    else snprintf(key, sizeof key, "compiled/compiled");
    out("%s %zu group=%s ops=%zu", label, i, key, operation->group.operation_count);
    for (size_t r = 0; r < operation->group.operation_count; r++) dump_render(label, i, r, &operation->group.operations[r]);
  }
}

/* ---- sentinel callbacks ---- */
typedef struct warn_state { int sentinel; int count; char last[512]; } warn_state;
static warn_state warn_holder;
static void assets_warn(void *ctx, const char *text) {
  warn_state *warn = ctx;
  CHECK(warn == &warn_holder && warn->sentinel == 0x5a);
  warn->count++;
  snprintf(warn->last, sizeof warn->last, "%s", text);
}
typedef struct planar_state { int sentinel; int calls; } planar_state;
static planar_state planar_holder;
static bool planar(void *ctx) {
  planar_state *state = ctx;
  CHECK(state == &planar_holder && state->sentinel == 0x51);
  state->calls++;
  return false;
}
/* HeldAssets over ApplicationAssets with a sentinel ctx. */
typedef struct held_ctx { int sentinel; qa_app_application_assets *assets; int mounts, models; } held_ctx;
static qa_content_mounted *held_mounts(void *ctx, qa_content_id content) {
  held_ctx *held = ctx;
  CHECK(held->sentinel == 0x77);
  held->mounts++;
  return qa_app_application_assets_provider(held->assets, content).mounts;
}
static qa_app_model_asset held_model(void *ctx, qa_content_id content, const char *path) {
  held_ctx *held = ctx;
  CHECK(held->sentinel == 0x77);
  held->models++;
  return qa_app_application_assets_model(held->assets, content, path);
}
typedef struct policy_ctx { int sentinel; int reads; qa_app_application_assets *assets; } policy_ctx;
static qa_model_replacement_policy native_policy(void *ctx) {
  policy_ctx *policy = ctx;
  CHECK(policy->sentinel == 0x33);
  policy->reads++;
  return qa_default_model_replacement_policy;
}
static qa_content_mounted *native_mounts(void *ctx, qa_content_id content) {
  policy_ctx *policy = ctx;
  CHECK(policy->sentinel == 0x33);
  return qa_app_application_assets_provider(policy->assets, content).mounts;
}

/* ---- content ---- */
static qa_app_application_command loaded_command;
static qa_app_loaded_content *load_content(const char *root, const char *game, const char *map) {
  const char *argv[] = {"--game", game, "--map", map, "--dedicated", "--content-root", root};
  qa_content_root_search search = {NULL, NULL};
  loaded_command = qa_app_parse_application_command(argv, 7, search, NULL);
  if (strcmp(loaded_command.kind, "run") != 0) return NULL;
  qa_app_content_host host;
  memset(&host, 0, sizeof host);
  qa_app_loaded_content *content = qa_app_load_application_content_native(host, loaded_command.options, NULL, false, NULL, NULL, NULL);
  memset(&loaded_command.options, 0, sizeof loaded_command.options);
  return content;
}
static qa_app_application_assets *open_assets(qa_app_loaded_content *content, qa_identity_owner *ids, int *owner_symbol) {
  qa_renderer_resource_owner owner = {owner_symbol, ids->session, 0};
  qa_app_application_assets_options options;
  memset(&options, 0, sizeof options);
  options.warn = assets_warn; options.warn_ctx = &warn_holder;
  return qa_app_application_assets_create(content, owner, NULL, &options);
}
static qa_app_simulation_presentation presentation_base(qa_content_id content, qa_game_family family) {
  qa_app_simulation_presentation value;
  memset(&value, 0, sizeof value);
  value.content = content; value.family = family;
  value.angles = (qa_vec3){0, 30, 0}; value.scale = 1; value.visible = true;
  return value;
}
static qa_scene_camera camera_at(qa_vec3 origin, qa_vec3 angles) {
  qa_scene_camera camera;
  memset(&camera, 0, sizeof camera);
  camera.origin = origin; camera.axis = qa_angles_to_axis(angles);
  camera.projection = qa_scene_perspective_projection(90, 73.74, 16384, 4);
  camera.viewport = (qa_rect){0, 0, 640, 480};
  return camera;
}

typedef struct raise_call { void (*fn)(void *ctx); void *ctx; } raise_call;
static bool raises(void (*fn)(void *), void *ctx, const char *message) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) return false;
  if (strstr(caught.message, message) == NULL) { fprintf(stderr, "raised: %s\n", caught.message); return false; }
  return true;
}
typedef struct frame_call { qa_app_foreign_held_weapons *weapons; qa_app_simulation_presentation source; } frame_call;
static void frame_body(void *ctx) {
  frame_call *call = ctx;
  qa_q3_character_pass pass;
  qa_app_held_character character = { {0, 0, 0}, {1, 1, 1, 1}, false, 0 };
  (void)qa_app_foreign_held_weapons_frame(call->weapons, &call->source, character, &pass);
}

static void test_seat_model_visible(void) {
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "visible"));
  qa_actor_id actor = qa_identity_owner_actor(&ids, 0, 0), other = qa_identity_owner_actor(&ids, 1, 0);
  CHECK(qa_app_seat_model_visible(&actor, actor, true));
  CHECK(!qa_app_seat_model_visible(&actor, actor, false));
  CHECK(!qa_app_seat_model_visible(&actor, other, true));
  CHECK(qa_app_seat_model_visible(&actor, other, false));
  CHECK(!qa_app_seat_model_visible(NULL, actor, true));
  CHECK(qa_app_seat_model_visible(NULL, actor, false));
  CHECK(qa_app_seat_model_visible(&other, other, true));
}

static void test_q1(const char *root) {
  qa_app_loaded_content *content = load_content(root, "q1-classic-id1", "e1m1");
  CHECK(content != NULL);
  if (content == NULL) return;
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "world-scene"));
  int owner_symbol = 0;
  qa_app_application_assets *assets = open_assets(content, &ids, &owner_symbol);
  qa_app_application_assets_load_world(assets);
  planar_holder.sentinel = 0x51;
  qa_app_application_world_scene *scene = qa_app_application_world_scene_create(assets, NULL, planar, &planar_holder);
  CHECK(scene != NULL && qa_app_application_world_scene_assets(scene) == assets);
  qa_actor_id viewer = qa_identity_owner_actor(&ids, 0, 0);
  qa_content_id entities = qa_app_loaded_content_recipe(content)->map.entities.content;
  qa_app_simulation_presentation p[6];
  for (size_t i = 0; i < 6; i++) p[i] = presentation_base(entities, QA_GAME_FAMILY_Q1);
  p[0].actor = qa_identity_owner_actor(&ids, 1, 0); p[0].path = "progs/soldier.mdl"; p[0].origin = (qa_vec3){480, -352, 88};
  p[0].has_alpha = true; p[0].alpha = (float)0.371337; p[0].frame = 3; p[0].old_frame = 2; p[0].has_back_lerp = true; p[0].back_lerp = 0.25;
  p[1].actor = qa_identity_owner_actor(&ids, 2, 0); p[1].path = "progs/flame.mdl"; p[1].origin = (qa_vec3){400, -300, 80}; p[1].scale = (float)1.31337;
  p[2].actor = qa_identity_owner_actor(&ids, 3, 0); p[2].path = "*1"; p[2].has_alpha = true; p[2].alpha = 0.5;
  p[3].actor = qa_identity_owner_actor(&ids, 4, 0); p[3].path = "progs/s_explod.spr"; p[3].origin = (qa_vec3){420, -330, 90}; p[3].frame = 2; p[3].old_frame = 2;
  p[4].actor = viewer; p[4].path = "progs/v_shot.mdl"; p[4].origin = (qa_vec3){480, -400, 110};
  p[4].has_view_origin = true; p[4].view_origin = p[4].origin; p[4].view_weapon = true;
  p[5].actor = qa_identity_owner_actor(&ids, 5, 0); p[5].path = "progs/player.mdl"; p[5].origin = (qa_vec3){500, -380, 88};
  p[5].frame = 12; p[5].old_frame = 11; p[5].has_back_lerp = true; p[5].back_lerp = 0.5;
  qa_q1_event styles[2];
  memset(styles, 0, sizeof styles);
  styles[0].kind = "lightstyle"; styles[0].style = 0; styles[0].pattern = "abcdefghijklmnopqrstuvwxyz";
  styles[1].kind = "lightstyle"; styles[1].style = 5; styles[1].pattern = "mmnmmommommnonmmonqnmmo";
  qa_app_sim_presentation_event events[2];
  memset(events, 0, sizeof events);
  for (size_t i = 0; i < 2; i++) { events[i].kind = "q1"; events[i].q1 = &styles[i]; events[i].content = entities; events[i].sequence = (double)i + 1; }
  qa_app_application_world_scene_receive(scene, events, 2);
  qa_world_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.frame.frame = 1; snapshot.frame.time.kind = QA_SOURCE_TIME_SECONDS; snapshot.frame.time.value = 1.25;
  qa_app_application_world_scene_prepare(scene, &viewer, &snapshot, p, 6, NULL, 0, NULL);
  qa_app_world_scene_styles style_rows = qa_app_application_world_scene_styles(scene);
  out("q1.styles %s %s %s %s %s %s %s style0=%s", F(style_rows.q1_styles[0]), F(style_rows.q1_styles[1]), F(style_rows.q1_styles[5]),
      F(style_rows.q1_styles[63]), F(style_rows.q2_styles[0].white), F(style_rows.q2_styles[5].white), F(style_rows.q2_styles[7].white),
      F(qa_app_application_world_scene_style(scene, 0, 12)));
  qa_world_view_input input;
  memset(&input, 0, sizeof input);
  input.camera = camera_at((qa_vec3){360, -352, 100}, (qa_vec3){0, 0, 0});
  input.target_is_seat = true; input.target_seat = qa_identity_owner_seat(&ids, 0);
  input.time.kind = QA_SOURCE_TIME_SECONDS; input.time.value = 1.25;
  size_t count = 0;
  const qa_scene_operation *operations = qa_app_application_world_scene_supplemental(scene, &input, NULL, false, &count);
  dump_operations("q1.supplemental", operations, count);
  CHECK(planar_holder.calls > 0);
  qa_prepared_world_view prepared;
  qa_app_application_world_scene_view(scene, &input, NULL, 0, NULL, 0, false, NULL, &prepared);
  size_t object_count = 0;
  for (size_t i = 0; i < prepared.view.operation_count; i++) if (prepared.view.operations[i].kind == QA_RENDER_OP_OBJECT_OPACITY) object_count++;
  out("q1.view.objects count=%zu", object_count);
  size_t index = 0;
  for (size_t i = 0; i < prepared.view.operation_count; i++) {
    if (prepared.view.operations[i].kind != QA_RENDER_OP_OBJECT_OPACITY) continue;
    out("q1.view.objects %zu group=sequence/translucent ops=1", index);
    dump_render("q1.view.objects", index, 0, &prepared.view.operations[i]);
    index++;
  }
  qa_prepared_world_view_destroy(&prepared);
  /* ForeignHeldWeapons over the HeldAssets view with a sentinel ctx. */
  held_ctx held = { 0x77, assets, 0, 0 };
  qa_app_held_assets held_assets = { &held, held_mounts, held_model };
  qa_app_foreign_held_weapons *weapons = qa_app_foreign_held_weapons_create(held_assets);
  qa_app_simulation_presentation gun = presentation_base(entities, QA_GAME_FAMILY_Q1);
  gun.actor = viewer; gun.path = "progs/v_shot.mdl"; gun.view_weapon = true;
  qa_app_held_character character = { {1, 2, 3}, {1, 1, 1, 1}, false, 0 };
  qa_q3_character_pass pass;
  CHECK(qa_app_foreign_held_weapons_declaration(weapons, &gun) == NULL);
  size_t pass_count = qa_app_foreign_held_weapons_frame(weapons, &gun, character, &pass);
  CHECK(pass_count == 1 && held.mounts > 0 && held.models == 1);
  if (pass_count == 1) {
    const qa_scene_entity *entity = &pass.entity;
    const qa_model_transform *t = &entity->transform;
    out("q1.held path=%s kind=q1-mdl triangles=%zu frame=%s opacity=%s", entity->resource.requested_path,
        entity->model.as.q1_alias->triangle_count, QA_JS_NUMBER(entity->pose.frame), QA_JS_NUMBER(entity->opacity));
    out("q1.held transform %s %s %s %s %s %s %s %s %s %s %s %s", F(t->origin.x), F(t->origin.y), F(t->origin.z),
        F(t->axis.vectors[0].x), F(t->axis.vectors[0].y), F(t->axis.vectors[0].z), F(t->axis.vectors[1].x), F(t->axis.vectors[1].y),
        F(t->axis.vectors[1].z), F(t->axis.vectors[2].x), F(t->axis.vectors[2].y), F(t->axis.vectors[2].z));
    CHECK(entity->actor == &gun.actor && pass.options != NULL && pass.content == entities);
    CHECK(entity->lighting_origin.x == 1 && entity->lighting_origin.z == 3 && strcmp(entity->flags_kind, "q1") == 0);
    /* The cached subset is reused. */
    qa_q3_character_pass again;
    CHECK(qa_app_foreign_held_weapons_frame(weapons, &gun, character, &again) == 1 && again.entity.model.as.q1_alias == entity->model.as.q1_alias);
    CHECK(held.models == 1);
  }
  /* An explicit "none" declaration holds nothing; an unknown model has no declaration. */
  qa_held_weapon_declaration none = { false, {0} };
  qa_app_simulation_presentation hidden = gun;
  hidden.path = ""; hidden.held_weapon = &none;
  CHECK(qa_app_foreign_held_weapons_declaration(weapons, &hidden) == &none);
  CHECK(qa_app_foreign_held_weapons_frame(weapons, &hidden, character, &pass) == 0);
  frame_call unknown = { weapons, gun };
  unknown.source.path = "progs/unknown.mdl";
  CHECK(raises(frame_body, &unknown, "has no authored held model declaration"));
  frame_call digest = { weapons, gun };
  qa_held_weapon_declaration wrong = { true, {0} };
  wrong.model = *qa_q1_held_weapon("progs/v_shot.mdl");
  wrong.model.has_digest = true; wrong.model.digest = "sha256:0000000000000000000000000000000000000000000000000000000000000000";
  digest.source.held_weapon = &wrong;
  CHECK(raises(frame_body, &digest, "digest"));
  qa_app_foreign_held_weapons_destroy(weapons);
  qa_app_application_world_scene_destroy(scene);
  qa_app_application_assets_destroy(assets);
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
  qa_identity_owner_destroy(&ids);
}

static void test_q2_native(const char *root) {
  qa_app_loaded_content *content = load_content(root, "q2-rerelease-baseq2", "base1");
  CHECK(content != NULL);
  if (content == NULL) return;
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "native-held"));
  int owner_symbol = 0;
  qa_app_application_assets *assets = open_assets(content, &ids, &owner_symbol);
  qa_app_application_assets_load_world(assets);
  qa_content_id contents = qa_app_loaded_content_recipe(content)->presentation.assets;
  const char *carriers[] = {"players/male/w_blaster.md2", "players/female/w_blaster.md2"};
  policy_ctx policy = { 0x33, 0, assets };
  qa_app_native_held_assets native_assets = { &policy, native_policy, native_mounts };
  qa_app_native_held_weapons *weapons = qa_app_native_held_weapons_create(native_assets);
  for (size_t c = 0; c < 2; c++) {
    qa_app_model_asset asset = qa_app_application_assets_model(assets, contents, carriers[c]);
    const qa_model_attachment_definition *definition = qa_q2_weapon_attachment(asset.resource.digest);
    CHECK(definition != NULL && !definition->is_joint);
    if (definition == NULL) continue;
    qa_scene_entity reference;
    memset(&reference, 0, sizeof reference);
    reference.resource = asset.resource; reference.model = asset.model;
    reference.transform.axis = qa_angles_to_axis((qa_vec3){0, 0, 0}); reference.transform.scale = (qa_vec3){1, 1, 1};
    reference.pose.is_frame = true; reference.color = (qa_vec4){1, 1, 1, 1};
    reference.has_opacity = true; reference.opacity = 0.7;
    reference.flags_kind = "q2"; reference.flags_bits = 1024; reference.lighting_origin = (qa_vec3){10, 20, 30}; reference.shadow_plane = 4;
    qa_scene_entity posed = reference;
    posed.pose.frame = 42; posed.pose.previous_frame = 41; posed.pose.back_lerp = 0.5; posed.previous_origin = (qa_vec3){-8, 2, 0};
    qa_model_grip grip;
    CHECK(qa_create_model_grip(&reference, *definition, &grip));
    qa_model_transform sampled;
    CHECK(qa_model_grip_transform(&grip, &posed, &sampled));
    qa_app_native_held_attachment attachment;
    CHECK(qa_app_native_held_weapons_attachment(weapons, contents, &posed, &attachment));
    qa_scene_entity child = reference;
    child.transform = qa_align_model_attachment(definition->grip, qa_q3_weapon_hand_grip);
    child.pose.frame = 0; child.pose.previous_frame = 0; child.pose.back_lerp = 0;
    qa_app_resolved_held_entity *resolved = qa_app_native_held_attachment_bind(&attachment, &child);
    qa_scene_entity far;
    CHECK(qa_app_resolved_held_entity_resolve(resolved, (qa_vec3){3000, 0, 0}, "view", &far));
    qa_vec3 point = qa_model_world_point(far.transform, definition->grip.origin);
    CHECK(fabs(point.x - sampled.origin.x) < 1e-4 && fabs(point.y - sampled.origin.y) < 1e-4 && fabs(point.z - sampled.origin.z) < 1e-4);
    CHECK(far.flags_bits == 1024 && strcmp(far.flags_kind, "q2") == 0 && far.opacity == 0.7 && far.shadow_plane == 4);
    CHECK(strcmp(far.resource.id, child.resource.id) == 0);
    CHECK(far.previous_origin.x == far.transform.origin.x && far.previous_origin.z == far.transform.origin.z);
    CHECK(policy.reads > 0);
    qa_scene_entity near;
    CHECK(qa_app_resolved_held_entity_resolve(resolved, (qa_vec3){0, 0, 0}, "view", &near));
    qa_scene_entity replacement;
    if (qa_replacement_entity(&posed, &replacement)) {
      qa_scene_entity shadow;
      CHECK(qa_app_resolved_held_entity_resolve(resolved, (qa_vec3){3000, 0, 0}, "shadow", &shadow));
      CHECK(shadow.transform.origin.x == near.transform.origin.x && shadow.transform.origin.y == near.transform.origin.y);
    } else CHECK(near.transform.origin.x == far.transform.origin.x);
    qa_app_resolved_held_entity_destroy(resolved);
    qa_model_grip_destroy(&grip);
  }
  qa_app_native_held_weapons_destroy(weapons);
  qa_app_application_assets_destroy(assets);
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
  qa_identity_owner_destroy(&ids);
}

static void test_q3(const char *root) {
  qa_app_loaded_content *content = load_content(root, "q3-baseq3", "q3dm1");
  CHECK(content != NULL);
  if (content == NULL) return;
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "world-scene-q3"));
  int owner_symbol = 0;
  qa_app_application_assets *assets = open_assets(content, &ids, &owner_symbol);
  qa_app_application_assets_load_world(assets);
  planar_holder.sentinel = 0x51;
  qa_app_application_world_scene *scene = qa_app_application_world_scene_create(assets, NULL, planar, &planar_holder);
  qa_actor_id viewer = qa_identity_owner_actor(&ids, 0, 0);
  qa_app_simulation_presentation weapon = presentation_base(qa_app_loaded_content_recipe(content)->presentation.assets, QA_GAME_FAMILY_Q3);
  weapon.actor = viewer; weapon.path = "models/weapons2/shotgun/shotgun.md3";
  weapon.origin = (qa_vec3){100, 200, 60}; weapon.angles = (qa_vec3){5, 40, 0}; weapon.view_weapon = true;
  weapon.has_q3_weapon = true; weapon.q3_weapon_time_ms = 1250; weapon.q3_weapon_torso_animation = 11;
  weapon.has_q3_weapon_last_fire = true; weapon.q3_weapon_last_fire_ms = 1240; weapon.q3_weapon_firing = false;
  weapon.q3_weapon_horizontal_speed = 120; weapon.q3_weapon_bob_cycle = 37; weapon.q3_weapon_weapon = 3;
  qa_world_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.frame.frame = 1; snapshot.frame.time.kind = QA_SOURCE_TIME_MILLISECONDS; snapshot.frame.time.value = 1250;
  qa_app_world_scene_prepare_options options = qa_app_world_scene_prepare_defaults();
  options.field_of_view = 90;
  qa_app_application_world_scene_prepare(scene, &viewer, &snapshot, &weapon, 1, NULL, 0, &options);
  qa_world_view_input input;
  memset(&input, 0, sizeof input);
  input.camera = camera_at((qa_vec3){100, 200, 86}, (qa_vec3){5, 40, 0});
  input.target_is_seat = true; input.target_seat = qa_identity_owner_seat(&ids, 0);
  input.time.kind = QA_SOURCE_TIME_MILLISECONDS; input.time.value = 1250;
  size_t count = 0;
  const qa_scene_operation *operations = qa_app_application_world_scene_supplemental(scene, &input, NULL, false, &count);
  dump_operations("q3.supplemental", operations, count);
  /* The world form of the same presenter: invisible powerup replaces the gun pass. */
  qa_app_selected_q3_weapon_presenter *presenter = qa_app_selected_q3_weapon_presenter_create(assets, NULL);
  size_t pass_count = 0;
  const qa_q3_character_pass *passes = qa_app_selected_q3_weapon_presenter_world(presenter, &weapon, (qa_vec3){1, 2, 3}, 1 << 4, true, &pass_count);
  /* The flash fired 10 ms ago (<= 20): its pass follows the gun pass. */
  CHECK(pass_count == 2 && passes[0].shader != NULL && strcmp(passes[0].shader, "powerups/invisibility") == 0 && passes[1].shader == NULL);
  if (pass_count == 2) {
    qa_model_source_options value = passes[0].options(passes[0].options_ctx, &passes[0].entity);
    CHECK(value.custom_shader != NULL && strcmp(value.custom_shader, "powerups/invisibility") == 0);
    CHECK(passes[0].entity.flags_bits == (128 | 2) && passes[0].entity.lighting_origin.y == 2);
    /* The shotgun has no _barrel.md3: no tag_barrel attachment. */
    CHECK(passes[0].entity.attachment_count == 0);
  }
  passes = qa_app_selected_q3_weapon_presenter_world(presenter, &weapon, (qa_vec3){1, 2, 3}, (1 << 1) | (1 << 2), false, &pass_count);
  CHECK(pass_count == 4 && passes[0].shader == NULL && strcmp(passes[1].shader, "powerups/battleWeapon") == 0
        && strcmp(passes[2].shader, "powerups/quadWeapon") == 0 && passes[3].shader == NULL);
  if (pass_count == 4) {
    qa_model_source_options flash = passes[3].options(passes[3].options_ctx, &passes[3].entity);
    CHECK(flash.custom_shader == NULL && strstr(passes[3].entity.resource.requested_path, "_flash.md3") != NULL);
  }
  qa_app_simulation_presentation invisible = weapon;
  invisible.visible = false;
  passes = qa_app_selected_q3_weapon_presenter_world(presenter, &invisible, (qa_vec3){1, 2, 3}, 0, false, &pass_count);
  CHECK(pass_count == 0);
  qa_app_selected_q3_weapon_presenter_destroy(presenter);
  qa_app_application_world_scene_destroy(scene);
  qa_app_application_assets_destroy(assets);
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
  qa_identity_owner_destroy(&ids);
}

/* ---- WorldSeatPresentation over a hidden CPU renderer and real Q1 assets ---- */
static int seat_sentinel;
#define SEAT ((void *)&seat_sentinel)
typedef struct seat_calls { int view, text, fov, size, override_, planar, effect_frames, is_player, action; } seat_calls;
static seat_calls calls;
static qa_app_sim_player_view seat_view(void *ctx, qa_actor_id actor) {
  (void)actor;
  CHECK(ctx == SEAT); calls.view++;
  qa_app_sim_player_view view;
  memset(&view, 0, sizeof view);
  view.origin = (qa_vec3){480, -352, 24}; view.view_height = 22; view.angles = (qa_vec3){0, 0, 0};
  return view;
}
static const qa_world_text *seat_world_text(void *ctx, size_t *out_count) { CHECK(ctx == SEAT); calls.text++; *out_count = 0; return NULL; }
static qa_app_sim_player_ui seat_player_ui(void *ctx, qa_actor_id actor) {
  (void)actor;
  CHECK(ctx == SEAT);
  qa_app_sim_player_ui ui;
  memset(&ui, 0, sizeof ui);
  return ui;
}
static double seat_fov(void *ctx) { CHECK(ctx == SEAT); calls.fov++; return 90; }
static bool seat_size(void *ctx, qa_app_q1_view_settings *out) { (void)out; CHECK(ctx == SEAT); calls.size++; return false; }
static qa_scene_camera seat_override(void *ctx, qa_scene_camera camera) { CHECK(ctx == SEAT); calls.override_++; return camera; }
static bool seat_planar(void *ctx) { CHECK(ctx == SEAT); calls.planar++; return false; }
static bool seat_is_player(void *ctx, qa_actor_id actor) { (void)actor; CHECK(ctx == SEAT); calls.is_player++; return false; }
static void seat_effect_frame(void *ctx, qa_scene_camera camera, qa_source_scene_order *source, const qa_scene_fog *fog,
                              qa_app_application_effect_frame *out) {
  (void)camera; (void)source; (void)fog; (void)out;
  CHECK(ctx == SEAT); calls.effect_frames++;
}
static void seat_quit(void *ctx) { CHECK(ctx == SEAT); }
static void seat_command(void *ctx, const char *name, const char *const *args, size_t count) { (void)name; (void)args; (void)count; CHECK(ctx == SEAT); }
static void action_quit(void *ctx) { CHECK(ctx == SEAT); calls.action++; }
static void action_execute(void *ctx, const char *name, const char *const *list, size_t count, const qa_seat_id *seat, const qa_command_context *context) {
  (void)name; (void)list; (void)count; (void)seat; (void)context; CHECK(ctx == SEAT); calls.action++;
}
static void action_print(void *ctx, const char *text) { (void)text; CHECK(ctx == SEAT); calls.action++; }
static double seat_now(void *ctx) { CHECK(ctx == SEAT); return 0; }
static double audio_now(void *ctx) { CHECK(ctx == SEAT); return 0; }
static void audio_print(void *ctx, const char *text) { (void)text; CHECK(ctx == SEAT); }
typedef struct layout_call { qa_app_world_seat_presentation *presentation; double index, count; } layout_call;
static void layout_body(void *ctx) { layout_call *call = ctx; qa_app_world_seat_presentation_publish_layout(call->presentation, call->index, call->count); }
typedef struct replace_call { qa_app_world_seat_presentation *presentation; qa_app_seat_q3_client client; } replace_call;
static void replace_body(void *ctx) { replace_call *call = ctx; (void)qa_app_world_seat_presentation_replace_q3_client(call->presentation, &call->client, NULL); }

typedef struct render_call { qa_app_world_seat_presentation *presentation; qa_render_frame frame; qa_renderer_backend backend; } render_call;
static void render_body(void *ctx) { render_call *call = ctx; qa_app_world_seat_presentation_render(call->presentation, call->frame, call->backend); }
static void test_seat(const char *root) {
  qa_app_loaded_content *content = load_content(root, "q1-classic-id1", "e1m1");
  CHECK(content != NULL);
  if (content == NULL) return;
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "seat"));
  int owner_symbol = 0;
  qa_renderer_resource_owner owner = {&owner_symbol, ids.session, 0};
  qa_app_native_renderer_host renderer_host;
  memset(&renderer_host, 0, sizeof renderer_host);
  qa_app_native_renderer *renderer = qa_app_native_renderer_open(renderer_host, "cpu", 320, 240, true, 1, false, owner, NULL);
  if (renderer == NULL) { fprintf(stderr, "skip seat: no SDL window\n"); qa_app_loaded_content_close(content); qa_app_loaded_content_destroy(content); qa_identity_owner_destroy(&ids); return; }
  qa_app_application_assets *assets = qa_app_create_world_assets(content, owner, qa_app_native_renderer_images(renderer), assets_warn, &warn_holder, NULL);
  qa_world_scene *world = qa_app_application_assets_load_world(assets);
  qa_shared_scene_queries *queries = qa_create_scene_queries(world->map);
  qa_app_application_effects *effects = qa_app_application_effects_create(assets, qa_shared_as_scene_queries(queries), seat_is_player, SEAT, 1);
  qa_session_client *client = qa_session_client_create(qa_identity_owner_client(&ids, 0, 1));
  qa_seat_id seat_id = qa_identity_owner_seat(&ids, 0);
  qa_session_seat *seat = qa_session_seat_create(seat_id, client);
  qa_actor_id actor = qa_identity_owner_actor(&ids, 0, 0);
  char store_root[] = "build/agents/app-seat-pres/tmp/seat-store-XXXXXX";
  CHECK(mkdtemp(store_root) != NULL);
  qa_config_store store;
  CHECK(qa_config_store_init(&store, store_root));
  qa_app_options app_options;
  memset(&app_options, 0, sizeof app_options);
  app_options.network_kind = "offline"; app_options.movement = QA_GAME_FAMILY_Q1; app_options.user_content_root = store_root;
  qa_app_sim_presentation_access simulation;
  memset(&simulation, 0, sizeof simulation);
  simulation.ctx = SEAT; simulation.player_view = seat_view; simulation.world_text = seat_world_text; simulation.player_ui = seat_player_ui;
  qa_app_local_player player = {seat, actor};
  qa_app_input_open_args args;
  memset(&args, 0, sizeof args);
  args.window = qa_app_input_window_from_sdl(qa_app_native_renderer_window(renderer));
  args.players = &player; args.player_count = 1;
  args.options = &app_options; args.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE; args.simulation = &simulation;
  args.actions.ctx = SEAT; args.actions.quit = action_quit; args.actions.execute = action_execute; args.actions.print = action_print;
  args.now = seat_now; args.now_ctx = SEAT; args.settings = &store;
  qa_app_application_input *input = qa_app_application_input_open(&args);
  qa_app_local_input *local = qa_app_application_input_local(input, 0);
  qa_text_font_selection font = qa_app_application_assets_load_console_font(assets);
  qa_native_ui_art art;
  memset(&art, 0, sizeof art);
  art.skin = qa_ui_default_skin("resource:font");
  art.white.name = "white"; art.white.image = world->shaders->textures->white->image;
  qa_app_audio_options audio_options;
  memset(&audio_options, 0, sizeof audio_options);
  audio_options.defer_output = true;
  qa_app_application_audio *audio = qa_app_application_audio_create(content, audio_now, SEAT, 1, "missing-custom-model", audio_print, SEAT, &audio_options);
  qa_app_seat_ui_options ui_options;
  memset(&ui_options, 0, sizeof ui_options);
  ui_options.audio = audio;
  ui_options.local = local; ui_options.art = &art; ui_options.input = input; ui_options.simulation = &simulation; ui_options.font = font;
  ui_options.quit = seat_quit; ui_options.quit_ctx = SEAT; ui_options.command = seat_command; ui_options.command_ctx = SEAT;
  ui_options.typography = qa_app_application_assets_load_menu_typography(assets);
  qa_app_seat_ui *ui = qa_app_seat_ui_create(&ui_options);
  qa_app_world_seat_presentation_options options;
  memset(&options, 0, sizeof options);
  options.local = local; options.assets = assets; options.renderer = renderer; options.simulation = &simulation; options.seat_count = 1;
  options.font = font; options.ui = ui; options.effects = effects;
  options.field_of_view = seat_fov; options.field_of_view_ctx = SEAT;
  options.view_size = seat_size; options.view_size_ctx = SEAT;
  options.camera_override = seat_override; options.camera_override_ctx = SEAT;
  options.planar_shadows = seat_planar; options.planar_shadows_ctx = SEAT;
  qa_app_world_seat_presentation *presentation = qa_app_world_seat_presentation_create(&options);
  CHECK(presentation != NULL && qa_app_world_seat_presentation_ui(presentation) == ui && qa_app_world_seat_presentation_local(presentation) == local);
  CHECK(qa_app_world_seat_presentation_q3_client(presentation) == NULL && !qa_app_world_seat_presentation_split_screen(presentation));
  qa_rect viewport = qa_app_world_seat_presentation_viewport(presentation);
  CHECK(viewport.x == 0 && viewport.y == 0 && viewport.width == 320 && viewport.height == 240);
  qa_app_world_seat_presentation_publish_layout(presentation, 1, 2);
  viewport = qa_app_world_seat_presentation_viewport(presentation);
  CHECK(viewport.x == 0 && viewport.y == 120 && viewport.width == 320 && viewport.height == 120 && qa_app_world_seat_presentation_split_screen(presentation));
  layout_call bad = { presentation, 2, 2 };
  CHECK(raises(layout_body, &bad, "Invalid local seat layout"));
  layout_call fraction = { presentation, 0.5, 2 };
  CHECK(raises(layout_body, &fraction, "Invalid local seat layout"));
  qa_app_world_seat_presentation_publish_layout(presentation, 0, 1);
  replace_call replace;
  memset(&replace, 0, sizeof replace);
  replace.presentation = presentation; replace.client.seat = qa_identity_owner_seat(&ids, 1); replace.client.actor = actor;
  CHECK(raises(replace_body, &replace, "Replacement presentation belongs to another local player"));
  qa_app_component_effect_frame component = { SEAT, seat_effect_frame };
  int64_t binding = qa_app_world_seat_presentation_bind_component_effects(presentation, component, NULL);
  qa_seat_client_state state = qa_app_world_seat_presentation_state(presentation);
  CHECK(qa_same_seat(state.seat, seat_id) && state.actor.is_player && qa_same_actor(state.actor.actor, actor));
  CHECK(state.presentation.viewport.width == 320 && state.presentation.hud_scale == 1 && !state.ui.show_scores);
  qa_content_id entities = qa_app_loaded_content_recipe(content)->map.entities.content;
  qa_app_simulation_presentation p[3];
  for (size_t i = 0; i < 3; i++) p[i] = presentation_base(entities, QA_GAME_FAMILY_Q1);
  p[0].actor = qa_identity_owner_actor(&ids, 1, 0); p[0].path = "progs/soldier.mdl"; p[0].origin = (qa_vec3){600, -352, 24};
  p[0].has_alpha = true; p[0].alpha = 0.5;
  p[1].actor = actor; p[1].path = "progs/v_shot.mdl"; p[1].origin = (qa_vec3){480, -352, 46}; p[1].view_weapon = true;
  p[2].actor = qa_identity_owner_actor(&ids, 2, 0); p[2].path = "*1";
  qa_world_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.session = ids.session; snapshot.frame.frame = 1; snapshot.frame.time.kind = QA_SOURCE_TIME_SECONDS; snapshot.frame.time.value = 0.5;
  qa_app_world_seat_presentation_prepare(presentation, &snapshot, p, 3, NULL, 0);
  CHECK(calls.view > 0 && calls.text == 1 && calls.fov > 0 && calls.size > 0 && calls.override_ > 0);
  qa_render_frame frame = qa_app_world_seat_presentation_frame(presentation, &snapshot);
  CHECK(frame.command_count > 0 && calls.effect_frames == 1 && calls.planar > 0);
  size_t views = 0;
  for (size_t i = 0; i < frame.command_count; i++) if (frame.commands[i].kind == QA_RENDER_COMMAND_VIEW) views++;
  CHECK(views >= 1);
  render_call wrong = { presentation, frame, qa_app_native_renderer_backend(renderer) };
  wrong.backend.ctx = SEAT;
  CHECK(raises(render_body, &wrong, "Seat presentation uses another renderer"));
  qa_app_viewing_seat viewing = qa_app_world_seat_presentation_viewing_seat(presentation);
  CHECK(viewing.ctx == presentation && qa_same_actor(viewing.actor(viewing.ctx), actor) && !viewing.split_screen(viewing.ctx));
  qa_scene_camera camera = viewing.camera(viewing.ctx);
  CHECK(camera.origin.z == 46 && camera.viewport.width == 320);
  viewing.unbind_component_effects(viewing.ctx, binding);
  frame = qa_app_world_seat_presentation_frame(presentation, &snapshot);
  CHECK(calls.effect_frames == 1);
  qa_seat_presentation seat_presentation = qa_app_world_seat_presentation_seat(presentation);
  CHECK(seat_presentation.ctx == presentation && qa_same_seat(seat_presentation.state.seat, seat_id));
  qa_app_world_seat_presentation_destroy(presentation);
  qa_app_seat_ui_destroy(ui);
  qa_app_application_input_close(input);
  qa_app_application_input_destroy(input);
  qa_app_application_effects_destroy(effects);
  qa_app_application_audio_destroy(audio);
  qa_shared_scene_queries_destroy(queries);
  qa_app_application_assets_destroy(assets);
  qa_app_native_renderer_destroy(renderer);
  qa_session_seat_destroy(seat);
  qa_session_client_destroy(client);
  qa_config_store_destroy(&store);
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
  qa_identity_owner_destroy(&ids);
}

static void compare_oracle(void) {
  FILE *file = fopen("tests/app/oracles/world_scene.txt", "r");
  CHECK(file != NULL);
  if (file == NULL) return;
  char buffer[1024];
  size_t index = 0;
  while (fgets(buffer, sizeof buffer, file) != NULL) {
    buffer[strcspn(buffer, "\n")] = '\0';
    if (index >= line_count) { fprintf(stderr, "missing C line for oracle: %s\n", buffer); failures++; checks++; index++; continue; }
    checks++;
    if (strcmp(buffer, lines[index]) != 0) { failures++; fprintf(stderr, "oracle: %s\n     C: %s\n", buffer, lines[index]); }
    index++;
  }
  fclose(file);
  CHECK(index == line_count && line_count > 50);
}

int main(void) {
  test_seat_model_visible();
  struct stat status;
  const char *configured = getenv("QTS_TEST_CORPUS");
  const char *home = getenv("HOME");
  char root[512];
  snprintf(root, sizeof root, "%s", configured != NULL && configured[0] != '\0' ? configured : "");
  if (root[0] == '\0') snprintf(root, sizeof root, "%s/qfiles", home != NULL ? home : "");
  char probe[600];
  snprintf(probe, sizeof probe, "%s/Quake/Id1/PAK0.PAK", root);
  warn_holder.sentinel = 0x5a;
  if (stat(probe, &status) == 0) {
    test_q1(root);
    test_q3(root);
    test_q2_native(root);
    test_seat(root);
    compare_oracle();
  } else fprintf(stderr, "skip: no retail corpus at %s\n", root);
  for (size_t i = 0; i < line_count; i++) free(lines[i]);
  free(lines);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
