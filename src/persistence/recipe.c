/* Port of src/persistence/recipe.ts. GPL-2.0-or-later. */
#include "qa/persistence/recipe.h"

#include <stdlib.h>
#include <string.h>

#include "qa/compat/q2/rerelease/native_weapon_declaration.h"
#include "qa/compat/q2/rerelease/weapon_behavior_profile.h"
#include "qa/persistence/execution.h"
#include "qa/persistence/mods.h"
#include "qa/persistence/native_weapon.h"
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

/* --- qa_read_recipe and its static readers (TS readRecipe) --- */
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static double field_integer(const qa_save_reader *reader, const char *name,
                            double minimum) {
  return (double)field_integer_min(reader, name, minimum);
}
/* Generic list loop is open-coded per element type below, matching
 * the mods.c style. `with_field` reads one named sub-reader field. */
static qa_recipe_id read_recipe_id(const qa_save_reader *reader) {
  char *name = NULL, *revision = NULL;
  identity_parts(reader, "recipe", &name, &revision);
  char *id = qa_create_recipe_id(name, revision);
  free(name); free(revision);
  if (id == NULL) qa_save_reader_fail(reader, "out of memory");
  return id;
}
static qa_provider_reference field_provider(const qa_save_reader *reader,
                                            const char *name) {
  qa_save_reader f = field(reader, name);
  qa_provider_reference value = qa_read_provider(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_content_id field_content(const qa_save_reader *reader,
                                   const char *name) {
  qa_save_reader f = field(reader, name);
  qa_content_id value = qa_read_content_id(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_mount_id *read_mount_ids(const qa_save_reader *reader,
                                   size_t *out_count) {
  size_t count = qa_save_reader_list_count(reader);
  qa_mount_id *values = count > 0 ? xmalloc(reader, count * sizeof *values) : NULL;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(reader, i);
    values[i] = read_mount_id(&entry);
    qa_save_reader_destroy(&entry);
  }
  *out_count = count;
  return values;
}
static qa_resolved_mount_plan read_mount_plan(const qa_save_reader *reader) {
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  qa_save_reader id = field(reader, "id");
  plan.id = read_mount_plan_id(&id);
  qa_save_reader_destroy(&id);
  {
    qa_save_reader mounts = field(reader, "mounts");
    size_t count = qa_save_reader_list_count(&mounts);
    qa_content_mount *values =
        count > 0 ? xmalloc(&mounts, count * sizeof *values) : NULL;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&mounts, i);
      values[i] = read_mount(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&mounts);
    plan.mounts = values;
    plan.mount_count = count;
  }
  {
    qa_save_reader order = field(reader, "defaultOrder");
    plan.default_order = read_mount_ids(&order, &plan.default_order_count);
    qa_save_reader_destroy(&order);
  }
  {
    qa_save_reader orders = field(reader, "prefixOrders");
    size_t count = qa_save_reader_list_count(&orders);
    qa_prefix_mount_order *values =
        count > 0 ? xmalloc(&orders, count * sizeof *values) : NULL;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&orders, i);
      values[i].prefix = field_string(&entry, "prefix");
      qa_save_reader mounts = field(&entry, "mounts");
      values[i].mounts = read_mount_ids(&mounts, &values[i].mount_count);
      qa_save_reader_destroy(&mounts);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&orders);
    plan.prefix_orders = values;
    plan.prefix_order_count = count;
  }
  return plan;
}
static qa_campaign_selection read_campaign(const qa_save_reader *reader) {
  static const char *const kinds[] = {"none", "campaign"};
  qa_campaign_selection campaign;
  memset(&campaign, 0, sizeof campaign);
  if (strcmp(field_choice(reader, "kind", kinds, 2), "none") == 0) return campaign;
  campaign.is_campaign = true;
  campaign.mission = field_provider(reader, "mission");
  campaign.gamecode = field_provider(reader, "gamecode");
  return campaign;
}
static qa_monster_selection_target read_monster_target(
    const qa_save_reader *reader) {
  qa_monster_selection_target target;
  memset(&target, 0, sizeof target);
  qa_save_reader kind = field(reader, "kind");
  bool has_kind = !qa_save_reader_is_undefined(&kind);
  qa_save_reader_destroy(&kind);
  if (has_kind) {
    (void)field_literal(reader, "kind", "map-defined");
    const qa_save_value *value = reader->value;
    if (value == NULL || value->kind != QA_SAVE_OBJECT)
      qa_save_reader_fail(reader, "expected only a native monster target kind");
    for (size_t i = 0; i < value->entry_count; i++)
      if (strcmp(value->entries[i].key, "kind") != 0)
        qa_save_reader_fail(reader,
                            "expected only a native monster target kind");
    target.is_map_defined = true;
    return target;
  }
  target.reference.source = field_provider(reader, "source");
  target.reference.classname = field_string(reader, "classname");
  return target;
}
static qa_enemy_selection read_enemies(const qa_save_reader *reader) {
  static const char *const kinds[] = {"map-defined", "replace", "players"};
  qa_enemy_selection enemies;
  memset(&enemies, 0, sizeof enemies);
  const char *kind = field_choice(reader, "kind", kinds, 3);
  if (strcmp(kind, "map-defined") == 0) {
    enemies.kind = QA_ENEMY_SELECTION_MAP_DEFINED;
    return enemies;
  }
  if (strcmp(kind, "players") == 0) {
    enemies.kind = QA_ENEMY_SELECTION_PLAYERS;
    enemies.character = field_provider(reader, "character");
    return enemies;
  }
  enemies.kind = QA_ENEMY_SELECTION_REPLACE;
  qa_save_reader overrides = field(reader, "byClassname");
  const qa_save_value *value = overrides.value;
  if (value == NULL || value->kind != QA_SAVE_OBJECT)
    qa_save_reader_fail(&overrides, "expected authored classname replacements");
  qa_save_reader fallback = field(reader, "default");
  enemies.default_target = read_monster_target(&fallback);
  qa_save_reader_destroy(&fallback);
  size_t count = value->entry_count;
  qa_monster_by_classname *values =
      count > 0 ? xmalloc(&overrides, count * sizeof *values) : NULL;
  for (size_t i = 0; i < count; i++) {
    values[i].classname = value->entries[i].key;
    qa_save_reader entry = field(&overrides, value->entries[i].key);
    values[i].target = read_monster_target(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&overrides);
  enemies.by_classname = values;
  enemies.by_classname_count = count;
  return enemies;
}
static qa_environment_selection read_environment(const qa_save_reader *reader) {
  static const char *const kinds[] = {"audio-content", "disabled", "selected"};
  qa_environment_selection environment;
  memset(&environment, 0, sizeof environment);
  if (qa_save_reader_is_undefined(reader)) {
    environment.kind = QA_ENVIRONMENT_AUDIO_CONTENT;
    return environment;
  }
  const char *kind = field_choice(reader, "kind", kinds, 3);
  if (strcmp(kind, "audio-content") == 0) {
    environment.kind = QA_ENVIRONMENT_AUDIO_CONTENT;
    return environment;
  }
  if (strcmp(kind, "disabled") == 0) {
    environment.kind = QA_ENVIRONMENT_DISABLED;
    return environment;
  }
  environment.kind = QA_ENVIRONMENT_SELECTED;
  qa_save_reader resource = field(reader, "resource");
  environment.resource.content = field_content(&resource, "content");
  environment.resource.path = field_string(&resource, "path");
  qa_save_reader_destroy(&resource);
  return environment;
}
static qa_presentation_selection read_presentation(
    const qa_save_reader *reader) {
  static const char *const kinds[] = {"source", "disabled"};
  qa_presentation_selection presentation;
  memset(&presentation, 0, sizeof presentation);
  qa_save_reader doppler = field(reader, "doppler");
  presentation.doppler.is_source =
      qa_save_reader_is_undefined(&doppler) ||
      strcmp(field_choice(&doppler, "kind", kinds, 2), "source") == 0;
  qa_save_reader_destroy(&doppler);
  qa_save_reader environment = field(reader, "environment");
  presentation.environment = read_environment(&environment);
  qa_save_reader_destroy(&environment);
  presentation.assets = field_content(reader, "assets");
  presentation.hud = field_provider(reader, "hud");
  presentation.effects = field_provider(reader, "effects");
  presentation.audio = field_provider(reader, "audio");
  return presentation;
}
/* TS GameApiIdentity kind strings, recovered from the C union. */
static const char *api_kind_name(qa_game_api_identity api) {
  switch (api.kind) {
    case QA_GAME_API_QUAKEC:
      return api.as.quakec.kind == QA_QUAKEC_API_Q1_NETQUAKE ? "q1-netquake"
                                                              : "q1-quakeworld";
    case QA_GAME_API_Q2_GAME:
      return api.as.q2_game.kind == QA_Q2_GAME_API_CLASSIC ? "q2-classic-game"
                                                           : "q2-rerelease-game";
    case QA_GAME_API_Q2_CGAME:
      return "q2-rerelease-cgame";
    case QA_GAME_API_Q3:
      return api.as.q3.kind == QA_Q3_API_QAGAME ? "q3-qagame"
             : api.as.q3.kind == QA_Q3_API_CGAME ? "q3-cgame"
                                                 : "q3-ui";
  }
  return "";
}
static bool api_is(qa_game_api_identity api, const char *a, const char *b) {
  const char *kind = api_kind_name(api);
  return strcmp(kind, a) == 0 || (b != NULL && strcmp(kind, b) == 0);
}
static qa_resolved_execution_module read_execution(
    const qa_save_reader *reader) {
  static const char *const roles[] = {"server-game", "client-game", "ui"};
  static const char *const kinds[] = {"typescript", "quakec", "qvm", "native"};
  qa_resolved_execution_module module;
  memset(&module, 0, sizeof module);
  module.owner = field_provider(reader, "owner");
  qa_save_reader api = field(reader, "api");
  module.api = qa_read_api(&api);
  qa_save_reader_destroy(&api);
  module.role = field_choice(reader, "role", roles, 3);
  const char *kind = field_choice(reader, "kind", kinds, 4);
  bool server = strcmp(module.role, "server-game") == 0;
  bool client = strcmp(module.role, "client-game") == 0;
  if (strcmp(kind, "typescript") == 0) {
    module.kind = QA_EXECUTION_MODULE_TYPESCRIPT;
    qa_save_reader implementation = field(reader, "implementation");
    module.implementation = qa_namespaced(&implementation);
    qa_save_reader_destroy(&implementation);
    if (api_is(module.api, "q2-rerelease-cgame", NULL)) {
      if (!client)
        qa_save_reader_fail(reader, "cgame API requires client-game role");
    } else if (api_is(module.api, "q3-cgame", NULL)) {
      if (!client)
        qa_save_reader_fail(reader, "cgame API requires client-game role");
    } else if (api_is(module.api, "q3-ui", NULL)) {
      if (strcmp(module.role, "ui") != 0)
        qa_save_reader_fail(reader, "UI API requires ui role");
    } else if (!server) {
      qa_save_reader_fail(reader, "game API requires server-game role");
    }
    return module;
  }
  qa_save_reader artifact_reader = field(reader, "artifact");
  qa_resolved_resource_reference *artifact =
      xmalloc(reader, sizeof *artifact);
  *artifact = qa_read_resource(&artifact_reader);
  qa_save_reader_destroy(&artifact_reader);
  module.artifact = artifact;
  if (strcmp(kind, "quakec") == 0) {
    module.kind = QA_EXECUTION_MODULE_QUAKEC;
    if (!server || !api_is(module.api, "q1-netquake", "q1-quakeworld"))
      qa_save_reader_fail(reader, "QuakeC requires a Q1 server-game API");
    return module;
  }
  if (strcmp(kind, "qvm") == 0) {
    module.kind = QA_EXECUTION_MODULE_QVM;
    if (api_is(module.api, "q3-qagame", NULL)) {
      if (!server) qa_save_reader_fail(reader, "qagame requires server-game role");
    } else if (api_is(module.api, "q3-cgame", NULL)) {
      if (!client) qa_save_reader_fail(reader, "cgame requires client-game role");
    } else if (api_is(module.api, "q3-ui", NULL)) {
      if (strcmp(module.role, "ui") != 0)
        qa_save_reader_fail(reader, "UI requires ui role");
    } else {
      qa_save_reader_fail(reader, "QVM requires a Q3 API");
    }
    return module;
  }
  module.kind = QA_EXECUTION_MODULE_NATIVE;
  qa_save_reader profile = field(reader, "profile");
  module.profile = qa_read_native_abi(&profile);
  qa_save_reader_destroy(&profile);
  if (api_is(module.api, "q2-classic-game", "q2-rerelease-game") ||
      api_is(module.api, "q3-qagame", NULL)) {
    if (!server) qa_save_reader_fail(reader, "game API requires server-game role");
  } else if (api_is(module.api, "q2-rerelease-cgame", "q3-cgame")) {
    if (!client)
      qa_save_reader_fail(reader, "cgame API requires client-game role");
  } else if (api_is(module.api, "q3-ui", NULL)) {
    if (strcmp(module.role, "ui") != 0)
      qa_save_reader_fail(reader, "UI API requires ui role");
  } else {
    qa_save_reader_fail(reader, "native game module cannot use a QuakeC API");
  }
  return module;
}
/* Compat-backed profiles for qa_read_saved_native_weapon_declaration
 * (TS native-weapon.ts imports these compat functions directly). */
static bool profiles_built_in(void *ctx, qa_module_identity module,
                              qa_native_weapon_behavior_declaration *out) {
  (void)ctx;
  return qa_compat_q2_built_in_rerelease_weapon_declaration(module, out);
}
static qa_native_weapon_behavior_declaration profiles_read(
    void *ctx, const qa_save_value *value, qa_module_identity module) {
  (void)ctx;
  qa_native_weapon_behavior_declaration out;
  if (!qa_compat_q2_read_native_weapon_declaration(value, &module, &out))
    qa_save_format_error("native-weapon-profile", "out of memory");
  return out;
}
static bool profiles_definition(
    void *ctx, qa_module_identity module,
    const qa_native_weapon_behavior_declaration *declaration,
    qa_weapon_behavior_definition *out) {
  (void)ctx;
  return qa_compat_q2_rerelease_weapon_definition(module, declaration, out);
}
static const qa_native_weapon_profiles RECIPE_PROFILES = {
    NULL, profiles_built_in, profiles_read, profiles_definition};
static qa_weapon_behavior_callback read_behavior_callback(
    const qa_save_reader *reader, qa_module_identity module) {
  static const char *const kinds[] = {"quakec", "qvm", "native-artifact"};
  qa_weapon_behavior_callback callback;
  memset(&callback, 0, sizeof callback);
  const char *kind = field_choice(reader, "kind", kinds, 3);
  qa_save_reader owner = field(reader, "module");
  callback.module = qa_read_module(&owner);
  qa_save_reader_destroy(&owner);
  if (strcmp(callback.module.id, module.id) != 0 ||
      strcmp(callback.module.digest, module.digest) != 0 ||
      strcmp(callback.module.revision, module.revision) != 0 ||
      strcmp(callback.module.artifact_path, module.artifact_path) != 0)
    qa_save_reader_fail(reader,
                        "weapon behavior callback differs from selected module");
  if (strcmp(kind, "quakec") == 0) {
    callback.kind = QA_WEAPON_CALLBACK_QUAKEC;
    callback.function_index = field_integer(reader, "functionIndex", 1);
  } else if (strcmp(kind, "qvm") == 0) {
    callback.kind = QA_WEAPON_CALLBACK_QVM;
    callback.instruction_index = field_integer(reader, "instructionIndex", 0);
  } else {
    callback.kind = QA_WEAPON_CALLBACK_NATIVE_ARTIFACT;
    qa_save_reader abi = field(reader, "abi");
    callback.abi = qa_read_native_call_abi(&abi);
    qa_save_reader_destroy(&abi);
    qa_save_reader offset = field(reader, "imageOffset");
    callback.image_offset = qa_save_reader_bigint(&offset);
    qa_save_reader_destroy(&offset);
    /* TS compares against the unsigned ABI width; a negative int64
     * or a value above UINT32_MAX under a 4-byte ABI fails. Values
     * above INT64_MAX cannot be represented in the C bigint field. */
    if (callback.image_offset < 0 ||
        (callback.abi.pointer_bytes == 4 && callback.image_offset > 4294967295LL))
      qa_save_reader_fail(
          reader,
          "native behavior image offset is outside its ABI address width");
  }
  return callback;
}
static qa_resolved_weapon_behavior_selection read_weapon_behavior(
    const qa_save_reader *reader) {
  static const char *const roles[] = {"rocket", "grenade", "nail",  "bolt",
                                      "plasma", "energy",  "grapple"};
  static const char *const component_kinds[] = {"qvm", "rerelease-native"};
  static const char *const abi_profiles[] = {"q3-modern", "q3-1.16n-base"};
  qa_resolved_weapon_behavior_selection behavior;
  memset(&behavior, 0, sizeof behavior);
  qa_save_reader value = field(reader, "definition");
  qa_save_reader module_reader = field(&value, "module");
  qa_module_identity module = qa_read_module(&module_reader);
  qa_save_reader_destroy(&module_reader);
  qa_weapon_behavior_definition *definition =
      xmalloc(reader, sizeof *definition);
  memset(definition, 0, sizeof *definition);
  definition->module = module;
  qa_save_reader fire_reader = field(&value, "fire");
  definition->fire = read_behavior_callback(&fire_reader, module);
  qa_save_reader_destroy(&fire_reader);
  qa_save_reader activate = field(&value, "activate");
  if (!qa_save_reader_is_null(&activate)) {
    definition->has_activate = true;
    definition->activate = read_behavior_callback(&activate, module);
  }
  qa_save_reader_destroy(&activate);
  behavior.source = field_provider(reader, "source");
  qa_save_reader artifact_reader = field(reader, "artifact");
  behavior.artifact = qa_read_resource(&artifact_reader);
  qa_save_reader_destroy(&artifact_reader);
  if (strcmp(behavior.source.provider, module.id) != 0 ||
      strcmp(behavior.artifact.digest, module.digest) != 0 ||
      strcmp(behavior.artifact.requested_path, module.artifact_path) != 0)
    qa_save_reader_fail(reader,
                        "weapon behavior source differs from selected artifact");
  qa_save_reader id = field(&value, "id");
  definition->id = qa_namespaced(&id);
  qa_save_reader_destroy(&id);
  definition->title = field_string(&value, "title");
  definition->role = field_choice(&value, "role", roles, 7);
  definition->aspect = field_literal(&value, "aspect", "trajectory");
  qa_save_reader_destroy(&value);
  behavior.definition = definition;
  qa_save_reader saved_component = field(reader, "component");
  const char *component_kind = NULL;
  if (!qa_save_reader_is_undefined(&saved_component))
    component_kind =
        field_choice(&saved_component, "kind", component_kinds, 2);
  if ((component_kind != NULL && strcmp(component_kind, "rerelease-native") == 0) ||
      (component_kind == NULL &&
       definition->fire.kind == QA_WEAPON_CALLBACK_NATIVE_ARTIFACT)) {
    qa_native_weapon_behavior_declaration *declaration =
        xmalloc(reader, sizeof *declaration);
    if (component_kind == NULL) {
      *declaration = qa_read_saved_native_weapon_declaration(
          &saved_component, &RECIPE_PROFILES, *definition);
    } else {
      qa_save_reader decl_reader = field(&saved_component, "declaration");
      *declaration = qa_read_saved_native_weapon_declaration(
          &decl_reader, &RECIPE_PROFILES, *definition);
      qa_save_reader_destroy(&decl_reader);
    }
    behavior.has_component = true;
    behavior.component_is_qvm = false;
    behavior.component_declaration = declaration;
  } else if (component_kind != NULL && strcmp(component_kind, "qvm") == 0) {
    qa_qvm_weapon_behavior_layout *layout = xmalloc(reader, sizeof *layout);
    memset(layout, 0, sizeof *layout);
    qa_save_reader layout_reader = field(&saved_component, "layout");
    qa_save_reader fields = field(&layout_reader, "fields");
    layout->entity_stride = field_integer(&layout_reader, "entityStride", 4);
    layout->level_time = field_integer(&layout_reader, "levelTime", 4);
    if ((int64_t)layout->entity_stride % 4 != 0 ||
        (int64_t)layout->level_time % 4 != 0)
      qa_save_reader_fail(&layout_reader, "unaligned QVM behavior layout");
    double occupied[4];
    size_t occupied_count = 0;
    const char *const names[4] = {"inuse", "nextthink", "think", "health"};
    double offsets[4];
    for (size_t i = 0; i < 4; i++) {
      qa_save_reader offset = field(&fields, names[i]);
      double result = qa_save_reader_integer_min(&offset, 0);
      bool seen = false;
      for (size_t k = 0; k < occupied_count; k++)
        if (occupied[k] == result) seen = true;
      if ((int64_t)result % 4 != 0 || result > layout->entity_stride - 4 || seen)
        qa_save_reader_fail(
            &offset, "overlapping or out-of-range QVM behavior field");
      occupied[occupied_count++] = result;
      offsets[i] = result;
      qa_save_reader_destroy(&offset);
    }
    qa_save_reader_destroy(&fields);
    layout->field_inuse = offsets[0];
    layout->field_nextthink = offsets[1];
    layout->field_think = offsets[2];
    layout->field_health = offsets[3];
    layout->allocate = field_integer(&layout_reader, "allocate", 1);
    layout->free = field_integer(&layout_reader, "free", 1);
    layout->fire_abi =
        field_literal(&layout_reader, "fireAbi", "entity-pointer-start-direction");
    qa_save_reader_destroy(&layout_reader);
    behavior.has_component = true;
    behavior.component_is_qvm = true;
    behavior.component_abi_profile =
        field_choice(&saved_component, "abiProfile", abi_profiles, 2);
    behavior.component_layout = layout;
  }
  qa_save_reader_destroy(&saved_component);
  if (definition->fire.kind == QA_WEAPON_CALLBACK_QVM &&
      (!behavior.has_component || !behavior.component_is_qvm ||
       (definition->has_activate &&
        definition->activate.kind != QA_WEAPON_CALLBACK_QVM)))
    qa_save_reader_fail(
        reader, "QVM behavior requires its source layout and QVM callbacks");
  if ((definition->fire.kind == QA_WEAPON_CALLBACK_NATIVE_ARTIFACT &&
       (!behavior.has_component || behavior.component_is_qvm ||
        (definition->has_activate &&
         definition->activate.kind != QA_WEAPON_CALLBACK_NATIVE_ARTIFACT))) ||
      (definition->fire.kind == QA_WEAPON_CALLBACK_QUAKEC &&
       (behavior.has_component ||
        (definition->has_activate &&
         definition->activate.kind != QA_WEAPON_CALLBACK_QUAKEC))))
    qa_save_reader_fail(
        reader,
        "weapon behavior callbacks and component declaration do not match");
  return behavior;
}
qa_executable_recipe qa_read_recipe(const qa_save_reader *reader) {
  qa_executable_recipe recipe;
  memset(&recipe, 0, sizeof recipe);
  qa_save_reader mods = field(reader, "mods");
  if (!qa_save_reader_is_undefined(&mods)) {
    size_t count = qa_save_reader_list_count(&mods);
    const qa_resolved_gameplay_mod **values =
        count > 0 ? xmalloc(&mods, count * sizeof *values) : NULL;
    for (size_t i = 0; i < count; i++) {
      qa_resolved_gameplay_mod *mod = xmalloc(&mods, sizeof *mod);
      qa_save_reader entry = item(&mods, i);
      *mod = qa_read_gameplay_mod(&entry);
      qa_save_reader_destroy(&entry);
      values[i] = mod;
    }
    recipe.mods = values;
    recipe.mod_count = count;
  }
  qa_save_reader_destroy(&mods);
  qa_save_reader behaviors = field(reader, "weaponBehaviors");
  if (!qa_save_reader_is_undefined(&behaviors)) {
    size_t count = qa_save_reader_list_count(&behaviors);
    qa_resolved_weapon_behavior_selection *values =
        count > 0 ? xmalloc(&behaviors, count * sizeof *values) : NULL;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&behaviors, i);
      values[i] = read_weapon_behavior(&entry);
      qa_save_reader_destroy(&entry);
    }
    recipe.weapon_behaviors = values;
    recipe.weapon_behavior_count = count;
  }
  qa_save_reader_destroy(&behaviors);
  qa_save_reader version = field(reader, "schemaVersion");
  recipe.schema_version = (int64_t)qa_save_reader_literal_number(&version, 3);
  qa_save_reader_destroy(&version);
  qa_save_reader id = field(reader, "id");
  recipe.id = read_recipe_id(&id);
  qa_save_reader_destroy(&id);
  qa_save_reader preset = field(reader, "preset");
  recipe.preset = read_recipe_id(&preset);
  qa_save_reader_destroy(&preset);
  {
    qa_save_reader map = field(reader, "map");
    recipe.map.geometry_content = field_content(&map, "geometryContent");
    qa_save_reader geometry = field(&map, "geometry");
    recipe.map.geometry = qa_read_resource(&geometry);
    qa_save_reader_destroy(&geometry);
    recipe.map.entities = field_provider(&map, "entities");
    qa_save_reader_destroy(&map);
  }
  {
    qa_save_reader campaign = field(reader, "campaign");
    recipe.campaign = read_campaign(&campaign);
    qa_save_reader_destroy(&campaign);
  }
  recipe.movement = field_provider(reader, "movement");
  {
    qa_save_reader character = field(reader, "character");
    recipe.character = qa_read_character(&character);
    qa_save_reader_destroy(&character);
  }
  {
    qa_save_reader weapons = field(reader, "weapons");
    size_t count = qa_save_reader_list_count(&weapons);
    qa_provider_reference *values =
        count > 0 ? xmalloc(&weapons, count * sizeof *values) : NULL;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&weapons, i);
      values[i] = qa_read_provider(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&weapons);
    recipe.weapons.values = values;
    recipe.weapons.count = count;
  }
  {
    qa_save_reader equipment = field(reader, "equipment");
    recipe.equipment = qa_read_equipment(&equipment);
    qa_save_reader_destroy(&equipment);
  }
  {
    qa_save_reader enemies = field(reader, "enemies");
    recipe.enemies = read_enemies(&enemies);
    qa_save_reader_destroy(&enemies);
  }
  {
    qa_save_reader presentation = field(reader, "presentation");
    recipe.presentation = read_presentation(&presentation);
    qa_save_reader_destroy(&presentation);
  }
  recipe.engine_behavior = field_provider(reader, "engineBehavior");
  recipe.combat = field_provider(reader, "combat");
  recipe.inventory = field_provider(reader, "inventory");
  recipe.match = field_provider(reader, "match");
  recipe.transition = field_provider(reader, "transition");
  {
    qa_save_reader execution = field(reader, "execution");
    size_t count = qa_save_reader_list_count(&execution);
    qa_resolved_execution_module *values =
        count > 0 ? xmalloc(&execution, count * sizeof *values) : NULL;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&execution, i);
      values[i] = read_execution(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&execution);
    recipe.execution = values;
    recipe.execution_count = count;
  }
  {
    qa_save_reader mounts = field(reader, "mounts");
    recipe.mounts = read_mount_plan(&mounts);
    qa_save_reader_destroy(&mounts);
  }
  {
    qa_save_reader resources = field(reader, "resources");
    size_t count = qa_save_reader_list_count(&resources);
    qa_resolved_resource_reference *values =
        count > 0 ? xmalloc(&resources, count * sizeof *values) : NULL;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&resources, i);
      values[i] = qa_read_resource(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&resources);
    recipe.resources = values;
    recipe.resource_count = count;
  }
  {
    qa_save_reader timing = field(reader, "timing");
    size_t count = qa_save_reader_list_count(&timing);
    qa_provider_timing *values =
        count > 0 ? xmalloc(&timing, count * sizeof *values) : NULL;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&timing, i);
      qa_save_reader provider = field(&entry, "provider");
      values[i].provider = qa_namespaced(&provider);
      qa_save_reader_destroy(&provider);
      qa_save_reader clock = field(&entry, "clock");
      values[i].clock = qa_read_clock(&clock);
      qa_save_reader_destroy(&clock);
      qa_save_reader numeric = field(&entry, "numeric");
      values[i].numeric = qa_read_numeric(&numeric);
      qa_save_reader_destroy(&numeric);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&timing);
    recipe.timing = values;
    recipe.timing_count = count;
  }
  {
    qa_save_reader ordering = field(reader, "ordering");
    recipe.ordering = qa_read_ordering(&ordering);
    qa_save_reader_destroy(&ordering);
  }
  return recipe;
}
