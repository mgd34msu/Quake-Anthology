/* Port of src/content/q1/foundation/held-weapons.ts. GPL-2.0-or-later. */
#include "qa/content/q1/foundation/held_weapons.h"

#include <string.h>

/* player.qc stand1/axstnd1 use one carried firearm and one axe for the
 * complete Q1 arsenal. TS shares one digests array between both parts. */
static const char *const DIGESTS[2] = {
    "sha256:10cecfe08d312ff17c63529e280b976c50018f683f541cb03b2048c7a681ebf9",
    "sha256:7bd9988aa264d27cea670bbf280d9101d712cf27d87dbe774ac41d363bdf01d2"};
static const double GUN_VERTICES[] = {40, 41, 42, 100, 101, 102, 116, 117, 118, 142, 143, 144,
                                      145, 146, 147, 148, 156, 157, 158, 174, 175, 177, 178, 179,
                                      185, 186, 187, 188, 189, 190, 191, 194, 195, 196, 199, 200, 201, 202};
static const double AXE_VERTICES[] = {9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
static const qa_held_weapon_part GUN_PART = {{DIGESTS, 2},
                                             {GUN_VERTICES, sizeof GUN_VERTICES / sizeof *GUN_VERTICES}};
static const qa_held_weapon_part AXE_PART = {{DIGESTS, 2},
                                             {AXE_VERTICES, sizeof AXE_VERTICES / sizeof *AXE_VERTICES}};
static const qa_held_weapon_model GUN = {
    false, NULL, "progs/player.mdl", 12,
    {{3.192499796549479, -5.947635650634766, 10.002536137898764},
     {{{0.7190015912055969, 0.6357764005661011, -0.280758261680603},
       {-0.608428418636322, 0.7710461020469666, 0.18789082765579224},
       {0.3359340727329254, 0.03572748228907585, 0.941207766532898}}},
     {1, 1, 1}},
    NULL, &GUN_PART};
static const qa_held_weapon_model AXE = {
    false, NULL, "progs/player.mdl", 17,
    {{-1.0801829099655151, -16.939233779907227, 3.4784603118896484},
     {{{0.8529600501060486, 0.34326276183128357, 0.3932298421859741},
       {-0.49903303384780884, 0.31537508964538574, 0.8071582913398743},
       {0.15305252373218536, -0.8847085237503052, 0.4403018355369568}}},
     {1, 1, 1}},
    NULL, &AXE_PART};

const qa_held_weapon_model *qa_q1_held_weapon(const char *view_model) {
  const size_t length = strlen(view_model);
  if (strncmp(view_model, "progs/v_", 8) != 0 || length < 4 ||
      strcmp(view_model + length - 4, ".mdl") != 0)
    return NULL;
  return strcmp(view_model, "progs/v_axe.mdl") == 0 ? &AXE : &GUN;
}
