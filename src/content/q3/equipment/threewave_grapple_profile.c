/* Port of src/content/q3/equipment/threewave-grapple-profile.ts. GPL-2.0-or-later. */
#include "qa/content/q3/equipment/threewave_grapple_profile.h"
#include <string.h>
const char qa_threewave_grapple_digest[] = "sha256:9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337";
static const double FIRE_ARGS[]={0};
static const qa_qvm_grapple_movement_word WORDS[]={{232,10},{236,0}};
static const qa_qvm_grapple_cvar CVARS[]={{"g_gametype","10"},{"g_lithium","0"},{"p_enablePortal","0"}};
static const qa_qvm_grapple_view_attachment ATTACH[]={{"models/weapons2/grapple/grapple_hand.md3","tag_hook"}};
bool qa_threewave_grapple_profile(const qa_q3_equipment_qvm_artifact *artifact,
    struct qa_qvm_grapple_definition *out){
  if(!artifact->module.digest||strcmp(artifact->module.digest,qa_threewave_grapple_digest)!=0) return false;
  struct qa_qvm_grapple_definition d={0};
  d.id="threewave-1.7"; d.title="Threewave CTF (Quake 3)"; d.module=artifact->module; d.abi_profile="q3-modern";
  d.entity_stride=876; d.client_stride=944;
  d.fields=(qa_qvm_grapple_fields){520,516,600,768,false,0,816,732,736,552,556};
  d.globals=(qa_qvm_grapple_globals){1077712,1077708,1091860,1091720,1091768};
  d.callbacks=(qa_qvm_grapple_callbacks){210993,211210,217563,215035,215169,177663,false,0,16897,29990,false,0,162405,197341,35535};
  d.fire_arguments=(qa_double_view){FIRE_ARGS,1};
  d.movement=(qa_qvm_grapple_movement){240,WORDS,2};
  d.initial_cvars=CVARS; d.initial_cvar_count=3; d.pulling_flag=2048;
  d.event_lifetime_milliseconds=100; d.grapple_damage_method=29;
  d.presentation.projectile_model="models/weapons2/grapple/grapple_hook.md3";
  d.presentation.view_model="models/weapons2/grapple/grap.md3"; d.presentation.weapon_index=11;
  d.presentation.view_anchor=(qa_qvm_grapple_view_anchor){"models/weapons2/shotgun/shotgun_hand.md3","tag_weapon",{5,0,-1},90,-0.2};
  d.presentation.view_attachments=ATTACH; d.presentation.view_attachment_count=1;
  d.presentation.cable=(qa_qvm_grapple_cable){false,NULL,0,"models/weapons2/grapple/grapple1_cord_s.md3",
    "models/weapons2/grapple/grapple1_cord_p.md3","models/weapons2/grapple/grapple1_cord_f.md3",14};
  d.presentation.fire_sound="sound/cctf/grapple/grapple_fire.wav";
  d.presentation.attach_sound="sound/cctf/grapple/grapple_hit.wav";
  d.presentation.pull_sound="sound/cctf/grapple/grapple_pull.wav";
  d.presentation.hang_sound="sound/cctf/grapple/grapple_hang.wav";
  *out=qa_q3_equipment_read_grapple_profile(d,artifact);
  return true;
}
