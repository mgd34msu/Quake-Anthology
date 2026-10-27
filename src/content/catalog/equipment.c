/* Port of src/content/catalog/equipment.ts. GPL-2.0-or-later. */
#include "qa/content/catalog/equipment.h"

#include "catalog_internal.h"
#include "qa/contracts/qvm_grapple.h"
#include "qa/core/numeric.h"

const qa_equipment_provider_set qa_equipment_provider_ids = {
    "q1:equipment/threewave-grapple", "q2:equipment/ctf-grapple",
    "q2:equipment/lmctf-grapple", "q2:equipment/hand-grenades"};

qa_equipment_selection qa_disabled_equipment(void) {
  qa_equipment_selection selection = {0};
  return selection;
}

static char *equipment_unavailable(const qa_catalog_discovered_product *product) {
  if (product->availability.kind == QA_CATALOG_INSTALLED) return NULL;
  if (product->availability.kind == QA_CATALOG_UNRESOLVED)
    return qa_catalog_dup(product->availability.reason);
  return qa_catalog_format("Requires %s game files", product->expectation.title);
}

static bool grapple_for_product(const qa_catalog_discovered_product *product,
                                qa_grapple_selection *out) {
  const qa_catalog_product_expectation *expectation = &product->expectation;
  memset(out, 0, sizeof *out);
  bool classic_or_rerelease = strcmp(expectation->edition, "classic") == 0 ||
                              strcmp(expectation->edition, "rerelease") == 0;
  if (expectation->family == QA_GAME_FAMILY_Q1 &&
      strcmp(expectation->campaign, "ctf") == 0 && classic_or_rerelease) {
    out->is_enabled = true;
    out->mechanic = "q1-threewave";
    out->edition = expectation->edition;
    out->binding = "slot";
    out->source.provider = qa_equipment_provider_ids.threewave;
    out->source.content = product->id;
    return true;
  }
  if (expectation->family == QA_GAME_FAMILY_Q2 &&
      strcmp(expectation->campaign, "ctf") == 0 && classic_or_rerelease) {
    out->is_enabled = true;
    out->mechanic = "q2-ctf";
    out->edition = expectation->edition;
    out->binding = "slot";
    out->source.provider = qa_equipment_provider_ids.ctf;
    out->source.content = product->id;
    return true;
  }
  if (expectation->family == QA_GAME_FAMILY_Q2 &&
      strcmp(expectation->campaign, "lmctf") == 0 &&
      strcmp(expectation->edition, "classic") == 0) {
    out->is_enabled = true;
    out->mechanic = "q2-lmctf";
    out->edition = expectation->edition;
    out->binding = "offhand";
    out->source.provider = qa_equipment_provider_ids.lmctf;
    out->source.content = product->id;
    return true;
  }
  return false;
}

typedef struct style_source {
  const qa_catalog_discovered_product *product;
  size_t index;
} style_source;

static const char *style_preferred_edition;
static int compare_style_sources(const void *left, const void *right) {
  const style_source *a = (const style_source *)left;
  const style_source *b = (const style_source *)right;
  int installed = (b->product->availability.kind == QA_CATALOG_INSTALLED) -
                  (a->product->availability.kind == QA_CATALOG_INSTALLED);
  if (installed != 0) return installed;
  int edition = (strcmp(b->product->expectation.edition,
                         style_preferred_edition) == 0) -
                (strcmp(a->product->expectation.edition,
                        style_preferred_edition) == 0);
  if (edition != 0) return edition;
  return a->index < b->index ? -1 : a->index > b->index ? 1 : 0;
}

qa_grapple_style *qa_grapple_styles(const qa_content_catalog *catalog,
                                    const qa_catalog_discovered_product *preferred,
                                    size_t *out_count) {
  *out_count = 0;
  style_source *sources = NULL;
  size_t source_count = 0, source_capacity = 0;
  for (size_t i = 0; i < catalog->product_count; i++) {
    const char *campaign = catalog->products[i].expectation.campaign;
    if (strcmp(campaign, "ctf") != 0 && strcmp(campaign, "lmctf") != 0) continue;
    if (source_count == source_capacity) {
      size_t next = source_capacity != 0 ? source_capacity * 2 : 8;
      style_source *grown = (style_source *)realloc(sources, next * sizeof *grown);
      if (grown == NULL) { free(sources); return NULL; }
      sources = grown; source_capacity = next;
    }
    sources[source_count].product = &catalog->products[i];
    sources[source_count].index = i;
    source_count++;
  }
  style_preferred_edition = preferred->expectation.edition;
  if (source_count > 1)
    qsort(sources, source_count, sizeof *sources, compare_style_sources);
  qa_grapple_style *styles = NULL;
  size_t count = 0, capacity = 0;
  for (size_t i = 0; i < source_count; i++) {
    qa_grapple_selection selection;
    if (!grapple_for_product(sources[i].product, &selection)) continue;
    if (strcmp(selection.mechanic, "q1-threewave") == 0 &&
        strcmp(selection.edition, "classic") != 0) continue;
    bool seen = false;
    for (size_t s = 0; s < count; s++)
      if (strcmp(styles[s].id, selection.mechanic) == 0) seen = true;
    if (seen) continue;
    if (count == capacity) {
      size_t next = capacity != 0 ? capacity * 2 : 4;
      qa_grapple_style *grown = (qa_grapple_style *)realloc(styles, next * sizeof *grown);
      if (grown == NULL) { free(sources); qa_grapple_styles_free(styles, count); return NULL; }
      styles = grown; capacity = next;
    }
    styles[count].id = selection.mechanic;
    styles[count].title = strcmp(selection.mechanic, "q1-threewave") == 0
                              ? "Threewave CTF (Quake 1)"
                              : strcmp(selection.mechanic, "q2-ctf") == 0
                                    ? "Threewave CTF (Quake 2)" : "LMCTF (Quake 2)";
    styles[count].selection = selection;
    styles[count].unavailable = equipment_unavailable(sources[i].product);
    count++;
  }
  free(sources);
  *out_count = count;
  return styles;
}

void qa_grapple_styles_free(qa_grapple_style *styles, size_t count) {
  for (size_t i = 0; i < count; i++) free(styles[i].unavailable);
  free(styles);
}

const qa_catalog_discovered_product *qa_offhand_grenade_source(
    const qa_content_catalog *catalog,
    const qa_catalog_discovered_product *preferred) {
  const qa_catalog_discovered_product *best = NULL;
  for (size_t i = 0; i < catalog->product_count; i++) {
    const qa_catalog_discovered_product *product = &catalog->products[i];
    if (product->expectation.family != QA_GAME_FAMILY_Q2 ||
        strcmp(product->expectation.campaign, "baseq2") != 0 ||
        product->availability.kind != QA_CATALOG_INSTALLED ||
        (strcmp(product->expectation.edition, "classic") != 0 &&
         strcmp(product->expectation.edition, "rerelease") != 0)) continue;
    if (best == NULL) { best = product; continue; }
    bool candidate_preferred =
        strcmp(product->expectation.edition, preferred->expectation.edition) == 0;
    bool best_preferred =
        strcmp(best->expectation.edition, preferred->expectation.edition) == 0;
    if (candidate_preferred && !best_preferred) best = product;
  }
  return best;
}

qa_equipment_selection qa_native_equipment(const qa_content_catalog *catalog,
                                           qa_provider_reference map,
                                           qa_provider_reference match) {
  const qa_catalog_discovered_product *product =
      qa_content_catalog_require(catalog, map.content);
  const qa_catalog_discovered_product *rules =
      qa_content_catalog_require(catalog, match.content);
  qa_equipment_selection selection = qa_disabled_equipment();
  qa_grapple_selection native = {0};
  if (grapple_for_product(product, &native) &&
      strcmp(native.mechanic, "q1-threewave") == 0) {
    selection.grapple = native;
    return selection;
  }
  const qa_catalog_discovered_product *source =
      strcmp(match.provider, "q2:ctf") == 0 ||
              strcmp(match.provider, "q2:lmctf") == 0
          ? rules : product;
  if (grapple_for_product(source, &native)) selection.grapple = native;
  return selection;
}

qa_provider_reference *qa_equipment_providers(qa_equipment_selection equipment,
                                              size_t *out_count) {
  *out_count = 0;
  size_t count = (size_t)equipment.grapple.is_enabled +
                 (size_t)equipment.hand_grenades.is_enabled;
  if (count == 0) return NULL;
  qa_provider_reference *values =
      (qa_provider_reference *)malloc(count * sizeof *values);
  if (values == NULL) return NULL;
  if (equipment.grapple.is_enabled) values[(*out_count)++] = equipment.grapple.source;
  if (equipment.hand_grenades.is_enabled)
    values[(*out_count)++] = equipment.hand_grenades.source;
  return values;
}

static void check_equipment_source(qa_provider_reference source,
                                   qa_provider_id provider,
                                   qa_game_family family, const char *campaign,
                                   const char *edition,
                                   const qa_content_catalog *catalog) {
  const qa_catalog_product_expectation *product =
      &qa_content_catalog_require(catalog, source.content)->expectation;
  if (strcmp(source.provider, provider) != 0 || product->family != family ||
      strcmp(product->campaign, campaign) != 0 ||
      strcmp(product->edition, edition) != 0) {
    qa_range_error("Equipment source %s/%s does not supply %s %s",
                   source.provider, source.content, provider, edition);
  }
}

void qa_validate_equipment(qa_equipment_selection equipment,
                           const qa_content_catalog *catalog) {
  qa_grapple_selection grapple = equipment.grapple;
  if (grapple.is_enabled) {
    if (strcmp(grapple.mechanic, "q1-threewave") == 0) {
      check_equipment_source(grapple.source, qa_equipment_provider_ids.threewave,
                             QA_GAME_FAMILY_Q1, "ctf", grapple.edition, catalog);
    } else if (strcmp(grapple.mechanic, "q2-ctf") == 0) {
      check_equipment_source(grapple.source, qa_equipment_provider_ids.ctf,
                             QA_GAME_FAMILY_Q2, "ctf", grapple.edition, catalog);
    } else if (strcmp(grapple.mechanic, "q2-lmctf") == 0) {
      check_equipment_source(grapple.source, qa_equipment_provider_ids.lmctf,
                             QA_GAME_FAMILY_Q2, "lmctf", grapple.edition, catalog);
    } else if (strcmp(grapple.mechanic, "q3-qvm") == 0) {
      const qa_catalog_product_expectation *source =
          &qa_content_catalog_require(catalog, grapple.source.content)->expectation;
      if (source->family != QA_GAME_FAMILY_Q3 || grapple.profile == NULL ||
          strcmp(grapple.source.provider, grapple.profile->module.id) != 0)
        qa_range_error("QVM hook source differs from its declared executable");
    } else {
      qa_range_error("Equipment source %s/%s does not supply %s %s",
                     grapple.source.provider, grapple.source.content,
                     grapple.source.provider, grapple.edition);
    }
  }
  qa_hand_grenade_selection grenades = equipment.hand_grenades;
  if (grenades.is_enabled) {
    check_equipment_source(grenades.source,
                           qa_equipment_provider_ids.hand_grenades,
                           QA_GAME_FAMILY_Q2, "baseq2", grenades.edition, catalog);
    bool valid = grenades.initial_ammo >= 0 && grenades.capacity >= 0 &&
                 grenades.capacity >= grenades.initial_ammo &&
                 grenades.initial_ammo == (double)(int64_t)grenades.initial_ammo &&
                 grenades.capacity == (double)(int64_t)grenades.capacity &&
                 grenades.initial_ammo <= 9007199254740991.0 &&
                 grenades.capacity <= 9007199254740991.0;
    if (!valid)
      qa_range_error(
          "Equipment hand grenade allowance must be whole ammunition within capacity");
  }
}

static qa_provider_timing equipment_timing_entry(qa_provider_reference source,
                                                 const char *edition, bool q1) {
  qa_provider_timing timing = {0};
  timing.provider = source.provider;
  timing.numeric = q1 ? qa_q1_donor_profile : qa_q2_donor_profile;
  if (q1) {
    timing.clock.kind = QA_CLOCK_Q1_NETQUAKE;
    timing.clock.minimum_frame_seconds = 0.001;
    timing.clock.maximum_frame_seconds = 0.1;
    timing.clock.has_fixed_frame_seconds = false;
  } else if (strcmp(edition, "classic") == 0) {
    timing.clock.kind = QA_CLOCK_Q2_CLASSIC;
    timing.clock.frame_milliseconds = 100;
  } else {
    timing.clock.kind = QA_CLOCK_Q2_RERELEASE;
    timing.clock.frame_milliseconds = 25;
  }
  return timing;
}

qa_provider_timing *qa_equipment_timing(qa_equipment_selection equipment,
                                        size_t *out_count) {
  *out_count = 0;
  size_t count = (size_t)equipment.grapple.is_enabled +
                 (size_t)equipment.hand_grenades.is_enabled;
  if (count == 0) return NULL;
  qa_provider_timing *values =
      (qa_provider_timing *)malloc(count * sizeof *values);
  if (values == NULL) return NULL;
  if (equipment.grapple.is_enabled) {
    if (strcmp(equipment.grapple.mechanic, "q3-qvm") == 0) {
      qa_provider_timing timing = {0};
      timing.provider = equipment.grapple.source.provider;
      timing.numeric = qa_q3_binary32_profile;
      timing.clock.kind = QA_CLOCK_Q3;
      timing.clock.server_frame_milliseconds = 50;
      timing.clock.has_fixed_movement_milliseconds = false;
      timing.clock.maximum_command_milliseconds = 200;
      values[(*out_count)++] = timing;
    } else {
      values[(*out_count)++] = equipment_timing_entry(
          equipment.grapple.source, equipment.grapple.edition,
          strcmp(equipment.grapple.mechanic, "q1-threewave") == 0);
    }
  }
  if (equipment.hand_grenades.is_enabled)
    values[(*out_count)++] = equipment_timing_entry(
        equipment.hand_grenades.source, equipment.hand_grenades.edition, false);
  return values;
}

void qa_catalog_resource_list_free(qa_catalog_resource_list *list) {
  free(list->values);
  for (size_t i = 0; i < list->owned_count; i++) free(list->owned_paths[i]);
  free(list->owned_paths);
  memset(list, 0, sizeof *list);
}

bool qa_catalog_resource_push(qa_catalog_resource_list *list,
                              qa_content_id content, const char *path);
bool qa_catalog_resource_push_owned(qa_catalog_resource_list *list,
                                    qa_content_id content, char *path);

bool qa_catalog_resource_push(qa_catalog_resource_list *list,
                              qa_content_id content, const char *path) {
  qa_resource_request *grown = (qa_resource_request *)realloc(
      list->values, (list->count + 1) * sizeof *grown);
  if (grown == NULL) return false;
  list->values = grown;
  list->values[list->count].content = content;
  list->values[list->count].path = path;
  list->count++;
  return true;
}

bool qa_catalog_resource_push_owned(qa_catalog_resource_list *list,
                                    qa_content_id content, char *path) {
  char **grown = (char **)realloc(list->owned_paths,
                                  (list->owned_count + 1) * sizeof *grown);
  if (grown == NULL) return false;
  list->owned_paths = grown;
  list->owned_paths[list->owned_count++] = path;
  return qa_catalog_resource_push(list, content, path);
}

static bool add_paths(qa_catalog_resource_list *list, qa_provider_reference source,
                      const char *const *paths, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (!qa_catalog_resource_push(list, source.content, paths[i])) return false;
  return true;
}

bool qa_equipment_resources(qa_equipment_selection equipment,
                            qa_catalog_resource_list *out) {
  memset(out, 0, sizeof *out);
  qa_grapple_selection grapple = equipment.grapple;
  qa_hand_grenade_selection grenades = equipment.hand_grenades;
  if (grapple.is_enabled) {
    if (strcmp(grapple.mechanic, "q1-threewave") == 0) {
      static const char *const base[] = {
          "progs/star.mdl", "sound/weapons/chain1.wav", "sound/blob/land1.wav",
          "sound/player/axhit2.wav"};
      static const char *const classic[] = {
          "progs/bit.mdl", "sound/weapons/chain2.wav", "sound/weapons/chain3.wav",
          "sound/weapons/bounce2.wav"};
      static const char *const rerelease[] = {"progs/beam.mdl"};
      if (!add_paths(out, grapple.source, base, 4)) goto oom;
      if (!add_paths(out, grapple.source,
                     strcmp(grapple.edition, "classic") == 0 ? classic : rerelease,
                     strcmp(grapple.edition, "classic") == 0 ? 4 : 1)) goto oom;
      if (strcmp(grapple.binding, "slot") == 0) {
        static const char *const slot[] = {"progs/v_star.mdl"};
        if (!add_paths(out, grapple.source, slot, 1)) goto oom;
      }
    } else if (strcmp(grapple.mechanic, "q2-ctf") == 0) {
      static const char *const base[] = {
          "models/weapons/grapple/hook/tris.md2",
          "models/weapons/grapple/hook/skin.pcx",
          "sound/weapons/grapple/grfire.wav", "sound/weapons/grapple/grpull.wav",
          "sound/weapons/grapple/grhit.wav", "sound/weapons/grapple/grhang.wav",
          "sound/weapons/grapple/grreset.wav"};
      static const char *const fly[] = {"sound/weapons/grapple/grfly.wav"};
      static const char *const slot[] = {"models/weapons/grapple/tris.md2",
                                         "models/weapons/grapple/skin.pcx"};
      if (!add_paths(out, grapple.source, base, 7)) goto oom;
      if (strcmp(grapple.edition, "rerelease") == 0 &&
          !add_paths(out, grapple.source, fly, 1)) goto oom;
      if (strcmp(grapple.binding, "slot") == 0 &&
          !add_paths(out, grapple.source, slot, 2)) goto oom;
    } else if (strcmp(grapple.mechanic, "q2-lmctf") == 0) {
      static const char *const base[] = {
          "models/objects/ghook/tris.md2", "models/objects/ghook/skin.pcx",
          "sound/weapons/grapple/grfire.wav", "sound/weapons/grapple/gflyair.wav",
          "sound/weapons/grapple/gpulling.wav",
          "sound/weapons/grapple/gkilling.wav", "sound/weapons/grapple/ghit.wav",
          "sound/weapons/grapple/ghitwall.wav"};
      static const char *const slot[] = {"models/weapons/v_hook/tris.md2",
                                         "models/weapons/v_hook/skin.pcx"};
      if (!add_paths(out, grapple.source, base, 8)) goto oom;
      if (strcmp(grapple.binding, "slot") == 0 &&
          !add_paths(out, grapple.source, slot, 2)) goto oom;
    } else if (strcmp(grapple.mechanic, "q3-qvm") == 0) {
      const qa_qvm_grapple_definition *profile = grapple.profile;
      if (profile == NULL) goto oom;
      const qa_qvm_grapple_presentation *presentation = &profile->presentation;
      if (!qa_catalog_resource_push(out, grapple.source.content,
                                    profile->module.artifact_path)) goto oom;
      if (!qa_catalog_resource_push(out, grapple.source.content,
                                    presentation->projectile_model)) goto oom;
      if (strcmp(grapple.binding, "slot") == 0) {
        if (!qa_catalog_resource_push(out, grapple.source.content,
                                      presentation->view_anchor.path)) goto oom;
        if (!qa_catalog_resource_push(out, grapple.source.content,
                                      presentation->view_model)) goto oom;
        for (size_t i = 0; i < presentation->view_attachment_count; i++)
          if (!qa_catalog_resource_push(out, grapple.source.content,
                                        presentation->view_attachments[i].path))
            goto oom;
      }
      if (!presentation->cable.is_shader) {
        const char *cable[] = {presentation->cable.flight, presentation->cable.pull,
                               presentation->cable.hold};
        if (!add_paths(out, grapple.source, cable, 3)) goto oom;
      }
      const char *sounds[] = {presentation->fire_sound, presentation->attach_sound,
                              presentation->release_sound, presentation->pull_sound,
                              presentation->hang_sound};
      for (size_t i = 0; i < 5; i++)
        if (sounds[i] != NULL &&
            !qa_catalog_resource_push(out, grapple.source.content, sounds[i]))
          goto oom;
    } else {
      goto oom; /* TS exhaustive never; unknown mechanic */
    }
  }
  if (grenades.is_enabled) {
    const char *model = strcmp(grenades.edition, "classic") == 0 ? "grenade2"
                                                                 : "grenade3";
    char *tris = qa_catalog_format("models/objects/%s/tris.md2", model);
    char *skin = qa_catalog_format("models/objects/%s/skin.pcx", model);
    if (tris == NULL || skin == NULL ||
        !qa_catalog_resource_push_owned(out, grenades.source.content, tris) ||
        !qa_catalog_resource_push_owned(out, grenades.source.content, skin)) {
      free(tris); free(skin); goto oom;
    }
    static const char *const sounds[] = {
        "sound/weapons/hgrena1b.wav", "sound/weapons/hgrenc1b.wav",
        "sound/weapons/hgrent1a.wav", "sound/weapons/hgrenb1a.wav",
        "sound/weapons/hgrenb2a.wav"};
    if (!add_paths(out, grenades.source, sounds, 5)) goto oom;
  }
  return true;
oom:
  qa_catalog_resource_list_free(out);
  return false;
}
