/* Port of src/app/bootstrap/presentation-scene.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/presentation_scene.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/held_weapon.h"
#include "qa/app/bootstrap/native_held_weapon.h"
#include "qa/app/bootstrap/q3_selected_weapon.h"
#include "qa/app/bootstrap/weapon_view.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/core/math.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/materials/evaluate.h"
#include "qa/render/scene/flare.h"
#include "qa/render/scene/models/renderer.h"
#include "qa/render/scene/models/transform.h"
#include "qa/render/scene/particles/primitives.h"
#include "qa/render/scene/shadows.h"

bool qa_app_seat_model_visible(const qa_actor_id *viewer, qa_actor_id actor, bool view_weapon) {
  bool first_person = viewer != NULL && qa_same_actor(*viewer, actor);
  return view_weapon ? first_person : !first_person;
}

/* ---- owned values with a shared lifetime (the GC lifetime of one TS prepare) ---- */
typedef struct held_value { void (*release)(void *value); void *value; } held_value;
typedef struct value_store { held_value *items; size_t count, capacity; } value_store;
static void store_add(value_store *store, void (*release)(void *value), void *value) {
  if (store->count == store->capacity) {
    size_t capacity = store->capacity == 0 ? 64 : store->capacity * 2;
    held_value *next = realloc(store->items, capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    store->items = next; store->capacity = capacity;
  }
  store->items[store->count++] = (held_value){ release, value };
}
static void *store_alloc(value_store *store, size_t size) {
  void *value = calloc(1, size == 0 ? 1 : size);
  if (value == NULL) qa_range_error("Out of memory");
  store_add(store, free, value);
  return value;
}
static void store_release(value_store *store) {
  while (store->count > 0) {
    held_value item = store->items[--store->count];
    item.release(item.value);
  }
}
static void store_destroy(value_store *store) { store_release(store); free(store->items); memset(store, 0, sizeof *store); }
static char *store_text(value_store *store, const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text) + 1;
  char *out = store_alloc(store, length);
  memcpy(out, text, length);
  return out;
}

/* ---- TS (entity) => ModelSourceOptions closures ---- */
typedef struct pass_options {
  qa_model_source_options (*fn)(void *ctx, const qa_scene_entity *entity);
  void *ctx;
} pass_options;
static qa_model_source_options options_of(pass_options options, const qa_scene_entity *entity) {
  if (options.fn == NULL) { qa_model_source_options empty; memset(&empty, 0, sizeof empty); return empty; }
  return options.fn(options.ctx, entity);
}
static bool view_model_of(pass_options options, const qa_scene_entity *entity) {
  qa_model_source_options value = options_of(options, entity);
  return value.has_view_model && value.view_model;
}
/* () => ({ viewModel: true }) */
static qa_model_source_options view_model_options(void *ctx, const qa_scene_entity *entity) {
  (void)ctx; (void)entity;
  qa_model_source_options options; memset(&options, 0, sizeof options);
  options.has_view_model = true; options.view_model = true;
  return options;
}
/* () => options (a constant value). */
static qa_model_source_options constant_options(void *ctx, const qa_scene_entity *entity) {
  (void)entity;
  return *(const qa_model_source_options *)ctx;
}
/* value => ({ ...inner(value), customShader: shader }) */
typedef struct shader_wrap { pass_options inner; const char *shader; } shader_wrap;
static qa_model_source_options shader_wrap_options(void *ctx, const qa_scene_entity *entity) {
  const shader_wrap *wrap = ctx;
  qa_model_source_options options = options_of(wrap->inner, entity);
  options.custom_shader = wrap->shader;
  return options;
}
/* The posed body material closure. */
typedef struct body_wrap {
  pass_options inner;
  const char *custom_shader;
  const qa_model_custom_skin_entry *custom_skin; size_t custom_skin_count;
  qa_vec2 shader_tex_coord;
  bool non_normalized_axes;
} body_wrap;
static qa_model_source_options body_wrap_options(void *ctx, const qa_scene_entity *entity) {
  const body_wrap *wrap = ctx;
  qa_model_source_options options = options_of(wrap->inner, entity);
  options.custom_shader = wrap->custom_shader;
  options.custom_skin = wrap->custom_skin; options.custom_skin_count = wrap->custom_skin_count;
  options.has_shader_tex_coord = true; options.shader_tex_coord = wrap->shader_tex_coord;
  options.has_non_normalized_axes = true; options.non_normalized_axes = wrap->non_normalized_axes;
  return options;
}

/* ---- scene state ---- */
typedef enum resolve_kind { RESOLVE_NONE, RESOLVE_NATIVE, RESOLVE_ANCHORED } resolve_kind;
typedef struct scene_pass {
  size_t group, object;
  qa_scene_entity entity;
  pass_options options;
  resolve_kind resolve;
  const qa_app_resolved_held_entity *native;
  qa_vec3 anchor;
  bool has_view_axis; qa_axis view_axis;
  bool has_time; qa_source_time time;
  bool personal;
} scene_pass;
typedef struct scene_group {
  char *key;
  qa_scene_model_renderer renderer;
  size_t *passes; size_t pass_count, pass_capacity;
} scene_group;
typedef struct scene_object { double opacity; size_t *passes; size_t pass_count, pass_capacity; } scene_object;
typedef struct presenter_entry { char *key; void *presenter; } presenter_entry;
typedef struct used_entry { char *tag; double last; } used_entry;
typedef struct light_style_row { double style; char *pattern; bool has_owner; qa_presentation_owner owner; } light_style_row;
typedef struct brush_presentation {
  qa_world_scene *scene; double model; qa_scene_model_transform transform; double frame, alpha;
} brush_presentation;
typedef struct flare_row { qa_scene_flare flare; qa_vec3 origin; qa_renderer_image image; char *image_path; } flare_row;
typedef struct beam_row { qa_registered_scene_material material; qa_vec3 origin, end; double width; } beam_row;
typedef struct posed_row {
  char *key; const char *part; qa_content_id content; qa_scene_entity entity; pass_options options;
} posed_row;

struct qa_app_application_world_scene {
  qa_app_application_assets *assets;
  const qa_q3_character_assets *character_assets;
  bool (*planar_shadows)(void *ctx); void *planar_shadows_ctx;
  scene_group **groups; size_t group_count, group_capacity;
  scene_pass *passes; size_t pass_count, pass_capacity;       /* TS ordered */
  scene_object *objects; size_t object_count, object_capacity; /* TS objects */
  presenter_entry *characters; size_t character_count, character_capacity;
  presenter_entry *selected_weapons; size_t selected_count, selected_capacity;
  used_entry *last_used; size_t used_count, used_capacity;
  double prepares;
  qa_app_foreign_held_weapons *foreign_weapons;
  qa_app_native_held_weapons *native_weapons;
  light_style_row *light_styles; size_t light_style_count, light_style_capacity;
  double light_styles_version;
  bool has_styles_cache; double styles_key_time, styles_key_version;
  double q1_styles[256]; qa_q2_light_style q2_styles[256];
  qa_world_inline_model *inline_models; size_t inline_model_count;
  brush_presentation *brush_models; size_t brush_model_count;
  double prepared_time, previous_time;
  flare_row *flares; size_t flare_count, flare_capacity;
  beam_row *beams; size_t beam_count, beam_capacity;
  value_store prepared; /* values a prepare derives */
  value_store frame;    /* operation lists and batches handed out since the last prepare */
};

#define GROW(array, count, capacity, initial) do { \
  if ((count) == (capacity)) { \
    size_t next_capacity_ = (capacity) == 0 ? (initial) : (capacity) * 2; \
    void *next_ = realloc((array), next_capacity_ * sizeof *(array)); \
    if (next_ == NULL) qa_range_error("Out of memory"); \
    (array) = next_; (capacity) = next_capacity_; \
  } } while (0)

static char *heap_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *out = malloc(length);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, length);
  return out;
}

static bool default_planar_shadows(void *ctx) { (void)ctx; return false; }

qa_app_application_world_scene *qa_app_application_world_scene_create(qa_app_application_assets *assets,
    const qa_q3_character_assets *character_assets, bool (*planar_shadows)(void *ctx), void *planar_shadows_ctx) {
  qa_app_application_world_scene *scene = calloc(1, sizeof *scene);
  if (scene == NULL) return NULL;
  scene->assets = assets;
  scene->character_assets = character_assets;
  scene->planar_shadows = planar_shadows != NULL ? planar_shadows : default_planar_shadows;
  scene->planar_shadows_ctx = planar_shadows_ctx;
  scene->foreign_weapons = qa_app_foreign_held_weapons_create(qa_app_held_assets_application(assets));
  scene->native_weapons = qa_app_native_held_weapons_create(qa_app_native_held_assets_application(assets));
  if (scene->foreign_weapons == NULL || scene->native_weapons == NULL) {
    qa_app_foreign_held_weapons_destroy(scene->foreign_weapons);
    qa_app_native_held_weapons_destroy(scene->native_weapons);
    free(scene);
    return NULL;
  }
  return scene;
}
qa_app_application_assets *qa_app_application_world_scene_assets(const qa_app_application_world_scene *scene) {
  return scene->assets;
}

static void destroy_character(void *presenter) {
  qa_q3_character_presenter_destroy(presenter);
  free(presenter);
}
static void remove_presenter(presenter_entry *entries, size_t *count, size_t index, bool character) {
  free(entries[index].key);
  if (character) destroy_character(entries[index].presenter);
  else qa_app_selected_q3_weapon_presenter_destroy(entries[index].presenter);
  memmove(&entries[index], &entries[index + 1], (*count - index - 1) * sizeof *entries);
  (*count)--;
}
static void clear_frame_lists(qa_app_application_world_scene *scene) {
  for (size_t i = 0; i < scene->object_count; i++) free(scene->objects[i].passes);
  scene->object_count = 0;
  scene->pass_count = 0;
  for (size_t i = 0; i < scene->flare_count; i++) free(scene->flares[i].image_path);
  scene->flare_count = 0;
  scene->beam_count = 0;
}

void qa_app_application_world_scene_close(qa_app_application_world_scene *scene) {
  clear_frame_lists(scene);
  for (size_t i = 0; i < scene->group_count; i++) {
    qa_scene_model_renderer_destroy(&scene->groups[i]->renderer);
    free(scene->groups[i]->passes); free(scene->groups[i]->key); free(scene->groups[i]);
  }
  scene->group_count = 0;
  while (scene->character_count > 0) remove_presenter(scene->characters, &scene->character_count, scene->character_count - 1, true);
  while (scene->selected_count > 0) remove_presenter(scene->selected_weapons, &scene->selected_count, scene->selected_count - 1, false);
  store_release(&scene->frame);
  store_release(&scene->prepared);
}
void qa_app_application_world_scene_destroy(qa_app_application_world_scene *scene) {
  if (scene == NULL) return;
  qa_app_application_world_scene_close(scene);
  free(scene->groups); free(scene->passes); free(scene->objects);
  free(scene->characters); free(scene->selected_weapons);
  for (size_t i = 0; i < scene->used_count; i++) free(scene->last_used[i].tag);
  free(scene->last_used);
  for (size_t i = 0; i < scene->light_style_count; i++) free(scene->light_styles[i].pattern);
  free(scene->light_styles);
  free(scene->inline_models); free(scene->brush_models);
  free(scene->flares); free(scene->beams);
  store_destroy(&scene->frame);
  store_destroy(&scene->prepared);
  qa_app_foreign_held_weapons_destroy(scene->foreign_weapons);
  qa_app_native_held_weapons_destroy(scene->native_weapons);
  free(scene);
}

/* ---- presenter bookkeeping (usedWeapon / usedCharacter / pruneUnused) ---- */
static void mark_used(qa_app_application_world_scene *scene, const char *tag) {
  for (size_t i = 0; i < scene->used_count; i++)
    if (strcmp(scene->last_used[i].tag, tag) == 0) { scene->last_used[i].last = scene->prepares; return; }
  GROW(scene->last_used, scene->used_count, scene->used_capacity, 16);
  scene->last_used[scene->used_count++] = (used_entry){ heap_text(tag), scene->prepares };
}
static void *find_presenter(presenter_entry *entries, size_t count, const char *key) {
  for (size_t i = 0; i < count; i++) if (strcmp(entries[i].key, key) == 0) return entries[i].presenter;
  return NULL;
}
static void prune_unused(qa_app_application_world_scene *scene) {
  scene->prepares++;
  if ((qa_js_to_int32(scene->prepares) & 255) != 0) return;
  for (size_t i = 0; i < scene->used_count;) {
    used_entry *entry = &scene->last_used[i];
    if (scene->prepares - entry->last <= 256) { i++; continue; }
    bool weapon = strncmp(entry->tag, "w:", 2) == 0;
    const char *key = entry->tag + 2;
    presenter_entry *entries = weapon ? scene->selected_weapons : scene->characters;
    size_t *count = weapon ? &scene->selected_count : &scene->character_count;
    for (size_t k = 0; k < *count; k++)
      if (strcmp(entries[k].key, key) == 0) { remove_presenter(entries, count, k, !weapon); break; }
    free(entry->tag);
    memmove(&scene->last_used[i], &scene->last_used[i + 1], (scene->used_count - i - 1) * sizeof *scene->last_used);
    scene->used_count--;
  }
}
static qa_app_selected_q3_weapon_presenter *selected_weapon(qa_app_application_world_scene *scene,
    const qa_app_simulation_presentation *equipped, const qa_q3_player_animation_config *animation) {
  char key[512];
  snprintf(key, sizeof key, "%s/%lld/%lld", equipped->content, (long long)equipped->actor.slot, (long long)equipped->actor.generation);
  char tag[520];
  snprintf(tag, sizeof tag, "w:%s", key);
  mark_used(scene, tag);
  qa_app_selected_q3_weapon_presenter *presenter = find_presenter(scene->selected_weapons, scene->selected_count, key);
  if (presenter == NULL) {
    presenter = qa_app_selected_q3_weapon_presenter_create(scene->assets, animation);
    if (presenter == NULL) qa_range_error("Out of memory");
    GROW(scene->selected_weapons, scene->selected_count, scene->selected_capacity, 8);
    scene->selected_weapons[scene->selected_count++] = (presenter_entry){ heap_text(key), presenter };
  }
  return presenter;
}

/* ---- light styles ---- */
void qa_app_application_world_scene_receive(qa_app_application_world_scene *scene,
    const qa_app_sim_presentation_event *events, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *source = &events[i];
    if (strcmp(source->kind, "presentation-owner") == 0 && strcmp(source->presentation_owner.kind, "retired") == 0) {
      for (size_t k = 0; k < scene->light_style_count;) {
        light_style_row *row = &scene->light_styles[k];
        if (qa_same_presentation_owner(row->has_owner ? &row->owner : NULL, source->presentation_owner.owner)) {
          free(row->pattern);
          memmove(row, row + 1, (scene->light_style_count - k - 1) * sizeof *row);
          scene->light_style_count--;
          scene->light_styles_version++;
        } else k++;
      }
      continue;
    }
    double style; const char *pattern;
    if (strcmp(source->kind, "q1") == 0 && strcmp(source->q1->kind, "lightstyle") == 0) {
      style = source->q1->style; pattern = source->q1->pattern;
    } else if (strcmp(source->kind, "q2") == 0 && strcmp(source->q2->kind, "lightstyle") == 0) {
      style = source->q2->style; pattern = source->q2->pattern;
    } else continue;
    light_style_row *row = NULL;
    for (size_t k = 0; k < scene->light_style_count; k++)
      if (scene->light_styles[k].style == style) { row = &scene->light_styles[k]; break; }
    if (row == NULL) {
      GROW(scene->light_styles, scene->light_style_count, scene->light_style_capacity, 16);
      row = &scene->light_styles[scene->light_style_count++];
      row->style = style; row->pattern = NULL;
    }
    char *copy = heap_text(pattern);
    free(row->pattern);
    row->pattern = copy;
    row->has_owner = source->has_owner;
    if (source->has_owner) row->owner = source->owner;
    scene->light_styles_version++;
  }
}
static const light_style_row *light_style(const qa_app_application_world_scene *scene, double index) {
  for (size_t i = 0; i < scene->light_style_count; i++) if (scene->light_styles[i].style == index) return &scene->light_styles[i];
  return NULL;
}
double qa_app_application_world_scene_style(const qa_app_application_world_scene *scene, double index, double absent) {
  const light_style_row *row = light_style(scene, index);
  if (row == NULL || row->pattern[0] == '\0') return absent;
  double length = (double)strlen(row->pattern);
  double position = fmod(trunc(scene->prepared_time * 10), length);
  /* charCodeAt outside the string is NaN. */
  if (!(position >= 0) || position >= length) return NAN;
  return (double)(unsigned char)row->pattern[(size_t)position] - 97;
}
qa_app_world_scene_styles qa_app_application_world_scene_styles(qa_app_application_world_scene *scene) {
  double key_time = trunc(scene->prepared_time * 10);
  if (!(scene->has_styles_cache && scene->styles_key_time == key_time && scene->styles_key_version == scene->light_styles_version)) {
    for (size_t index = 0; index < 256; index++) {
      scene->q1_styles[index] = light_style(scene, (double)index) != NULL
          ? qa_app_application_world_scene_style(scene, (double)index, 12) * 22 : 256;
      double v = qa_app_application_world_scene_style(scene, (double)index, 12) / 12;
      scene->q2_styles[index] = (qa_q2_light_style){ { v, v, v }, v * 3 };
    }
    scene->has_styles_cache = true; scene->styles_key_time = key_time; scene->styles_key_version = scene->light_styles_version;
  }
  qa_app_world_scene_styles out = { scene->q1_styles, 256, scene->q2_styles, 256 };
  return out;
}

/* ---- prepare ---- */
typedef struct prepare_state {
  qa_app_application_world_scene *scene;
  const qa_actor_id *viewer;
  const qa_app_world_scene_prepare_options *options;
  posed_row *posed; size_t posed_count, posed_capacity;
  qa_world_inline_model *inline_models; size_t inline_count, inline_capacity;
  brush_presentation *brushes; size_t brush_count, brush_capacity;
} prepare_state;

static qa_scene_entity copy_entity(value_store *store, const qa_scene_entity *source) {
  qa_scene_entity out = *source;
  if (source->actor != NULL) {
    qa_actor_id *actor = store_alloc(store, sizeof *actor);
    *actor = *source->actor;
    out.actor = actor;
  }
  if (source->attachment_count > 0) {
    qa_scene_entity_attachment *attachments = store_alloc(store, source->attachment_count * sizeof *attachments);
    for (size_t i = 0; i < source->attachment_count; i++) {
      qa_scene_entity *child = store_alloc(store, sizeof *child);
      *child = copy_entity(store, source->attachments[i].entity);
      attachments[i].tag = store_text(store, source->attachments[i].tag);
      attachments[i].entity = child;
    }
    out.attachments = attachments;
  } else out.attachments = NULL;
  return out;
}

static bool same_actor_ptr(const qa_actor_id *left, const qa_actor_id *right) {
  return left != NULL && right != NULL && qa_same_actor(*left, *right);
}

static scene_group *group_for(qa_app_application_world_scene *scene, qa_content_id content, qa_content_id shader_content, size_t *index_out) {
  char key[1024];
  snprintf(key, sizeof key, "%s/%s", content, shader_content != NULL ? shader_content : content);
  for (size_t i = 0; i < scene->group_count; i++)
    if (strcmp(scene->groups[i]->key, key) == 0) { *index_out = i; return scene->groups[i]; }
  qa_model_render_provider provider = qa_app_application_assets_render_provider(scene->assets, content);
  if (shader_content != NULL) {
    /* TS { family: "q3", palette, textures, shaders }: a plain object read once. */
    qa_model_render_provider shaders = qa_app_application_assets_render_provider(scene->assets, shader_content);
    qa_model_render_provider composite;
    memset(&composite, 0, sizeof composite);
    composite.has_model_policy = false;
    composite.family = QA_GAME_FAMILY_Q3;
    composite.palette = provider.palette;
    composite.textures = qa_model_render_provider_textures(&provider);
    composite.shaders = shaders.shaders;
    provider = composite;
  }
  scene_group *group = calloc(1, sizeof *group);
  if (group == NULL) qa_range_error("Out of memory");
  if (!qa_scene_model_renderer_init(&group->renderer, provider, qa_app_application_assets_world(scene->assets))) {
    free(group); qa_range_error("Out of memory");
  }
  group->key = heap_text(key);
  GROW(scene->groups, scene->group_count, scene->group_capacity, 8);
  scene->groups[scene->group_count] = group;
  *index_out = scene->group_count++;
  return group;
}

typedef struct append_args {
  pass_options options;            /* fn NULL is TS undefined: () => ({}) */
  bool has_object; size_t object;
  qa_content_id shader_content;    /* NULL is TS undefined */
  bool has_time; qa_source_time time;
  resolve_kind resolve; const qa_app_resolved_held_entity *native; qa_vec3 anchor;
  bool has_view_axis; qa_axis view_axis;
} append_args;
static append_args no_args(void) { append_args args; memset(&args, 0, sizeof args); return args; }
static size_t new_object(qa_app_application_world_scene *scene, double opacity) {
  GROW(scene->objects, scene->object_count, scene->object_capacity, 32);
  scene->objects[scene->object_count] = (scene_object){ opacity, NULL, 0, 0 };
  return scene->object_count++;
}
/* TS append. entity is copied into the prepare's values. Returns the pass index. */
static size_t append(prepare_state *state, qa_content_id content, const qa_scene_entity *entity, append_args args) {
  qa_app_application_world_scene *scene = state->scene;
  size_t group_index;
  scene_group *group = group_for(scene, content, args.shader_content, &group_index);
  scene_pass pass;
  memset(&pass, 0, sizeof pass);
  pass.entity = copy_entity(&scene->prepared, entity);
  pass.options = args.options;
  pass.personal = state->viewer != NULL && same_actor_ptr(pass.entity.actor, state->viewer)
      && !(args.options.fn != NULL && view_model_of(args.options, &pass.entity));
  pass.resolve = args.resolve; pass.native = args.native; pass.anchor = args.anchor;
  pass.has_view_axis = args.has_view_axis; pass.view_axis = args.view_axis;
  pass.has_time = args.has_time; pass.time = args.time;
  pass.group = group_index;
  pass.object = args.has_object ? args.object : new_object(scene, entity->has_opacity ? entity->opacity : 1);
  GROW(scene->passes, scene->pass_count, scene->pass_capacity, 64);
  size_t index = scene->pass_count++;
  scene->passes[index] = pass;
  GROW(group->passes, group->pass_count, group->pass_capacity, 16);
  group->passes[group->pass_count++] = index;
  scene_object *object = &scene->objects[pass.object];
  GROW(object->passes, object->pass_count, object->pass_capacity, 4);
  object->passes[object->pass_count++] = index;
  return index;
}

static bool affected(const prepare_state *state, const qa_actor_id *actor) {
  if (actor == NULL) return false;
  for (size_t i = 0; i < state->options->body_count; i++) if (qa_same_actor(state->options->bodies[i].actor, *actor)) return true;
  return false;
}
static void retain(prepare_state *state, const char *part, qa_content_id content, const qa_scene_entity *entity, pass_options options) {
  if (entity->actor == NULL) return;
  char key[1024];
  snprintf(key, sizeof key, "%lld/%lld/%s/%s", (long long)entity->actor->slot, (long long)entity->actor->generation, part, entity->resource.id);
  for (size_t i = 0; i < state->posed_count; i++) if (strcmp(state->posed[i].key, key) == 0) return;
  value_store *store = &state->scene->prepared;
  GROW(state->posed, state->posed_count, state->posed_capacity, 8);
  state->posed[state->posed_count++] = (posed_row){ store_text(store, key), part, content, copy_entity(store, entity), options };
}
static bool same_resource(const qa_resolved_resource_reference *a, const qa_resolved_resource_reference *b) {
  return a->id != NULL && b->id != NULL && strcmp(a->id, b->id) == 0;
}
/* TS appendBody. */
static void append_body(prepare_state *state, qa_content_id content, const qa_scene_entity *entity, pass_options options,
                        bool base, bool has_object, size_t object) {
  append_args args = no_args();
  args.options = options; args.has_object = has_object; args.object = object;
  if (!affected(state, entity->actor)) { append(state, content, entity, args); return; }
  const qa_q3_character_assets *assets = state->scene->character_assets;
  const char *part = assets != NULL && same_resource(&entity->resource, &assets->lower.resource) ? "lower"
      : assets != NULL && same_resource(&entity->resource, &assets->upper.resource) ? "upper"
      : assets != NULL && same_resource(&entity->resource, &assets->head.resource) ? "head" : "body";
  qa_scene_entity body = *entity;
  body.attachments = NULL; body.attachment_count = 0;
  retain(state, part, content, &body, options);
  if (!base || qa_app_body_base_visible(state->options->bodies, state->options->body_count, *entity->actor, part))
    append(state, content, &body, args);
  for (size_t i = 0; i < entity->attachment_count; i++) {
    qa_model_attachment_tag_result tag;
    if (!qa_model_attachment_tag(entity, entity->attachments[i].tag, &tag)) continue;
    qa_scene_entity child = qa_attach_scene_entity(entity, entity->attachments[i].entity, tag);
    if (assets != NULL && (same_resource(&assets->lower.resource, &child.resource) || same_resource(&assets->upper.resource, &child.resource)
                           || same_resource(&assets->head.resource, &child.resource)))
      append_body(state, content, &child, options, base, has_object, object);
    else append(state, content, &child, args);
  }
}

static const qa_app_simulation_presentation *equipped_for(const qa_app_simulation_presentation *presentations, size_t count, qa_actor_id actor) {
  for (size_t i = 0; i < count; i++) if (presentations[i].view_weapon && qa_same_actor(presentations[i].actor, actor)) return &presentations[i];
  return NULL;
}

/* TS skipModel. */
static void skip_model(qa_app_application_world_scene *scene, const qa_app_simulation_presentation *source, const char *message) {
  size_t key_length = strlen(source->content) + strlen(source->path) + 16;
  char *key = malloc(key_length);
  size_t text_length = strlen(source->path) + strlen(message) + 32;
  char *text = malloc(text_length);
  if (key == NULL || text == NULL) qa_range_error("Out of memory");
  snprintf(key, key_length, "model:%s/%s", source->content, source->path);
  snprintf(text, text_length, "Model %s cannot be drawn: %s\n", source->path, message);
  qa_app_application_assets_warn_once(scene->assets, key, text);
  free(key); free(text);
}
typedef struct model_job { qa_app_application_assets *assets; qa_content_id content; const char *path; qa_app_model_asset asset; } model_job;
static void model_run(void *ctx) {
  model_job *job = ctx;
  job->asset = qa_app_application_assets_model(job->assets, job->content, job->path);
}

/* The source options closure. */
typedef struct source_options_ctx { qa_model_source_options value; } source_options_ctx;

/* Copies passes of a held-weapon producer into pass storage owned by the prepare. */
static qa_q3_character_pass *copy_passes(value_store *store, const qa_q3_character_pass *passes, size_t count) {
  qa_q3_character_pass *out = store_alloc(store, count * sizeof *out);
  for (size_t i = 0; i < count; i++) { out[i] = passes[i]; out[i].entity = copy_entity(store, &passes[i].entity); }
  return out;
}
/* TS selected.world({ ...equipped, visible: true }, character, personal). */
static qa_q3_character_pass *selected_world(prepare_state *state, const qa_app_simulation_presentation *equipped,
    const qa_q3_player_animation_config *animation, qa_vec3 origin, double powerups, bool personal, size_t *count) {
  qa_app_selected_q3_weapon_presenter *selected = selected_weapon(state->scene, equipped, animation);
  qa_app_simulation_presentation visible = *equipped;
  visible.visible = true;
  const qa_q3_character_pass *passes = qa_app_selected_q3_weapon_presenter_world(selected, &visible, origin, powerups, personal, count);
  return copy_passes(&state->scene->prepared, passes, *count);
}

static void prepare_source(prepare_state *state, const qa_app_simulation_presentation *original,
                           const qa_app_simulation_presentation *presentations, size_t presentation_count,
                           const qa_q3_character_view *characters, size_t character_count) {
  qa_app_application_world_scene *scene = state->scene;
  value_store *store = &scene->prepared;
  const qa_actor_id *viewer = state->viewer;
  qa_app_simulation_presentation source = *original;
  bool viewer_is_actor = viewer != NULL && qa_same_actor(*viewer, source.actor);
  if (!source.visible || (source.view_weapon && !state->options->view_weapon_visible && viewer_is_actor)) return;
  if (source.has_shader_beam) {
    qa_app_provider_scene_assets provider = qa_app_application_assets_provider(scene->assets, source.content);
    GROW(scene->beams, scene->beam_count, scene->beam_capacity, 8);
    beam_row row = { qa_scene_shader_registry_register(provider.shaders, source.shader_beam_path, NULL), source.origin,
                     source.shader_beam_end, source.shader_beam_width };
    scene->beams[scene->beam_count++] = row;
    return;
  }
  if (source.has_flare) {
    qa_app_provider_scene_assets provider = qa_app_application_assets_provider(scene->assets, source.content);
    const char *image_path = source.flare.image;
    qa_scene_texture_load_options load = { true, false, "clamp", "q2", "sprite" };
    const qa_scene_texture *texture = qa_scene_texture_loader_load(provider.textures, image_path, &load);
    if (texture == NULL && strcmp(image_path, "misc/flare.tga") != 0) {
      image_path = "misc/flare.tga";
      texture = qa_scene_texture_loader_load(provider.textures, image_path, &load);
    }
    if (texture != NULL) {
      GROW(scene->flares, scene->flare_count, scene->flare_capacity, 8);
      flare_row *row = &scene->flares[scene->flare_count++];
      row->flare = source.flare;
      row->flare.image = store_text(store, source.flare.image);
      if (source.flare.rim_color != NULL) {
        qa_vec3 *rim = store_alloc(store, sizeof *rim);
        *rim = *source.flare.rim_color;
        row->flare.rim_color = rim;
      }
      row->origin = source.origin; row->image = texture->image; row->image_path = heap_text(image_path);
    }
    return;
  }
  if (source.path[0] == '\0') return;
  if (source.view_weapon && !viewer_is_actor) return;
  if (!source.view_weapon)
    for (size_t i = 0; i < character_count; i++) if (qa_same_actor(characters[i].actor, source.actor)) return;
  const qa_app_sim_q3_grapple_cable *cable = source.q3_grapple_cable;
  bool has_cable_start = false; qa_vec3 cable_start = {0, 0, 0};
  if (cable != NULL) {
    bool local = viewer != NULL && qa_same_actor(*viewer, cable->owner);
    qa_angle_vectors aim = qa_qvm_angle_vectors(cable->owner_angles);
    cable_start = cable->offhand ? qa_add3(qa_add3(cable->owner_origin, (qa_vec3){0, 0, 26}), qa_scale3(aim.right, local ? -10 : -6))
        : qa_add3(cable->owner_origin, (qa_vec3){0, 0, local ? cable->view_height : 0});
    if (local && cable->offhand) cable_start = qa_add3(cable_start, qa_scale3(aim.forward, 3));
    has_cable_start = true;
    source.path = !cable->attached ? cable->flight : qa_length3(qa_sub3(source.origin, cable_start)) > 64 ? cable->pull : cable->hold;
  }
  if (source.has_q3_weapon) {
    qa_app_selected_q3_weapon_presenter *presenter = selected_weapon(scene, &source,
        scene->character_assets != NULL ? &scene->character_assets->animation : NULL);
    qa_scene_entity held;
    if (qa_app_selected_q3_weapon_presenter_frame(presenter, &source, state->options->field_of_view, &held)) {
      append_args args = no_args();
      args.options = (pass_options){ view_model_options, NULL };
      append(state, source.content, &held, args);
    }
    return;
  }
  model_job job;
  memset(&job, 0, sizeof job);
  job.assets = scene->assets; job.content = source.content; job.path = source.path;
  qa_caught_error caught;
  if (!qa_try(model_run, &job, &caught)) {
    if (source.family != QA_GAME_FAMILY_Q2) qa_rethrow(&caught);
    if (strncmp(source.path, "players/", 8) != 0 || strcmp(source.path, "players/male/tris.md2") == 0) {
      skip_model(scene, &source, caught.message);
      return;
    }
    source.path = "players/male/tris.md2";
    source.has_skin_path = true; source.skin_path = "players/male/grunt.pcx";
    job.path = source.path;
    if (!qa_try(model_run, &job, &caught)) { skip_model(scene, &source, caught.message); return; }
  }
  qa_app_model_asset asset = job.asset;
  double model_flags = asset.model.kind == QA_DECODED_MODEL_Q1_ALIAS ? asset.model.as.q1_alias->flags
      : asset.model.kind == QA_DECODED_MODEL_MD5 && asset.model.as.md5->skin_selection.kind != NULL
        && strcmp(asset.model.as.md5->skin_selection.kind, "q1-mdl-replacement") == 0 ? asset.model.as.md5->skin_selection.flags : 0;
  qa_vec3 angles = source.angles;
  if (source.family == QA_GAME_FAMILY_Q1 && (qa_js_to_int32(model_flags) & 8) != 0) {
    double turn = trunc((double)(float)(100 * scene->prepared_time) * (65536.0 / 360));
    angles.y = (float)((360.0 / 65536) * (double)(qa_js_to_int32(turn) & 65535));
  }
  qa_axis axis = qa_angles_to_axis(angles);
  qa_app_simulation_presentation_view weapon_view = { source.view_weapon, qa_game_family_name(source.family), source.origin };
  qa_vec3 view_origin = qa_app_weapon_view_origin(weapon_view);
  if (asset.model.kind == QA_DECODED_MODEL_BRUSH_MODEL) {
    qa_scene_model_transform transform = { view_origin, axis, true, source.scale };
    double alpha = source.has_alpha ? source.alpha : 1;
    if (asset.brush_scene == qa_app_application_assets_world(scene->assets)) {
      GROW(state->inline_models, state->inline_count, state->inline_capacity, 8);
      qa_world_inline_model *model = &state->inline_models[state->inline_count++];
      memset(model, 0, sizeof *model);
      model->model = asset.model.brush_model_index; model->transform = transform;
      model->has_animation_frame = true; model->animation_frame = source.frame;
      model->has_alternate_animation = true; model->alternate_animation = source.family == QA_GAME_FAMILY_Q1 && source.frame != 0;
      model->has_entity_rgba = true; model->entity_rgba = (qa_vec4){ 255, 255, 255, alpha * 255 };
      model->has_casts_shadow = true; model->casts_shadow = alpha == 1;
    } else {
      if (asset.brush_scene == NULL) qa_raise("Error", 0, 0, "Brush model %s has no prepared scene", source.path);
      GROW(state->brushes, state->brush_count, state->brush_capacity, 8);
      state->brushes[state->brush_count++] = (brush_presentation){ asset.brush_scene, asset.model.brush_model_index, transform,
                                                                    source.frame, alpha };
    }
    return;
  }
  qa_actor_id *actor = store_alloc(store, sizeof *actor);
  *actor = source.actor;
  qa_scene_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.actor = actor;
  entity.resource = asset.resource; entity.model = asset.model;
  entity.transform.origin = view_origin; entity.transform.axis = axis;
  entity.transform.scale = (qa_vec3){ source.scale, source.scale, source.scale };
  entity.previous_origin = source.has_previous_origin ? source.previous_origin : source.origin;
  entity.pose.is_frame = true; entity.pose.frame = source.frame; entity.pose.previous_frame = source.old_frame;
  entity.pose.back_lerp = source.has_back_lerp ? source.back_lerp : 0;
  entity.skin = source.skin;
  entity.has_opacity = true; entity.opacity = source.has_alpha ? source.alpha : 1;
  entity.color = (qa_vec4){ 1, 1, 1, 1 };
  entity.shader_time.kind = QA_SOURCE_TIME_SECONDS; entity.shader_time.value = 0;
  entity.flags_kind = qa_game_family_name(source.family); entity.flags_bits = source.render_flags;
  entity.lighting_origin = source.origin; entity.shadow_plane = 0;
  size_t attachment_count = source.model_attachment_count;
  qa_scene_entity_attachment *attachments = attachment_count == 0 ? NULL : store_alloc(store, attachment_count * sizeof *attachments);
  for (size_t i = 0; i < attachment_count; i++) {
    qa_app_model_asset child_asset = qa_app_application_assets_model(scene->assets, source.content, source.model_attachments[i].path);
    qa_scene_entity *child = store_alloc(store, sizeof *child);
    *child = entity;
    child->resource = child_asset.resource; child->model = child_asset.model;
    child->transform.origin = (qa_vec3){0, 0, 0}; child->transform.axis = qa_angles_to_axis((qa_vec3){0, 0, 0});
    child->transform.scale = (qa_vec3){1, 1, 1};
    child->attachments = NULL; child->attachment_count = 0;
    attachments[i].tag = store_text(store, source.model_attachments[i].tag);
    attachments[i].entity = child;
  }
  if (cable != NULL && has_cable_start) {
    qa_vec3 delta = qa_sub3(cable_start, source.origin);
    double distance = qa_length3(delta);
    qa_vec3 direction = qa_normalize3_or_zero(delta);
    qa_axis cable_axis = qa_angles_to_axis(qa_vector_to_angles(direction));
    double count = floor(distance / cable->segment_length);
    if (count > 65536) qa_raise("Error", 0, 0, "Source grapple cable exceeds the model segment limit");
    for (double index = 0; index <= count; index++) {
      qa_scene_entity segment = entity;
      segment.transform.origin = qa_sub3(cable_start, qa_scale3(direction, (index + 1) * cable->segment_length));
      segment.transform.axis = cable_axis;
      append(state, source.content, &segment, no_args());
    }
    return;
  }
  source_options_ctx *option_ctx = store_alloc(store, sizeof *option_ctx);
  qa_model_source_options *value = &option_ctx->value;
  value->has_view_model = true; value->view_model = source.view_weapon;
  if (source.has_model_beam) { value->has_model_beam = true; value->model_beam_segment_length = source.model_beam_segment_length; }
  if (source.has_indexed_skin) { value->has_indexed_skin = true; value->indexed_skin = source.indexed_skin; }
  if (source.has_player_colors) {
    value->has_player_colors = true; value->player_color_top = source.player_color_top; value->player_color_bottom = source.player_color_bottom;
  }
  value->has_player = true;
  value->player = source.family == QA_GAME_FAMILY_Q2 && strncmp(source.path, "players/", 8) == 0;
  value->custom_shader = source.has_skin_path ? store_text(store, source.skin_path) : NULL;
  if (value->has_indexed_skin) value->indexed_skin.name = store_text(store, source.indexed_skin.name);
  pass_options options = { constant_options, value };
  if (source.native_held_weapon) {
    const qa_app_simulation_presentation *equipped = equipped_for(presentations, presentation_count, source.actor);
    if (equipped == NULL || (equipped->path[0] == '\0' && equipped->held_weapon == NULL)) return;
    const qa_held_weapon_declaration *declaration = qa_app_foreign_held_weapons_declaration(scene->foreign_weapons, equipped);
    if (declaration != NULL && !declaration->is_model) return;
    qa_app_native_held_attachment attach;
    if (!qa_app_native_held_weapons_attachment(scene->native_weapons, source.content, &entity, &attach)) return;
    qa_q3_character_pass *passes; size_t pass_count;
    if (equipped->has_q3_weapon && declaration == NULL) {
      passes = selected_world(state, equipped, scene->character_assets != NULL ? &scene->character_assets->animation : NULL,
                              entity.lighting_origin, 0, false, &pass_count);
    } else {
      qa_app_held_character character = { entity.lighting_origin, entity.color, true, entity.has_opacity ? entity.opacity : 1 };
      qa_q3_character_pass pass;
      pass_count = qa_app_foreign_held_weapons_frame(scene->foreign_weapons, equipped, character, &pass);
      passes = copy_passes(store, &pass, pass_count);
    }
    if (pass_count == 0) qa_raise("Error", 0, 0, "Selected weapon %s has no source held model", equipped->path);
    size_t object = new_object(scene, entity.has_opacity ? entity.opacity : 1);
    for (size_t i = 0; i < pass_count; i++) {
      qa_app_resolved_held_entity *resolved = qa_app_native_held_attachment_bind(&attach, &passes[i].entity);
      if (resolved == NULL) qa_range_error("Out of memory");
      store_add(store, (void (*)(void *))qa_app_resolved_held_entity_destroy, resolved);
      append_args args = no_args();
      args.options = (pass_options){ passes[i].options, passes[i].options_ctx };
      args.has_object = true; args.object = object;
      args.resolve = RESOLVE_NATIVE; args.native = resolved;
      append(state, passes[i].content != NULL ? passes[i].content : equipped->content, &passes[i].entity, args);
    }
    return;
  }
  entity.attachments = attachments; entity.attachment_count = attachment_count;
  if (source.view_weapon) {
    append_args args = no_args();
    args.options = options;
    args.resolve = RESOLVE_ANCHORED;
    args.anchor = source.has_view_origin ? source.view_origin : source.origin;
    args.has_view_axis = true;
    args.view_axis = qa_angles_to_axis(source.has_view_angles ? source.view_angles : source.angles);
    append(state, source.content, &entity, args);
  } else append_body(state, source.content, &entity, options, true, false, 0);
}

static qa_content_id appearance_content(const qa_app_application_world_scene *scene) {
  return qa_app_loaded_content_recipe(qa_app_application_assets_content(scene->assets))->character.appearance.content;
}

static void prepare_character(prepare_state *state, const qa_q3_character_view *character,
                              const qa_app_simulation_presentation *presentations, size_t presentation_count) {
  qa_app_application_world_scene *scene = state->scene;
  value_store *store = &scene->prepared;
  const qa_q3_character_assets *assets = scene->character_assets;
  char key[64];
  snprintf(key, sizeof key, "%lld:%lld", (long long)character->actor.slot, (long long)character->actor.generation);
  char tag[72];
  snprintf(tag, sizeof tag, "c:%s", key);
  mark_used(scene, tag);
  qa_q3_character_presenter *presenter = find_presenter(scene->characters, scene->character_count, key);
  double time_ms = trunc(scene->prepared_time * 1000);
  if (presenter == NULL) {
    presenter = calloc(1, sizeof *presenter);
    if (presenter == NULL) qa_range_error("Out of memory");
    qa_q3_character_presenter_init(presenter, assets);
    qa_q3_character_presenter_reset(presenter, *character, time_ms);
    GROW(scene->characters, scene->character_count, scene->character_capacity, 8);
    scene->characters[scene->character_count++] = (presenter_entry){ heap_text(key), presenter };
  }
  const qa_app_simulation_presentation *equipped = equipped_for(presentations, presentation_count, character->actor);
  const qa_held_weapon_declaration *declaration = equipped == NULL ? NULL
      : qa_app_foreign_held_weapons_declaration(scene->foreign_weapons, equipped);
  bool personal = state->viewer != NULL && qa_same_actor(*state->viewer, character->actor);
  qa_q3_character_pass *weapon = NULL; size_t weapon_count = 0;
  if (equipped != NULL && equipped->has_q3_weapon && declaration == NULL) {
    weapon = selected_world(state, equipped, &assets->animation, character->origin, character->powerups, personal, &weapon_count);
  } else if (equipped != NULL && !personal) {
    qa_app_held_character held = { character->origin, character->color, character->has_opacity, character->opacity };
    qa_q3_character_pass pass;
    weapon_count = qa_app_foreign_held_weapons_frame(scene->foreign_weapons, equipped, held, &pass);
    weapon = copy_passes(store, &pass, weapon_count);
  }
  qa_q3_character_render_options options;
  memset(&options, 0, sizeof options);
  options.time_milliseconds = time_ms;
  options.frame_milliseconds = qa_js_max(0, time_ms - trunc(scene->previous_time * 1000));
  options.shader_time.kind = QA_SOURCE_TIME_SECONDS; options.shader_time.value = 0;
  options.swing_speed = 0.3; options.no_player_animations = false; options.personal_model = personal;
  options.has_shadow_plane = false;
  options.weapon = weapon; options.weapon_count = weapon_count;
  size_t pass_count = 0;
  const qa_q3_character_pass *passes = qa_q3_character_presenter_frame(presenter, *character, options, &pass_count);
  size_t object = new_object(scene, character->has_opacity ? character->opacity : 1);
  for (size_t i = 0; i < pass_count; i++) {
    const qa_q3_character_pass *pass = &passes[i];
    qa_content_id content = pass->content != NULL ? pass->content : appearance_content(scene);
    pass_options pass_opts = { pass->options, pass->options_ctx };
    if (same_resource(&pass->entity.resource, &assets->lower.resource))
      append_body(state, content, &pass->entity, pass_opts, pass->shader == NULL, true, object);
    else {
      append_args args = no_args();
      args.options = pass_opts; args.has_object = true; args.object = object;
      append(state, content, &pass->entity, args);
    }
  }
}

static void prepare_held(prepare_state *state, const qa_app_qvm_held_weapon *held,
                         const qa_app_simulation_presentation *presentations, size_t presentation_count) {
  qa_app_application_world_scene *scene = state->scene;
  value_store *store = &scene->prepared;
  const qa_app_simulation_presentation *equipped = equipped_for(presentations, presentation_count, held->parent_actor);
  if (equipped == NULL) return;
  const qa_held_weapon_declaration *declaration = qa_app_foreign_held_weapons_declaration(scene->foreign_weapons, equipped);
  if (declaration != NULL && !declaration->is_model) return;
  const qa_scene_entity *parent = held->parent;
  qa_model_attachment_tag_result tag;
  if (!qa_model_attachment_tag(parent, "tag_weapon", &tag)) qa_raise("Error", 0, 0, "Original source torso has no weapon attachment");
  qa_q3_character_pass *passes; size_t pass_count;
  if (equipped->has_q3_weapon && declaration == NULL) {
    passes = selected_world(state, equipped, scene->character_assets != NULL ? &scene->character_assets->animation : NULL,
                            parent->lighting_origin, 0, false, &pass_count);
  } else {
    qa_app_held_character character = { parent->lighting_origin, { 1, 1, 1, 1 }, false, 0 };
    qa_q3_character_pass pass;
    pass_count = qa_app_foreign_held_weapons_frame(scene->foreign_weapons, equipped, character, &pass);
    passes = copy_passes(store, &pass, pass_count);
  }
  if (pass_count == 0) qa_raise("Error", 0, 0, "Selected weapon %s has no source held model", equipped->path);
  for (size_t index = 0; index < pass_count; index++) {
    const qa_q3_character_pass *pass = &passes[index];
    qa_scene_entity attached = qa_attach_scene_entity(parent, &pass->entity, tag);
    attached.flags_kind = parent->flags_kind; attached.flags_bits = parent->flags_bits;
    attached.lighting_origin = parent->lighting_origin; attached.shadow_plane = parent->shadow_plane;
    qa_content_id content = pass->content != NULL ? pass->content : equipped->content;
    pass_options inner = { pass->options, pass->options_ctx };
    if (index != 0) {
      append_args args = no_args();
      args.options = inner;
      append(state, content, &attached, args);
      continue;
    }
    for (size_t k = 0; k < held->pass_count; k++) {
      const qa_app_qvm_held_weapon_pass *source_pass = &held->passes[k];
      const char *shader = source_pass->shader;
      qa_scene_entity entity = attached;
      if (shader != NULL) {
        qa_vec4 c = source_pass->color;
        entity.color = (qa_vec4){ c.x / 255, c.y / 255, c.z / 255, c.w / 255 };
      } else entity.color = pass->entity.color;
      entity.shader_time.kind = QA_SOURCE_TIME_SECONDS; entity.shader_time.value = source_pass->shader_time;
      shader_wrap *wrap = store_alloc(store, sizeof *wrap);
      wrap->inner = inner; wrap->shader = store_text(store, shader);
      append_args args = no_args();
      args.options = (pass_options){ shader_wrap_options, wrap };
      args.shader_content = shader == NULL ? NULL : store_text(store, held->content);
      append(state, content, &entity, args);
    }
  }
}

qa_app_world_scene_prepare_options qa_app_world_scene_prepare_defaults(void) {
  qa_app_world_scene_prepare_options options;
  memset(&options, 0, sizeof options);
  options.field_of_view = 90;
  options.view_weapon_visible = true;
  return options;
}

static void free_prepare_state(void *value) {
  prepare_state *state = value;
  free(state->posed); free(state->inline_models); free(state->brushes);
}

void qa_app_application_world_scene_prepare(qa_app_application_world_scene *scene, const qa_actor_id *viewer,
    const qa_world_snapshot *snapshot, const qa_app_simulation_presentation *presentations, size_t presentation_count,
    const qa_q3_character_view *characters, size_t character_count, const qa_app_world_scene_prepare_options *options_in) {
  qa_app_world_scene_prepare_options options = options_in != NULL ? *options_in : qa_app_world_scene_prepare_defaults();
  prune_unused(scene);
  clear_frame_lists(scene);
  store_release(&scene->frame);
  store_release(&scene->prepared);
  scene->previous_time = scene->prepared_time;
  scene->prepared_time = snapshot->frame.time.kind == QA_SOURCE_TIME_SECONDS ? snapshot->frame.time.value : snapshot->frame.time.value / 1000;
  for (size_t i = 0; i < scene->group_count; i++) scene->groups[i]->pass_count = 0;
  qa_actor_id viewer_copy;
  if (viewer != NULL) viewer_copy = *viewer;
  prepare_state state;
  memset(&state, 0, sizeof state);
  state.scene = scene;
  state.viewer = viewer != NULL ? &viewer_copy : NULL;
  state.options = &options;
  /* Frees whatever the arrays hold when a raise abandons the prepare. */
  qa_cleanup state_guard;
  qa_cleanup_push(&state_guard, free_prepare_state, &state);
  for (size_t i = 0; i < presentation_count; i++) {
    prepare_source(&state, &presentations[i], presentations, presentation_count, characters, character_count);
  }
  if (scene->character_assets != NULL)
    for (size_t i = 0; i < character_count; i++) { prepare_character(&state, &characters[i], presentations, presentation_count); }
  for (size_t i = 0; i < options.held_weapon_count; i++) {
    prepare_held(&state, &options.held_weapons[i], presentations, presentation_count);
  }
  for (size_t i = 0; i < options.primary_body_count; i++) {
    const qa_app_prepared_primary_body *source = &options.primary_bodies[i];
    qa_model_source_options *value = store_alloc(&scene->prepared, sizeof *value);
    *value = source->options;
    pass_options constant = { constant_options, value };
    retain(&state, source->part, source->content, &source->entity, constant);
    if (!source->base || qa_app_body_base_visible(options.bodies, options.body_count, source->actor, source->part)) {
      append_args args = no_args();
      args.options = constant;
      args.shader_content = source->shader_content;
      args.has_time = true; args.time.kind = QA_SOURCE_TIME_MILLISECONDS; args.time.value = source->time;
      append(&state, source->content, &source->entity, args);
    }
  }
  for (size_t p = 0; p < state.posed_count; p++) {
    const posed_row *model = &state.posed[p];
    const qa_actor_id *actor = model->entity.actor;
    if (actor == NULL) continue;
    for (size_t b = 0; b < options.body_count; b++) {
      const qa_app_component_body *body = &options.bodies[b];
      if (!qa_same_actor(body->actor, *actor)) continue;
      size_t capacity = 0;
      for (size_t k = 0; k < body->part_count; k++) capacity += body->parts[k].pass_count;
      const qa_app_body_material **materials = store_alloc(&scene->prepared, (capacity == 0 ? 1 : capacity) * sizeof *materials);
      size_t material_count = qa_app_body_materials(body, model->part, materials, capacity);
      for (size_t m = 0; m < material_count; m++) {
        const qa_app_body_material *pass = materials[m];
        qa_vec4 c = pass->shader_rgba;
        qa_scene_entity entity = model->entity;
        entity.color = (qa_vec4){ c.x / 255, c.y / 255, c.z / 255, c.w / 255 };
        entity.flags_kind = "q3"; entity.flags_bits = pass->render_flags;
        entity.shader_time.kind = QA_SOURCE_TIME_SECONDS; entity.shader_time.value = pass->shader_time;
        entity.lighting_origin = pass->lighting_origin; entity.shadow_plane = pass->shadow_plane;
        body_wrap *wrap = store_alloc(&scene->prepared, sizeof *wrap);
        wrap->inner = model->options;
        wrap->custom_shader = pass->has_custom_shader ? store_text(&scene->prepared, pass->custom_shader_name) : NULL;
        if (pass->custom_skin != NULL && pass->custom_skin->surface_count > 0) {
          qa_model_custom_skin_entry *entries = store_alloc(&scene->prepared, pass->custom_skin->surface_count * sizeof *entries);
          for (size_t s = 0; s < pass->custom_skin->surface_count; s++)
            entries[s] = (qa_model_custom_skin_entry){ store_text(&scene->prepared, pass->custom_skin->surfaces[s].name),
                                                       store_text(&scene->prepared, pass->custom_skin->surfaces[s].shader) };
          wrap->custom_skin = entries; wrap->custom_skin_count = pass->custom_skin->surface_count;
        } else if (pass->custom_skin != NULL) {
          wrap->custom_skin = store_alloc(&scene->prepared, sizeof *wrap->custom_skin); wrap->custom_skin_count = 0;
        }
        wrap->shader_tex_coord = pass->shader_tex_coord;
        wrap->non_normalized_axes = pass->non_normalized_axes;
        append_args args = no_args();
        args.options = (pass_options){ body_wrap_options, wrap };
        args.shader_content = body->content;
        args.has_time = true; args.time.kind = QA_SOURCE_TIME_MILLISECONDS; args.time.value = body->time;
        append(&state, model->content, &entity, args);
      }
    }
  }
  qa_cleanup_pop(&state_guard);
  free(state.posed);
  free(scene->inline_models);
  scene->inline_models = state.inline_models; scene->inline_model_count = state.inline_count;
  free(scene->brush_models);
  scene->brush_models = state.brushes; scene->brush_model_count = state.brush_count;
  for (size_t g = 0; g < scene->group_count; g++) {
    scene_group *group = scene->groups[g];
    for (size_t k = 0; k < group->pass_count; k++) {
      scene_pass *pass = &scene->passes[group->passes[k]];
      qa_scene_model_renderer_preload(&group->renderer, &pass->entity, 1, pass->options.fn, pass->options.ctx);
    }
  }
}

/* ---- view ---- */
static qa_sequence_phase phase_named(const char *name) {
  if (name != NULL && strcmp(name, "sky") == 0) return QA_SEQUENCE_SKY;
  if (name != NULL && strcmp(name, "opaque") == 0) return QA_SEQUENCE_OPAQUE;
  return QA_SEQUENCE_TRANSLUCENT;
}
static void release_group(void *value) { qa_scene_group_destroy(value); free(value); }
static void release_model_groups(void *value) {
  qa_scene_model_group *groups = value;
  size_t count = 0;
  while (groups[count].kind != NULL) count++;
  qa_scene_model_groups_destroy(groups, count);
}
/* A TS SceneGroup over one model group's batches. */
static qa_scene_operation model_group_operation(value_store *store, const qa_scene_model_group *group) {
  qa_scene_group *wrapped = calloc(1, sizeof *wrapped);
  if (wrapped == NULL) qa_range_error("Out of memory");
  if (strcmp(group->kind, "compiled") == 0) *wrapped = qa_compiled_draw_group(group->material, group->batches, group->batch_count);
  else if (strcmp(group->kind, "source") == 0) {
    qa_source_surface_order order = { group->source_view, group->source_entity, group->surface, group->fog, group->dlight };
    *wrapped = qa_source_draw_group(group->source_material, order, group->batches, group->batch_count);
  } else *wrapped = qa_sequence_draw_group(phase_named(group->sequence), group->batches, group->batch_count);
  store_add(store, release_group, wrapped);
  qa_scene_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.is_group = true;
  operation.group = *wrapped;
  return operation;
}
typedef struct operation_list { qa_scene_operation *items; size_t count, capacity; } operation_list;
static void list_push(operation_list *list, qa_scene_operation operation) {
  GROW(list->items, list->count, list->capacity, 32);
  list->items[list->count++] = operation;
}
static void list_append(operation_list *list, const qa_scene_operation *items, size_t count) {
  for (size_t i = 0; i < count; i++) list_push(list, items[i]);
}
/* Keeps a renderer prepare result for the frame; returns its groups. The
 * array gets a zeroed sentinel so its release can find the count. */
static qa_scene_model_group *keep_groups(value_store *store, qa_scene_model_group *groups, size_t count) {
  qa_scene_model_group *kept = realloc(groups, (count + 1) * sizeof *kept);
  if (kept == NULL) qa_range_error("Out of memory");
  memset(&kept[count], 0, sizeof kept[count]);
  store_add(store, release_model_groups, kept);
  return kept;
}
static qa_scene_operation sequence_operation(value_store *store, qa_sequence_phase phase, const qa_draw_batch *batches, size_t count) {
  qa_scene_group *wrapped = calloc(1, sizeof *wrapped);
  if (wrapped == NULL) qa_range_error("Out of memory");
  *wrapped = qa_sequence_draw_group(phase, batches, count);
  store_add(store, release_group, wrapped);
  qa_scene_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.is_group = true;
  operation.group = *wrapped;
  return operation;
}
static bool polygon_operation(const qa_scene_operation *operation) {
  return operation->is_group && operation->group.order.kind == QA_GROUP_SOURCE && !operation->group.order.source.entity.is_refentity;
}

typedef struct planar_ctx {
  pass_options inner;
  bool infrared, no_world_model, planar_shadow;
} planar_ctx;
static qa_model_source_options planar_options(void *ctx, const qa_scene_entity *entity) {
  const planar_ctx *planar = ctx;
  qa_model_source_options options = options_of(planar->inner, entity);
  options.has_infrared = true; options.infrared = planar->infrared;
  options.has_no_world_model = true; options.no_world_model = planar->no_world_model;
  options.has_planar_shadow = true; options.planar_shadow = planar->planar_shadow;
  return options;
}
/* TS pass.resolve(camera, purpose) or pass.entity; false is TS null. */
static bool resolve_pass(const scene_pass *pass, qa_vec3 camera, const char *purpose, qa_scene_entity *out) {
  if (pass->resolve == RESOLVE_NATIVE) return qa_app_resolved_held_entity_resolve(pass->native, camera, purpose, out);
  *out = pass->entity;
  if (pass->resolve == RESOLVE_ANCHORED && strcmp(purpose, "view") == 0) {
    qa_app_view_weapon_entity anchored = { pass->entity.transform, pass->entity.previous_origin, pass->entity.lighting_origin,
                                           pass->entity.shadow_plane };
    anchored = qa_app_anchored_view_weapon(anchored, pass->anchor, camera);
    out->transform = anchored.transform; out->previous_origin = anchored.previous_origin;
    out->lighting_origin = anchored.lighting_origin; out->shadow_plane = anchored.shadow_plane;
  }
  return true;
}
/* TS modelOperations prepare(group, pass): appends the pass's model groups. */
static void prepare_pass_groups(qa_app_application_world_scene *scene, const qa_world_view_input *input, bool infrared,
                                const qa_scene_camera *weapon_camera, bool anchor_view_rotation, const scene_pass *pass,
                                qa_scene_model_group **out_groups, size_t *out_count) {
  *out_groups = NULL; *out_count = 0;
  qa_scene_entity entity;
  if (!resolve_pass(pass, input->camera.origin, "view", &entity)) return;
  bool view_model = view_model_of(pass->options, &entity);
  if (view_model && input->camera.clip_is_portal) return;
  if (pass->personal && !input->camera.clip_is_portal) return;
  if (anchor_view_rotation && pass->has_view_axis && view_model)
    entity.transform.axis = qa_app_anchored_view_weapon_axis(entity.transform.axis, pass->view_axis, weapon_camera->axis);
  qa_world_view_input current = *input;
  if (pass->has_time) current.time = pass->time;
  if (view_model) current.camera = *weapon_camera;
  planar_ctx planar = { pass->options, infrared, input->no_world_model,
                        !input->no_world_model && scene->planar_shadows(scene->planar_shadows_ctx) };
  size_t count = 0;
  qa_scene_model_group *groups = qa_scene_model_renderer_prepare(&scene->groups[pass->group]->renderer, &entity, 1, &current,
                                                                 planar_options, &planar, NULL, &count);
  *out_groups = keep_groups(&scene->frame, groups, count);
  *out_count = count;
}
static void model_operations(qa_app_application_world_scene *scene, const qa_world_view_input *input, bool infrared,
                             const qa_scene_camera *weapon_camera, bool anchor_view_rotation, operation_list *out) {
  value_store *store = &scene->frame;
  bool *emitted = scene->object_count == 0 ? NULL : calloc(scene->object_count, sizeof *emitted);
  if (scene->object_count != 0 && emitted == NULL) qa_range_error("Out of memory");
  qa_cleanup emitted_guard;
  qa_cleanup_push(&emitted_guard, free, emitted);
  operation_list model = {0};
  for (size_t i = 0; i < scene->pass_count; i++) {
    const scene_pass *pass = &scene->passes[i];
    const scene_object *object = &scene->objects[pass->object];
    if (object->opacity == 1) {
      qa_scene_model_group *groups; size_t count;
      prepare_pass_groups(scene, input, infrared, weapon_camera, anchor_view_rotation, pass, &groups, &count);
      for (size_t g = 0; g < count; g++) list_push(&model, model_group_operation(store, &groups[g]));
      continue;
    }
    if (emitted[pass->object]) continue;
    emitted[pass->object] = true;
    if (object->opacity == 0) continue;
    size_t batch_count = 0, batch_capacity = 0;
    qa_draw_batch *batches = NULL;
    for (size_t k = 0; k < object->pass_count; k++) {
      qa_scene_model_group *groups; size_t count;
      prepare_pass_groups(scene, input, infrared, weapon_camera, anchor_view_rotation, &scene->passes[object->passes[k]], &groups, &count);
      for (size_t g = 0; g < count; g++)
        for (size_t b = 0; b < groups[g].batch_count; b++) {
          GROW(batches, batch_count, batch_capacity, 8);
          batches[batch_count++] = groups[g].batches[b];
        }
    }
    if (batches != NULL) store_add(store, free, batches);
    qa_render_operation *opacity = store_alloc(store, sizeof *opacity);
    opacity->kind = QA_RENDER_OP_OBJECT_OPACITY; opacity->opacity = object->opacity;
    opacity->batches = batches; opacity->batch_count = batch_count;
    qa_scene_group *group = store_alloc(store, sizeof *group);
    group->order.kind = QA_GROUP_SEQUENCE; group->order.phase = QA_SEQUENCE_TRANSLUCENT;
    group->operations = opacity; group->operation_count = 1;
    qa_scene_operation operation; memset(&operation, 0, sizeof operation);
    operation.is_group = true; operation.group = *group;
    list_push(&model, operation);
  }
  qa_cleanup_pop(&emitted_guard);
  free(emitted);
  if (scene->flare_count > 0) {
    qa_draw_batch *batches = store_alloc(store, scene->flare_count * sizeof *batches);
    for (size_t i = 0; i < scene->flare_count; i++) {
      qa_scene_owned_batch *owned = store_alloc(store, sizeof *owned);
      if (!qa_prepare_flare(scene->flares[i].flare, scene->flares[i].origin, input->camera, scene->flares[i].image,
                            scene->flares[i].image_path, owned)) qa_range_error("Out of memory");
      store_add(store, (void (*)(void *))qa_scene_owned_batch_destroy, owned);
      batches[i] = owned->batch;
    }
    list_push(&model, sequence_operation(store, QA_SEQUENCE_TRANSLUCENT, batches, scene->flare_count));
  }
  qa_world_scene *world = qa_app_application_assets_world(scene->assets);
  for (size_t i = 0; i < scene->beam_count; i++) {
    const beam_row *beam = &scene->beams[i];
    qa_rail_pose pose = { "rail-core", beam->end, beam->origin, { 255, 255, 255, 255 } };
    qa_rail_settings settings = { beam->width, 16, 32 };
    qa_entity_geometry *geometry = store_alloc(store, sizeof *geometry);
    *geometry = qa_rail_geometry(pose, input->camera.origin, &settings);
    store_add(store, (void (*)(void *))qa_entity_geometry_destroy, geometry);
    qa_world_material_context *context = store_alloc(store, sizeof *context);
    qa_world_scene_material_context(world, input, NULL, NULL, context);
    qa_material_geometry material_geometry = { geometry->vertices, geometry->vertex_count, geometry->indices, geometry->index_count };
    size_t count = 0;
    qa_draw_batch *batches = qa_prepare_material_batches(qa_registered_scene_material_compiled(beam->material), material_geometry,
                                                         &context->context, &count);
    if (batches != NULL) store_add(store, free, batches);
    list_push(&model, sequence_operation(store, QA_SEQUENCE_TRANSLUCENT, batches, count));
  }
  for (size_t i = 0; i < scene->brush_model_count; i++) {
    const brush_presentation *brush = &scene->brush_models[i];
    qa_world_view_input brush_input = *input;
    brush_input.has_animation_frame = true; brush_input.animation_frame = brush->frame;
    brush_input.has_alternate_animation = true;
    brush_input.alternate_animation = brush->scene->map.kind == QA_DECODED_WORLD_Q1 && brush->frame != 0;
    brush_input.has_material_entity_rgba = true;
    brush_input.material_entity_rgba = (qa_vec4){ 255, 255, 255, brush->alpha * 255 };
    qa_world_operations *operations = store_alloc(store, sizeof *operations);
    qa_world_scene_prepare_model(brush->scene, brush->model, brush->transform, &brush_input, NULL, operations);
    store_add(store, (void (*)(void *))qa_world_operations_destroy, operations);
    list_append(out, operations->operations, operations->count);
  }
  list_append(out, model.items, model.count);
  free(model.items);
}

typedef struct retain_ctx { qa_shadow_body_filter filter; } retain_ctx;
static bool retain_body(void *ctx, qa_render_scene_shadow_sphere sphere) {
  const retain_ctx *retain_state = ctx;
  qa_shadow_sphere value = { sphere.origin, sphere.radius };
  return qa_shadow_body_filter_test(retain_state->filter, value);
}
static void release_casters(void *value) {
  /* value: a zero-radius-terminated record list made by keep_casters. */
  struct caster_block { qa_render_scene_shadow_caster *casters; size_t count; } *block = value;
  qa_render_scene_shadow_casters_destroy(block->casters, block->count);
  free(block);
}

void qa_app_application_world_scene_view(qa_app_application_world_scene *scene, const qa_world_view_input *input_in,
    const qa_scene_operation *operations, size_t operation_count, const qa_scene_light *shadow_lights,
    size_t shadow_light_count, bool infrared, const qa_scene_camera *weapon_camera_in, qa_prepared_world_view *out) {
  value_store *store = &scene->frame;
  qa_scene_camera weapon_camera = weapon_camera_in != NULL ? *weapon_camera_in : input_in->camera;
  qa_world_view_input input = *input_in;
  if (input.source == NULL) {
    qa_source_scene_order *order = store_alloc(store, sizeof *order);
    qa_source_scene_order_init(order, qa_app_application_assets_material_registrations(scene->assets));
    qa_world_surface_admission *admission = store_alloc(store, sizeof *admission);
    qa_create_world_surface_admission(order, admission);
    store_add(store, (void (*)(void *))qa_world_surface_admission_destroy, admission);
    input.source = admission;
  }
  input.inline_models = scene->inline_models; input.inline_model_count = scene->inline_model_count;
  qa_app_world_scene_styles styles = qa_app_application_world_scene_styles(scene);
  input.q1_styles = styles.q1_styles; input.q1_style_count = styles.q1_style_count;
  input.q2_styles = styles.q2_styles; input.q2_style_count = styles.q2_style_count;
  qa_world_scene *world = qa_app_application_assets_world(scene->assets);
  if (shadow_light_count > 0 && !input.no_world_model) {
    retain_ctx retain_state = { qa_shadow_body_filter_new(shadow_lights, shadow_light_count) };
    size_t caster_count = 0, caster_capacity = 0;
    qa_shadow_caster *casters = NULL;
    for (size_t g = 0; g < scene->group_count; g++) {
      scene_group *group = scene->groups[g];
      for (size_t k = 0; k < group->pass_count; k++) {
        const scene_pass *pass = &scene->passes[group->passes[k]];
        if (scene->objects[pass->object].opacity != 1 || (pass->personal && !input.camera.clip_is_portal)) continue;
        qa_scene_entity entity;
        if (!resolve_pass(pass, input.camera.origin, "shadow", &entity)) continue;
        qa_world_view_input current = input;
        if (pass->has_time) current.time = pass->time;
        size_t count = 0;
        qa_render_scene_shadow_caster *prepared = qa_scene_model_renderer_prepare_shadow_casters(&group->renderer, &entity, 1, &current,
            pass->options.fn, pass->options.ctx, NULL, retain_body, &retain_state, &count);
        struct caster_block { qa_render_scene_shadow_caster *casters; size_t count; } *block = malloc(sizeof *block);
        if (block == NULL) qa_range_error("Out of memory");
        block->casters = prepared; block->count = count;
        store_add(store, release_casters, block);
        for (size_t c = 0; c < count; c++) {
          qa_shadow_mesh *meshes = store_alloc(store, prepared[c].mesh_count * sizeof *meshes);
          for (size_t m = 0; m < prepared[c].mesh_count; m++) {
            const qa_render_scene_shadow_mesh *mesh = &prepared[c].meshes[m];
            meshes[m] = (qa_shadow_mesh){ mesh->positions, mesh->position_count,
                                          mesh->owned_indices != NULL ? mesh->owned_indices : mesh->indices, mesh->index_count };
          }
          GROW(casters, caster_count, caster_capacity, 16);
          casters[caster_count++] = (qa_shadow_caster){ prepared[c].origin, prepared[c].radius, meshes, prepared[c].mesh_count };
        }
      }
    }
    if (casters != NULL) store_add(store, free, casters);
    size_t light_count = shadow_light_count + input.light_count;
    qa_scene_light *lights = store_alloc(store, (light_count == 0 ? 1 : light_count) * sizeof *lights);
    memcpy(lights, shadow_lights, shadow_light_count * sizeof *lights);
    for (size_t i = 0; i < input.light_count; i++) {
      qa_scene_light *light = &lights[shadow_light_count + i];
      memset(light, 0, sizeof *light);
      light->origin = input.lights[i].origin; light->radius = input.lights[i].radius; light->color = input.lights[i].color;
      light->additive = true;
      light->profile.kind = "q2"; light->profile.scale = 1; light->profile.has_cone = false; light->profile.shadow_cast = false;
    }
    qa_prepared_shadows *shadows = store_alloc(store, sizeof *shadows);
    if (!qa_world_scene_prepare_shadows(world, lights, light_count, &input, casters, caster_count, NULL, shadows))
      qa_range_error("Out of memory");
    store_add(store, (void (*)(void *))qa_prepared_shadows_destroy, shadows);
    input.has_q2_fragment_lighting = true;
    input.fragment_lights = shadows->lighting.lights; input.fragment_light_count = shadows->lighting.light_count;
    input.fragment_atlas = shadows->lighting_atlas;
    input.before_view = shadows->operations; input.before_view_count = shadows->operation_count;
  }
  qa_world_operations world_operations;
  qa_world_scene_prepare_world_operations(world, &input, NULL, &world_operations);
  qa_world_operations_destroy(&world_operations);
  operation_list models = {0};
  model_operations(scene, &input, infrared, &weapon_camera, false, &models);
  if (models.items != NULL) store_add(store, free, models.items);
  operation_list merged = {0};
  for (size_t i = 0; i < operation_count; i++) if (polygon_operation(&operations[i])) list_push(&merged, operations[i]);
  list_append(&merged, models.items, models.count);
  for (size_t i = 0; i < operation_count; i++) if (!polygon_operation(&operations[i])) list_push(&merged, operations[i]);
  if (merged.items != NULL) store_add(store, free, merged.items);
  input.operations = merged.items; input.operation_count = merged.count;
  qa_world_scene_prepare_view(world, &input, out);
}

const qa_scene_operation *qa_app_application_world_scene_supplemental(qa_app_application_world_scene *scene,
    const qa_world_view_input *input, const qa_scene_camera *weapon_camera_in, bool infrared, size_t *out_count) {
  value_store *store = &scene->frame;
  qa_scene_camera weapon_camera = weapon_camera_in != NULL ? *weapon_camera_in : input->camera;
  qa_world_scene *world = qa_app_application_assets_world(scene->assets);
  operation_list out = {0};
  for (size_t i = 0; i < scene->inline_model_count; i++) {
    const qa_world_inline_model *model = &scene->inline_models[i];
    qa_world_view_input model_input = *input;
    if (model->has_animation_frame) { model_input.has_animation_frame = true; model_input.animation_frame = model->animation_frame; }
    if (model->has_alternate_animation) { model_input.has_alternate_animation = true; model_input.alternate_animation = model->alternate_animation; }
    qa_world_operations *operations = store_alloc(store, sizeof *operations);
    qa_world_scene_prepare_model(world, model->model, model->transform, &model_input, NULL, operations);
    store_add(store, (void (*)(void *))qa_world_operations_destroy, operations);
    list_append(&out, operations->operations, operations->count);
  }
  operation_list models = {0};
  model_operations(scene, input, infrared, &weapon_camera, true, &models);
  if (models.items != NULL) store_add(store, free, models.items);
  list_append(&out, models.items, models.count);
  if (out.items != NULL) store_add(store, free, out.items);
  *out_count = out.count;
  return out.items;
}

const qa_scene_operation *qa_app_application_world_scene_effect_operations(qa_app_application_world_scene *scene,
    const qa_app_effect_operations *operations, size_t *out_count) {
  value_store *store = &scene->frame;
  operation_list out = {0};
  for (size_t i = 0; i < operations->count; i++) {
    const qa_app_effect_operation *operation = &operations->items[i];
    list_push(&out, operation->is_model_group ? model_group_operation(store, operation->model_group) : operation->scene);
  }
  if (out.items != NULL) store_add(store, free, out.items);
  *out_count = out.count;
  return out.items;
}
