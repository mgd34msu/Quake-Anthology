/*
 * Port of src/settings/server/rotation.ts: the Q2 map-rotation
 * collection plus rotationMapName/moveRotationMap. Returned
 * collections point at static definitions; do not free.
 * qa_rotation_map_name returns a malloc'd normalized name and aborts
 * with the TS Error on invalid input. qa_move_rotation_map returns a
 * malloc'd array of the borrowed map pointers with the swap applied,
 * or NULL when the TS returns the input array unchanged (bad index
 * or destination). The 127 length gate counts bytes; the TS counts
 * UTF-16 units.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_ROTATION_H
#define QA_SETTINGS_SERVER_ROTATION_H

#include <stddef.h>

#include "qa/settings/server/types.h"

qa_server_setting_collection qa_q2_rotation_settings(bool rerelease);
/* Malloc'd; NULL only on OOM. */
char *qa_rotation_map_name(const char *value);
/* Malloc'd pointer array (free the array only), or NULL unchanged. */
const char **qa_move_rotation_map(const char *const *maps, size_t count,
                                  double index, int direction);

#endif
