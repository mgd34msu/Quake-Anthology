/* Port of src/render/gl/object-opacity.ts (reduced). GPL-2.0-or-later. */
#include "qa/render/gl/object_opacity.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
typedef struct oimpl { qa_gl *gl; qa_gl_precision precision; bool active; } oimpl;
bool qa_gl_object_opacity_init(qa_gl_object_opacity *o,const qa_gl_context *context,qa_gl *gl,qa_gl_precision precision){
  (void)context; o->impl=calloc(1,sizeof(oimpl)); if(!o->impl)return false;
  ((oimpl*)o->impl)->gl=gl; ((oimpl*)o->impl)->precision=precision; return true;
}
bool qa_gl_object_opacity_bind(qa_gl_object_opacity *o){ (void)o; return false; }
void qa_gl_object_opacity_draw(qa_gl_object_opacity *o,double value,double width,double height,qa_gl_opacity_draw_fn draw,void *ctx){
  (void)width;(void)height; oimpl *p=(oimpl*)o->impl;
  if(!isfinite(value)||value<0||value>1) qa_range_error("Object opacity must be in 0..1");
  if(p->active) qa_range_error("Object opacity scopes cannot nest");
  if(value==0)return;
  p->active=true; draw(ctx); p->active=false;
}
void qa_gl_object_opacity_close(qa_gl_object_opacity *o){ free(o->impl); o->impl=NULL; }
