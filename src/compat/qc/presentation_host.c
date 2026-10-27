/* Port of src/compat/qc/presentation-host.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/presentation_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qc/message_effects.h"
#include "qa/network/q1/profile.h"
#include "qa/network/q1/wire_types.h"
#include "qa/persistence/save_image.h"
#include "qa/foundation/support.h"

/* Finale */
static bool actor_eq(qa_actor_id a, qa_actor_id b) { return a.slot == b.slot && a.generation == b.generation && a.token == b.token && a.session.token == b.session.token; }
void qa_compat_qc_finale_ack_init(qa_compat_qc_finale_ack *a) { memset(a, 0, sizeof *a); }
void qa_compat_qc_finale_ack_destroy(qa_compat_qc_finale_ack *a) { free(a->actors); free(a->held); memset(a, 0, sizeof *a); }
void qa_compat_qc_finale_ack_reset(qa_compat_qc_finale_ack *a) { a->has_last_poll = false; a->acknowledged = false; a->count = 0; }
void qa_compat_qc_finale_ack_dismiss(qa_compat_qc_finale_ack *a, double seconds) { a->has_last_poll = true; a->last_poll = seconds; a->acknowledged = true; }
static int held_index(qa_compat_qc_finale_ack *a, qa_actor_id actor, bool create) {
  for (size_t i = 0; i < a->count; i++) if (actor_eq(a->actors[i], actor)) return (int)i;
  if (!create) return -1;
  if (a->count == a->capacity) { size_t c = a->capacity ? a->capacity * 2 : 8; a->actors = realloc(a->actors, c * sizeof *a->actors); a->held = realloc(a->held, c * sizeof *a->held); if (!a->actors || !a->held) qa_range_error("finale allocation failed"); a->capacity = c; }
  a->actors[a->count] = actor; a->held[a->count] = false; return (int)a->count++;
}
bool qa_compat_qc_finale_ack_poll(qa_compat_qc_finale_ack *a, double seconds, const qa_actor_id *actors, const bool *down, size_t count) {
  if (!a->has_last_poll || seconds < a->last_poll || seconds - a->last_poll > 1) {
    a->acknowledged = false; a->count = 0;
    for (size_t i = 0; i < count; i++) { int idx = held_index(a, actors[i], true); a->held[idx] = down[i]; }
  }
  a->has_last_poll = true; a->last_poll = seconds;
  for (size_t i = 0; i < a->count; i++) {
    bool present = false; for (size_t j = 0; j < count; j++) if (actor_eq(actors[j], a->actors[i])) present = true;
    if (!present) { memmove(&a->actors[i], &a->actors[i+1], (a->count-i-1)*sizeof *a->actors); memmove(&a->held[i], &a->held[i+1], (a->count-i-1)*sizeof *a->held); a->count--; i--; }
  }
  for (size_t i = 0; i < count; i++) {
    int idx = held_index(a, actors[i], true); bool previous = a->held[idx];
    if (down[i] && previous != true && !(idx == (int)a->count - 1 && a->held[idx] == false && false)) {
      /* TS: previous undefined (new entry) also counts as press. held_index create sets false; TS get returns undefined != true, same result. */
      a->acknowledged = true;
    }
    a->held[idx] = down[i];
  }
  return a->acknowledged;
}

/* Presentation bindings */
typedef qa_compat_qc_presentation_state PS;
static void p_check(PS *s, qa_compat_qc_machine *m) { if (m->program != s->world->program || m->entities != s->world->entities) qa_compat_qc_machine_fail(m, "presentation builtin belongs to another QC machine"); }
static bool is_qw(PS *s) { return s->world->program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD; }
static void do_emit(PS *s, qa_q1_event e) { s->services.emit(s->services.ctx, s->services.content, e); }
static void do_register(PS *s, qa_compat_qc_precached_resource v) { s->services.register_resource(s->services.ctx, s->services.content, v.resource.requested_path, v.resource); }
static qa_compat_qc_builtin host_get(PS *s, const char *name) {
  for (size_t i = 0; i < s->world->host_count; i++) if (strcmp(s->world->host[i].name, name) == 0) return s->world->host[i].fn;
  return (qa_compat_qc_builtin){NULL, NULL};
}
enum { W_FINALE, W_EXFLAGS, W_INERT, W_INERT0, W_BPRINT, W_LOCALCMD, W_MAKESTATIC, W_SPRINT, W_CENTERPRINT, W_STUFFCMD, W_EXSPRINT, W_EXCENTER, W_PRECACHE_SOUND, W_PRECACHE_MODEL, W_PRECACHE_FILE, W_SOUND, W_LOCALSOUND, W_AMBIENT, W_PARTICLE, W_LIGHTSTYLE };
static void pres_fn(void *ctx, qa_compat_qc_machine *m) {
  struct Ctx { const char *name; PS *state; int which; } *c = ctx;
  PS *s = c->state; p_check(s, m);
  qa_compat_qc_presentation_services *sv = &s->services;
  const qa_compat_qc_quakeworld_message_services *qw = is_qw(s) ? sv->qw : NULL;
  switch (c->which) {
    case W_FINALE: qa_compat_qc_machine_return_float(m, sv->finale_finished(sv->ctx) ? 1 : 0); return;
    case W_EXFLAGS: case W_INERT0: qa_compat_qc_machine_return_float(m, 0); return;
    case W_INERT: return;
    case W_BPRINT: {
      if (sv->broadcast_print) { char *t = qa_compat_qc_machine_var_string(m, qw ? 1 : 0); double lvl = qw ? trunc(qa_compat_qc_machine_arg_float(m, 0)) : 2; sv->broadcast_print(sv->ctx, t ? t : "", lvl); free(t); return; }
      if (!qw) {
        char *t = qa_compat_qc_machine_var_string(m, 0); sv->print(sv->ctx, t ? t : "");
        if (!sv->print_broadcasts_to_clients && sv->nq) { qa_netquake_message msg; memset(&msg,0,sizeof msg); msg.kind="print"; msg.text=t; sv->nq->route(sv->nq->ctx, &msg, 1, (qa_compat_qc_message_destination){QA_QC_DEST_BROADCAST, true, {{NULL,0},0,0,NULL}, {0,0,0}, NULL}, NULL, NULL, 0); }
        free(t); return;
      }
      char *t = qa_compat_qc_machine_var_string(m, 1); sv->print(sv->ctx, t ? t : "");
      if (!sv->print_broadcasts_to_clients) { qa_compat_qc_routed_message e; memset(&e,0,sizeof e); e.message.kind="print"; e.message.level=trunc(qa_compat_qc_machine_arg_float(m,0)); e.message.text=t; qw->route(qw->ctx, &e, 1, (qa_compat_qc_message_destination){QA_QC_DEST_BROADCAST, true, {{NULL,0},0,0,NULL}, {0,0,0}, NULL}); }
      free(t); return;
    }
    case W_LOCALCMD: { qa_q1_event e; memset(&e,0,sizeof e); e.kind="server-command"; e.text=qa_compat_qc_machine_arg_string(m,0); do_emit(s,e); return; }
    case W_MAKESTATIC: {
      qa_compat_qc_words *w = qa_compat_qc_entity_memory_from_reference(m->entities, qa_compat_qc_machine_arg_int(m,0));
      const char *path0 = qa_compat_qc_strings_get(&m->strings, qa_compat_qc_words_int(w, qa_compat_qc_machine_field_offset(m,"model")));
      char path[512]; snprintf(path,sizeof path,"%s",path0);
      qa_compat_qc_precached_resource res; bool has=false;
      if (path[0]) { has = sv->lookup(sv->ctx,"model",path,&res); if(!has && path[0]=='*'){ res=sv->precache(sv->ctx,"model",path); has=true; } }
      if (path[0] && !has) { qa_compat_qc_machine_fail(m, "makestatic model was not precached: %s", path); }
      qa_compat_qc_builtin remove_b = host_get(s, "remove");
      if (!remove_b.fn) { qa_compat_qc_machine_fail(m, "makestatic requires source entity removal"); }
      if (has) do_register(s, res);
      qa_q1_event e; memset(&e,0,sizeof e); e.kind="static-model"; e.path=path;
      e.frame = trunc(qa_compat_qc_words_float(w, qa_compat_qc_machine_field_offset(m,"frame")));
      e.color_map = trunc(qa_compat_qc_words_float(w, qa_compat_qc_machine_field_offset(m,"colormap")));
      e.skin = trunc(qa_compat_qc_words_float(w, qa_compat_qc_machine_field_offset(m,"skin")));
      e.has_origin=true; e.origin = qa_compat_qc_words_vector(w, qa_compat_qc_machine_field_offset(m,"origin"));
      e.angles = qa_compat_qc_words_vector(w, qa_compat_qc_machine_field_offset(m,"angles"));
      do_emit(s, e);
      remove_b.fn(remove_b.ctx, m); return;
    }
    case W_SPRINT: case W_CENTERPRINT: case W_STUFFCMD: case W_EXSPRINT: case W_EXCENTER: {
      const char *name = c->name; bool center = c->which==W_CENTERPRINT||c->which==W_EXCENTER;
      qa_owned_actor actor = s->world->actor(s->world->ctx, qa_compat_qc_entity_memory_slot(s->world->entities, qa_compat_qc_machine_arg_int(m,0)));
      char *t = qa_compat_qc_machine_var_string(m, (qw && c->which==W_SPRINT) ? 2 : 1);
      qa_compat_qc_message_destination dest = {QA_QC_DEST_CLIENT, true, actor.id, {0,0,0}, NULL};
      if (qw) {
        if (!qw->client(qw->ctx, actor.id)) { char buf[256]; snprintf(buf,sizeof buf,"tried to %s to a non-client\n", name); sv->print(sv->ctx, buf); free(t); return; }
        qa_compat_qc_routed_message e; memset(&e,0,sizeof e); e.has_actor=true; e.actor=actor.id;
        bool is_sprint = c->which==W_SPRINT||c->which==W_EXSPRINT;
        e.message.kind = is_sprint ? "print" : center ? "center-print" : "stufftext";
        if (is_sprint) e.message.level = c->which==W_SPRINT ? trunc(qa_compat_qc_machine_arg_float(m,1)) : 2;
        e.message.text = t; qw->route(qw->ctx, &e, 1, dest); free(t); return;
      }
      bool is_sprint = c->which==W_SPRINT||c->which==W_EXSPRINT;
      if (sv->nq) {
        if (!sv->nq->client(sv->nq->ctx, actor.id)) { free(t); qa_compat_qc_machine_fail(m, "Client message addressed a non-client"); }
        qa_netquake_message msg; memset(&msg,0,sizeof msg); msg.kind = is_sprint?"print":center?"center-print":"stufftext"; msg.text=t;
        sv->nq->route(sv->nq->ctx, &msg, 1, dest, NULL, NULL, 0); free(t); return;
      }
      if (!sv->message) { free(t); qa_compat_qc_machine_fail(m, "Missing client message service"); }
      qa_network_event ev; memset(&ev,0,sizeof ev);
      ev.kind = is_sprint ? QA_NETWORK_EVENT_PRINT : center ? QA_NETWORK_EVENT_CENTER_PRINT : QA_NETWORK_EVENT_COMMAND_TEXT;
      ev.level = 2; ev.text = t; sv->message(sv->ctx, ev, actor.id); free(t); return;
    }
    case W_PRECACHE_SOUND: case W_PRECACHE_MODEL: {
      const char *kind = c->which==W_PRECACHE_SOUND?"sound":"model";
      if (!sv->loading(sv->ctx)) { qa_compat_qc_machine_fail(m, "PF_Precache_*: Precache can only be done in spawn functions"); }
      const char *name0 = qa_compat_qc_machine_arg_string(m,0); char name[512]; snprintf(name,sizeof name,"%s",name0);
      qa_compat_qc_machine_return_int(m, qa_compat_qc_machine_arg_int(m,0));
      if (!name[0] || (unsigned char)name[0] <= 32) { qa_compat_qc_machine_fail(m, "Bad string"); }
      do_register(s, sv->precache(sv->ctx, kind, name)); return;
    }
    case W_PRECACHE_FILE: qa_compat_qc_machine_return_int(m, qa_compat_qc_machine_arg_int(m,0)); return;
    case W_SOUND: {
      double channel = trunc(qa_compat_qc_machine_arg_float(m,1));
      double volume = trunc(m->numeric->multiply(m->numeric->ctx, qa_compat_qc_machine_arg_float(m,3), 255));
      double attenuation = qa_compat_qc_machine_arg_float(m,4);
      if (!isfinite(volume)||volume<0||volume>255) { qa_compat_qc_machine_fail(m, "SV_StartSound: volume = %g", volume); }
      if (!isfinite(attenuation)||attenuation<0||attenuation>4) { qa_compat_qc_machine_fail(m, "SV_StartSound: attenuation = %g", attenuation); }
      if (!isfinite(channel)||channel<0||channel>(qw?15:7)) { qa_compat_qc_machine_fail(m, "SV_StartSound: channel = %g", channel); }
      int channel_id = (int)channel & 7;
      const char *sc; char chnum[8];
      switch(channel_id){case 0:sc="auto";break;case 1:sc="weapon";break;case 2:sc="voice";break;case 3:sc="item";break;case 4:sc="body";break;default:snprintf(chnum,sizeof chnum,"%d",channel_id);sc=chnum;break;}
      const char *path0 = qa_compat_qc_machine_arg_string(m,2); char path[512]; snprintf(path,sizeof path,"%s",path0);
      qa_compat_qc_precached_resource res;
      if (!sv->lookup(sv->ctx,"sound",path,&res)) { char buf[600]; snprintf(buf,sizeof buf,"SV_StartSound: %s not precacheed\n",path); sv->print(sv->ctx,buf); return; }
      do_register(s,res);
      qa_owned_actor actor = s->world->actor(s->world->ctx, qa_compat_qc_entity_memory_slot(s->world->entities, qa_compat_qc_machine_arg_int(m,0)));
      const qa_numeric_operations *n=m->numeric; void *nc=n->ctx;
      if (qw) {
        qa_compat_qc_words *w = qa_compat_qc_entity_memory_from_reference(m->entities, qa_compat_qc_machine_arg_int(m,0));
        qa_vec3 origin=qa_compat_qc_words_vector(w,qa_compat_qc_machine_field_offset(m,"origin")), mn=qa_compat_qc_words_vector(w,qa_compat_qc_machine_field_offset(m,"mins")), mx=qa_compat_qc_words_vector(w,qa_compat_qc_machine_field_offset(m,"maxs"));
        qa_vec3 pos = qa_compat_qc_words_float(w,qa_compat_qc_machine_field_offset(m,"solid"))==4
          ? (qa_vec3){n->add(nc,origin.x,n->multiply(nc,n->add(nc,mn.x,mx.x),0.5)),n->add(nc,origin.y,n->multiply(nc,n->add(nc,mn.y,mx.y),0.5)),n->add(nc,origin.z,n->multiply(nc,n->add(nc,mn.z,mx.z),0.5))} : origin;
        qa_compat_qc_routed_message e; memset(&e,0,sizeof e); e.has_actor=true; e.actor=actor.id;
        e.message.kind="sound"; e.message.entity=(double)qa_compat_qc_entity_memory_slot(s->world->entities, qa_compat_qc_machine_arg_int(m,0));
        e.message.channel=(double)channel_id; e.message.index=res.index; e.message.origin=pos; e.message.volume=volume; e.message.attenuation=attenuation;
        bool reliable = ((int)channel & 8)!=0;
        qw->route(qw->ctx,&e,1,(qa_compat_qc_message_destination){QA_QC_DEST_MULTICAST,reliable,actor.id,pos,reliable||!qw->phs(qw->ctx)?"all":"phs"});
        return;
      }
      if (sv->nq && sv->nq->native(sv->nq->ctx)) {
        qa_compat_qc_words *w = qa_compat_qc_entity_memory_from_reference(m->entities, qa_compat_qc_machine_arg_int(m,0));
        qa_vec3 origin=qa_compat_qc_words_vector(w,qa_compat_qc_machine_field_offset(m,"origin")), mn=qa_compat_qc_words_vector(w,qa_compat_qc_machine_field_offset(m,"mins")), mx=qa_compat_qc_words_vector(w,qa_compat_qc_machine_field_offset(m,"maxs"));
        qa_vec3 center={n->add(nc,origin.x,n->multiply(nc,n->add(nc,mn.x,mx.x),0.5)),n->add(nc,origin.y,n->multiply(nc,n->add(nc,mn.y,mx.y),0.5)),n->add(nc,origin.z,n->multiply(nc,n->add(nc,mn.z,mx.z),0.5))};
        qa_netquake_message msg; memset(&msg,0,sizeof msg); msg.kind="sound";
        msg.entity=(double)qa_compat_qc_entity_memory_slot(s->world->entities, qa_compat_qc_machine_arg_int(m,0));
        msg.channel=channel_id; msg.index=res.index; msg.origin=center; msg.volume=volume; msg.attenuation=attenuation;
        sv->nq->route(sv->nq->ctx,&msg,1,(qa_compat_qc_message_destination){QA_QC_DEST_BROADCAST,false,actor.id,center,NULL},NULL,NULL,0);
      }
      qa_q1_event e; memset(&e,0,sizeof e); e.kind="sound"; e.has_actor=true; e.actor=actor.id; e.path=path; e.sound_channel=sc; e.volume=volume/255; e.attenuation=attenuation; do_emit(s,e); return;
    }
    case W_LOCALSOUND: {
      const char *path0 = qa_compat_qc_machine_arg_string(m,1); char path[512]; snprintf(path,sizeof path,"%s",path0);
      qa_compat_qc_precached_resource res; bool has=sv->lookup(sv->ctx,"sound",path,&res);
      qa_owned_actor actor = s->world->actor(s->world->ctx, qa_compat_qc_entity_memory_slot(s->world->entities, qa_compat_qc_machine_arg_int(m,0)));
      if (has) do_register(s,res);
      if (qw || (sv->nq && sv->nq->native(sv->nq->ctx))) {
        if (!has) { char buf[600]; snprintf(buf,sizeof buf,"SV_StartSound: %s not precacheed\n",path); sv->print(sv->ctx,buf); return; }
        qa_compat_qc_words *w = qa_compat_qc_entity_memory_from_reference(m->entities, qa_compat_qc_machine_arg_int(m,0));
        qa_vec3 origin=qa_compat_qc_words_vector(w,qa_compat_qc_machine_field_offset(m,"origin"));
        qa_compat_qc_message_destination dest={QA_QC_DEST_CLIENT,true,actor.id,{0,0,0},NULL};
        if (qw) { qa_compat_qc_routed_message e; memset(&e,0,sizeof e); e.has_actor=true; e.actor=actor.id; e.message.kind="sound";
          e.message.entity=(double)qa_compat_qc_entity_memory_slot(s->world->entities, qa_compat_qc_machine_arg_int(m,0)); e.message.channel=0; e.message.index=res.index; e.message.origin=origin; e.message.volume=255; e.message.attenuation=0;
          qw->route(qw->ctx,&e,1,dest); return; }
        qa_netquake_message msg; memset(&msg,0,sizeof msg); msg.kind="sound"; msg.entity=(double)qa_compat_qc_entity_memory_slot(s->world->entities, qa_compat_qc_machine_arg_int(m,0));
        msg.channel=0; msg.index=res.index; msg.origin=origin; msg.volume=255; msg.attenuation=0;
        sv->nq->route(sv->nq->ctx,&msg,1,dest,NULL,NULL,0); return;
      }
      qa_q1_event e; memset(&e,0,sizeof e); e.kind="sound"; e.has_actor=true; e.actor=actor.id; e.path=path; e.sound_channel="auto"; e.volume=1; e.attenuation=0; do_emit(s,e); return;
    }
    case W_AMBIENT: {
      const char *path0 = qa_compat_qc_machine_arg_string(m,1); char path[512]; snprintf(path,sizeof path,"%s",path0);
      qa_compat_qc_precached_resource res;
      if (!sv->lookup(sv->ctx,"sound",path,&res)) { char buf[600]; snprintf(buf,sizeof buf,"no precache: %s\n",path); sv->print(sv->ctx,buf); return; }
      do_register(s,res);
      qa_q1_event e; memset(&e,0,sizeof e); e.kind="ambient"; e.has_origin=true; e.origin=qa_compat_qc_machine_arg_vector(m,0); e.path=path; e.volume=qa_compat_qc_machine_arg_float(m,2); e.attenuation=qa_compat_qc_machine_arg_float(m,3); do_emit(s,e); return;
    }
    case W_PARTICLE: { qa_q1_event e; memset(&e,0,sizeof e); e.kind="particles"; e.has_origin=true; e.origin=qa_compat_qc_machine_arg_vector(m,0); e.direction=qa_compat_qc_machine_arg_vector(m,1); e.color=trunc(qa_compat_qc_machine_arg_float(m,2)); e.count=trunc(qa_compat_qc_machine_arg_float(m,3)); do_emit(s,e); return; }
    case W_LIGHTSTYLE: {
      double style=trunc(qa_compat_qc_machine_arg_float(m,0));
      if (!isfinite(style)||style<0||style>=64) { qa_compat_qc_machine_fail(m, "lightstyle outside source style table"); }
      qa_q1_event e; memset(&e,0,sizeof e); e.kind="lightstyle"; e.style=style; e.pattern=qa_compat_qc_machine_arg_string(m,1); do_emit(s,e); return;
    }
  }
}
qa_compat_qc_presentation_bindings qa_compat_qc_create_presentation_bindings(qa_compat_qc_presentation_world *world, qa_compat_qc_presentation_services services, PS *st) {
  st->world = world; st->services = services;
  qa_compat_qc_presentation_bindings r; r.count = 0;
#define INSTALL(name_, which_) do { size_t i_=r.count++; st->ctxs[i_].name=name_; st->ctxs[i_].state=st; st->ctxs[i_].which=which_; r.entries[i_] = (qa_compat_qc_host_binding){name_, {(void*)&st->ctxs[i_], pres_fn}}; } while(0)
  /* ctxs element type matches struct Ctx layout */
  if (services.finale_finished) INSTALL("ex_finaleFinished", W_FINALE);
  INSTALL("ex_CheckPlayerEXFlags", W_EXFLAGS);
  static const char *const inert[] = {"ex_draw_point","ex_draw_line","ex_draw_arrow","ex_draw_ray","ex_draw_circle","ex_draw_bounds","ex_draw_worldtext","ex_draw_sphere","ex_draw_cylinder","ex_prompt","ex_clearprompt"};
  for (size_t i = 0; i < 11; i++) INSTALL(inert[i], W_INERT);
  static const char *const inert0[] = {"ex_bot_movetopoint","ex_bot_followentity","ex_walkpathtogoal","ex_promptchoice"};
  for (size_t i = 0; i < 4; i++) INSTALL(inert0[i], W_INERT0);
  bool qw = world->program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD;
  if (qw && !services.qw) qa_range_error("QuakeWorld presentation requires routed message services");
  INSTALL("bprint", W_BPRINT); INSTALL("ex_bprint", W_BPRINT);
  INSTALL("localcmd", W_LOCALCMD); INSTALL("makestatic", W_MAKESTATIC);
  if (services.message || (qw && services.qw)) {
    INSTALL("sprint", W_SPRINT); INSTALL("centerprint", W_CENTERPRINT); INSTALL("stuffcmd", W_STUFFCMD);
    INSTALL("ex_sprint", W_EXSPRINT); INSTALL("ex_centerprint", W_EXCENTER);
  }
  INSTALL("precache_sound", W_PRECACHE_SOUND); INSTALL("precache_model", W_PRECACHE_MODEL);
  INSTALL("precache_file", W_PRECACHE_FILE); INSTALL("sound", W_SOUND); INSTALL("ex_localsound", W_LOCALSOUND);
  INSTALL("ambientsound", W_AMBIENT); INSTALL("particle", W_PARTICLE); INSTALL("lightstyle", W_LIGHTSTYLE);
#undef INSTALL
  return r;
}
/* Broadcast messages */
typedef qa_compat_qc_broadcast_messages B;
static void owner_set(qa_compat_qc_owner_entry **arr, size_t *n, size_t *cap, int64_t off, bool has, qa_actor_id a) {
  for (size_t i = 0; i < *n; i++) if ((*arr)[i].offset == off) { (*arr)[i].has_actor = has; (*arr)[i].actor = a; return; }
  if (*n == *cap) { size_t c = *cap ? *cap * 2 : 16; *arr = realloc(*arr, c * sizeof **arr); if (!*arr) qa_range_error("message owner allocation failed"); *cap = c; }
  (*arr)[(*n)++] = (qa_compat_qc_owner_entry){off, has, a};
}
static bool owner_get(const qa_compat_qc_owner_entry *arr, size_t n, int64_t off, qa_actor_id *out) {
  for (size_t i = 0; i < n; i++) if (arr[i].offset == off) { if (arr[i].has_actor) *out = arr[i].actor; return arr[i].has_actor; }
  return false;
}
static qa_compat_qc_routed_buffer *routed_get(B *b, const char *key) {
  for (size_t i = 0; i < b->routed_count; i++) { if (strcmp(b->routed[i].key, key) == 0) return &b->routed[i]; } return NULL;
}
static void routed_delete(B *b, const char *key) {
  for (size_t i = 0; i < b->routed_count; i++) if (strcmp(b->routed[i].key, key) == 0) {
    free(b->routed[i].key); qa_size_buf_destroy(&b->routed[i].buffer); free(b->routed[i].owners);
    memmove(&b->routed[i], &b->routed[i+1], (b->routed_count-i-1)*sizeof *b->routed); b->routed_count--; return; }
}
static void emit_nq_effects(B *b, qa_netquake_message *messages, size_t count, const qa_compat_qc_owner_entry *owners, size_t owner_count, long long maximum, bool routed, const qa_actor_id *recipient) {
  qa_size_buf encoded; if (!qa_size_buf_init(&encoded, maximum, false)) qa_range_error("message buffer allocation failed");
  qa_q1_event *effects = calloc(count ? count : 1, sizeof *effects);
  bool *has = calloc(count ? count : 1, sizeof *has);
  if (!effects || !has) qa_range_error("message effect allocation failed");
  for (size_t i = 0; i < count; i++) {
    long long offset = encoded.cursize;
    if (strcmp(messages[i].kind, "entity") == 0) qa_range_error("QC cannot write host entity snapshots");
    qa_write_netquake_message(&encoded, b->decoder.protocol, &messages[i], b->decoder.rerelease_messages, b->decoder.standard_quake);
    if (strcmp(messages[i].kind, "temporary-entity") != 0) {
      if (routed) continue;
      qa_range_error("Unsupported QC broadcast message %s", messages[i].kind);
    }
    qa_actor_id owner; bool has_owner = owner_get(owners, owner_count, offset + 2, &owner);
    effects[i] = qa_compat_qc_quake_temporary_event(messages[i].effect, has_owner ? &owner : NULL, false); has[i] = true;
  }
  qa_size_buf_destroy(&encoded);
  for (size_t i = 0; i < count; i++) if (has[i]) b->emit(b->emit_ctx, effects[i], recipient);
  free(effects); free(has);
}
static void route_buffer(B *b, qa_size_buf *buffer, const qa_compat_qc_owner_entry *owners, size_t owner_count, qa_compat_qc_message_destination dest) {
  if (b->nq) {
    size_t len = 0; uint8_t *bytes = qa_size_buf_bytes(buffer, &len);
    size_t count = 0; qa_netquake_message *messages = qa_netquake_decoder_decode(&b->decoder, bytes, len, &count);
    qa_size_buf encoded; if (!qa_size_buf_init(&encoded, buffer->maxsize, false)) qa_range_error("message buffer allocation failed");
    qa_actor_id *view_targets = calloc(count ? count : 1, sizeof *view_targets);
    bool *view_has = calloc(count ? count : 1, sizeof *view_has);
    if (!view_targets || !view_has) qa_range_error("message view allocation failed");
    for (size_t i = 0; i < count; i++) {
      if (strcmp(messages[i].kind, "entity") == 0) qa_range_error("QC cannot write host entity snapshots");
      if (strcmp(messages[i].kind, "set-view") == 0) view_has[i] = owner_get(owners, owner_count, encoded.cursize + 1, &view_targets[i]);
      qa_write_netquake_message(&encoded, b->decoder.protocol, &messages[i], b->decoder.rerelease_messages, b->decoder.standard_quake);
    }
    qa_size_buf_destroy(&encoded);
    b->nq->route(b->nq->ctx, messages, count, dest, view_targets, view_has, count);
    bool local = b->nq->local ? b->nq->local(b->nq->ctx) : false;
    if (!b->nq->native(b->nq->ctx) || local || (dest.kind == QA_QC_DEST_BROADCAST && !dest.reliable)) {
      const qa_actor_id *recip = dest.kind == QA_QC_DEST_CLIENT ? &dest.actor : NULL;
      emit_nq_effects(b, messages, count, owners, owner_count, buffer->maxsize, true, recip);
    }
    for (size_t i = 0; i < count; i++) qa_netquake_message_destroy(&messages[i]);
    free(messages); free(bytes); free(view_targets); free(view_has);
    return;
  }
  if (!b->qw) qa_range_error("Missing QuakeWorld routing service");
  size_t len = 0; uint8_t *bytes = qa_size_buf_bytes(buffer, &len);
  size_t count = 0; qa_quakeworld_message *messages = qa_quakeworld_decoder_decode(&b->qw_decoder, bytes, len, 0, &count);
  qa_size_buf encoded; if (!qa_size_buf_init(&encoded, buffer->maxsize, false)) qa_range_error("message buffer allocation failed");
  qa_compat_qc_routed_message *entries = calloc(count ? count : 1, sizeof *entries);
  if (!entries) qa_range_error("message entry allocation failed");
  for (size_t i = 0; i < count; i++) {
    if (strcmp(messages[i].kind, "packet-entities") == 0 || strcmp(messages[i].kind, "invalid-delta") == 0) qa_range_error("QC cannot write snapshot entity deltas");
    long long offset = encoded.cursize;
    qa_write_quakeworld_message(&encoded, b->qw_decoder.protocol, &messages[i]);
    qa_actor_id actor; bool has = false;
    if (strcmp(messages[i].kind, "temporary-entity") == 0 && messages[i].effect.kind == QA_TE_BEAM) has = owner_get(owners, owner_count, offset + 2, &actor);
    else if (strcmp(messages[i].kind, "sound") == 0 || strcmp(messages[i].kind, "stop-sound") == 0) has = owner_get(owners, owner_count, -offset - 2, &actor);
    else if (strcmp(messages[i].kind, "muzzle-flash") == 0 || strcmp(messages[i].kind, "set-view") == 0) has = owner_get(owners, owner_count, offset + 1, &actor);
    if (strcmp(messages[i].kind, "temporary-entity") == 0 && messages[i].effect.kind == QA_TE_BEAM && !has) qa_range_error("QC beam had no owned actor when written");
    entries[i].message = messages[i]; entries[i].has_actor = has; if (has) entries[i].actor = actor;
  }
  qa_size_buf_destroy(&encoded);
  b->qw->route(b->qw->ctx, entries, count, dest);
  for (size_t i = 0; i < count; i++) qa_quakeworld_message_destroy(&entries[i].message);
  free(entries); free(messages); free(bytes);
}
enum { BW_BYTE, BW_CHAR, BW_SHORT, BW_LONG, BW_COORD, BW_ANGLE, BW_STRING, BW_ENTITY, BW_MULTICAST };
static qa_compat_qc_routed_buffer *target_buffer(B *b, qa_compat_qc_machine *m) {
  bool is_qw = b->world->program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD;
  const void *routing = b->qw ? (const void *)b->qw : (const void *)b->nq;
  int64_t dest = (int64_t)trunc(qa_compat_qc_machine_arg_float(m, 0));
  char key[96]; qa_compat_qc_message_destination destination; memset(&destination, 0, sizeof destination); bool has_dest = true;
  if (dest == 0) { snprintf(key,sizeof key,"broadcast"); destination.kind=QA_QC_DEST_BROADCAST; destination.reliable=false; }
  else if (dest == 1) {
    qa_owned_actor actor = b->world->actor(b->world->ctx, qa_compat_qc_entity_memory_slot(b->world->entities, qa_compat_qc_words_int(&m->globals, qa_compat_qc_machine_global_offset(m, "msg_entity"))));
    bool is_client = b->qw ? b->qw->client(b->qw->ctx, actor.id) : b->nq ? b->nq->client(b->nq->ctx, actor.id) : false;
    if (!routing || !is_client) { qa_compat_qc_machine_fail(m, "WriteDest: not a client"); }
    snprintf(key,sizeof key,"client:%lld:%lld", (long long)actor.id.slot, (long long)actor.id.generation);
    destination.kind=QA_QC_DEST_CLIENT; destination.reliable=true; destination.actor=actor.id;
  }
  else if (dest == 2) { snprintf(key,sizeof key,"all"); destination.kind=QA_QC_DEST_BROADCAST; destination.reliable=true; }
  else if (dest == 3) {
    if (is_qw && (!b->qw || !b->qw->loading(b->qw->ctx))) { qa_compat_qc_machine_fail(m, "PF_Write_*: MSG_INIT can only be written in spawn functions"); }
    snprintf(key,sizeof key,"signon"); destination.kind=QA_QC_DEST_SIGNON;
  }
  else if (dest == 4) {
    if (!is_qw) { qa_compat_qc_machine_fail(m, "WriteDest: bad destination"); }
    snprintf(key,sizeof key,"multicast"); has_dest=false;
  }
  else { qa_compat_qc_machine_fail(m, "WriteDest: bad destination"); }
  qa_compat_qc_routed_buffer *entry = routed_get(b, key);
  if (!entry) {
    if (b->routed_count == b->routed_capacity) { size_t c = b->routed_capacity ? b->routed_capacity*2 : 4; b->routed = realloc(b->routed, c*sizeof *b->routed); if(!b->routed) qa_range_error("routed buffer allocation failed"); b->routed_capacity=c; }
    entry = &b->routed[b->routed_count++]; memset(entry, 0, sizeof *entry);
    entry->key = strdup(key);
    long long maxsize = !is_qw ? (dest==0||dest==2 ? 1024 : 8000) : dest==1 ? QA_Q1_MAX_MSGLEN*5 : (dest==0||dest==3 ? QA_Q1_MAX_DATAGRAM : QA_Q1_MAX_MSGLEN);
    if (!qa_size_buf_init(&entry->buffer, maxsize, dest == 0)) qa_range_error("routed buffer allocation failed");
    entry->has_destination = has_dest; entry->destination = destination;
  }
  return entry;
}
static void write_fn(void *ctx, qa_compat_qc_machine *m) {
  struct BCtx { B *self; int which; } *c = ctx; B *b = c->self;
  if (c->which == BW_MULTICAST) {
    if (m->program != b->world->program || m->entities != b->world->entities) { qa_compat_qc_machine_fail(m, "QC multicast belongs to another source"); }
    int64_t mode = (int64_t)trunc(qa_compat_qc_machine_arg_float(m, 1));
    if (mode < 0 || mode > 5) { qa_compat_qc_machine_fail(m, "SV_Multicast: bad destination"); }
    qa_compat_qc_routed_buffer *entry = routed_get(b, "multicast");
    if (entry) {
      qa_compat_qc_message_destination d = {QA_QC_DEST_MULTICAST, mode>=3, {{NULL,NULL},0,0,NULL}, qa_compat_qc_machine_arg_vector(m,0), mode%3==0?"all":mode%3==1?"phs":"pvs"};
      route_buffer(b, &entry->buffer, entry->owners, entry->owner_count, d);
      routed_delete(b, "multicast");
    }
    return;
  }
  if (m->program != b->world->program || m->entities != b->world->entities) { qa_compat_qc_machine_fail(m, "QC message belongs to another source"); }
  bool routing = b->qw || b->nq;
  if (!routing && qa_compat_qc_machine_arg_float(m, 0) != 0) { qa_compat_qc_machine_fail(m, "QC message destination is not the supported MSG_BROADCAST datagram"); }
  qa_size_buf *buffer; qa_compat_qc_owner_entry **owners; size_t *owner_count, *owner_cap;
  qa_compat_qc_routed_buffer *entry = NULL;
  if (routing) { entry = target_buffer(b, m); buffer = &entry->buffer; owners=&entry->owners; owner_count=&entry->owner_count; owner_cap=&entry->owner_capacity; }
  else { buffer = &b->buffer; owners=&b->owners; owner_count=&b->owner_count; owner_cap=&b->owner_capacity; }
  long long before = buffer->cursize;
  switch (c->which) {
    case BW_BYTE: case BW_CHAR: qa_msg_write_byte(buffer, qa_compat_qc_machine_arg_float(m,1)); break;
    case BW_SHORT: qa_msg_write_short(buffer, qa_compat_qc_machine_arg_float(m,1)); break;
    case BW_LONG: qa_msg_write_long(buffer, qa_compat_qc_machine_arg_float(m,1)); break;
    case BW_COORD: qa_msg_write_coord(buffer, qa_compat_qc_machine_arg_float(m,1)); break;
    case BW_ANGLE: qa_msg_write_angle(buffer, qa_compat_qc_machine_arg_float(m,1)); break;
    case BW_STRING: qa_msg_write_string(buffer, qa_compat_qc_machine_arg_string(m,1)); break;
    case BW_ENTITY: qa_msg_write_short(buffer, (double)qa_compat_qc_entity_memory_slot(b->world->entities, qa_compat_qc_machine_arg_int(m,1))); break;
  }
  bool is_qw = b->world->program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD;
  for (long long end = before > 1 ? before : 1; end < buffer->cursize; end++) {
    unsigned low = buffer->data[end-1], high = buffer->data[end];
    qa_actor_id a; bool has = b->world->slot_actor && b->world->slot_actor(b->world->ctx, (int64_t)(low + high*256), &a);
    owner_set(owners, owner_count, owner_cap, end - 1, has, has ? a : (qa_actor_id){0});
    if (is_qw) { bool has2 = b->world->slot_actor && b->world->slot_actor(b->world->ctx, (int64_t)(((low + high*256) >> 3) & 1023), &a); owner_set(owners, owner_count, owner_cap, -end, has2, has2 ? a : (qa_actor_id){0}); }
  }
}
bool qa_compat_qc_broadcast_messages_init(B *b, qa_compat_qc_presentation_world *world, void *emit_ctx, void (*emit)(void *, qa_q1_event, const qa_actor_id *), const qa_compat_qc_quakeworld_message_services *qw, const qa_compat_qc_netquake_message_services *nq) {
  memset(b, 0, sizeof *b); b->world = world; b->emit_ctx = emit_ctx; b->emit = emit; b->qw = qw; b->nq = nq; b->signon_buffers = 1;
  if (!qa_size_buf_init(&b->buffer, 1024, false)) return false;
  qa_q1_protocol_identity proto = {"q1-netquake", 15, 0, 0};
  qa_netquake_decoder_init(&b->decoder, proto, nq && nq->has_message_dialect ? nq->message_dialect : "known-retail", false);
  qa_quakeworld_profile qwproto = {"q1-quakeworld", 28, 0, 0};
  qa_quakeworld_decoder_init(&b->qw_decoder, qwproto);
  bool is_qw = world->program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD;
  if (is_qw && !qw) qa_range_error("QuakeWorld messages require routed message services");
  if (!is_qw && qw) qa_range_error("NetQuake messages cannot use QuakeWorld routes");
  if (is_qw && nq) qa_range_error("QuakeWorld cannot use NetQuake routes");
  static const struct { const char *name; int which; } defs[8] = {{"WriteByte",BW_BYTE},{"WriteChar",BW_CHAR},{"WriteShort",BW_SHORT},{"WriteLong",BW_LONG},{"WriteCoord",BW_COORD},{"WriteAngle",BW_ANGLE},{"WriteString",BW_STRING},{"WriteEntity",BW_ENTITY}};
  size_t n = 0;
  for (int i = 0; i < 8; i++) { b->ctxs[n].self=b; b->ctxs[n].which=defs[i].which; b->host_entries[n]=(qa_compat_qc_host_binding){defs[i].name, {&b->ctxs[n], write_fn}}; n++; }
  if (is_qw) { b->ctxs[n].self=b; b->ctxs[n].which=BW_MULTICAST; b->host_entries[n]=(qa_compat_qc_host_binding){"multicast", {&b->ctxs[n], write_fn}}; n++; }
  b->host_count = n;
  return true;
}
void qa_compat_qc_broadcast_messages_destroy(B *b) {
  qa_size_buf_destroy(&b->buffer); free(b->owners);
  qa_netquake_decoder_destroy(&b->decoder); qa_quakeworld_decoder_destroy(&b->qw_decoder);
  for (size_t i = 0; i < b->routed_count; i++) { free(b->routed[i].key); qa_size_buf_destroy(&b->routed[i].buffer); free(b->routed[i].owners); }
  free(b->routed); memset(b, 0, sizeof *b);
}
uint8_t *qa_compat_qc_broadcast_messages_bytes(B *b, size_t *out_len) { return qa_size_buf_bytes(&b->buffer, out_len); }
void qa_compat_qc_broadcast_messages_flush_signon(B *b) {
  if (b->nq) return;
  qa_compat_qc_routed_buffer *entry = routed_get(b, "signon");
  if (entry && entry->buffer.cursize >= QA_Q1_MAX_DATAGRAM - 512) {
    if (b->signon_buffers == 7) qa_range_error("QW MAX_SIGNON_BUFFERS exhausted");
    b->signon_buffers++;
    route_buffer(b, &entry->buffer, entry->owners, entry->owner_count, (qa_compat_qc_message_destination){QA_QC_DEST_SIGNON,false,{{NULL,NULL},0,0,NULL},{0,0,0},NULL});
    routed_delete(b, "signon");
  }
}
void qa_compat_qc_broadcast_messages_flush(B *b) {
  if (b->qw || b->nq) {
    size_t i = 0;
    while (i < b->routed_count) {
      qa_compat_qc_routed_buffer *entry = &b->routed[i];
      if (!entry->has_destination) { i++; continue; }
      char *key = strdup(entry->key);
      if (!entry->buffer.overflowed) route_buffer(b, &entry->buffer, entry->owners, entry->owner_count, entry->destination);
      routed_delete(b, key); free(key);
    }
    return;
  }
  size_t len = 0; uint8_t *bytes = qa_size_buf_bytes(&b->buffer, &len);
  size_t count = 0; qa_netquake_message *messages = qa_netquake_decoder_decode(&b->decoder, bytes, len, &count);
  emit_nq_effects(b, messages, count, b->owners, b->owner_count, b->buffer.maxsize, false, NULL);
  for (size_t i = 0; i < count; i++) qa_netquake_message_destroy(&messages[i]);
  free(messages); free(bytes);
  qa_size_buf_clear(&b->buffer); b->owner_count = 0;
}
uint8_t *qa_compat_qc_broadcast_capture_netquake_messages(B *b, const qa_netquake_message *messages, size_t count, size_t *out_len) {
  qa_size_buf buffer; if (!qa_size_buf_init(&buffer, 8000, false)) return NULL;
  for (size_t i = 0; i < count; i++) {
    if (strcmp(messages[i].kind, "entity") == 0) qa_range_error("QC cannot write host entity snapshots");
    qa_write_netquake_message(&buffer, b->decoder.protocol, &messages[i], b->decoder.rerelease_messages, b->decoder.standard_quake);
  }
  uint8_t *r = qa_size_buf_bytes(&buffer, out_len); qa_size_buf_destroy(&buffer); return r;
}
qa_netquake_message *qa_compat_qc_broadcast_restore_netquake_messages(B *b, const uint8_t *bytes, size_t length, size_t *out_count) {
  qa_netquake_decoder d; qa_q1_protocol_identity proto = {"q1-netquake", 15, 0, 0};
  qa_netquake_decoder_init(&d, proto, b->decoder.rerelease_messages, false);
  qa_netquake_message *r = qa_netquake_decoder_decode(&d, bytes, length, out_count);
  qa_netquake_decoder_destroy(&d); return r;
}
bool qa_compat_qc_saved_actor(const qa_actor_id *actor, qa_saved_actor_id *out) {
  if (!actor) { return false; } *out = qa_saved_actor_id_of(*actor); return true;
}
qa_compat_qc_captured_destination qa_compat_qc_capture_destination(qa_compat_qc_message_destination d) {
  qa_compat_qc_captured_destination r; memset(&r, 0, sizeof r); r.destination = d;
  if (d.kind == QA_QC_DEST_CLIENT) { r.has_saved = true; r.saved = qa_saved_actor_id_of(d.actor); }
  return r;
}
static bool read_qc_actor(const qa_save_reader *reader, qa_compat_qc_actor_resolver resolve, qa_actor_id *out) {
  if (qa_save_reader_is_null(reader)) return false;
  qa_saved_actor_id saved = qa_read_saved_actor(reader); *out = resolve.fn(resolve.ctx, saved); return true;
}
qa_compat_qc_message_destination qa_compat_qc_read_destination(const qa_save_reader *reader, qa_compat_qc_actor_resolver resolve) {
  qa_save_reader kf; qa_save_reader_field(reader, "kind", &kf);
  static const char *const kinds[4] = {"client","broadcast","signon","multicast"};
  const char *kind = qa_save_reader_choice_string(&kf, kinds, 4); qa_save_reader_destroy(&kf);
  qa_compat_qc_message_destination r; memset(&r, 0, sizeof r);
  if (strcmp(kind, "signon") == 0) { r.kind = QA_QC_DEST_SIGNON; return r; }
  qa_save_reader rf; qa_save_reader_field(reader, "reliable", &rf);
  if (strcmp(kind, "broadcast") == 0) { r.kind = QA_QC_DEST_BROADCAST; r.reliable = qa_save_reader_boolean(&rf); qa_save_reader_destroy(&rf); return r; }
  if (strcmp(kind, "client") == 0) {
    r.kind = QA_QC_DEST_CLIENT;
    qa_save_reader af; qa_save_reader_field(reader, "actor", &af);
    if (!read_qc_actor(&af, resolve, &r.actor)) qa_save_reader_fail(reader, "missing message recipient");
    qa_save_reader_destroy(&af);
    r.reliable = qa_save_reader_literal_boolean(&rf, true); qa_save_reader_destroy(&rf); return r;
  }
  r.kind = QA_QC_DEST_MULTICAST; r.reliable = qa_save_reader_boolean(&rf); qa_save_reader_destroy(&rf);
  qa_save_reader of, xf, yf, zf, vf;
  qa_save_reader_field(reader, "origin", &of);
  qa_save_reader_field(&of, "x", &xf); r.origin.x = qa_save_reader_finite(&xf); qa_save_reader_destroy(&xf);
  qa_save_reader_field(&of, "y", &yf); r.origin.y = qa_save_reader_finite(&yf); qa_save_reader_destroy(&yf);
  qa_save_reader_field(&of, "z", &zf); r.origin.z = qa_save_reader_finite(&zf); qa_save_reader_destroy(&zf);
  qa_save_reader_destroy(&of);
  qa_save_reader_field(reader, "visibility", &vf);
  static const char *const vis[3] = {"all","pvs","phs"};
  r.visibility = qa_save_reader_choice_string(&vf, vis, 3); qa_save_reader_destroy(&vf);
  return r;
}
/* Capture/restore */
static qa_save_value *owners_value(const qa_compat_qc_owner_entry *owners, size_t n) {
  qa_save_value *arr = qa_save_value_array(); if (!arr) return NULL;
  for (size_t i = 0; i < n; i++) {
    qa_save_value *o = qa_save_value_object(); if (!o) { qa_save_value_free(arr); return NULL; }
    qa_save_value_set(o, "offset", qa_save_value_number((double)owners[i].offset));
    if (owners[i].has_actor) { qa_saved_actor_id s = qa_saved_actor_id_of(owners[i].actor); qa_save_value *a = qa_save_value_object();
      qa_save_value_set(a, "slot", qa_save_value_number(s.slot)); qa_save_value_set(a, "generation", qa_save_value_number(s.generation)); qa_save_value_set(o, "actor", a); }
    else qa_save_value_set(o, "actor", qa_save_value_null());
    qa_save_value_push(arr, o);
  }
  return arr;
}
static qa_save_value *vec_value(double x, double y, double z) {
  qa_save_value *o = qa_save_value_object(); if (!o) return NULL;
  qa_save_value_set(o, "x", qa_save_value_number(x)); qa_save_value_set(o, "y", qa_save_value_number(y)); qa_save_value_set(o, "z", qa_save_value_number(z)); return o;
}
static qa_save_value *wire_value(const qa_captured_wire_entity *s, bool qw, double number, double flags) {
  qa_save_value *o = qa_save_value_object(); if (!o) return NULL;
  qa_save_value_set(o, "origin", vec_value(s->origin[0], s->origin[1], s->origin[2]));
  qa_save_value_set(o, "angles", vec_value(s->angles[0], s->angles[1], s->angles[2]));
  qa_save_value_set(o, "modelindex", qa_save_value_number(s->modelindex));
  qa_save_value_set(o, "frame", qa_save_value_number(s->frame));
  qa_save_value_set(o, "colormap", qa_save_value_number(s->colormap));
  qa_save_value_set(o, "skin", qa_save_value_number(s->skin));
  qa_save_value_set(o, "alpha", qa_save_value_number(s->alpha));
  qa_save_value_set(o, "scale", qa_save_value_number(s->scale));
  qa_save_value_set(o, "effects", qa_save_value_number(s->effects));
  if (qw) { qa_save_value_set(o, "number", qa_save_value_number(number)); qa_save_value_set(o, "flags", qa_save_value_number(flags)); }
  return o;
}
static qa_save_value *nq_decoder_value(B *b) {
  qa_nq_decoder_capture cap; qa_netquake_decoder_capture(&b->decoder, &cap);
  qa_save_value *o = qa_save_value_object();
  qa_save_value_set(o, "version", qa_save_value_number(cap.version));
  qa_save_value_set(o, "flags", qa_save_value_number(cap.flags));
  qa_save_value_set(o, "timeSeconds", qa_save_value_number(cap.time_seconds));
  qa_save_value *arr = qa_save_value_array();
  for (size_t i = 0; i < cap.count; i++) { qa_save_value *e = qa_save_value_object(); qa_save_value_set(e, "slot", qa_save_value_number(cap.baselines[i].slot)); qa_save_value_set(e, "state", wire_value(&cap.baselines[i].state, false, 0, 0)); qa_save_value_push(arr, e); }
  qa_save_value_set(o, "baselines", arr);
  qa_nq_decoder_capture_destroy(&cap); return o;
}
static qa_save_value *qw_decoder_value(B *b) {
  qa_qw_decoder_capture cap; qa_quakeworld_decoder_capture(&b->qw_decoder, &cap);
  qa_save_value *o = qa_save_value_object();
  qa_save_value_set(o, "version", qa_save_value_number(cap.version));
  qa_save_value_set(o, "flags", qa_save_value_number(cap.flags));
  qa_save_value_set(o, "playerModelIndex", qa_save_value_number(cap.player_model_index));
  qa_save_value *arr = qa_save_value_array();
  for (size_t i = 0; i < cap.baseline_count; i++) { qa_save_value *e = qa_save_value_object(); qa_save_value_set(e, "slot", qa_save_value_number(cap.baselines[i].slot)); qa_save_value_set(e, "state", wire_value(&cap.baselines[i].state.base, true, cap.baselines[i].state.number, cap.baselines[i].state.flags)); qa_save_value_push(arr, e); }
  qa_save_value_set(o, "baselines", arr);
  qa_save_value *frames = qa_save_value_array();
  for (size_t i = 0; i < cap.frame_count; i++) { qa_save_value *e = qa_save_value_object(); qa_save_value_set(e, "sequence", qa_save_value_number(cap.frames[i].sequence));
    qa_save_value *states = qa_save_value_array();
    for (size_t j = 0; j < cap.frames[i].count; j++) qa_save_value_push(states, wire_value(&cap.frames[i].states[j].base, true, cap.frames[i].states[j].number, cap.frames[i].states[j].flags));
    qa_save_value_set(e, "states", states); qa_save_value_push(frames, e); }
  qa_save_value_set(o, "frames", frames);
  qa_save_value *deltas = qa_save_value_array();
  for (size_t i = 0; i < cap.delta_count; i++) { qa_save_value *e = qa_save_value_object(); qa_save_value_set(e, "sequence", qa_save_value_number(cap.delta_requests[i].sequence));
    if (cap.delta_requests[i].has_base) qa_save_value_set(e, "base", qa_save_value_number(cap.delta_requests[i].base)); else qa_save_value_set(e, "base", qa_save_value_null());
    qa_save_value_push(deltas, e); }
  qa_save_value_set(o, "deltaRequests", deltas);
  qa_qw_decoder_capture_destroy(&cap); return o;
}
static qa_save_value *destination_value(qa_compat_qc_message_destination d) {
  qa_save_value *o = qa_save_value_object(); if (!o) return NULL;
  const char *kind = d.kind==QA_QC_DEST_CLIENT?"client":d.kind==QA_QC_DEST_SIGNON?"signon":d.kind==QA_QC_DEST_MULTICAST?"multicast":"broadcast";
  qa_save_value_set(o, "kind", qa_save_value_string(kind));
  if (d.kind == QA_QC_DEST_CLIENT) { qa_saved_actor_id s = qa_saved_actor_id_of(d.actor); qa_save_value *a = qa_save_value_object();
    qa_save_value_set(a, "slot", qa_save_value_number(s.slot)); qa_save_value_set(a, "generation", qa_save_value_number(s.generation)); qa_save_value_set(o, "actor", a); }
  if (d.kind != QA_QC_DEST_SIGNON) qa_save_value_set(o, "reliable", qa_save_value_boolean(d.reliable));
  if (d.kind == QA_QC_DEST_MULTICAST) { qa_save_value_set(o, "origin", vec_value(d.origin.x, d.origin.y, d.origin.z)); qa_save_value_set(o, "visibility", qa_save_value_string(d.visibility)); }
  return o;
}
qa_save_value *qa_compat_qc_broadcast_messages_capture(B *b) {
  qa_save_value *o = qa_save_value_object(); if (!o) return NULL;
  size_t len = 0; uint8_t *bytes = qa_size_buf_bytes(&b->buffer, &len);
  qa_save_value_set(o, "buffer", qa_save_value_bytes(bytes, len)); free(bytes);
  qa_save_value_set(o, "overflowed", qa_save_value_boolean(b->buffer.overflowed));
  qa_save_value_set(o, "owners", owners_value(b->owners, b->owner_count));
  qa_save_value_set(o, "signonBuffers", qa_save_value_number((double)b->signon_buffers));
  qa_save_value_set(o, "decoder", nq_decoder_value(b));
  qa_save_value_set(o, "qwDecoder", qw_decoder_value(b));
  qa_save_value *arr = qa_save_value_array();
  for (size_t i = 0; i < b->routed_count; i++) {
    qa_compat_qc_routed_buffer *e = &b->routed[i]; qa_save_value *ev = qa_save_value_object();
    qa_save_value_set(ev, "key", qa_save_value_string(strncmp(e->key, "client:", 7) == 0 ? "client" : e->key));
    size_t bl = 0; uint8_t *bb = qa_size_buf_bytes(&e->buffer, &bl);
    qa_save_value_set(ev, "bytes", qa_save_value_bytes(bb, bl)); free(bb);
    qa_save_value_set(ev, "maxsize", qa_save_value_number((double)e->buffer.maxsize));
    qa_save_value_set(ev, "allowoverflow", qa_save_value_boolean(e->buffer.allow_overflow));
    qa_save_value_set(ev, "overflowed", qa_save_value_boolean(e->buffer.overflowed));
    qa_save_value_set(ev, "owners", owners_value(e->owners, e->owner_count));
    qa_save_value_set(ev, "destination", e->has_destination ? destination_value(e->destination) : qa_save_value_null());
    qa_save_value_push(arr, ev);
  }
  qa_save_value_set(o, "routedBuffers", arr);
  return o;
}
static void restore_buffer(const qa_save_reader *reader, qa_size_buf *target, const uint8_t *bytes, size_t length, bool overflowed) {
  if ((long long)length > target->maxsize) qa_save_reader_fail(reader, "saved message exceeds buffer capacity");
  qa_size_buf_clear(target); memcpy(target->data, bytes, length); target->cursize = (long long)length; target->overflowed = overflowed;
}
static void read_owners(const qa_save_reader *field, qa_compat_qc_actor_resolver resolve, qa_compat_qc_owner_entry **out, size_t *out_n, size_t *out_cap) {
  *out = NULL; *out_n = 0; *out_cap = 0;
  size_t n = qa_save_reader_list_count(field);
  for (size_t i = 0; i < n; i++) {
    qa_save_reader item, of, af; qa_save_reader_item(field, i, &item);
    qa_save_reader_field(&item, "offset", &of); int64_t off = (int64_t)qa_save_reader_integer(&of); qa_save_reader_destroy(&of);
    qa_save_reader_field(&item, "actor", &af); qa_actor_id a; bool has = read_qc_actor(&af, resolve, &a); qa_save_reader_destroy(&af);
    owner_set(out, out_n, out_cap, off, has, has ? a : (qa_actor_id){0});
    qa_save_reader_destroy(&item);
  }
}
void qa_compat_qc_broadcast_messages_restore(B *b, const qa_save_value *value, qa_compat_qc_actor_resolver resolve) {
  qa_save_reader reader; qa_save_reader_init(&reader, value, "qc.messages");
  qa_save_reader bf, of, ow, sf, df, qf, rf;
  qa_save_reader_field(&reader, "buffer", &bf); size_t bl = 0; const uint8_t *bb = qa_save_reader_bytes(&bf, &bl);
  qa_save_reader_field(&reader, "overflowed", &of); bool ov = qa_save_reader_boolean(&of); qa_save_reader_destroy(&of);
  restore_buffer(&reader, &b->buffer, bb, bl, ov); qa_save_reader_destroy(&bf);
  free(b->owners); qa_save_reader_field(&reader, "owners", &ow); read_owners(&ow, resolve, &b->owners, &b->owner_count, &b->owner_capacity); qa_save_reader_destroy(&ow);
  qa_save_reader_field(&reader, "signonBuffers", &sf); b->signon_buffers = (int64_t)qa_save_reader_integer_min(&sf, 1); qa_save_reader_destroy(&sf);
  if (b->signon_buffers > 7) qa_save_reader_fail(&reader, "invalid signon buffer count");
  qa_save_reader_field(&reader, "decoder", &df); qa_netquake_decoder_restore(&b->decoder, &df); qa_save_reader_destroy(&df);
  qa_save_reader_field(&reader, "qwDecoder", &qf); qa_quakeworld_decoder_restore(&b->qw_decoder, &qf); qa_save_reader_destroy(&qf);
  for (size_t i = 0; i < b->routed_count; i++) { free(b->routed[i].key); qa_size_buf_destroy(&b->routed[i].buffer); free(b->routed[i].owners); }
  b->routed_count = 0;
  qa_save_reader_field(&reader, "routedBuffers", &rf);
  size_t n = qa_save_reader_list_count(&rf);
  for (size_t i = 0; i < n; i++) {
    qa_save_reader entry; qa_save_reader_item(&rf, i, &entry);
    qa_save_reader destf; qa_save_reader_field(&entry, "destination", &destf);
    qa_compat_qc_message_destination dest; bool has_dest = !qa_save_reader_is_null(&destf);
    memset(&dest, 0, sizeof dest);
    if (has_dest) dest = qa_compat_qc_read_destination(&destf, resolve);
    qa_save_reader_destroy(&destf);
    qa_save_reader kf; qa_save_reader_field(&entry, "key", &kf);
    static const char *const keys[5] = {"client","broadcast","all","signon","multicast"};
    const char *saved_key = qa_save_reader_choice_string(&kf, keys, 5); qa_save_reader_destroy(&kf);
    char key[96];
    if (has_dest && dest.kind == QA_QC_DEST_CLIENT) snprintf(key, sizeof key, "client:%lld:%lld", (long long)dest.actor.slot, (long long)dest.actor.generation);
    else snprintf(key, sizeof key, "%s", saved_key);
    qa_save_reader mf; qa_save_reader_field(&entry, "maxsize", &mf); int64_t maxsize = (int64_t)qa_save_reader_integer_min(&mf, 1); qa_save_reader_destroy(&mf);
    int64_t max_allowed = 8000 > QA_Q1_MAX_MSGLEN * 5 ? 8000 : QA_Q1_MAX_MSGLEN * 5;
    if (maxsize > max_allowed) qa_save_reader_fail(&reader, "invalid routed buffer capacity");
    qa_save_reader aof; qa_save_reader_field(&entry, "allowoverflow", &aof); bool ao = qa_save_reader_boolean(&aof); qa_save_reader_destroy(&aof);
    if (routed_get(b, key)) qa_save_reader_fail(&reader, "duplicate routed buffer");
    if (b->routed_count == b->routed_capacity) { size_t c = b->routed_capacity ? b->routed_capacity*2 : 4; b->routed = realloc(b->routed, c*sizeof *b->routed); b->routed_capacity = c; }
    qa_compat_qc_routed_buffer *rb = &b->routed[b->routed_count++]; memset(rb, 0, sizeof *rb);
    rb->key = strdup(key); qa_size_buf_init(&rb->buffer, maxsize, ao);
    qa_save_reader byf, ovf, owf;
    qa_save_reader_field(&entry, "bytes", &byf); size_t ebl = 0; const uint8_t *ebb = qa_save_reader_bytes(&byf, &ebl);
    qa_save_reader_field(&entry, "overflowed", &ovf); bool eov = qa_save_reader_boolean(&ovf); qa_save_reader_destroy(&ovf);
    restore_buffer(&reader, &rb->buffer, ebb, ebl, eov); qa_save_reader_destroy(&byf);
    qa_save_reader_field(&entry, "owners", &owf); read_owners(&owf, resolve, &rb->owners, &rb->owner_count, &rb->owner_capacity); qa_save_reader_destroy(&owf);
    rb->has_destination = has_dest; rb->destination = dest;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&rf);
  qa_save_reader_destroy(&reader);
}
qa_save_value *qa_compat_qc_broadcast_capture_entries(B *b, const qa_compat_qc_routed_message *entries, size_t count) {
  qa_save_value *o = qa_save_value_object(); if (!o) return NULL;
  qa_save_value_set(o, "version", qa_save_value_number((double)b->qw_decoder.protocol.version));
  qa_save_value_set(o, "flags", qa_save_value_number(qa_q1_protocol_flags(b->qw_decoder.protocol)));
  qa_save_value *arr = qa_save_value_array();
  for (size_t i = 0; i < count; i++) {
    if (strcmp(entries[i].message.kind, "packet-entities") == 0 || strcmp(entries[i].message.kind, "invalid-delta") == 0) qa_range_error("QC source messages cannot contain snapshot entity deltas");
    qa_size_buf buffer; qa_size_buf_init(&buffer, QA_Q1_MAX_MSGLEN * 5, false);
    qa_write_quakeworld_message(&buffer, b->qw_decoder.protocol, &entries[i].message);
    size_t len = 0; uint8_t *bytes = qa_size_buf_bytes(&buffer, &len); qa_size_buf_destroy(&buffer);
    qa_save_value *e = qa_save_value_object();
    qa_save_value_set(e, "bytes", qa_save_value_bytes(bytes, len)); free(bytes);
    if (entries[i].has_actor) { qa_saved_actor_id s = qa_saved_actor_id_of(entries[i].actor); qa_save_value *a = qa_save_value_object();
      qa_save_value_set(a, "slot", qa_save_value_number(s.slot)); qa_save_value_set(a, "generation", qa_save_value_number(s.generation)); qa_save_value_set(e, "actor", a); }
    else qa_save_value_set(e, "actor", qa_save_value_null());
    qa_save_value_push(arr, e);
  }
  qa_save_value_set(o, "entries", arr);
  return o;
}
void qa_compat_qc_routed_messages_free(qa_compat_qc_routed_message *entries, size_t count) {
  for (size_t i = 0; i < count; i++) qa_quakeworld_message_destroy(&entries[i].message);
  free(entries);
}
qa_compat_qc_routed_message *qa_compat_qc_broadcast_restore_entries(B *b, const qa_save_value *value, qa_compat_qc_actor_resolver resolve, size_t *out_count) {
  (void)b;
  qa_save_reader reader; qa_save_reader_init(&reader, value, "qc.routed");
  qa_save_reader vf, ff; qa_save_reader_field(&reader, "version", &vf); double version = qa_save_reader_integer_min(&vf, 0); qa_save_reader_destroy(&vf);
  qa_save_reader_field(&reader, "flags", &ff); double flags = qa_save_reader_integer_min(&ff, 0); qa_save_reader_destroy(&ff);
  qa_quakeworld_decoder decoder; qa_quakeworld_decoder_init(&decoder, qa_quakeworld_profile_make(version, flags));
  qa_save_reader ef; qa_save_reader_field(&reader, "entries", &ef);
  size_t n = qa_save_reader_list_count(&ef);
  qa_compat_qc_routed_message *out = calloc(n ? n : 1, sizeof *out);
  if (!out) { qa_save_reader_destroy(&ef); qa_save_reader_destroy(&reader); qa_quakeworld_decoder_destroy(&decoder); return NULL; }
  for (size_t i = 0; i < n; i++) {
    qa_save_reader item, bf, af; qa_save_reader_item(&ef, i, &item);
    qa_save_reader_field(&item, "bytes", &bf); size_t bl = 0; const uint8_t *bb = qa_save_reader_bytes(&bf, &bl);
    size_t mc = 0; qa_quakeworld_message *messages = qa_quakeworld_decoder_decode(&decoder, bb, bl, 0, &mc);
    qa_save_reader_destroy(&bf);
    if (mc != 1 || strcmp(messages[0].kind, "packet-entities") == 0 || strcmp(messages[0].kind, "invalid-delta") == 0) qa_save_reader_fail(&item, "invalid saved source message");
    out[i].message = messages[0]; free(messages);
    qa_save_reader_field(&item, "actor", &af); out[i].has_actor = read_qc_actor(&af, resolve, &out[i].actor); qa_save_reader_destroy(&af);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&ef); qa_save_reader_destroy(&reader); qa_quakeworld_decoder_destroy(&decoder);
  *out_count = n; return out;
}

