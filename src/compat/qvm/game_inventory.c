/* Port of src/compat/qvm/game-inventory.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/game_inventory.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/item_storage.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static bool is_integer(double v) { return isfinite(v) && v == trunc(v); }
static bool same_text(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

typedef struct inventory_field {
  qa_item_id item;
  double weapon;
  bool is_weapon;
} inventory_field;

typedef struct binding_ctx {
  qa_qvm_inventory_options options;
  inventory_field *fields;
  size_t field_count, field_capacity;
  const qa_qvm_inventory_weapon *previous;
  bool has_previous;
  qa_inventory_entry *entries;
  size_t entry_capacity;
} binding_ctx;

static void validate_profile(const qa_qvm_inventory_options *options) {
  const qa_module_identity *identity = &qa_qvm_module_get_profile(options->module)->module;
  const qa_module_identity *profile_module;
  qa_qvm_abi_profile profile_abi;
  if (options->profile.private_profile != NULL) {
    profile_module = &options->profile.private_profile->module;
    profile_abi = options->profile.private_profile->abi_profile;
  } else {
    profile_module = &options->profile.public_profile->module;
    profile_abi = options->profile.public_profile->abi_profile;
  }
  qa_qvm_abi_profile module_abi = qa_qvm_module_get_abi_profile(options->module);
  if (!same_text(identity->id, profile_module->id) ||
      !same_text(identity->digest, profile_module->digest) ||
      !same_text(identity->artifact_path, profile_module->artifact_path) ||
      !same_text(identity->revision, profile_module->revision) ||
      !same_text(module_abi, profile_abi) || !same_text(options->data->abi_profile, profile_abi))
    qa_range_error("QVM inventory profile belongs to another source module");
}

static void refresh(binding_ctx *ctx) {
  const qa_qvm_inventory_weapons_source *source = &ctx->options.weapons;
  size_t count = source->count;
  const qa_qvm_inventory_weapon *definitions =
      source->get != NULL ? source->get(source->ctx, &count) : source->list;
  if (ctx->has_previous && definitions == ctx->previous) return;
  ctx->field_count = 0;
  bool slots[16] = {false};
  for (size_t i = 0; i < count; i++) {
    const qa_qvm_inventory_weapon *definition = &definitions[i];
    if (!is_integer(definition->weapon) || definition->weapon < 1 || definition->weapon >= 16 ||
        slots[(size_t)definition->weapon])
      qa_range_error("QVM inventory requires distinct public weapon slots");
    slots[(size_t)definition->weapon] = true;
    for (int which = 0; which < 2; which++) {
      qa_item_id item = which == 0 ? definition->item : definition->ammo;
      if (which == 1 && item == NULL) continue;
      for (size_t f = 0; f < ctx->field_count; f++)
        if (strcmp(ctx->fields[f].item, item) == 0)
          qa_range_error("QVM inventory item %s aliases another source field", item);
      if (ctx->field_count == ctx->field_capacity) {
        size_t capacity = ctx->field_capacity == 0 ? 16 : ctx->field_capacity * 2;
        inventory_field *grown = realloc(ctx->fields, capacity * sizeof *grown);
        if (grown == NULL) qa_range_error("out of memory");
        ctx->fields = grown;
        ctx->field_capacity = capacity;
      }
      ctx->fields[ctx->field_count].item = item;
      ctx->fields[ctx->field_count].weapon = definition->weapon;
      ctx->fields[ctx->field_count].is_weapon = which == 0;
      ctx->field_count++;
    }
  }
  ctx->previous = definitions;
  ctx->has_previous = true;
}

static void public_view(binding_ctx *ctx, qa_qvm_writable_view *out) {
  qa_qvm_memory_assert_live(ctx->options.module->memory);
  refresh(ctx);
  const qa_qvm_public_inventory_profile *profile = ctx->options.profile.public_profile;
  qa_qvm_game_data_public_player_bytes(ctx->options.data, ctx->options.client(ctx->options.ctx), out);
  if ((int64_t)profile->weapons_offset + 4 > out->length ||
      (int64_t)profile->ammo_offset + 64 > out->length)
    qa_range_error("QVM inventory fields exceed the public player record");
}

static double field_capacity(binding_ctx *ctx, const inventory_field *field) {
  const qa_qvm_public_inventory_profile *profile = ctx->options.profile.public_profile;
  if (field->is_weapon) return 1;
  qa_qvm_writable_view client_view, entity_view;
  qa_qvm_game_data_client_bytes(ctx->options.data, ctx->options.client(ctx->options.ctx), &client_view);
  qa_qvm_game_data_entity_bytes(ctx->options.data, ctx->options.client(ctx->options.ctx), &entity_view);
  qa_qvm_capacity_context context = {ctx->options.module, (double)client_view.start,
                                     (double)entity_view.start, ctx->options.client(ctx->options.ctx)};
  double value = profile->capacity(profile->ctx, ctx->options.module->memory, field->weapon, context);
  if (!is_integer(value) || value < 0 || value > 2147483647.0)
    qa_range_error("QVM source ammo capacity is not a nonnegative int32");
  return value;
}

static const qa_inventory_entry *public_read(void *ctx, size_t *out_count) {
  binding_ctx *binding = ctx;
  qa_qvm_writable_view source;
  public_view(binding, &source);
  const qa_qvm_public_inventory_profile *profile = binding->options.profile.public_profile;
  if (binding->entry_capacity < binding->field_count) {
    qa_inventory_entry *grown = realloc(binding->entries, binding->field_count * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    binding->entries = grown;
    binding->entry_capacity = binding->field_count;
  }
  for (size_t i = 0; i < binding->field_count; i++) {
    inventory_field *field = &binding->fields[i];
    qa_inventory_entry *entry = &binding->entries[i];
    memset(entry, 0, sizeof *entry);
    entry->item = field->item;
    if (field->is_weapon) {
      entry->count =
          (qa_qvm_writable_view_get_int32(&source, (int64_t)profile->weapons_offset, true) &
           (1 << (int)field->weapon)) == 0
              ? 0
              : 1;
      entry->capacity = 1;
    } else {
      entry->count = qa_qvm_writable_view_get_int32(
          &source, (int64_t)profile->ammo_offset + (int64_t)field->weapon * 4, true);
      entry->capacity = field_capacity(binding, field);
      entry->has_count_policy = true;
      entry->count_policy.kind = "source-counter";
      entry->count_policy.arithmetic = "int32";
    }
  }
  *out_count = binding->field_count;
  return binding->entries;
}

static void public_write(void *ctx, qa_inventory_entry entry) {
  binding_ctx *binding = ctx;
  qa_qvm_writable_view source;
  public_view(binding, &source);
  inventory_field *field = NULL;
  for (size_t i = 0; i < binding->field_count; i++)
    if (strcmp(binding->fields[i].item, entry.item) == 0) field = &binding->fields[i];
  if (field == NULL) qa_range_error("QVM inventory did not admit %s", entry.item);
  if (entry.capacity != field_capacity(binding, field))
    qa_range_error("QVM inventory cannot change source capacity");
  const qa_qvm_public_inventory_profile *profile = binding->options.profile.public_profile;
  if (field->is_weapon) {
    if ((entry.count != 0 && entry.count != 1) ||
        (entry.has_count_policy && strcmp(entry.count_policy.kind, "stack") != 0))
      qa_range_error("QVM weapon ownership requires a zero or one stack");
    int32_t before = qa_qvm_writable_view_get_int32(&source, (int64_t)profile->weapons_offset, true);
    int32_t bit = 1 << (int)field->weapon;
    qa_qvm_writable_view_set_int32(&source, (int64_t)profile->weapons_offset,
                                   entry.count == 0 ? before & ~bit : before | bit, true);
  } else {
    if (!is_integer(entry.count) || entry.count < -2147483648.0 || entry.count > 2147483647.0 ||
        !entry.has_count_policy || strcmp(entry.count_policy.kind, "source-counter") != 0 ||
        strcmp(entry.count_policy.arithmetic, "int32") != 0)
      qa_range_error("QVM ammo requires its signed int32 source counter");
    qa_qvm_writable_view_set_int32(
        &source, (int64_t)profile->ammo_offset + (int64_t)field->weapon * 4, qa_int32(entry.count),
        true);
  }
}

/* Private inventory access over game-data records. */
typedef struct access_ctx {
  binding_ctx *binding;
} access_ctx;

static int64_t field_address(binding_ctx *binding, qa_qvm_item_field field, const char *stride_message) {
  const qa_qvm_private_inventory_profile *profile = binding->options.profile.private_profile;
  if (stride_message != NULL) qa_qvm_memory_assert_live(binding->options.module->memory);
  double slot = binding->options.client(binding->options.ctx);
  if (stride_message != NULL &&
      (qa_qvm_game_data_get_entity_stride_bytes(binding->options.data) != profile->entity_stride ||
       qa_qvm_game_data_get_client_stride_bytes(binding->options.data) != profile->client_stride))
    qa_range_error("%s", stride_message);
  qa_qvm_writable_view record;
  bool has_record = true;
  if (strcmp(field.record, "client") == 0)
    qa_qvm_game_data_client_bytes(binding->options.data, slot, &record);
  else if (strcmp(field.record, "entity") == 0)
    qa_qvm_game_data_entity_bytes(binding->options.data, slot, &record);
  else
    has_record = false;
  if (!has_record || field.offset < 0 ||
      (stride_message != NULL && (int64_t)field.offset % 4 != 0) ||
      field.offset + 4 > (double)record.length)
    qa_range_error(stride_message != NULL ? "Private QVM inventory field exceeds its source record"
                                          : "Projected QVM item field exceeds its original record");
  return record.start + (int64_t)field.offset;
}

static double access_read(void *ctx, qa_qvm_item_field field) {
  access_ctx *access = ctx;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(access->binding->options.module->memory,
                          field_address(access->binding, field, "Private QVM inventory source records changed"),
                          4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}
static double access_global(void *ctx, double address) {
  access_ctx *access = ctx;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(access->binding->options.module->memory, (int64_t)address, 4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}
static void access_write(void *ctx, qa_qvm_item_field field, double value) {
  access_ctx *access = ctx;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(access->binding->options.module->memory,
                          field_address(access->binding, field, "Private QVM inventory source records changed"),
                          4, &view);
  qa_qvm_writable_view_set_int32(&view, 0, qa_int32(value), true);
}

static const qa_qvm_item_storage *private_storage_for(binding_ctx *binding, qa_item_id item) {
  const qa_qvm_private_inventory_profile *profile = binding->options.profile.private_profile;
  /* TS Map.set in storage order: the last storage naming an item owns it. */
  for (size_t i = profile->storage_count; i-- > 0;) {
    const qa_qvm_item_storage *storage = &profile->storage[i];
    if (strcmp(storage->kind, "counter") == 0) {
      if (strcmp(storage->item, item) == 0) return storage;
    } else {
      for (size_t j = 0; j < storage->item_count; j++)
        if (strcmp(storage->items[j].item, item) == 0) return storage;
    }
  }
  return NULL;
}

static qa_qvm_item_storage_access private_access(access_ctx *storage, binding_ctx *binding) {
  storage->binding = binding;
  qa_qvm_item_storage_access access = {storage, access_read, access_global, access_write};
  return access;
}

static const qa_inventory_entry *private_read(void *ctx, size_t *out_count) {
  binding_ctx *binding = ctx;
  const qa_qvm_private_inventory_profile *profile = binding->options.profile.private_profile;
  access_ctx storage;
  size_t total = 0;
  /* First pass counts, second fills (entries are binding scratch). */
  for (size_t i = 0; i < profile->storage_count; i++) {
    qa_qvm_inventory_entries entries;
    if (!qa_qvm_read_item_storage(profile->image, profile->storage[i],
                                  private_access(&storage, binding), &entries))
      qa_range_error("out of memory");
    total += entries.count;
    qa_qvm_inventory_entries_destroy(&entries);
  }
  if (binding->entry_capacity < total) {
    qa_inventory_entry *grown = realloc(binding->entries, (total == 0 ? 1 : total) * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    binding->entries = grown;
    binding->entry_capacity = total;
  }
  size_t index = 0;
  for (size_t i = 0; i < profile->storage_count; i++) {
    qa_qvm_inventory_entries entries;
    if (!qa_qvm_read_item_storage(profile->image, profile->storage[i],
                                  private_access(&storage, binding), &entries))
      qa_range_error("out of memory");
    for (size_t j = 0; j < entries.count; j++) binding->entries[index++] = entries.entries[j];
    qa_qvm_inventory_entries_destroy(&entries);
  }
  *out_count = index;
  return binding->entries;
}

static bool private_entry(void *ctx, qa_item_id item, qa_inventory_entry *out) {
  binding_ctx *binding = ctx;
  const qa_qvm_item_storage *storage = private_storage_for(binding, item);
  if (storage == NULL) return false;
  access_ctx access_storage;
  qa_qvm_inventory_entries entries;
  if (!qa_qvm_read_item_storage(binding->options.profile.private_profile->image, *storage,
                                private_access(&access_storage, binding), &entries))
    qa_range_error("out of memory");
  bool found = false;
  for (size_t i = 0; i < entries.count; i++)
    if (!found && strcmp(entries.entries[i].item, item) == 0) {
      *out = entries.entries[i];
      found = true;
    }
  qa_qvm_inventory_entries_destroy(&entries);
  return found;
}

static bool private_mutable_capacity(void *ctx, qa_item_id item) {
  binding_ctx *binding = ctx;
  const qa_qvm_item_storage *storage = private_storage_for(binding, item);
  return storage != NULL && strcmp(storage->kind, "counter") == 0 &&
         strcmp(storage->capacity.kind, "field") == 0;
}

static void private_write(void *ctx, qa_inventory_entry entry) {
  binding_ctx *binding = ctx;
  const qa_qvm_item_storage *storage = private_storage_for(binding, entry.item);
  if (storage == NULL) qa_range_error("QVM inventory did not admit %s", entry.item);
  access_ctx access_storage;
  qa_qvm_write_item_storage(binding->options.profile.private_profile->image, *storage, entry,
                            private_access(&access_storage, binding));
}

qa_inventory_state_binding qa_qvm_inventory_binding(qa_qvm_inventory_options options) {
  validate_profile(&options);
  binding_ctx *binding = calloc(1, sizeof *binding);
  qa_inventory_state_binding result;
  memset(&result, 0, sizeof result);
  if (binding == NULL) return result;
  binding->options = options;
  result.ctx = binding;
  if (options.profile.private_profile != NULL) {
    result.read = private_read;
    result.entry = private_entry;
    result.mutable_capacity = private_mutable_capacity;
    result.write = private_write;
    return result;
  }
  const qa_qvm_public_inventory_profile *profile = options.profile.public_profile;
  if (options.weapons.get == NULL) refresh(binding);
  double offsets[] = {profile->weapons_offset, profile->ammo_offset};
  for (size_t i = 0; i < 2; i++)
    if (!is_integer(offsets[i]) || offsets[i] < 0 || (int64_t)offsets[i] % 4 != 0)
      qa_range_error("QVM inventory fields require aligned source offsets");
  if (profile->weapons_offset >= profile->ammo_offset &&
      profile->weapons_offset < profile->ammo_offset + 64)
    qa_range_error("QVM inventory weapon bits overlap ammo counters");
  result.read = public_read;
  result.write = public_write;
  return result;
}

void qa_qvm_inventory_binding_destroy(qa_inventory_state_binding *binding) {
  if (binding == NULL || binding->ctx == NULL) return;
  binding_ctx *ctx = binding->ctx;
  free(ctx->fields);
  free(ctx->entries);
  free(ctx);
  binding->ctx = NULL;
}

typedef struct projected_access {
  binding_ctx binding;
  qa_qvm_inventory_words words;
} projected_access;

static double projected_read(void *ctx, qa_qvm_item_field field) {
  projected_access *access = ctx;
  int64_t address = field_address(&access->binding, field, NULL);
  for (size_t i = 0; i < access->words.count; i++)
    if ((int64_t)access->words.entries[i].address == address) return access->words.entries[i].value;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(access->binding.options.module->memory, address, 4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}
static double projected_global(void *ctx, double address) {
  projected_access *access = ctx;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(access->binding.options.module->memory, (int64_t)address, 4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}
static void projected_write(void *ctx, qa_qvm_item_field field, double value) {
  projected_access *access = ctx;
  int64_t address = field_address(&access->binding, field, NULL);
  for (size_t i = 0; i < access->words.count; i++)
    if ((int64_t)access->words.entries[i].address == address) {
      access->words.entries[i].value = value;
      return;
    }
  qa_qvm_inventory_word *grown =
      realloc(access->words.entries, (access->words.count + 1) * sizeof *grown);
  if (grown == NULL) qa_range_error("out of memory");
  access->words.entries = grown;
  access->words.entries[access->words.count].address = (double)address;
  access->words.entries[access->words.count].value = value;
  access->words.count++;
}

bool qa_qvm_inventory_projection(qa_qvm_inventory_options options, qa_item_id item, double count,
                                 qa_qvm_inventory_words *out) {
  validate_profile(&options);
  if (!is_integer(count) || count < -2147483648.0 || count > 2147483647.0)
    qa_range_error("QVM inventory projection requires an int32 counter");
  out->entries = NULL;
  out->count = 0;
  if (options.profile.private_profile != NULL) {
    qa_inventory_state_binding binding = qa_qvm_inventory_binding(options);
    if (binding.ctx == NULL) return false;
    qa_inventory_entry entry;
    bool has_entry = binding.entry(binding.ctx, item, &entry);
    const qa_qvm_private_inventory_profile *profile = options.profile.private_profile;
    const qa_qvm_item_storage *storage = NULL;
    for (size_t i = 0; i < profile->storage_count; i++) {
      const qa_qvm_item_storage *candidate = &profile->storage[i];
      bool matches = strcmp(candidate->kind, "counter") == 0
                         ? strcmp(candidate->item, item) == 0
                         : false;
      if (!matches && strcmp(candidate->kind, "counter") != 0)
        for (size_t j = 0; j < candidate->item_count; j++)
          matches = matches || strcmp(candidate->items[j].item, item) == 0;
      if (matches) {
        storage = candidate; /* TS profile.storage.find: the first match */
        break;
      }
    }
    if (!has_entry || storage == NULL) {
      qa_qvm_inventory_binding_destroy(&binding);
      qa_range_error("Projected QVM item has no original inventory storage");
    }
    projected_access access;
    memset(&access, 0, sizeof access);
    access.binding.options = options;
    entry.count = count;
    qa_qvm_item_storage_access storage_access = {&access, projected_read, projected_global,
                                                 projected_write};
    qa_qvm_write_item_storage(profile->image, *storage, entry, storage_access);
    qa_qvm_inventory_binding_destroy(&binding);
    size_t kept = 0;
    for (size_t i = 0; i < access.words.count; i++) {
      qa_qvm_writable_view view;
      qa_qvm_memory_data_view(options.module->memory, (int64_t)access.words.entries[i].address, 4, &view);
      if (qa_qvm_writable_view_get_int32(&view, 0, true) != (int32_t)access.words.entries[i].value)
        access.words.entries[kept++] = access.words.entries[i];
    }
    access.words.count = kept;
    *out = access.words;
    return true;
  }
  size_t definition_count = options.weapons.count;
  const qa_qvm_inventory_weapon *definitions =
      options.weapons.get != NULL ? options.weapons.get(options.weapons.ctx, &definition_count)
                                  : options.weapons.list;
  const qa_qvm_inventory_weapon *weapon = NULL;
  for (size_t i = 0; i < definition_count; i++)
    if (strcmp(definitions[i].item, item) == 0 ||
        (definitions[i].ammo != NULL && strcmp(definitions[i].ammo, item) == 0)) {
      weapon = &definitions[i];
      break;
    }
  if (weapon == NULL || weapon->weapon < 1 || weapon->weapon > 15)
    qa_range_error("Projected QVM item has no public inventory slot");
  const qa_qvm_public_inventory_profile *profile = options.profile.public_profile;
  qa_qvm_writable_view source;
  qa_qvm_game_data_public_player_bytes(options.data, options.client(options.ctx), &source);
  out->entries = malloc(sizeof *out->entries);
  if (out->entries == NULL) return false;
  out->count = 1;
  if (strcmp(weapon->item, item) == 0) {
    if (count != 0 && count != 1) qa_range_error("Projected QVM ownership requires zero or one");
    int32_t before = qa_qvm_writable_view_get_int32(&source, (int64_t)profile->weapons_offset, true);
    int32_t mask = 1 << (int)weapon->weapon;
    out->entries[0].address = (double)(source.start + (int64_t)profile->weapons_offset);
    out->entries[0].value = count == 0 ? (before & ~mask) : (before | mask);
  } else {
    out->entries[0].address =
        (double)(source.start + (int64_t)profile->ammo_offset + (int64_t)weapon->weapon * 4);
    out->entries[0].value = count;
  }
  return true;
}

void qa_qvm_inventory_words_destroy(qa_qvm_inventory_words *words) {
  if (words == NULL) return;
  free(words->entries);
  words->entries = NULL;
  words->count = 0;
}
