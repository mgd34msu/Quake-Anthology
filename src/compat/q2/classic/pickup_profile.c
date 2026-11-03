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
/* 1998 retail Xatrix DLL (0b0d119e): Touch_Item 0xa3a0 (pickup call returns at 0xa3d8, G_UseTargets at 0xa4ce), Add_Ammo 0x9b60,
 * 72-byte items. The regular grant is Pickup_Armor (0xa020), whose region starts at the ArmorIndex call and joins the respawn tail. */
static const qa_compat_q2_native_pickup_grant grants1998[] = {
  {0xa020, 0xa035, 0xa16b, "regular", NULL, 0, false, NULL, 0, NULL, 0, 0, 0, 0},
  {0x9c30, 0x9c34, 0x9d08, "inventory", NULL, 0, true, "ammo", 0x9c6c, "rbx", 0, 0, 0, 0},
  {0x34c10, 0x34c84, 0x34c97, "inventory", NULL, 0, true, "weapon", 0, NULL, 0x34ce1, 0x34cea, 0x34d4f, 0x34da1},
  {0x91b0, 0x91bb, 0x92c2, "inventory", NULL, 0, false, NULL, 0, NULL, 0, 0, 0, 0},
  {0x9310, 0x931b, 0x95e8, "inventory", NULL, 0, false, NULL, 0, NULL, 0, 0, 0, 0},
};
static const qa_compat_q2_native_pickup_profile xatrix1998 = {
  "sha256:0b0d119ea9ed1abbce02f32fe1243bcb69457d10a34a49b775c473210bcb65a3", 0xa3a0, 0xa3d8, 0xa4ce,
  CSIG(p4, true), CSIG(p2, false),
  grants1998, 5,
  0x497c8, 72, 48, 0, 4,
  0x288, 0x214, 0x11c, 88, 4, false, 0,
  0x73fe4, "float32-seconds",
  0x54, 0x2e4, 0x38, 1,
  0x9b60, CSIG(p2i, false), false, 0, 0x40, caps, 8, 4
};
const qa_compat_q2_native_pickup_profile *qa_compat_q2_classic_get_pickup_profile(qa_content_digest digest) {
  (void)qa_compat_q2_xatrix_combat_profile;
  if (digest && strcmp(digest, xatrix.digest) == 0) return &xatrix;
  if (digest && strcmp(digest, qa_compat_q2_xatrix1998_combat_profile.digest) == 0) return &xatrix1998;
  return NULL;
}
