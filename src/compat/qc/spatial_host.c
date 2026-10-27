/* Port of src/compat/qc/spatial-host.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/spatial_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef qa_compat_qc_spatial_world W;
static void check(W *w, qa_compat_qc_machine *m) { if (m->entities != w->entities || m->program != w->program) qa_compat_qc_machine_fail(m, "world builtin belongs to another QC machine"); }
static void set_size(W *w, qa_compat_qc_machine *m, int64_t slot, qa_bounds b) {
  int64_t ref = qa_compat_qc_entity_memory_reference(w->entities, slot);
  const qa_numeric_operations *n = m->numeric; void *c = n->ctx;
  qa_compat_qc_machine_set_entity_vector(m, ref, "mins", b.min);
  qa_compat_qc_machine_set_entity_vector(m, ref, "maxs", b.max);
  qa_compat_qc_machine_set_entity_vector(m, ref, "size", (qa_vec3){n->subtract(c,b.max.x,b.min.x), n->subtract(c,b.max.y,b.min.y), n->subtract(c,b.max.z,b.min.z)});
  w->link(w->ctx, slot);
}
static int64_t hit_ref(W *w, qa_trace_result t) { return t.hit.kind == QA_TRACE_HIT_ACTOR ? w->reference(w->ctx, t.hit.actor) : 0; }
static void write_trace(W *w, qa_compat_qc_machine *m, qa_trace_result t) {
  if (strcmp(t.kind, "q1") != 0) { qa_compat_qc_machine_fail(m, "QC traceline requires Q1 source trace fields"); }
  qa_compat_qc_words_set_float(&m->globals, qa_compat_qc_machine_global_offset(m, "trace_allsolid"), t.all_solid ? 1 : 0);
  qa_compat_qc_words_set_float(&m->globals, qa_compat_qc_machine_global_offset(m, "trace_startsolid"), t.start_solid ? 1 : 0);
  qa_compat_qc_words_set_float(&m->globals, qa_compat_qc_machine_global_offset(m, "trace_fraction"), t.fraction);
  qa_compat_qc_words_set_float(&m->globals, qa_compat_qc_machine_global_offset(m, "trace_inwater"), t.in_water ? 1 : 0);
  qa_compat_qc_words_set_float(&m->globals, qa_compat_qc_machine_global_offset(m, "trace_inopen"), t.in_open ? 1 : 0);
  qa_compat_qc_words_set_float(&m->globals, qa_compat_qc_machine_global_offset(m, "trace_plane_dist"), t.source_plane_q1.distance);
  qa_compat_qc_words_set_vector(&m->globals, qa_compat_qc_machine_global_offset(m, "trace_endpos"), t.end);
  qa_compat_qc_words_set_vector(&m->globals, qa_compat_qc_machine_global_offset(m, "trace_plane_normal"), t.source_plane_q1.normal);
  qa_compat_qc_words_set_int(&m->globals, qa_compat_qc_machine_global_offset(m, "trace_ent"), (double)hit_ref(w, t));
}
static qa_trace_policy q1pol(const char *move) { qa_trace_policy p; memset(&p,0,sizeof p); p.kind="q1"; p.move=move; return p; }
static void b_setorigin(void *ctx, qa_compat_qc_machine *m) { W *w=ctx; check(w,m);
  int64_t slot = qa_compat_qc_entity_memory_slot(w->entities, qa_compat_qc_machine_arg_int(m,0));
  qa_compat_qc_machine_set_entity_vector(m, qa_compat_qc_machine_arg_int(m,0), "origin", qa_compat_qc_machine_arg_vector(m,1));
  w->link(w->ctx, slot); }
static void b_setsize(void *ctx, qa_compat_qc_machine *m) { W *w=ctx; check(w,m);
  set_size(w,m,qa_compat_qc_entity_memory_slot(w->entities, qa_compat_qc_machine_arg_int(m,0)),(qa_bounds){qa_compat_qc_machine_arg_vector(m,1),qa_compat_qc_machine_arg_vector(m,2)}); }
static void b_setmodel(void *ctx, qa_compat_qc_machine *m) { W *w=ctx; check(w,m);
  const char *name0 = qa_compat_qc_machine_arg_string(m,1); char name[512]; snprintf(name,sizeof name,"%s",name0);
  qa_compat_qc_spatial_model model; if (!w->model(w->ctx, name, &model)) { qa_compat_qc_machine_fail(m, "no precache: %s", name); }
  int64_t slot = qa_compat_qc_entity_memory_slot(w->entities, qa_compat_qc_machine_arg_int(m,0));
  qa_compat_qc_machine_set_entity_int(m, qa_compat_qc_machine_arg_int(m,0), "model", qa_compat_qc_machine_arg_int(m,1));
  qa_compat_qc_machine_set_entity_float(m, qa_compat_qc_machine_arg_int(m,0), "modelindex", model.index);
  set_size(w,m,slot,model.bounds); }
static void b_pointcontents(void *ctx, qa_compat_qc_machine *m) { W *w=ctx; check(w,m);
  qa_point_contents_query q; memset(&q,0,sizeof q); q.point=qa_compat_qc_machine_arg_vector(m,0); q.policy=q1pol("normal"); q.numeric=w->numeric;
  qa_point_contents_result r = w->scene.point_contents(w->scene.ctx, q);
  if (strcmp(r.kind,"q1")!=0) { qa_compat_qc_machine_fail(m,"QC pointcontents requires Q1 source contents"); }
  qa_compat_qc_machine_return_float(m, r.contents); }
static void b_traceline(void *ctx, qa_compat_qc_machine *m) { W *w=ctx; check(w,m);
  int64_t mode = (int64_t)trunc(qa_compat_qc_machine_arg_float(m,2));
  qa_trace_query q; memset(&q,0,sizeof q);
  q.start=qa_compat_qc_machine_arg_vector(m,0); q.end=qa_compat_qc_machine_arg_vector(m,1); q.shape.kind="point";
  q.policy=q1pol(mode==1?"no-monsters":mode==2?"missile":"normal"); q.numeric=w->numeric;
  qa_actor_id pass; bool has=false;
  if (qa_compat_qc_machine_arg_int(m,3)!=0) { pass=w->actor(w->ctx, qa_compat_qc_entity_memory_slot(w->entities, qa_compat_qc_machine_arg_int(m,3))).id; has=true; }
  q.pass_actor = has ? &pass : NULL;
  write_trace(w,m,w->scene.trace(w->scene.ctx,q)); }
static void b_findradius(void *ctx, qa_compat_qc_machine *m) { W *w=ctx; check(w,m);
  if (w->prepare_entities) w->prepare_entities(w->ctx);
  qa_vec3 center=qa_compat_qc_machine_arg_vector(m,0); double radius=qa_compat_qc_machine_arg_float(m,1);
  const qa_numeric_operations *n=m->numeric; void *c=n->ctx; int64_t chain=0;
  for (int64_t slot=1; slot<w->entities->count; slot++) {
    if (w->is_free_entity(w->ctx,slot)) continue;
    int64_t ref=qa_compat_qc_entity_memory_reference(w->entities,slot);
    if (qa_compat_qc_machine_entity_float(m,ref,"solid")==0) continue;
    qa_vec3 o=qa_compat_qc_machine_entity_vector(m,ref,"origin"), mn=qa_compat_qc_machine_entity_vector(m,ref,"mins"), mx=qa_compat_qc_machine_entity_vector(m,ref,"maxs");
    double x=n->subtract(c,center.x,n->add(c,o.x,n->multiply(c,n->add(c,mn.x,mx.x),0.5)));
    double y=n->subtract(c,center.y,n->add(c,o.y,n->multiply(c,n->add(c,mn.y,mx.y),0.5)));
    double z=n->subtract(c,center.z,n->add(c,o.z,n->multiply(c,n->add(c,mn.z,mx.z),0.5)));
    if (n->square_root(c,n->add(c,n->add(c,n->multiply(c,x,x),n->multiply(c,y,y)),n->multiply(c,z,z)))>radius) continue;
    qa_compat_qc_machine_set_entity_int(m,ref,"chain",(double)chain); chain=ref;
  }
  qa_compat_qc_machine_return_int(m,(double)chain); }
static void b_droptofloor(void *ctx, qa_compat_qc_machine *m) { W *w=ctx; check(w,m);
  int64_t ref=qa_compat_qc_words_int(&m->globals, qa_compat_qc_machine_global_offset(m,"self"));
  int64_t slot=qa_compat_qc_entity_memory_slot(w->entities,ref);
  qa_vec3 origin=qa_compat_qc_machine_entity_vector(m,ref,"origin");
  qa_trace_query q; memset(&q,0,sizeof q); q.start=origin;
  q.end=origin; q.end.z=m->numeric->subtract(m->numeric->ctx,origin.z,256);
  q.shape.kind="box"; q.shape.bounds=(qa_bounds){qa_compat_qc_machine_entity_vector(m,ref,"mins"),qa_compat_qc_machine_entity_vector(m,ref,"maxs")};
  q.policy=q1pol("normal"); q.numeric=w->numeric;
  qa_actor_id pass=w->actor(w->ctx,slot).id; q.pass_actor=&pass;
  qa_trace_result t=w->scene.trace(w->scene.ctx,q);
  if (t.fraction==1||t.all_solid){ qa_compat_qc_machine_return_float(m,0); return; }
  qa_compat_qc_machine_set_entity_vector(m,ref,"origin",t.end);
  w->link(w->ctx,slot);
  qa_compat_qc_machine_set_entity_float(m,ref,"flags",(double)((int64_t)trunc(qa_compat_qc_machine_entity_float(m,ref,"flags"))|512));
  qa_compat_qc_machine_set_entity_int(m,ref,"groundentity",(double)hit_ref(w,t));
  qa_compat_qc_machine_return_float(m,1); }
qa_compat_qc_spatial_bindings qa_compat_qc_create_spatial_bindings(W *w) {
  qa_compat_qc_spatial_bindings r; r.count=7;
  r.entries[0]=(qa_compat_qc_host_binding){"setorigin",{w,b_setorigin}};
  r.entries[1]=(qa_compat_qc_host_binding){"setsize",{w,b_setsize}};
  r.entries[2]=(qa_compat_qc_host_binding){"setmodel",{w,b_setmodel}};
  r.entries[3]=(qa_compat_qc_host_binding){"pointcontents",{w,b_pointcontents}};
  r.entries[4]=(qa_compat_qc_host_binding){"traceline",{w,b_traceline}};
  r.entries[5]=(qa_compat_qc_host_binding){"findradius",{w,b_findradius}};
  r.entries[6]=(qa_compat_qc_host_binding){"droptofloor",{w,b_droptofloor}};
  return r;
}
