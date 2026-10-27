/* Port of src/world/collision/contents.ts. GPL-2.0-or-later. */
#include "qa/world/collision/contents.h"
#include <stdint.h>
#include <string.h>
static int fam(qa_collision_family f){return f&&strcmp(f,"q1")==0?1:f&&strcmp(f,"q2")==0?2:3;}
static int32_t bits32(double v){return (int32_t)(uint32_t)v;}
double qa_convert_contents(double contents,qa_collision_family from,qa_collision_family to){
  int F=fam(from),T=fam(to);
  if(F==T)return contents;
  if(F==1){
    int64_t c=(int64_t)contents;
    switch(c){
      case -2:return 1; case -3:return 32; case -4:return 16; case -5:return 8; case -6:return 1;
      case -9:return 32+(T==2?0x40000:0); case -10:return 32+(T==2?0x80000:0);
      case -11:return 32+(T==2?0x100000:0); case -12:return 32+(T==2?0x200000:0);
      case -13:return 32+(T==2?0x400000:0); case -14:return 32+(T==2?0x800000:0);
      default:return 0;
    }
  }
  if(T==1){
    uint32_t c=(uint32_t)bits32(contents);
    if((c&(F==2?0xe2010003u:0x02010001u))!=0)return -2;
    if((c&8u)!=0){return -5;}
    if((c&16u)!=0){return -4;}
    if((c&32u)!=0){return -3;}
    return -1;
  }
  uint32_t c=(uint32_t)bits32(contents);
  uint32_t r=c&0x0f038079u;
  if(F==2){if(c&2u)r|=1u; if(c&0xc0000000u)r|=0x02000000u; if(c&0x10000000u)r|=0x20000000u;}
  else{if(c&0x20000000u)r|=0x10000000u; if(c&0x02000000u)r|=0x40000000u;}
  return (double)(int32_t)r;
}
double qa_actor_contents(qa_actor_collision col,qa_collision_family to){
  int ismodel=col.shape_kind&&strcmp(col.shape_kind,"model")==0;
  if(fam(col.family)==1&&!ismodel&&fam(to)!=1)return col.dead_monster?0x04000000:0x02000000;
  if(col.dead_monster&&!ismodel&&fam(to)==1)return -1;
  return qa_convert_contents(col.contents,col.family,to);
}
bool qa_contents_block(double contents,qa_collision_family family,qa_trace_policy policy){
  if(policy.kind&&strcmp(policy.kind,"q1")==0)return qa_convert_contents(contents,family,"q1")==-2;
  double cv=qa_convert_contents(contents,family,policy.kind);
  return ((uint32_t)bits32(cv)&(uint32_t)bits32(policy.contents_mask))!=0;
}
bool qa_blocks_q1_contents(double c,qa_trace_policy p){return qa_contents_block(c,"q1",p);}
double qa_geometry_mask(qa_trace_policy policy,qa_collision_family family){
  int32_t mask=0;
  for(int bit=0;bit<32;bit++){int32_t flag=(int32_t)(1u<<bit); if(bit==31)flag=(-2147483647-1);
    if(qa_contents_block((double)flag,family,policy))mask|=flag;}
  return (double)mask;
}
static qa_bsp_plane plane_with_type(qa_trace_result r){
  if(!(r.kind&&strcmp(r.kind,"q1")==0))return r.source_plane;
  qa_vec3 n=r.contact.has_plane?r.contact.plane.normal:r.source_plane_q1.normal;
  /* TS q1 sourcePlane is Plane (distance only); contract stores it in source_plane_q1 */
  qa_bsp_plane p; p.plane.normal=n; p.plane.distance=r.source_plane_q1.distance;
  p.type=n.x==1?0:n.y==1?1:n.z==1?2:3;
  p.signbits=(n.x<0?1:0)|((n.y<0?1:0)<<1)|((n.z<0?1:0)<<2);
  return p;
}
static qa_q2_surface_info sky_surf={"sky",4,0,""};
static qa_q2_surface_info empty_surf={"",0,0,""};
static qa_q2_surface_info q3_surf={"",0,0,""};
qa_trace_result qa_adapt_trace_result_ex(qa_trace_result r,qa_trace_policy policy,bool has_q1c,double q1c){
  if(r.kind&&policy.kind&&strcmp(r.kind,policy.kind)==0)return r;
  qa_trace_result o=r;
  int rk=r.kind&&strcmp(r.kind,"q1")==0?1:r.kind&&strcmp(r.kind,"q2")==0?2:3;
  double native=rk==1?(has_q1c?q1c:(r.start_solid||r.hit.kind!=QA_TRACE_HIT_NONE?-2:-1)):r.contents;
  /* contract trace has no q1 contents field; contents member carries it when set by Q1Collision port */
  if(rk==1&&!has_q1c&&r.contents!=0)native=r.contents;
  double contents=qa_convert_contents(native,r.kind,policy.kind);
  qa_bsp_plane sp=plane_with_type(r);
  o.kind=policy.kind;
  if(policy.kind&&strcmp(policy.kind,"q1")==0){
    double sf=rk==2?(r.surface?r.surface->flags:0):rk==3?(r.has_surface_flags?r.surface_flags:0):0;
    if(rk==2&&(((uint32_t)bits32(r.contents))&0x20000000u))sf=(double)(bits32(sf)|8);
    o.has_surface_flags=true;o.surface_flags=sf;o.in_open=!r.all_solid;
    o.in_water=((bits32(native)&56)!=0);o.source_plane_q1=sp.plane;o.source_plane=sp;o.contents=native;
    return o;
  }
  if(policy.kind&&strcmp(policy.kind,"q2")==0){
    int sky=rk==1&&(native==-6||(r.has_surface_flags&&((bits32(r.surface_flags)&4)!=0)));
    if(rk==3){q3_surf.flags=qa_convert_surface_flags(r.has_surface_flags?r.surface_flags:0,"q3","q2");o.surface=&q3_surf;}
    else if(sky)o.surface=&sky_surf;
    else o.surface=r.hit.kind!=QA_TRACE_HIT_NONE?&empty_surf:NULL;
    double ladder=(rk==3&&r.has_surface_flags&&((bits32(r.surface_flags)&8)!=0))?0x20000000:0;
    o.contents=(double)(bits32(contents)|bits32(ladder));o.source_plane=sp;o.has_secondary=false;o.secondary_surface=NULL;
    return o;
  }
  /* q3 */
  double sf=0;
  if(rk==2&&r.surface)sf=qa_convert_surface_flags(r.surface->flags,"q2","q3");
  else if(rk==1&&(native==-6||(r.has_surface_flags&&((bits32(r.surface_flags)&4)!=0))))sf=4|16;
  if(rk==2&&(((uint32_t)bits32(r.contents))&0x20000000u))sf=(double)(bits32(sf)|8);
  o.contents=contents;o.has_surface_flags=true;o.surface_flags=sf;o.source_plane=sp;
  return o;
}
qa_trace_result qa_adapt_trace_result(qa_trace_result r,qa_trace_policy p){return qa_adapt_trace_result_ex(r,p,false,0);}
qa_point_contents_result qa_adapt_point_contents(qa_point_contents_result r,qa_trace_policy policy){
  if(r.kind&&policy.kind&&strcmp(r.kind,policy.kind)==0)return r;
  double src=(r.kind&&strcmp(r.kind,"q2")==0)?r.merged:r.contents;
  double c=qa_convert_contents(src,r.kind,policy.kind);
  qa_point_contents_result o={0};
  if(policy.kind&&strcmp(policy.kind,"q2")==0){o.kind="q2";o.stored=c;o.merged=c;o.contents=c;return o;}
  o.kind=policy.kind;o.contents=c;return o;
}
double qa_convert_surface_flags(double flags,qa_collision_family from,qa_collision_family to){
  int F=fam(from),T=fam(to);
  if(F==T)return flags;
  if(F==1||T==1)return 0;
  int32_t f=bits32(flags);
  return (double)((f&0x86)|(F==2&&T==3&&((f&4)!=0)?16:0));
}
