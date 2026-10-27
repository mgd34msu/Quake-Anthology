/* Port of src/compat/q2/classic/pickup-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/classic/pickup_profile.h"
#include <string.h>
#include "qa/compat/q2/classic/combat_profile.h"

static const qa_guest_value_layout p4[] = {{false,"pointer",{0}},{false,"pointer",{0}},{false,"pointer",{0}},{false,"pointer",{0}}};
static const qa_guest_value_layout p2[] = {{false,"pointer",{0}},{false,"pointer",{0}}};
static const qa_guest_value_layout p2i[] = {{false,"pointer",{0}},{false,"pointer",{0}},{false,"int32",{0}}};
#define CSIG(p, isvoid) {{"windows-i386","pe32",4,"cdecl"}, (p), sizeof(p)/sizeof((p)[0]), (isvoid), {false,"int32",{NULL,0,0,0,NULL,0}}, false}
static const qa_compat_q2_native_pickup_grant grants[] = {
  {0xa780, 0xa795, 0xa8cb, "regular", NULL, 0, false, NULL, 0, NULL, 0, 0, 0, 0},
  {0xa3e0, 0xa3e4, 0xa4b8, "inventory", NULL, 0, true, "ammo", 0xa41c, "rbx", 0, 0, 0, 0},
  {0x35ff0, 0x36064, 0x36077, "inventory", NULL, 0, true, "weapon", 0, NULL, 0x360dc, 0x360e5, 0x3614a, 0x3619b},
  {0x9960, 0x996b, 0x9a72, "inventory", NULL, 0, false, NULL, 0, NULL, 0, 0, 0, 0},
  {0x9ac0, 0x9acb, 0x9d98, "inventory", NULL, 0, false, NULL, 0, NULL, 0, 0, 0, 0},
};
static const double caps[] = {0x6e4, 0x6e8, 0x6ec, 0x6f0, 0x6f4, 0x6f8, 0x6fc, 0x700};
static const qa_compat_q2_native_pickup_profile xatrix = {
  "sha256:8187df3fd5b4d435d8227434d3351aad2b47e546236403e52adcd4d275810c45", 0xab00, 0xab38, 0xac90,
  CSIG(p4, true), CSIG(p2, false),
  grants, 5,
  0x4b828, 76, 48, 0, 4,
  0x288, 0x214, 0x11c, 88, 4, false, 0,
  0x76804, "float32-seconds",
  0x54, 0x2e4, 0x38, 1,
  0xa310, CSIG(p2i, false), false, 0, 0x44, caps, 8, 4
};
const qa_compat_q2_native_pickup_profile *qa_compat_q2_classic_get_pickup_profile(qa_content_digest digest) {
  (void)qa_compat_q2_xatrix_combat_profile;
  return digest && strcmp(digest, xatrix.digest) == 0 ? &xatrix : NULL;
}
