/* Port of src/content/q2/missionpacks/damage.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/damage.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <string.h>

static const double friendly_fire = 0x8000000;

static bool safe_integer(double v) { return v == trunc(v) && fabs(v) <= 9007199254740991.0; }
static bool is_game(const char *game, const char *name) { return game != NULL && strcmp(game, name) == 0; }
static bool valid_classic(const char *game, double id) {
  return id <= 33 || (is_game(game, "xatrix") && id <= 39) || (is_game(game, "rogue") && id >= 40 && id <= 55) ||
         (is_game(game, "ctf") && id == 34);
}

bool qa_q2mp_canonical_cause_from_native(qa_q2_native_cause native, double *out) {
  if (is_game(native.edition, "classic")) {
    if (!safe_integer(native.value) || native.value < 0 || native.value > friendly_fire + 55) return false;
    int32_t bits = qa_js_to_int32(native.value); /* validated 0..2^27+58: ToInt32 is the value */
    bool friendly = (bits & qa_js_to_int32(friendly_fire)) != 0;
    double id = (double)(bits & ~qa_js_to_int32(friendly_fire));
    if (!valid_classic(native.game, id)) return false;
    *out = (is_game(native.game, "ctf") && id == 34 ? QA_Q2MP_CAUSE_GRAPPLE : id) + (friendly ? friendly_fire : 0);
    return true;
  }
  if (!(native.id == trunc(native.id)) || native.id < 0 || native.id > 58) return false;
  double id = native.id < 22 ? native.id : native.id == 22 ? QA_Q2MP_CAUSE_TELEFRAG_SPAWN
              : native.id <= 56 ? native.id - 1
              : native.id == 57 ? QA_Q2MP_CAUSE_GRAPPLE : QA_Q2MP_CAUSE_BLUE_BLASTER;
  *out = id + (native.friendly_fire ? friendly_fire : 0);
  return true;
}

bool qa_q2mp_native_cause_from_canonical(qa_q2mp_native_cause_profile profile, double canonical,
                                         qa_q2_native_cause *out) {
  if (!safe_integer(canonical) || canonical < 0 || canonical > friendly_fire + 58) return false;
  int32_t bits = qa_js_to_int32(canonical); /* validated 0..2^27+58: ToInt32 is the value */
  bool friendly = (bits & qa_js_to_int32(friendly_fire)) != 0;
  double id = (double)(bits & ~qa_js_to_int32(friendly_fire));
  memset(out, 0, sizeof *out);
  if (!profile.rerelease) {
    double raw = is_game(profile.game, "ctf") && id == QA_Q2MP_CAUSE_GRAPPLE ? 34 : id;
    if ((is_game(profile.game, "ctf") && id == 34) || !valid_classic(profile.game, raw)) return false;
    out->edition = "classic";
    out->game = profile.game;
    out->value = raw + (friendly ? friendly_fire : 0);
    return true;
  }
  if (id > 58) return false;
  double raw = id < 22 ? id : id <= 55 ? id + 1 : id == QA_Q2MP_CAUSE_GRAPPLE ? 57
               : id == QA_Q2MP_CAUSE_TELEFRAG_SPAWN ? 22 : 58;
  out->edition = "rerelease";
  out->id = raw;
  out->friendly_fire = friendly;
  out->no_point_loss = profile.no_point_loss;
  return true;
}
