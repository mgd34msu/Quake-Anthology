/* Port of src/compat/q2/native-primary-drop-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_drop_profile.h"
#include <string.h>
static const qa_compat_q2_native_region_span_fwd cc[]={{0x39dc,0x39df},{0x3cdc,0x3cde}};
static const qa_compat_q2_native_region_span_fwd cd[]={{0x36b57,0x36b59},{0xa5ab,0xa5ae}};
static const qa_compat_q2_native_region_span_fwd rc[]={{0x58601,0x58607},{0x58a22,0x58a28}};
static const qa_compat_q2_native_region_span_fwd rd[]={{0xf0964,0xf096b},{0x6749f,0x674a3}};
static const qa_compat_q2_native_primary_drop_profile classic = {
 .digest="sha256:8187df3fd5b4d435d8227434d3351aad2b47e546236403e52adcd4d275810c45",.abi={"windows-i386","pe32",4,"cdecl"},
 .client_pointer=0x54,.client_inventory=0x2e4,.client_cursor=0x2e0,.client_weapon=0x704,.client_pending=0xddc,
 .named=0x3860,.inv_entry=0x3c80,.inv_admitted=0x3c8b,.find=0x9590,.lookup_return=0x3872,.allocate=0xad00,.free_entry=0x19140,
 .has_consumer=false,.callbacks=cc,.callback_count=2,.debits=cd,.debit_count=2};
/* 1998 retail Xatrix DLL (0b0d119e): Cmd_Drop_f 0x3280, Cmd_InvDrop_f 0x3680, FindItem 0x8de0, Drop_Item 0xa540, G_FreeEdict 0x18750;
 * debits are the counter stores in Drop_Weapon (0x35707) and Drop_Ammo (0x9dfb); newweapon sits at 0xdd0. */
static const qa_compat_q2_native_region_span_fwd xc[]={{0x33fc,0x33ff},{0x36d9,0x36db}};
static const qa_compat_q2_native_region_span_fwd xd[]={{0x35707,0x35709},{0x9dfb,0x9dfe}};
static const qa_compat_q2_native_primary_drop_profile xatrix1998 = {
 .digest="sha256:0b0d119ea9ed1abbce02f32fe1243bcb69457d10a34a49b775c473210bcb65a3",.abi={"windows-i386","pe32",4,"cdecl"},
 .client_pointer=0x54,.client_inventory=0x2e4,.client_cursor=0x2e0,.client_weapon=0x704,.client_pending=0xdd0,
 .named=0x3280,.inv_entry=0x3680,.inv_admitted=0x368b,.find=0x8de0,.lookup_return=0x3292,.allocate=0xa540,.free_entry=0x18750,
 .has_consumer=false,.callbacks=xc,.callback_count=2,.debits=xd,.debit_count=2};
static const qa_compat_q2_native_primary_drop_profile retail = {
 .digest="sha256:045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd",.abi={"windows-x86-64","pe32+",8,"microsoft-x64"},
 .client_pointer=0x78,.client_inventory=0xa80,.client_cursor=0xa70,.client_weapon=0xbe8,.client_pending=0x1898,
 .named=0x58410,.inv_entry=0x58970,.inv_admitted=0x58998,.find=0x660a0,.lookup_return=0x58541,.allocate=0x680c0,.free_entry=0x96600,
 .has_consumer=true,.consumer_entry=0x674a6,.consumer_join=0x674ab,.callbacks=rc,.callback_count=2,.debits=rd,.debit_count=2};
const qa_compat_q2_native_primary_drop_profile *qa_compat_q2_get_native_primary_drop_profile(qa_content_digest digest) {
 if (digest && strcmp(digest,classic.digest)==0) return &classic;
 if (digest && strcmp(digest,xatrix1998.digest)==0) return &xatrix1998;
 if (digest && strcmp(digest,retail.digest)==0) return &retail;
  return NULL; }
