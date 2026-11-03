/* Port of src/compat/qvm/grapple-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/grapple_profile.h"

#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/player_record.h"
#include "qa/compat/qvm/shared_entity_record.h"
#include "qa/foundation/support.h"
#include "qa/persistence/shared.h"

typedef struct profile_ctx {
  qa_save_reader reader;
  const qa_resolved_qvm_artifact *artifact;
  qa_qvm_abi_profile abi;
  double entity_stride, client_stride;
} profile_ctx;

static void reader_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}

static double field_offset(profile_ctx *ctx, const char *name, double minimum, double maximum) {
  qa_save_reader fields, field;
  reader_field(&ctx->reader, "fields", &fields);
  reader_field(&fields, name, &field);
  double result = qa_save_reader_integer_min(&field, minimum);
  if ((int64_t)result % 4 != 0 || result > maximum - 4)
    qa_save_reader_fail(&field, "field is outside its source record");
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&fields);
  return result;
}

static double entity_field(profile_ctx *ctx, const char *name) {
  return field_offset(ctx, name, qa_qvm_shared_entity_bytes(ctx->abi), ctx->entity_stride);
}

static double global_at(profile_ctx *ctx, const char *name, double bytes) {
  qa_save_reader globals, field;
  reader_field(&ctx->reader, "globals", &globals);
  reader_field(&globals, name, &field);
  double result = qa_save_reader_integer_min(&field, 4);
  const qa_qvm_image *image = &ctx->artifact->image;
  double total = (double)(image->data_length + image->literal_length + image->bss_length);
  if ((int64_t)result % 4 != 0 || result + bytes > total)
    qa_save_reader_fail(&field, "global is outside declared source memory");
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&globals);
  return result;
}

static double callback_at(profile_ctx *ctx, const char *name) {
  qa_save_reader callbacks, field;
  reader_field(&ctx->reader, "callbacks", &callbacks);
  reader_field(&callbacks, name, &field);
  double entry = qa_save_reader_integer_min(&field, 1);
  const qa_qvm_image *image = &ctx->artifact->image;
  bool is_entry = entry >= 0 && entry < (double)image->instruction_count &&
                  image->instructions[(size_t)entry].opcode == QA_QVM_OP_ENTER;
  if (!is_entry) qa_save_reader_fail(&field, "callback is not a source function entry");
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&callbacks);
  return entry;
}

static bool field_is_null(profile_ctx *ctx, const char *group, const char *name) {
  qa_save_reader group_reader, field;
  reader_field(&ctx->reader, group, &group_reader);
  reader_field(&group_reader, name, &field);
  bool result = qa_save_reader_is_null(&field);
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&group_reader);
  return result;
}

static bool nullable_callback(profile_ctx *ctx, const char *name, double *out) {
  if (field_is_null(ctx, "callbacks", name)) return false;
  *out = callback_at(ctx, name);
  return true;
}

static const char *top_string(profile_ctx *ctx, const char *name) {
  qa_save_reader field;
  reader_field(&ctx->reader, name, &field);
  const char *value = qa_save_reader_string(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static double top_integer(profile_ctx *ctx, const char *name, double minimum) {
  qa_save_reader field;
  reader_field(&ctx->reader, name, &field);
  double value = qa_save_reader_integer_min(&field, minimum);
  qa_save_reader_destroy(&field);
  return value;
}

static const char *nested_string(const qa_save_reader *parent, const char *name) {
  qa_save_reader field;
  reader_field(parent, name, &field);
  const char *value = qa_save_reader_string(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static double nested_integer(const qa_save_reader *parent, const char *name, double minimum) {
  qa_save_reader field;
  reader_field(parent, name, &field);
  double value = qa_save_reader_integer_min(&field, minimum);
  qa_save_reader_destroy(&field);
  return value;
}
static const char *nested_nullable_string(const qa_save_reader *parent, const char *name) {
  qa_save_reader field;
  reader_field(parent, name, &field);
  const char *value = qa_save_reader_is_null(&field) ? NULL : qa_save_reader_string(&field);
  qa_save_reader_destroy(&field);
  return value;
}

qa_qvm_grapple_profile *qa_qvm_read_grapple_profile(const qa_save_value *value,
                                                    const qa_resolved_qvm_artifact *artifact) {
  profile_ctx ctx;
  ctx.artifact = artifact;
  if (!qa_save_reader_init(&ctx.reader, value, "qvm-grapple-profile")) return NULL;
  qa_save_reader version, digest, path, abi;
  reader_field(&ctx.reader, "version", &version);
  (void)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  reader_field(&ctx.reader, "artifactDigest", &digest);
  (void)qa_save_reader_literal_string(&digest, artifact->module.digest);
  qa_save_reader_destroy(&digest);
  reader_field(&ctx.reader, "artifactPath", &path);
  (void)qa_save_reader_literal_string(&path, artifact->module.artifact_path);
  qa_save_reader_destroy(&path);
  reader_field(&ctx.reader, "abiProfile", &abi);
  qa_qvm_abi_profile expected = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
  ctx.abi = qa_save_reader_literal_string(&abi, expected);
  qa_save_reader_destroy(&abi);
  if (strcmp(artifact->role, "qagame") != 0)
    qa_save_reader_fail(&ctx.reader, "grapple callbacks require a qagame module");
  ctx.entity_stride = top_integer(&ctx, "entityStride", qa_qvm_shared_entity_bytes(ctx.abi));
  ctx.client_stride = top_integer(&ctx, "clientStride", qa_qvm_player_state_bytes(ctx.abi));
  double max_stride = ctx.entity_stride > ctx.client_stride ? ctx.entity_stride : ctx.client_stride;
  if ((int64_t)ctx.entity_stride % 4 != 0 || (int64_t)ctx.client_stride % 4 != 0 ||
      max_stride > (double)artifact->image.allocated_data_length)
    qa_save_reader_fail(&ctx.reader, "unaligned or oversized source records");
  qa_qvm_grapple_profile *profile = calloc(1, sizeof *profile);
  if (profile == NULL) {
    qa_save_reader_destroy(&ctx.reader);
    return NULL;
  }
  profile->module = artifact->module;
  profile->abi_profile = ctx.abi;
  profile->entity_stride = ctx.entity_stride;
  profile->client_stride = ctx.client_stride;
  profile->id = top_string(&ctx, "id");
  profile->title = top_string(&ctx, "title");
  profile->fields.inuse = entity_field(&ctx, "inuse");
  profile->fields.client = entity_field(&ctx, "client");
  profile->fields.parent = entity_field(&ctx, "parent");
  profile->fields.target = entity_field(&ctx, "target");
  profile->fields.has_mover = !field_is_null(&ctx, "fields", "mover");
  if (profile->fields.has_mover) profile->fields.mover = entity_field(&ctx, "mover");
  profile->fields.health = entity_field(&ctx, "health");
  profile->fields.takedamage = entity_field(&ctx, "takedamage");
  profile->fields.event_time = entity_field(&ctx, "eventTime");
  profile->fields.free_after_event = entity_field(&ctx, "freeAfterEvent");
  profile->fields.hook = field_offset(&ctx, "hook", qa_qvm_player_state_bytes(ctx.abi),
                                       ctx.client_stride);
  profile->globals.time = global_at(&ctx, "time", 4);
  profile->globals.frame = global_at(&ctx, "frame", 4);
  profile->globals.movement = global_at(&ctx, "movement", 4);
  profile->globals.forward = global_at(&ctx, "forward", 12);
  profile->globals.ground_plane = global_at(&ctx, "groundPlane", 4);
  profile->callbacks.allocate = callback_at(&ctx, "allocate");
  profile->callbacks.free = callback_at(&ctx, "free");
  profile->callbacks.fire = callback_at(&ctx, "fire");
  profile->callbacks.release = callback_at(&ctx, "release");
  profile->callbacks.force_release = callback_at(&ctx, "forceRelease");
  profile->callbacks.missile = callback_at(&ctx, "missile");
  profile->callbacks.has_follow = nullable_callback(&ctx, "follow", &profile->callbacks.follow);
  profile->callbacks.think = callback_at(&ctx, "think");
  profile->callbacks.pull = callback_at(&ctx, "pull");
  profile->callbacks.has_move_mover_hooks =
      nullable_callback(&ctx, "moveMoverHooks", &profile->callbacks.move_mover_hooks);
  profile->callbacks.damage = callback_at(&ctx, "damage");
  profile->callbacks.same_team = callback_at(&ctx, "sameTeam");
  profile->callbacks.player_move = callback_at(&ctx, "playerMove");
  double pulling = top_integer(&ctx, "pullingFlag", 1);
  if (pulling > 1073741824.0 || ((int64_t)pulling & ((int64_t)pulling - 1)) != 0) {
    qa_save_reader field;
    reader_field(&ctx.reader, "pullingFlag", &field);
    qa_save_reader_fail(&field, "expected one player movement flag");
  }
  profile->pulling_flag = pulling;
  /* fire arguments */
  qa_save_reader fire;
  reader_field(&ctx.reader, "fireArguments", &fire);
  size_t fire_count = qa_save_reader_list_count(&fire);
  double *fire_values = calloc(fire_count == 0 ? 1 : fire_count, sizeof(double));
  if (fire_values == NULL) goto oom;
  for (size_t i = 0; i < fire_count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&fire, i, &item)) goto oom;
    fire_values[i] = qa_save_reader_integer(&item);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&fire);
  profile->fire_arguments.values = fire_values;
  profile->fire_arguments.count = fire_count;
  /* movement */
  {
    qa_save_reader movement, words;
    reader_field(&ctx.reader, "movement", &movement);
    profile->movement.byte_length = nested_integer(&movement, "byteLength", 4);
    reader_field(&movement, "words", &words);
    size_t word_count = qa_save_reader_list_count(&words);
    qa_qvm_grapple_movement_word *movement_words =
        calloc(word_count == 0 ? 1 : word_count, sizeof *movement_words);
    if (movement_words == NULL) goto oom;
    for (size_t i = 0; i < word_count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&words, i, &item)) goto oom;
      movement_words[i].offset = nested_integer(&item, "offset", 4);
      {
        qa_save_reader value_reader;
        reader_field(&item, "value", &value_reader);
        movement_words[i].value = qa_save_reader_integer(&value_reader);
        qa_save_reader_destroy(&value_reader);
      }
      double byte_length = nested_integer(&movement, "byteLength", 4);
      if ((int64_t)movement_words[i].offset % 4 != 0 || movement_words[i].offset + 4 > byte_length)
        qa_save_reader_fail(&item, "Movement field is outside the declared source record");
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&words);
    qa_save_reader_destroy(&movement);
    profile->movement.words = movement_words;
    profile->movement.word_count = word_count;
  }
  /* initial cvars object */
  {
    qa_save_reader cvars;
    reader_field(&ctx.reader, "initialCvars", &cvars);
    if (cvars.value == NULL || cvars.value->kind != QA_SAVE_OBJECT)
      qa_save_reader_fail(&cvars, "Expected initial source settings");
    size_t count = cvars.value->entry_count;
    qa_qvm_grapple_cvar *list = calloc(count == 0 ? 1 : count, sizeof *list);
    if (list == NULL) goto oom;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry;
      reader_field(&cvars, cvars.value->entries[i].key, &entry);
      list[i].name = cvars.value->entries[i].key;
      list[i].value = qa_save_reader_string(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&cvars);
    profile->initial_cvars = list;
    profile->initial_cvar_count = count;
  }
  profile->event_lifetime_milliseconds = top_integer(&ctx, "eventLifetimeMilliseconds", 1);
  profile->grapple_damage_method = top_integer(&ctx, "grappleDamageMethod", 0);
  /* presentation */
  {
    qa_save_reader presentation, cable, anchor, attachments;
    reader_field(&ctx.reader, "presentation", &presentation);
    reader_field(&presentation, "cable", &cable);
    qa_save_reader kind_reader;
    reader_field(&cable, "kind", &kind_reader);
    const char *kinds[] = {"shader", "model"};
    const char *cable_kind = qa_save_reader_choice_string(&kind_reader, kinds, 2);
    qa_save_reader_destroy(&kind_reader);
    reader_field(&presentation, "viewAnchor", &anchor);
    profile->presentation.projectile_model = nested_string(&presentation, "projectileModel");
    profile->presentation.view_model = nested_string(&presentation, "viewModel");
    profile->presentation.weapon_index = nested_integer(&presentation, "weaponIndex", 1);
    profile->presentation.view_anchor.path = nested_string(&anchor, "path");
    profile->presentation.view_anchor.tag = nested_string(&anchor, "tag");
    qa_save_reader offset_reader, fov;
    reader_field(&anchor, "offset", &offset_reader);
    profile->presentation.view_anchor.offset = qa_read_vector(&offset_reader);
    qa_save_reader_destroy(&offset_reader);
    reader_field(&anchor, "fovOffset", &fov);
    profile->presentation.view_anchor.fov_offset_above = nested_integer(&fov, "above", 1);
    qa_save_reader scale;
    reader_field(&fov, "scale", &scale);
    profile->presentation.view_anchor.fov_offset_scale = qa_save_reader_finite(&scale);
    qa_save_reader_destroy(&scale);
    qa_save_reader_destroy(&fov);
    qa_save_reader_destroy(&anchor);
    reader_field(&presentation, "viewAttachments", &attachments);
    size_t attachment_count = qa_save_reader_list_count(&attachments);
    qa_qvm_grapple_view_attachment *list =
        calloc(attachment_count == 0 ? 1 : attachment_count, sizeof *list);
    if (list == NULL) goto oom;
    for (size_t i = 0; i < attachment_count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&attachments, i, &item)) goto oom;
      list[i].path = nested_string(&item, "path");
      list[i].tag = nested_string(&item, "tag");
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&attachments);
    profile->presentation.view_attachments = list;
    profile->presentation.view_attachment_count = attachment_count;
    profile->presentation.cable.is_shader = strcmp(cable_kind, "shader") == 0;
    if (profile->presentation.cable.is_shader) {
      profile->presentation.cable.path = nested_string(&cable, "path");
      profile->presentation.cable.width = nested_integer(&cable, "width", 1);
    } else {
      profile->presentation.cable.flight = nested_string(&cable, "flight");
      profile->presentation.cable.pull = nested_string(&cable, "pull");
      profile->presentation.cable.hold = nested_string(&cable, "hold");
      profile->presentation.cable.segment_length = nested_integer(&cable, "segmentLength", 1);
    }
    qa_save_reader_destroy(&cable);
    profile->presentation.fire_sound = nested_nullable_string(&presentation, "fireSound");
    profile->presentation.attach_sound = nested_nullable_string(&presentation, "attachSound");
    profile->presentation.release_sound = nested_nullable_string(&presentation, "releaseSound");
    profile->presentation.pull_sound = nested_nullable_string(&presentation, "pullSound");
    profile->presentation.hang_sound = nested_nullable_string(&presentation, "hangSound");
    qa_save_reader_destroy(&presentation);
  }
  qa_save_reader_destroy(&ctx.reader);
  return profile;
oom:
  qa_qvm_grapple_profile_free(profile);
  qa_save_reader_destroy(&ctx.reader);
  return NULL;
}

void qa_qvm_grapple_profile_free(qa_qvm_grapple_profile *profile) {
  if (profile == NULL) return;
  free((void *)profile->fire_arguments.values);
  free((void *)profile->movement.words);
  free((void *)profile->initial_cvars);
  free((void *)profile->presentation.view_attachments);
  free(profile);
}

/* Declaration builder: a failed allocation latches and yields NULL at the end. */
typedef struct declaration_builder { bool failed; } declaration_builder;

static void put(declaration_builder *b, qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || object == NULL) {
    b->failed = true;
    qa_save_value_free(child);
    return;
  }
  if (!qa_save_value_set(object, key, child)) {
    b->failed = true;
    qa_save_value_free(child);
  }
}
static void put_number(declaration_builder *b, qa_save_value *object, const char *key, double value) {
  put(b, object, key, qa_save_value_number(value));
}
static void put_string(declaration_builder *b, qa_save_value *object, const char *key, const char *value) {
  put(b, object, key, value == NULL ? qa_save_value_null() : qa_save_value_string(value));
}
static void put_nullable_number(declaration_builder *b, qa_save_value *object, const char *key, bool has,
                                double value) {
  put(b, object, key, has ? qa_save_value_number(value) : qa_save_value_null());
}
static void push(declaration_builder *b, qa_save_value *array, qa_save_value *child) {
  if (child == NULL || array == NULL) {
    b->failed = true;
    qa_save_value_free(child);
    return;
  }
  if (!qa_save_value_push(array, child)) {
    b->failed = true;
    qa_save_value_free(child);
  }
}

/* TS { ...profile, version: 1, artifactDigest, artifactPath }, in that key order. */
qa_save_value *qa_qvm_grapple_profile_declaration(const qa_qvm_grapple_profile *profile) {
  declaration_builder b = {false};
  qa_save_value *object = qa_save_value_object();
  put_string(&b, object, "id", profile->id);
  put_string(&b, object, "title", profile->title);
  qa_save_value *module = qa_save_value_object();
  put_string(&b, module, "id", profile->module.id);
  put_string(&b, module, "artifactPath", profile->module.artifact_path);
  put_string(&b, module, "digest", profile->module.digest);
  put_string(&b, module, "revision", profile->module.revision);
  put(&b, object, "module", module);
  put_string(&b, object, "abiProfile", profile->abi_profile);
  put_number(&b, object, "entityStride", profile->entity_stride);
  put_number(&b, object, "clientStride", profile->client_stride);
  qa_save_value *fields = qa_save_value_object();
  put_number(&b, fields, "inuse", profile->fields.inuse);
  put_number(&b, fields, "client", profile->fields.client);
  put_number(&b, fields, "parent", profile->fields.parent);
  put_number(&b, fields, "target", profile->fields.target);
  put_nullable_number(&b, fields, "mover", profile->fields.has_mover, profile->fields.mover);
  put_number(&b, fields, "health", profile->fields.health);
  put_number(&b, fields, "takedamage", profile->fields.takedamage);
  put_number(&b, fields, "eventTime", profile->fields.event_time);
  put_number(&b, fields, "freeAfterEvent", profile->fields.free_after_event);
  put_number(&b, fields, "hook", profile->fields.hook);
  put(&b, object, "fields", fields);
  qa_save_value *globals = qa_save_value_object();
  put_number(&b, globals, "time", profile->globals.time);
  put_number(&b, globals, "frame", profile->globals.frame);
  put_number(&b, globals, "movement", profile->globals.movement);
  put_number(&b, globals, "forward", profile->globals.forward);
  put_number(&b, globals, "groundPlane", profile->globals.ground_plane);
  put(&b, object, "globals", globals);
  qa_save_value *callbacks = qa_save_value_object();
  put_number(&b, callbacks, "allocate", profile->callbacks.allocate);
  put_number(&b, callbacks, "free", profile->callbacks.free);
  put_number(&b, callbacks, "fire", profile->callbacks.fire);
  put_number(&b, callbacks, "release", profile->callbacks.release);
  put_number(&b, callbacks, "forceRelease", profile->callbacks.force_release);
  put_number(&b, callbacks, "missile", profile->callbacks.missile);
  put_nullable_number(&b, callbacks, "follow", profile->callbacks.has_follow, profile->callbacks.follow);
  put_number(&b, callbacks, "think", profile->callbacks.think);
  put_number(&b, callbacks, "pull", profile->callbacks.pull);
  put_nullable_number(&b, callbacks, "moveMoverHooks", profile->callbacks.has_move_mover_hooks,
                      profile->callbacks.move_mover_hooks);
  put_number(&b, callbacks, "damage", profile->callbacks.damage);
  put_number(&b, callbacks, "sameTeam", profile->callbacks.same_team);
  put_number(&b, callbacks, "playerMove", profile->callbacks.player_move);
  put(&b, object, "callbacks", callbacks);
  put_number(&b, object, "pullingFlag", profile->pulling_flag);
  qa_save_value *fire = qa_save_value_array();
  for (size_t i = 0; i < profile->fire_arguments.count; i++)
    push(&b, fire, qa_save_value_number(profile->fire_arguments.values[i]));
  put(&b, object, "fireArguments", fire);
  qa_save_value *movement = qa_save_value_object();
  put_number(&b, movement, "byteLength", profile->movement.byte_length);
  qa_save_value *words = qa_save_value_array();
  for (size_t i = 0; i < profile->movement.word_count; i++) {
    qa_save_value *word = qa_save_value_object();
    put_number(&b, word, "offset", profile->movement.words[i].offset);
    put_number(&b, word, "value", profile->movement.words[i].value);
    push(&b, words, word);
  }
  put(&b, movement, "words", words);
  put(&b, object, "movement", movement);
  qa_save_value *cvars = qa_save_value_object();
  for (size_t i = 0; i < profile->initial_cvar_count; i++)
    put_string(&b, cvars, profile->initial_cvars[i].name, profile->initial_cvars[i].value);
  put(&b, object, "initialCvars", cvars);
  put_number(&b, object, "eventLifetimeMilliseconds", profile->event_lifetime_milliseconds);
  put_number(&b, object, "grappleDamageMethod", profile->grapple_damage_method);
  qa_save_value *presentation = qa_save_value_object();
  put_string(&b, presentation, "projectileModel", profile->presentation.projectile_model);
  put_string(&b, presentation, "viewModel", profile->presentation.view_model);
  put_number(&b, presentation, "weaponIndex", profile->presentation.weapon_index);
  qa_save_value *anchor = qa_save_value_object();
  put_string(&b, anchor, "path", profile->presentation.view_anchor.path);
  put_string(&b, anchor, "tag", profile->presentation.view_anchor.tag);
  qa_save_value *offset = qa_save_value_object();
  put_number(&b, offset, "x", profile->presentation.view_anchor.offset.x);
  put_number(&b, offset, "y", profile->presentation.view_anchor.offset.y);
  put_number(&b, offset, "z", profile->presentation.view_anchor.offset.z);
  put(&b, anchor, "offset", offset);
  qa_save_value *fov = qa_save_value_object();
  put_number(&b, fov, "above", profile->presentation.view_anchor.fov_offset_above);
  put_number(&b, fov, "scale", profile->presentation.view_anchor.fov_offset_scale);
  put(&b, anchor, "fovOffset", fov);
  put(&b, presentation, "viewAnchor", anchor);
  qa_save_value *attachments = qa_save_value_array();
  for (size_t i = 0; i < profile->presentation.view_attachment_count; i++) {
    qa_save_value *attachment = qa_save_value_object();
    put_string(&b, attachment, "path", profile->presentation.view_attachments[i].path);
    put_string(&b, attachment, "tag", profile->presentation.view_attachments[i].tag);
    push(&b, attachments, attachment);
  }
  put(&b, presentation, "viewAttachments", attachments);
  qa_save_value *cable = qa_save_value_object();
  if (profile->presentation.cable.is_shader) {
    put_string(&b, cable, "kind", "shader");
    put_string(&b, cable, "path", profile->presentation.cable.path);
    put_number(&b, cable, "width", profile->presentation.cable.width);
  } else {
    put_string(&b, cable, "kind", "model");
    put_string(&b, cable, "flight", profile->presentation.cable.flight);
    put_string(&b, cable, "pull", profile->presentation.cable.pull);
    put_string(&b, cable, "hold", profile->presentation.cable.hold);
    put_number(&b, cable, "segmentLength", profile->presentation.cable.segment_length);
  }
  put(&b, presentation, "cable", cable);
  put_string(&b, presentation, "fireSound", profile->presentation.fire_sound);
  put_string(&b, presentation, "attachSound", profile->presentation.attach_sound);
  put_string(&b, presentation, "releaseSound", profile->presentation.release_sound);
  put_string(&b, presentation, "pullSound", profile->presentation.pull_sound);
  put_string(&b, presentation, "hangSound", profile->presentation.hang_sound);
  put(&b, object, "presentation", presentation);
  put_number(&b, object, "version", 1);
  put_string(&b, object, "artifactDigest", profile->module.digest);
  put_string(&b, object, "artifactPath", profile->module.artifact_path);
  if (b.failed) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}
