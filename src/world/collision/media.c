/* Port of src/world/collision/media.ts. GPL-2.0-or-later. */
#include "qa/world/collision/media.h"
#include "qa/world/collision/contents.h"
#include <stdlib.h>
#include "qa/foundation/js.h"
typedef struct{double s,e;} Iv;
static int iv_cmp(const void*a,const void*b){double d=((const Iv*)a)->s-((const Iv*)b)->s;return d<0?-1:d>0?1:0;}
static int uncovered(Iv iv,const Iv*occ,size_t n){
  if(iv.s==iv.e){for(size_t i=0;i<n;i++)if(occ[i].s<=iv.s&&occ[i].e>=iv.e)return 0;return 1;}
  double pos=iv.s;
  for(size_t i=0;i<n;i++){if(occ[i].e<pos)continue;if(occ[i].s>pos)return 1;pos=qa_js_max(pos,occ[i].e);if(pos>=iv.e)return 0;}
  return pos<iv.e;
}
#define NDIV(a,b) numeric->divide(numeric->ctx,(a),(b))
#define NSUB(a,b) numeric->subtract(numeric->ctx,(a),(b))
qa_trace_media qa_trace_brush_media(const qa_medium_brush*brushes,size_t bc,qa_collision_family family,qa_media_distance_fn distance,void*dctx,const qa_numeric_operations*numeric,double fraction){
  Iv*solids=malloc((bc?bc:1)*sizeof* solids); Iv*liquids=malloc((bc?bc:1)*sizeof*liquids);
  if(!solids||!liquids)abort();
  size_t ns=0,nl=0;
  for(size_t bi=0;bi<bc;bi++){
    double contents=qa_convert_contents(brushes[bi].contents,family,"q1");
    if(contents==-1)continue;
    double start=0,end=fraction; int outside=0;
    for(size_t pi=0;pi<brushes[bi].plane_count;pi++){
      double first=distance(dctx,brushes[bi].planes[pi],0),last=distance(dctx,brushes[bi].planes[pi],1);
      if(first>0&&last>0){outside=1;break;}
      if(first<=0&&last<=0)continue;
      double crossing=NDIV(first,NSUB(first,last));
      /* Math.max/Math.min: a NaN crossing propagates. */
      if(first>last)start=qa_js_max(start,crossing);else end=qa_js_min(end,crossing);
      if(start>end){outside=1;break;}
    }
    if(!outside&&brushes[bi].plane_count>0){Iv iv={start,end}; if(contents==-2)solids[ns++]=iv; else liquids[nl++]=iv;}
  }
  qa_js_sort(solids,ns,sizeof* solids,iv_cmp);
  size_t no=ns+nl; Iv*occ=malloc((no?no:1)*sizeof*occ); if(!occ)abort();
  for(size_t i=0;i<ns;i++){occ[i]=solids[i];}
  for(size_t i=0;i<nl;i++){occ[ns+i]=liquids[i];}
  qa_js_sort(occ,no,sizeof*occ,iv_cmp);
  Iv whole={0,fraction};
  qa_trace_media r; r.in_open=uncovered(whole,occ,no)!=0; r.in_water=false;
  for(size_t i=0;i<nl;i++)if(uncovered(liquids[i],solids,ns)){r.in_water=true;break;}
  free(solids);free(liquids);free(occ); return r;
}
