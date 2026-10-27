/* Port of src/render/gl/output-gamma.ts (reduced). GPL-2.0-or-later. */
#include "qa/render/gl/output_gamma.h"
#include <stdlib.h>
#include <string.h>
typedef struct gimpl { qa_gl *gl; uint8_t table[256]; bool has_table; } gimpl;
bool qa_gl_output_gamma_init(qa_gl_output_gamma *g,const qa_gl_context *context,qa_gl *gl,qa_gl_precision precision){
  (void)context;(void)precision; g->impl=calloc(1,sizeof(gimpl)); if(!g->impl)return false;
  ((gimpl*)g->impl)->gl=gl; return true;
}
void qa_gl_output_gamma_update(qa_gl_output_gamma *g,const uint8_t table[256]){ gimpl *p=(gimpl*)g->impl; memcpy(p->table,table,256); p->has_table=true; }
bool qa_gl_output_gamma_bind(qa_gl_output_gamma *g,double buffer,double w,double h){ (void)g;(void)buffer;(void)w;(void)h; return false; }
void qa_gl_output_gamma_select_default(qa_gl_output_gamma *g,double buffer){ (void)g;(void)buffer; }
void qa_gl_output_gamma_finish(qa_gl_output_gamma *g,double buffer){ (void)g;(void)buffer; }
void qa_gl_output_gamma_restore(qa_gl_output_gamma *g,double buffer){ (void)g;(void)buffer; }
void qa_gl_output_gamma_close(qa_gl_output_gamma *g){ free(g->impl); g->impl=NULL; }
