/* Port of src/compat/q2/compatibility.ts (parse; async mount open is
 * synchronous in C callers). GPL-2.0-or-later. */
#include "qa/compat/q2/compatibility.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_primary_profiles.h"
#include "qa/content/mounts/paths.h"
#include "qa/persistence/shared.h"
typedef qa_save_reader R;
static const R *F(const R *r,const char *n,R *o){qa_save_reader_field(r,n,o);return o;}
static bool ieq(const char *a,const char *b){ while(*a&&*b){ if(tolower((unsigned char)*a)!=tolower((unsigned char)*b)) return false; a++;b++; } return *a==*b; }
bool qa_compat_q2_parse_native_compatibility(const qa_save_value *value, const qa_resolved_execution_module *execution, qa_compat_q2_world_readers worlds, qa_compat_q2_native_compatibility_result *out) {
  memset(out,0,sizeof *out);
  R root; if(!qa_save_reader_init(&root,value,"native-compatibility.json")) return false;
  {R v; qa_save_reader_literal_number(F(&root,"version",&v),1); qa_save_reader_destroy(&v);}
  const qa_resolved_resource_reference *artifact=(const qa_resolved_resource_reference*)execution->artifact;
  R mods; F(&root,"modules",&mods); size_t n=qa_save_reader_list_count(&mods);
  char **seen=calloc(n?n:1,sizeof *seen); size_t seen_n=0;
  for(size_t i=0;i<n;i++){ R entry,pr,dr,av; qa_save_reader_item(&mods,i,&entry);
    const char *raw=qa_save_reader_string(F(&entry,"artifactPath",&pr)); char *path=qa_normalize_resource_path(raw); qa_save_reader_destroy(&pr);
    for(size_t j=0;j<seen_n;j++) if(ieq(seen[j],path)) qa_save_reader_fail(&entry,"duplicate native module declaration");
    seen[seen_n++]=path;
    qa_content_digest digest=qa_read_digest(F(&entry,"artifactDigest",&dr)); qa_save_reader_destroy(&dr);
    static const double apis[]={3,2023}; double api=qa_save_reader_choice_number(F(&entry,"apiVersion",&av),apis,2); qa_save_reader_destroy(&av);
    if(!ieq(path,artifact->requested_path)){ qa_save_reader_destroy(&entry); continue; }
    double want_api = execution->api.kind==QA_GAME_API_Q2_GAME ? (double)execution->api.as.q2_game.version : 0;
    if(strcmp(digest,artifact->digest)!=0||api!=want_api) qa_save_reader_fail(&entry,"native primary declaration differs from the selected artifact or API");
    R prim; F(&entry,"primary",&prim); qa_native_abi abi=execution->profile;
    R a,b,c2,d2,e2,f2;
    qa_compat_q2_read_native_primary_weapons(F(&prim,"weapons",&a),digest,abi,&out->weapons); qa_save_reader_destroy(&a);
    qa_compat_q2_read_native_primary_player(F(&prim,"player",&b),digest,&out->player); qa_save_reader_destroy(&b);
    qa_compat_q2_read_native_primary_commands(F(&prim,"commands",&c2),digest,abi,&out->commands); qa_save_reader_destroy(&c2);
    qa_compat_q2_read_native_primary_inventory(F(&prim,"inventory",&d2),digest,abi,&out->inventory); qa_save_reader_destroy(&d2);
    qa_compat_q2_read_native_primary_drop(F(&prim,"drop",&e2),digest,abi,&out->drop); qa_save_reader_destroy(&e2);
    qa_compat_q2_read_native_primary_pickups(F(&prim,"pickups",&f2),digest,abi,&out->pickups); qa_save_reader_destroy(&f2);
    if(out->weapons.entity_client!=out->inventory.client||out->commands.client_pointer!=out->inventory.client||out->drop.client_pointer!=out->inventory.client||out->pickups.supply_client!=out->inventory.client
      ||out->commands.client_inventory!=out->inventory.inventory||out->drop.client_inventory!=out->inventory.inventory||out->pickups.supply_inventory!=out->inventory.inventory
      ||out->commands.items_table!=out->pickups.items_table||out->commands.items_stride!=out->pickups.items_stride||out->commands.items_count!=out->pickups.items_count
      ||out->commands.items_count>out->inventory.count) qa_save_reader_fail(&prim,"primary services disagree about their source client or item storage");
    out->profile.weapons=&out->weapons; out->profile.player=&out->player; out->profile.commands=&out->commands; out->profile.inventory=&out->inventory; out->profile.drop=&out->drop; out->profile.pickups=&out->pickups;
    R wr; F(&prim,"world",&wr);
    if(api==3){ out->profile.edition="classic"; out->profile.world=worlds.classic?worlds.classic(worlds.ctx,&wr,digest):NULL; }
    else { out->profile.edition="rerelease"; out->profile.world=worlds.rerelease?worlds.rerelease(worlds.ctx,&wr,digest):NULL; }
    qa_save_reader_destroy(&wr); qa_save_reader_destroy(&prim); out->found=true; qa_save_reader_destroy(&entry);
  }
  for(size_t j=0;j<seen_n;j++) free(seen[j]);
  free(seen);
  qa_save_reader_destroy(&mods); qa_save_reader_destroy(&root); return true;
}
