/* Port of src/compat/q2/rerelease/client-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/client_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static qa_compat_q2_rerelease_client_profile source_profile, retail_profile;
static bool built = false;
static void ensure(void) {
  if (built) return;
  built = true;
  qa_compat_q2_rerelease_layouts_ensure();
  memset(&source_profile, 0, sizeof source_profile);
  source_profile.authority_is_artifact = false;
  source_profile.header_digest = "sha256:3257c79f07d9e8ef333342b9a0be0bde7dd514d9de1b9b36173aad157601aecf";
  source_profile.layout = qa_compat_q2_rerelease_private_client_prefix_layout;
  source_profile.inventory_count = 82; source_profile.ammo_count = 12;
  memset(&retail_profile, 0, sizeof retail_profile);
  retail_profile.authority_is_artifact = true;
  retail_profile.digest = "sha256:045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd";
  retail_profile.inventory_count = 84; retail_profile.ammo_count = 12;
  /* clientLayout.fields renamed shared.* plus observed retail fields. */
  size_t shared = qa_compat_q2_rerelease_client_layout.field_count;
  static const struct { const char *name; double off; const char *storage; double count; } extra[] = {
    {"resp.score", 0x17c8, "int32", 1}, {"resp.ctf_team", 0x17dc, "int32", 1},
    {"pers.selected_item", 2672, "int32", 1}, {"pers.inventory", 2688, "int32", 84},
    {"pers.max_ammo", 3024, "int16", 12}, {"pers.weapon", 3048, "pointer", 1},
    {"newweapon", 6296, "pointer", 1}, {"v_angle", 6552, "float32", 3},
    {"invincible_time", 6672, "int64", 1}, {"no_weapon_chains", 7016, "uint8", 1}};
  size_t total = shared + sizeof extra / sizeof extra[0];
  qa_guest_field_layout *fields = calloc(total, sizeof *fields);
  if (!fields) qa_range_error("Rerelease client profile out of memory");
  for (size_t i = 0; i < shared; i++) {
    char buf[512]; snprintf(buf, sizeof buf, "shared.%s", qa_compat_q2_rerelease_client_layout.fields[i].name);
    size_t n = strlen(buf) + 1; char *name = malloc(n); if (!name) qa_range_error("Rerelease client profile out of memory");
    memcpy(name, buf, n);
    fields[i] = qa_compat_q2_rerelease_client_layout.fields[i]; fields[i].name = name;
  }
  for (size_t i = 0; i < sizeof extra / sizeof extra[0]; i++)
    fields[shared + i] = (qa_guest_field_layout){extra[i].name, extra[i].off, extra[i].storage, extra[i].count};
  retail_profile.layout.id = "q2-rerelease-retail:observed-client-fields";
  retail_profile.layout.byte_length = 7344; retail_profile.layout.alignment = 8; retail_profile.layout.pointer_bytes = 8;
  retail_profile.layout.fields = fields; retail_profile.layout.field_count = total;
}
const qa_compat_q2_rerelease_client_profile *qa_compat_q2_rerelease_get_source_client_profile(void) { ensure(); return &source_profile; }
const qa_compat_q2_rerelease_client_profile *qa_compat_q2_rerelease_get_retail_client_profile(void) { ensure(); return &retail_profile; }
