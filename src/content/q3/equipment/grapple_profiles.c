/* Port of src/content/q3/equipment/grapple-profiles.ts. GPL-2.0-or-later. */
#include "qa/content/q3/equipment/grapple_profiles.h"
#include "qa/content/q3/equipment/threewave_grapple_profile.h"
bool qa_q3_grapple_profile(const qa_q3_equipment_qvm_artifact *artifact,
    struct qa_qvm_grapple_definition *out){
  if(qa_lrctf_grapple_profile(artifact,out)) return true;
  return qa_threewave_grapple_profile(artifact,out);
}
