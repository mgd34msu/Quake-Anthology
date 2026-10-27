/* Port of src/render/output-gamma.ts. GPL-2.0-or-later. */
#include "qa/render/output_gamma.h"
#include "qa/formats/images/palette.h"
#include "qa/foundation/support.h"
#include <math.h>
bool qa_output_gamma_table(double gamma, uint8_t table[256]){
  if(!isfinite(gamma)||gamma<0.5||gamma>3) qa_range_error("Output gamma must be between 0.5 and 3");
  if(gamma==1) return false;
  qa_gamma_profile profile; profile.kind=QA_GAMMA_Q3; profile.gamma=gamma;
  profile.intensity=1; profile.overbright_bits=0; profile.only_gamma=true;
  qa_build_gamma_table(&profile, table); return true;
}
