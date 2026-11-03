/* Port of src/app/bootstrap/network/unified-frame-values.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/held_weapon.h"
#include "qa/content/item_icon.h"
#include "qa/foundation/support.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/save_image.h"
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static char *copy_text(const char *s) {
  size_t n = strlen(s) + 1;
  char *out = xmalloc(n);
  memcpy(out, s, n);
  return out;
}
static qa_save_reader field_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("out of memory");
  return out;
}
static bool has_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  *out = field_of(reader, name);
  if (qa_save_reader_is_undefined(out)) {
    qa_save_reader_destroy(out);
    return false;
  }
  return true;
}
static qa_actor_id actor_field(const qa_save_reader *reader, const char *name,
    const qa_app_unified_identity_decoder *identity);
static double finite_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  double value = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  return value;
}
qa_save_value *qa_app_unified_wire_actor(qa_actor_id actor) {
  qa_save_value *object = qa_save_value_object();
  if (!object) return NULL;
  if (!qa_save_value_set(object, "slot", qa_save_value_number((double)actor.slot))
      || !qa_save_value_set(object, "generation", qa_save_value_number((double)actor.generation))) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}
qa_actor_id qa_app_unified_read_actor(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity) {
  qa_save_reader slot = field_of(reader, "slot"), generation = field_of(reader, "generation");
  double s = qa_save_reader_integer_min(&slot, 0);
  double g = qa_save_reader_integer_min(&generation, 0);
  qa_save_reader_destroy(&slot); qa_save_reader_destroy(&generation);
  return identity->actor(identity->ctx, s, g);
}
double qa_app_unified_non_negative(const qa_save_reader *reader) {
  double value = qa_save_reader_finite(reader);
  if (!(value >= 0)) qa_save_reader_fail(reader, "expected a non-negative number");
  return value;
}
double qa_app_unified_positive(const qa_save_reader *reader) {
  double value = qa_save_reader_finite(reader);
  if (!(value > 0)) qa_save_reader_fail(reader, "expected a positive number");
  return value;
}
void qa_app_unified_finite_vectors(const qa_save_reader *reader, const qa_vec3 *vectors, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (!isfinite(vectors[i].x) || !isfinite(vectors[i].y) || !isfinite(vectors[i].z))
      qa_save_reader_fail(reader, "nonfinite source player vector");
}
static qa_actor_id actor_field(const qa_save_reader *reader, const char *name,
    const qa_app_unified_identity_decoder *identity) {
  qa_save_reader f = field_of(reader, name);
  return qa_app_unified_read_actor(&f, identity);
}
qa_vec3 qa_app_unified_read_vector(const qa_save_reader *reader) {
  qa_vec3 out = {finite_field(reader, "x"), finite_field(reader, "y"), finite_field(reader, "z")};
  return out;
}
qa_vec4 qa_app_unified_read_color(const qa_save_reader *reader) {
  qa_vec3 base = qa_app_unified_read_vector(reader);
  qa_vec4 out = {base.x, base.y, base.z, finite_field(reader, "w")};
  return out;
}
void qa_app_unified_read_axis(const qa_save_reader *reader, qa_vec3 out[3]) {
  size_t count = qa_save_reader_list_count(reader);
  if (count != 3) qa_save_reader_fail(reader, "expected three axes");
  for (size_t i = 0; i < 3; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(reader, i, &item)) qa_range_error("out of memory");
    out[i] = qa_app_unified_read_vector(&item);
    qa_save_reader_destroy(&item);
  }
}
qa_app_sim_player_view qa_app_unified_read_player_view(const qa_save_reader *reader) {
  qa_app_sim_player_view view = {0};
  qa_save_reader f;
  if (has_field(reader, "blend", &f)) {
    view.blend = qa_app_unified_read_color(&f); view.has_blend = true;
    qa_save_reader_destroy(&f);
  }
  if (has_field(reader, "damageBlend", &f)) {
    view.damage_blend = qa_app_unified_read_color(&f); view.has_damage_blend = true;
    qa_save_reader_destroy(&f);
  }
  if (has_field(reader, "kickAngles", &f)) {
    view.kick_angles = qa_app_unified_read_vector(&f); view.has_kick_angles = true;
    qa_save_reader_destroy(&f);
  }
  if (has_field(reader, "fieldOfView", &f)) {
    view.field_of_view = qa_save_reader_finite(&f); view.has_field_of_view = true;
    qa_save_reader_destroy(&f);
  }
  if (has_field(reader, "foreignCharacterDeath", &f)) {
    view.foreign_character_death = qa_save_reader_literal_boolean(&f, true);
    qa_save_reader_destroy(&f);
  }
  if (has_field(reader, "clientViewOffsetDelta", &f)) {
    view.client_view_offset_delta = qa_app_unified_read_vector(&f);
    view.has_client_view_offset_delta = true;
    qa_save_reader_destroy(&f);
  }
  if (has_field(reader, "pitchDrift", &f)) {
    view.has_pitch_drift = true;
    qa_save_reader g = field_of(&f, "grounded");
    view.pitch_drift_grounded = qa_save_reader_boolean(&g); qa_save_reader_destroy(&g);
    view.pitch_drift_ideal_pitch = finite_field(&f, "idealPitch");
    g = field_of(&f, "disabled");
    view.pitch_drift_disabled = qa_save_reader_boolean(&g); qa_save_reader_destroy(&g);
    qa_save_reader_destroy(&f);
  }
  qa_save_reader origin = field_of(reader, "origin"), angles = field_of(reader, "angles");
  view.origin = qa_app_unified_read_vector(&origin);
  view.angles = qa_app_unified_read_vector(&angles);
  qa_save_reader_destroy(&origin); qa_save_reader_destroy(&angles);
  view.view_height = finite_field(reader, "viewHeight");
  return view;
}
static const char *namespaced_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  const char *value = qa_namespaced(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_save_value *clone_value(const qa_save_value *value) {
  if (!value) return qa_save_value_undefined();
  qa_save_value *out = NULL;
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: out = qa_save_value_undefined(); break;
    case QA_SAVE_NULL: out = qa_save_value_null(); break;
    case QA_SAVE_BOOLEAN: out = qa_save_value_boolean(value->boolean); break;
    case QA_SAVE_NUMBER: out = qa_save_value_number(value->number); break;
    case QA_SAVE_BIGINT: out = qa_save_value_bigint_text(value->text); break;
    case QA_SAVE_STRING: out = qa_save_value_string(value->text); break;
    case QA_SAVE_BYTES: out = qa_save_value_bytes(value->bytes, value->byte_length); break;
    case QA_SAVE_ARRAY:
      out = qa_save_value_array();
      if (out) for (size_t i = 0; i < value->item_count; i++)
        if (!qa_save_value_push(out, clone_value(value->items[i]))) { qa_save_value_free(out); return NULL; }
      break;
    case QA_SAVE_OBJECT:
      out = qa_save_value_object();
      if (out) for (size_t i = 0; i < value->entry_count; i++)
        if (!qa_save_value_set(out, value->entries[i].key, clone_value(value->entries[i].value))) {
          qa_save_value_free(out); return NULL;
        }
      break;
  }
  if (!out) qa_range_error("out of memory");
  return out;
}
static void validate_native_inventory(const qa_save_reader *reader) {
  qa_save_reader presentation;
  if (has_field(reader, "presentation", &presentation)) {
    qa_save_reader source_reader = field_of(&presentation, "source");
    qa_provider_reference source = qa_read_provider(&source_reader);
    qa_save_reader_destroy(&source_reader);
    static const char *kinds[] = {"weapon", "ammunition", "item"};
    qa_save_reader kind_reader = field_of(&presentation, "kind");
    const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 3);
    qa_save_reader_destroy(&kind_reader);
    if (strcmp(kind, "item") == 0) {
      qa_save_reader icon = field_of(&presentation, "icon");
      if (!qa_save_reader_is_null(&icon)) {
        qa_weapon_hud_icon declaration;
        qa_read_source_item_icon(&icon, source.content, &declaration);
        qa_content_weapon_hud_icon_free(&declaration);
      }
      qa_save_reader_destroy(&icon);
    } else {
      namespaced_field(&presentation, "weapon");
    }
    qa_save_reader_destroy(&presentation);
  }
  qa_save_reader items = field_of(reader, "items");
  size_t count = qa_save_reader_list_count(&items);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&items, i, &row)) qa_range_error("out of memory");
    namespaced_field(&row, "item");
    qa_save_reader label = field_of(&row, "label");
    qa_save_reader_string(&label); qa_save_reader_destroy(&label);
    finite_field(&row, "count");
    qa_save_reader_destroy(&row);
  }
  qa_save_reader_destroy(&items);
  qa_save_reader selected = field_of(reader, "selected");
  if (!qa_save_reader_is_null(&selected)) qa_namespaced(&selected);
  qa_save_reader_destroy(&selected);
}
static void cleanup_ui(void *arg) { qa_app_unified_player_ui_free(arg); }
static double integer_field(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader f = field_of(reader, name);
  return qa_save_reader_integer_min(&f, minimum);
}
static bool boolean_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  return qa_save_reader_boolean(&f);
}
static const char *string_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  return qa_save_reader_string(&f);
}
static qa_vec3 vector_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  return qa_app_unified_read_vector(&f);
}
#define SAFE_MIN (-9007199254740991.0)
qa_app_sim_player_ui qa_app_unified_read_player_ui(const qa_save_reader *reader) {
  qa_app_sim_player_ui ui;
  memset(&ui, 0, sizeof ui);
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_ui, &ui);
  qa_save_reader f;
  if (has_field(reader, "selectedArsenal", &f)) ui.selected_arsenal = qa_save_reader_literal_boolean(&f, true);
  if (has_field(reader, "nativeInventory", &f)) {
    validate_native_inventory(&f);
    ui.native_inventory = clone_value(f.value);
  }
  ui.health = finite_field(reader, "health");
  f = field_of(reader, "armor");
  ui.armor = qa_read_armor(&f);
  f = field_of(reader, "activeWeapon");
  ui.active_weapon = qa_save_reader_is_null(&f) ? NULL : qa_namespaced(&f);
  f = field_of(reader, "ammo");
  if (!qa_save_reader_is_null(&f)) {
    ui.ammo_item = namespaced_field(&f, "item");
    ui.ammo_count = finite_field(&f, "count");
    ui.has_ammo = true;
  }
  f = field_of(reader, "inventory");
  size_t inv_count = qa_save_reader_list_count(&f);
  qa_inventory_entry *inventory = calloc(inv_count ? inv_count : 1, sizeof *inventory);
  if (!inventory) qa_range_error("out of memory");
  ui.inventory = inventory; ui.inventory_count = inv_count;
  for (size_t i = 0; i < inv_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&f, i, &row)) qa_range_error("out of memory");
    inventory[i] = qa_read_inventory_entry(&row);
  }
  static const char *warnings[] = {"none", "low", "empty"};
  f = field_of(reader, "arsenalWarning");
  ui.arsenal_warning = qa_save_reader_choice_string(&f, warnings, 3);
  f = field_of(reader, "powerups");
  size_t powerup_count = qa_save_reader_list_count(&f);
  qa_active_powerup_timer *powerups = calloc(powerup_count ? powerup_count : 1, sizeof *powerups);
  if (!powerups) qa_range_error("out of memory");
  ui.powerups = powerups; ui.powerup_count = powerup_count;
  for (size_t i = 0; i < powerup_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&f, i, &row)) qa_range_error("out of memory");
    powerups[i].item = namespaced_field(&row, "item");
    powerups[i].label = string_field(&row, "label");
    powerups[i].remaining_seconds = finite_field(&row, "remainingSeconds");
  }
  f = field_of(reader, "items");
  size_t item_count = qa_save_reader_list_count(&f);
  qa_app_sim_player_ui_item *items = calloc(item_count ? item_count : 1, sizeof *items);
  if (!items) qa_range_error("out of memory");
  ui.items = items; ui.item_count = item_count;
  static const char *item_kinds[] = {"weapon", "powerup"};
  for (size_t i = 0; i < item_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&f, i, &row)) qa_range_error("out of memory");
    items[i].id = namespaced_field(&row, "id");
    items[i].label = string_field(&row, "label");
    qa_save_reader kind = field_of(&row, "kind");
    items[i].kind = qa_save_reader_choice_string(&kind, item_kinds, 2);
    items[i].source_ordinal = integer_field(&row, "sourceOrdinal", SAFE_MIN);
    items[i].owned = boolean_field(&row, "owned");
    items[i].has_ammo = boolean_field(&row, "hasAmmo");
    qa_save_reader count_field = field_of(&row, "count");
    if (!qa_save_reader_is_null(&count_field)) {
      items[i].count = qa_save_reader_finite(&count_field);
      items[i].has_count = true;
    }
    items[i].warning_count = finite_field(&row, "warningCount");
  }
  f = field_of(reader, "weaponStatus");
  if (!qa_save_reader_is_null(&f)) {
    qa_weapon_hud_status *status = calloc(1, sizeof *status);
    if (!status) qa_range_error("out of memory");
    ui.weapon_status = status;
    qa_save_reader ammo = field_of(&f, "ammo");
    static const char *ammo_kinds[] = {"unmetered", "finite"};
    qa_save_reader kind_reader = field_of(&ammo, "kind");
    const char *kind = qa_save_reader_choice_string(&kind_reader, ammo_kinds, 2);
    qa_save_reader source_reader = field_of(&f, "source");
    status->source = qa_read_provider(&source_reader);
    status->item = namespaced_field(&f, "item");
    status->label = string_field(&f, "label");
    status->ammo_is_finite = strcmp(kind, "finite") == 0;
    if (status->ammo_is_finite) {
      status->ammo_item = namespaced_field(&ammo, "item");
      status->ammo_count = finite_field(&ammo, "count");
      status->has_ammo_to_start = boolean_field(&ammo, "hasAmmoToStart");
      status->low = boolean_field(&ammo, "low");
    }
  }
  qa_cleanup_pop(&guard);
  return ui;
}
void qa_app_unified_player_ui_free(qa_app_sim_player_ui *ui) {
  qa_save_value_free((qa_save_value *)ui->native_inventory);
  free((void *)ui->inventory);
  free((void *)ui->powerups);
  free((void *)ui->items);
  free((void *)ui->weapon_status);
  memset(ui, 0, sizeof *ui);
}
static void cleanup_model(void *arg) { qa_app_unified_model_free(arg); }
qa_app_simulation_presentation qa_app_unified_read_model(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity) {
  qa_app_simulation_presentation model;
  memset(&model, 0, sizeof model);
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_model, &model);
  qa_save_reader f;
  if (has_field(reader, "heldWeapon", &f)) {
    qa_held_weapon_declaration *declaration = calloc(1, sizeof *declaration);
    if (!declaration) qa_range_error("out of memory");
    model.held_weapon = declaration;
    if (!qa_read_held_weapon_declaration(&f, declaration)) qa_range_error("out of memory");
  }
  if (has_field(reader, "nativeHeldWeapon", &f)) model.native_held_weapon = qa_save_reader_literal_boolean(&f, true);
  if (has_field(reader, "weaponItem", &f)) model.weapon_item = qa_namespaced(&f);
  if (has_field(reader, "flare", &f)) {
    model.flare.image = string_field(&f, "image");
    model.flare.fade_start = finite_field(&f, "fadeStart");
    model.flare.fade_end = finite_field(&f, "fadeEnd");
    model.flare.scale = finite_field(&f, "scale");
    model.flare.color = vector_field(&f, "color");
    qa_save_reader rim = field_of(&f, "rimColor");
    if (!qa_save_reader_is_null(&rim)) {
      qa_vec3 color = qa_app_unified_read_vector(&rim);
      qa_vec3 *rim_owned = xmalloc(sizeof *rim_owned);
      *rim_owned = color;
      model.flare.rim_color = rim_owned;
    }
    model.flare.lock_angle = boolean_field(&f, "lockAngle");
    model.has_flare = true;
  }
  if (has_field(reader, "backLerp", &f)) {
    model.back_lerp = qa_save_reader_finite(&f); model.has_back_lerp = true;
  }
  if (has_field(reader, "skinPath", &f)) {
    model.skin_path = qa_save_reader_is_null(&f) ? NULL : qa_save_reader_string(&f);
    model.has_skin_path = true;
  }
  if (has_field(reader, "indexedSkin", &f)) {
    double width = integer_field(&f, "width", 1);
    double height = integer_field(&f, "height", 1);
    qa_save_reader pixels_reader = field_of(&f, "pixels");
    size_t length = 0;
    const uint8_t *pixels = qa_save_reader_bytes(&pixels_reader, &length);
    if ((double)length != width * height) qa_save_reader_fail(&f, "indexed skin length differs");
    const char *name = string_field(&f, "name");
    uint8_t *owned = xmalloc(length);
    if (length) memcpy(owned, pixels, length);
    model.indexed_skin.pixels.bytes = owned; model.indexed_skin.pixels.length = length;
    model.indexed_skin.name = copy_text(name);
    model.indexed_skin.width = width; model.indexed_skin.height = height;
    model.has_indexed_skin = true;
  }
  if (has_field(reader, "playerColors", &f)) {
    model.player_color_top = integer_field(&f, "top", SAFE_MIN);
    model.player_color_bottom = integer_field(&f, "bottom", SAFE_MIN);
    model.has_player_colors = true;
  }
  if (has_field(reader, "previousOrigin", &f)) {
    model.previous_origin = qa_app_unified_read_vector(&f);
    model.has_previous_origin = true;
  }
  if (has_field(reader, "modelBeam", &f)) {
    qa_save_reader segment = field_of(&f, "segmentLength");
    model.model_beam_segment_length = qa_app_unified_non_negative(&segment);
    model.has_model_beam = true;
  }
  if (has_field(reader, "alpha", &f)) {
    model.alpha = qa_save_reader_finite(&f); model.has_alpha = true;
  }
  if (has_field(reader, "shaderBeam", &f)) {
    model.shader_beam_path = string_field(&f, "path");
    model.shader_beam_end = vector_field(&f, "end");
    model.shader_beam_width = integer_field(&f, "width", 1);
    model.has_shader_beam = true;
  }
  if (has_field(reader, "modelAttachments", &f)) {
    size_t count = qa_save_reader_list_count(&f);
    qa_app_sim_presentation_attachment *items = calloc(count ? count : 1, sizeof *items);
    if (!items) qa_range_error("out of memory");
    model.model_attachments = items; model.model_attachment_count = count;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry;
      if (!qa_save_reader_item(&f, i, &entry)) qa_range_error("out of memory");
      items[i].path = string_field(&entry, "path");
      items[i].tag = string_field(&entry, "tag");
    }
  }
  if (has_field(reader, "modelAnchor", &f)) {
    /* TS builds {path, tag, offset, fovOffset:{above, scale}} (strings borrow from the tree). */
    qa_qvm_grapple_view_anchor anchor;
    memset(&anchor, 0, sizeof anchor);
    anchor.path = string_field(&f, "path");
    anchor.tag = string_field(&f, "tag");
    anchor.offset = vector_field(&f, "offset");
    qa_save_reader fov = field_of(&f, "fovOffset");
    anchor.fov_offset_above = integer_field(&fov, "above", 1);
    qa_save_reader fov_again = field_of(&f, "fovOffset");
    anchor.fov_offset_scale = finite_field(&fov_again, "scale");
    qa_qvm_grapple_view_anchor *owned = xmalloc(sizeof *owned);
    *owned = anchor;
    model.model_anchor = owned;
  }
  if (has_field(reader, "replacesBody", &f)) model.replaces_body = qa_save_reader_literal_boolean(&f, true);
  if (has_field(reader, "renderOwner", &f)) model.render_owner = qa_save_reader_literal_string(&f, "source-client");
  if (has_field(reader, "q3GrappleCable", &f)) {
    qa_app_sim_q3_grapple_cable cable;
    memset(&cable, 0, sizeof cable);
    cable.owner = actor_field(&f, "owner", identity);
    cable.owner_origin = vector_field(&f, "ownerOrigin");
    cable.owner_angles = vector_field(&f, "ownerAngles");
    cable.view_height = finite_field(&f, "viewHeight");
    cable.offhand = boolean_field(&f, "offhand");
    cable.attached = boolean_field(&f, "attached");
    cable.flight = string_field(&f, "flight");
    cable.pull = string_field(&f, "pull");
    cable.hold = string_field(&f, "hold");
    cable.segment_length = integer_field(&f, "segmentLength", 1);
    qa_app_sim_q3_grapple_cable *owned = xmalloc(sizeof *owned);
    *owned = cable;
    model.q3_grapple_cable = owned;
  }
  if (has_field(reader, "q3Weapon", &f)) {
    model.q3_weapon_time_ms = finite_field(&f, "timeMilliseconds");
    model.q3_weapon_torso_animation = integer_field(&f, "torsoAnimation", SAFE_MIN);
    qa_save_reader v = field_of(&f, "lastFireMilliseconds");
    if (!qa_save_reader_is_null(&v)) {
      model.q3_weapon_last_fire_ms = qa_save_reader_finite(&v);
      model.has_q3_weapon_last_fire = true;
    }
    model.q3_weapon_firing = boolean_field(&f, "firing");
    model.q3_weapon_horizontal_speed = finite_field(&f, "horizontalSpeed");
    model.q3_weapon_bob_cycle = finite_field(&f, "bobCycle");
    model.q3_weapon_weapon = integer_field(&f, "weapon", SAFE_MIN);
    model.has_q3_weapon = true;
  }
  model.actor = actor_field(reader, "actor", identity);
  f = field_of(reader, "content");
  model.content = qa_read_content_id(&f);
  static const char *families[] = {"q1", "q2", "q3"};
  f = field_of(reader, "family");
  const char *family_text = qa_save_reader_choice_string(&f, families, 3);
  if (!qa_game_family_parse(family_text, &model.family)) qa_save_reader_fail(&f, "invalid family");
  model.path = string_field(reader, "path");
  model.frame = integer_field(reader, "frame", SAFE_MIN);
  model.old_frame = integer_field(reader, "oldFrame", SAFE_MIN);
  model.skin = integer_field(reader, "skin", SAFE_MIN);
  model.effects = integer_field(reader, "effects", SAFE_MIN);
  model.render_flags = integer_field(reader, "renderFlags", SAFE_MIN);
  model.origin = vector_field(reader, "origin");
  model.angles = vector_field(reader, "angles");
  model.scale = finite_field(reader, "scale");
  model.visible = boolean_field(reader, "visible");
  model.view_weapon = boolean_field(reader, "viewWeapon");
  qa_cleanup_pop(&guard);
  return model;
}
void qa_app_unified_model_free(qa_app_simulation_presentation *model) {
  if (model->held_weapon) {
    qa_held_weapon_declaration *held = (qa_held_weapon_declaration *)model->held_weapon;
    qa_content_held_weapon_free(held);
    free(held);
  }
  free((void *)model->flare.rim_color);
  free((void *)model->indexed_skin.name);
  free((void *)model->indexed_skin.pixels.bytes);
  free((void *)model->model_attachments);
  free((void *)model->model_anchor);
  free((void *)model->q3_grapple_cable);
  memset(model, 0, sizeof *model);
}
qa_q3_character_view qa_app_unified_read_character_view(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity) {
  qa_q3_character_view view = {0};
  qa_save_reader a = field_of(reader, "animation"), f;
  qa_save_reader kind = field_of(&a, "kind");
  qa_save_reader_literal_string(&kind, "q3");
  qa_save_reader_destroy(&kind);
  view.animation.kind = "q3";
  qa_save_reader legs = field_of(&a, "legs"), torso = field_of(&a, "torso");
  view.animation.legs = qa_save_reader_integer(&legs);
  view.animation.torso = qa_save_reader_integer(&torso);
  qa_save_reader_destroy(&legs); qa_save_reader_destroy(&torso);
  view.animation.legs_timer_milliseconds = finite_field(&a, "legsTimerMilliseconds");
  view.animation.torso_timer_milliseconds = finite_field(&a, "torsoTimerMilliseconds");
  qa_save_reader_destroy(&a);
  if (has_field(reader, "scale", &f)) {
    view.scale = qa_save_reader_finite(&f); view.has_scale = true;
    qa_save_reader_destroy(&f);
  }
  if (has_field(reader, "opacity", &f)) {
    view.opacity = qa_save_reader_finite(&f); view.has_opacity = true;
    qa_save_reader_destroy(&f);
  }
  qa_save_reader actor_reader = field_of(reader, "actor");
  view.actor = qa_app_unified_read_actor(&actor_reader, identity);
  qa_save_reader_destroy(&actor_reader);
  qa_save_reader origin = field_of(reader, "origin"), angles = field_of(reader, "angles"),
      velocity = field_of(reader, "velocity");
  view.origin = qa_app_unified_read_vector(&origin);
  view.angles = qa_app_unified_read_vector(&angles);
  view.velocity = qa_app_unified_read_vector(&velocity);
  qa_save_reader_destroy(&origin); qa_save_reader_destroy(&angles);
  qa_save_reader_destroy(&velocity);
  qa_save_reader v = field_of(reader, "movementDirection");
  view.movement_direction = qa_save_reader_integer(&v);
  qa_save_reader_destroy(&v);
  v = field_of(reader, "sourceFlags");
  view.source_flags = qa_save_reader_integer(&v);
  qa_save_reader_destroy(&v);
  v = field_of(reader, "powerups");
  view.powerups = qa_save_reader_integer(&v);
  qa_save_reader_destroy(&v);
  v = field_of(reader, "team");
  if (!qa_save_reader_is_null(&v)) {
    static const char *teams[] = {"red", "blue"};
    view.team = qa_save_reader_choice_string(&v, teams, 2);
  }
  qa_save_reader_destroy(&v);
  v = field_of(reader, "color");
  view.color = qa_app_unified_read_color(&v);
  qa_save_reader_destroy(&v);
  return view;
}
qa_world_text qa_app_unified_read_world_text(const qa_save_reader *reader) {
  qa_world_text text = {0};
  qa_save_reader o = field_of(reader, "orientation"), f;
  static const char *kinds[] = {"billboard", "fixed"};
  qa_save_reader kind_reader = field_of(&o, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 2);
  qa_save_reader_destroy(&kind_reader);
  text.orientation_fixed = strcmp(kind, "fixed") == 0;
  if (has_field(reader, "distanceCullFactor", &f)) {
    text.has_distance_cull_factor = true;
    text.distance_cull_factor = qa_save_reader_finite(&f);
    qa_save_reader_destroy(&f);
  }
  f = field_of(reader, "content");
  text.content = qa_read_content_id(&f);
  qa_save_reader_destroy(&f);
  f = field_of(reader, "text");
  text.text = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  f = field_of(reader, "origin");
  text.origin = qa_app_unified_read_vector(&f);
  qa_save_reader_destroy(&f);
  f = field_of(reader, "color");
  text.color = qa_app_unified_read_color(&f);
  qa_save_reader_destroy(&f);
  f = field_of(reader, "cellSize");
  text.cell_size = qa_app_unified_positive(&f);
  if (text.orientation_fixed) text.angles = vector_field(&o, "angles");
  f = field_of(reader, "depthTest");
  text.depth_test = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  static const char *fonts[] = {"classic", "selected"};
  f = field_of(reader, "font");
  text.font_selected = strcmp(qa_save_reader_choice_string(&f, fonts, 2), "selected") == 0;
  qa_save_reader_destroy(&f);
  return text;
}
qa_native_mod_camera_view qa_app_unified_read_native_camera_view(const qa_save_reader *reader) {
  qa_save_reader native = field_of(reader, "native");
  static const char *editions[] = {"classic", "rerelease"};
  qa_save_reader edition_reader = field_of(&native, "edition");
  const char *edition = qa_save_reader_choice_string(&edition_reader, editions, 2);
  qa_save_reader_destroy(&edition_reader);
  qa_app_sim_player_view view = qa_app_unified_read_player_view(reader);
  if (strcmp(edition, "classic") == 0 && view.has_damage_blend)
    qa_save_reader_fail(reader, "Classic camera cannot publish rerelease damage blend");
  qa_native_mod_camera_view out;
  memset(&out, 0, sizeof out);
  /* qa_session_player_view mirrors qa_app_sim_player_view fields. */
  out.view.has_client_view_offset_delta = view.has_client_view_offset_delta;
  out.view.client_view_offset_delta = view.client_view_offset_delta;
  out.view.has_blend = view.has_blend; out.view.blend = view.blend;
  out.view.has_damage_blend = view.has_damage_blend; out.view.damage_blend = view.damage_blend;
  out.view.origin = view.origin; out.view.angles = view.angles;
  out.view.view_height = view.view_height;
  out.view.has_kick_angles = view.has_kick_angles; out.view.kick_angles = view.kick_angles;
  out.view.has_field_of_view = view.has_field_of_view; out.view.field_of_view = view.field_of_view;
  out.view.foreign_character_death = view.foreign_character_death;
  out.view.has_pitch_drift = view.has_pitch_drift;
  out.view.pitch_drift_grounded = view.pitch_drift_grounded;
  out.view.pitch_drift_ideal_pitch = view.pitch_drift_ideal_pitch;
  out.view.pitch_drift_disabled = view.pitch_drift_disabled;
  out.edition = edition;
  qa_save_reader origin = field_of(&native, "movementOrigin");
  out.movement_origin = qa_app_unified_read_vector(&origin);
  qa_save_reader_destroy(&origin);
  qa_save_reader flags = field_of(&native, "renderFlags");
  out.render_flags = qa_save_reader_integer_min(&flags, 0);
  qa_save_reader_destroy(&flags);
  qa_save_reader flag = field_of(&native, "positionPrediction");
  out.position_prediction = qa_save_reader_boolean(&flag); qa_save_reader_destroy(&flag);
  flag = field_of(&native, "angularPrediction");
  out.angular_prediction = qa_save_reader_boolean(&flag); qa_save_reader_destroy(&flag);
  flag = field_of(&native, "weaponVisible");
  out.weapon_visible = qa_save_reader_boolean(&flag); qa_save_reader_destroy(&flag);
  qa_save_reader_destroy(&native);
  return out;
}
