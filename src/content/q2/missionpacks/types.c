/* Port of src/content/q2/missionpacks/types.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/types.h"

#include <string.h>

#include "missionpacks_internal.h"

const char *qa_q2mp_pack_name(qa_q2mp_pack pack) { return pack == QA_Q2MP_PACK_XATRIX ? "xatrix" : "rogue"; }
const char *qa_q2mp_player_effect_kind_name(qa_q2mp_player_effect_kind kind) {
  switch (kind) {
    case QA_Q2MP_EFFECT_TRACKER_PAIN: return "tracker-pain";
    case QA_Q2MP_EFFECT_NUKE_BLIND: return "nuke-blind";
    case QA_Q2MP_EFFECT_IR: return "ir";
    case QA_Q2MP_EFFECT_SPHERE_CAMERA: return "sphere-camera";
  }
  return "tracker-pain";
}
