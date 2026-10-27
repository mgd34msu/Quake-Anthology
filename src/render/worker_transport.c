/* Port of src/render/worker-transport.ts (direct/pumped). GPL-2.0-or-later. */
#include "qa/render/worker_transport.h"
#include <stdlib.h>
#include <string.h>
typedef struct timpl { qa_render_worker_runtime runtime; bool has_runtime; bool closed; } timpl;
static qa_render_worker_initialize_fn g_factory; static void *g_factory_ctx;
void qa_serve_render_worker(qa_render_worker_initialize_fn initialize,void *ctx){ g_factory=initialize; g_factory_ctx=ctx; }
bool qa_render_worker_transport_open(qa_render_worker_transport *t,qa_render_worker_initialize_fn initialize,void *ictx,const void *payload,size_t payload_size,qa_render_worker_host host){
  t->impl=calloc(1,sizeof(timpl)); if(!t->impl)return false;
  timpl *p=(timpl*)t->impl;
  if(!initialize(ictx,payload,payload_size,host,&p->runtime)){ free(p); t->impl=NULL; return false; }
  p->has_runtime=true; return true;
}
bool qa_render_worker_transport_open_default(qa_render_worker_transport *t,const void *payload,size_t payload_size,qa_render_worker_host host){
  if(g_factory==NULL)return false;
  return qa_render_worker_transport_open(t,g_factory,g_factory_ctx,payload,payload_size,host);
}
bool qa_render_worker_transport_request(qa_render_worker_transport *t,qa_worker_request request,qa_worker_response *out){
  timpl *p=(timpl*)t->impl; if(!p||p->closed||!p->has_runtime)return false;
  return qa_render_worker_runtime_handle(&p->runtime,request,out);
}
void qa_render_worker_transport_close(qa_render_worker_transport *t){ if(!t->impl)return; timpl *p=(timpl*)t->impl;
  if(p->has_runtime)qa_render_worker_runtime_close(&p->runtime);
  free(p); t->impl=NULL; }
