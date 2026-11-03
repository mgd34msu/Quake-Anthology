/* Port of src/render/q3-hardware.ts. GPL-2.0-or-later. */
#include "qa/render/q3_hardware.h"
#include <ctype.h>
#include <string.h>
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
qa_q3_effect_hardware qa_q3_effect_hardware_make(qa_q3_hardware_read_fn read, void *ctx){ qa_q3_effect_hardware p; p.read=read; p.ctx=ctx; return p; }
const char *qa_q3_effect_hardware_hardware(const qa_q3_effect_hardware *p){ return strcmp(p->read(p->ctx),"ragepro")==0?"ragepro":"generic"; }
const char *qa_q3_effect_hardware_type(const qa_q3_effect_hardware *p){ return strcmp(p->read(p->ctx),"ragepro")==0?"rage-pro":"generic"; }
/* TS renderer.toLowerCase().includes(needle): Unicode lowering of the GL string. */
static int contains_ci(const char *hay, const char *needle){
  char *h=qa_js_to_lower_case(hay), *n=qa_js_to_lower_case(needle);
  if(!h||!n) qa_range_error("Out of memory");
  int found=strstr(h,n)!=NULL; free(h); free(n); return found;
}
qa_q3_hardware qa_q3_hardware_classify(const char *renderer){
  if(contains_ci(renderer,"banshee")||contains_ci(renderer,"voodoo_graphics")) return "3dfx";
  if(contains_ci(renderer,"rage pro")||contains_ci(renderer,"ragepro")) return "ragepro";
  if(contains_ci(renderer,"permedia2")) return "permedia2";
  if(contains_ci(renderer,"riva 128")) return "riva128";
  return "generic";
}
double qa_q3_hardware_number(qa_q3_hardware h){
  if (strcmp(h, "3dfx") == 0) return 1;
  if (strcmp(h, "riva128") == 0) return 2;
  if (strcmp(h, "ragepro") == 0) return 3;
  if (strcmp(h, "permedia2") == 0) return 4;
  return 0;
}
