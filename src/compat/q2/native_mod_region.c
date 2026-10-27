/* Port of src/compat/q2/native-mod-region.ts (validate + execution
 * state; live bind needs guest host, see unresolved). GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_region.h"
#include <math.h>
#include <string.h>
static bool safei(double v){return v==trunc(v)&&v>=-9007199254740991.0&&v<=9007199254740991.0;}
static int sbytes(const char *s,int64_t pb){ if(!strcmp(s,"pointer"))return (int)pb; if(!strcmp(s,"int8")||!strcmp(s,"uint8"))return 1; if(!strcmp(s,"int16")||!strcmp(s,"uint16"))return 2; if(!strcmp(s,"int32")||!strcmp(s,"uint32")||!strcmp(s,"float32"))return 4; return 8; }
typedef struct { const char *bank; double off,len; } Ext;
static Ext extent(const qa_native_mod_protection_region *r, qa_native_mod_region_location loc, int64_t pb){
  double len=sbytes(loc.storage,pb); Ext e;
  if(loc.kind==QA_NATIVE_MOD_LOCATION_REGISTER){ if(pb==4&&(len>4||(loc.reg&&loc.reg[0]=='r'&&(loc.reg[1]=='8'||loc.reg[1]=='9'||loc.reg[1]=='1')))) qa_range_error("Native region register exceeds the source architecture"); e.bank=loc.reg; e.off=0; e.len=len; return e; }
  if(!safei(loc.offset)||loc.offset<0) qa_range_error("Invalid native region field offset");
  if(loc.kind==QA_NATIVE_MOD_LOCATION_SIMD){ if(!safei(loc.index)||loc.index<0||loc.index>=(pb==4?8:16)||loc.offset+len>16) qa_range_error("Native region SIMD field exceeds its register"); e.bank="simd"; e.off=loc.index*16+loc.offset; e.len=len; return e; }
  if(loc.offset+len>r->frame.stack_bytes+pb+r->frame.argument_bytes||(loc.offset<r->frame.stack_bytes+pb&&loc.offset+len>r->frame.stack_bytes)) qa_range_error("Native region field exceeds its frame or overlaps the return address");
  e.bank="stack"; e.off=loc.offset; e.len=len; return e;
}
void qa_compat_q2_validate_native_mod_region(const qa_native_mod_protection_region *region, int64_t pb){
  double b[4]={region->frame.entry,region->entry,region->join,region->frame.exit};
  for(int i=0;i<4;i++) if(!safei(b[i])||b[i]<0) qa_range_error("Invalid standalone native region frame");
  for(int i=0;i<4;i++) for(int j=i+1;j<4;j++) if(b[i]==b[j]) qa_range_error("Invalid standalone native region frame");
  if(!safei(region->frame.stack_bytes)||region->frame.stack_bytes<0||fmod(region->frame.stack_bytes,(double)pb)!=0) qa_range_error("Invalid standalone native region frame");
  if(!safei(region->frame.argument_bytes)||region->frame.argument_bytes<0||fmod(region->frame.argument_bytes,(double)pb)!=0) qa_range_error("Invalid standalone native region frame");
  if(region->call.skip_count!=0||!strcmp(region->result.storage,"pointer")) qa_range_error("Invalid standalone native region frame");
  Ext ranges[64]; size_t n=region->input_count<64?region->input_count:64;
  for(size_t i=0;i<n;i++) ranges[i]=extent(region,region->inputs[i].target,pb);
  (void)extent(region,region->result,pb);
  for(size_t i=0;i<n;i++) for(size_t j=0;j<i;j++) if(ranges[i].bank&&ranges[j].bank&&!strcmp(ranges[i].bank,ranges[j].bank)&&ranges[i].off<ranges[j].off+ranges[j].len&&ranges[j].off<ranges[i].off+ranges[i].len) qa_range_error("Native region inputs overlap");
}
void qa_compat_q2_native_mod_region_execution_init(qa_compat_q2_native_mod_region_execution *s, const qa_native_mod_protection_region *def, size_t input_count, int64_t pb){
  qa_compat_q2_validate_native_mod_region(def,pb);
  if(input_count!=def->input_count) qa_range_error("Native region input count differs from its declaration");
  s->definition=def; s->input_count=input_count; s->phase=0; s->has_output=false;
}
