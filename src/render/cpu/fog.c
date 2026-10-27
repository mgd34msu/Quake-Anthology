/* Port of src/render/cpu/fog.ts. GPL-2.0-or-later. */
#include "qa/render/cpu/fog.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <string.h>
static double f32(double v){ return (double)(float)v; }
double qa_eye_depth_from_window_depth(qa_mat4 p,double wd,const double dr[2]){
  double n=(wd-dr[0])/(dr[1]-dr[0])*2-1;
  return fabs((p.values[14]-n*p.values[15])/(n*p.values[11]-p.values[10]));
}
double qa_eye_depth_from_window_depth_default(qa_mat4 p,double wd){ const double dr[2]={0,1}; return qa_eye_depth_from_window_depth(p,wd,dr); }
void qa_apply_q1_depth_fog(qa_cpu_framebuffer *frame,qa_rect vp,qa_mat4 proj,qa_q1_fog fog,double sky_fraction,const char *output,double clear_depth){
  if(!strcmp(output,"classic-indexed")||fog.density<=0)return;
  if(proj.values[2]!=0||proj.values[6]!=0||proj.values[3]!=0||proj.values[7]!=0) qa_range_error("Q1 fog requires a projection whose depth is independent of eye X/Y");
  double sky=qa_cpu_clamp01(sky_fraction);
  for(double y=fmax(0,vp.y);y<fmin(frame->height,vp.y+vp.height);y++)
   for(double x=fmax(0,vp.x);x<fmin(frame->width,vp.x+vp.width);x++){
    double index=(y-frame->origin_y)*frame->stride+x-frame->origin_x; long ii=(long)index;
    if(ii<0||(size_t)ii>=frame->storage_count) qa_range_error("Q1 fog rectangle lies outside its framebuffer storage");
    double depth=frame->depth[ii]; size_t off=(size_t)ii*4;
    qa_vec3 color; color.x=frame->pixels[off]/255.0;color.y=frame->pixels[off+1]/255.0;color.z=frame->pixels[off+2]/255.0;
    qa_vec3 result;
    if(depth==clear_depth){ result.x=color.x+(fog.color.x-color.x)*sky;result.y=color.y+(fog.color.y-color.y)*sky;result.z=color.z+(fog.color.z-color.z)*sky; }
    else result=qa_q1_fog_color(color,fog,qa_eye_depth_from_window_depth_default(proj,depth));
    frame->pixels[off]=(uint8_t)qa_cpu_byte(result.x);frame->pixels[off+1]=(uint8_t)qa_cpu_byte(result.y);frame->pixels[off+2]=(uint8_t)qa_cpu_byte(result.z);
   }
}
static void blend_fog(qa_cpu_framebuffer *frame,double offset_d,double r,double g,double b,double amount,double alpha_bits){
  long off=(long)offset_d; if(off<0||(size_t)(off+3)>=frame->storage_count*4) qa_range_error("Q2 fog rectangle lies outside its framebuffer storage");
  double factor=qa_cpu_clamp01(amount),inv=1-factor;
  double pr=frame->pixels[off],pg=frame->pixels[off+1],pb=frame->pixels[off+2],pa=frame->pixels[off+3];
  frame->pixels[off]=(uint8_t)qa_cpu_byte(qa_cpu_clamp01(r)*factor+pr/255*inv);
  frame->pixels[off+1]=(uint8_t)qa_cpu_byte(qa_cpu_clamp01(g)*factor+pg/255*inv);
  frame->pixels[off+2]=(uint8_t)qa_cpu_byte(qa_cpu_clamp01(b)*factor+pb/255*inv);
  frame->pixels[off+3]=alpha_bits==0?255:(uint8_t)qa_cpu_byte(factor*factor+pa/255*inv);
}
void qa_apply_q2_depth_fog(qa_cpu_framebuffer *frame,qa_q2_fog_operation op,double alpha_bits){
  qa_scene_camera camera=op.camera; qa_scene_fog fog=op.fog; qa_rect vp=camera.viewport;
  int global=fog.density>0, height=fog.height.density>0&&fog.height.falloff>0, sky=op.sky_drawn&&fog.sky_factor>0;
  if(!global&&!height&&!sky)return;
  const double *pr=camera.projection.values;
  if(pr[1]!=0||pr[2]!=0||pr[3]!=0||pr[4]!=0||pr[6]!=0||pr[7]!=0||pr[8]!=0||pr[9]!=0||pr[12]!=0||pr[13]!=0||pr[11]!=-1||pr[15]!=0||pr[0]==0||pr[5]==0) qa_range_error("Q2 fog requires the source symmetric perspective projection");
  qa_vec3 fwd=camera.axis.vectors[0],left=camera.axis.vectors[1],up=camera.axis.vectors[2],origin=camera.origin;
  double a=f32(-pr[10]),b=f32(-pr[14]),tanX=f32(1/pr[0]),tanY=f32(1/pr[5]);
  double density=f32(fog.density/64),threshold=f32(op.far_depth);
  for(double y=fmax(0,vp.y);y<fmin(frame->height,vp.y+vp.height);y++){
    double ndcY=1-(y+0.5-vp.y)*2/vp.height;
    for(double x=fmax(0,vp.x);x<fmin(frame->width,vp.x+vp.width);x++){
      double index=(y-frame->origin_y)*frame->stride+x-frame->origin_x; long ii=(long)index;
      if(ii<0||(size_t)ii>=frame->storage_count) qa_range_error("Q2 fog rectangle lies outside its depth storage");
      double depth=f32(frame->depth[ii]); double off=index*4;
      if(depth>=threshold){ if(sky)blend_fog(frame,off,fog.color.x,fog.color.y,fog.color.z,fog.sky_factor,alpha_bits); continue; }
      double eyeW=b/(a-(2*depth-1)),frag=depth*eyeW;
      if(global)blend_fog(frame,off,fog.color.x,fog.color.y,fog.color.z,qa_global_fog_amount(density,frag),alpha_bits);
      if(height){
        double ndcX=(x+0.5-vp.x)*2/vp.width-1;
        double rx=fwd.x-left.x*ndcX*tanX+up.x*ndcY*tanY, ry=fwd.y-left.y*ndcX*tanX+up.y*ndcY*tanY, rz=fwd.z-left.z*ndcX*tanX+up.z*ndcY*tanY;
        double wx=origin.x+rx*eyeW,wy=origin.y+ry*eyeW,wz=origin.z+rz*eyeW;
        double dist=sqrt((wx-origin.x)*(wx-origin.x)+(wy-origin.y)*(wy-origin.y)+(wz-origin.z)*(wz-origin.z));
        double dirz=qa_height_fog_dir_z(dist==0?0:(wz-origin.z)/dist);
        qa_q2_height_fog hf=fog.height;
        double ext=qa_height_fog_extinction(origin.z,wz,hf.start.distance,hf.falloff,dirz);
        double fr=qa_height_fog_fraction(wz,hf.start.distance,hf.end.distance);
        blend_fog(frame,off,(hf.start.color.x+(hf.end.color.x-hf.start.color.x)*fr)*ext,(hf.start.color.y+(hf.end.color.y-hf.start.color.y)*fr)*ext,(hf.start.color.z+(hf.end.color.z-hf.start.color.z)*fr)*ext,qa_height_fog_amount(hf.density,frag,ext),alpha_bits);
      }
    }
  }
}
