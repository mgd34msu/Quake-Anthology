/* Port of src/content/q2/foundation/infinite-ammo.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_INFINITE_AMMO_H
#define QA_CONTENT_Q2_FOUNDATION_INFINITE_AMMO_H
#include <stdbool.h>
#include "qa/content/q2/foundation/host.h"

/* TS noInfiniteAmmo: IF_NO_INFINITE_AMMO, rerelease trap and tesla ammo is
 * spent even under g_infinite_ammo. */
bool qa_q2_no_infinite_ammo(const qa_q2_game_options *options, qa_item_id ammo);
#endif
