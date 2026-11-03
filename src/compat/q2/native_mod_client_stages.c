/* Port of src/compat/q2/native-mod-client-stages.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_client_stages.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_mod_host_api.h"
#include "qa/foundation/js.h"
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
  /* DataView setUint8/setInt16: ToUint8/ToInt16 (aim shorts may be any safe integer). */
  view[0]=qa_js_to_uint8(c.milliseconds); view[1]=qa_js_to_uint8(c.buttons);
  for(int i=0;i<3;i++) put16(view,2+(size_t)i*2,qa_js_to_int16(c.angle_shorts.values[i]));
  put16(view,8,qa_js_to_int16(c.forward_move)); put16(view,10,qa_js_to_int16(c.side_move)); put16(view,12,qa_js_to_int16(c.up_move));
  view[14]=qa_js_to_uint8(c.impulse); view[15]=qa_js_to_uint8(c.light_level);
}
size_t qa_compat_q2_native_mod_user_command(uint8_t *out, bool rerelease, double frame, double elapsed_ms, qa_vec3 aim, qa_compat_q2_mod_command_inputs in, void *rr_ctx, void (*write_rerelease)(void *, uint8_t *, size_t, qa_q2_rerelease_user_command)) {
  double vals[6]={in.attack,in.jump,in.impulse,in.forward_move,in.side_move,in.up_move};
  for(int i=0;i<6;i++) if(!in.has[i]||!isfinite(vals[i])) qa_range_error("Missing native command input");
  double ms=qa_js_round(elapsed_ms);
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

struct qa_compat_q2_native_mod_stage_scope {
  const qa_native_mod_source_call *call; uint64_t *handles; size_t handle_count;
  void *extra_ctx; void (*extra)(void *); void (*extra_remove)(void *); bool extra_bound;
};
typedef qa_compat_q2_native_mod_stage_scope stage_scope;
typedef struct stage_call { qa_compat_q2_native_mod_stage_runner *s; stage_scope *scope, *previous; void *ctx; void (*invoke)(void *); } stage_call;
void qa_compat_q2_native_mod_stage_runner_init(qa_compat_q2_native_mod_stage_runner *s, const qa_compat_q2_native_mod_host *host) { s->host = host; s->active = NULL; }
static void skip_region(void *ctx, qa_compat_q2_inline_continuation *continuation) { (void)ctx; continuation->skip(continuation->ctx); }
static void stage_bind(qa_compat_q2_native_mod_stage_runner *s, stage_scope *scope) {
  const qa_compat_q2_native_mod_host *host = s->host;
  for (size_t i = 0; i < scope->call->skip_count; i++) {
    const qa_native_mod_skip *region = &scope->call->skips[i];
    scope->handles[scope->handle_count++] = qa_compat_q2_native_mod_bind_region(host, qa_compat_q2_native_mod_image_rva(host, region->entry), qa_compat_q2_native_mod_image_rva(host, region->join), NULL, skip_region, NULL, NULL);
  }
  if (scope->extra != NULL) { scope->extra(scope->extra_ctx); scope->extra_bound = true; }
}
static void stage_unbind(qa_compat_q2_native_mod_stage_runner *s, stage_scope *scope) {
  if (scope == NULL) return;
  if (scope->extra_bound) { scope->extra_bound = false; scope->extra_remove(scope->extra_ctx); }
  while (scope->handle_count > 0) qa_compat_q2_native_mod_unbind_region(s->host, scope->handles[--scope->handle_count]);
}
static void stage_body(void *ctx) { stage_call *c = ctx; stage_bind(c->s, c->scope); c->invoke(c->ctx); }
static void stage_cleanup(void *ctx) {
  stage_call *c = ctx; stage_unbind(c->s, c->scope); c->s->active = c->previous;
  if (c->previous != NULL) stage_bind(c->s, c->previous);
  free(c->scope->handles);
}
void qa_compat_q2_native_mod_stage_runner_run(qa_compat_q2_native_mod_stage_runner *s, const qa_native_mod_source_call *call, void *invoke_ctx, void (*invoke)(void *),
    void *extra_ctx, void (*extra)(void *), void (*extra_remove)(void *)) {
  stage_scope scope; memset(&scope, 0, sizeof scope);
  scope.call = call; scope.extra_ctx = extra_ctx; scope.extra = extra; scope.extra_remove = extra_remove;
  scope.handles = calloc(call->skip_count == 0 ? 1 : call->skip_count, sizeof *scope.handles); if (scope.handles == NULL) qa_range_error("Out of memory");
  stage_call c = {s, &scope, s->active, invoke_ctx, invoke};
  stage_unbind(s, s->active); s->active = &scope;
  qa_compat_q2_native_mod_finally(stage_body, &c, stage_cleanup, &c);
}
