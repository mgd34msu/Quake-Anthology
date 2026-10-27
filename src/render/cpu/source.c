/* Port of src/render/cpu/source.ts. GPL-2.0-or-later. */
#include "qa/render/cpu/source.h"
#include "qa/foundation/support.h"
qa_source_primitive_mode qa_source_primitive_mode_for(double requested, bool compiled){
  if(requested==0) return compiled?"elements":"array-strips";
  if (requested == 1) return "array-strips";
  if (requested == 2) return "elements";
  if (requested == 3) return "discrete-strips";
  return "none";
}
void qa_emit_source_triangle_strips(const double *indices, size_t count, qa_source_strip_emitter emit){
  if(count==0) return;
  if(count%3!=0) qa_range_error("Source triangle strips require complete index triples");
  double lastA=indices[0], lastB=indices[1], lastC=indices[2];
  emit.begin(emit.ctx); emit.element(emit.ctx,lastA); emit.element(emit.ctx,lastB); emit.element(emit.ctx,lastC);
  bool even=false;
  for(size_t i=3;i<count;i+=3){
    double a=indices[i], b=indices[i+1], c=indices[i+2];
    bool cont = even ? (a==lastA&&b==lastC) : (a==lastC&&b==lastB);
    if(cont){ emit.element(emit.ctx,c); even=!even; }
    else { emit.end(emit.ctx); emit.begin(emit.ctx); emit.element(emit.ctx,a); emit.element(emit.ctx,b); emit.element(emit.ctx,c); even=false; }
    lastA=a; lastB=b; lastC=c;
  }
  emit.end(emit.ctx);
}
