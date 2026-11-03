/* Port of src/render/debug-graph.ts. GPL-2.0-or-later. */
#include "qa/render/debug_graph.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <string.h>
#include "qa/foundation/js.h"
static double f32(double v){ return (double)(float)v; }
qa_vec4 qa_debug_graph_color(qa_palette palette, double index){
  int32_t i=qa_js_to_int32(index); size_t offset=(size_t)(i&255)*3; /* index & 255 */
  if(offset+2>=palette.colors.length) qa_range_error("Incomplete debug graph palette");
  qa_vec4 c; c.x=palette.colors.bytes[offset]/255.0; c.y=palette.colors.bytes[offset+1]/255.0;
  c.z=palette.colors.bytes[offset+2]/255.0; c.w=1; return c;
}
void qa_source_debug_graph_init(qa_source_debug_graph *g){ memset(g,0,sizeof *g); }
void qa_source_debug_graph_add(qa_source_debug_graph *g, double value, double color){
  g->values[g->current]=(float)value; g->colors[g->current]=qa_js_to_int32(color); /* Int32Array store */
  g->current=(g->current+1)&1023;
}
void qa_source_debug_graph_add_frame(qa_source_debug_graph *g, double seconds, qa_debug_graph_settings s){
  if(s.timegraph!=0) qa_source_debug_graph_add(g, f32(f32(seconds)*300), 0);
}
void qa_source_debug_graph_add_network(qa_source_debug_graph *g, qa_debug_graph_packet p, qa_debug_graph_settings s){
  if(s.debuggraph!=0||s.timegraph!=0) return;
  for(double i=0;i<p.dropped;i++) qa_source_debug_graph_add(g,30,0x40);
  for(double i=0;i<p.suppressed;i++) qa_source_debug_graph_add(g,30,0xdf);
  qa_source_debug_graph_add(g, qa_js_min(30, trunc(p.ping_milliseconds/30)), 0xd0); /* Math.min keeps NaN */
}
size_t qa_source_debug_graph_bars(const qa_source_debug_graph *g, qa_rect rect, qa_debug_graph_settings s, qa_debug_graph_bar *out, size_t capacity){
  if(s.debuggraph==0&&s.timegraph==0&&s.netgraph==0) return 0;
  double height=trunc(s.height);
  if(!isfinite(s.height)||height<=0) return 0;
  double y=rect.y+rect.height; size_t n=0;
  if(n<capacity){ out[n].rect.x=rect.x; out[n].rect.y=y-s.height; out[n].rect.width=rect.width; out[n].rect.height=s.height; out[n].color=8; } n++;
  for(double a=0;a<rect.width;a++){
    int i=qa_js_to_int32((double)g->current-1-a+1024)&1023; double value=g->values[i];
    double v=f32(f32(value*f32(s.scale))+f32(s.shift));
    if(v<0) v=f32(v+f32(s.height*(1+trunc(-v/s.height))));
    double h=fmod(trunc(v),height);
    if(n<capacity){ out[n].rect.x=rect.x+rect.width-1-a; out[n].rect.y=y-h; out[n].rect.width=1; out[n].rect.height=h; out[n].color=g->colors[i]; } n++;
  }
  return n;
}
void qa_source_debug_graph_draw(const qa_source_debug_graph *g, qa_debug_graph_fill_fn fill, void *fill_ctx, qa_rect rect, qa_debug_graph_settings s, qa_debug_graph_palette_color_fn pc, void *pctx, const qa_picture_asset *white){
  /* Bars are recomputed inline to avoid a heap array (same order/values). */
  if(s.debuggraph==0&&s.timegraph==0&&s.netgraph==0) return;
  double height=trunc(s.height); if(!isfinite(s.height)||height<=0) return;
  double y=rect.y+rect.height;
  qa_rect bg; bg.x=rect.x; bg.y=y-s.height; bg.width=rect.width; bg.height=s.height;
  fill(fill_ctx,bg,pc(pctx,8),white);
  for(double a=0;a<rect.width;a++){
    int i=qa_js_to_int32((double)g->current-1-a+1024)&1023; double value=g->values[i];
    double v=f32(f32(value*f32(s.scale))+f32(s.shift));
    if(v<0) v=f32(v+f32(s.height*(1+trunc(-v/s.height))));
    double h=fmod(trunc(v),height); qa_rect r; r.x=rect.x+rect.width-1-a; r.y=y-h; r.width=1; r.height=h;
    fill(fill_ctx,r,pc(pctx,g->colors[i]),white);
  }
}
