/* Port of src/app/bootstrap/network/unified-frame-codec.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_frame_codec.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/unified_event_codec.h"
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include "qa/content/held_weapon.h"
#include "qa/foundation/support.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/mods.h"
#include "qa/persistence/shared.h"
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
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
static void set(qa_save_value *target, const char *key, qa_save_value *child) {
  if (!child || !qa_save_value_set(target, key, child)) qa_range_error("out of memory");
}
static void push(qa_save_value *target, qa_save_value *child) {
  if (!child || !qa_save_value_push(target, child)) qa_range_error("out of memory");
}
static qa_save_value *num(double value) { return qa_save_value_number(value); }
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
      if (out) for (size_t i = 0; i < value->item_count; i++) push(out, clone_value(value->items[i]));
      break;
    case QA_SAVE_OBJECT:
      out = qa_save_value_object();
      if (out) for (size_t i = 0; i < value->entry_count; i++)
        set(out, value->entries[i].key, clone_value(value->entries[i].value));
      break;
  }
  if (!out) qa_range_error("out of memory");
  return out;
}
static qa_save_value *write_vector(qa_vec3 v) {
  qa_save_value *out = object();
  set(out, "x", num(v.x)); set(out, "y", num(v.y)); set(out, "z", num(v.z));
  return out;
}
static qa_save_value *write_color(qa_vec4 v) {
  qa_save_value *out = write_vector((qa_vec3){v.x, v.y, v.z});
  set(out, "w", num(v.w));
  return out;
}
static qa_save_value *write_bounds(qa_bounds bounds) {
  qa_save_value *out = object();
  set(out, "min", write_vector(bounds.min));
  set(out, "max", write_vector(bounds.max));
  return out;
}
static qa_save_value *write_time(qa_source_time time) {
  qa_save_value *out = object();
  set(out, "kind", qa_save_value_string(time.kind == QA_SOURCE_TIME_SECONDS ? "seconds" : "milliseconds"));
  set(out, "value", num(time.value));
  return out;
}
static qa_save_value *write_key(const qa_resolved_resource_reference *reference) {
  qa_app_unified_resource_key key = qa_app_unified_resource_key_from_reference(reference);
  qa_save_value *out = qa_app_unified_write_resource_key(&key);
  if (!out) qa_range_error("out of memory");
  return out;
}
typedef const qa_app_unified_identity_encoder encoder_t;
static qa_save_value *encode_actor(encoder_t *identity, qa_actor_id actor) {
  qa_app_unified_actor_reference reference = identity->actor(identity->ctx, actor);
  qa_save_value *out = object();
  set(out, "slot", num(reference.slot));
  set(out, "generation", num(reference.generation));
  return out;
}
static qa_save_value *write_body(encoder_t *identity, const qa_body_state *state) {
  qa_save_value *out = object();
  set(out, "origin", write_vector(state->origin));
  set(out, "angles", write_vector(state->angles));
  set(out, "velocity", write_vector(state->velocity));
  set(out, "bounds", write_bounds(state->bounds));
  if (state->ground) set(out, "ground", encode_actor(identity, *state->ground));
  else set(out, "ground", qa_save_value_null());
  return out;
}
static qa_save_value *write_inventory_entries(const qa_inventory_entry *entries, size_t count) {
  qa_save_value *list = array();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *entry = object();
    set(entry, "item", qa_save_value_string(entries[i].item));
    set(entry, "count", num(entries[i].count));
    push(list, entry);
  }
  return list;
}
static qa_save_value *write_provider(qa_provider_reference reference) {
  qa_save_value *out = object();
  set(out, "provider", qa_save_value_string(reference.provider));
  set(out, "content", qa_save_value_string(reference.content));
  return out;
}
static qa_save_value *write_configuration(encoder_t *identity, const qa_actor_configuration *c) {
  qa_save_value *out = object();
  set(out, "actor", encode_actor(identity, c->actor));
  set(out, "movement", write_provider(c->movement));
  qa_save_value *character = object();
  set(character, "definition", write_provider(c->character.definition));
  set(character, "appearance", write_provider(c->character.appearance));
  set(out, "character", character);
  qa_save_value *weapons = array();
  for (size_t i = 0; i < c->weapons.count; i++) push(weapons, write_provider(c->weapons.values[i]));
  set(out, "weapons", weapons);
  set(out, "inventory", write_provider(c->inventory));
  return out;
}
static qa_save_value *write_pose(const qa_model_pose *pose) {
  qa_save_value *out = object();
  if (pose->is_frame) {
    set(out, "kind", qa_save_value_string("frame"));
    set(out, "frame", num(pose->frame));
    set(out, "previousFrame", num(pose->previous_frame));
    set(out, "backLerp", num(pose->back_lerp));
  } else {
    set(out, "kind", qa_save_value_string("skeleton"));
    qa_save_value *joints = array();
    for (size_t i = 0; i < pose->joint_count; i++) {
      qa_save_value *joint = object();
      set(joint, "position", write_vector(pose->joints[i].position));
      set(joint, "orientation", write_color(pose->joints[i].orientation));
      set(joint, "scale", num(pose->joints[i].scale));
      push(joints, joint);
    }
    set(out, "joints", joints);
  }
  return out;
}
static qa_save_value *encode_entity(encoder_t *identity, const qa_scene_entity *entity) {
  qa_save_value *out = object();
  if (entity->has_opacity) set(out, "opacity", num(entity->opacity));
  if (entity->actor) set(out, "actor", encode_actor(identity, *entity->actor));
  else set(out, "actor", qa_save_value_null());
  set(out, "resource", write_key(&entity->resource));
  qa_save_value *transform = object();
  set(transform, "origin", write_vector(entity->transform.origin));
  qa_save_value *axis = array();
  for (int a = 0; a < 3; a++) push(axis, write_vector(entity->transform.axis.vectors[a]));
  set(transform, "axis", axis);
  set(transform, "scale", write_vector(entity->transform.scale));
  set(out, "transform", transform);
  set(out, "previousOrigin", write_vector(entity->previous_origin));
  set(out, "pose", write_pose(&entity->pose));
  set(out, "skin", num(entity->skin));
  set(out, "color", write_color(entity->color));
  set(out, "shaderTime", write_time(entity->shader_time));
  qa_save_value *flags = object();
  set(flags, "kind", qa_save_value_string(entity->flags_kind));
  set(flags, "bits", num(entity->flags_bits));
  set(out, "flags", flags);
  set(out, "lightingOrigin", write_vector(entity->lighting_origin));
  set(out, "shadowPlane", num(entity->shadow_plane));
  qa_save_value *attachments = array();
  for (size_t i = 0; i < entity->attachment_count; i++) {
    qa_save_value *attachment = object();
    set(attachment, "tag", qa_save_value_string(entity->attachments[i].tag));
    set(attachment, "entity", encode_entity(identity, entity->attachments[i].entity));
    push(attachments, attachment);
  }
  set(out, "attachments", attachments);
  return out;
}
static qa_save_value *write_light(const qa_scene_light *light) {
  qa_save_value *out = object();
  set(out, "origin", write_vector(light->origin));
  set(out, "color", write_vector(light->color));
  set(out, "radius", num(light->radius));
  set(out, "additive", qa_save_value_boolean(light->additive));
  qa_save_value *profile = object();
  set(profile, "kind", qa_save_value_string(light->profile.kind));
  if (strcmp(light->profile.kind, "q2") == 0) {
    set(profile, "scale", num(light->profile.scale));
    if (light->profile.has_cone) {
      qa_save_value *cone = object();
      set(cone, "direction", write_vector(light->profile.cone_direction));
      set(cone, "cosHalfAngle", num(light->profile.cone_cos_half_angle));
      set(profile, "cone", cone);
    } else set(profile, "cone", qa_save_value_null());
    qa_save_value *shadow = object();
    if (light->profile.shadow_cast) {
      set(shadow, "kind", qa_save_value_string("cast"));
      set(shadow, "resolution", num(light->profile.shadow_resolution));
    } else set(shadow, "kind", qa_save_value_string("none"));
    set(profile, "shadow", shadow);
  }
  set(out, "profile", profile);
  return out;
}
static qa_save_value *write_particle(const qa_scene_particle *particle) {
  qa_save_value *out = object();
  set(out, "origin", write_vector(particle->origin));
  set(out, "size", num(particle->size));
  if (!particle->is_rgba) {
    set(out, "kind", qa_save_value_string("indexed"));
    set(out, "paletteIndex", num(particle->palette_index));
    set(out, "alpha", num(particle->alpha));
  } else {
    set(out, "kind", qa_save_value_string("rgba"));
    set(out, "color", write_color(particle->color));
    set(out, "rotation", num(particle->rotation));
  }
  return out;
}
static qa_save_value *write_style(const qa_scene_light_style *style) {
  qa_save_value *out = object();
  if (!style->is_q2) {
    set(out, "kind", qa_save_value_string("q1"));
    set(out, "style", num(style->style));
    set(out, "value", num(style->value));
  } else {
    set(out, "kind", qa_save_value_string("q2"));
    set(out, "style", num(style->style));
    set(out, "rgb", write_vector(style->rgb));
    set(out, "white", num(style->white));
  }
  return out;
}
static qa_save_value *write_player_view_value(const qa_app_sim_player_view *view) {
  qa_save_value *out = object();
  set(out, "origin", write_vector(view->origin));
  set(out, "angles", write_vector(view->angles));
  set(out, "viewHeight", num(view->view_height));
  if (view->has_blend) set(out, "blend", write_color(view->blend));
  if (view->has_damage_blend) set(out, "damageBlend", write_color(view->damage_blend));
  if (view->has_kick_angles) set(out, "kickAngles", write_vector(view->kick_angles));
  if (view->has_field_of_view) set(out, "fieldOfView", num(view->field_of_view));
  if (view->has_client_view_offset_delta)
    set(out, "clientViewOffsetDelta", write_vector(view->client_view_offset_delta));
  if (view->foreign_character_death) set(out, "foreignCharacterDeath", qa_save_value_boolean(true));
  if (view->has_pitch_drift) {
    qa_save_value *drift = object();
    set(drift, "grounded", qa_save_value_boolean(view->pitch_drift_grounded));
    set(drift, "idealPitch", num(view->pitch_drift_ideal_pitch));
    set(drift, "disabled", qa_save_value_boolean(view->pitch_drift_disabled));
    set(out, "pitchDrift", drift);
  }
  return out;
}
static qa_save_value *write_player_ui_value(const qa_app_sim_player_ui *ui) {
  qa_save_value *out = object();
  set(out, "health", num(ui->health));
  {
    qa_save_value *armor = object();
    qa_save_value *regular = object();
    const char *regular_kind = ui->armor.regular.kind == QA_REGULAR_ARMOR_Q1 ? "q1"
        : ui->armor.regular.kind == QA_REGULAR_ARMOR_Q2 ? "q2"
        : ui->armor.regular.kind == QA_REGULAR_ARMOR_Q3 ? "q3"
        : ui->armor.regular.kind == QA_REGULAR_ARMOR_SOURCE ? "source" : "none";
    set(regular, "kind", qa_save_value_string(regular_kind));
    if (ui->armor.regular.kind != QA_REGULAR_ARMOR_NONE) {
      set(regular, "points", num(ui->armor.regular.points));
      if (ui->armor.regular.kind == QA_REGULAR_ARMOR_Q1) {
        set(regular, "absorption", num(ui->armor.regular.absorption));
        set(regular, "item", qa_save_value_string(ui->armor.regular.item));
      } else if (ui->armor.regular.kind == QA_REGULAR_ARMOR_Q2) {
        set(regular, "normalProtection", num(ui->armor.regular.normal_protection));
        set(regular, "energyProtection", num(ui->armor.regular.energy_protection));
        set(regular, "item", qa_save_value_string(ui->armor.regular.item));
      } else if (ui->armor.regular.kind == QA_REGULAR_ARMOR_Q3) {
        set(regular, "protection", num(ui->armor.regular.protection));
      } else {
        if (ui->armor.regular.item) set(regular, "item", qa_save_value_string(ui->armor.regular.item));
        else set(regular, "item", qa_save_value_null());
      }
    }
    set(armor, "regular", regular);
    qa_save_value *powered = object();
    set(powered, "kind", qa_save_value_string(ui->armor.powered.kind));
    if (strcmp(ui->armor.powered.kind, "none") != 0)
      set(powered, "cells", num(ui->armor.powered.cells));
    set(armor, "powered", powered);
    set(out, "armor", armor);
  }
  if (ui->active_weapon) set(out, "activeWeapon", qa_save_value_string(ui->active_weapon));
  else set(out, "activeWeapon", qa_save_value_null());
  if (!ui->has_ammo) {
    set(out, "ammo", qa_save_value_null());
  } else {
    qa_save_value *ammo = object();
    set(ammo, "item", qa_save_value_string(ui->ammo_item));
    set(ammo, "count", num(ui->ammo_count));
    set(out, "ammo", ammo);
  }
  set(out, "inventory", write_inventory_entries(ui->inventory, ui->inventory_count));
  set(out, "arsenalWarning", qa_save_value_string(ui->arsenal_warning));
  qa_save_value *powerups = array();
  for (size_t i = 0; i < ui->powerup_count; i++) {
    qa_save_value *row = object();
    set(row, "item", qa_save_value_string(ui->powerups[i].item));
    set(row, "label", qa_save_value_string(ui->powerups[i].label));
    set(row, "remainingSeconds", num(ui->powerups[i].remaining_seconds));
    push(powerups, row);
  }
  set(out, "powerups", powerups);
  qa_save_value *items = array();
  for (size_t i = 0; i < ui->item_count; i++) {
    const qa_app_sim_player_ui_item *item = &ui->items[i];
    qa_save_value *row = object();
    set(row, "id", qa_save_value_string(item->id));
    set(row, "label", qa_save_value_string(item->label));
    set(row, "kind", qa_save_value_string(item->kind));
    set(row, "sourceOrdinal", num(item->source_ordinal));
    set(row, "owned", qa_save_value_boolean(item->owned));
    set(row, "hasAmmo", qa_save_value_boolean(item->has_ammo));
    if (item->has_count) set(row, "count", num(item->count));
    else set(row, "count", qa_save_value_null());
    set(row, "warningCount", num(item->warning_count));
    push(items, row);
  }
  set(out, "items", items);
  if (ui->selected_arsenal) set(out, "selectedArsenal", qa_save_value_boolean(true));
  if (ui->weapon_status) {
    const qa_weapon_hud_status *status = ui->weapon_status;
    qa_save_value *out_status = object();
    qa_save_value *ammo = object();
    if (status->ammo_is_finite) {
      set(ammo, "kind", qa_save_value_string("finite"));
      set(ammo, "item", qa_save_value_string(status->ammo_item));
      set(ammo, "count", num(status->ammo_count));
      set(ammo, "hasAmmoToStart", qa_save_value_boolean(status->has_ammo_to_start));
      set(ammo, "low", qa_save_value_boolean(status->low));
    } else set(ammo, "kind", qa_save_value_string("unmetered"));
    set(out_status, "ammo", ammo);
    set(out_status, "source", write_provider(status->source));
    set(out_status, "item", qa_save_value_string(status->item));
    set(out_status, "label", qa_save_value_string(status->label));
    set(out, "weaponStatus", out_status);
  } else set(out, "weaponStatus", qa_save_value_null());
  if (ui->native_inventory) set(out, "nativeInventory", clone_value((const qa_save_value *)ui->native_inventory));
  return out;
}
static qa_save_value *write_model_value(encoder_t *identity, const qa_app_simulation_presentation *model);
static qa_save_value *write_character_value(encoder_t *identity, const qa_q3_character_view *view);
static qa_save_value *write_world_text_value(const qa_world_text *text);
uint8_t *qa_app_unified_encode_frame(const qa_app_unified_presentation_frame_t *frame,
    const qa_app_unified_identity_encoder *identity, size_t *out_length) {
  const qa_world_snapshot *snapshot = &frame->output.snapshot;
  qa_save_value *out = object();
  set(out, "schema", qa_save_value_string("qts-frame"));
  set(out, "version", num(9));
  set(out, "epoch", num(frame->epoch));
  set(out, "frame", num(snapshot->frame.frame));
  set(out, "acknowledgedInput", num(frame->acknowledged_input));
  if (frame->has_components)
    set(out, "components", qa_app_unified_write_component_frames(&frame->components));
  if (frame->has_native_camera) {
    qa_save_value *camera = object();
    qa_save_value *owner = object();
    set(owner, "provider", qa_save_value_string(frame->native_camera.owner.provider));
    set(owner, "generation", num(frame->native_camera.owner.generation));
    set(camera, "owner", owner);
    set(camera, "identity", clone_value(frame->native_camera.identity_wire));
    set(camera, "generation", num(frame->native_camera.generation));
    /* Native camera view writer mirrors unified-native-components. */
    qa_app_sim_player_view view;
    memset(&view, 0, sizeof view);
    view.origin = frame->native_camera.view.view.origin;
    view.angles = frame->native_camera.view.view.angles;
    view.view_height = frame->native_camera.view.view.view_height;
    view.has_blend = frame->native_camera.view.view.has_blend;
    view.blend = frame->native_camera.view.view.blend;
    view.has_damage_blend = frame->native_camera.view.view.has_damage_blend;
    view.damage_blend = frame->native_camera.view.view.damage_blend;
    view.has_kick_angles = frame->native_camera.view.view.has_kick_angles;
    view.kick_angles = frame->native_camera.view.view.kick_angles;
    view.has_field_of_view = frame->native_camera.view.view.has_field_of_view;
    view.field_of_view = frame->native_camera.view.view.field_of_view;
    view.has_client_view_offset_delta = frame->native_camera.view.view.has_client_view_offset_delta;
    view.client_view_offset_delta = frame->native_camera.view.view.client_view_offset_delta;
    view.foreign_character_death = frame->native_camera.view.view.foreign_character_death;
    view.has_pitch_drift = frame->native_camera.view.view.has_pitch_drift;
    view.pitch_drift_grounded = frame->native_camera.view.view.pitch_drift_grounded;
    view.pitch_drift_ideal_pitch = frame->native_camera.view.view.pitch_drift_ideal_pitch;
    view.pitch_drift_disabled = frame->native_camera.view.view.pitch_drift_disabled;
    qa_save_value *view_value = write_player_view_value(&view);
    qa_save_value *native = object();
    set(native, "edition", qa_save_value_string(frame->native_camera.view.edition));
    set(native, "movementOrigin", write_vector(frame->native_camera.view.movement_origin));
    set(native, "renderFlags", num(frame->native_camera.view.render_flags));
    set(native, "positionPrediction", qa_save_value_boolean(frame->native_camera.view.position_prediction));
    set(native, "angularPrediction", qa_save_value_boolean(frame->native_camera.view.angular_prediction));
    set(native, "weaponVisible", qa_save_value_boolean(frame->native_camera.view.weapon_visible));
    set(view_value, "native", native);
    set(camera, "view", view_value);
    set(out, "nativeCamera", camera);
  }
  size_t prediction_length = 0;
  uint8_t *prediction = qa_app_unified_encode_prediction(&frame->prediction, &prediction_length);
  if (!prediction) qa_range_error("out of memory");
  set(out, "prediction", qa_save_value_bytes(prediction, prediction_length));
  free(prediction);
  qa_save_value *actors = array();
  for (size_t i = 0; i < snapshot->actor_count; i++) {
    qa_save_value *row = object();
    set(row, "id", encode_actor(identity, snapshot->actors[i].id));
    set(row, "owner", qa_save_value_string(snapshot->actors[i].owner));
    set(row, "definition", qa_save_value_string(snapshot->actors[i].definition));
    push(actors, row);
  }
  set(out, "actors", actors);
  qa_save_value *bodies = array();
  for (size_t i = 0; i < snapshot->body_count; i++) {
    qa_save_value *row = object();
    set(row, "actor", encode_actor(identity, snapshot->bodies[i].actor));
    set(row, "body", write_body(identity, &snapshot->bodies[i].body));
    push(bodies, row);
  }
  set(out, "bodies", bodies);
  qa_save_value *inventories = array();
  for (size_t i = 0; i < snapshot->inventory_count; i++) {
    qa_save_value *row = object();
    set(row, "actor", encode_actor(identity, snapshot->inventories[i].actor));
    set(row, "entries", write_inventory_entries(
        snapshot->inventories[i].entries, snapshot->inventories[i].entry_count));
    push(inventories, row);
  }
  set(out, "inventories", inventories);
  qa_save_value *configs = array();
  for (size_t i = 0; i < snapshot->configuration_count; i++)
    push(configs, write_configuration(identity, &snapshot->configurations[i]));
  set(out, "configurations", configs);
  qa_save_value *scene = object();
  if (!identity->session_matches(identity->ctx, snapshot->scene.session))
    qa_range_error("Unified frame scene belongs to another session");
  set(scene, "time", write_time(snapshot->scene.time));
  qa_save_value *entities = array();
  for (size_t i = 0; i < snapshot->scene.entity_count; i++)
    push(entities, encode_entity(identity, &snapshot->scene.entities[i]));
  set(scene, "entities", entities);
  qa_save_value *lights = array();
  for (size_t i = 0; i < snapshot->scene.light_count; i++)
    push(lights, write_light(&snapshot->scene.lights[i]));
  set(scene, "lights", lights);
  qa_save_value *particles = array();
  for (size_t i = 0; i < snapshot->scene.particle_count; i++)
    push(particles, write_particle(&snapshot->scene.particles[i]));
  set(scene, "particles", particles);
  qa_save_value *styles = array();
  for (size_t i = 0; i < snapshot->scene.light_style_count; i++)
    push(styles, write_style(&snapshot->scene.light_styles[i]));
  set(scene, "lightStyles", styles);
  if (snapshot->scene.area_bits)
    set(scene, "areaBits", qa_save_value_bytes(
        snapshot->scene.area_bits->bytes, snapshot->scene.area_bits->length));
  else set(scene, "areaBits", qa_save_value_null());
  set(out, "scene", scene);
  qa_save_value *events = array();
  for (size_t i = 0; i < frame->event_count; i++) {
    qa_save_value *written = qa_app_unified_write_simulation_event(frame->events[i]);
    push(events, written);
  }
  set(out, "events", events);
  qa_save_value *models = array();
  for (size_t i = 0; i < frame->model_count; i++)
    push(models, write_model_value(identity, &frame->models[i]));
  set(out, "models", models);
  qa_save_value *characters = array();
  for (size_t i = 0; i < frame->character_count; i++)
    push(characters, write_character_value(identity, &frame->characters[i]));
  set(out, "characters", characters);
  qa_save_value *texts = array();
  for (size_t i = 0; i < frame->world_text_count; i++)
    push(texts, write_world_text_value(&frame->world_text[i]));
  set(out, "worldText", texts);
  qa_save_value *player = object();
  set(player, "actor", encode_actor(identity, frame->player_actor));
  set(player, "view", write_player_view_value(&frame->player_view));
  set(player, "ui", write_player_ui_value(&frame->player_ui));
  set(out, "player", player);
  uint8_t *bytes = qa_encode_checkpoint_value(out, out_length);
  qa_save_value_free(out);
  if (bytes && *out_length > 32 * 1024 * 1024) {
    free(bytes);
    qa_range_error("Unified frame exceeds byte limit");
  }
  return bytes;
}
static qa_save_value *write_axis(const qa_axis *axis) {
  qa_save_value *out = array();
  for (int i = 0; i < 3; i++) push(out, write_vector(axis->vectors[i]));
  return out;
}
static qa_save_value *write_held_weapon(const qa_held_weapon_declaration *declaration) {
  qa_save_value *out = object();
  if (!declaration->is_model) {
    set(out, "kind", qa_save_value_string("none"));
    return out;
  }
  set(out, "kind", qa_save_value_string("model"));
  qa_save_value *model = object();
  if (declaration->model.has_digest)
    set(model, "digest", qa_save_value_string(declaration->model.digest));
  set(model, "path", qa_save_value_string(declaration->model.path));
  set(model, "referenceFrame", num(declaration->model.reference_frame));
  qa_save_value *grip = object();
  set(grip, "origin", write_vector(declaration->model.grip.origin));
  set(grip, "axis", write_axis(&declaration->model.grip.axis));
  set(grip, "scale", write_vector(declaration->model.grip.scale));
  set(model, "grip", grip);
  if (declaration->model.fallback)
    set(model, "fallback", qa_save_value_string(declaration->model.fallback));
  if (declaration->model.part) {
    qa_save_value *part = object();
    qa_save_value *digests = array();
    for (size_t i = 0; i < declaration->model.part->digests.count; i++)
      push(digests, qa_save_value_string(declaration->model.part->digests.values[i]));
    set(part, "digests", digests);
    qa_save_value *vertices = array();
    for (size_t i = 0; i < declaration->model.part->vertices.count; i++)
      push(vertices, num(declaration->model.part->vertices.values[i]));
    set(part, "vertices", vertices);
    set(model, "part", part);
  }
  set(out, "model", model);
  return out;
}
static const char *family_name(qa_game_family family) {
  switch (family) {
    case QA_GAME_FAMILY_Q1: return "q1";
    case QA_GAME_FAMILY_Q2: return "q2";
    case QA_GAME_FAMILY_Q3: return "q3";
  }
  return "q1";
}
static qa_save_value *write_model_value(encoder_t *identity, const qa_app_simulation_presentation *model) {
  qa_save_value *out = object();
  set(out, "actor", encode_actor(identity, model->actor));
  set(out, "content", qa_save_value_string(model->content));
  set(out, "family", qa_save_value_string(family_name(model->family)));
  set(out, "path", qa_save_value_string(model->path));
  set(out, "frame", num(model->frame));
  set(out, "oldFrame", num(model->old_frame));
  set(out, "skin", num(model->skin));
  set(out, "effects", num(model->effects));
  set(out, "renderFlags", num(model->render_flags));
  set(out, "origin", write_vector(model->origin));
  set(out, "angles", write_vector(model->angles));
  set(out, "scale", num(model->scale));
  set(out, "visible", qa_save_value_boolean(model->visible));
  set(out, "viewWeapon", qa_save_value_boolean(model->view_weapon));
  if (model->held_weapon)
    set(out, "heldWeapon", write_held_weapon((const qa_held_weapon_declaration *)model->held_weapon));
  if (model->native_held_weapon) set(out, "nativeHeldWeapon", qa_save_value_boolean(true));
  if (model->weapon_item) set(out, "weaponItem", qa_save_value_string(model->weapon_item));
  if (model->has_flare) {
    qa_save_value *flare = object();
    set(flare, "image", qa_save_value_string(model->flare.image));
    set(flare, "fadeStart", num(model->flare.fade_start));
    set(flare, "fadeEnd", num(model->flare.fade_end));
    set(flare, "scale", num(model->flare.scale));
    set(flare, "color", write_vector(model->flare.color));
    if (model->flare.rim_color) set(flare, "rimColor", write_vector(*model->flare.rim_color));
    else set(flare, "rimColor", qa_save_value_null());
    set(flare, "lockAngle", qa_save_value_boolean(model->flare.lock_angle));
    set(out, "flare", flare);
  }
  if (model->has_back_lerp) set(out, "backLerp", num(model->back_lerp));
  if (model->skin_path || model->has_skin_path) {
    if (model->skin_path) set(out, "skinPath", qa_save_value_string(model->skin_path));
    else set(out, "skinPath", qa_save_value_null());
  }
  if (model->has_indexed_skin) {
    qa_save_value *skin = object();
    set(skin, "name", qa_save_value_string(model->indexed_skin.name));
    set(skin, "width", num(model->indexed_skin.width));
    set(skin, "height", num(model->indexed_skin.height));
    set(skin, "pixels", qa_save_value_bytes(model->indexed_skin.pixels.bytes,
        model->indexed_skin.pixels.length));
    set(out, "indexedSkin", skin);
  }
  if (model->has_player_colors) {
    qa_save_value *colors = object();
    set(colors, "top", num(model->player_color_top));
    set(colors, "bottom", num(model->player_color_bottom));
    set(out, "playerColors", colors);
  }
  if (model->has_previous_origin) set(out, "previousOrigin", write_vector(model->previous_origin));
  if (model->has_model_beam) {
    qa_save_value *beam = object();
    set(beam, "segmentLength", num(model->model_beam_segment_length));
    set(out, "modelBeam", beam);
  }
  if (model->has_alpha) set(out, "alpha", num(model->alpha));
  if (model->has_shader_beam) {
    qa_save_value *beam = object();
    set(beam, "path", qa_save_value_string(model->shader_beam_path));
    set(beam, "end", write_vector(model->shader_beam_end));
    set(beam, "width", num(model->shader_beam_width));
    set(out, "shaderBeam", beam);
  }
  if (model->model_attachment_count) {
    qa_save_value *attachments = array();
    for (size_t i = 0; i < model->model_attachment_count; i++) {
      qa_save_value *row = object();
      set(row, "path", qa_save_value_string(model->model_attachments[i].path));
      set(row, "tag", qa_save_value_string(model->model_attachments[i].tag));
      push(attachments, row);
    }
    set(out, "modelAttachments", attachments);
  }
  if (model->model_anchor) set(out, "modelAnchor", clone_value((const qa_save_value *)model->model_anchor));
  if (model->replaces_body) set(out, "replacesBody", qa_save_value_boolean(true));
  if (model->render_owner) set(out, "renderOwner", qa_save_value_string(model->render_owner));
  if (model->q3_grapple_cable)
    set(out, "q3GrappleCable", clone_value((const qa_save_value *)model->q3_grapple_cable));
  if (model->has_q3_weapon) {
    qa_save_value *weapon = object();
    set(weapon, "timeMilliseconds", num(model->q3_weapon_time_ms));
    set(weapon, "torsoAnimation", num(model->q3_weapon_torso_animation));
    if (model->has_q3_weapon_last_fire)
      set(weapon, "lastFireMilliseconds", num(model->q3_weapon_last_fire_ms));
    else set(weapon, "lastFireMilliseconds", qa_save_value_null());
    set(weapon, "firing", qa_save_value_boolean(model->q3_weapon_firing));
    set(weapon, "horizontalSpeed", num(model->q3_weapon_horizontal_speed));
    set(weapon, "bobCycle", num(model->q3_weapon_bob_cycle));
    set(weapon, "weapon", num(model->q3_weapon_weapon));
    set(out, "q3Weapon", weapon);
  }
  return out;
}
static qa_save_value *write_character_value(encoder_t *identity, const qa_q3_character_view *view) {
  qa_save_value *out = object();
  set(out, "actor", encode_actor(identity, view->actor));
  set(out, "origin", write_vector(view->origin));
  set(out, "angles", write_vector(view->angles));
  set(out, "velocity", write_vector(view->velocity));
  set(out, "movementDirection", num(view->movement_direction));
  set(out, "sourceFlags", num(view->source_flags));
  qa_save_value *animation = object();
  set(animation, "kind", qa_save_value_string("q3"));
  set(animation, "legs", num(view->animation.legs));
  set(animation, "torso", num(view->animation.torso));
  set(animation, "legsTimerMilliseconds", num(view->animation.legs_timer_milliseconds));
  set(animation, "torsoTimerMilliseconds", num(view->animation.torso_timer_milliseconds));
  set(out, "animation", animation);
  set(out, "powerups", num(view->powerups));
  if (view->team) set(out, "team", qa_save_value_string(view->team));
  else set(out, "team", qa_save_value_null());
  set(out, "color", write_color(view->color));
  if (view->has_scale) set(out, "scale", num(view->scale));
  if (view->has_opacity) set(out, "opacity", num(view->opacity));
  return out;
}
static qa_save_value *write_world_text_value(const qa_world_text *text) {
  qa_save_value *out = object();
  set(out, "content", qa_save_value_string(text->content));
  set(out, "text", qa_save_value_string(text->text));
  set(out, "origin", write_vector(text->origin));
  qa_save_value *orientation = object();
  if (text->orientation_fixed) {
    set(orientation, "kind", qa_save_value_string("fixed"));
    set(orientation, "angles", write_vector(text->angles));
  } else set(orientation, "kind", qa_save_value_string("billboard"));
  set(out, "orientation", orientation);
  set(out, "color", write_color(text->color));
  set(out, "cellSize", num(text->cell_size));
  set(out, "depthTest", qa_save_value_boolean(text->depth_test));
  if (text->has_distance_cull_factor) set(out, "distanceCullFactor", num(text->distance_cull_factor));
  set(out, "font", qa_save_value_string(text->font_selected ? "selected" : "classic"));
  return out;
}
/* ---------------- decode ---------------- */
static qa_save_reader field_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("out of memory");
  return out;
}
static bool field_absent(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  *out = field_of(reader, name);
  if (qa_save_reader_is_undefined(out)) { qa_save_reader_destroy(out); return true; }
  return false;
}
static double finite_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  double value = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_vec3 vec_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  qa_vec3 value = qa_app_unified_read_vector(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static void bounded_list(const qa_save_reader *reader, size_t maximum) {
  if (qa_save_reader_list_count(reader) > maximum)
    qa_save_reader_fail(reader, "unified frame list exceeds limit");
}
static qa_source_time read_time_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  qa_source_time time = qa_read_time(&f);
  qa_save_reader_destroy(&f);
  return time;
}
static qa_bounds read_bounds(const qa_save_reader *reader) {
  qa_bounds out = {vec_field(reader, "min"), vec_field(reader, "max")};
  return out;
}
static qa_body_state read_body(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity) {
  qa_body_state out;
  memset(&out, 0, sizeof out);
  out.origin = vec_field(reader, "origin");
  out.angles = vec_field(reader, "angles");
  out.velocity = vec_field(reader, "velocity");
  qa_save_reader bounds = field_of(reader, "bounds");
  out.bounds = read_bounds(&bounds);
  qa_save_reader_destroy(&bounds);
  qa_save_reader ground = field_of(reader, "ground");
  if (!qa_save_reader_is_null(&ground)) {
    qa_actor_id *owned = xmalloc(sizeof *owned);
    *owned = qa_app_unified_read_actor(&ground, identity);
    out.ground = owned;
  }
  qa_save_reader_destroy(&ground);
  return out;
}
static size_t read_inventory_entries(const qa_save_reader *reader, qa_inventory_entry **out) {
  bounded_list(reader, 1024);
  size_t count = qa_save_reader_list_count(reader);
  qa_inventory_entry *entries = xmalloc(count * sizeof *entries);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(reader, i, &row)) qa_range_error("out of memory");
    entries[i] = qa_read_inventory_entry(&row);
    qa_save_reader_destroy(&row);
  }
  *out = entries;
  return count;
}
static qa_actor_configuration read_configuration(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity) {
  qa_actor_configuration out;
  memset(&out, 0, sizeof out);
  qa_save_reader actor_reader = field_of(reader, "actor");
  out.actor = qa_app_unified_read_actor(&actor_reader, identity);
  qa_save_reader_destroy(&actor_reader);
  qa_save_reader movement = field_of(reader, "movement");
  out.movement = qa_read_provider(&movement);
  qa_save_reader_destroy(&movement);
  qa_save_reader character = field_of(reader, "character");
  out.character = qa_read_character(&character);
  qa_save_reader_destroy(&character);
  qa_save_reader weapons = field_of(reader, "weapons");
  bounded_list(&weapons, 256);
  size_t count = qa_save_reader_list_count(&weapons);
  qa_provider_reference *values = xmalloc(count * sizeof *values);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&weapons, i, &row)) qa_range_error("out of memory");
    values[i] = qa_read_provider(&row);
    qa_save_reader_destroy(&row);
  }
  out.weapons.values = values;
  out.weapons.count = count;
  qa_save_reader_destroy(&weapons);
  qa_save_reader inventory = field_of(reader, "inventory");
  out.inventory = qa_read_provider(&inventory);
  qa_save_reader_destroy(&inventory);
  return out;
}
static void read_axis_into(const qa_save_reader *reader, qa_vec3 out[3]) {
  qa_app_unified_read_axis(reader, out);
}
static qa_scene_entity read_entity(const qa_save_reader *reader,
    const qa_app_unified_frame_decoder *decoder, double depth);
static qa_scene_entity read_entity(const qa_save_reader *reader,
    const qa_app_unified_frame_decoder *decoder, double depth) {
  if (depth > 16) qa_save_reader_fail(reader, "scene attachment depth exceeds limit");
  qa_scene_entity entity;
  memset(&entity, 0, sizeof entity);
  qa_save_reader opacity, attachments;
  const qa_app_unified_identity_decoder *identity = &decoder->identity;
  if (!field_absent(reader, "opacity", &opacity)) {
    entity.has_opacity = true;
    entity.opacity = qa_save_reader_finite(&opacity);
    qa_save_reader_destroy(&opacity);
  }
  qa_save_reader key_reader = field_of(reader, "resource");
  qa_app_unified_resource_key key = qa_app_unified_read_resource_key(&key_reader);
  qa_save_reader_destroy(&key_reader);
  entity.resource = decoder->resource(decoder->ctx, &key);
  entity.model = decoder->model(decoder->ctx, &entity.resource);
  if (qa_app_unified_resource_key_from_reference(&entity.resource).content != key.content
      && strcmp(qa_app_unified_resource_key_from_reference(&entity.resource).content, key.content) != 0)
    qa_save_reader_fail(reader, "scene resource differs from its unified key");
  else {
    qa_app_unified_resource_key actual = qa_app_unified_resource_key_from_reference(&entity.resource);
    if (strcmp(actual.path, key.path) != 0 || strcmp(actual.digest, key.digest) != 0
        || actual.byte_length != key.byte_length)
      qa_save_reader_fail(reader, "scene resource differs from its unified key");
  }
  qa_save_reader actor_reader = field_of(reader, "actor");
  if (!qa_save_reader_is_null(&actor_reader)) {
    qa_actor_id *owned = xmalloc(sizeof *owned);
    *owned = qa_app_unified_read_actor(&actor_reader, identity);
    entity.actor = owned;
  }
  qa_save_reader_destroy(&actor_reader);
  qa_save_reader transform = field_of(reader, "transform");
  entity.transform.origin = vec_field(&transform, "origin");
  qa_save_reader axis_reader = field_of(&transform, "axis");
  read_axis_into(&axis_reader, entity.transform.axis.vectors);
  qa_save_reader_destroy(&axis_reader);
  entity.transform.scale = vec_field(&transform, "scale");
  qa_save_reader_destroy(&transform);
  entity.previous_origin = vec_field(reader, "previousOrigin");
  qa_save_reader pose = field_of(reader, "pose");
  static const char *pose_kinds[] = {"frame", "skeleton"};
  qa_save_reader pose_kind = field_of(&pose, "kind");
  const char *pose_name = qa_save_reader_choice_string(&pose_kind, pose_kinds, 2);
  qa_save_reader_destroy(&pose_kind);
  if (strcmp(pose_name, "frame") == 0) {
    entity.pose.is_frame = true;
    entity.pose.frame = finite_field(&pose, "frame");
    entity.pose.previous_frame = finite_field(&pose, "previousFrame");
    entity.pose.back_lerp = finite_field(&pose, "backLerp");
  } else {
    qa_save_reader joints_reader = field_of(&pose, "joints");
    bounded_list(&joints_reader, 1024);
    size_t joint_count = qa_save_reader_list_count(&joints_reader);
    qa_skeleton_joint_pose *joints = xmalloc(joint_count * sizeof *joints);
    for (size_t i = 0; i < joint_count; i++) {
      qa_save_reader joint;
      if (!qa_save_reader_item(&joints_reader, i, &joint)) qa_range_error("out of memory");
      joints[i].position = vec_field(&joint, "position");
      qa_save_reader orientation = field_of(&joint, "orientation");
      joints[i].orientation = qa_app_unified_read_color(&orientation);
      qa_save_reader_destroy(&orientation);
      joints[i].scale = finite_field(&joint, "scale");
      qa_save_reader_destroy(&joint);
    }
    entity.pose.joints = joints;
    entity.pose.joint_count = joint_count;
    qa_save_reader_destroy(&joints_reader);
  }
  qa_save_reader_destroy(&pose);
  entity.skin = finite_field(reader, "skin");
  qa_save_reader color = field_of(reader, "color");
  entity.color = qa_app_unified_read_color(&color);
  qa_save_reader_destroy(&color);
  entity.shader_time = read_time_field(reader, "shaderTime");
  qa_save_reader flags = field_of(reader, "flags");
  static const char *flag_kinds[] = {"q1", "q2", "q3"};
  qa_save_reader flag_kind = field_of(&flags, "kind");
  entity.flags_kind = qa_save_reader_choice_string(&flag_kind, flag_kinds, 3);
  qa_save_reader_destroy(&flag_kind);
  qa_save_reader bits = field_of(&flags, "bits");
  entity.flags_bits = (double)qa_save_reader_integer(&bits);
  qa_save_reader_destroy(&bits);
  qa_save_reader_destroy(&flags);
  entity.lighting_origin = vec_field(reader, "lightingOrigin");
  entity.shadow_plane = finite_field(reader, "shadowPlane");
  attachments = field_of(reader, "attachments");
  bounded_list(&attachments, 64);
  size_t attachment_count = qa_save_reader_list_count(&attachments);
  qa_scene_entity_attachment *items = xmalloc(attachment_count * sizeof *items);
  for (size_t i = 0; i < attachment_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&attachments, i, &row)) qa_range_error("out of memory");
    qa_save_reader tag = field_of(&row, "tag");
    items[i].tag = qa_save_reader_string(&tag);
    qa_save_reader_destroy(&tag);
    qa_save_reader child = field_of(&row, "entity");
    qa_scene_entity *owned = xmalloc(sizeof *owned);
    *owned = read_entity(&child, decoder, depth + 1);
    items[i].entity = owned;
    qa_save_reader_destroy(&child);
    qa_save_reader_destroy(&row);
  }
  entity.attachments = items;
  entity.attachment_count = attachment_count;
  qa_save_reader_destroy(&attachments);
  return entity;
}
static qa_scene_light read_light(const qa_save_reader *reader) {
  qa_scene_light light;
  memset(&light, 0, sizeof light);
  qa_save_reader profile_reader = field_of(reader, "profile");
  static const char *kinds[] = {"q1", "q2", "q3"};
  qa_save_reader kind_reader = field_of(&profile_reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 3);
  qa_save_reader_destroy(&kind_reader);
  light.profile.kind = kind;
  if (strcmp(kind, "q2") == 0) {
    light.profile.scale = finite_field(&profile_reader, "scale");
    qa_save_reader cone = field_of(&profile_reader, "cone");
    if (!qa_save_reader_is_null(&cone)) {
      light.profile.has_cone = true;
      light.profile.cone_direction = vec_field(&cone, "direction");
      light.profile.cone_cos_half_angle = finite_field(&cone, "cosHalfAngle");
    }
    qa_save_reader_destroy(&cone);
    qa_save_reader shadow = field_of(&profile_reader, "shadow");
    static const char *shadow_kinds[] = {"none", "cast"};
    qa_save_reader shadow_kind = field_of(&shadow, "kind");
    const char *shadow_name = qa_save_reader_choice_string(&shadow_kind, shadow_kinds, 2);
    qa_save_reader_destroy(&shadow_kind);
    if (strcmp(shadow_name, "cast") == 0) {
      light.profile.shadow_cast = true;
      light.profile.shadow_resolution = finite_field(&shadow, "resolution");
    }
    qa_save_reader_destroy(&shadow);
  }
  qa_save_reader_destroy(&profile_reader);
  light.origin = vec_field(reader, "origin");
  light.color = vec_field(reader, "color");
  light.radius = finite_field(reader, "radius");
  qa_save_reader additive = field_of(reader, "additive");
  light.additive = qa_save_reader_boolean(&additive);
  qa_save_reader_destroy(&additive);
  return light;
}
static void entity_free(qa_scene_entity *entity) {
  free((void *)entity->actor);
  free((void *)entity->pose.joints);
  for (size_t i = 0; i < entity->attachment_count; i++) {
    entity_free((qa_scene_entity *)entity->attachments[i].entity);
    free((void *)entity->attachments[i].entity);
  }
  free((void *)entity->attachments);
}
void qa_app_unified_presentation_frame_free(qa_app_unified_presentation_frame_t *frame) {
  if (frame->has_components) qa_app_unified_component_frames_free(&frame->components);
  qa_app_unified_prediction_projection_free(&frame->prediction);
  free((void *)frame->output.snapshot.actors);
  for (size_t i = 0; i < frame->output.snapshot.body_count; i++)
    free((void *)frame->output.snapshot.bodies[i].body.ground);
  free((void *)frame->output.snapshot.bodies);
  for (size_t i = 0; i < frame->output.snapshot.inventory_count; i++)
    free((void *)frame->output.snapshot.inventories[i].entries);
  free((void *)frame->output.snapshot.inventories);
  for (size_t i = 0; i < frame->output.snapshot.configuration_count; i++)
    free((void *)frame->output.snapshot.configurations[i].weapons.values);
  free((void *)frame->output.snapshot.configurations);
  for (size_t i = 0; i < frame->output.snapshot.scene.entity_count; i++)
    entity_free((qa_scene_entity *)&frame->output.snapshot.scene.entities[i]);
  free((void *)frame->output.snapshot.scene.entities);
  free((void *)frame->output.snapshot.scene.lights);
  free((void *)frame->output.snapshot.scene.particles);
  free((void *)frame->output.snapshot.scene.light_styles);
  free((void *)frame->output.snapshot.scene.area_bits);
  for (size_t i = 0; i < frame->event_count; i++) qa_save_value_free(frame->events[i]);
  free(frame->events);
  for (size_t i = 0; i < frame->model_count; i++) qa_app_unified_model_free(&frame->models[i]);
  free(frame->models);
  free(frame->characters);
  free(frame->world_text);
  qa_app_unified_player_ui_free(&frame->player_ui);
  memset(frame, 0, sizeof *frame);
}
qa_app_unified_presentation_frame_t qa_app_unified_decode_frame(
    const uint8_t *bytes, size_t length, const qa_app_unified_frame_decoder *decoder) {
  if (length > 32 * 1024 * 1024) qa_range_error("Unified frame exceeds byte limit");
  qa_save_value *tree = qa_decode_checkpoint_value(bytes, length);
  if (!tree) qa_range_error("out of memory");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, tree, "qts-frame")) qa_range_error("out of memory");
  const qa_app_unified_identity_decoder *identity = &decoder->identity;
  qa_save_reader f = field_of(&reader, "schema");
  qa_save_reader_literal_string(&f, "qts-frame");
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "version");
  qa_save_reader_literal_number(&f, 9);
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "epoch");
  double epoch = (double)qa_save_reader_integer_min(&f, 1);
  qa_save_reader_destroy(&f);
  qa_app_unified_presentation_frame_t out;
  memset(&out, 0, sizeof out);
  out.epoch = epoch;
  if (!field_absent(&reader, "components", &f)) {
    out.has_components = true;
    out.components = qa_app_unified_read_component_frames(&f, identity);
    qa_save_reader_destroy(&f);
  }
  if (!field_absent(&reader, "nativeCamera", &f)) {
    out.has_native_camera = true;
    qa_save_reader view_reader = field_of(&f, "view");
    out.native_camera.view = qa_app_unified_read_native_camera_view(&view_reader);
    qa_save_reader_destroy(&view_reader);
    qa_save_reader owner_reader = field_of(&f, "owner");
    out.native_camera.owner = qa_app_unified_read_component_owner(&owner_reader);
    qa_save_reader_destroy(&owner_reader);
    qa_save_reader identity_reader = field_of(&f, "identity");
    out.native_camera.identity = qa_read_mod_identity(&identity_reader);
    out.native_camera.identity_wire = clone_value(identity_reader.value);
    qa_save_reader_destroy(&identity_reader);
    out.native_camera.generation = finite_field(&f, "generation");
    qa_save_reader_destroy(&f);
  }
  f = field_of(&reader, "prediction");
  size_t prediction_length = 0;
  const uint8_t *prediction_bytes = qa_save_reader_bytes(&f, &prediction_length);
  out.prediction = qa_app_unified_decode_prediction(prediction_bytes, prediction_length, identity);
  qa_save_reader_destroy(&f);
  qa_world_snapshot *snapshot = &out.output.snapshot;
  /* Actors are resolved before every actor-bearing section. */
  f = field_of(&reader, "actors");
  bounded_list(&f, 65536);
  size_t actor_count = qa_save_reader_list_count(&f);
  qa_actor_observation *actors = xmalloc(actor_count * sizeof *actors);
  for (size_t i = 0; i < actor_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&f, i, &row)) qa_range_error("out of memory");
    qa_save_reader id_reader = field_of(&row, "id");
    actors[i].id = qa_app_unified_read_actor(&id_reader, identity);
    qa_save_reader_destroy(&id_reader);
    qa_save_reader owner_reader = field_of(&row, "owner");
    actors[i].owner = qa_namespaced(&owner_reader);
    qa_save_reader_destroy(&owner_reader);
    qa_save_reader definition = field_of(&row, "definition");
    actors[i].definition = qa_namespaced(&definition);
    qa_save_reader_destroy(&definition);
    qa_save_reader_destroy(&row);
  }
  snapshot->actors = actors;
  snapshot->actor_count = actor_count;
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "bodies");
  bounded_list(&f, 65536);
  size_t body_count = qa_save_reader_list_count(&f);
  qa_body_snapshot *bodies = xmalloc(body_count * sizeof *bodies);
  for (size_t i = 0; i < body_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&f, i, &row)) qa_range_error("out of memory");
    qa_save_reader actor_reader = field_of(&row, "actor");
    bodies[i].actor = qa_app_unified_read_actor(&actor_reader, identity);
    qa_save_reader_destroy(&actor_reader);
    qa_save_reader body_reader = field_of(&row, "body");
    bodies[i].body = read_body(&body_reader, identity);
    qa_save_reader_destroy(&body_reader);
    qa_save_reader_destroy(&row);
  }
  snapshot->bodies = bodies;
  snapshot->body_count = body_count;
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "inventories");
  bounded_list(&f, 65536);
  size_t inventory_count = qa_save_reader_list_count(&f);
  qa_inventory_snapshot *inventories = xmalloc(inventory_count * sizeof *inventories);
  for (size_t i = 0; i < inventory_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&f, i, &row)) qa_range_error("out of memory");
    qa_save_reader actor_reader = field_of(&row, "actor");
    inventories[i].actor = qa_app_unified_read_actor(&actor_reader, identity);
    qa_save_reader_destroy(&actor_reader);
    qa_save_reader entries = field_of(&row, "entries");
    qa_inventory_entry *owned = NULL;
    inventories[i].entry_count = read_inventory_entries(&entries, &owned);
    inventories[i].entries = owned;
    qa_save_reader_destroy(&entries);
    qa_save_reader_destroy(&row);
  }
  snapshot->inventories = inventories;
  snapshot->inventory_count = inventory_count;
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "configurations");
  bounded_list(&f, 65536);
  size_t configuration_count = qa_save_reader_list_count(&f);
  qa_actor_configuration *configurations = xmalloc(configuration_count * sizeof *configurations);
  for (size_t i = 0; i < configuration_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&f, i, &row)) qa_range_error("out of memory");
    configurations[i] = read_configuration(&row, identity);
    qa_save_reader_destroy(&row);
  }
  snapshot->configurations = configurations;
  snapshot->configuration_count = configuration_count;
  qa_save_reader_destroy(&f);
  /* Scene. */
  f = field_of(&reader, "scene");
  qa_scene_snapshot *scene = &snapshot->scene;
  scene->session = identity->session;
  scene->time = read_time_field(&f, "time");
  scene->world = decoder->world(decoder->ctx, NULL);
  qa_save_reader entities_reader = field_of(&f, "entities");
  bounded_list(&entities_reader, 65536);
  size_t entity_count = qa_save_reader_list_count(&entities_reader);
  qa_scene_entity *entities = xmalloc(entity_count * sizeof *entities);
  for (size_t i = 0; i < entity_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&entities_reader, i, &row)) qa_range_error("out of memory");
    entities[i] = read_entity(&row, decoder, 0);
    qa_save_reader_destroy(&row);
  }
  scene->entities = entities;
  scene->entity_count = entity_count;
  qa_save_reader_destroy(&entities_reader);
  qa_save_reader lights_reader = field_of(&f, "lights");
  bounded_list(&lights_reader, 65536);
  size_t light_count = qa_save_reader_list_count(&lights_reader);
  qa_scene_light *lights = xmalloc(light_count * sizeof *lights);
  for (size_t i = 0; i < light_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&lights_reader, i, &row)) qa_range_error("out of memory");
    lights[i] = read_light(&row);
    qa_save_reader_destroy(&row);
  }
  scene->lights = lights;
  scene->light_count = light_count;
  qa_save_reader_destroy(&lights_reader);
  qa_save_reader particles_reader = field_of(&f, "particles");
  bounded_list(&particles_reader, 1048576);
  size_t particle_count = qa_save_reader_list_count(&particles_reader);
  qa_scene_particle *particles = xmalloc(particle_count * sizeof *particles);
  for (size_t i = 0; i < particle_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&particles_reader, i, &row)) qa_range_error("out of memory");
    memset(&particles[i], 0, sizeof particles[i]);
    static const char *particle_kinds[] = {"indexed", "rgba"};
    qa_save_reader kind_reader = field_of(&row, "kind");
    const char *particle_kind = qa_save_reader_choice_string(&kind_reader, particle_kinds, 2);
    qa_save_reader_destroy(&kind_reader);
    particles[i].origin = vec_field(&row, "origin");
    particles[i].size = finite_field(&row, "size");
    if (strcmp(particle_kind, "indexed") == 0) {
      particles[i].palette_index = finite_field(&row, "paletteIndex");
      particles[i].alpha = finite_field(&row, "alpha");
    } else {
      particles[i].is_rgba = true;
      qa_save_reader color = field_of(&row, "color");
      particles[i].color = qa_app_unified_read_color(&color);
      qa_save_reader_destroy(&color);
      particles[i].rotation = finite_field(&row, "rotation");
    }
    qa_save_reader_destroy(&row);
  }
  scene->particles = particles;
  scene->particle_count = particle_count;
  qa_save_reader_destroy(&particles_reader);
  qa_save_reader styles_reader = field_of(&f, "lightStyles");
  bounded_list(&styles_reader, 1024);
  size_t style_count = qa_save_reader_list_count(&styles_reader);
  qa_scene_light_style *styles = xmalloc(style_count * sizeof *styles);
  for (size_t i = 0; i < style_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&styles_reader, i, &row)) qa_range_error("out of memory");
    memset(&styles[i], 0, sizeof styles[i]);
    static const char *style_kinds[] = {"q1", "q2"};
    qa_save_reader kind_reader = field_of(&row, "kind");
    const char *style_kind = qa_save_reader_choice_string(&kind_reader, style_kinds, 2);
    qa_save_reader_destroy(&kind_reader);
    styles[i].style = finite_field(&row, "style");
    if (strcmp(style_kind, "q1") == 0) {
      styles[i].value = finite_field(&row, "value");
    } else {
      styles[i].is_q2 = true;
      styles[i].rgb = vec_field(&row, "rgb");
      styles[i].white = finite_field(&row, "white");
    }
    qa_save_reader_destroy(&row);
  }
  scene->light_styles = styles;
  scene->light_style_count = style_count;
  qa_save_reader_destroy(&styles_reader);
  qa_save_reader area = field_of(&f, "areaBits");
  if (!qa_save_reader_is_null(&area)) {
    size_t area_length = 0;
    const uint8_t *area_bytes = qa_save_reader_bytes(&area, &area_length);
    uint8_t *owned = xmalloc(area_length);
    memcpy(owned, area_bytes, area_length);
    qa_byte_view *view = xmalloc(sizeof *view);
    view->bytes = owned;
    view->length = area_length;
    scene->area_bits = view;
  }
  qa_save_reader_destroy(&area);
  qa_save_reader_destroy(&f);
  /* Events (bounded, decoded through the event codec). */
  f = field_of(&reader, "events");
  bounded_list(&f, 65536);
  size_t event_count = qa_save_reader_list_count(&f);
  out.events = xmalloc(event_count * sizeof *out.events);
  for (size_t i = 0; i < event_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&f, i, &row)) qa_range_error("out of memory");
    out.events[i] = qa_app_unified_read_simulation_event(row.value, identity);
    qa_save_reader_destroy(&row);
  }
  out.event_count = event_count;
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "acknowledgedInput");
  out.acknowledged_input = (double)qa_save_reader_integer_min(&f, -1);
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "frame");
  snapshot->session = identity->session;
  snapshot->frame.frame = (double)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "models");
  bounded_list(&f, 65536);
  size_t model_count = qa_save_reader_list_count(&f);
  out.models = xmalloc(model_count * sizeof *out.models);
  for (size_t i = 0; i < model_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&f, i, &row)) qa_range_error("out of memory");
    out.models[i] = qa_app_unified_read_model(&row, identity);
    qa_save_reader_destroy(&row);
  }
  out.model_count = model_count;
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "characters");
  bounded_list(&f, 65536);
  size_t character_count = qa_save_reader_list_count(&f);
  out.characters = xmalloc(character_count * sizeof *out.characters);
  for (size_t i = 0; i < character_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&f, i, &row)) qa_range_error("out of memory");
    out.characters[i] = qa_app_unified_read_character_view(&row, identity);
    qa_save_reader_destroy(&row);
  }
  out.character_count = character_count;
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "worldText");
  bounded_list(&f, 65536);
  size_t text_count = qa_save_reader_list_count(&f);
  out.world_text = xmalloc(text_count * sizeof *out.world_text);
  for (size_t i = 0; i < text_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&f, i, &row)) qa_range_error("out of memory");
    out.world_text[i] = qa_app_unified_read_world_text(&row);
    qa_save_reader_destroy(&row);
  }
  out.world_text_count = text_count;
  qa_save_reader_destroy(&f);
  f = field_of(&reader, "player");
  qa_save_reader player_actor = field_of(&f, "actor");
  out.player_actor = qa_app_unified_read_actor(&player_actor, identity);
  qa_save_reader_destroy(&player_actor);
  qa_save_reader view_reader = field_of(&f, "view");
  out.player_view = qa_app_unified_read_player_view(&view_reader);
  qa_save_reader_destroy(&view_reader);
  qa_save_reader ui_reader = field_of(&f, "ui");
  out.player_ui = qa_app_unified_read_player_ui(&ui_reader);
  qa_save_reader_destroy(&ui_reader);
  qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&reader);
  (void)tree;
  return out;
}
