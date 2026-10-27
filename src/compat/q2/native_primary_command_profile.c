/* Port of src/compat/q2/native-primary-command-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_command_profile.h"
#include <string.h>
static const qa_compat_q2_ammo_grant xg[] = {{0x34ce,0x34d5,"rsi","set"},{0x34db,0x34f3,"rsi","add"}};
static const qa_compat_q2_ammo_grant rg[] = {{0x57127,0x5712e,"rdi","set"},{0x57133,0x5713d,"rdi","add"}};
static const qa_compat_q2_native_primary_command_profile xatrix = {
  .digest="sha256:8187df3fd5b4d435d8227434d3351aad2b47e546236403e52adcd4d275810c45", .abi={"windows-i386","pe32",4,"cdecl"},
  .give_entry=0x3140,.give_weapons=0x3256,.give_ammo=0x32af,.give_unknown_entry=0x3455,.give_unknown_join=0x3466,
  .ammo_grants=xg,.ammo_grant_count=2,.give_argc=0x7677c,.give_argv=0x76780,
  .drop_entry=0x307c0,.drop_elig_entry=0x307e7,.drop_elig_join=0x3083b,
  .client_pointer=0x54,.client_weapon=0x704,.has_client_ammo_index=true,.client_ammo_index=0xdc8,.client_inventory=0x2e4,
  .items_table=0x4b828,.items_stride=76,.items_count=48,.items_classname=0,.items_flags=0x38,.items_weapon_flag=1,.items_ammunition_flag=2,.items_icon=0x24,
  .ammo_kind="name",.ammo_offset=0x34,.ammo_label=0x28};
static const qa_compat_q2_native_primary_command_profile retail = {
  .digest="sha256:045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd", .abi={"windows-x86-64","pe32+",8,"microsoft-x64"},
  .give_entry=0x56de0,.give_weapons=0x57372,.give_ammo=0x573f7,.give_unknown_entry=0x57091,.give_unknown_join=0x5781d,
  .ammo_grants=rg,.ammo_grant_count=2,.give_argc=0x1da780,.give_argv=0x1da788,
  .drop_entry=0xd5ec0,.drop_elig_entry=0xd5eed,.drop_elig_join=0xd5f30,
  .client_pointer=0x78,.client_weapon=0xbe8,.has_client_ammo_index=false,.client_inventory=0xa80,
  .items_table=0x195320,.items_stride=192,.items_count=84,.items_classname=8,.items_flags=0x7c,.items_weapon_flag=1,.items_ammunition_flag=2,.items_icon=0x50,
  .ammo_kind="index",.ammo_offset=0x74};
const qa_compat_q2_native_primary_command_profile *qa_compat_q2_get_native_primary_command_profile(qa_content_digest digest) {
  if (digest && strcmp(digest,xatrix.digest)==0) return &xatrix;
  if (digest && strcmp(digest,retail.digest)==0) return &retail;
  return NULL;
}
