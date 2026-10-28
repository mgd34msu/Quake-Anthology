/* Port of src/app/bootstrap/remote-seat-source.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/remote_seat_source.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
struct qa_app_remote_seat_source {
  qa_app_remote_seat_source_options owner;
  qa_app_remote_seat_presentation_mirror remote;
  qa_app_remote_seat_network_mirror network;
  bool qw_downloads, q3_downloads, q3_content;
  double download_generation;
  bool closed, closing;
  char *all_skins;
};
qa_app_remote_seat_source *qa_app_remote_seat_source_create(const qa_app_remote_seat_source_options *owner) {
  /* Family guards run before construction, as in TS. */
  if (owner->family == QA_APP_REMOTE_FAMILY_QW && owner->address.kind == QA_NET_IPX)
    qa_range_error("QuakeWorld requires UDP");
  if (owner->family == QA_APP_REMOTE_FAMILY_Q3 && owner->address.kind != QA_NET_IPV4 && owner->address.kind != QA_NET_IPX)
    qa_range_error("Native Q3 remote requires IPv4");
  if (owner->family == QA_APP_REMOTE_FAMILY_Q2 && !owner->has_download_permission)
    qa_range_error("Q2 remote client has no download policy");
  qa_app_remote_seat_source *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->owner = *owner;
  s->all_skins = qa_input_dup("");
  s->remote = owner->make_remote(owner->factory_ctx, owner->family);
  s->network = owner->make_network(owner->factory_ctx, owner->family);
  return s;
}
void qa_app_remote_seat_source_destroy(qa_app_remote_seat_source *s) {
  if (!s) return;
  qa_app_remote_seat_source_close(s);
  qa_net_address_destroy(&s->owner.address);
  free(s->all_skins); free(s);
}
const qa_app_remote_seat_presentation_mirror *qa_app_remote_seat_source_remote(const qa_app_remote_seat_source *s) { return &s->remote; }
const qa_app_remote_seat_network_mirror *qa_app_remote_seat_source_network(const qa_app_remote_seat_source *s) { return &s->network; }
double qa_app_remote_seat_source_qport(const qa_app_remote_seat_source *s) { return s->owner.qport; }
const void *qa_app_remote_seat_source_transport(const qa_app_remote_seat_source *s) { return s->owner.transport; }
void qa_app_remote_seat_source_set_all_skins(qa_app_remote_seat_source *s, const char *value) {
  free(s->all_skins); s->all_skins = qa_input_dup(value);
  if (!s->all_skins) qa_range_error("Out of memory");
}
const char *qa_app_remote_seat_source_all_skins(const qa_app_remote_seat_source *s) { return s->all_skins; }
char *qa_app_remote_seat_source_pure_command(qa_app_remote_seat_source *s, double server_id) {
  if (!s->q3_content || s->owner.pure_command == NULL) return NULL;
  return s->owner.pure_command(s->owner.factory_ctx, server_id);
}
const char *const *qa_app_remote_seat_source_download_progress(qa_app_remote_seat_source *s, size_t *out_count) {
  if (s->remote.family == QA_APP_REMOTE_FAMILY_Q2 && s->remote.download_progress)
    return s->remote.download_progress(s->remote.ctx, out_count);
  if (s->qw_downloads && s->owner.qw_download_progress) return s->owner.qw_download_progress(s->owner.factory_ctx, out_count);
  if (s->q3_downloads && s->owner.q3_download_progress) return s->owner.q3_download_progress(s->owner.factory_ctx, out_count);
  *out_count = 0;
  return NULL;
}
void qa_app_remote_seat_source_cancel_downloads(qa_app_remote_seat_source *s) {
  if (s->remote.family == QA_APP_REMOTE_FAMILY_Q2 && s->remote.cancel_downloads) s->remote.cancel_downloads(s->remote.ctx);
  if (s->qw_downloads && s->owner.cancel_qw_downloads) s->owner.cancel_qw_downloads(s->owner.factory_ctx);
  if (s->q3_downloads && s->owner.cancel_q3_downloads) s->owner.cancel_q3_downloads(s->owner.factory_ctx);
}
void qa_app_remote_seat_source_retry_downloads(qa_app_remote_seat_source *s) {
  if (s->remote.family == QA_APP_REMOTE_FAMILY_Q2 && s->remote.retry_downloads) s->remote.retry_downloads(s->remote.ctx);
  if (s->qw_downloads && s->owner.retry_qw_downloads) s->owner.retry_qw_downloads(s->owner.factory_ctx);
  if (s->q3_downloads && s->owner.retry_q3_downloads) s->owner.retry_q3_downloads(s->owner.factory_ctx);
}
void qa_app_remote_seat_source_require_qw_downloads(qa_app_remote_seat_source *s) {
  if (!s->qw_downloads) qa_range_error("QW download before serverdata");
}
void qa_app_remote_seat_source_require_q3_downloads(qa_app_remote_seat_source *s) {
  if (!s->q3_downloads) qa_range_error("Q3 download has no content owner");
}
static void close_qw(qa_app_remote_seat_source *s) {
  s->download_generation++;
  if (s->qw_downloads && s->owner.close_qw_downloads) s->owner.close_qw_downloads(s->owner.factory_ctx);
  s->qw_downloads = false;
}
static void close_q3dl(qa_app_remote_seat_source *s) {
  if (s->q3_downloads && s->owner.close_q3_downloads) s->owner.close_q3_downloads(s->owner.factory_ctx);
  s->q3_downloads = false;
}
static void assert_open(qa_app_remote_seat_source *s) { if (s->closed) qa_range_error("Remote seat source is retired"); }
void qa_app_remote_seat_source_prepare_qw_downloads(qa_app_remote_seat_source *s, const char *game_directory) {
  close_qw(s);
  double generation = s->download_generation;
  /* Host performs selectContent + receiver construction; a generation
   * change during it mirrors the TS assertCurrent cancellation. */
  bool ok = s->owner.prepare_qw_downloads(s->owner.factory_ctx, game_directory);
  if (s->closed || generation != s->download_generation) qa_range_error("QW directory selection was cancelled");
  s->qw_downloads = ok;
}
bool qa_app_remote_seat_source_prepare_q3_downloads(qa_app_remote_seat_source *s, const void *connection) {
  assert_open(s);
  if (s->network.family != QA_APP_REMOTE_FAMILY_Q3 || s->network.native == NULL || s->network.native(s->network.ctx) != connection)
    qa_range_error("Q3 package download belongs to a retired connection");
  close_q3dl(s);
  bool pending = s->owner.prepare_q3_downloads(s->owner.factory_ctx, connection);
  s->q3_downloads = true;
  return pending;
}
const void *qa_app_remote_seat_source_load_q3_world(qa_app_remote_seat_source *s, const void *world, const void *connection, double generation) {
  (void)generation;
  const qa_app_remote_seat_source_hooks *hooks = &s->owner.hooks;
  const void *seed = hooks->current_content(hooks->ctx);
  if (seed == NULL) qa_range_error("Q3 world loading requires its selected content owner");
  assert_open(s);
  if (hooks->current_content(hooks->ctx) != seed) qa_range_error("Remote Q3 content loading was cancelled");
  const void *prepared = s->owner.load_q3_world_prepared(s->owner.factory_ctx, world, connection);
  assert_open(s);
  if (hooks->current_content(hooks->ctx) != seed) qa_range_error("Remote Q3 content loading was cancelled");
  const void *content = hooks->load_q3_world(hooks->ctx, world, connection, prepared);
  bool had_previous = s->q3_content;
  s->q3_content = true;
  if (had_previous && s->owner.close_q3_content) s->owner.close_q3_content(s->owner.factory_ctx);
  return content;
}
void qa_app_remote_seat_source_close(qa_app_remote_seat_source *s) {
  if (s->closing) return;
  s->closed = true;
  s->closing = true;
  close_qw(s); close_q3dl(s);
  if (s->network.close) s->network.close(s->network.ctx);
  if (s->q3_content && s->owner.close_q3_content) s->owner.close_q3_content(s->owner.factory_ctx);
  s->q3_content = false;
}
