/* Port of src/compat/q2/native-primary-weapon-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_weapon_profile.h"
#include <string.h>
#define FLD(rec,off,enc) {{(off),(enc)},(rec)}
#define ST(rec,off,enc,val) {"scalar",FLD(rec,off,enc),false,0,"equals",(val),{0,0},NULL}
static const qa_source_equipment_context ectx[]={{"q2:equipment/hand-grenades","q2:ammo_grenades"}};
static const qa_native_weapon_decision_field xb[]={{FLD("client",0xdcc,"int32"),1},{FLD("client",0xdd4,"int32"),1}};
static const qa_native_weapon_decision_field xs[]={{FLD("client",0xdcc,"int32"),1}};
static const qa_native_weapon_decision_field rb[]={{FLD("client",0x1860,"uint8"),1},{FLD("client",0x1862,"uint8"),1}};
static const qa_native_weapon_decision_field rs[]={{FLD("client",0x1860,"uint8"),1}};
static const qa_native_weapon_decision xd[]={{0x36c96,0x36ca6,xb,2},{0x370bd,0x370ce,xb,2},{0x3927d,0x3928e,xb,2},{0x378f2,0x378f9,xs,1},{0x37b28,0x37b2f,xs,1},{0x37f6d,0x37f74,xs,1},{0x37f97,0x37f9e,xs,1}};
static const qa_native_weapon_decision rd[]={{0xf0f3d,0xf0f4d,rb,2},{0xf1a4b,0xf1a5a,rb,2},{0xf28d1,0xf28d8,rs,1},{0xf2967,0xf296e,rs,1},{0xf2b7e,0xf2b85,rs,1},{0xf3076,0xf307d,rs,1},{0xf30be,0xf30c5,rs,1},{0x118fe0,0x118fe7,rs,1},{0x11913f,0x119146,rs,1},{0x119873,0x11987a,rs,1},{0x119cd9,0x119ce0,rs,1}};
static const qa_native_item_test xa[]={ST("image",0x768c8,"float32",0),ST("client",0xd98,"int32",0),ST("entity",0x1ec,"int32",0)};
static const qa_native_item_test xc0[]={ST("client",0xe00,"int32",3)};
static const qa_native_item_test_row xcont[]={{xc0,1}};
static const qa_native_item_test ra[]={ST("image",0x241c30,"int64",0),ST("client",0x17d8,"uint8",0),ST("client",0x1c54,"uint8",0),ST("entity",0x7a4,"uint8",0)};
static const qa_native_item_test rsp[]={ST("client",0x1c54,"uint8",0)};
static const qa_native_item_test rc0[]={ST("client",0x1890,"uint8",1)};
static const qa_native_item_test_row rcomm[]={{rc0,1}};
static const qa_native_item_test rk0[]={ST("client",0x1924,"int32",3)};
static const qa_native_item_test rk1[]={ST("client",0x1890,"uint8",1)};
static const qa_native_item_test_row rcont[]={{rk0,1},{rk1,1}};
static const qa_compat_q2_native_region_span_fwd xskip[]={{0x36b6b,0x36be5},{0x36bea,0x36de6},{0x36d31,0x36dcc}};
static const qa_compat_q2_native_region_span_fwd rskip[]={{0xf11c3,0xf12d8}};
static const double xf[]={1,0.5}; static const double xdf[]={1,4};
static const qa_compat_q2_weapon_projection rproj[]={{FLD("client",0x1924,"int32"),3},{FLD("client",0x78,"int32"),1}};
static const qa_native_item_field rwrites[]={FLD("client",0x7c,"int32")};
static const qa_compat_q2_native_primary_weapon_profile xatrix={
 .digest="sha256:8187df3fd5b4d435d8227434d3351aad2b47e546236403e52adcd4d275810c45",.abi={"windows-i386","pe32",4,"cdecl"},
 .dispatcher={{"rva",NULL,0x36710},"entity",0,1},.decisions=xd,.decision_count=7,
 .spawn_entry=0x319d0,.active=xa,.active_count=3,.continuations=xcont,.continuation_count=1,
 .time_address=0x76804,.time_encoding="float32",.time_milliseconds=1000,
 .entity_client=84,.entity_water_level=FLD("entity",0x264,"int32"),.entity_view_height=FLD("entity",0x1fc,"int32"),.entity_max_health=FLD("entity",0x1e4,"int32"),
 .client_byte_length=3832,.client_view_angles=0xe44,.client_buttons=FLD("client",0xdcc,"int32"),.client_latched_buttons=FLD("client",0xdd4,"int32"),
 .attack_animation_entry=0x36b60,.attack_skip=xskip,.attack_skip_count=3,
 .anim_frame=FLD("entity",56,"int32"),.anim_end=FLD("client",0xe7c,"int32"),.anim_priority=FLD("client",0xe80,"int32"),.anim_duck=FLD("client",0xe84,"int32"),.anim_run=FLD("client",0xe88,"int32"),
 .equipment_contexts=ectx,.equipment_context_count=1,
 .delay_flag=FLD("image",0x6b694,"int32"),.delay_entry=0x3676f,.delay_join=0x36793,.delay_evaluate_kind="source-flag",.delay_factors=xf,.delay_factor_count=2,
 .damage_kind="source-flag",.damage_address=0x6b690,.damage_encoding="int32",.damage_factors=xdf,.damage_factor_count=2,.damage_region_entry=0x36748,.damage_region_join=0x3676f};
/* 1998 retail Xatrix DLL (PE32, 0b0d119e). Every address below is the 8187df profile's counterpart found by instruction shape in this PE:
 * Think_Weapon 0x352c0 (is_quad store 0x68e70, is_quadfire 0x68e74), Weapon_Generic 0x35710, PutClientInServer 0x30bd0. The client record
 * has no resp.spectator in this build (resp.cmd_angles 0xd84 is followed directly by old_pmove 0xd90), so buttons sit at 0xdc0 and
 * sizeof(gclient_t) is 0xee4. */
static const qa_native_weapon_decision_field x98b[]={{FLD("client",0xdc0,"int32"),1},{FLD("client",0xdc8,"int32"),1}};
static const qa_native_weapon_decision_field x98s[]={{FLD("client",0xdc0,"int32"),1}};
static const qa_native_weapon_decision x98d[]={{0x35846,0x35856,x98b,2},{0x35c6d,0x35c7e,x98b,2},{0x37e2d,0x37e3e,x98b,2},{0x364a2,0x364a9,x98s,1},{0x366d8,0x366df,x98s,1},{0x36b1d,0x36b24,x98s,1},{0x36b47,0x36b4e,x98s,1}};
static const qa_native_item_test x98a[]={ST("image",0x740a8,"float32",0),ST("entity",0x1ec,"int32",0)};
static const qa_native_item_test x98c0[]={ST("client",0xdf4,"int32",3)};
static const qa_native_item_test_row x98cont[]={{x98c0,1}};
static const qa_compat_q2_native_region_span_fwd x98skip[]={{0x3571b,0x35795},{0x3579a,0x35996},{0x358e1,0x3597c}};
static const qa_compat_q2_native_primary_weapon_profile xatrix1998={
 .digest="sha256:0b0d119ea9ed1abbce02f32fe1243bcb69457d10a34a49b775c473210bcb65a3",.abi={"windows-i386","pe32",4,"cdecl"},
 .dispatcher={{"rva",NULL,0x352c0},"entity",0,1},.decisions=x98d,.decision_count=7,
 .spawn_entry=0x30bd0,.active=x98a,.active_count=2,.continuations=x98cont,.continuation_count=1,
 .time_address=0x73fe4,.time_encoding="float32",.time_milliseconds=1000,
 .entity_client=84,.entity_water_level=FLD("entity",0x264,"int32"),.entity_view_height=FLD("entity",0x1fc,"int32"),.entity_max_health=FLD("entity",0x1e4,"int32"),
 .client_byte_length=3812,.client_view_angles=0xe38,.client_buttons=FLD("client",0xdc0,"int32"),.client_latched_buttons=FLD("client",0xdc8,"int32"),
 .attack_animation_entry=0x35710,.attack_skip=x98skip,.attack_skip_count=3,
 .anim_frame=FLD("entity",56,"int32"),.anim_end=FLD("client",0xe70,"int32"),.anim_priority=FLD("client",0xe74,"int32"),.anim_duck=FLD("client",0xe78,"int32"),.anim_run=FLD("client",0xe7c,"int32"),
 .equipment_contexts=ectx,.equipment_context_count=1,
 .delay_flag=FLD("image",0x68e74,"int32"),.delay_entry=0x3531f,.delay_join=0x35343,.delay_evaluate_kind="source-flag",.delay_factors=xf,.delay_factor_count=2,
 .damage_kind="source-flag",.damage_address=0x68e70,.damage_encoding="int32",.damage_factors=xdf,.damage_factor_count=2,.damage_region_entry=0x352f8,.damage_region_join=0x3531f};
static const qa_compat_q2_native_primary_weapon_profile retail={
 .digest="sha256:045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd",.abi={"windows-x86-64","pe32+",8,"microsoft-x64"},
 .dispatcher={{"rva",NULL,0xf05d0},"entity",0,1},.decisions=rd,.decision_count=11,
 .spawn_entry=0xda4b0,.spawn_accepted=rsp,.spawn_accepted_count=1,.active=ra,.active_count=4,
 .committed_input=rcomm,.committed_input_count=1,.continuations=rcont,.continuation_count=2,
 .time_address=0x241b28,.time_encoding="int64",.time_milliseconds=1,
 .entity_client=0x78,.entity_water_level=FLD("entity",0x83c,"uint8"),.entity_view_height=FLD("entity",0x7a0,"int32"),.entity_max_health=FLD("entity",0x77c,"int32"),
 .client_byte_length=7344,.client_view_angles=0x1998,.client_buttons=FLD("client",0x1860,"uint8"),.client_latched_buttons=FLD("client",0x1862,"uint8"),
 .attack_animation_entry=0xf1180,.attack_skip=rskip,.attack_skip_count=1,
 .anim_frame=FLD("entity",56,"int32"),.anim_end=FLD("client",0x19f4,"int32"),.anim_priority=FLD("client",0x19f8,"int32"),.anim_duck=FLD("client",0x19fc,"uint8"),.anim_run=FLD("client",0x19fd,"uint8"),
 .equipment_contexts=ectx,.equipment_context_count=1,
 .delay_flag=FLD("image",0x1d634d,"uint8"),.delay_entry=0xeff75,.delay_join=0xeff8a,.delay_evaluate_kind="source-animation",
 .delay_anim_entry=0xf04f0,.delay_baseline_ms=100,.delay_projection=rproj,.delay_projection_count=2,.delay_writes=rwrites,.delay_write_count=1,
 .damage_kind="source-result",.damage_entry=0xef5f0,.damage_result="uint8"};
const qa_compat_q2_native_primary_weapon_profile *qa_compat_q2_get_native_primary_weapon_profile(qa_content_digest digest) {
 if (digest && strcmp(digest,xatrix.digest)==0) return &xatrix;
 if (digest && strcmp(digest,xatrix1998.digest)==0) return &xatrix1998;
 if (digest && strcmp(digest,retail.digest)==0) return &retail;
 return NULL; }
