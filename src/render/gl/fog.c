/* Port of src/render/gl/fog.ts (reduced). GPL-2.0-or-later. */
#include "qa/render/gl/fog.h"
#include <stdlib.h>
#include <string.h>
typedef struct fimpl { qa_gl *gl; uint32_t texture; bool closed; } fimpl;
bool qa_q2_fog_pass_init(qa_q2_fog_pass *pass,const qa_gl_context *context,qa_gl *gl){
  (void)context; pass->impl=calloc(1,sizeof(fimpl)); if(!pass->impl)return false;
  fimpl *p=(fimpl*)pass->impl; p->gl=gl; gl->glGenTextures(1,&p->texture); return true;
}
void qa_q2_fog_pass_draw(qa_q2_fog_pass *pass,qa_q2_fog_operation operation,double tw,double th){
  (void)pass; (void)operation; (void)tw; (void)th;
  /* Reduced: see header. CPU fog (qa_apply_q2_depth_fog) is exact. */
}
void qa_q2_fog_pass_close(qa_q2_fog_pass *pass){ if(!pass->impl)return; fimpl *p=(fimpl*)pass->impl;
  if (p->texture) p->gl->glDeleteTextures(1, &p->texture);
  free(p); pass->impl = NULL; }
