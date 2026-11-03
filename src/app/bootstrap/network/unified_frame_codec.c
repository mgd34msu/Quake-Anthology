/* Port of src/app/bootstrap/network/unified-frame-codec.ts.
 * Wire: deflate-raw(level 1) of the checkpoint JSON. Objects are written in the key order the
 * TS reader builds them, so a C frame encodes as TS re-encodes a decoded frame.
 * Decoding fills a frame that owns the parsed tree (strings borrow from it); every array and
 * node reaches the frame before the next read can raise, and a guard frees it on a raise.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_frame_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "qa/app/bootstrap/network/unified_event_codec.h"
#include "qa/app/bootstrap/network/unified_finite.h"
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include "qa/content/held_weapon.h"
#include "qa/foundation/support.h"
#include "qa/persistence/mods.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"

#define FRAME_VALUE_LIMIT (32u * 1024u * 1024u)
#define FRAME_CHANNEL_LIMIT (4u * 1024u * 1024u)

static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count ? count : 1, size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
/* ---------------- encode ---------------- */
static void cleanup_value(void *arg) { qa_save_value_free(arg); }
static qa_save_value *object(void) {
  qa_save_value *value = qa_save_value_object();
  if (!value) qa_range_error("out of memory");
  return value;
}
static qa_save_value *array(void) {
  qa_save_value *value = qa_save_value_array();
  if (!value) qa_range_error("out of memory");
  return value;
}
/* Takes ownership of child even when it fails. Only called on a target that is itself
 * reachable from the root guarded by the encoder. */
static void set(qa_save_value *target, const char *key, qa_save_value *child) {
  if (!child) qa_range_error("out of memory");
  if (!qa_save_value_set(target, key, child)) {
    qa_save_value_free(child);
    qa_range_error("out of memory");
  }
}
static void push(qa_save_value *target, qa_save_value *child) {
  if (!child) qa_range_error("out of memory");
  if (!qa_save_value_push(target, child)) {
    qa_save_value_free(child);
    qa_range_error("out of memory");
  }
}
/* New object already attached under `key` of `target` (so a raise while filling it frees it). */
static qa_save_value *child_object(qa_save_value *target, const char *key) {
  qa_save_value *out = object();
  set(target, key, out);
  return out;
}
static qa_save_value *child_array(qa_save_value *target, const char *key) {
  qa_save_value *out = array();
  set(target, key, out);
  return out;
}
static qa_save_value *pushed_object(qa_save_value *list) {
  qa_save_value *out = object();
  push(list, out);
  return out;
}
static qa_save_value *num(double value) { return qa_save_value_number(value); }
static qa_save_value *str(const char *value) { return qa_save_value_string(value); }
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
      if (!out) break;
      for (size_t i = 0; i < value->item_count; i++) {
        qa_save_value *item = clone_value(value->items[i]);
        if (!item || !qa_save_value_push(out, item)) {
          qa_save_value_free(item); qa_save_value_free(out); out = NULL; break;
        }
      }
      break;
    case QA_SAVE_OBJECT:
      out = qa_save_value_object();
      if (!out) break;
      for (size_t i = 0; i < value->entry_count; i++) {
        qa_save_value *item = clone_value(value->entries[i].value);
        if (!item || !qa_save_value_set(out, value->entries[i].key, item)) {
          qa_save_value_free(item); qa_save_value_free(out); out = NULL; break;
        }
      }
      break;
  }
  return out;
}
static qa_save_value *cloned(const qa_save_value *value) {
  qa_save_value *out = clone_value(value);
  if (!out) qa_range_error("out of memory");
  return out;
}
static qa_save_value *write_vector(qa_vec3 v) {
  qa_save_value *out = object();
  if (!qa_save_value_set(out, "x", num(v.x)) || !qa_save_value_set(out, "y", num(v.y))
      || !qa_save_value_set(out, "z", num(v.z))) {
    qa_save_value_free(out);
    return NULL;
  }
  return out;
}
static qa_save_value *write_color(qa_vec4 v) {
  qa_save_value *out = write_vector((qa_vec3){v.x, v.y, v.z});
  if (out && !qa_save_value_set(out, "w", num(v.w))) {
    qa_save_value_free(out);
    return NULL;
  }
  return out;
}
static void set_vector(qa_save_value *target, const char *key, qa_vec3 v) { set(target, key, write_vector(v)); }
static void set_color(qa_save_value *target, const char *key, qa_vec4 v) { set(target, key, write_color(v)); }
static void set_bounds(qa_save_value *target, const char *key, qa_bounds bounds) {
  qa_save_value *out = child_object(target, key);
  set_vector(out, "min", bounds.min);
  set_vector(out, "max", bounds.max);
}
static void set_time(qa_save_value *target, const char *key, qa_source_time time) {
  qa_save_value *out = child_object(target, key);
  set(out, "kind", str(time.kind == QA_SOURCE_TIME_SECONDS ? "seconds" : "milliseconds"));
  set(out, "value", num(time.value));
}
static const char *phase_name(qa_frame_phase phase) {
  static const char *const names[] = {"frame-entry", "client-command", "entity-prethink",
      "entity-physics", "entity-think", "client-end-frame", "frame-exit"};
  return (unsigned)phase < 7 ? names[phase] : "frame-entry";
}
static void set_frame_stamp(qa_save_value *target, const char *key, const qa_frame_context *frame) {
  qa_save_value *out = child_object(target, key);
  set(out, "frame", num(frame->frame));
  set_time(out, "time", frame->time);
  set_time(out, "elapsed", frame->elapsed);
  set(out, "phase", str(phase_name(frame->phase)));
}
typedef const qa_app_unified_identity_encoder encoder_t;
static void set_actor(qa_save_value *target, const char *key, encoder_t *identity, qa_actor_id actor) {
  qa_app_unified_actor_reference reference = identity->actor(identity->ctx, actor);
  qa_save_value *out = child_object(target, key);
  set(out, "slot", num(reference.slot));
  set(out, "generation", num(reference.generation));
}
static void set_provider(qa_save_value *target, const char *key, qa_provider_reference reference) {
  qa_save_value *out = child_object(target, key);
  set(out, "provider", str(reference.provider));
  set(out, "content", str(reference.content));
}
static void write_inventory_entries(qa_save_value *list, const qa_inventory_entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) {
    qa_save_value *entry = pushed_object(list);
    set(entry, "item", str(entries[i].item));
    set(entry, "count", num(entries[i].count));
    set(entry, "capacity", num(entries[i].capacity));
    if (entries[i].has_count_policy) {
      qa_save_value *policy = child_object(entry, "countPolicy");
      set(policy, "kind", str(entries[i].count_policy.kind));
      if (strcmp(entries[i].count_policy.kind, "source-counter") == 0)
        set(policy, "arithmetic", str(entries[i].count_policy.arithmetic));
    }
  }
}
static void set_resource_key(qa_save_value *target, const char *key,
    const qa_resolved_resource_reference *reference) {
  qa_app_unified_resource_key value = qa_app_unified_resource_key_from_reference(reference);
  set(target, key, qa_app_unified_write_resource_key(&value));
}
static void write_entity(qa_save_value *out, encoder_t *identity, const qa_scene_entity *entity) {
  if (entity->actor) set_actor(out, "actor", identity, *entity->actor);
  else set(out, "actor", qa_save_value_null());
  set_resource_key(out, "resource", &entity->resource);
  if (entity->model.kind == QA_DECODED_MODEL_BRUSH_MODEL) set(out, "brushModel", num(entity->model.brush_model_index));
  else set(out, "brushModel", qa_save_value_null());
  qa_save_value *transform = child_object(out, "transform");
  set_vector(transform, "origin", entity->transform.origin);
  qa_save_value *axis = child_array(transform, "axis");
  for (int a = 0; a < 3; a++) push(axis, write_vector(entity->transform.axis.vectors[a]));
  set_vector(transform, "scale", entity->transform.scale);
  set_vector(out, "previousOrigin", entity->previous_origin);
  qa_save_value *pose = child_object(out, "pose");
  if (entity->pose.is_frame) {
    set(pose, "kind", str("frame"));
    set(pose, "frame", num(entity->pose.frame));
    set(pose, "previousFrame", num(entity->pose.previous_frame));
    set(pose, "backLerp", num(entity->pose.back_lerp));
  } else {
    set(pose, "kind", str("skeleton"));
    qa_save_value *joints = child_array(pose, "joints");
    for (size_t i = 0; i < entity->pose.joint_count; i++) {
      qa_save_value *joint = pushed_object(joints);
      set_vector(joint, "position", entity->pose.joints[i].position);
      set_color(joint, "orientation", entity->pose.joints[i].orientation);
      set(joint, "scale", num(entity->pose.joints[i].scale));
    }
  }
  set(out, "skin", num(entity->skin));
  set_color(out, "color", entity->color);
  set_time(out, "shaderTime", entity->shader_time);
  qa_save_value *flags = child_object(out, "flags");
  set(flags, "kind", str(entity->flags_kind));
  set(flags, "bits", num(entity->flags_bits));
  set_vector(out, "lightingOrigin", entity->lighting_origin);
  set(out, "shadowPlane", num(entity->shadow_plane));
  if (entity->has_opacity) set(out, "opacity", num(entity->opacity));
  qa_save_value *attachments = child_array(out, "attachments");
  for (size_t i = 0; i < entity->attachment_count; i++) {
    qa_save_value *attachment = pushed_object(attachments);
    set(attachment, "tag", str(entity->attachments[i].tag));
    write_entity(child_object(attachment, "entity"), identity, entity->attachments[i].entity);
  }
}
static void write_light(qa_save_value *out, const qa_scene_light *light) {
  set_vector(out, "origin", light->origin);
  set_vector(out, "color", light->color);
  set(out, "radius", num(light->radius));
  set(out, "additive", qa_save_value_boolean(light->additive));
  qa_save_value *profile = child_object(out, "profile");
  set(profile, "kind", str(light->profile.kind));
  if (strcmp(light->profile.kind, "q2") != 0) return;
  set(profile, "scale", num(light->profile.scale));
  if (light->profile.has_cone) {
    qa_save_value *cone = child_object(profile, "cone");
    set_vector(cone, "direction", light->profile.cone_direction);
    set(cone, "cosHalfAngle", num(light->profile.cone_cos_half_angle));
  } else set(profile, "cone", qa_save_value_null());
  qa_save_value *shadow = child_object(profile, "shadow");
  set(shadow, "kind", str(light->profile.shadow_cast ? "cast" : "none"));
  if (light->profile.shadow_cast) set(shadow, "resolution", num(light->profile.shadow_resolution));
}
static void write_particle(qa_save_value *out, const qa_scene_particle *particle) {
  set_vector(out, "origin", particle->origin);
  set(out, "size", num(particle->size));
  if (!particle->is_rgba) {
    set(out, "kind", str("indexed"));
    set(out, "paletteIndex", num(particle->palette_index));
    set(out, "alpha", num(particle->alpha));
  } else {
    set(out, "kind", str("rgba"));
    set_color(out, "color", particle->color);
    set(out, "rotation", num(particle->rotation));
  }
}
static void write_style(qa_save_value *out, const qa_scene_light_style *style) {
  set(out, "kind", str(style->is_q2 ? "q2" : "q1"));
  set(out, "style", num(style->style));
  if (!style->is_q2) set(out, "value", num(style->value));
  else {
    set_vector(out, "rgb", style->rgb);
    set(out, "white", num(style->white));
  }
}
static void write_player_view(qa_save_value *out, const qa_app_sim_player_view *view) {
  set_vector(out, "origin", view->origin);
  set_vector(out, "angles", view->angles);
  set(out, "viewHeight", num(view->view_height));
  if (view->has_blend) set_color(out, "blend", view->blend);
  if (view->has_damage_blend) set_color(out, "damageBlend", view->damage_blend);
  if (view->has_kick_angles) set_vector(out, "kickAngles", view->kick_angles);
  if (view->has_field_of_view) set(out, "fieldOfView", num(view->field_of_view));
  if (view->has_client_view_offset_delta)
    set_vector(out, "clientViewOffsetDelta", view->client_view_offset_delta);
  if (view->foreign_character_death) set(out, "foreignCharacterDeath", qa_save_value_boolean(true));
  if (view->has_pitch_drift) {
    qa_save_value *drift = child_object(out, "pitchDrift");
    set(drift, "grounded", qa_save_value_boolean(view->pitch_drift_grounded));
    set(drift, "idealPitch", num(view->pitch_drift_ideal_pitch));
    set(drift, "disabled", qa_save_value_boolean(view->pitch_drift_disabled));
  }
}
static void write_armor(qa_save_value *out, const qa_armor_state *armor) {
  qa_save_value *regular = child_object(out, "regular");
  switch (armor->regular.kind) {
    case QA_REGULAR_ARMOR_NONE: set(regular, "kind", str("none")); break;
    case QA_REGULAR_ARMOR_SOURCE:
      set(regular, "kind", str("source"));
      set(regular, "points", num(armor->regular.points));
      set(regular, "item", armor->regular.item ? str(armor->regular.item) : qa_save_value_null());
      break;
    case QA_REGULAR_ARMOR_Q1:
      set(regular, "kind", str("q1"));
      set(regular, "points", num(armor->regular.points));
      set(regular, "absorption", num(armor->regular.absorption));
      set(regular, "item", str(armor->regular.item));
      break;
    case QA_REGULAR_ARMOR_Q3:
      set(regular, "kind", str("q3"));
      set(regular, "points", num(armor->regular.points));
      set(regular, "protection", num(armor->regular.protection));
      break;
    case QA_REGULAR_ARMOR_Q2:
      set(regular, "kind", str("q2"));
      set(regular, "points", num(armor->regular.points));
      set(regular, "normalProtection", num(armor->regular.normal_protection));
      set(regular, "energyProtection", num(armor->regular.energy_protection));
      set(regular, "item", str(armor->regular.item));
      break;
  }
  qa_save_value *powered = child_object(out, "powered");
  const char *kind = armor->powered.kind ? armor->powered.kind : "none";
  set(powered, "kind", str(kind));
  if (strcmp(kind, "none") != 0) set(powered, "cells", num(armor->powered.cells));
}
static void write_player_ui(qa_save_value *out, const qa_app_sim_player_ui *ui) {
  if (ui->selected_arsenal) set(out, "selectedArsenal", qa_save_value_boolean(true));
  if (ui->native_inventory) set(out, "nativeInventory", cloned((const qa_save_value *)ui->native_inventory));
  set(out, "health", num(ui->health));
  write_armor(child_object(out, "armor"), &ui->armor);
  set(out, "activeWeapon", ui->active_weapon ? str(ui->active_weapon) : qa_save_value_null());
  if (!ui->has_ammo) set(out, "ammo", qa_save_value_null());
  else {
    qa_save_value *ammo = child_object(out, "ammo");
    set(ammo, "item", str(ui->ammo_item));
    set(ammo, "count", num(ui->ammo_count));
  }
  write_inventory_entries(child_array(out, "inventory"), ui->inventory, ui->inventory_count);
  set(out, "arsenalWarning", str(ui->arsenal_warning));
  qa_save_value *powerups = child_array(out, "powerups");
  for (size_t i = 0; i < ui->powerup_count; i++) {
    qa_save_value *row = pushed_object(powerups);
    set(row, "item", str(ui->powerups[i].item));
    set(row, "label", str(ui->powerups[i].label));
    set(row, "remainingSeconds", num(ui->powerups[i].remaining_seconds));
  }
  qa_save_value *items = child_array(out, "items");
  for (size_t i = 0; i < ui->item_count; i++) {
    const qa_app_sim_player_ui_item *item = &ui->items[i];
    qa_save_value *row = pushed_object(items);
    set(row, "id", str(item->id));
    set(row, "label", str(item->label));
    set(row, "kind", str(item->kind));
    set(row, "sourceOrdinal", num(item->source_ordinal));
    set(row, "owned", qa_save_value_boolean(item->owned));
    set(row, "hasAmmo", qa_save_value_boolean(item->has_ammo));
    set(row, "count", item->has_count ? num(item->count) : qa_save_value_null());
    set(row, "warningCount", num(item->warning_count));
  }
  if (!ui->weapon_status) {
    set(out, "weaponStatus", qa_save_value_null());
    return;
  }
  const qa_weapon_hud_status *status = ui->weapon_status;
  qa_save_value *row = child_object(out, "weaponStatus");
  set_provider(row, "source", status->source);
  set(row, "item", str(status->item));
  set(row, "label", str(status->label));
  qa_save_value *ammo = child_object(row, "ammo");
  if (!status->ammo_is_finite) set(ammo, "kind", str("unmetered"));
  else {
    set(ammo, "kind", str("finite"));
    set(ammo, "item", str(status->ammo_item));
    set(ammo, "count", num(status->ammo_count));
    set(ammo, "hasAmmoToStart", qa_save_value_boolean(status->has_ammo_to_start));
    set(ammo, "low", qa_save_value_boolean(status->low));
  }
}
static qa_save_value *write_axis(const qa_axis *axis) {
  qa_save_value *out = array();
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_value, out);
  for (int i = 0; i < 3; i++) push(out, write_vector(axis->vectors[i]));
  qa_cleanup_pop(&guard);
  return out;
}
static void write_held_weapon(qa_save_value *out, const qa_held_weapon_declaration *declaration) {
  if (!declaration->is_model) {
    set(out, "kind", str("none"));
    return;
  }
  set(out, "kind", str("model"));
  qa_save_value *model = child_object(out, "model");
  if (declaration->model.has_digest) set(model, "digest", str(declaration->model.digest));
  set(model, "path", str(declaration->model.path));
  set(model, "referenceFrame", num(declaration->model.reference_frame));
  qa_save_value *grip = child_object(model, "grip");
  set_vector(grip, "origin", declaration->model.grip.origin);
  set(grip, "axis", write_axis(&declaration->model.grip.axis));
  set_vector(grip, "scale", declaration->model.grip.scale);
  if (declaration->model.fallback) set(model, "fallback", str(declaration->model.fallback));
  if (declaration->model.part) {
    qa_save_value *part = child_object(model, "part");
    qa_save_value *digests = child_array(part, "digests");
    for (size_t i = 0; i < declaration->model.part->digests.count; i++)
      push(digests, str(declaration->model.part->digests.values[i]));
    qa_save_value *vertices = child_array(part, "vertices");
    for (size_t i = 0; i < declaration->model.part->vertices.count; i++)
      push(vertices, num(declaration->model.part->vertices.values[i]));
  }
}
static const char *family_name(qa_game_family family) {
  switch (family) {
    case QA_GAME_FAMILY_Q1: return "q1";
    case QA_GAME_FAMILY_Q2: return "q2";
    case QA_GAME_FAMILY_Q3: return "q3";
  }
  return "q1";
}
static void write_model(qa_save_value *out, encoder_t *identity, const qa_app_simulation_presentation *model) {
  qa_save_value * target;
  qa_save_value * child;
  set_actor(out, "actor", identity, model->actor);
  set(out, "content", str(model->content));
  set(out, "family", str(family_name(model->family)));
  set(out, "path", str(model->path));
  set(out, "frame", num(model->frame));
  set(out, "oldFrame", num(model->old_frame));
  set(out, "skin", num(model->skin));
  set(out, "effects", num(model->effects));
  set(out, "renderFlags", num(model->render_flags));
  set_vector(out, "origin", model->origin);
  set_vector(out, "angles", model->angles);
  set(out, "scale", num(model->scale));
  set(out, "visible", qa_save_value_boolean(model->visible));
  set(out, "viewWeapon", qa_save_value_boolean(model->view_weapon));
  if (model->replaces_body) set(out, "replacesBody", qa_save_value_boolean(true));
  if (model->render_owner) set(out, "renderOwner", str(model->render_owner));
  if (model->held_weapon)
    write_held_weapon(child_object(out, "heldWeapon"), model->held_weapon);
  if (model->native_held_weapon) set(out, "nativeHeldWeapon", qa_save_value_boolean(true));
  if (model->weapon_item) set(out, "weaponItem", str(model->weapon_item));
  if (model->has_flare) {
    qa_save_value *flare = child_object(out, "flare");
    set(flare, "image", str(model->flare.image));
    set(flare, "fadeStart", num(model->flare.fade_start));
    set(flare, "fadeEnd", num(model->flare.fade_end));
    set(flare, "scale", num(model->flare.scale));
    set_vector(flare, "color", model->flare.color);
    if (model->flare.rim_color) set_vector(flare, "rimColor", *model->flare.rim_color);
    else set(flare, "rimColor", qa_save_value_null());
    set(flare, "lockAngle", qa_save_value_boolean(model->flare.lock_angle));
  }
  if (model->has_back_lerp) set(out, "backLerp", num(model->back_lerp));
  if (model->has_skin_path) set(out, "skinPath", model->skin_path ? str(model->skin_path) : qa_save_value_null());
  if (model->has_indexed_skin) {
    qa_save_value *skin = child_object(out, "indexedSkin");
    set(skin, "name", str(model->indexed_skin.name));
    set(skin, "width", num(model->indexed_skin.width));
    set(skin, "height", num(model->indexed_skin.height));
    set(skin, "pixels", qa_save_value_bytes(model->indexed_skin.pixels.bytes, model->indexed_skin.pixels.length));
  }
  if (model->has_player_colors) {
    qa_save_value *colors = child_object(out, "playerColors");
    set(colors, "top", num(model->player_color_top));
    set(colors, "bottom", num(model->player_color_bottom));
  }
  if (model->has_previous_origin) set_vector(out, "previousOrigin", model->previous_origin);
  if (model->has_model_beam)
    (target = child_object(out, "modelBeam"), child = num(model->model_beam_segment_length), set(target, "segmentLength", child));
  if (model->has_shader_beam) {
    qa_save_value *beam = child_object(out, "shaderBeam");
    set(beam, "path", str(model->shader_beam_path));
    set_vector(beam, "end", model->shader_beam_end);
    set(beam, "width", num(model->shader_beam_width));
  }
  if (model->model_attachments) {
    qa_save_value *attachments = child_array(out, "modelAttachments");
    for (size_t i = 0; i < model->model_attachment_count; i++) {
      qa_save_value *row = pushed_object(attachments);
      set(row, "path", str(model->model_attachments[i].path));
      set(row, "tag", str(model->model_attachments[i].tag));
    }
  }
  if (model->model_anchor) {
    const qa_qvm_grapple_view_anchor *anchor = model->model_anchor;
    qa_save_value *row = child_object(out, "modelAnchor");
    set(row, "path", str(anchor->path));
    set(row, "tag", str(anchor->tag));
    set_vector(row, "offset", anchor->offset);
    qa_save_value *fov = child_object(row, "fovOffset");
    set(fov, "above", num(anchor->fov_offset_above));
    set(fov, "scale", num(anchor->fov_offset_scale));
  }
  if (model->q3_grapple_cable) {
    const qa_app_sim_q3_grapple_cable *cable = model->q3_grapple_cable;
    qa_save_value *row = child_object(out, "q3GrappleCable");
    set_actor(row, "owner", identity, cable->owner);
    set_vector(row, "ownerOrigin", cable->owner_origin);
    set_vector(row, "ownerAngles", cable->owner_angles);
    set(row, "viewHeight", num(cable->view_height));
    set(row, "offhand", qa_save_value_boolean(cable->offhand));
    set(row, "attached", qa_save_value_boolean(cable->attached));
    set(row, "flight", str(cable->flight));
    set(row, "pull", str(cable->pull));
    set(row, "hold", str(cable->hold));
    set(row, "segmentLength", num(cable->segment_length));
  }
  if (model->has_alpha) set(out, "alpha", num(model->alpha));
  if (model->has_q3_weapon) {
    qa_save_value *weapon = child_object(out, "q3Weapon");
    set(weapon, "timeMilliseconds", num(model->q3_weapon_time_ms));
    set(weapon, "torsoAnimation", num(model->q3_weapon_torso_animation));
    set(weapon, "lastFireMilliseconds",
        model->has_q3_weapon_last_fire ? num(model->q3_weapon_last_fire_ms) : qa_save_value_null());
    set(weapon, "firing", qa_save_value_boolean(model->q3_weapon_firing));
    set(weapon, "horizontalSpeed", num(model->q3_weapon_horizontal_speed));
    set(weapon, "bobCycle", num(model->q3_weapon_bob_cycle));
    set(weapon, "weapon", num(model->q3_weapon_weapon));
  }
}
static void write_character(qa_save_value *out, encoder_t *identity, const qa_q3_character_view *view) {
  set_actor(out, "actor", identity, view->actor);
  set_vector(out, "origin", view->origin);
  set_vector(out, "angles", view->angles);
  set_vector(out, "velocity", view->velocity);
  set(out, "movementDirection", num(view->movement_direction));
  qa_save_value *animation = child_object(out, "animation");
  set(animation, "kind", str("q3"));
  set(animation, "legs", num(view->animation.legs));
  set(animation, "torso", num(view->animation.torso));
  set(animation, "legsTimerMilliseconds", num(view->animation.legs_timer_milliseconds));
  set(animation, "torsoTimerMilliseconds", num(view->animation.torso_timer_milliseconds));
  set(out, "sourceFlags", num(view->source_flags));
  set(out, "powerups", num(view->powerups));
  set(out, "team", view->team ? str(view->team) : qa_save_value_null());
  set_color(out, "color", view->color);
  if (view->has_scale) set(out, "scale", num(view->scale));
  if (view->has_opacity) set(out, "opacity", num(view->opacity));
}
static void write_world_text(qa_save_value *out, const qa_world_text *text) {
  set(out, "content", str(text->content));
  set(out, "text", str(text->text));
  set_vector(out, "origin", text->origin);
  set_color(out, "color", text->color);
  set(out, "cellSize", num(text->cell_size));
  qa_save_value *orientation = child_object(out, "orientation");
  set(orientation, "kind", str(text->orientation_fixed ? "fixed" : "billboard"));
  if (text->orientation_fixed) set_vector(orientation, "angles", text->angles);
  set(out, "depthTest", qa_save_value_boolean(text->depth_test));
  set(out, "font", str(text->font_selected ? "selected" : "classic"));
  if (text->has_distance_cull_factor) set(out, "distanceCullFactor", num(text->distance_cull_factor));
}
static void write_native_camera(qa_save_value *out, const qa_app_unified_native_camera_frame *camera) {
  qa_save_value *owner = child_object(out, "owner");
  set(owner, "provider", str(camera->owner.provider));
  set(owner, "generation", num(camera->owner.generation));
  set(out, "identity", cloned(camera->identity_wire));
  set(out, "generation", num(camera->generation));
  qa_save_value *view_value = child_object(out, "view");
  const qa_session_player_view *source = &camera->view.view;
  qa_app_sim_player_view view;
  memset(&view, 0, sizeof view);
  view.origin = source->origin; view.angles = source->angles; view.view_height = source->view_height;
  view.has_blend = source->has_blend; view.blend = source->blend;
  view.has_damage_blend = source->has_damage_blend; view.damage_blend = source->damage_blend;
  view.has_kick_angles = source->has_kick_angles; view.kick_angles = source->kick_angles;
  view.has_field_of_view = source->has_field_of_view; view.field_of_view = source->field_of_view;
  view.has_client_view_offset_delta = source->has_client_view_offset_delta;
  view.client_view_offset_delta = source->client_view_offset_delta;
  view.foreign_character_death = source->foreign_character_death;
  view.has_pitch_drift = source->has_pitch_drift;
  view.pitch_drift_grounded = source->pitch_drift_grounded;
  view.pitch_drift_ideal_pitch = source->pitch_drift_ideal_pitch;
  view.pitch_drift_disabled = source->pitch_drift_disabled;
  write_player_view(view_value, &view);
  qa_save_value *native = child_object(view_value, "native");
  set(native, "edition", str(camera->view.edition));
  set_vector(native, "movementOrigin", camera->view.movement_origin);
  set(native, "renderFlags", num(camera->view.render_flags));
  set(native, "positionPrediction", qa_save_value_boolean(camera->view.position_prediction));
  set(native, "angularPrediction", qa_save_value_boolean(camera->view.angular_prediction));
  set(native, "weaponVisible", qa_save_value_boolean(camera->view.weapon_visible));
}
static void write_frame_value(qa_save_value *out, const qa_app_unified_presentation_frame_t *frame,
    encoder_t *identity, bool finite) {
  const qa_world_snapshot *snapshot = &frame->output.snapshot;
  set(out, "schema", str("qts-unified-frame"));
  set(out, "version", num(10));
  if (frame->has_native_camera) write_native_camera(child_object(out, "nativeCamera"), &frame->native_camera);
  if (frame->has_components) set(out, "components", qa_app_unified_write_component_frames(&frame->components));
  else {
    qa_app_unified_component_frames empty;
    memset(&empty, 0, sizeof empty);
    set(out, "components", qa_app_unified_write_component_frames(&empty));
  }
  set(out, "epoch", num(frame->epoch));
  set(out, "acknowledgedInput", num(frame->acknowledged_input));
  /* zeroNonFinite reaches frame.prediction (depth 1) before it is encoded to bytes. */
  qa_save_value *prediction_tree = qa_app_unified_prediction_value(&frame->prediction);
  if (finite) qa_app_unified_zero_non_finite(prediction_tree, 1);
  size_t prediction_length = 0;
  uint8_t *prediction = qa_encode_checkpoint_value(prediction_tree, &prediction_length);
  qa_save_value_free(prediction_tree);
  if (!prediction) qa_range_error("out of memory");
  qa_save_value *prediction_value = qa_save_value_bytes(prediction, prediction_length);
  free(prediction);
  set(out, "prediction", prediction_value);
  qa_save_value *output = child_object(out, "output");
  qa_save_value *s = child_object(output, "snapshot");
  set_frame_stamp(s, "frame", &snapshot->frame);
  qa_save_value *actors = child_array(s, "actors");
  for (size_t i = 0; i < snapshot->actor_count; i++) {
    qa_save_value *row = pushed_object(actors);
    set_actor(row, "id", identity, snapshot->actors[i].id);
    set(row, "owner", str(snapshot->actors[i].owner));
    set(row, "definition", str(snapshot->actors[i].definition));
  }
  qa_save_value *bodies = child_array(s, "bodies");
  for (size_t i = 0; i < snapshot->body_count; i++) {
    qa_save_value *row = pushed_object(bodies);
    set_actor(row, "actor", identity, snapshot->bodies[i].actor);
    const qa_body_state *state = &snapshot->bodies[i].body;
    qa_save_value *body = child_object(row, "body");
    set_vector(body, "origin", state->origin);
    set_vector(body, "angles", state->angles);
    set_vector(body, "velocity", state->velocity);
    set_bounds(body, "bounds", state->bounds);
    if (state->ground) set_actor(body, "ground", identity, *state->ground);
    else set(body, "ground", qa_save_value_null());
  }
  qa_save_value *inventories = child_array(s, "inventories");
  for (size_t i = 0; i < snapshot->inventory_count; i++) {
    qa_save_value *row = pushed_object(inventories);
    set_actor(row, "actor", identity, snapshot->inventories[i].actor);
    write_inventory_entries(child_array(row, "entries"), snapshot->inventories[i].entries,
        snapshot->inventories[i].entry_count);
  }
  qa_save_value *configurations = child_array(s, "configurations");
  for (size_t i = 0; i < snapshot->configuration_count; i++) {
    const qa_actor_configuration *c = &snapshot->configurations[i];
    qa_save_value *row = pushed_object(configurations);
    set_actor(row, "actor", identity, c->actor);
    set_provider(row, "movement", c->movement);
    qa_save_value *character = child_object(row, "character");
    set_provider(character, "definition", c->character.definition);
    set_provider(character, "appearance", c->character.appearance);
    qa_save_value *weapons = child_array(row, "weapons");
    for (size_t j = 0; j < c->weapons.count; j++) {
      qa_save_value *weapon = pushed_object(weapons);
      set(weapon, "provider", str(c->weapons.values[j].provider));
      set(weapon, "content", str(c->weapons.values[j].content));
    }
    set_provider(row, "inventory", c->inventory);
  }
  qa_save_value *scene = child_object(s, "scene");
  set_time(scene, "time", snapshot->scene.time);
  if (snapshot->scene.world) set_resource_key(scene, "world", &snapshot->scene.world->resource);
  else set(scene, "world", qa_save_value_null());
  qa_save_value *entities = child_array(scene, "entities");
  for (size_t i = 0; i < snapshot->scene.entity_count; i++)
    write_entity(pushed_object(entities), identity, &snapshot->scene.entities[i]);
  qa_save_value *lights = child_array(scene, "lights");
  for (size_t i = 0; i < snapshot->scene.light_count; i++)
    write_light(pushed_object(lights), &snapshot->scene.lights[i]);
  qa_save_value *particles = child_array(scene, "particles");
  for (size_t i = 0; i < snapshot->scene.particle_count; i++)
    write_particle(pushed_object(particles), &snapshot->scene.particles[i]);
  qa_save_value *styles = child_array(scene, "lightStyles");
  for (size_t i = 0; i < snapshot->scene.light_style_count; i++)
    write_style(pushed_object(styles), &snapshot->scene.light_styles[i]);
  if (snapshot->scene.area_bits)
    set(scene, "areaBits", qa_save_value_bytes(snapshot->scene.area_bits->bytes, snapshot->scene.area_bits->length));
  else set(scene, "areaBits", qa_save_value_null());
  qa_save_value *events = child_array(output, "events");
  for (size_t i = 0; i < frame->event_count; i++)
    push(events, qa_app_unified_write_simulation_event(frame->events[i]));
  qa_save_value *models = child_array(out, "models");
  for (size_t i = 0; i < frame->model_count; i++) write_model(pushed_object(models), identity, &frame->models[i]);
  qa_save_value *characters = child_array(out, "characters");
  for (size_t i = 0; i < frame->character_count; i++)
    write_character(pushed_object(characters), identity, &frame->characters[i]);
  qa_save_value *texts = child_array(out, "worldText");
  for (size_t i = 0; i < frame->world_text_count; i++) write_world_text(pushed_object(texts), &frame->world_text[i]);
  qa_save_value *player = child_object(out, "player");
  set_actor(player, "actor", identity, frame->player_actor);
  write_player_view(child_object(player, "view"), &frame->player_view);
  write_player_ui(child_object(player, "ui"), &frame->player_ui);
}
static void cleanup_bytes(void *arg) { free(*(uint8_t **)arg); }
static uint8_t *deflate_raw(const uint8_t *input, size_t length, size_t *out_length) {
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  if (deflateInit2(&stream, 1, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) qa_range_error("out of memory");
  uLong bound = deflateBound(&stream, (uLong)length);
  uint8_t *out = malloc(bound ? bound : 1);
  if (!out) { deflateEnd(&stream); qa_range_error("out of memory"); }
  stream.next_in = (Bytef *)input;
  stream.avail_in = (uInt)length;
  stream.next_out = out;
  stream.avail_out = (uInt)bound;
  int status = deflate(&stream, Z_FINISH);
  *out_length = (size_t)stream.total_out;
  deflateEnd(&stream);
  if (status != Z_STREAM_END) { free(out); qa_range_error("out of memory"); }
  return out;
}
static uint8_t *encode_frame(const qa_app_unified_presentation_frame_t *frame,
    const qa_app_unified_identity_encoder *identity, size_t *out_length, bool finite) {
  qa_save_value *out = object();
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_value, out);
  write_frame_value(out, frame, identity, finite);
  if (finite) qa_app_unified_zero_non_finite(out, 0);
  size_t value_length = 0;
  uint8_t *value = qa_encode_checkpoint_value(out, &value_length);
  if (!value) qa_range_error("out of memory");
  qa_cleanup_pop(&guard);
  qa_save_value_free(out);
  qa_cleanup value_guard;
  qa_cleanup_push(&value_guard, cleanup_bytes, &value);
  if (value_length > FRAME_VALUE_LIMIT) qa_raise("RangeError", 0, 0, "Unified frame exceeds byte limit");
  uint8_t *bytes = deflate_raw(value, value_length, out_length);
  qa_cleanup_pop(&value_guard);
  free(value);
  return bytes;
}
uint8_t *qa_app_unified_encode_frame(const qa_app_unified_presentation_frame_t *frame,
    const qa_app_unified_identity_encoder *identity, size_t *out_length) {
  return encode_frame(frame, identity, out_length, false);
}
uint8_t *qa_app_unified_encode_frame_finite(const qa_app_unified_presentation_frame_t *frame,
    const qa_app_unified_identity_encoder *identity, size_t *out_length) {
  return encode_frame(frame, identity, out_length, true);
}
/* ---------------- decode ---------------- */
static qa_save_reader field_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("out of memory");
  return out;
}
static qa_save_reader item_of(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_range_error("out of memory");
  return out;
}
static bool absent(const qa_save_reader *reader) { return qa_save_reader_is_undefined(reader); }
static double finite_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  return qa_save_reader_finite(&f);
}
static double integer_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  return qa_save_reader_integer(&f);
}
static double integer_min_field(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader f = field_of(reader, name);
  return qa_save_reader_integer_min(&f, minimum);
}
static bool boolean_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  return qa_save_reader_boolean(&f);
}
static qa_vec3 vec_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  return qa_app_unified_read_vector(&f);
}
static qa_vec4 color_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  return qa_app_unified_read_color(&f);
}
static const char *choice_field(const qa_save_reader *reader, const char *name,
    const char *const *choices, size_t count) {
  qa_save_reader f = field_of(reader, name);
  return qa_save_reader_choice_string(&f, choices, count);
}
static qa_actor_id actor_field(const qa_save_reader *reader, const char *name,
    const qa_app_unified_identity_decoder *identity) {
  qa_save_reader f = field_of(reader, name);
  return qa_app_unified_read_actor(&f, identity);
}
static qa_actor_id *owned_actor(qa_actor_id actor) {
  qa_actor_id *owned = xcalloc(1, sizeof *owned);
  *owned = actor;
  return owned;
}
/* TS readKey: the frame codec's own relative-path rule. */
static bool unsafe_path(const char *path) {
  if (path[0] == '\0' || strchr(path, '\\') || path[0] == '/') return true;
  if (((path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z')) && path[1] == ':') return true;
  const char *cursor = path;
  for (;;) {
    const char *slash = strchr(cursor, '/');
    size_t part = slash ? (size_t)(slash - cursor) : strlen(cursor);
    if (part == 0 || (part == 1 && cursor[0] == '.') || (part == 2 && cursor[0] == '.' && cursor[1] == '.'))
      return true;
    if (!slash) return false;
    cursor = slash + 1;
  }
}
static qa_app_unified_resource_key read_key(const qa_save_reader *reader) {
  qa_save_reader path_reader = field_of(reader, "path");
  const char *path = qa_save_reader_string(&path_reader);
  if (unsafe_path(path))
    qa_save_reader_fail(reader, "invalid relative resource path");
  qa_app_unified_resource_key key;
  qa_save_reader f = field_of(reader, "content");
  key.content = qa_read_content_id(&f);
  key.path = path;
  f = field_of(reader, "digest");
  key.digest = qa_read_digest(&f);
  key.byte_length = integer_min_field(reader, "byteLength", 0);
  return key;
}
static void match_resource(const qa_app_unified_resource_key *key, const qa_resolved_resource_reference *resource) {
  qa_app_unified_resource_key actual = qa_app_unified_resource_key_from_reference(resource);
  if (strcmp(actual.content, key->content) != 0 || strcmp(actual.path, key->path) != 0
      || strcmp(actual.digest, key->digest) != 0 || actual.byte_length != key->byte_length)
    qa_raise("Error", 0, 0, "Unified resource differs from locally resolved content");
}
static bool same_world(const qa_decoded_world *a, const qa_decoded_world *b) {
  return a->kind == b->kind && memcmp(&a->as, &b->as, sizeof a->as) == 0;
}
static void entity_free(qa_scene_entity *entity) {
  free((void *)entity->actor);
  free((void *)entity->pose.joints);
  for (size_t i = 0; i < entity->attachment_count; i++) {
    if (entity->attachments[i].entity) entity_free((qa_scene_entity *)entity->attachments[i].entity);
    free((void *)entity->attachments[i].entity);
  }
  free((void *)entity->attachments);
  memset(entity, 0, sizeof *entity);
}
/* Fills a zeroed entity reachable from the frame; each allocation lands in it before the next
 * read. */
static void read_entity(const qa_save_reader *r, const qa_app_unified_frame_decoder *context,
    int depth, qa_scene_entity *entity) {
  if (depth > 32) qa_save_reader_fail(r, "scene attachment nesting exceeds limit");
  const qa_app_unified_identity_decoder *identity = &context->identity;
  qa_save_reader key_reader = field_of(r, "resource");
  qa_app_unified_resource_key key = read_key(&key_reader);
  entity->resource = context->resource(context->ctx, &key);
  match_resource(&key, &entity->resource);
  qa_save_reader brush_reader = field_of(r, "brushModel");
  bool has_brush = !qa_save_reader_is_null(&brush_reader);
  double brush_model = has_brush ? qa_save_reader_integer_min(&brush_reader, 0) : 0;
  entity->model = context->model(context->ctx, &key, has_brush, brush_model);
  if (has_brush && (entity->model.kind != QA_DECODED_MODEL_BRUSH_MODEL
      || entity->model.brush_model_index != brush_model || !context->world
      || !same_world(&entity->model.brush_world, &context->world->geometry)))
    qa_save_reader_fail(r, "brush model differs from local world");
  if (!has_brush && entity->model.kind == QA_DECODED_MODEL_BRUSH_MODEL)
    qa_save_reader_fail(r, "unexpected brush model");
  qa_save_reader transform = field_of(r, "transform"), flags = field_of(r, "flags"), opacity = field_of(r, "opacity");
  qa_save_reader attachments = field_of(r, "attachments");
  size_t attachment_count = qa_save_reader_list_count(&attachments);
  qa_scene_entity_attachment *items = xcalloc(attachment_count, sizeof *items);
  entity->attachments = items;
  for (size_t i = 0; i < attachment_count; i++) {
    qa_save_reader row = item_of(&attachments, i);
    entity->attachment_count = i + 1;
    qa_save_reader tag = field_of(&row, "tag");
    items[i].tag = qa_save_reader_string(&tag);
    qa_scene_entity *child = xcalloc(1, sizeof *child);
    items[i].entity = child;
    qa_save_reader child_reader = field_of(&row, "entity");
    read_entity(&child_reader, context, depth + 1, child);
  }
  qa_save_reader actor_reader = field_of(r, "actor");
  if (!qa_save_reader_is_null(&actor_reader))
    entity->actor = owned_actor(qa_app_unified_read_actor(&actor_reader, identity));
  entity->transform.origin = vec_field(&transform, "origin");
  qa_save_reader axis = field_of(&transform, "axis");
  qa_app_unified_read_axis(&axis, entity->transform.axis.vectors);
  entity->transform.scale = vec_field(&transform, "scale");
  entity->previous_origin = vec_field(r, "previousOrigin");
  qa_save_reader pose = field_of(r, "pose");
  static const char *const pose_kinds[] = {"frame", "skeleton"};
  if (strcmp(choice_field(&pose, "kind", pose_kinds, 2), "frame") == 0) {
    entity->pose.is_frame = true;
    entity->pose.frame = integer_field(&pose, "frame");
    entity->pose.previous_frame = integer_field(&pose, "previousFrame");
    entity->pose.back_lerp = finite_field(&pose, "backLerp");
  } else {
    qa_save_reader joints_reader = field_of(&pose, "joints");
    size_t joint_count = qa_save_reader_list_count(&joints_reader);
    qa_skeleton_joint_pose *joints = xcalloc(joint_count, sizeof *joints);
    entity->pose.joints = joints;
    entity->pose.joint_count = joint_count;
    for (size_t i = 0; i < joint_count; i++) {
      qa_save_reader joint = item_of(&joints_reader, i);
      joints[i].position = vec_field(&joint, "position");
      joints[i].orientation = color_field(&joint, "orientation");
      joints[i].scale = finite_field(&joint, "scale");
    }
  }
  entity->skin = integer_field(r, "skin");
  entity->color = color_field(r, "color");
  qa_save_reader shader_time = field_of(r, "shaderTime");
  entity->shader_time = qa_read_time(&shader_time);
  static const char *const flag_kinds[] = {"q1", "q2", "q3"};
  entity->flags_kind = choice_field(&flags, "kind", flag_kinds, 3);
  entity->flags_bits = integer_field(&flags, "bits");
  entity->lighting_origin = vec_field(r, "lightingOrigin");
  entity->shadow_plane = finite_field(r, "shadowPlane");
  if (!absent(&opacity)) {
    entity->opacity = qa_save_reader_finite(&opacity);
    entity->has_opacity = true;
  }
}
static qa_scene_light read_light(const qa_save_reader *r) {
  qa_scene_light light;
  memset(&light, 0, sizeof light);
  qa_save_reader p = field_of(r, "profile");
  static const char *const kinds[] = {"q1", "q2", "q3"};
  light.profile.kind = choice_field(&p, "kind", kinds, 3);
  light.origin = vec_field(r, "origin");
  light.color = vec_field(r, "color");
  light.radius = finite_field(r, "radius");
  light.additive = boolean_field(r, "additive");
  if (strcmp(light.profile.kind, "q2") != 0) return light;
  qa_save_reader s = field_of(&p, "shadow");
  static const char *const shadows[] = {"none", "cast"};
  const char *shadow = choice_field(&s, "kind", shadows, 2);
  light.profile.scale = finite_field(&p, "scale");
  qa_save_reader cone = field_of(&p, "cone");
  if (!qa_save_reader_is_null(&cone)) {
    light.profile.has_cone = true;
    light.profile.cone_direction = vec_field(&cone, "direction");
    light.profile.cone_cos_half_angle = finite_field(&cone, "cosHalfAngle");
  }
  if (strcmp(shadow, "cast") == 0) {
    light.profile.shadow_cast = true;
    light.profile.shadow_resolution = integer_min_field(&s, "resolution", 1);
  }
  return light;
}
static qa_scene_particle read_particle(const qa_save_reader *r) {
  qa_scene_particle particle;
  memset(&particle, 0, sizeof particle);
  static const char *const kinds[] = {"indexed", "rgba"};
  const char *kind = choice_field(r, "kind", kinds, 2);
  particle.origin = vec_field(r, "origin");
  particle.size = finite_field(r, "size");
  if (strcmp(kind, "indexed") == 0) {
    particle.palette_index = integer_min_field(r, "paletteIndex", 0);
    particle.alpha = finite_field(r, "alpha");
  } else {
    particle.is_rgba = true;
    particle.color = color_field(r, "color");
    particle.rotation = finite_field(r, "rotation");
  }
  return particle;
}
static qa_scene_light_style read_style(const qa_save_reader *r) {
  qa_scene_light_style style;
  memset(&style, 0, sizeof style);
  static const char *const kinds[] = {"q1", "q2"};
  const char *kind = choice_field(r, "kind", kinds, 2);
  style.style = integer_min_field(r, "style", 0);
  if (strcmp(kind, "q1") == 0) style.value = finite_field(r, "value");
  else {
    style.is_q2 = true;
    style.rgb = vec_field(r, "rgb");
    style.white = finite_field(r, "white");
  }
  return style;
}
static void mod_identity_release(qa_mod_identity *identity) {
  free((void *)identity->modules);
  free((void *)identity->providers);
  identity->modules = NULL;
  identity->providers = NULL;
}
void qa_app_unified_presentation_frame_free(qa_app_unified_presentation_frame_t *frame) {
  if (frame->has_components) qa_app_unified_component_frames_free(&frame->components);
  if (frame->has_native_camera && frame->tree) mod_identity_release(&frame->native_camera.identity);
  qa_app_unified_prediction_projection_free(&frame->prediction);
  qa_world_snapshot *snapshot = &frame->output.snapshot;
  free((void *)snapshot->actors);
  for (size_t i = 0; i < snapshot->body_count; i++) free((void *)snapshot->bodies[i].body.ground);
  free((void *)snapshot->bodies);
  for (size_t i = 0; i < snapshot->inventory_count; i++) free((void *)snapshot->inventories[i].entries);
  free((void *)snapshot->inventories);
  for (size_t i = 0; i < snapshot->configuration_count; i++)
    free((void *)snapshot->configurations[i].weapons.values);
  free((void *)snapshot->configurations);
  for (size_t i = 0; i < snapshot->scene.entity_count; i++)
    entity_free((qa_scene_entity *)&snapshot->scene.entities[i]);
  free((void *)snapshot->scene.entities);
  free((void *)snapshot->scene.lights);
  free((void *)snapshot->scene.particles);
  free((void *)snapshot->scene.light_styles);
  if (snapshot->scene.area_bits) free((void *)snapshot->scene.area_bits->bytes);
  free((void *)snapshot->scene.area_bits);
  for (size_t i = 0; i < frame->event_count; i++) qa_save_value_free(frame->events[i]);
  free(frame->events);
  for (size_t i = 0; i < frame->model_count; i++) qa_app_unified_model_free(&frame->models[i]);
  free(frame->models);
  free(frame->characters);
  free(frame->world_text);
  qa_app_unified_player_ui_free(&frame->player_ui);
  qa_save_value_free(frame->tree);
  memset(frame, 0, sizeof *frame);
}
static void cleanup_frame(void *arg) { qa_app_unified_presentation_frame_free(arg); }
static void cleanup_tree(void *arg) { qa_save_value_free(*(qa_save_value **)arg); }
static uint8_t *inflate_raw(const uint8_t *bytes, size_t length, size_t *out_length) {
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  if (inflateInit2(&stream, -15) != Z_OK) qa_range_error("out of memory");
  size_t capacity = length * 4 + 1024;
  if (capacity > FRAME_VALUE_LIMIT) capacity = FRAME_VALUE_LIMIT;
  uint8_t *out = malloc(capacity);
  if (!out) { inflateEnd(&stream); qa_range_error("out of memory"); }
  stream.next_in = (Bytef *)bytes;
  stream.avail_in = (uInt)length;
  size_t used = 0;
  const char *failure = NULL;
  bool too_large = false;
  for (;;) {
    if (used == capacity) {
      if (capacity >= FRAME_VALUE_LIMIT) { too_large = true; break; }
      size_t grown = capacity * 2 > FRAME_VALUE_LIMIT ? FRAME_VALUE_LIMIT : capacity * 2;
      uint8_t *next = realloc(out, grown);
      if (!next) { free(out); inflateEnd(&stream); qa_range_error("out of memory"); }
      out = next;
      capacity = grown;
    }
    stream.next_out = out + used;
    stream.avail_out = (uInt)(capacity - used);
    int status = inflate(&stream, Z_NO_FLUSH);
    used = capacity - stream.avail_out;
    if (status == Z_STREAM_END) break;
    if (status == Z_OK) continue;
    if (status == Z_BUF_ERROR && stream.avail_out == 0) continue;
    failure = status == Z_BUF_ERROR ? "unexpected end of file" : stream.msg ? stream.msg : "zlib error";
    break;
  }
  char message[128];
  if (failure) snprintf(message, sizeof message, "%s", failure);
  inflateEnd(&stream);
  if (too_large) {
    /* Node's maxOutputLength guard: one byte more than the limit is never materialized. */
    free(out);
    qa_raise("RangeError", 0, 0, "Cannot create a Buffer larger than %u bytes", FRAME_VALUE_LIMIT);
  }
  if (failure) {
    free(out);
    qa_raise("Error", 0, 0, "%s", message);
  }
  *out_length = used;
  return out;
}
void qa_app_unified_frame_envelope_free(qa_app_unified_frame_envelope *envelope) {
  qa_save_value_free(envelope->tree);
  memset(envelope, 0, sizeof *envelope);
}
qa_app_unified_frame_envelope qa_app_unified_read_frame(const uint8_t *bytes, size_t length) {
  if (length > FRAME_CHANNEL_LIMIT) qa_raise("RangeError", 0, 0, "Unified frame exceeds channel byte limit");
  size_t value_length = 0;
  uint8_t *value = inflate_raw(bytes, length, &value_length);
  qa_cleanup value_guard;
  qa_cleanup_push(&value_guard, cleanup_bytes, &value);
  qa_save_value *tree = qa_decode_checkpoint_value(value, value_length);
  if (!tree) qa_range_error("out of memory");
  qa_cleanup_pop(&value_guard);
  free(value);
  qa_cleanup tree_guard;
  qa_cleanup_push(&tree_guard, cleanup_tree, &tree);
  qa_save_reader r;
  if (!qa_save_reader_init(&r, tree, "unified-frame")) qa_range_error("out of memory");
  qa_save_reader f = field_of(&r, "schema");
  qa_save_reader_literal_string(&f, "qts-unified-frame");
  static const double versions[] = {2, 3, 4, 5, 6, 7, 8, 9, 10};
  f = field_of(&r, "version");
  qa_save_reader_choice_number(&f, versions, 9);
  qa_app_unified_frame_envelope envelope;
  envelope.epoch = integer_min_field(&r, "epoch", 1);
  envelope.tree = tree;
  qa_cleanup_pop(&tree_guard);
  return envelope;
}
static void read_native_camera(const qa_save_reader *camera, qa_app_unified_presentation_frame_t *out) {
  qa_app_unified_native_camera_frame *native = &out->native_camera;
  qa_save_reader owner = field_of(camera, "owner");
  native->owner = qa_app_unified_read_component_owner(&owner);
  qa_save_reader identity = field_of(camera, "identity");
  native->identity = qa_read_mod_identity(&identity);
  out->has_native_camera = true;
  native->identity_wire = identity.value;
  native->generation = integer_min_field(camera, "generation", 0);
  qa_save_reader view = field_of(camera, "view");
  native->view = qa_app_unified_read_native_camera_view(&view);
}
/* Each list is allocated zeroed and its count set before any element read can raise. */
#define READ_LIST(reader_expr, target, count_field, type) \
  qa_save_reader list_reader = (reader_expr); \
  size_t list_count = qa_save_reader_list_count(&list_reader); \
  type *list = xcalloc(list_count, sizeof *list); \
  (target) = list; \
  (count_field) = list_count
static void decode_frame_value(const qa_save_reader *r, const qa_app_unified_frame_decoder *context,
    qa_app_unified_presentation_frame_t *out) {
  const qa_app_unified_identity_decoder *identity = &context->identity;
  qa_save_reader output = field_of(r, "output"), s = field_of(&output, "snapshot");
  qa_save_reader scene = field_of(&s, "scene"), w = field_of(&scene, "world");
  if (qa_save_reader_is_null(&w)) {
    if (context->world) qa_save_reader_fail(&w, "missing negotiated world");
  } else {
    if (!context->world) qa_save_reader_fail(&w, "world is not locally loaded");
    qa_app_unified_resource_key key = read_key(&w);
    match_resource(&key, &context->world->resource);
  }
  qa_world_snapshot *snapshot = &out->output.snapshot;
  {
    READ_LIST(field_of(&scene, "entities"), snapshot->scene.entities, snapshot->scene.entity_count, qa_scene_entity);
    for (size_t i = 0; i < list_count; i++) {
      qa_save_reader row = item_of(&list_reader, i);
      read_entity(&row, context, 0, &list[i]);
    }
  }
  qa_save_reader player = field_of(r, "player");
  qa_save_reader camera = field_of(r, "nativeCamera");
  if (!absent(&camera)) read_native_camera(&camera, out);
  out->epoch = integer_min_field(r, "epoch", 0);
  qa_save_reader components = field_of(r, "components");
  if (!absent(&components)) {
    out->components = qa_app_unified_read_component_frames(&components, identity);
    out->has_components = true;
  }
  out->acknowledged_input = integer_min_field(r, "acknowledgedInput", -1);
  qa_save_reader prediction = field_of(r, "prediction");
  size_t prediction_length = 0;
  const uint8_t *prediction_bytes = qa_save_reader_bytes(&prediction, &prediction_length);
  out->prediction = qa_app_unified_decode_prediction(prediction_bytes, prediction_length, identity);
  snapshot->session = identity->session;
  qa_save_reader frame = field_of(&s, "frame");
  snapshot->frame = qa_read_frame(&frame);
  {
    READ_LIST(field_of(&s, "actors"), snapshot->actors, snapshot->actor_count, qa_actor_observation);
    for (size_t i = 0; i < list_count; i++) {
      qa_save_reader a = item_of(&list_reader, i);
      list[i].id = actor_field(&a, "id", identity);
      qa_save_reader owner = field_of(&a, "owner");
      list[i].owner = qa_namespaced(&owner);
      qa_save_reader definition = field_of(&a, "definition");
      list[i].definition = qa_namespaced(&definition);
    }
  }
  {
    READ_LIST(field_of(&s, "bodies"), snapshot->bodies, snapshot->body_count, qa_body_snapshot);
    for (size_t i = 0; i < list_count; i++) {
      qa_save_reader b = item_of(&list_reader, i);
      qa_save_reader state = field_of(&b, "body"), bounds = field_of(&state, "bounds");
      list[i].actor = actor_field(&b, "actor", identity);
      list[i].body.origin = vec_field(&state, "origin");
      list[i].body.angles = vec_field(&state, "angles");
      list[i].body.velocity = vec_field(&state, "velocity");
      list[i].body.bounds.min = vec_field(&bounds, "min");
      list[i].body.bounds.max = vec_field(&bounds, "max");
      qa_save_reader ground = field_of(&state, "ground");
      if (!qa_save_reader_is_null(&ground))
        list[i].body.ground = owned_actor(qa_app_unified_read_actor(&ground, identity));
    }
  }
  {
    READ_LIST(field_of(&s, "inventories"), snapshot->inventories, snapshot->inventory_count, qa_inventory_snapshot);
    for (size_t i = 0; i < list_count; i++) {
      qa_save_reader row = item_of(&list_reader, i);
      list[i].actor = actor_field(&row, "actor", identity);
      qa_save_reader entries = field_of(&row, "entries");
      size_t entry_count = qa_save_reader_list_count(&entries);
      qa_inventory_entry *values = xcalloc(entry_count, sizeof *values);
      list[i].entries = values;
      list[i].entry_count = entry_count;
      for (size_t j = 0; j < entry_count; j++) {
        qa_save_reader entry = item_of(&entries, j);
        values[j] = qa_read_inventory_entry(&entry);
      }
    }
  }
  {
    READ_LIST(field_of(&s, "configurations"), snapshot->configurations, snapshot->configuration_count,
        qa_actor_configuration);
    for (size_t i = 0; i < list_count; i++) {
      qa_save_reader c = item_of(&list_reader, i);
      list[i].actor = actor_field(&c, "actor", identity);
      qa_save_reader f = field_of(&c, "movement");
      list[i].movement = qa_read_provider(&f);
      f = field_of(&c, "character");
      list[i].character = qa_read_character(&f);
      qa_save_reader weapons = field_of(&c, "weapons");
      size_t weapon_count = qa_save_reader_list_count(&weapons);
      qa_provider_reference *values = xcalloc(weapon_count, sizeof *values);
      list[i].weapons.values = values;
      list[i].weapons.count = weapon_count;
      for (size_t j = 0; j < weapon_count; j++) {
        qa_save_reader weapon = item_of(&weapons, j);
        values[j] = qa_read_provider(&weapon);
      }
      f = field_of(&c, "inventory");
      list[i].inventory = qa_read_provider(&f);
    }
  }
  snapshot->scene.session = identity->session;
  qa_save_reader time = field_of(&scene, "time");
  snapshot->scene.time = qa_read_time(&time);
  snapshot->scene.world = context->world;
  {
    READ_LIST(field_of(&scene, "lights"), snapshot->scene.lights, snapshot->scene.light_count, qa_scene_light);
    for (size_t i = 0; i < list_count; i++) {
      qa_save_reader row = item_of(&list_reader, i);
      list[i] = read_light(&row);
    }
  }
  {
    READ_LIST(field_of(&scene, "particles"), snapshot->scene.particles, snapshot->scene.particle_count,
        qa_scene_particle);
    for (size_t i = 0; i < list_count; i++) {
      qa_save_reader row = item_of(&list_reader, i);
      list[i] = read_particle(&row);
    }
  }
  {
    READ_LIST(field_of(&scene, "lightStyles"), snapshot->scene.light_styles, snapshot->scene.light_style_count,
        qa_scene_light_style);
    for (size_t i = 0; i < list_count; i++) {
      qa_save_reader row = item_of(&list_reader, i);
      list[i] = read_style(&row);
    }
  }
  qa_save_reader area = field_of(&scene, "areaBits");
  if (!qa_save_reader_is_null(&area)) {
    size_t area_length = 0;
    const uint8_t *area_bytes = qa_save_reader_bytes(&area, &area_length);
    qa_byte_view *view = xcalloc(1, sizeof *view);
    snapshot->scene.area_bits = view;
    uint8_t *copy = xcalloc(area_length, 1);
    if (area_length) memcpy(copy, area_bytes, area_length);
    view->bytes = copy;
    view->length = area_length;
  }
  {
    qa_save_reader events = field_of(&output, "events");
    size_t event_count = qa_save_reader_list_count(&events);
    out->events = xcalloc(event_count, sizeof *out->events);
    for (size_t i = 0; i < event_count; i++) {
      qa_save_reader row = item_of(&events, i);
      out->events[i] = qa_app_unified_read_simulation_event(row.value, identity);
      if (!out->events[i]) qa_range_error("out of memory");
      out->event_count = i + 1;
    }
  }
  {
    READ_LIST(field_of(r, "models"), out->models, out->model_count, qa_app_simulation_presentation);
    for (size_t i = 0; i < list_count; i++) {
      qa_save_reader row = item_of(&list_reader, i);
      list[i] = qa_app_unified_read_model(&row, identity);
    }
  }
  {
    READ_LIST(field_of(r, "characters"), out->characters, out->character_count, qa_q3_character_view);
    for (size_t i = 0; i < list_count; i++) {
      qa_save_reader row = item_of(&list_reader, i);
      list[i] = qa_app_unified_read_character_view(&row, identity);
    }
  }
  {
    READ_LIST(field_of(r, "worldText"), out->world_text, out->world_text_count, qa_world_text);
    for (size_t i = 0; i < list_count; i++) {
      qa_save_reader row = item_of(&list_reader, i);
      list[i] = qa_app_unified_read_world_text(&row);
    }
  }
  out->player_actor = actor_field(&player, "actor", identity);
  qa_save_reader view = field_of(&player, "view");
  out->player_view = qa_app_unified_read_player_view(&view);
  qa_save_reader ui = field_of(&player, "ui");
  out->player_ui = qa_app_unified_read_player_ui(&ui);
}
#undef READ_LIST
qa_app_unified_presentation_frame_t qa_app_unified_decode_frame_envelope(
    qa_app_unified_frame_envelope *envelope, const qa_app_unified_frame_decoder *decoder) {
  qa_app_unified_presentation_frame_t out;
  memset(&out, 0, sizeof out);
  out.tree = envelope->tree;
  envelope->tree = NULL;
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_frame, &out);
  qa_save_reader r;
  if (!qa_save_reader_init(&r, out.tree, "unified-frame")) qa_range_error("out of memory");
  decode_frame_value(&r, decoder, &out);
  qa_cleanup_pop(&guard);
  return out;
}
qa_app_unified_presentation_frame_t qa_app_unified_decode_frame(
    const uint8_t *bytes, size_t length, const qa_app_unified_frame_decoder *decoder) {
  qa_app_unified_frame_envelope envelope = qa_app_unified_read_frame(bytes, length);
  return qa_app_unified_decode_frame_envelope(&envelope, decoder);
}
