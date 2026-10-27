/* Port of src/render/q3-hardware.ts. GPL-2.0-or-later. */
#include "qa/render/q3_hardware.h"
#include <ctype.h>
#include <string.h>
qa_q3_effect_hardware qa_q3_effect_hardware_make(qa_q3_hardware_read_fn read, void *ctx){ qa_q3_effect_hardware p; p.read=read; p.ctx=ctx; return p; }
const char *qa_q3_effect_hardware_hardware(const qa_q3_effect_hardware *p){ return strcmp(p->read(p->ctx),"ragepro")==0?"ragepro":"generic"; }
const char *qa_q3_effect_hardware_type(const qa_q3_effect_hardware *p){ return strcmp(p->read(p->ctx),"ragepro")==0?"rage-pro":"generic"; }
static int contains_ci(const char *hay, const char *needle){
  size_t n=strlen(needle); if(n==0) return 1;
  for(const char *p=hay;*p;p++){ size_t i=0; while(i<n && p[i] && tolower((unsigned char)p[i])==tolower((unsigned char)needle[i])) i++; if(i==n) return 1; }
  return 0;
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
