/* Port of src/content/q3/input-profile.ts. GPL-2.0-or-later. */
#include "qa/content/q3/input_profile.h"
#include <string.h>
#include "qa/content/q3/equipment/threewave_grapple_profile.h"
static const double LR_INTER[]={5,6}, TW_INTER[]={7,8};
bool qa_q3_input_profile(const qa_q3_equipment_qvm_artifact *artifact, qa_q3_qvm_input_definition *out){
  const char *digest=artifact->module.digest?artifact->module.digest:"";
  *out=(qa_q3_qvm_input_definition){0}; out->module=artifact->module;
  out->client_pointer=516; out->movement_normal=0; out->movement_noclip=1; out->movement_freeze=4;
  if(strcmp(digest,qa_lrctf_grapple_digest)==0){
    out->entity_stride=856; out->client_stride=872; out->intermission=LR_INTER; out->intermission_count=2;
    out->entry_client_think=114858; out->entry_run_client=114917; out->entry_client_spawn=125027;
    out->entry_move=22369; out->entry_slice=21620; return true;
  }
  if(strcmp(digest,qa_threewave_grapple_digest)==0){
    out->entity_stride=876; out->client_stride=944; out->intermission=TW_INTER; out->intermission_count=2;
    out->entry_client_think=120292; out->entry_run_client=120383; out->entry_client_spawn=132015;
    out->entry_move=35535; out->entry_slice=34707; return true;
  }
  return false;
}
