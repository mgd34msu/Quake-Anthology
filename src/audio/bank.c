/* Port of src/audio/bank.ts. See include/qa/audio/bank.h.
 * GPL-2.0-or-later. */
#include "qa/audio/bank.h"

#include <stdlib.h>
#include <string.h>

typedef struct asset_entry {
  qa_sound_asset asset;
  char *name_storage; /* owned copy of the registered path */
  /* Owned copy of the opened reference; the mounter replaces its
   * stored record (and frees its strings) on a repeated open. */
  qa_resolved_resource_reference reference;
  char *owned_strings[8];
} asset_entry;

struct qa_sound_bank {
  qa_content_mounted *content; /* borrowed */
  asset_entry **families[3];
  size_t family_counts[3];
  size_t family_capacities[3];
  asset_entry **touched;
  size_t touched_count;
  size_t touched_capacity;
};

static char *copy_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

static bool starts_with(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

static void entry_free(asset_entry *entry) {
  qa_pcm_sound_dispose(&entry->asset.pcm);
  free(entry->name_storage);
  for (size_t i = 0; i < 8; i++) free(entry->owned_strings[i]);
  free(entry);
}

static char *copy_or_null(const char *text, bool *oom) {
  if (text == NULL) return NULL;
  char *copy = copy_text(text);
  if (copy == NULL) *oom = true;
  return copy;
}

/* Deep-copies the reference strings into the entry. False on OOM. */
static bool entry_copy_reference(asset_entry *entry,
                                 const qa_resolved_resource_reference *from) {
  bool oom = false;
  entry->reference = *from;
  entry->owned_strings[0] = copy_or_null(from->id, &oom);
  entry->owned_strings[1] = copy_or_null(from->requested_path, &oom);
  entry->owned_strings[2] = copy_or_null(from->digest, &oom);
  entry->owned_strings[3] = copy_or_null(from->provenance.member_path, &oom);
  entry->owned_strings[4] = copy_or_null(from->resolution.plan, &oom);
  entry->owned_strings[5] = copy_or_null(from->resolution.prefix, &oom);
  entry->owned_strings[6] = copy_or_null(from->resolution.source_prefix, &oom);
  entry->owned_strings[7] = copy_or_null(from->resolution.target_path, &oom);
  if (oom) return false;
  entry->reference.id = entry->owned_strings[0];
  entry->reference.requested_path = entry->owned_strings[1];
  entry->reference.digest = entry->owned_strings[2];
  entry->reference.provenance.member_path = entry->owned_strings[3];
  entry->reference.resolution.plan = entry->owned_strings[4];
  entry->reference.resolution.prefix = entry->owned_strings[5];
  entry->reference.resolution.source_prefix = entry->owned_strings[6];
  entry->reference.resolution.target_path = entry->owned_strings[7];
  return true;
}

qa_sound_bank *qa_sound_bank_create(qa_content_mounted *content) {
  qa_sound_bank *bank = calloc(1, sizeof *bank);
  if (bank == NULL) return NULL;
  bank->content = content;
  return bank;
}

void qa_sound_bank_destroy(qa_sound_bank *bank) {
  if (bank == NULL) return;
  qa_sound_bank_clear(bank);
  for (int i = 0; i < 3; i++) free(bank->families[i]);
  free(bank->touched);
  free(bank);
}

qa_content_mounted *qa_sound_bank_content(const qa_sound_bank *bank) {
  return bank->content;
}

void qa_sound_bank_begin_registration(qa_sound_bank *bank) {
  bank->touched_count = 0;
}

static asset_entry *find_asset(const qa_sound_bank *bank,
                               qa_sound_family family, const char *resource) {
  if (resource == NULL) return NULL;
  int at = (int)family;
  for (size_t i = 0; i < bank->family_counts[at]; i++) {
    asset_entry *entry = bank->families[at][i];
    if (entry->asset.resource != NULL &&
        strcmp(entry->asset.resource, resource) == 0)
      return entry;
  }
  return NULL;
}

const qa_sound_asset *qa_sound_bank_get(const qa_sound_bank *bank,
                                        qa_resource_id resource,
                                        qa_sound_family family) {
  asset_entry *entry = find_asset(bank, family, resource);
  return entry != NULL ? &entry->asset : NULL;
}

static bool touch(qa_sound_bank *bank, asset_entry *entry) {
  for (size_t i = 0; i < bank->touched_count; i++)
    if (bank->touched[i] == entry) return true;
  if (bank->touched_count == bank->touched_capacity) {
    size_t capacity =
        bank->touched_capacity == 0 ? 8 : bank->touched_capacity * 2;
    asset_entry **grown = realloc(bank->touched, capacity * sizeof *grown);
    if (grown == NULL) return false;
    bank->touched = grown;
    bank->touched_capacity = capacity;
  }
  bank->touched[bank->touched_count++] = entry;
  return true;
}

static bool family_add(qa_sound_bank *bank, qa_sound_family family,
                       asset_entry *entry) {
  int at = (int)family;
  if (bank->family_counts[at] == bank->family_capacities[at]) {
    size_t capacity =
        bank->family_capacities[at] == 0 ? 8 : bank->family_capacities[at] * 2;
    asset_entry **grown =
        realloc(bank->families[at], capacity * sizeof *grown);
    if (grown == NULL) return false;
    bank->families[at] = grown;
    bank->family_capacities[at] = capacity;
  }
  bank->families[at][bank->family_counts[at]++] = entry;
  return true;
}

/* The TS register path rule; malloc'd, NULL only on OOM. */
static char *register_path(const char *name) {
  if (name[0] == '#') return copy_text(name + 1);
  if (starts_with(name, "sound/")) return copy_text(name);
  size_t length = strlen(name);
  char *path = malloc(length + 7);
  if (path == NULL) return NULL;
  memcpy(path, "sound/", 6);
  memcpy(path + 6, name, length + 1);
  return path;
}

bool qa_sound_bank_register(qa_sound_bank *bank, const char *name,
                            qa_sound_family family,
                            const qa_sound_asset **out) {
  *out = NULL;
  char *path = register_path(name);
  if (path == NULL) return false;
  qa_content_opened_resource opened = {0};
  if (!qa_content_mounted_open(bank->content, path, NULL, NULL, &opened)) {
    free(path);
    return true;
  }
  const qa_resolved_resource_reference *reference = opened.reference;
  asset_entry *prior = find_asset(bank, family, reference->id);
  if (prior != NULL) {
    bool touched = touch(bank, prior);
    free(opened.bytes);
    free(path);
    if (!touched) return false;
    *out = &prior->asset;
    return true;
  }
  /* Source engines substitute silence for an undecodable sound;
   * the TS catch turns the decode failure into this null return. */
  qa_pcm_sound pcm = {0};
  qa_wav_try_result wav_result = QA_WAV_TRY_OK;
  qa_pcm_try_result pcm_result = QA_PCM_TRY_OK;
  bool via_wav = opened.byte_length > 0 && opened.bytes[0] == 82;
  if (via_wav) {
    qa_decoded_wav decoded;
    wav_result = family == QA_SOUND_FAMILY_Q3
                     ? qa_try_decode_q3_wav(opened.bytes, opened.byte_length,
                                            path, &decoded)
                     : qa_try_decode_quake_wav(
                           opened.bytes, opened.byte_length, path, &decoded);
    if (wav_result == QA_WAV_TRY_OK) pcm = decoded.pcm;
  } else {
    pcm_result = qa_try_decode_sound_bytes(opened.bytes, opened.byte_length,
                                           path, &pcm);
  }
  free(opened.bytes);
  if ((via_wav && wav_result != QA_WAV_TRY_OK) ||
      (!via_wav && pcm_result != QA_PCM_TRY_OK)) {
    qa_wav_try_result failure = wav_result;
    free(path);
    bool invalid = via_wav ? failure == QA_WAV_TRY_INVALID
                           : pcm_result == QA_PCM_TRY_INVALID;
    return invalid; /* true with *out NULL: the TS catch -> null */
  }
  /* Source S_LoadSound rejects non-mono effects and skips them like a
     missing file. */
  if (pcm.channels != 1) {
    qa_pcm_sound_dispose(&pcm);
    free(path);
    return true;
  }
  asset_entry *entry = calloc(1, sizeof *entry);
  if (entry == NULL) {
    qa_pcm_sound_dispose(&pcm);
    free(path);
    return false;
  }
  entry->asset.pcm = pcm;
  entry->name_storage = path;
  if (!entry_copy_reference(entry, reference)) {
    entry_free(entry);
    return false;
  }
  entry->asset.resource = entry->reference.id;
  entry->asset.reference = &entry->reference;
  entry->asset.name = path;
  if (!family_add(bank, family, entry)) {
    entry_free(entry);
    return false;
  }
  if (!touch(bank, entry)) return false; /* asset stays registered */
  *out = &entry->asset;
  return true;
}

bool qa_sound_bank_register_sexed_sound(qa_sound_bank *bank, const char *base,
                                        const char *model,
                                        const qa_sound_asset **out) {
  *out = NULL;
  if (!starts_with(base, "*"))
    return qa_sound_bank_register(bank, base, QA_SOUND_FAMILY_Q2, out);
  /* model.split("/")[0] || "male" */
  size_t selected_length = strcspn(model, "/");
  const char *selected = model;
  if (selected_length == 0) {
    selected = "male";
    selected_length = 4;
  }
  const char *name = base + 1;
  size_t name_length = strlen(name);
  /* `#players/${selected}/${name}` */
  char *first = malloc(selected_length + name_length + 11);
  if (first == NULL) return false;
  char *at = first;
  memcpy(at, "#players/", 9);
  at += 9;
  memcpy(at, selected, selected_length);
  at += selected_length;
  *at++ = '/';
  memcpy(at, name, name_length + 1);
  bool ok = qa_sound_bank_register(bank, first, QA_SOUND_FAMILY_Q2, out);
  free(first);
  if (!ok || *out != NULL) return ok;
  char *fallback = malloc(name_length + 13);
  if (fallback == NULL) return false;
  memcpy(fallback, "player/male/", 12);
  memcpy(fallback + 12, name, name_length + 1);
  ok = qa_sound_bank_register(bank, fallback, QA_SOUND_FAMILY_Q2, out);
  free(fallback);
  return ok;
}

static bool was_touched(const qa_sound_bank *bank, const asset_entry *entry) {
  for (size_t i = 0; i < bank->touched_count; i++)
    if (bank->touched[i] == entry) return true;
  return false;
}

void qa_sound_bank_end_registration(qa_sound_bank *bank) {
  for (int family = 0; family < 3; family++) {
    size_t i = 0;
    while (i < bank->family_counts[family]) {
      asset_entry *entry = bank->families[family][i];
      if (was_touched(bank, entry)) {
        i++;
        continue;
      }
      entry_free(entry);
      memmove(&bank->families[family][i], &bank->families[family][i + 1],
              (bank->family_counts[family] - i - 1) *
                  sizeof *bank->families[family]);
      bank->family_counts[family]--;
    }
  }
}

bool qa_sound_bank_open_music(qa_sound_bank *bank, const char *path,
                              qa_content_id source, qa_pcm_stream **out) {
  *out = NULL;
  qa_content_opened_resource opened = {0};
  if (!qa_content_mounted_open(bank->content, path, NULL, NULL, &opened))
    return true;
  if (source != NULL) {
    const qa_resource_provenance *provenance = &opened.reference->provenance;
    qa_content_id mounted = NULL;
    if (provenance->kind == QA_CONTENT_MOUNT_ARCHIVE &&
        provenance->archive_mount != NULL)
      mounted = provenance->archive_mount->identity.content;
    if (provenance->kind == QA_CONTENT_MOUNT_LOOSE &&
        provenance->loose_mount != NULL)
      mounted = provenance->loose_mount->identity.content;
    if (mounted == NULL || strcmp(mounted, source) != 0) {
      free(opened.bytes);
      return true;
    }
  }
  qa_pcm_try_result result =
      qa_try_open_pcm_bytes(opened.bytes, opened.byte_length, path, out);
  free(opened.bytes);
  if (result == QA_PCM_TRY_INVALID) return true; /* the TS catch -> null */
  return result == QA_PCM_TRY_OK;
}

void qa_sound_bank_clear(qa_sound_bank *bank) {
  for (int family = 0; family < 3; family++) {
    for (size_t i = 0; i < bank->family_counts[family]; i++)
      entry_free(bank->families[family][i]);
    bank->family_counts[family] = 0;
  }
  bank->touched_count = 0;
}
