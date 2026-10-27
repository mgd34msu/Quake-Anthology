/* Port of src/compat/q2/native-primary.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary.h"
#include <string.h>
#include "qa/compat/q2/native_primary_command_profile.h"
#include "qa/compat/q2/native_primary_drop_profile.h"
#include "qa/compat/q2/native_primary_inventory_profile.h"
#include "qa/compat/q2/native_primary_player_profile.h"
#include "qa/compat/q2/native_primary_weapon_profile.h"
bool qa_compat_q2_builtin_native_primary(qa_content_digest digest, const char *edition, qa_compat_q2_primary_world_providers prov, qa_compat_q2_native_primary_profile *out) {
  const qa_compat_q2_native_pickup_profile *pk=NULL;
  bool classic = edition && strcmp(edition,"classic")==0;
  if (classic) { if(prov.pickups_classic) prov.pickups_classic(prov.ctx,digest,&pk); } else if(prov.pickups_rerelease) prov.pickups_rerelease(prov.ctx,digest,&pk);
  out->weapons=qa_compat_q2_get_native_primary_weapon_profile(digest);
  out->player=qa_compat_q2_get_native_primary_player_profile(digest);
  out->commands=qa_compat_q2_get_native_primary_command_profile(digest);
  out->inventory=qa_compat_q2_get_native_primary_inventory_profile(digest);
  out->drop=qa_compat_q2_get_native_primary_drop_profile(digest);
  out->pickups=pk; out->edition=edition;
  if(!out->weapons||!out->player||!out->commands||!out->inventory||!out->drop||!out->pickups) return false;
  out->world = classic ? (prov.world_classic?prov.world_classic(prov.ctx,digest):NULL) : (prov.world_rerelease?prov.world_rerelease(prov.ctx,digest):NULL);
  return out->world!=NULL;
}
