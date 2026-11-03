/* Port of src/compat/q2/native-primary-inventory-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_inventory_profile.h"
#include <string.h>
static const qa_compat_q2_selection_write cw[]={{0x2e0,4}};
static const qa_compat_q2_selection_write rw[]={{0xa70,4},{0xa78,8},{0x10c,2}};
static const qa_compat_q2_native_primary_inventory_profile classic = {
 .digest="sha256:8187df3fd5b4d435d8227434d3351aad2b47e546236403e52adcd4d275810c45",.abi={"windows-i386","pe32",4,"cdecl"},.client=0x54,.inventory=0x2e4,.count=256,.cursor=0x2e0,.empty=-1,
 .proto_weapon="q2:weapon_blaster",.proto_ammunition="q2:ammo_shells",.proto_usable="q2:item_quad",.proto_passive="q2:key_data_cd",.proto_droppable="q2:item_quad",.proto_undroppable="q2:weapon_blaster",
 .selection_writes=cw,.selection_write_count=1,
 .next_entry=0x2fe0,.next_scan=0x3003,.next_join=0x306a,.next_menu_argument=false,.prev_entry=0x3070,.prev_scan=0x3093,.prev_join=0x30ff,
 .validate_entry=0x3110,.has_validate_scan=false,.use_entry=0x3a60,.use_call=0x3abc,.use_join=0x3abe,
 .named_entry=0x36d0,.named_lookup_call=0x36dd,.named_lookup_return=0x36e2,.named_call=0x384c,.named_join=0x384f};
/* 1998 retail Xatrix DLL (0b0d119e): SelectNextItem 0x2a30, SelectPrevItem 0x2ab0, ValidateSelectedItem 0x2b30, Cmd_Use_f 0x30f0, Cmd_InvUse_f 0x3480;
 * each scan region starts at the loop head after the cursor is loaded and joins the final epilogue that follows the cursor store. */
static const qa_compat_q2_native_primary_inventory_profile xatrix1998 = {
 .digest="sha256:0b0d119ea9ed1abbce02f32fe1243bcb69457d10a34a49b775c473210bcb65a3",.abi={"windows-i386","pe32",4,"cdecl"},.client=0x54,.inventory=0x2e4,.count=256,.cursor=0x2e0,.empty=-1,
 .proto_weapon="q2:weapon_blaster",.proto_ammunition="q2:ammo_shells",.proto_usable="q2:item_quad",.proto_passive="q2:key_data_cd",.proto_droppable="q2:item_quad",.proto_undroppable="q2:weapon_blaster",
 .selection_writes=cw,.selection_write_count=1,
 .next_entry=0x2a30,.next_scan=0x2a4b,.next_join=0x2a9f,.next_menu_argument=false,.prev_entry=0x2ab0,.prev_scan=0x2ad0,.prev_join=0x2b24,
 .validate_entry=0x2b30,.has_validate_scan=false,.use_entry=0x3480,.use_call=0x34d9,.use_join=0x34db,
 .named_entry=0x30f0,.named_lookup_call=0x30fd,.named_lookup_return=0x3102,.named_call=0x326c,.named_join=0x326f};
static const qa_compat_q2_native_primary_inventory_profile retail = {
 .digest="sha256:045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd",.abi={"windows-x86-64","pe32+",8,"microsoft-x64"},.client=0x78,.inventory=0xa80,.count=84,.cursor=0xa70,.empty=0,
 .proto_weapon="q2:weapon_blaster",.proto_ammunition="q2:ammo_shells",.proto_usable="q2:item_quad",.proto_passive="q2:key_data_cd",.proto_droppable="q2:item_quad",.proto_undroppable="q2:weapon_blaster",
 .selection_writes=rw,.selection_write_count=3,
 .next_entry=0x56a40,.next_scan=0x56a76,.next_join=0x56b1e,.next_menu_argument=true,.prev_entry=0x56b30,.prev_scan=0x56bde,.prev_join=0x56c84,
 .validate_entry=0x56c90,.has_validate_scan=true,.validate_scan_entry=0x56ca6,.validate_scan_join=0x56d14,
 .use_entry=0x58670,.use_call=0x58775,.use_join=0x5877b,
 .named_entry=0x581f0,.named_lookup_call=0x582cf,.named_lookup_return=0x582d4,.named_call=0x583bc,.named_join=0x583c2};
const qa_compat_q2_native_primary_inventory_profile *qa_compat_q2_get_native_primary_inventory_profile(qa_content_digest digest) {
 if (digest && strcmp(digest,classic.digest)==0) return &classic;
 if (digest && strcmp(digest,xatrix1998.digest)==0) return &xatrix1998;
 if (digest && strcmp(digest,retail.digest)==0) return &retail;
  return NULL; }
