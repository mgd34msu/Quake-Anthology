/* Port of src/content/catalog/weapon-hud.ts. GPL-2.0-or-later. */
#include "qa/content/catalog/weapon_hud.h"

#include "catalog_internal.h"
#include "q2_weapon_data.h"

typedef struct q1_picture {
  const char *item;
  const char *classic; /* nullable */
  const char *wheel;
  const char *ammo; /* nullable */
} q1_picture;

static const q1_picture Q1_PICTURES[] = {
    {"q1:weapon/axe", NULL, "axe", NULL},
    {"q1:weapon/shotgun", "shotgun", "shotgun1", "sb_shells"},
    {"q1:weapon/supershotgun", "sshotgun", "shotgun2", "sb_shells"},
    {"q1:weapon/nailgun", "nailgun", "nail1", "sb_nails"},
    {"q1:weapon/supernailgun", "snailgun", "nail2", "sb_nails"},
    {"q1:weapon/grenadelauncher", "rlaunch", "rocket1", "sb_rocket"},
    {"q1:weapon/rocketlauncher", "srlaunch", "rocket2", "sb_rocket"},
    {"q1:weapon/lightning", "lightng", "light", "sb_cells"}};
static const q1_picture HIPNOTIC_PICTURES[] = {
    {"q1:weapon/hipnotic:laser", "laser", "ui_h_weapon_laser", "sb_cells"},
    {"q1:weapon/hipnotic:mjolnir", "mjolnir", "ui_h_weapon_mjolnir", "sb_cells"},
    {"q1:weapon/hipnotic:proximity", "prox", "ui_h_weapon_gren", "sb_rocket"}};
static const q1_picture ROGUE_PICTURES[] = {
    {"q1:weapon/rogue:lava-nailgun", "r_lava", "ui_r_weapon_lava", "r_ammolava"},
    {"q1:weapon/rogue:lava-supernailgun", "r_superlava", "ui_r_weapon_superlava", "r_ammolava"},
    {"q1:weapon/rogue:multi-grenade", "r_gren", "ui_r_weapon_gren", "r_ammoplasma"},
    {"q1:weapon/rogue:multi-rocket", "r_multirock", "ui_r_weapon_multirock", "r_ammoplasma"},
    {"q1:weapon/rogue:plasma", "r_plasma", "ui_r_weapon_plasma", "r_ammomulti"}};
static const q1_picture MG3_PICTURES[] = {
    {"q1:weapon/mg3:laser", "laser", "ui_h_weapon_laser", "sb_cells"},
    {"q1:weapon/mg3:mjolnir", NULL, "ui_h_weapon_mjolnir", "sb_cells"}};

static const q1_picture *find_q1_picture(const char *campaign, qa_item_id item) {
  for (size_t i = 0; i < sizeof Q1_PICTURES / sizeof Q1_PICTURES[0]; i++)
    if (strcmp(Q1_PICTURES[i].item, item) == 0) return &Q1_PICTURES[i];
  const q1_picture *extra = NULL;
  size_t extra_count = 0;
  if (strcmp(campaign, "hipnotic") == 0) {
    extra = HIPNOTIC_PICTURES; extra_count = 3;
  } else if (strcmp(campaign, "rogue") == 0) {
    extra = ROGUE_PICTURES; extra_count = 5;
  } else if (strcmp(campaign, "mg3") == 0) {
    extra = MG3_PICTURES; extra_count = 2;
  }
  for (size_t i = 0; i < extra_count; i++)
    if (strcmp(extra[i].item, item) == 0) return &extra[i];
  return NULL;
}

static bool own_string(qa_weapon_hud_icons *icons, char *owned) {
  char **grown = (char **)realloc(icons->owned_paths,
                                  (icons->owned_count + 1) * sizeof *grown);
  if (grown == NULL) return false;
  icons->owned_paths = grown;
  icons->owned_paths[icons->owned_count++] = owned;
  return true;
}

void qa_weapon_hud_icons_free(qa_weapon_hud_icons *icons) {
  for (size_t i = 0; i < icons->owned_count; i++) free(icons->owned_paths[i]);
  free(icons->owned_paths);
  memset(icons, 0, sizeof *icons);
}

static qa_weapon_hud_icon image_icon(qa_provider_reference source, const char *path) {
  qa_weapon_hud_icon icon = {0};
  icon.kind = QA_WEAPON_HUD_ICON_IMAGE;
  icon.resource.content = source.content;
  icon.resource.path = path;
  return icon;
}
static qa_weapon_hud_icon wad_icon(qa_provider_reference source, const char *lump) {
  qa_weapon_hud_icon icon = {0};
  icon.kind = QA_WEAPON_HUD_ICON_WAD_PICTURE;
  icon.resource.content = source.content;
  icon.resource.path = "gfx.wad";
  icon.lump = lump;
  return icon;
}

static const qa_catalog_q2_weapon_definition *find_q2_weapon(
    const qa_catalog_product_expectation *product, qa_item_id item) {
  for (size_t i = 0; i < sizeof Q2_BASE_DATA / sizeof Q2_BASE_DATA[0]; i++)
    if (strcmp(Q2_BASE_DATA[i].item, item) == 0) return &Q2_BASE_DATA[i];
  if (strcmp(product->edition, "rerelease") == 0 ||
      strcmp(product->campaign, "xatrix") == 0)
    for (size_t i = 0; i < sizeof Q2_XATRIX_DATA / sizeof Q2_XATRIX_DATA[0]; i++)
      if (strcmp(Q2_XATRIX_DATA[i].item, item) == 0) return &Q2_XATRIX_DATA[i];
  if (strcmp(product->edition, "rerelease") == 0 ||
      strcmp(product->campaign, "rogue") == 0)
    for (size_t i = 0; i < sizeof Q2_ROGUE_DATA / sizeof Q2_ROGUE_DATA[0]; i++)
      if (strcmp(Q2_ROGUE_DATA[i].item, item) == 0) return &Q2_ROGUE_DATA[i];
  return NULL;
}

static const char *find_q2_icon(qa_item_id item) {
  for (size_t i = 0; i < sizeof Q2_BASE_ICONS / sizeof Q2_BASE_ICONS[0]; i++)
    if (strcmp(Q2_BASE_ICONS[i].item, item) == 0) return Q2_BASE_ICONS[i].icon;
  for (size_t i = 0; i < sizeof Q2_MISSION_ICONS / sizeof Q2_MISSION_ICONS[0]; i++)
    if (strcmp(Q2_MISSION_ICONS[i].item, item) == 0) return Q2_MISSION_ICONS[i].icon;
  return NULL;
}

bool qa_weapon_hud_icons_for(qa_provider_reference source,
                             const qa_catalog_product_expectation *product,
                             qa_item_id item, qa_weapon_hud_icons *out,
                             bool *out_found) {
  memset(out, 0, sizeof *out);
  *out_found = false;
  static const char *const q1_campaigns[] = {"id1", "hipnotic", "rogue",
                                             "dopa", "mg1", "mg3"};
  if (product->family == QA_GAME_FAMILY_Q1 &&
      qa_catalog_string_contains(q1_campaigns, 6, product->campaign) &&
      (strcmp(product->edition, "classic") == 0 ||
       strcmp(product->edition, "rerelease") == 0)) {
    const q1_picture *pictures = find_q1_picture(product->campaign, item);
    if (pictures == NULL) return true;
    bool image_icons = strcmp(product->edition, "rerelease") == 0 ||
                       qa_catalog_starts_with(item, "q1:weapon/mg3:");
    char *wheel = qa_catalog_starts_with(pictures->wheel, "ui_")
                      ? qa_catalog_dup(pictures->wheel)
                      : qa_catalog_format("ww_%s", pictures->wheel);
    if (wheel == NULL) return false;
    if (image_icons) {
      char *first = qa_catalog_format("gfx/weapons/%s_1.lmp", wheel);
      char *second = qa_catalog_format("gfx/weapons/%s_2.lmp", wheel);
      free(wheel);
      if (first == NULL || second == NULL ||
          !own_string(out, first) || !own_string(out, second)) {
        free(first); free(second);
        qa_weapon_hud_icons_free(out);
        return false;
      }
      out->weapon = image_icon(source, first);
      out->has_weapon = true;
      out->selected_weapon = image_icon(source, second);
      out->has_selected_weapon = true;
    } else {
      free(wheel);
      if (pictures->classic != NULL) {
        char *first = qa_catalog_starts_with(pictures->classic, "r_")
                          ? qa_catalog_dup(pictures->classic)
                          : qa_catalog_format("inv_%s", pictures->classic);
        char *second = qa_catalog_starts_with(pictures->classic, "r_")
                           ? qa_catalog_dup(pictures->classic)
                           : qa_catalog_format("inv2_%s", pictures->classic);
        if (first == NULL || second == NULL ||
            !own_string(out, first) || !own_string(out, second)) {
          free(first); free(second);
          qa_weapon_hud_icons_free(out);
          return false;
        }
        out->weapon = wad_icon(source, first);
        out->has_weapon = true;
        out->selected_weapon = wad_icon(source, second);
        out->has_selected_weapon = true;
      }
    }
    if (pictures->ammo != NULL) {
      out->ammo = wad_icon(source, pictures->ammo);
      out->has_ammo = true;
    }
    *out_found = true;
    return true;
  }
  static const char *const q2_campaigns[] = {"baseq2", "xatrix", "rogue", "mg2"};
  if (product->family == QA_GAME_FAMILY_Q2 &&
      qa_catalog_string_contains(q2_campaigns, 4, product->campaign) &&
      (strcmp(product->edition, "classic") == 0 ||
       strcmp(product->edition, "rerelease") == 0)) {
    const char *picture = find_q2_icon(item);
    const qa_catalog_q2_weapon_definition *weapon = find_q2_weapon(product, item);
    if (picture == NULL || weapon == NULL) return true;
    char *path = qa_catalog_format("pics/%s.pcx", picture);
    if (path == NULL || !own_string(out, path)) {
      free(path); qa_weapon_hud_icons_free(out); return false;
    }
    out->weapon = image_icon(source, path);
    out->has_weapon = true;
    out->selected_weapon = image_icon(source, path);
    out->has_selected_weapon = true;
    const char *ammo_icon = weapon->ammo != NULL ? find_q2_icon(weapon->ammo) : NULL;
    if (ammo_icon != NULL) {
      char *ammo_path = qa_catalog_format("pics/%s.pcx", ammo_icon);
      if (ammo_path == NULL || !own_string(out, ammo_path)) {
        free(ammo_path); qa_weapon_hud_icons_free(out); return false;
      }
      out->ammo = image_icon(source, ammo_path);
      out->has_ammo = true;
    }
    *out_found = true;
    return true;
  }
  if (product->family == QA_GAME_FAMILY_Q3 &&
      (strcmp(product->campaign, "baseq3") == 0 ||
       strcmp(product->campaign, "missionpack") == 0)) {
    const q3_weapon_icon_entry *definition = NULL;
    for (size_t i = 0; i < sizeof Q3_WEAPON_ICONS / sizeof Q3_WEAPON_ICONS[0]; i++)
      if (strcmp(Q3_WEAPON_ICONS[i].item, item) == 0) definition = &Q3_WEAPON_ICONS[i];
    if (definition == NULL) return true;
    if (definition->missionpack_only &&
        strcmp(product->campaign, "missionpack") != 0) return true;
    if (definition->weapon_icon != NULL) {
      qa_weapon_hud_icon icon = {0};
      icon.kind = QA_WEAPON_HUD_ICON_SHADER;
      icon.content = source.content;
      icon.name = definition->weapon_icon;
      out->weapon = icon;
      out->has_weapon = true;
      out->selected_weapon = icon;
      out->has_selected_weapon = true;
    }
    if (definition->ammo_icon != NULL) {
      qa_weapon_hud_icon icon = {0};
      icon.kind = QA_WEAPON_HUD_ICON_SHADER;
      icon.content = source.content;
      icon.name = definition->ammo_icon;
      out->ammo = icon;
      out->has_ammo = true;
    }
    *out_found = true;
    return true;
  }
  return true;
}

static bool path_seen(const qa_catalog_resource_list *list, const char *path) {
  for (size_t i = 0; i < list->count; i++)
    if (strcmp(list->values[i].path, path) == 0) return true;
  return false;
}

static bool add_icon_path(qa_catalog_resource_list *out,
                          qa_provider_reference source,
                          const qa_weapon_hud_icon *icon, bool present) {
  if (!present) return true;
  if (icon->kind == QA_WEAPON_HUD_ICON_SHADER) {
    static const struct { const char *name; const char *path; } team_arena[] = {
        {"icons/iconw_nailgun", "icons/nailgun128.tga"},
        {"icons/iconw_chaingun", "icons/chaingun128.tga"},
        {"icons/iconw_proxlauncher", "icons/proxmine.tga"},
        {"icons/icona_nailgun", "icons/ammo_nailgun.tga"},
        {"icons/icona_chaingun", "icons/ammo_chaingun.tga"},
        {"icons/icona_proxlauncher", "icons/ammo_proxmine.tga"}};
    const char *mapped = NULL;
    for (size_t i = 0; i < sizeof team_arena / sizeof team_arena[0]; i++)
      if (strcmp(team_arena[i].name, icon->name) == 0) mapped = team_arena[i].path;
    if (mapped != NULL) {
      return path_seen(out, mapped) ||
             qa_catalog_resource_push(out, source.content, mapped);
    }
    char *path = qa_catalog_format("%s.tga", icon->name);
    if (path == NULL) return false;
    if (path_seen(out, path)) { free(path); return true; }
    return qa_catalog_resource_push_owned(out, source.content, path);
  }
  return path_seen(out, icon->resource.path) ||
         qa_catalog_resource_push(out, source.content, icon->resource.path);
}

bool qa_weapon_hud_resources(qa_provider_reference source,
                             const qa_catalog_product_expectation *product,
                             qa_catalog_resource_list *out) {
  memset(out, 0, sizeof *out);
  const char *items[64];
  size_t item_count = 0;
  if (product->family == QA_GAME_FAMILY_Q1) {
    for (size_t i = 0; i < sizeof Q1_PICTURES / sizeof Q1_PICTURES[0]; i++)
      items[item_count++] = Q1_PICTURES[i].item;
    const q1_picture *extra = NULL;
    size_t extra_count = 0;
    if (strcmp(product->campaign, "hipnotic") == 0) { extra = HIPNOTIC_PICTURES; extra_count = 3; }
    else if (strcmp(product->campaign, "rogue") == 0) { extra = ROGUE_PICTURES; extra_count = 5; }
    else if (strcmp(product->campaign, "mg3") == 0) { extra = MG3_PICTURES; extra_count = 2; }
    for (size_t i = 0; i < extra_count; i++) items[item_count++] = extra[i].item;
  } else if (product->family == QA_GAME_FAMILY_Q2) {
    for (size_t i = 0; i < sizeof Q2_BASE_DATA / sizeof Q2_BASE_DATA[0]; i++)
      items[item_count++] = Q2_BASE_DATA[i].item;
    if (strcmp(product->edition, "rerelease") == 0 ||
        strcmp(product->campaign, "xatrix") == 0)
      for (size_t i = 0; i < sizeof Q2_XATRIX_DATA / sizeof Q2_XATRIX_DATA[0]; i++)
        items[item_count++] = Q2_XATRIX_DATA[i].item;
    if (strcmp(product->edition, "rerelease") == 0 ||
        strcmp(product->campaign, "rogue") == 0)
      for (size_t i = 0; i < sizeof Q2_ROGUE_DATA / sizeof Q2_ROGUE_DATA[0]; i++)
        items[item_count++] = Q2_ROGUE_DATA[i].item;
  } else {
    for (size_t i = 0; i < sizeof Q3_WEAPON_ICONS / sizeof Q3_WEAPON_ICONS[0]; i++)
      items[item_count++] = Q3_WEAPON_ICONS[i].item;
  }
  for (size_t i = 0; i < item_count; i++) {
    qa_weapon_hud_icons icons = {0};
    bool found = false;
    if (!qa_weapon_hud_icons_for(source, product, items[i], &icons, &found)) {
      qa_catalog_resource_list_free(out);
      return false;
    }
    if (found) {
      bool ok = add_icon_path(out, source, &icons.weapon, icons.has_weapon) &&
                add_icon_path(out, source, &icons.selected_weapon,
                              icons.has_selected_weapon) &&
                add_icon_path(out, source, &icons.ammo, icons.has_ammo);
      /* The requests borrow icon path strings; re-dup the image
       * paths into the output so freeing icons stays safe. */
      for (size_t r = 0; ok && r < out->count; r++) {
        bool borrowed = false;
        for (size_t o = 0; o < icons.owned_count; o++)
          if (out->values[r].path == icons.owned_paths[o]) borrowed = true;
        if (borrowed) {
          char *dup = qa_catalog_dup(out->values[r].path);
          if (dup == NULL) { ok = false; break; }
          char **grown = (char **)realloc(out->owned_paths,
                                          (out->owned_count + 1) * sizeof *grown);
          if (grown == NULL) { free(dup); ok = false; break; }
          out->owned_paths = grown;
          out->owned_paths[out->owned_count++] = dup;
          out->values[r].path = dup;
        }
      }
      qa_weapon_hud_icons_free(&icons);
      if (!ok) { qa_catalog_resource_list_free(out); return false; }
    } else {
      qa_weapon_hud_icons_free(&icons);
    }
  }
  if (out->count > 0 && product->family == QA_GAME_FAMILY_Q1) {
    if (!path_seen(out, "gfx/palette.lmp") &&
        !qa_catalog_resource_push(out, source.content, "gfx/palette.lmp"))
      goto fail;
    if (strcmp(product->edition, "rerelease") == 0 && !path_seen(out, "wwheel.txt") &&
        !qa_catalog_resource_push(out, source.content, "wwheel.txt")) goto fail;
  }
  if (out->count > 0 && product->family == QA_GAME_FAMILY_Q2 &&
      !path_seen(out, "pics/colormap.pcx") &&
      !qa_catalog_resource_push(out, source.content, "pics/colormap.pcx")) goto fail;
  if (out->count > 0 && product->family == QA_GAME_FAMILY_Q3 &&
      strcmp(product->campaign, "missionpack") == 0 &&
      !path_seen(out, "scripts/gfx.shader") &&
      !qa_catalog_resource_push(out, source.content, "scripts/gfx.shader")) goto fail;
  return true;
fail:
  qa_catalog_resource_list_free(out);
  return false;
}
