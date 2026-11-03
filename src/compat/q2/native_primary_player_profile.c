/* Port of src/compat/q2/native-primary-player-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_player_profile.h"
#include <string.h>
static const char *const red_args[]={"team","red"}; static const char *const blue_args[]={"team","blue"};
static const qa_source_team_command_entry teams[]={{{"q2:1","team:red"},{red_args,2}},{{"q2:2","team:blue"},{blue_args,2}}};
static const qa_compat_q2_native_primary_player_profile xatrix={"sha256:8187df3fd5b4d435d8227434d3351aad2b47e546236403e52adcd4d275810c45",true,{0xd88,NULL,0},0x312a0,"none",0,0xd8c,0x178,false,0};
/* 1998 retail Xatrix DLL (0b0d119e): resp.score 0xd80 and resp.cmd_angles 0xd84 (no resp.spectator in this build), SelectSpawnPoint at 0x307e0. */
static const qa_compat_q2_native_primary_player_profile xatrix1998={"sha256:0b0d119ea9ed1abbce02f32fe1243bcb69457d10a34a49b775c473210bcb65a3",true,{0xd80,NULL,0},0x307e0,"none",0,0xd84,0x178,false,0};
static const qa_compat_q2_native_primary_player_profile retail={"sha256:045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd",true,{0x17c8,teams,2},0xd9050,"entry",0x11eb10,0x17cc,0x694,true,0x19a4};
const qa_compat_q2_native_primary_player_profile *qa_compat_q2_get_native_primary_player_profile(qa_content_digest digest) {
 if (digest && strcmp(digest,xatrix1998.digest)==0) return &xatrix1998;
 if (digest && strcmp(digest,xatrix.digest)==0) return &xatrix;
 if (digest && strcmp(digest,retail.digest)==0) return &retail;
  return NULL; }
