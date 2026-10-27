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
    fire_values[i] = qa_save_reader_integer_min(&item, -9007199254740991.0);
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
      movement_words[i].value = nested_integer(&item, "value", -2147483648.0);
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

qa_save_value *qa_qvm_grapple_profile_declaration(const qa_qvm_grapple_profile *profile) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  qa_save_value *version = qa_save_value_number(1);
  qa_save_value *digest = qa_save_value_string(profile->module.digest);
  qa_save_value *path = qa_save_value_string(profile->module.artifact_path);
  qa_save_value *id = qa_save_value_string(profile->id);
  qa_save_value *title = qa_save_value_string(profile->title);
  qa_save_value *abi = qa_save_value_string(profile->abi_profile);
  if (version == NULL || digest == NULL || path == NULL || id == NULL || title == NULL ||
      abi == NULL || !qa_save_value_set(object, "version", version) ||
      !qa_save_value_set(object, "artifactDigest", digest) ||
      !qa_save_value_set(object, "artifactPath", path) || !qa_save_value_set(object, "id", id) ||
      !qa_save_value_set(object, "title", title) || !qa_save_value_set(object, "abiProfile", abi)) {
    qa_save_value_free(object);
    qa_save_value_free(version);
    qa_save_value_free(digest);
    qa_save_value_free(path);
    qa_save_value_free(id);
    qa_save_value_free(title);
    qa_save_value_free(abi);
    return NULL;
  }
  return object;
}
