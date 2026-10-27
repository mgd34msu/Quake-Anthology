/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/navigation_types.h"
static const int64_t TYPE_FLAGS[20] = {
    QA_TFL_INVALID,    QA_TFL_INVALID,  QA_TFL_WALK,         QA_TFL_CROUCH,  QA_TFL_BARRIERJUMP,
    QA_TFL_JUMP,       QA_TFL_LADDER,   QA_TFL_WALKOFFLEDGE, QA_TFL_SWIM,    QA_TFL_WATERJUMP,
    QA_TFL_TELEPORT,   QA_TFL_ELEVATOR, QA_TFL_ROCKETJUMP,   QA_TFL_BFGJUMP, QA_TFL_GRAPPLEHOOK,
    QA_TFL_DOUBLEJUMP, QA_TFL_RAMPJUMP, QA_TFL_STRAFEJUMP,   QA_TFL_JUMPPAD, QA_TFL_FUNCBOB};
int64_t qa_travel_flag_for_type(int64_t t) {
  int64_t i = t & QA_TRAVEL_MASK;
  return (i >= 0 && i < 20) ? TYPE_FLAGS[i] : QA_TFL_INVALID;
}
