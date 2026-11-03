/* Port of src/content/q2/foundation/rerelease-ammo.ts: ammunition capacities
 * of the Quake II rerelease game DLL. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_RERELEASE_AMMO_H
#define QA_CONTENT_Q2_FOUNDATION_RERELEASE_AMMO_H
#include <stdbool.h>

/* TS Q2AmmoLimits: tag indexes client.pers.max_ammo[12]; capacity is what
 * InitClientPersistant stores there; bandolier and pack are the ceilings
 * Pickup_Bandolier and Pickup_Pack raise it to (has_* false is TS null). */
typedef struct qa_q2_ammo_limits {
  const char *classname;
  double tag, capacity;
  bool has_bandolier; double bandolier;
  bool has_pack; double pack;
} qa_q2_ammo_limits;
/* TS q2RereleaseAmmoLimits[classname]: NULL is TS undefined. */
const qa_q2_ammo_limits *qa_q2_rerelease_ammo_limits(const char *classname);
#endif
