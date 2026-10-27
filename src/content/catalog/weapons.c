/* Port of src/content/catalog/weapons.ts. Mirrored data in
 * q2_weapon_data.h. The Q1 base list is precacheQ1World filtered
 * exactly as q1BaseWeaponPaths filters it (weapons/ sounds plus
 * player/axhit*, v_* models plus missile/grenade/spike/s_spike/
 * bolt2). GPL-2.0-or-later. */
#include "qa/content/catalog/weapons.h"

#include "catalog_internal.h"
#include "q2_weapon_data.h"
#include "qa/content/catalog/timing.h"
#include "qa/content/catalog/weapon_hud.h"

const qa_classic_rerelease_providers qa_q1_weapon_providers = {
    "q1:weapons/classic/id1", "q1:weapons/rerelease/id1"};
const qa_classic_rerelease_providers qa_q1_hipnotic_weapon_providers = {
    "q1:weapons/classic/hipnotic", "q1:weapons/rerelease/hipnotic"};
const qa_classic_rerelease_providers qa_q2_weapon_providers = {
    "q2:weapons/classic/baseq2", "q2:weapons/rerelease/baseq2"};

const qa_catalog_q2_weapon_definition *const qa_q2_base_weapons_catalog =
    Q2_BASE_DATA;
const size_t qa_q2_base_weapon_catalog_count =
    sizeof Q2_BASE_DATA / sizeof Q2_BASE_DATA[0];
const qa_catalog_q2_weapon_definition *const qa_xatrix_weapon_definitions_catalog =
    Q2_XATRIX_DATA;
const size_t qa_xatrix_weapon_definition_catalog_count =
    sizeof Q2_XATRIX_DATA / sizeof Q2_XATRIX_DATA[0];
const qa_catalog_q2_weapon_definition *const qa_rogue_weapon_definitions_catalog =
    Q2_ROGUE_DATA;
const size_t qa_rogue_weapon_definition_catalog_count =
    sizeof Q2_ROGUE_DATA / sizeof Q2_ROGUE_DATA[0];

static bool in_list(const char *text, const char *const *values, size_t count) {
  return qa_catalog_string_contains(values, count, text);
}

bool qa_supports_selected_weapon_product(
    const qa_catalog_product_expectation *product) {
  if (product->family == QA_GAME_FAMILY_Q3)
    return strcmp(product->campaign, "baseq3") == 0 ||
           strcmp(product->campaign, "missionpack") == 0;
  if (strcmp(product->edition, "classic") != 0 &&
      strcmp(product->edition, "rerelease") != 0) return false;
  if (product->family == QA_GAME_FAMILY_Q1) {
    static const char *const base[] = {"id1", "hipnotic", "rogue"};
    static const char *const rerelease_extra[] = {"dopa", "mg1", "mg3"};
    return in_list(product->campaign, base, 3) ||
           (strcmp(product->edition, "rerelease") == 0 &&
            in_list(product->campaign, rerelease_extra, 3));
  }
  if (product->family == QA_GAME_FAMILY_Q2) {
    static const char *const base[] = {"baseq2", "xatrix", "rogue"};
    return in_list(product->campaign, base, 3) ||
           (strcmp(product->edition, "rerelease") == 0 &&
            strcmp(product->campaign, "mg2") == 0);
  }
  return false;
}

static bool is_equipment_provider(qa_provider_id provider) {
  return strcmp(provider, qa_equipment_provider_ids.threewave) == 0 ||
         strcmp(provider, qa_equipment_provider_ids.ctf) == 0 ||
         strcmp(provider, qa_equipment_provider_ids.lmctf) == 0 ||
         strcmp(provider, qa_equipment_provider_ids.hand_grenades) == 0;
}

qa_provider_reference qa_canonical_weapon_source(
    qa_provider_reference map, qa_provider_reference weapon,
    const qa_content_catalog *catalog, char **owned_provider) {
  *owned_provider = NULL;
  if (is_equipment_provider(weapon.provider)) return weapon;
  if (qa_catalog_starts_with(weapon.provider, "q3:")) {
    const qa_catalog_product_expectation *product =
        &qa_content_catalog_require(catalog, weapon.content)->expectation;
    if (product->family != QA_GAME_FAMILY_Q3 ||
        !qa_supports_selected_weapon_product(product)) return weapon;
    char *provider = qa_catalog_format("q3:weapons/%s/%s", product->edition,
                                       product->campaign);
    if (provider == NULL) return weapon;
    if (strcmp(weapon.provider, "q3:official") != 0 &&
        strcmp(weapon.provider, provider) != 0) {
      free(provider);
      qa_catalog_error("Selected Q3 weapon role does not match %s", weapon.content);
    }
    if (strcmp(weapon.content, map.content) == 0 &&
        strcmp(map.provider, "q3:official") == 0) { free(provider); return map; }
    if (strcmp(product->campaign, "baseq3") == 0 &&
        !qa_catalog_starts_with(map.provider, "q3:") &&
        strcmp(weapon.provider, "q3:official") == 0) { free(provider); return weapon; }
    *owned_provider = provider;
    qa_provider_reference result = {provider, weapon.content};
    return result;
  }
  const char *family_name = qa_catalog_starts_with(weapon.provider, "q1:") ? "q1"
                            : qa_catalog_starts_with(weapon.provider, "q2:") ? "q2"
                                                                             : NULL;
  if (family_name == NULL) return weapon;
  qa_game_family family = family_name[1] == '1' ? QA_GAME_FAMILY_Q1 : QA_GAME_FAMILY_Q2;
  const char *family_upper = family == QA_GAME_FAMILY_Q1 ? "Q1" : "Q2";
  char *weapons_prefix = qa_catalog_format("%s:weapons/", family_name);
  char *official = qa_catalog_format("%s:official", family_name);
  bool role = weapons_prefix != NULL &&
              qa_catalog_starts_with(weapon.provider, weapons_prefix);
  if (!role && (official == NULL || strcmp(weapon.provider, official) != 0)) {
    qa_provider_id bad = weapon.provider;
    free(weapons_prefix); free(official);
    qa_catalog_error("Unsupported %s weapon provider %s", family_upper, bad);
  }
  const qa_catalog_product_expectation *product =
      &qa_content_catalog_require(catalog, weapon.content)->expectation;
  bool supported = product->family == family &&
                   qa_supports_selected_weapon_product(product);
  if (!supported) {
    if (role || strcmp(weapon.content, map.content) != 0 ||
        product->family != family) {
      free(weapons_prefix); free(official);
      qa_catalog_error("Selected %s arsenal has no source adapter: %s",
                       family_upper, weapon.content);
    }
    free(weapons_prefix); free(official);
    return weapon;
  }
  char *provider = qa_catalog_format("%s:weapons/%s/%s", family_name,
                                     product->edition, product->campaign);
  free(weapons_prefix);
  if (provider == NULL) { free(official); return weapon; }
  if (role && strcmp(weapon.provider, provider) != 0) {
    free(provider); free(official);
    qa_catalog_error("Selected weapon role %s does not match %s",
                     weapon.provider, weapon.content);
  }
  if (strcmp(weapon.content, map.content) == 0 &&
      official != NULL && strcmp(map.provider, official) == 0) {
    free(provider); free(official);
    return map;
  }
  free(official);
  *owned_provider = provider;
  qa_provider_reference result = {provider, weapon.content};
  return result;
}

void qa_catalog_path_list_free(qa_catalog_path_list *list) {
  for (size_t i = 0; i < list->count; i++) free(list->values[i]);
  free(list->values);
  list->values = NULL; list->count = 0;
}

static bool path_push_unique(qa_catalog_string_list *list, char *owned) {
  if (owned == NULL) return false;
  if (owned[0] == '\0') { free(owned); return true; }
  if (qa_catalog_string_contains((const char *const *)list->values,
                                 list->count, owned)) { free(owned); return true; }
  return qa_catalog_string_push(list, owned);
}
static bool path_push(qa_catalog_string_list *list, const char *path) {
  return path_push_unique(list, qa_catalog_dup(path));
}

/* q2BaseWeaponPaths. */
static bool q2_base_paths(bool rerelease, qa_catalog_string_list *out) {
  qa_catalog_string_list models = {0};
  for (size_t i = 0; i < sizeof Q2_BASE_DATA / sizeof Q2_BASE_DATA[0]; i++) {
    if (Q2_BASE_DATA[i].view_model[0] != '\0' &&
        !qa_catalog_string_push(&models, qa_catalog_dup(Q2_BASE_DATA[i].view_model)))
      goto fail;
    if (Q2_BASE_DATA[i].world_model[0] != '\0' &&
        !qa_catalog_string_push(&models, qa_catalog_dup(Q2_BASE_DATA[i].world_model)))
      goto fail;
  }
  {
    const char *objects[] = {"laser", "rocket", "debris2", "smoke", "explode",
                             "r_explode", rerelease ? "grenade4" : "grenade",
                             rerelease ? "grenade3" : "grenade2"};
    for (size_t i = 0; i < 8; i++) {
      char *path = qa_catalog_format("models/objects/%s/tris.md2", objects[i]);
      if (path == NULL || !qa_catalog_string_push(&models, path)) { free(path); goto fail; }
    }
  }
  for (size_t i = 0; i < models.count; i++) {
    if (!path_push(out, models.values[i])) goto fail;
    if (strcmp(models.values[i], "models/objects/r_explode/tris.md2") == 0) {
      for (int frame = 1; frame <= 7; frame++) {
        char *skin = qa_catalog_format("models/objects/r_explode/skin%d.pcx", frame);
        if (!path_push_unique(out, skin)) { qa_catalog_string_list_free(&models); return false; }
      }
    } else {
      char *skin = qa_catalog_dup(models.values[i]);
      char *tris = skin != NULL ? strstr(skin, "tris.md2") : NULL;
      if (tris != NULL) memcpy(tris, "skin.pcx", 8);
      if (!path_push_unique(out, skin)) { qa_catalog_string_list_free(&models); return false; }
    }
  }
  qa_catalog_string_list_free(&models);
  if (rerelease) {
    static const char *const extra[] = {
        "models/objects/explode/rskin.pcx", "models/objects/explode/skin2.pcx",
        "models/objects/laser/skinb.pcx", "models/objects/laser/sking.pcx"};
    for (size_t i = 0; i < 4; i++) if (!path_push(out, extra[i])) return false;
  }
  if (!path_push(out, "pics/colormap.pcx")) return false;
  if (!path_push(out, "sound/misc/lasfly.wav")) return false;
  {
    static const char *const sounds[] = {
        "blastf1a", "shotgf1b", "shotgr1b", "sshotf1b", "machgf1b", "machgf2b",
        "machgf3b", "machgf4b", "machgf5b", "chngnu1a", "chngnl1a", "chngnd1a",
        "hgrent1a", "hgrena1b", "hgrenc1b", "hgrenb1a", "hgrenb2a", "grenlf1a",
        "grenlr1b", "grenlb1b", "rockfly", "rocklf1a", "rocklr1b", "hyprbu1a",
        "hyprbl1a", "hyprbf1a", "hyprbd1a", "rg_hum", "railgf1a", "bfg__f1y",
        "bfg__l1a", "bfg__x1b", "bfg_hum", "noammo"};
    for (size_t i = 0; i < sizeof sounds / sizeof sounds[0]; i++) {
      char *path = qa_catalog_format("sound/weapons/%s.wav", sounds[i]);
      if (!path_push_unique(out, path)) return false;
    }
  }
  {
    static const int counts[] = {2, 4, 6};
    for (int index = 0; index < 3; index++) {
      char *sprite = qa_catalog_format("sprites/s_bfg%d.sp2", index + 1);
      if (!path_push_unique(out, sprite)) return false;
      for (int frame = 0; frame < counts[index]; frame++) {
        char *path = qa_catalog_format("sprites/s_bfg%d_%d.pcx", index + 1, frame);
        if (!path_push_unique(out, path)) return false;
      }
    }
  }
  if (rerelease) {
    if (!path_push(out, "sound/weapons/change.wav")) return false;
    if (!path_push(out, "sound/weapons/lowammo.wav")) return false;
  }
  return true;
fail:
  qa_catalog_string_list_free(&models);
  return false;
}

typedef struct expansion_paths {
  const char *name;
  const char *const *paths;
  size_t count;
} expansion_paths;
static const char *const EXP_TRAP[] = {
    "models/weapons/z_trap/tris.md2", "models/objects/trapfx/tris.md2",
    "models/objects/gibs/chest/tris.md2", "models/objects/gibs/sm_meat/tris.md2",
    "sound/misc/fhit3.wav", "sound/weapons/trapcock.wav",
    "sound/weapons/traploop.wav", "sound/weapons/trapsuck.wav",
    "sound/weapons/trapdown.wav", "sound/items/s_health.wav"};
static const char *const EXP_IONRIPPER[] = {
    "models/objects/boomrang/tris.md2", "sound/weapons/rg_hum.wav",
    "sound/weapons/rippfire.wav", "sound/misc/lasfly.wav"};
static const char *const EXP_PHALANX[] = {
    "sprites/s_photon.sp2", "sound/weapons/plasshot.wav",
    "sound/weapons/rockfly.wav"};
static const char *const EXP_TESLA[] = {
    "models/weapons/g_tesla/tris.md2", "sound/weapons/teslaopen.wav",
    "sound/weapons/hgrenb1a.wav", "sound/weapons/hgrenb2a.wav"};
static const char *const EXP_PROX[] = {
    "models/weapons/g_prox/tris.md2", "sound/weapons/grenlf1a.wav",
    "sound/weapons/grenlr1b.wav", "sound/weapons/grenlb1b.wav",
    "sound/weapons/proxwarn.wav", "sound/weapons/proxopen.wav"};
static const char *const EXP_CHAINFIST[] = {
    "sound/weapons/sawidle.wav", "sound/weapons/sawhit.wav",
    "sound/weapons/sawslice.wav"};
static const char *const EXP_DISINTEGRATOR[] = {
    "models/proj/disintegrator/tris.md2", "sound/weapons/disrupt.wav",
    "sound/weapons/disint2.wav", "sound/weapons/disrupthit.wav"};
static const char *const EXP_ETF[] = {
    "models/proj/flechette/tris.md2", "sound/weapons/nail1.wav"};
static const char *const EXP_HEATBEAM[] = {"sound/weapons/bfg__l1a.wav"};
static const expansion_paths EXPANSIONS[] = {
    {"trap", EXP_TRAP, 10}, {"ionripper", EXP_IONRIPPER, 4},
    {"phalanx", EXP_PHALANX, 3}, {"tesla", EXP_TESLA, 4},
    {"proxlauncher", EXP_PROX, 6}, {"chainfist", EXP_CHAINFIST, 3},
    {"disintegrator", EXP_DISINTEGRATOR, 4}, {"etf_rifle", EXP_ETF, 2},
    {"heatbeam", EXP_HEATBEAM, 1}};

bool qa_q2_registered_weapon_resources(
    const qa_catalog_q2_weapon_definition *definitions, size_t count,
    bool rerelease, qa_catalog_path_list *out) {
  memset(out, 0, sizeof *out);
  qa_catalog_string_list list = {0};
  if (!q2_base_paths(rerelease, &list)) { qa_catalog_string_list_free(&list); return false; }
  bool has_prox = false;
  for (size_t i = 0; i < count; i++)
    if (strcmp(definitions[i].name, "proxlauncher") == 0) has_prox = true;
  if (!rerelease && has_prox) {
    if (!path_push(&list, "models/weapons/v_plaunch/proxskin.pcx")) goto fail;
  }
  for (size_t i = 0; i < count; i++) {
    const qa_catalog_q2_weapon_definition *weapon = &definitions[i];
    if (!path_push(&list, weapon->view_model)) goto fail;
    if (!path_push(&list, weapon->world_model)) goto fail;
    for (size_t e = 0; e < sizeof EXPANSIONS / sizeof EXPANSIONS[0]; e++) {
      if (strcmp(EXPANSIONS[e].name, weapon->name) != 0) continue;
      for (size_t p = 0; p < EXPANSIONS[e].count; p++) {
        if (!rerelease && strcmp(EXPANSIONS[e].paths[p],
                                 "sound/weapons/sawslice.wav") == 0) continue;
        if (!path_push(&list, EXPANSIONS[e].paths[p])) goto fail;
      }
    }
    if (strcmp(weapon->name, "tesla") == 0 && !rerelease &&
        !path_push(&list, "models/weapons/v_tesla2/tris.md2")) goto fail;
    if (strcmp(weapon->name, "heatbeam") == 0 && !rerelease &&
        !path_push(&list, "models/weapons/v_beamer2/tris.md2")) goto fail;
  }
  if (rerelease && !path_push(&list, "sound/weapons/railgr1b.wav")) goto fail;
  out->values = list.values;
  out->count = list.count;
  return true;
fail:
  qa_catalog_string_list_free(&list);
  return false;
}

/* q1BaseWeaponPaths (precacheQ1World filtered). */
static const char *const Q1_BASE_PATHS[] = {
    "gfx/palette.lmp",
    "sound/weapons/r_exp3.wav", "sound/weapons/rocket1i.wav",
    "sound/weapons/sgun1.wav", "sound/weapons/guncock.wav",
    "sound/weapons/ric1.wav", "sound/weapons/ric2.wav",
    "sound/weapons/ric3.wav", "sound/weapons/spike2.wav",
    "sound/weapons/tink1.wav", "sound/weapons/grenade.wav",
    "sound/weapons/bounce.wav", "sound/weapons/shotgn2.wav",
    "sound/weapons/lock4.wav", "sound/weapons/pkup.wav",
    "sound/weapons/lhit.wav", "sound/weapons/lstart.wav",
    "sound/weapons/ax1.wav", "sound/player/axhit1.wav",
    "sound/player/axhit2.wav",
    "progs/v_axe.mdl", "progs/v_shot.mdl", "progs/v_nail.mdl",
    "progs/v_rock.mdl", "progs/v_shot2.mdl", "progs/v_nail2.mdl",
    "progs/v_rock2.mdl", "progs/bolt2.mdl", "progs/missile.mdl",
    "progs/grenade.mdl", "progs/spike.mdl", "progs/s_spike.mdl",
    "progs/v_light.mdl"};
static const char *const Q1_HIPNOTIC_PATHS[] = {
    "progs/v_laserg.mdl", "progs/g_laserg.mdl", "progs/v_hammer.mdl",
    "progs/g_hammer.mdl", "progs/v_prox.mdl", "progs/g_prox.mdl",
    "progs/lasrspik.mdl", "progs/proxbomb.mdl",
    "sound/hipweap/laserg.wav", "sound/hipweap/laserric.wav",
    "sound/enforcer/enfstop.wav", "sound/hipweap/proxbomb.wav",
    "sound/hipweap/proxwarn.wav", "sound/hipweap/mjoltink.wav",
    "sound/knight/sword1.wav", "sound/hipweap/mjolslap.wav",
    "sound/hipweap/mjolhit.wav"};
static const char *const Q1_ROGUE_PATHS[] = {
    "progs/v_lava.mdl", "progs/g_nail.mdl", "progs/v_lava2.mdl",
    "progs/g_nail2.mdl", "progs/v_multi.mdl", "progs/g_rock.mdl",
    "progs/v_multi2.mdl", "progs/g_rock2.mdl", "progs/v_plasma.mdl",
    "progs/g_light.mdl", "progs/hook.mdl", "progs/v_grpple.mdl",
    "sound/weapons/chain1.wav", "sound/pendulum/hit.wav",
    "progs/lspike.mdl", "progs/mervup.mdl", "progs/rockup.mdl",
    "progs/rockup_d.mdl", "progs/plasma.mdl", "sound/plasma/flight.wav",
    "sound/plasma/fire.wav", "sound/plasma/explode.wav",
    "sound/weapons/spike2.wav", "sound/weapons/lhit.wav"};
static const char *const Q1_MG3_EXTRA[] = {
    "progs/lasrspik.mdl", "progs/v_laserg.mdl", "progs/v_hammer.mdl",
    "progs/v_hammer_glow.mdl", "progs/v_bloodshot.mdl",
    "progs/v_bloodshot2.mdl"};

static bool push_paths(qa_catalog_resource_list *out, qa_content_id content,
                       const char *const *paths, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (!qa_catalog_resource_push(out, content, paths[i])) return false;
  return true;
}

static bool weapon_resources_one(qa_provider_reference map,
                                 qa_provider_reference reference,
                                 const qa_content_catalog *catalog,
                                 qa_catalog_resource_list *out) {
  char *owned = NULL;
  qa_provider_reference weapon = qa_canonical_weapon_source(map, reference, catalog, &owned);
  bool ok = true;
  if (!is_equipment_provider(weapon.provider)) {
    const qa_catalog_product_expectation *product =
        &qa_content_catalog_require(catalog, weapon.content)->expectation;
    static const char *const q2_campaigns[] = {"baseq2", "xatrix", "rogue", "mg2"};
    if (qa_catalog_starts_with(weapon.provider, "q2:") &&
        product->family == QA_GAME_FAMILY_Q2 &&
        in_list(product->campaign, q2_campaigns, 4) &&
        (strcmp(product->edition, "classic") == 0 ||
         strcmp(product->edition, "rerelease") == 0)) {
      qa_catalog_q2_weapon_definition definitions[32];
      size_t count = 0;
      for (size_t i = 0; i < sizeof Q2_BASE_DATA / sizeof Q2_BASE_DATA[0]; i++)
        definitions[count++] = Q2_BASE_DATA[i];
      if (strcmp(product->edition, "rerelease") == 0 ||
          strcmp(product->campaign, "xatrix") == 0)
        for (size_t i = 0; i < sizeof Q2_XATRIX_DATA / sizeof Q2_XATRIX_DATA[0]; i++)
          definitions[count++] = Q2_XATRIX_DATA[i];
      if (strcmp(product->edition, "rerelease") == 0 ||
          strcmp(product->campaign, "rogue") == 0)
        for (size_t i = 0; i < sizeof Q2_ROGUE_DATA / sizeof Q2_ROGUE_DATA[0]; i++)
          definitions[count++] = Q2_ROGUE_DATA[i];
      qa_catalog_path_list paths = {0};
      ok = qa_q2_registered_weapon_resources(
          definitions, count, strcmp(product->edition, "rerelease") == 0, &paths);
      for (size_t i = 0; ok && i < paths.count; i++) {
        char *path = paths.values[i];
        paths.values[i] = NULL;
        ok = qa_catalog_resource_push_owned(out, weapon.content, path);
        if (!ok) free(path);
      }
      qa_catalog_path_list_free(&paths);
      if (ok) {
        qa_catalog_resource_list hud = {0};
        ok = qa_weapon_hud_resources(weapon, product, &hud);
        for (size_t i = 0; ok && i < hud.count; i++)
          ok = qa_catalog_resource_push(out, hud.values[i].content, hud.values[i].path);
        for (size_t i = 0; i < hud.owned_count; i++) {
          char **grown = (char **)realloc(out->owned_paths,
                                          (out->owned_count + 1) * sizeof *grown);
          if (grown == NULL) { ok = false; break; }
          out->owned_paths = grown;
          out->owned_paths[out->owned_count++] = hud.owned_paths[i];
          hud.owned_paths[i] = NULL;
        }
        qa_catalog_resource_list_free(&hud);
      }
    } else if (qa_catalog_starts_with(weapon.provider, "q3:") &&
               product->family == QA_GAME_FAMILY_Q3) {
      qa_catalog_resource_list hud = {0};
      ok = qa_weapon_hud_resources(weapon, product, &hud);
      for (size_t i = 0; ok && i < hud.count; i++)
        ok = qa_catalog_resource_push(out, hud.values[i].content, hud.values[i].path);
      for (size_t i = 0; i < hud.owned_count; i++) {
        char **grown = (char **)realloc(out->owned_paths,
                                        (out->owned_count + 1) * sizeof *grown);
        if (grown == NULL) { ok = false; break; }
        out->owned_paths = grown;
        out->owned_paths[out->owned_count++] = hud.owned_paths[i];
        hud.owned_paths[i] = NULL;
      }
      qa_catalog_resource_list_free(&hud);
    } else {
      static const char *const q1_campaigns[] = {"id1", "hipnotic", "rogue",
                                                 "dopa", "mg1", "mg3"};
      if (qa_catalog_starts_with(weapon.provider, "q1:") &&
          product->family == QA_GAME_FAMILY_Q1 &&
          in_list(product->campaign, q1_campaigns, 6) &&
          (strcmp(product->edition, "classic") == 0 ||
           strcmp(product->edition, "rerelease") == 0)) {
        ok = push_paths(out, weapon.content, Q1_BASE_PATHS,
                        sizeof Q1_BASE_PATHS / sizeof Q1_BASE_PATHS[0]);
        if (ok && strcmp(product->campaign, "hipnotic") == 0)
          ok = push_paths(out, weapon.content, Q1_HIPNOTIC_PATHS,
                          sizeof Q1_HIPNOTIC_PATHS / sizeof Q1_HIPNOTIC_PATHS[0]);
        if (ok && strcmp(product->campaign, "rogue") == 0)
          ok = push_paths(out, weapon.content, Q1_ROGUE_PATHS,
                          sizeof Q1_ROGUE_PATHS / sizeof Q1_ROGUE_PATHS[0]);
        if (ok && strcmp(product->campaign, "mg3") == 0) {
          ok = push_paths(out, weapon.content, Q1_MG3_EXTRA,
                          sizeof Q1_MG3_EXTRA / sizeof Q1_MG3_EXTRA[0]);
          for (size_t i = 0; ok && i < sizeof Q1_HIPNOTIC_PATHS / sizeof Q1_HIPNOTIC_PATHS[0]; i++) {
            if (qa_catalog_starts_with(Q1_HIPNOTIC_PATHS[i], "sound/") &&
                strstr(Q1_HIPNOTIC_PATHS[i], "prox") == NULL)
              ok = qa_catalog_resource_push(out, weapon.content, Q1_HIPNOTIC_PATHS[i]);
          }
        }
        if (ok) {
          qa_catalog_resource_list hud = {0};
          ok = qa_weapon_hud_resources(weapon, product, &hud);
          for (size_t i = 0; ok && i < hud.count; i++)
            ok = qa_catalog_resource_push(out, hud.values[i].content, hud.values[i].path);
          for (size_t i = 0; i < hud.owned_count; i++) {
            char **grown = (char **)realloc(out->owned_paths,
                                            (out->owned_count + 1) * sizeof *grown);
            if (grown == NULL) { ok = false; break; }
            out->owned_paths = grown;
            out->owned_paths[out->owned_count++] = hud.owned_paths[i];
            hud.owned_paths[i] = NULL;
          }
          qa_catalog_resource_list_free(&hud);
        }
      }
    }
  }
  free(owned);
  return ok;
}

bool qa_weapon_resources(qa_provider_reference map,
                         const qa_provider_reference *weapons,
                         size_t weapon_count,
                         const qa_content_catalog *catalog,
                         qa_catalog_resource_list *out) {
  memset(out, 0, sizeof *out);
  for (size_t i = 0; i < weapon_count; i++) {
    if (!weapon_resources_one(map, weapons[i], catalog, out)) {
      qa_catalog_resource_list_free(out);
      return false;
    }
  }
  return true;
}

bool qa_selected_weapon_resources(qa_provider_reference map,
                                  const qa_provider_reference *weapons,
                                  size_t weapon_count,
                                  const qa_content_catalog *catalog,
                                  qa_catalog_resource_list *out) {
  qa_catalog_resource_list all = {0};
  if (!qa_weapon_resources(map, weapons, weapon_count, catalog, &all)) return false;
  memset(out, 0, sizeof *out);
  for (size_t i = 0; i < all.count; i++) {
    if (strcmp(all.values[i].content, map.content) == 0) continue;
    if (!qa_catalog_resource_push(out, all.values[i].content, all.values[i].path)) {
      qa_catalog_resource_list_free(&all);
      qa_catalog_resource_list_free(out);
      return false;
    }
  }
  out->owned_paths = all.owned_paths;
  out->owned_count = all.owned_count;
  all.owned_paths = NULL; all.owned_count = 0;
  qa_catalog_resource_list_free(&all);
  return true;
}

bool qa_catalog_timing_push(qa_catalog_timing_list *list,
                            qa_provider_timing timing) {
  if (list->count == list->capacity) {
    size_t next = list->capacity != 0 ? list->capacity * 2 : 8;
    qa_provider_timing *grown =
        (qa_provider_timing *)realloc(list->values, next * sizeof *grown);
    if (grown == NULL) return false;
    list->values = grown; list->capacity = next;
  }
  list->values[list->count++] = timing;
  return true;
}

void qa_catalog_timing_list_free(qa_catalog_timing_list *list) {
  free(list->values);
  memset(list, 0, sizeof *list);
}

qa_provider_timing *qa_selected_weapon_timing(
    qa_provider_reference map, const qa_provider_reference *weapons,
    size_t weapon_count, const qa_content_catalog *catalog, size_t *out_count) {
  *out_count = 0;
  qa_game_family map_family =
      qa_content_catalog_require(catalog, map.content)->expectation.family;
  qa_catalog_timing_list list = {0};
  /* provider -> content map (parallel arrays). */
  qa_catalog_string_list providers = {0};
  qa_catalog_string_list contents = {0};
  for (size_t i = 0; i < weapon_count; i++) {
    char *owned = NULL;
    qa_provider_reference weapon =
        qa_canonical_weapon_source(map, weapons[i], catalog, &owned);
    qa_provider_reference weapon_copy = weapon;
    char *provider_copy = qa_catalog_dup(weapon_copy.provider);
    free(owned);
    weapon_copy.provider = provider_copy;
    weapon = weapon_copy;
    bool skip = is_equipment_provider(weapon.provider) ||
                strcmp(weapon.content, map.content) == 0;
    const qa_catalog_product_expectation *product = NULL;
    if (!skip) {
      product = &qa_content_catalog_require(catalog, weapon.content)->expectation;
      char *prefix = qa_catalog_format("%s:", qa_game_family_name(product->family));
      bool supported = qa_supports_selected_weapon_product(product) &&
                       prefix != NULL &&
                       qa_catalog_starts_with(weapon.provider, prefix);
      free(prefix);
      if (!supported) skip = true;
    }
    if (!skip) {
      const char *prior = NULL;
      for (size_t p = 0; p < providers.count; p++)
        if (strcmp(providers.values[p], weapon.provider) == 0)
          prior = contents.values[p];
      if (strcmp(weapon.provider, map.provider) == 0 ||
          (prior != NULL && strcmp(prior, weapon.content) != 0)) {
        qa_catalog_error("Selected weapon provider %s has conflicting content",
                         weapon.provider);
      }
      char *weapons_prefix = qa_catalog_format(
          "%s:weapons/", qa_game_family_name(product->family));
      bool same_family_non_weapon =
          map_family == product->family && weapons_prefix != NULL &&
          !qa_catalog_starts_with(weapon.provider, weapons_prefix);
      free(weapons_prefix);
      if (same_family_non_weapon || prior != NULL) {
        free((void *)weapon.provider);
        continue;
      }
      (void)qa_catalog_string_push(&providers, qa_catalog_dup(weapon.provider));
      (void)qa_catalog_string_push(&contents, qa_catalog_dup(weapon.content));
      qa_provider_timing timing = {0};
      /* The timing entry owns its own provider dup (freed by
       * qa_selected_weapon_timing_free). */
      qa_provider_reference reference = {qa_catalog_dup(weapon.provider),
                                         weapon.content};
      bool pushed = qa_native_provider_timing(
                        reference, product->family,
                        strcmp(product->edition, "rerelease") == 0, &timing) &&
                    qa_catalog_timing_push(&list, timing);
      if (!pushed) {
        free((void *)reference.provider);
        free((void *)timing.numeric.id);
        free((void *)weapon.provider);
        qa_catalog_string_list_free(&providers);
        qa_catalog_string_list_free(&contents);
        for (size_t t = 0; t < list.count; t++) {
          free((void *)list.values[t].provider);
          free((void *)list.values[t].numeric.id);
        }
        qa_catalog_timing_list_free(&list);
        return NULL;
      }
    }
    free((void *)weapon.provider);
  }
  qa_catalog_string_list_free(&providers);
  qa_catalog_string_list_free(&contents);
  *out_count = list.count;
  return list.values;
}

void qa_selected_weapon_timing_free(qa_provider_timing *values, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free((void *)values[i].numeric.id);
    free((void *)values[i].provider);
  }
  free(values);
}

void qa_admit_weapon_timing(qa_catalog_timing_list *timing,
                            qa_provider_timing weapon) {
  qa_provider_timing *existing = NULL;
  for (size_t i = 0; i < timing->count; i++)
    if (strcmp(timing->values[i].provider, weapon.provider) == 0)
      existing = &timing->values[i];
  if (existing == NULL) {
    (void)qa_catalog_timing_push(timing, weapon);
    return;
  }
  const qa_numeric_profile *actual = &existing->numeric;
  const qa_numeric_profile *expected = &weapon.numeric;
  bool numeric_matches =
      ((actual->id == NULL && expected->id == NULL) ||
       (actual->id != NULL && expected->id != NULL &&
        strcmp(actual->id, expected->id) == 0)) &&
      actual->arithmetic.kind == QA_ARITHMETIC_BINARY32 &&
      expected->arithmetic.kind == QA_ARITHMETIC_BINARY32 &&
      actual->float_to_int == expected->float_to_int;
  const qa_clock_profile *clock = &existing->clock;
  const qa_clock_profile *wanted = &weapon.clock;
  bool clock_matches = false;
  if (wanted->kind == QA_CLOCK_Q1_NETQUAKE && clock->kind == QA_CLOCK_Q1_NETQUAKE)
    clock_matches = clock->minimum_frame_seconds == wanted->minimum_frame_seconds &&
                    clock->maximum_frame_seconds == wanted->maximum_frame_seconds &&
                    clock->has_fixed_frame_seconds == wanted->has_fixed_frame_seconds &&
                    (!clock->has_fixed_frame_seconds ||
                     clock->fixed_frame_seconds == wanted->fixed_frame_seconds);
  else if (wanted->kind == QA_CLOCK_Q2_CLASSIC && clock->kind == QA_CLOCK_Q2_CLASSIC)
    clock_matches = clock->frame_milliseconds == wanted->frame_milliseconds;
  else if (wanted->kind == QA_CLOCK_Q2_RERELEASE && clock->kind == QA_CLOCK_Q2_RERELEASE)
    clock_matches = clock->frame_milliseconds == wanted->frame_milliseconds;
  else if (wanted->kind == QA_CLOCK_Q3 && clock->kind == QA_CLOCK_Q3)
    clock_matches = clock->server_frame_milliseconds == wanted->server_frame_milliseconds &&
                    clock->has_fixed_movement_milliseconds ==
                        wanted->has_fixed_movement_milliseconds &&
                    (!clock->has_fixed_movement_milliseconds ||
                     clock->fixed_movement_milliseconds ==
                         wanted->fixed_movement_milliseconds) &&
                    clock->maximum_command_milliseconds ==
                        wanted->maximum_command_milliseconds;
  if (!numeric_matches || !clock_matches)
    qa_catalog_error("Selected weapon provider %s has conflicting timing",
                     weapon.provider);
}
