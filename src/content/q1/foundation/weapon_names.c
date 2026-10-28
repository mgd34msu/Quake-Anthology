/* Port of src/content/q1/foundation/weapon-names.ts. GPL-2.0-or-later. */
#include "qa/content/q1/foundation/weapon_names.h"

#include <ctype.h>
#include <string.h>

static const char *const DISPLAY_NAMES[8] = {
    "Axe", "Shotgun", "Double-barrelled Shotgun", "Nailgun",
    "Super Nailgun", "Grenade Launcher", "Rocket Launcher", "Thunderbolt"};

const char *qa_q1_weapon_display_name(qa_q1_weapon weapon, char *out, size_t out_size) {
  if (qa_q1_is_base_weapon(weapon)) {
    for (size_t i = 0; i < 8; i++)
      if (!strcmp(weapon, QA_Q1_WEAPONS[i])) return DISPLAY_NAMES[i];
  }
  if (!strcmp(weapon, "hipnotic:laser")) return "Laser Cannon";
  if (!strcmp(weapon, "hipnotic:mjolnir")) return "Mjolnir";
  if (!strcmp(weapon, "hipnotic:proximity")) return "Proximity Gun";
  /* TS: weapon.split(":").at(-1), "_" / "-" -> " ", then /\b\w/g upper. */
  const char *name = strrchr(weapon, ':');
  name = name ? name + 1 : weapon;
  if (out == NULL || out_size == 0) return name;
  size_t n = 0;
  bool boundary = true;
  for (size_t i = 0; name[i] != '\0' && n + 1 < out_size; i++) {
    char c = name[i] == '_' || name[i] == '-' ? ' ' : name[i];
    const bool word = isalnum((unsigned char)c) != 0 || c == '_';
    if (boundary && word) c = (char)toupper((unsigned char)c);
    boundary = !word;
    out[n++] = c;
  }
  out[n] = '\0';
  return out;
}
