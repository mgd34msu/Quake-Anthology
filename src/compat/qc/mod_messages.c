/* Port of src/compat/qc/mod-messages.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/mod_messages.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qc/message_routing.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
typedef qa_compat_qc_mod_messages M;
static bool actor_eq(qa_actor_id a, qa_actor_id b){return qa_same_actor(a, b);}
static bool client_fn_impl(void *ctx, qa_actor_id actor) {
  M *m = ctx;
  return !m->operations.has_clients ? m->operations.client_is_player(m->operations.ctx, actor) : m->operations.clients_for_actor(m->operations.ctx, actor);
}
typedef struct qa_compat_qc_mod_messages_host HostWrap;
/* TS host.message: the sink is optional and required only when used. */
static void wrap_message(void *ctx, qa_network_event event, const qa_actor_id *target) {
  HostWrap *w = ctx;
  if (!w->inner.message) qa_range_error("Source mod message requires a destination message sink");
  w->inner.message(w->inner.ctx, event, target);
}
/* TS emit(event, recipient = target ?? undefined). */
static void wrap_emit(void *ctx, qa_q1_event event, const qa_actor_id *recipient) {
  HostWrap *w = ctx; M *m = w->m;
  /* False publishesKills: the objective owner publishes the kill, so the borrower's copy would double it. */
  if (m->operations.withholds_kills && event.kind != NULL && strcmp(event.kind, "monster-killed") == 0) return;
  const qa_actor_id *to = recipient ? recipient : w->has_target ? &w->target : NULL;
  if (m->operations.emit) m->operations.emit(m->operations.ctx, event, to);
  else w->inner.emit(w->inner.ctx, event, to);
}
static void wrap_present(void *ctx, qa_netquake_message message, const qa_actor_id *target, qa_compat_qc_local_messages state, const qa_actor_id *fixed) {
  HostWrap *w = ctx; w->inner.present_local(w->inner.ctx, message, target, state, fixed);
}
/* TS muzzle: players only, body origin with the camera's angles. */
static bool wrap_muzzle(void *ctx, qa_actor_id actor, qa_vec3 *origin, qa_vec3 *angles) {
  HostWrap *w = ctx; M *m = w->m;
  if (!m->operations.body_origin || !m->operations.camera_angles) return w->inner.muzzle(w->inner.ctx, actor, origin, angles);
  size_t count = 0; const qa_actor_id *players = m->operations.players(m->operations.ctx, &count);
  bool player = false; for (size_t i = 0; i < count; i++) if (actor_eq(players[i], actor)) player = true;
  if (!player || !m->operations.body_origin(m->operations.ctx, actor, origin)) return false;
  return m->operations.camera_angles(m->operations.ctx, actor, angles);
}
static qa_actor_id wrap_actor_of(void *ctx, int64_t slot) { HostWrap *w = ctx; return w->m->operations.world_actor(w->m->operations.ctx, slot); }
static qa_compat_qc_local_message_host host_of(M *m, const qa_actor_id *target) {
  size_t count = 0; const qa_actor_id *players = m->operations.players(m->operations.ctx, &count);
  qa_actor_id source = m->operations.world_actor(m->operations.ctx, 0);
  qa_compat_qc_local_message_host inner = m->operations.host(m->operations.host_ctx, target, m->operations.time_seconds(m->operations.ctx), players, count, source);
  HostWrap *w = &m->hosts[m->host_next++ % (sizeof m->hosts / sizeof m->hosts[0])];
  w->m = m; w->inner = inner; w->has_target = target != NULL; if (target) w->target = *target;
  qa_compat_qc_local_message_host host = inner;
  host.ctx = w; host.message = wrap_message; host.emit = wrap_emit; host.muzzle = wrap_muzzle; host.actor_of = wrap_actor_of;
  if (inner.present_local) host.present_local = wrap_present;
  return host;
}
static int64_t admitted_cursor(M *m, qa_actor_id actor) { for(size_t i=0;i<m->admitted_count;i++) if(actor_eq(m->admitted[i].actor,actor)) return m->admitted[i].cursor; return 0; }
static void admitted_set(M *m, qa_actor_id actor, int64_t cursor) {
  for(size_t i=0;i<m->admitted_count;i++) if(actor_eq(m->admitted[i].actor,actor)){ m->admitted[i].cursor=cursor; return; }
  if(m->admitted_count==m->admitted_capacity){ size_t c=m->admitted_capacity?m->admitted_capacity*2:8; m->admitted=realloc(m->admitted,c*sizeof *m->admitted); if(!m->admitted) qa_range_error("mod messages allocation failed"); m->admitted_capacity=c; }
  m->admitted[m->admitted_count].actor=actor; m->admitted[m->admitted_count++].cursor=cursor;
}
static void admitted_delete(M *m, qa_actor_id actor) { for(size_t i=0;i<m->admitted_count;i++) if(actor_eq(m->admitted[i].actor,actor)){ memmove(&m->admitted[i],&m->admitted[i+1],(m->admitted_count-i-1)*sizeof *m->admitted); m->admitted_count--; return; } }
static void admit_actor(M *m, qa_actor_id actor) {
  m->local.admit(m->local.ctx, actor);
  int64_t cursor = admitted_cursor(m, actor);
  for (int64_t index = cursor; index < (int64_t)m->signon_count; index++)
    qa_compat_qc_present_quakeworld_message(m->signon[index], &actor, m->local.messages, host_of(m, &actor));
  admitted_set(m, actor, (int64_t)m->signon_count);
}
static void retire_client(M *m, qa_actor_id actor) { m->local.retire(m->local.ctx, actor); admitted_delete(m, actor); }
typedef struct { M *m; qa_actor_id actor; } OriginCtx;
static qa_vec3 origin_of(void *ctx) {
  OriginCtx *c = ctx; qa_vec3 out;
  if (!c->m->operations.body_origin(c->m->operations.ctx, c->actor, &out)) qa_range_error("QW multicast recipient has no destination body");
  return out;
}
typedef struct { M *m; } VisCtx;
static qa_q1_client_visibility_scene visibility_of(void *ctx) {
  VisCtx *c = ctx; qa_q1_client_visibility_scene scene;
  if (!c->m->operations.visibility(c->m->operations.ctx, &scene)) qa_range_error("QW multicast requires destination PVS/PHS queries");
  return scene;
}
typedef struct { const qa_compat_qc_routed_message *entries; size_t count; } ModCopyResolve;
static qa_actor_id copy_resolve(void *ctx, qa_saved_actor_id saved) {
  ModCopyResolve *cr = ctx;
  for (size_t i = 0; i < cr->count; i++) if (cr->entries[i].has_actor && cr->entries[i].actor.slot == (int64_t)saved.slot && cr->entries[i].actor.generation == (int64_t)saved.generation) return cr->entries[i].actor;
  qa_range_error("signon copy lost its source actor");
}
static void qw_route(void *ctx, const qa_compat_qc_routed_message *entries, size_t count, qa_compat_qc_message_destination destination) {
  M *m = ctx;
  if (destination.kind == QA_QC_DEST_SIGNON) {
    /* Deep-copy via encode/decode: broadcast frees its entries after route(). */
    qa_save_value *tree = qa_compat_qc_broadcast_capture_entries(&m->messages, entries, count);
    ModCopyResolve cr = {entries, count};
    size_t copied_count = 0;
    qa_compat_qc_routed_message *copied = qa_compat_qc_broadcast_restore_entries(&m->messages, tree, (qa_compat_qc_actor_resolver){&cr, copy_resolve}, &copied_count);
    qa_save_value_free(tree);
    for (size_t i = 0; i < copied_count; i++) { if(m->signon_count==m->signon_capacity){ size_t c=m->signon_capacity?m->signon_capacity*2:8; m->signon=realloc(m->signon,c*sizeof *m->signon); if(!m->signon) qa_range_error("mod messages allocation failed"); m->signon_capacity=c; }
      m->signon[m->signon_count++] = copied[i]; }
    free(copied);
    qa_compat_qc_mod_messages_start(m);
  } else if (destination.kind == QA_QC_DEST_BROADCAST) {
    qa_compat_qc_mod_messages_start(m);
    for (size_t i = 0; i < count; i++) qa_compat_qc_present_quakeworld_message(entries[i], NULL, m->local.messages, host_of(m, NULL));
  } else {
    size_t player_count = 0; const qa_actor_id *players = m->operations.players(m->operations.ctx, &player_count);
    const qa_actor_id *recipients = destination.kind == QA_QC_DEST_CLIENT ? &destination.actor : players;
    size_t recipient_count = destination.kind == QA_QC_DEST_CLIENT ? 1 : player_count;
    for (size_t ri = 0; ri < recipient_count; ri++) {
      qa_actor_id actor = recipients[ri];
      OriginCtx oc = {m, actor}; VisCtx vc = {m};
      if (qa_compat_qc_receives_quakeworld_message(actor, destination, (qa_compat_qc_message_origin){&oc, origin_of}, (qa_compat_qc_message_visibility){&vc, visibility_of})) {
        if (!client_fn_impl(m, actor)) { retire_client(m, actor); continue; }
        admit_actor(m, actor);
        for (size_t i = 0; i < count; i++) qa_compat_qc_present_quakeworld_message(entries[i], &actor, m->local.messages, host_of(m, &actor));
      }
    }
  }
}
static bool qw_loading(void *ctx) { M *m=ctx; return m->operations.loading(m->operations.ctx); }
static bool qw_phs(void *ctx) { M *m=ctx; return m->operations.phs(m->operations.ctx); }
static void nq_route(void *ctx, const qa_netquake_message *messages, size_t count, qa_compat_qc_message_destination destination, const qa_actor_id *view_targets, const bool *view_has, size_t view_count) {
  M *m = ctx; (void)view_count;
  if (destination.kind == QA_QC_DEST_MULTICAST) qa_range_error("NetQuake cannot route a multicast message");
  const qa_actor_id *target = destination.kind == QA_QC_DEST_CLIENT ? &destination.actor : NULL;
  size_t player_count = 0; const qa_actor_id *players = m->operations.players(m->operations.ctx, &player_count);
  for (size_t i = 0; i < player_count; i++) m->local.admit(m->local.ctx, players[i]);
  for (size_t i = 0; i < count; i++) {
    if (strcmp(messages[i].kind, "set-view") == 0 && !(view_has && view_has[i])) qa_range_error("QC camera message has no captured source actor");
    m->local.messages.receive(m->local.messages.ctx, &messages[i], 1, target, view_has && view_has[i] ? &view_targets[i] : NULL);
    /* presentQuakeCLocalMessage via host.present_local */
    qa_compat_qc_local_message_host host = host_of(m, target);
    host.present_local(host.ctx, messages[i], target, m->local.messages, NULL);
  }
}
static bool nq_native(void *ctx) { (void)ctx; return false; }
static void on_client_event(void *ctx, qa_mod_client_event event) {
  M *m = ctx;
  if (strcmp(event.kind, "admitted") == 0) admit_actor(m, event.identity.actor);
  else if (strcmp(event.kind, "disconnecting") == 0) retire_client(m, event.identity.actor);
}
static void on_release(void *ctx, qa_owned_actor actor) { retire_client(ctx, actor.id); }
static void emit_event(void *ctx, qa_q1_event effect, const qa_actor_id *recipient) {
  M *m = ctx; m->operations.emit(m->operations.ctx, effect, recipient);
}
bool qa_compat_qc_mod_messages_init(M *m, qa_compat_qc_presentation_world *world, qa_session_actor_registry *actors, qa_content_id content, qa_compat_qc_mod_messages_operations ops, qa_compat_qc_local_message_ops local) {
  memset(m, 0, sizeof *m); m->world = world; m->content = content; m->operations = ops; m->local = local; m->actors = actors; m->self_ref = m;
  m->is_quakeworld = world->program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD;
  const qa_compat_qc_quakeworld_message_services *qw = NULL;
  const qa_compat_qc_netquake_message_services *nq = NULL;
  if (m->is_quakeworld) {
    m->qw_routes = (qa_compat_qc_quakeworld_message_services){m, qw_loading, client_fn_impl, qw_phs, qw_route};
    qw = &m->qw_routes;
    if (ops.subscribe_clients) { m->unsubscribe = ops.subscribe_clients(ops.ctx, on_client_event, m); m->has_unsubscribe = true; }
  } else {
    m->nq_routes = (qa_compat_qc_netquake_message_services){m, NULL, false, nq_native, NULL, qw_loading, client_fn_impl, nq_route};
    nq = &m->nq_routes;
  }
  if (!qa_compat_qc_broadcast_messages_init(&m->messages, world, m, emit_event, qw, nq)) return false;
  m->release_id = qa_session_actor_registry_on_release(actors, on_release, m);
  return true;
}
void qa_compat_qc_mod_messages_destroy(M *m) {
  qa_compat_qc_mod_messages_close(m);
  qa_compat_qc_broadcast_messages_destroy(&m->messages);
  for (size_t i = 0; i < m->signon_count; i++) qa_quakeworld_message_destroy(&m->signon[i].message);
  free(m->signon); free(m->admitted); memset(m, 0, sizeof *m);
}
const qa_compat_qc_local_messages *qa_compat_qc_mod_messages_client_state(M *m) { return &m->local.messages; }
void qa_compat_qc_mod_messages_start(M *m) {
  if (!m->is_quakeworld) return;
  size_t count = 0; const qa_actor_id *players = m->operations.players(m->operations.ctx, &count);
  for (size_t i = 0; i < count; i++) admit_actor(m, players[i]);
}
static qa_save_value *saved_value(qa_actor_id actor) {
  qa_saved_actor_id s = qa_saved_actor_id_of(actor); qa_save_value *o = qa_save_value_object();
  qa_save_value_set(o, "slot", qa_save_value_number(s.slot)); qa_save_value_set(o, "generation", qa_save_value_number(s.generation)); return o;
}
qa_save_value *qa_compat_qc_mod_messages_capture(M *m) {
  qa_save_value *o = qa_save_value_object(); if (!o) return NULL;
  qa_compat_qc_local_capture local = m->local.capture(m->local.ctx);
  qa_compat_qc_local_views views = m->local.capture_views(m->local.ctx);
  qa_save_value_set(o, "messages", qa_compat_qc_broadcast_messages_capture(&m->messages));
  size_t len = 0; uint8_t *baseline = qa_compat_qc_broadcast_capture_netquake_messages(&m->messages, local.baseline, local.baseline_count, &len);
  qa_save_value_set(o, "baseline", qa_save_value_bytes(baseline, len)); free(baseline);
  qa_save_value *clients = qa_save_value_array();
  for (size_t i = 0; i < local.client_count; i++) {
    qa_save_value *c = qa_save_value_object();
    qa_save_value_set(c, "actor", saved_value(local.clients[i].actor));
    size_t ml = 0; uint8_t *mb = qa_compat_qc_broadcast_capture_netquake_messages(&m->messages, local.clients[i].messages, local.clients[i].message_count, &ml);
    qa_save_value_set(c, "messages", qa_save_value_bytes(mb, ml)); free(mb);
    qa_save_value_push(clients, c);
  }
  qa_save_value_set(o, "clients", clients);
  qa_save_value *views_value = qa_save_value_object();
  qa_save_value_set(views_value, "baseline", views.has_baseline ? saved_value(views.baseline) : qa_save_value_null());
  qa_save_value *vclients = qa_save_value_array();
  for (size_t i = 0; i < views.client_count; i++) {
    qa_save_value *c = qa_save_value_object();
    qa_save_value_set(c, "actor", saved_value(views.clients[i].actor));
    qa_save_value_set(c, "target", saved_value(views.clients[i].target));
    qa_save_value_push(vclients, c);
  }
  qa_save_value_set(views_value, "clients", vclients);
  qa_save_value_set(o, "views", views_value);
  if (m->is_quakeworld) {
    qa_save_value *qw = qa_save_value_object();
    qa_save_value_set(qw, "signon", qa_compat_qc_broadcast_capture_entries(&m->messages, m->signon, m->signon_count));
    qa_save_value *adm = qa_save_value_array();
    for (size_t i = 0; i < m->admitted_count; i++) {
      qa_save_value *c = qa_save_value_object();
      qa_save_value_set(c, "actor", saved_value(m->admitted[i].actor));
      qa_save_value_set(c, "cursor", qa_save_value_number((double)m->admitted[i].cursor));
      qa_save_value_push(adm, c);
    }
    qa_save_value_set(qw, "admitted", adm);
    qa_save_value_set(o, "quakeworld", qw);
  }
  m->local.capture_free(m->local.ctx, local); m->local.views_free(m->local.ctx, views);
  return o;
}
void qa_compat_qc_mod_messages_restore(M *m, const qa_save_reader *reader, qa_compat_qc_actor_resolver resolve) {
  qa_save_reader mf; qa_save_reader_field(reader, "messages", &mf);
  qa_save_value *messages_value = (qa_save_value *)mf.value;
  qa_compat_qc_broadcast_messages_restore(&m->messages, messages_value, resolve);
  qa_save_reader_destroy(&mf);
  qa_save_reader bf; qa_save_reader_field(reader, "baseline", &bf);
  size_t bl = 0; const uint8_t *bb = qa_save_reader_bytes(&bf, &bl);
  size_t bc = 0; qa_netquake_message *baseline = qa_compat_qc_broadcast_restore_netquake_messages(&m->messages, bb, bl, &bc);
  qa_save_reader_destroy(&bf);
  qa_save_reader cf; qa_save_reader_field(reader, "clients", &cf);
  size_t cn = qa_save_reader_list_count(&cf);
  qa_compat_qc_local_client_messages *clients = calloc(cn ? cn : 1, sizeof *clients);
  for (size_t i = 0; i < cn; i++) {
    qa_save_reader item, af, msgf; qa_save_reader_item(&cf, i, &item);
    qa_save_reader_field(&item, "actor", &af); clients[i].actor = resolve.fn(resolve.ctx, qa_read_saved_actor(&af)); qa_save_reader_destroy(&af);
    qa_save_reader_field(&item, "messages", &msgf); size_t ml = 0; const uint8_t *mb = qa_save_reader_bytes(&msgf, &ml);
    clients[i].messages = qa_compat_qc_broadcast_restore_netquake_messages(&m->messages, mb, ml, &clients[i].message_count);
    qa_save_reader_destroy(&msgf); qa_save_reader_destroy(&item);
  }
  m->local.restore(m->local.ctx, baseline, bc, clients, cn);
  for (size_t i = 0; i < cn; i++) { for (size_t j = 0; j < clients[i].message_count; j++) { qa_netquake_message_destroy(&clients[i].messages[j]); } free(clients[i].messages); }
  free(clients);
  for (size_t i = 0; i < bc; i++) { qa_netquake_message_destroy(&baseline[i]); } free(baseline);
  qa_save_reader_destroy(&cf);
  qa_save_reader vf; qa_save_reader_field(reader, "views", &vf);
  if (!qa_save_reader_is_undefined(&vf)) {
    qa_save_reader bvf; qa_save_reader_field(&vf, "baseline", &bvf);
    qa_actor_id baseline_actor; const qa_actor_id *baseline_ptr = NULL;
    if (!qa_save_reader_is_null(&bvf)) { baseline_actor = resolve.fn(resolve.ctx, qa_read_saved_actor(&bvf)); baseline_ptr = &baseline_actor; }
    qa_save_reader_destroy(&bvf);
    qa_save_reader vcf; qa_save_reader_field(&vf, "clients", &vcf);
    size_t vn = qa_save_reader_list_count(&vcf);
    qa_compat_qc_local_view *views = calloc(vn ? vn : 1, sizeof *views);
    for (size_t i = 0; i < vn; i++) {
      qa_save_reader item, af, tf; qa_save_reader_item(&vcf, i, &item);
      qa_save_reader_field(&item, "actor", &af); views[i].actor = resolve.fn(resolve.ctx, qa_read_saved_actor(&af)); qa_save_reader_destroy(&af);
      qa_save_reader_field(&item, "target", &tf); views[i].target = resolve.fn(resolve.ctx, qa_read_saved_actor(&tf)); qa_save_reader_destroy(&tf);
      qa_save_reader_destroy(&item);
    }
    m->local.restore_views(m->local.ctx, baseline_ptr, views, vn);
    free(views); qa_save_reader_destroy(&vcf);
  }
  qa_save_reader_destroy(&vf);
  if (m->is_quakeworld) {
    qa_save_reader qf; qa_save_reader_field(reader, "quakeworld", &qf);
    qa_save_reader sf; qa_save_reader_field(&qf, "signon", &sf);
    size_t sn = 0; qa_compat_qc_routed_message *signon = qa_compat_qc_broadcast_restore_entries(&m->messages, (qa_save_value *)sf.value, resolve, &sn);
    qa_save_reader_destroy(&sf);
    for (size_t i = 0; i < m->signon_count; i++) qa_quakeworld_message_destroy(&m->signon[i].message);
    free(m->signon); m->signon = signon; m->signon_count = sn; m->signon_capacity = sn;
    m->admitted_count = 0;
    qa_save_reader adf; qa_save_reader_field(&qf, "admitted", &adf);
    size_t an = qa_save_reader_list_count(&adf);
    for (size_t i = 0; i < an; i++) {
      qa_save_reader item, af, cuf; qa_save_reader_item(&adf, i, &item);
      qa_save_reader_field(&item, "actor", &af); qa_actor_id actor = resolve.fn(resolve.ctx, qa_read_saved_actor(&af)); qa_save_reader_destroy(&af);
      qa_save_reader_field(&item, "cursor", &cuf); int64_t cursor = (int64_t)qa_save_reader_integer_min(&cuf, 0); qa_save_reader_destroy(&cuf);
      bool dup = false; for(size_t j=0;j<m->admitted_count;j++) if(actor_eq(m->admitted[j].actor,actor)) dup=true;
      if (cursor > (int64_t)sn || dup) qa_save_reader_fail(&item, "Invalid component signon cursor");
      admitted_set(m, actor, cursor);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&adf); qa_save_reader_destroy(&qf);
  }
}
void qa_compat_qc_mod_messages_close(M *m) {
  if (m->has_unsubscribe) { m->unsubscribe.invoke(m->unsubscribe.ctx); m->has_unsubscribe = false; }
  for (size_t i = 0; i < m->signon_count; i++) qa_quakeworld_message_destroy(&m->signon[i].message);
  m->signon_count = 0; m->admitted_count = 0;
  if (m->actors) { qa_session_actor_registry_off_release(m->actors, m->release_id); m->actors = NULL; }
}

/* Landed-owner adapter: qa_app_sim_quakec_local_messages behind the ops seam. */
typedef qa_compat_qc_local_message_app_adapter AppAdapter;
static void app_admit(void *ctx, qa_actor_id actor) {
  qa_app_sim_quakec_local_messages_admit(((AppAdapter *)ctx)->state, actor);
}
static void app_retire(void *ctx, qa_actor_id actor) {
  qa_app_sim_quakec_local_messages_retire(((AppAdapter *)ctx)->state, actor);
}
static void app_receive(void *ctx, const qa_netquake_message *messages, size_t count,
                        const qa_actor_id *target, const qa_actor_id *actor) {
  qa_app_sim_quakec_local_messages_receive(((AppAdapter *)ctx)->state, messages, count, target,
                                           actor != NULL, actor);
}
static qa_compat_qc_local_capture app_capture(void *ctx) {
  AppAdapter *a = ctx;
  if (a->has_held) qa_app_sim_quakec_capture_free(&a->held);
  a->held = qa_app_sim_quakec_local_messages_capture(a->state);
  a->has_held = true;
  qa_compat_qc_local_capture out;
  memset(&out, 0, sizeof out);
  out.baseline = a->held.baseline;
  out.baseline_count = a->held.baseline_count;
  out.client_count = a->held.client_count;
  if (out.client_count > 0) {
    out.clients = malloc(out.client_count * sizeof *out.clients);
    if (out.clients == NULL) qa_range_error("mod messages allocation failed");
    for (size_t i = 0; i < out.client_count; i++) {
      out.clients[i].actor = a->held.clients[i].actor;
      out.clients[i].messages = (qa_netquake_message *)a->held.clients[i].messages;
      out.clients[i].message_count = a->held.clients[i].message_count;
    }
  }
  return out;
}
static void app_capture_free(void *ctx, qa_compat_qc_local_capture capture) {
  AppAdapter *a = ctx;
  free(capture.clients);
  if (a->has_held) { qa_app_sim_quakec_capture_free(&a->held); a->has_held = false; }
}
static qa_compat_qc_local_views app_capture_views(void *ctx) {
  qa_app_sim_quakec_captured_views views =
      qa_app_sim_quakec_local_messages_capture_views(((AppAdapter *)ctx)->state);
  qa_compat_qc_local_views out;
  memset(&out, 0, sizeof out);
  out.has_baseline = views.has_baseline;
  out.baseline = views.baseline;
  out.client_count = views.client_count;
  if (out.client_count > 0) {
    out.clients = malloc(out.client_count * sizeof *out.clients);
    if (out.clients == NULL) qa_range_error("mod messages allocation failed");
    for (size_t i = 0; i < out.client_count; i++) {
      out.clients[i].actor = views.clients[i].actor;
      out.clients[i].target = views.clients[i].target;
    }
  }
  free(views.clients);
  return out;
}
static void app_views_free(void *ctx, qa_compat_qc_local_views views) {
  (void)ctx;
  free(views.clients);
}
static void app_restore(void *ctx, const qa_netquake_message *baseline, size_t baseline_count,
                        const qa_compat_qc_local_client_messages *clients, size_t client_count) {
  qa_app_sim_quakec_captured_client *converted = NULL;
  if (client_count > 0) {
    converted = malloc(client_count * sizeof *converted);
    if (converted == NULL) qa_range_error("mod messages allocation failed");
    for (size_t i = 0; i < client_count; i++) {
      converted[i].actor = clients[i].actor;
      converted[i].messages = clients[i].messages;
      converted[i].message_count = clients[i].message_count;
    }
  }
  qa_app_sim_quakec_local_messages_restore(((AppAdapter *)ctx)->state, baseline, baseline_count,
                                           converted, client_count);
  free(converted);
}
static void app_restore_views(void *ctx, const qa_actor_id *baseline,
                              const qa_compat_qc_local_view *clients, size_t client_count) {
  qa_app_sim_quakec_view_entry *converted = NULL;
  if (client_count > 0) {
    converted = malloc(client_count * sizeof *converted);
    if (converted == NULL) qa_range_error("mod messages allocation failed");
    for (size_t i = 0; i < client_count; i++) {
      converted[i].actor = clients[i].actor;
      converted[i].target = clients[i].target;
    }
  }
  qa_app_sim_quakec_local_messages_restore_views(((AppAdapter *)ctx)->state, baseline, converted,
                                                 client_count);
  free(converted);
}
void qa_compat_qc_local_message_app_adapter_init(qa_compat_qc_local_message_app_adapter *adapter,
                                                 qa_app_sim_quakec_local_messages *state) {
  memset(adapter, 0, sizeof *adapter);
  adapter->state = state;
  adapter->ops.ctx = adapter;
  adapter->ops.messages.ctx = adapter;
  adapter->ops.messages.receive = app_receive;
  adapter->ops.admit = app_admit;
  adapter->ops.retire = app_retire;
  adapter->ops.capture = app_capture;
  adapter->ops.capture_free = app_capture_free;
  adapter->ops.capture_views = app_capture_views;
  adapter->ops.views_free = app_views_free;
  adapter->ops.restore = app_restore;
  adapter->ops.restore_views = app_restore_views;
}
void qa_compat_qc_local_message_app_adapter_destroy(qa_compat_qc_local_message_app_adapter *adapter) {
  if (adapter->has_held) qa_app_sim_quakec_capture_free(&adapter->held);
  adapter->has_held = false;
}
