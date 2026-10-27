/* Port of src/persistence/recipe.ts (see recipe.h for the blocked
 * qa_read_recipe). GPL-2.0-or-later. */
#include "qa/persistence/recipe.h"

#include <stdlib.h>
#include <string.h>

#include "qa/persistence/qvm_grapple.h"
#include "qa/persistence/shared.h"

/* Field reader or abort (OOM has no TS counterpart). */
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *field_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *field_choice(const qa_save_reader *reader, const char *name,
                                const char *const *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *field_literal(const qa_save_reader *reader, const char *name,
                                 const char *expected) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_literal_string(&f, expected);
  qa_save_reader_destroy(&f);
  return value;
}
static int64_t field_integer_min(const qa_save_reader *reader, const char *name,
                                 double minimum) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer_min(&f, minimum);
  qa_save_reader_destroy(&f);
  return (int64_t)value;
}
static void *xmalloc(const qa_save_reader *reader, size_t size) {
  void *memory = malloc(size);
  if (memory == NULL) qa_save_reader_fail(reader, "out of memory");
  return memory;
}
static char *dup_range(const qa_save_reader *reader, const char *start,
                       size_t length) {
  char *out = xmalloc(reader, length + 1);
  memcpy(out, start, length);
  out[length] = '\0';
  return out;
}

/* TS identityParts: split(":") must yield exactly [prefix, a, b]. */
static void identity_parts(const qa_save_reader *reader, const char *prefix,
                           char **out_first, char **out_second) {
  const char *text = qa_save_reader_string(reader);
  const char *first_colon = strchr(text, ':');
  const char *second_colon =
      first_colon == NULL ? NULL : strchr(first_colon + 1, ':');
  bool extra = second_colon != NULL && strchr(second_colon + 1, ':') != NULL;
  size_t prefix_length = strlen(prefix);
  if (first_colon == NULL || second_colon == NULL || extra ||
      (size_t)(first_colon - text) != prefix_length ||
      strncmp(text, prefix, prefix_length) != 0)
    qa_save_reader_fail(reader, "expected a %s identity", prefix);
  *out_first = dup_range(reader, first_colon + 1,
                         (size_t)(second_colon - (first_colon + 1)));
  *out_second = dup_range(reader, second_colon + 1, strlen(second_colon + 1));
}
static qa_mount_id read_mount_id(const qa_save_reader *reader) {
  char *name_space = NULL, *name = NULL;
  identity_parts(reader, "mount", &name_space, &name);
  char *id = qa_create_mount_id(name_space, name);
  free(name_space); free(name);
  if (id == NULL) qa_save_reader_fail(reader, "out of memory");
  return id;
}
static qa_mount_plan_id read_mount_plan_id(const qa_save_reader *reader) {
  char *name = NULL, *revision = NULL;
  identity_parts(reader, "mount-plan", &name, &revision);
  char *id = qa_create_mount_plan_id(name, revision);
  free(name); free(revision);
  if (id == NULL) qa_save_reader_fail(reader, "out of memory");
  return id;
}

qa_content_id qa_read_content_id(const qa_save_reader *reader) {
  const qa_save_value *value = reader->value;
  if (value == NULL || value->kind != QA_SAVE_STRING ||
      !qa_is_content_id(value->text))
    qa_save_reader_fail(reader, "expected a content identity");
  return value->text;
}

qa_provider_reference qa_read_provider(const qa_save_reader *reader) {
  qa_provider_reference provider;
  qa_save_reader provider_field = field(reader, "provider");
  provider.provider = qa_namespaced(&provider_field);
  qa_save_reader_destroy(&provider_field);
  qa_save_reader content = field(reader, "content");
  provider.content = qa_read_content_id(&content);
  qa_save_reader_destroy(&content);
  return provider;
}

static qa_content_mount read_mount(const qa_save_reader *reader) {
  static const char *const kinds[] = {"archive", "loose"};
  static const char *const formats[] = {"pak", "pk3", "kpf", "zip"};
  qa_save_reader identity = field(reader, "identity");
  qa_mount_identity common;
  qa_save_reader id = field(&identity, "id");
  common.id = read_mount_id(&id);
  qa_save_reader_destroy(&id);
  qa_save_reader content = field(&identity, "content");
  common.content = qa_read_content_id(&content);
  qa_save_reader_destroy(&content);
  common.generation = field_integer_min(&identity, "generation", 0);
  qa_save_reader_destroy(&identity);
  qa_content_mount mount;
  memset(&mount, 0, sizeof mount);
  if (strcmp(field_choice(reader, "kind", kinds, 2), "loose") == 0) {
    mount.kind = QA_CONTENT_MOUNT_LOOSE;
    mount.as.loose.identity = common;
    mount.as.loose.root_path = field_string(reader, "rootPath");
  } else {
    mount.kind = QA_CONTENT_MOUNT_ARCHIVE;
    mount.as.archive.identity = common;
    const char *format = field_choice(reader, "format", formats, 4);
    mount.as.archive.format = strcmp(format, "pak") == 0 ? QA_ARCHIVE_FORMAT_PAK
        : strcmp(format, "pk3") == 0 ? QA_ARCHIVE_FORMAT_PK3
        : strcmp(format, "kpf") == 0 ? QA_ARCHIVE_FORMAT_KPF
                                     : QA_ARCHIVE_FORMAT_ZIP;
    mount.as.archive.archive_path = field_string(reader, "archivePath");
    qa_save_reader digest = field(reader, "archiveDigest");
    mount.as.archive.archive_digest = qa_read_digest(&digest);
    qa_save_reader_destroy(&digest);
  }
  return mount;
}
static qa_archive_mount read_archive(const qa_save_reader *reader) {
  qa_content_mount mount = read_mount(reader);
  if (mount.kind != QA_CONTENT_MOUNT_ARCHIVE)
    qa_save_reader_fail(reader, "expected an archive mount");
  return mount.as.archive;
}
static qa_resource_provenance read_provenance(const qa_save_reader *reader) {
  static const char *const kinds[] = {"archive", "loose"};
  qa_resource_provenance provenance;
  memset(&provenance, 0, sizeof provenance);
  provenance.member_path = field_string(reader, "memberPath");
  if (strcmp(field_choice(reader, "kind", kinds, 2), "archive") == 0) {
    qa_save_reader mount = field(reader, "mount");
    qa_archive_mount *archive = xmalloc(reader, sizeof *archive);
    *archive = read_archive(&mount);
    qa_save_reader_destroy(&mount);
    provenance.kind = QA_CONTENT_MOUNT_ARCHIVE;
    provenance.archive_mount = archive;
    provenance.member_index = field_integer_min(reader, "memberIndex", 0);
    return provenance;
  }
  qa_save_reader mount_reader = field(reader, "mount");
  qa_content_mount mount = read_mount(&mount_reader);
  qa_save_reader_destroy(&mount_reader);
  if (mount.kind != QA_CONTENT_MOUNT_LOOSE)
    qa_save_reader_fail(reader, "expected a loose mount");
  qa_loose_mount *loose = xmalloc(reader, sizeof *loose);
  *loose = mount.as.loose;
  provenance.kind = QA_CONTENT_MOUNT_LOOSE;
  provenance.loose_mount = loose;
  return provenance;
}
static qa_resource_resolution read_resolution(const qa_save_reader *reader) {
  static const char *const kinds[] = {"default-order", "prefix-order", "link"};
  qa_resource_resolution resolution;
  memset(&resolution, 0, sizeof resolution);
  qa_save_reader plan = field(reader, "plan");
  resolution.plan = read_mount_plan_id(&plan);
  qa_save_reader_destroy(&plan);
  const char *kind = field_choice(reader, "kind", kinds, 3);
  if (strcmp(kind, "default-order") == 0) {
    resolution.kind = QA_RESOURCE_RESOLUTION_DEFAULT_ORDER;
    resolution.rank = field_integer_min(reader, "rank", 0);
  } else if (strcmp(kind, "prefix-order") == 0) {
    resolution.kind = QA_RESOURCE_RESOLUTION_PREFIX_ORDER;
    resolution.rank = field_integer_min(reader, "rank", 0);
    resolution.prefix = field_string(reader, "prefix");
  } else {
    resolution.kind = QA_RESOURCE_RESOLUTION_LINK;
    resolution.source_prefix = field_string(reader, "sourcePrefix");
    resolution.target_path = field_string(reader, "targetPath");
  }
  return resolution;
}

qa_resolved_resource_reference qa_read_resource(const qa_save_reader *reader) {
  qa_resolved_resource_reference resource;
  memset(&resource, 0, sizeof resource);
  resource.requested_path = field_string(reader, "requestedPath");
  qa_save_reader provenance = field(reader, "provenance");
  resource.provenance = read_provenance(&provenance);
  qa_save_reader_destroy(&provenance);
  qa_save_reader digest = field(reader, "digest");
  resource.digest = qa_read_digest(&digest);
  qa_save_reader_destroy(&digest);
  resource.byte_length = field_integer_min(reader, "byteLength", 0);
  qa_save_reader resolution = field(reader, "resolution");
  resource.resolution = read_resolution(&resolution);
  qa_save_reader_destroy(&resolution);
  char *id = qa_create_resource_id(&resource);
  if (id == NULL) qa_save_reader_fail(reader, "out of memory");
  resource.id = id;
  if (strcmp(field_string(reader, "id"), id) != 0)
    qa_save_reader_fail(reader, "resource identity differs from its provenance");
  return resource;
}

qa_character_selection qa_read_character(const qa_save_reader *reader) {
  qa_character_selection character;
  qa_save_reader definition = field(reader, "definition");
  character.definition = qa_read_provider(&definition);
  qa_save_reader_destroy(&definition);
  qa_save_reader appearance = field(reader, "appearance");
  character.appearance = qa_read_provider(&appearance);
  qa_save_reader_destroy(&appearance);
  return character;
}

static qa_grapple_selection read_grapple(const qa_save_reader *reader) {
  static const char *const kinds[] = {"disabled", "enabled"};
  static const char *const bindings[] = {"slot", "offhand"};
  static const char *const mechanics[] = {"q1-threewave", "q2-ctf", "q2-lmctf",
                                          "q3-qvm"};
  static const char *const editions[] = {"classic", "rerelease"};
  qa_grapple_selection grapple;
  memset(&grapple, 0, sizeof grapple);
  if (strcmp(field_choice(reader, "kind", kinds, 2), "disabled") == 0)
    return grapple;
  grapple.is_enabled = true;
  qa_save_reader source = field(reader, "source");
  grapple.source = qa_read_provider(&source);
  qa_save_reader_destroy(&source);
  grapple.binding = field_choice(reader, "binding", bindings, 2);
  grapple.mechanic = field_choice(reader, "mechanic", mechanics, 4);
  if (strcmp(grapple.mechanic, "q1-threewave") == 0 ||
      strcmp(grapple.mechanic, "q2-ctf") == 0) {
    grapple.edition = field_choice(reader, "edition", editions, 2);
  } else if (strcmp(grapple.mechanic, "q2-lmctf") == 0) {
    grapple.edition = field_literal(reader, "edition", "classic");
  } else {
    grapple.edition = field_literal(reader, "edition", "classic");
    qa_save_reader profile = field(reader, "profile");
    grapple.profile = qa_read_qvm_grapple_definition(&profile);
    qa_save_reader_destroy(&profile);
    if (grapple.profile == NULL) qa_save_reader_fail(reader, "out of memory");
  }
  return grapple;
}
static qa_hand_grenade_selection read_hand_grenades(
    const qa_save_reader *reader) {
  static const char *const kinds[] = {"disabled", "enabled"};
  static const char *const editions[] = {"classic", "rerelease"};
  qa_hand_grenade_selection grenades;
  memset(&grenades, 0, sizeof grenades);
  if (strcmp(field_choice(reader, "kind", kinds, 2), "disabled") == 0)
    return grenades;
  grenades.is_enabled = true;
  grenades.initial_ammo = (double)field_integer_min(reader, "initialAmmo", 0);
  grenades.capacity = (double)field_integer_min(reader, "capacity", 0);
  if (grenades.initial_ammo > grenades.capacity)
    qa_save_reader_fail(reader, "hand grenade allowance exceeds capacity");
  qa_save_reader source = field(reader, "source");
  grenades.source = qa_read_provider(&source);
  qa_save_reader_destroy(&source);
  (void)field_literal(reader, "binding", "offhand");
  grenades.edition = field_choice(reader, "edition", editions, 2);
  return grenades;
}

qa_equipment_selection qa_read_equipment(const qa_save_reader *reader) {
  qa_equipment_selection equipment;
  qa_save_reader grapple = field(reader, "grapple");
  equipment.grapple = read_grapple(&grapple);
  qa_save_reader_destroy(&grapple);
  qa_save_reader grenades = field(reader, "handGrenades");
  equipment.hand_grenades = read_hand_grenades(&grenades);
  qa_save_reader_destroy(&grenades);
  return equipment;
}
