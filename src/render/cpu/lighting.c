/* Port of src/render/cpu/lighting.ts. GPL-2.0-or-later. */
#include "qa/render/cpu/lighting.h"
#include "qa/materials/q2_lighting.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <string.h>
qa_cpu_lighting qa_cull_triangle_lights(qa_cpu_lighting lighting, const qa_vec3 pos[3], qa_q2_fragment_light *storage){
  qa_batch_lighting *p=&lighting.parameters;
  if(strcmp(p->kind,"q2-world")||p->light_count==0) return lighting;
  double minX=fmin(pos[0].x,fmin(pos[1].x,pos[2].x)),maxX=fmax(pos[0].x,fmax(pos[1].x,pos[2].x));
  double minY=fmin(pos[0].y,fmin(pos[1].y,pos[2].y)),maxY=fmax(pos[0].y,fmax(pos[1].y,pos[2].y));
  double minZ=fmin(pos[0].z,fmin(pos[1].z,pos[2].z)),maxZ=fmax(pos[0].z,fmax(pos[1].z,pos[2].z));
  size_t n=0;
  for(size_t i=0;i<p->light_count;i++){
    const qa_q2_fragment_light *l=&p->lights[i];
    if(l->scale==0||(l->color.x==0&&l->color.y==0&&l->color.z==0)) continue;
    double reach=l->radius+(l->cone==NULL?16:0);
    double dx=l->origin.x<minX?minX-l->origin.x:l->origin.x>maxX?l->origin.x-maxX:0;
    double dy=l->origin.y<minY?minY-l->origin.y:l->origin.y>maxY?l->origin.y-maxY:0;
    double dz=l->origin.z<minZ?minZ-l->origin.z:l->origin.z>maxZ?l->origin.z-maxZ:0;
    if(dx*dx+dy*dy+dz*dz<=reach*reach) storage[n++]=*l;
  }
  if(n==p->light_count) return lighting;
  p->lights=storage; p->light_count=n; return lighting;
}
qa_cpu_world_attributes qa_cpu_world_attributes_for(qa_batch_lighting lighting, double index){
  qa_cpu_world_attributes out; out.world_position.x=out.world_position.y=out.world_position.z=0; out.world_normal=out.world_position;
  if(!strcmp(lighting.kind,"vertex")) return out;
  long i=(long)index;
  if(i<0||(size_t)i>=lighting.world_position_count) qa_range_error("CPU fragment lighting arrays do not cover the vertex");
  out.world_position=lighting.world_positions[i];
  if(!strcmp(lighting.kind,"q2-world")){
    if((size_t)i>=lighting.normal_count) qa_range_error("CPU fragment lighting arrays do not cover the vertex");
    out.world_normal=lighting.normals[i];
  }
  return out;
}
static double bound(double v,double lo,double hi){ return fmin(hi,fmax(lo,v)); }
static double depth_sample(qa_depth_image_level im,double u,double v){
  double x=bound(floor(u*im.width),0,im.width-1),y=bound(floor(v*im.height),0,im.height-1);
  size_t idx=(size_t)(y*im.width+x);
  if(idx>=im.pixel_count) qa_range_error("CPU shadow atlas sample escaped its image");
  return im.pixels[idx];
}
double qa_cpu_shadow_visibility(qa_vec3 position,qa_vec3 origin,double radius,qa_q2_shadow_projection shadow,qa_q2_shadow_atlas atlas,qa_depth_image_level depth,bool model){
  if(!strcmp(shadow.kind,"none")) return 1;
  qa_vec4 rect=shadow.atlas_rect; double texel=atlas.texel_size;
  double baseX,baseY,lowX,lowY,highX,highY,cone_limit=0,axial=0,bias=0,pa=0,pb=0; bool is_cone=false;
  if(!strcmp(shadow.kind,"cone")){
    is_cone=true; const double *m=shadow.matrix.values;
    double clipW=m[3]*position.x+m[7]*position.y+m[11]*position.z+m[15]; if(clipW<=0)return 1;
    double x=(m[0]*position.x+m[4]*position.y+m[8]*position.z+m[12])/clipW;
    double y=(m[1]*position.x+m[5]*position.y+m[9]*position.z+m[13])/clipW;
    double z=(m[2]*position.x+m[6]*position.y+m[10]*position.z+m[14])/clipW;
    if(x<0||x>1||y<0||y>1||z>1)return 1;
    baseX=x*rect.z+rect.x;baseY=y*rect.w+rect.y;lowX=rect.x+texel;lowY=rect.y+texel;highX=rect.x+rect.z-texel;highY=rect.y+rect.w-texel;
    cone_limit=z-(model?0.0025:0.0005);
  } else {
    double x=position.x-origin.x,y=position.y-origin.y,z=position.z-origin.z;
    double ax=fabs(x),ay=fabs(y),az=fabs(z); double face,right,up;
    if(ax>=ay&&ax>=az){face=x>=0?0:1;right=x>=0?-y:y;up=z;axial=ax;}
    else if(ay>=az){face=y>=0?2:3;right=y>=0?x:-x;up=z;axial=ay;}
    else {face=z>=0?4:5;right=z>=0?y:-y;up=x;axial=az;}
    double nearp=atlas.near_plane; if(axial<=nearp)return 1;
    double farp=fmax(radius,nearp*2); pa=(farp+nearp)/(nearp-farp); pb=2*farp*nearp/(nearp-farp);
    double cw=rect.z/3,ch=rect.w/2,cx=rect.x+fmod(face,3)*cw,cy=rect.y+floor(face/3)*ch;
    baseX=cx+(right/axial*0.5+0.5)*cw;baseY=cy+(up/axial*0.5+0.5)*ch;
    lowX=cx+texel;lowY=cy+texel;highX=cx+cw-texel;highY=cy+ch-texel;
    bias=(model?5:1)+axial*(2/(cw/texel))*(model?6:2);
  }
  double lit=0;
  for(int y=0;y<2;y++)for(int x=0;x<2;x++){
    double sm=depth_sample(depth,bound(baseX+(x-0.5)*texel,lowX,highX),bound(baseY+(y-0.5)*texel,lowY,highY));
    bool vis=is_cone?(cone_limit<=sm):(axial-bias<=pb/(2*sm-1+pa));
    if(vis)lit++;
  }
  return lit*0.25;
}
static qa_vec3 alias_shade(qa_vec3 position,qa_vec4 vertex,double scale,const qa_q2_model_shadow_light *lights,size_t n,qa_q2_shadow_atlas atlas,qa_depth_image_level depth){
  double kr=1,kg=1,kb=1;
  for(size_t i=0;i<n;i++){
    const qa_q2_model_shadow_light *l=&lights[i];
    if(l->fraction.x==0&&l->fraction.y==0&&l->fraction.z==0)continue;
    double occ=1-qa_cpu_shadow_visibility(position,l->origin,l->radius,l->shadow,atlas,depth,true);
    kr-=l->fraction.x*occ;kg-=l->fraction.y*occ;kb-=l->fraction.z*occ;
  }
  qa_vec3 o;o.x=fmin(vertex.x*scale*fmax(kr,0),1);o.y=fmin(vertex.y*scale*fmax(kg,0),1);o.z=fmin(vertex.z*scale*fmax(kb,0),1);return o;
}
qa_cpu_sample qa_shade_q2_fragment(const qa_cpu_lighting *l,qa_vec3 p,qa_vec3 n,qa_vec4 v,const qa_cpu_sample *t){ qa_cpu_sample o;o.r=o.g=o.b=o.a=0; qa_shade_q2_fragment_into(&o,l,p,n,v,t); return o; }
void qa_shade_q2_fragment_into(qa_cpu_sample *out,const qa_cpu_lighting *lighting,qa_vec3 position,qa_vec3 normal,qa_vec4 vertex,const qa_cpu_sample *texel){
  const qa_batch_lighting *p=&lighting->parameters;
  if(!strcmp(p->kind,"vertex")){ out->r=vertex.x*texel->r;out->g=vertex.y*texel->g;out->b=vertex.z*texel->b;out->a=vertex.w*texel->a; return; }
  if(!strcmp(p->kind,"q2-world")){
    bool is_model=p->pass&&!strcmp(p->pass,"model"), is_tex=p->pass&&!strcmp(p->pass,"texture");
    double r=is_model?vertex.x:!is_tex?texel->r:texel->r*vertex.x;
    double g=is_model?vertex.y:!is_tex?texel->g:texel->g*vertex.y;
    double b=is_model?vertex.z:!is_tex?texel->b:texel->b*vertex.z;
    if(is_model&&p->has_shade_scale){
      if(p->atlas==NULL||lighting->depth==NULL) qa_range_error("CPU model shadows require their depth atlas");
      /* TS passes Q2FragmentLight[] here; fraction lives on that shape. */
      qa_vec3 shade; shade.x=vertex.x;shade.y=vertex.y;shade.z=vertex.z;
      double kr=1,kg=1,kb=1;
      for(size_t i=0;i<p->light_count;i++){ const qa_q2_fragment_light *l=&p->lights[i];
        if(l->fraction.x==0&&l->fraction.y==0&&l->fraction.z==0)continue;
        double occ=1-qa_cpu_shadow_visibility(position,l->origin,l->radius,l->shadow,*p->atlas,*lighting->depth,true);
        kr-=l->fraction.x*occ;kg-=l->fraction.y*occ;kb-=l->fraction.z*occ; }
      shade.x=fmin(vertex.x*p->shade_scale*fmax(kr,0),1);shade.y=fmin(vertex.y*p->shade_scale*fmax(kg,0),1);shade.z=fmin(vertex.z*p->shade_scale*fmax(kb,0),1);
      r=shade.x;g=shade.y;b=shade.z;
    }
    for(size_t i=0;i<p->light_count;i++){
      const qa_q2_fragment_light *l=&p->lights[i];
      if(l->scale==0||(l->color.x==0&&l->color.y==0&&l->color.z==0))continue;
      double dx=l->origin.x-position.x,dy=l->origin.y-position.y,dz=l->origin.z-position.z;
      double reach=l->radius+(l->cone==NULL?16:0);
      if(dx*dx+dy*dy+dz*dz>reach*reach)continue;
      qa_dynamic_light_sample sample; sample.origin=l->origin;sample.radius=l->radius;sample.color=l->color;sample.scale=l->scale;sample.cone=l->cone;
      qa_vec3 contrib; qa_calc_dynamic_light_contribution_into(&contrib,sample,position,normal);
      if(contrib.x==0&&contrib.y==0&&contrib.z==0)continue;
      double vis=1;
      if(strcmp(l->shadow.kind,"none")){
        if(p->atlas==NULL||lighting->depth==NULL) qa_range_error("CPU shadow light requires its depth atlas");
        vis=qa_cpu_shadow_visibility(position,l->origin,l->radius,l->shadow,*p->atlas,*lighting->depth,false);
      }
      r+=contrib.x*vis;g+=contrib.y*vis;b+=contrib.z*vis;
    }
    if(is_model){r*=texel->r;g*=texel->g;b*=texel->b;}
    if(p->pass&&!strcmp(p->pass,"material-lightmap")){r*=vertex.x;g*=vertex.y;b*=vertex.z;}
    out->r=r;out->g=g;out->b=b;out->a=(p->pass&&!strcmp(p->pass,"lightmap"))?1:texel->a*vertex.w; return;
  }
  if(lighting->depth==NULL) qa_range_error("CPU model shadows require their depth atlas");
  qa_vec3 shade=alias_shade(position,vertex,p->shade_scale,p->model_shadow_lights,p->model_shadow_light_count,p->atlas_value,*lighting->depth);
  out->r=texel->r*shade.x;out->g=texel->g*shade.y;out->b=texel->b*shade.z;out->a=texel->a*vertex.w;
}
qa_vec3 qa_cpu_interpolate_world(qa_vec3 a,qa_vec3 b,qa_vec3 c,double wa,double wb,double wc){ qa_vec3 o; qa_cpu_interpolate_world_into(&o,a,b,c,wa,wb,wc); return o; }
void qa_cpu_interpolate_world_into(qa_vec3 *o,qa_vec3 a,qa_vec3 b,qa_vec3 c,double wa,double wb,double wc){ o->x=a.x*wa+b.x*wb+c.x*wc;o->y=a.y*wa+b.y*wb+c.y*wc;o->z=a.z*wa+b.z*wb+c.z*wc; }
