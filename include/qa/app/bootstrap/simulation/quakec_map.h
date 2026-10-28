/* Port of src/app/bootstrap/simulation/quakec-map.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_MAP_H
#define QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_MAP_H
#include "qa/contracts/scene.h"
/* Malloc'd entity text; caller frees. mode: "singleplayer"|"coop"|"deathmatch". */
char *qa_app_sim_quakec_map_entities(qa_decoded_world world, const char *mode);
#endif
