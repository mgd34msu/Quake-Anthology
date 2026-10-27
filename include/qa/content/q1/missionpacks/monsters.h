/*
 * Umbrella for the port of src/content/q1/missionpacks/monsters/index.ts.
 * TS Q1MissionPack (owner src/content/q1/missionpacks/types.ts) is the
 * pack string ("hipnotic" | "rogue"). register returns the runtime via
 * the caller-owned *out struct (false only on OOM). GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_H

#include "qa/content/q1/missionpacks/monsters/armagon.h"
#include "qa/content/q1/missionpacks/monsters/charmed_base.h"
#include "qa/content/q1/missionpacks/monsters/charm.h"
#include "qa/content/q1/missionpacks/monsters/decoy.h"
#include "qa/content/q1/missionpacks/monsters/dragon.h"
#include "qa/content/q1/missionpacks/monsters/eel.h"
#include "qa/content/q1/missionpacks/monsters/gremlin.h"
#include "qa/content/q1/missionpacks/monsters/gremlin_ai.h"
#include "qa/content/q1/missionpacks/monsters/gremlin_weapons.h"
#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/lavaman.h"
#include "qa/content/q1/missionpacks/monsters/morph.h"
#include "qa/content/q1/missionpacks/monsters/mummy.h"
#include "qa/content/q1/missionpacks/monsters/overlord.h"
#include "qa/content/q1/missionpacks/monsters/paths.h"
#include "qa/content/q1/missionpacks/monsters/runtime.h"
#include "qa/content/q1/missionpacks/monsters/scourge.h"
#include "qa/content/q1/missionpacks/monsters/spikemine.h"
#include "qa/content/q1/missionpacks/monsters/sword.h"
#include "qa/content/q1/missionpacks/monsters/types.h"
#include "qa/content/q1/missionpacks/monsters/wrath.h"

/* TS MissionMonsterHooks default {}: hooks may be NULL. */
bool qa_q1mpm_register_mission_pack_monsters(qa_q1mpm_game *game, qa_q1mpm_q1base *base,
                                             const char *pack,
                                             const qa_q1mpm_mission_monster_hooks *hooks,
                                             qa_q1mpm_runtime *out);

#endif
