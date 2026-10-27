/* Port of src/content/q1/addons/horde/squads.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/horde/squads.h"

#include <string.h>

typedef struct {
  qa_q1ad_squad_spawn *out;
  size_t capacity, count;
} builder;
static void spawn(builder *b, const char *monster, double x, double y, double z) {
  if (b->count < b->capacity) b->out[b->count] = (qa_q1ad_squad_spawn){monster, {x, y, z}};
  b->count++;
}

size_t qa_q1ad_horde_squad(const char *name, double skill, qa_q1ad_random_fn random, void *random_ctx,
                           qa_q1ad_squad_spawn *out, size_t capacity) {
  builder b = {out, capacity, 0};
  if (strcmp(name, "3 grunts") == 0) {
    if (skill > 0) { spawn(&b, "grunt", 0, -40, 0); spawn(&b, "grunt", 40, 40, 0); spawn(&b, "grunt", -40, 40, 0); }
    else { spawn(&b, "grunt", -40, 0, 0); spawn(&b, "grunt", 40, 0, 0); }
  } else if (strcmp(name, "2 grunts, 1 dog") == 0) {
    spawn(&b, "dog", 44, 0, 0); spawn(&b, "grunt", -40, -40, 0); spawn(&b, "grunt", -40, 40, 0);
  } else if (strcmp(name, "2 dogs") == 0) {
    spawn(&b, "dog", 0, -44, 0);
    if (skill > 0) spawn(&b, "dog", 0, 44, 0);
  } else if (strcmp(name, "1 enforcer") == 0) spawn(&b, "enforcer", 0, 0, 0);
  else if (strcmp(name, "2 enforcers") == 0) {
    if (skill > 0) { spawn(&b, "enforcer", 40, 0, 0); spawn(&b, "enforcer", -40, 0, 0); }
    else spawn(&b, "enforcer", 0, 0, 0);
  } else if (strcmp(name, "1 ogre") == 0) spawn(&b, "ogre", 0, 0, 0);
  else if (strcmp(name, "2 knights") == 0) {
    spawn(&b, "knight", 40, 0, 0);
    if (skill > 0) spawn(&b, "knight", -40, 0, 0);
  } else if (strcmp(name, "2 zombies") == 0) {
    spawn(&b, "zombie", 40, 0, 0); spawn(&b, "zombie", -40, 0, 0);
  } else if (strcmp(name, "1 wizard") == 0) spawn(&b, "wizard", 0, 0, 0);
  else if (strcmp(name, "2 hellknights") == 0) {
    spawn(&b, "hellknight", 0, 40, 0);
    if (skill > 0) spawn(&b, "hellknight", 0, -40, 0);
  } else if (strcmp(name, "2 knights, 1 hellknight") == 0) {
    spawn(&b, "hellknight", 40, 0, 0); spawn(&b, "knight", -40, 40, 0); spawn(&b, "knight", -40, -40, 0);
  } else if (strcmp(name, "3 wizards") == 0) {
    spawn(&b, "wizard", 40, 40, 40); spawn(&b, "wizard", -40, 40, 40);
    if (skill > 0) spawn(&b, "wizard", -40, -40, 40);
  } else if (strcmp(name, "shambler") == 0) spawn(&b, "shambler", 0, 0, 0);
  else if (strcmp(name, "double demon") == 0) {
    if (skill >= 3 && random(random_ctx) > 0.8) { spawn(&b, "shambler", 40, 40, 0); spawn(&b, "shambler", -40, -40, 0); }
    else if (skill >= 1) { spawn(&b, "demon", 40, 40, 0); spawn(&b, "demon", -40, -40, 0); }
    else spawn(&b, "demon", 0, 0, 0);
  } else if (strcmp(name, "shalrath") == 0) spawn(&b, "shalrath", 0, 0, 0);
  return b.count;
}

bool qa_q1ad_choose_horde_squad(bool army, const char *category, qa_q1ad_random_fn random, void *random_ctx,
                                const char **out_squad, const char **out_type) {
  if (army) {
    if (strcmp(category, "bosses") == 0) return false;
    if (strcmp(category, "elites") == 0) {
      *out_squad = random(random_ctx) * 2 < 1.5 ? "2 enforcers" : "1 ogre";
      *out_type = "ranged";
      return true;
    }
    double roll = random(random_ctx) * 4;
    *out_squad = roll < 1 ? "3 grunts" : roll < 2 ? "2 grunts, 1 dog" : roll < 3.5 ? "2 dogs" : "1 enforcer";
    *out_type = roll < 3.5 ? "normal" : "ranged";
    return true;
  }
  if (strcmp(category, "fodder") == 0) {
    double roll = random(random_ctx) * 4;
    *out_squad = roll < 2 ? "2 knights" : roll < 3 ? "2 zombies" : "1 wizard";
    *out_type = roll < 3 ? "normal" : "flying";
    return true;
  }
  if (strcmp(category, "elites") == 0) {
    double roll = random(random_ctx) * 4;
    *out_squad = roll < 1 ? "2 hellknights" : roll < 2 ? "2 knights, 1 hellknight" : roll < 3 ? "1 ogre" : "3 wizards";
    *out_type = roll < 2 ? "normal" : roll < 3 ? "ranged" : "flying";
    return true;
  }
  double roll = random(random_ctx) * 3;
  *out_squad = roll < 1 ? "shambler" : roll < 2.5 ? "double demon" : "shalrath";
  *out_type = roll >= 1 && roll < 2.5 ? "normal" : "boss";
  return true;
}
