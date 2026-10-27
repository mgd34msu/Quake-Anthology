/* Port of src/compat/q2/native-mod-client-stages.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_client_stages.h"
#include <math.h>
#include <string.h>
void qa_compat_q2_native_mod_client_stages_init(qa_compat_q2_native_mod_client_stages *s, void *host_ctx) { s->host_ctx=host_ctx; s->active=false; }
static void put16(uint8_t *v,size_t o,int16_t x){ v[o]=(uint8_t)x; v[o+1]=(uint8_t)((uint16_t)x>>8); }
void qa_compat_q2_write_native_user_command(uint8_t *view, size_t len, qa_user_command command, void *rr_ctx, void (*write_rerelease)(void *, uint8_t *, size_t, qa_q2_rerelease_user_command)) {
  if (command.kind==QA_USER_COMMAND_Q2_RERELEASE) {
    qa_q2_rerelease_user_command c=command.as.q2_rerelease;
    double vs[5]={c.angles.x,c.angles.y,c.angles.z,c.forward_move,c.side_move};
    for(int i=0;i<5;i++) if(!isfinite((double)(float)vs[i])) qa_range_error("Native input output exceeds the float32 command ABI");
    if(!write_rerelease) qa_range_error("Native input output exceeds the float32 command ABI");
    write_rerelease(rr_ctx,view,len,c); return;
  }
  qa_q2_user_command c=command.as.q2;
  for(int i=0;i<3;i++) { double t=trunc(c.angle_shorts.values[i]); if(t!=c.angle_shorts.values[i]&&false) (void)t; if(!isfinite(t)||t<-9007199254740991.0||t>9007199254740991.0) qa_range_error("Native input aim exceeds the integer command ABI"); }
  double mv[3]={c.forward_move,c.side_move,c.up_move};
  for(int i=0;i<3;i++) if(!isfinite(mv[i])||mv[i]<-32768||mv[i]>32767) qa_range_error("Native input movement exceeds the int16 command ABI");
  if(len<16) qa_range_error("Native input movement exceeds the int16 command ABI");
  view[0]=(uint8_t)c.milliseconds; view[1]=(uint8_t)c.buttons;
  for(int i=0;i<3;i++) put16(view,2+(size_t)i*2,(int16_t)c.angle_shorts.values[i]);
  put16(view,8,(int16_t)c.forward_move); put16(view,10,(int16_t)c.side_move); put16(view,12,(int16_t)c.up_move);
  view[14]=(uint8_t)c.impulse; view[15]=(uint8_t)c.light_level;
}
size_t qa_compat_q2_native_mod_user_command(uint8_t *out, bool rerelease, double frame, double elapsed_ms, qa_vec3 aim, qa_compat_q2_mod_command_inputs in, void *rr_ctx, void (*write_rerelease)(void *, uint8_t *, size_t, qa_q2_rerelease_user_command)) {
  double vals[6]={in.attack,in.jump,in.impulse,in.forward_move,in.side_move,in.up_move};
  for(int i=0;i<6;i++) if(!in.has[i]||!isfinite(vals[i])) qa_range_error("Missing native command input");
  double ms=round(elapsed_ms);
  if(ms<0||ms>255) qa_range_error("Native command interval exceeds its original byte ABI");
  size_t len=rerelease?28:16; memset(out,0,len);
  double fwd=in.forward_move*200, side=in.side_move*200, up=in.up_move*200;
  qa_user_command cmd; memset(&cmd,0,sizeof cmd);
  if(rerelease){ cmd.kind=QA_USER_COMMAND_Q2_RERELEASE; cmd.as.q2_rerelease.milliseconds=ms; cmd.as.q2_rerelease.angles=aim;
    cmd.as.q2_rerelease.buttons=(double)((in.attack!=0?1:0)|(in.jump!=0?8:0)|(up<0?16:0)); cmd.as.q2_rerelease.forward_move=fwd; cmd.as.q2_rerelease.side_move=side; cmd.as.q2_rerelease.server_frame=frame; }
  else { cmd.kind=QA_USER_COMMAND_Q2; cmd.as.q2.milliseconds=ms; cmd.as.q2.buttons=in.attack!=0?1:0;
    cmd.as.q2.angle_shorts.values[0]=aim.x*65536/360; cmd.as.q2.angle_shorts.values[1]=aim.y*65536/360; cmd.as.q2.angle_shorts.values[2]=aim.z*65536/360;
    cmd.as.q2.forward_move=fwd; cmd.as.q2.side_move=side; cmd.as.q2.up_move=in.jump!=0?(up>200?up:200):up; cmd.as.q2.impulse=in.impulse; cmd.as.q2.light_level=0; }
  qa_compat_q2_write_native_user_command(out,len,cmd,rr_ctx,write_rerelease); return len;
}
