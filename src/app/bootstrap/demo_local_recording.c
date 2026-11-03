/* Port of src/app/bootstrap/demo-local-recording.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/demo_local_recording.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q1.h"
#include "qa/app/bootstrap/network/q2_client_receiver.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/session.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/netquake.h"
#include "qa/network/q1/quakeworld.h"
#include "qa/network/q1/qw_constants.h"
#include "qa/network/q1/qw_recording.h"
#include "qa/network/q1/session.h"
#include "qa/network/q1/wire_types.h"
#include "qa/network/q2/codec.h"
#include "qa/network/q2/frames.h"
#include "qa/network/q2/server_messages.h"
#include "qa/network/q2/server_write.h"
#include "qa/network/q3/configstrings.h"
#include "qa/network/q3/server_message.h"

/* Packet storage of one seed: the packet rows and every buffer they
 * borrow (TS Uint8Array lifetimes). */
typedef struct seed_arena {
  qa_app_demo_recording_packet *packets; size_t packet_count, packet_cap;
  uint8_t **buffers; size_t buffer_count, buffer_cap;
  qa_quakeworld_demo_record *qw_records; size_t qw_record_count;
} seed_arena;

/* TS Prepared: source + seed + the per-kind retained wire state. */
typedef struct prepared {
  qa_app_local_recording_source source;
  qa_app_demo_recording_identity identity;
  seed_arena arena;
  qa_app_q1_game_state q1_state;
  qa_qw_entity_state *qw_states; qa_qw_baseline_map_entry *qw_baselines; size_t qw_baseline_count;
  qa_app_q2_game_state q2_state; qa_q2_wire_codec wire; bool has_wire;
  const qa_q3_gamestate *q3_state;
} prepared;

struct qa_app_local_demo_recording {
  qa_app_local_recording_read_fn read; void *read_ctx;
  bool has_sink; qa_app_demo_recording_sink sink;
  prepared *prepared; /* NULL is TS null */
  double sequence;
  int64_t command_sequence;
};

static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size ? size : 1);
  if (next == NULL) qa_range_error("Out of memory");
  return next;
}

static void arena_destroy(seed_arena *arena) {
  for (size_t i = 0; i < arena->buffer_count; i++) free(arena->buffers[i]);
  free(arena->buffers);
  free(arena->packets);
  for (size_t i = 0; i < arena->qw_record_count; i++) qa_quakeworld_demo_record_destroy(&arena->qw_records[i]);
  free(arena->qw_records);
  memset(arena, 0, sizeof *arena);
}
static uint8_t *arena_keep(seed_arena *arena, uint8_t *bytes) {
  if (arena->buffer_count == arena->buffer_cap) {
    arena->buffer_cap = arena->buffer_cap ? arena->buffer_cap * 2 : 16;
    arena->buffers = xrealloc(arena->buffers, arena->buffer_cap * sizeof *arena->buffers);
  }
  arena->buffers[arena->buffer_count++] = bytes;
  return bytes;
}
static void arena_push(seed_arena *arena, qa_app_demo_recording_packet packet) {
  if (arena->packet_count == arena->packet_cap) {
    arena->packet_cap = arena->packet_cap ? arena->packet_cap * 2 : 16;
    arena->packets = xrealloc(arena->packets, arena->packet_cap * sizeof *arena->packets);
  }
  arena->packets[arena->packet_count++] = packet;
}

static void prepared_destroy(prepared *p) {
  if (p == NULL) return;
  arena_destroy(&p->arena);
  free(p->qw_states); free(p->qw_baselines);
  if (p->has_wire) qa_q2_wire_codec_destroy(&p->wire);
  free(p);
}
static void prepared_release(void *arg) { prepared_destroy(arg); }

qa_app_local_demo_recording *qa_app_local_demo_recording_create(
    qa_app_local_recording_read_fn read, void *read_ctx) {
  qa_app_local_demo_recording *recording = calloc(1, sizeof *recording);
  if (recording == NULL) return NULL;
  recording->read = read;
  recording->read_ctx = read_ctx;
  return recording;
}
void qa_app_local_demo_recording_destroy(qa_app_local_demo_recording *recording) {
  if (recording == NULL) return;
  prepared_destroy(recording->prepared);
  free(recording);
}

/* TS `admission.kind !== 'supported'` -> Error(reasons.join('; ')). */
static void require_supported(const void *support) {
  if (support == NULL) return;
  const qa_wire_admission *admission = support;
  if (admission->supported) return;
  size_t total = 1;
  for (size_t i = 0; i < admission->reason_count; i++) total += strlen(admission->reasons[i]) + 2;
  char *message = xmalloc(total);
  size_t used = 0;
  for (size_t i = 0; i < admission->reason_count; i++) {
    if (i != 0) { memcpy(message + used, "; ", 2); used += 2; }
    size_t length = strlen(admission->reasons[i]);
    memcpy(message + used, admission->reasons[i], length);
    used += length;
  }
  message[used] = '\0';
  char text[QA_CAUGHT_MESSAGE_CAPACITY];
  size_t copy = used < sizeof text - 1 ? used : sizeof text - 1;
  memcpy(text, message, copy);
  text[copy] = '\0';
  free(message);
  qa_raise("Error", 0, 0, "%s", text);
}

static void size_buf_release(void *arg) { qa_size_buf_destroy(arg); }
/* new SizeBuf(capacity); write(bytes); bytes.bytes(). */
typedef void (*buffer_writer)(qa_size_buf *buffer, void *ctx);
static uint8_t *encode_size_buf(long long capacity, buffer_writer write, void *ctx, size_t *out_length) {
  qa_size_buf buffer;
  if (!qa_size_buf_init(&buffer, capacity, false)) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, size_buf_release, &buffer);
  write(&buffer, ctx);
  qa_cleanup_pop(&guard);
  uint8_t *bytes = qa_size_buf_bytes(&buffer, out_length);
  qa_size_buf_destroy(&buffer);
  if (bytes == NULL) qa_range_error("Out of memory");
  return bytes;
}

typedef struct nq_write { qa_q1_protocol_identity protocol; const qa_netquake_message *message; } nq_write;
static void write_nq_message(qa_size_buf *buffer, void *ctx) {
  nq_write *write = ctx;
  qa_write_netquake_message(buffer, write->protocol, write->message, "known-retail", true);
}

/* TS qwPacket: an 8-byte sequence header (LE int32 ++sequence, then
 * zero) before the payload. */
static uint8_t *qw_packet(qa_app_local_demo_recording *recording, const uint8_t *payload, size_t length,
    double seconds, qa_quakeworld_demo_record *out) {
  if (length > 1442) qa_raise("RangeError", 0, 0, "QWD message exceeds native packet capacity");
  uint8_t *message = xmalloc(length + 8);
  memset(message, 0, 8);
  recording->sequence += 1;
  uint32_t sequence = qa_js_to_uint32(recording->sequence);
  message[0] = (uint8_t)sequence; message[1] = (uint8_t)(sequence >> 8);
  message[2] = (uint8_t)(sequence >> 16); message[3] = (uint8_t)(sequence >> 24);
  if (length != 0) memcpy(message + 8, payload, length);
  memset(out, 0, sizeof *out);
  out->kind = "packet";
  out->seconds = seconds;
  out->message = message;
  out->length = length + 8;
  return message;
}

/* --- seed --- */

static void qw_message_release(void *arg) { qa_quakeworld_message_destroy(arg); }
static void qw_recording_state_release(void *arg) { qa_quakeworld_recording_state_free(arg); }

static void seed_qw(qa_app_local_demo_recording *recording, const qa_app_local_recording_source *source, prepared *next) {
  qa_app_qw_server_host *host_api = source->qw_host;
  const qa_qw_signon_host *host = host_api->signon(host_api->ctx, source->qw_player);
  qa_quakeworld_message data;
  memset(&data, 0, sizeof data);
  host->server_data(host->ctx, &data);
  qa_cleanup data_guard;
  qa_cleanup_push(&data_guard, qw_message_release, &data);
  size_t signon_count = 0;
  const qa_app_qw_bytes *signon = host_api->recording_signon == NULL ? NULL
      : host_api->recording_signon(host_api->ctx, source->qw_player, &signon_count);
  if (host_api->recording_signon == NULL) qa_raise("Error", 0, 0, "QW source has no read-only recording signon");
  qa_app_demo_recording_packet sequences;
  memset(&sequences, 0, sizeof sequences);
  sequences.kind = QA_APP_RECORDING_QW;
  sequences.record.kind = "sequences";
  sequences.record.seconds = source->seconds;
  sequences.record.outgoing = 0;
  sequences.record.incoming = 0;
  arena_push(&next->arena, sequences);
  size_t model_count = 0, sound_count = 0;
  const char *const *models = host->models(host->ctx, &model_count);
  const char *const *sounds = host->sounds(host->ctx, &sound_count);
  qa_quakeworld_recording_state *state = qa_quakeworld_recording_state_create();
  if (state == NULL) qa_range_error("Out of memory");
  qa_cleanup state_guard;
  qa_cleanup_push(&state_guard, qw_recording_state_release, state);
  size_t initial_count = 0;
  qa_quakeworld_demo_record *initial = qa_quakeworld_recording_state_seed(state, &data, models, model_count,
      sounds, sound_count, source->seconds, 0, 0, &initial_count);
  qa_cleanup_pop(&state_guard);
  qa_quakeworld_recording_state_free(state);
  next->arena.qw_records = initial;
  next->arena.qw_record_count = initial_count;
  double packets = 0;
  for (size_t i = 0; i < initial_count; i++) {
    if (strcmp(initial[i].kind, "packet") != 0) continue;
    qa_app_demo_recording_packet packet;
    memset(&packet, 0, sizeof packet);
    packet.kind = QA_APP_RECORDING_QW;
    packet.record = initial[i];
    arena_push(&next->arena, packet);
    packets++;
  }
  recording->sequence = packets;
  const uint8_t *const *buffers = NULL;
  const size_t *lengths = NULL;
  size_t buffer_count = 0;
  host->signon_buffers(host->ctx, &buffers, &lengths, &buffer_count);
  for (size_t i = 0; i < buffer_count + signon_count; i++) {
    const uint8_t *bytes = i < buffer_count ? buffers[i] : signon[i - buffer_count].data;
    size_t length = i < buffer_count ? lengths[i] : signon[i - buffer_count].length;
    qa_app_demo_recording_packet packet;
    memset(&packet, 0, sizeof packet);
    packet.kind = QA_APP_RECORDING_QW;
    arena_keep(&next->arena, qw_packet(recording, bytes, length, source->seconds, &packet.record));
    arena_push(&next->arena, packet);
  }
  size_t baseline_count = 0;
  const void *const *baselines = host_api->baselines(host_api->ctx, source->qw_player, &baseline_count);
  next->qw_states = xmalloc(baseline_count * sizeof *next->qw_states);
  next->qw_baselines = xmalloc(baseline_count * sizeof *next->qw_baselines);
  /* new Map(number => entity): a later entity with the same number replaces the earlier one. */
  size_t count = 0;
  for (size_t i = 0; i < baseline_count; i++) {
    const qa_quakeworld_entity *entity = baselines[i];
    qa_qw_entity_state state_value;
    qa_qw_wire_entity_from_extended(&entity->state, false, &state_value);
    size_t at = count;
    for (size_t k = 0; k < count; k++) if (next->qw_states[k].number == entity->state.base.number) at = k;
    next->qw_states[at] = state_value;
    if (at == count) count++;
  }
  for (size_t i = 0; i < count; i++) {
    next->qw_baselines[i].number = next->qw_states[i].number;
    next->qw_baselines[i].state = &next->qw_states[i];
  }
  next->qw_baseline_count = count;
  next->identity.kind = QA_APP_RECORDING_QW;
  next->identity.protocol = 28;
  qa_cleanup_pop(&data_guard);
  qa_quakeworld_message_destroy(&data);
}

static void push_q1_message(prepared *next, const qa_app_local_recording_source *source, const qa_netquake_message *message) {
  nq_write write = {source->q1_host->protocol, message};
  size_t length = 0;
  uint8_t *bytes = arena_keep(&next->arena, encode_size_buf(65536, write_nq_message, &write, &length));
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_Q1;
  packet.view_angles = source->view_angles;
  packet.message = bytes;
  packet.message_length = length;
  arena_push(&next->arena, packet);
}
static void seed_q1(const qa_app_local_recording_source *source, prepared *next) {
  qa_app_q1_server_host *host = source->q1_host;
  qa_app_q1_game_state state = host->game_state(host->ctx, source->q1_player);
  next->q1_state = state;
  push_q1_message(next, source, (const qa_netquake_message *)state.info);
  qa_netquake_message message;
  memset(&message, 0, sizeof message);
  message.kind = "set-view";
  message.entity = source->q1_player.source_entity;
  push_q1_message(next, source, &message);
  memset(&message, 0, sizeof message);
  message.kind = "signon"; message.stage = 1;
  push_q1_message(next, source, &message);
  for (size_t i = 0; i < state.signon_count; i++)
    push_q1_message(next, source, &((const qa_netquake_message *)state.signon)[i]);
  const qa_app_q1_baselines *baselines = state.baselines;
  for (size_t i = 0; baselines != NULL && i < baselines->count; i++) {
    memset(&message, 0, sizeof message);
    message.kind = "baseline";
    message.state = baselines->entries[i].state;
    push_q1_message(next, source, &message);
  }
  memset(&message, 0, sizeof message);
  message.kind = "signon"; message.stage = 2;
  push_q1_message(next, source, &message);
  size_t spawn_count = 0;
  const qa_netquake_message *spawn = (const qa_netquake_message *)host->spawn(host->ctx, source->q1_player, &spawn_count);
  for (size_t i = 0; i < spawn_count; i++) push_q1_message(next, source, &spawn[i]);
  memset(&message, 0, sizeof message);
  message.kind = "signon"; message.stage = 3;
  push_q1_message(next, source, &message);
  next->identity.kind = QA_APP_RECORDING_Q1;
  next->identity.protocol = (double)host->protocol.version;
  next->identity.track = -1;
}

static void push_q2_event(prepared *next, const qa_q2_server_event *event) {
  size_t length = 0;
  uint8_t *bytes = qa_q2_encode_server_event(&next->wire, event, &length);
  if (bytes == NULL) qa_range_error("Out of memory");
  arena_keep(&next->arena, bytes);
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_Q2;
  packet.message = bytes;
  packet.message_length = length;
  arena_push(&next->arena, packet);
}
static void seed_q2(const qa_app_local_recording_source *source, prepared *next) {
  qa_app_q2_server_host *host = source->q2_host;
  qa_app_q2_game_state state = host->game_state(host->ctx, source->q2_player, &source->protocol);
  next->q2_state = state;
  if (!qa_q2_wire_codec_init(&next->wire, source->protocol)) qa_range_error("Out of memory");
  next->has_wire = true;
  qa_q2_server_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_SERVER_DATA;
  event.data = *(const qa_q2_server_data_result *)state.data;
  event.data.attractloop = true;
  push_q2_event(next, &event);
  const qa_app_q2_config_snapshot *configs = state.config_strings;
  for (size_t i = 0; configs != NULL && i < configs->count; i++) {
    memset(&event, 0, sizeof event);
    event.kind = QA_Q2_SERVER_CONFIG_STRING;
    event.index = configs->entries[i].index;
    event.text = configs->entries[i].value;
    push_q2_event(next, &event);
  }
  const qa_q2_baseline_list *baselines = state.baselines;
  for (size_t i = 0; baselines != NULL && i < baselines->count; i++) {
    memset(&event, 0, sizeof event);
    event.kind = QA_Q2_SERVER_BASELINE;
    event.entity = baselines->entries[i].state;
    push_q2_event(next, &event);
  }
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_SERVER_COMMAND_TEXT;
  event.text = (char *)"precache\n";
  push_q2_event(next, &event);
  next->identity.kind = QA_APP_RECORDING_Q2;
  next->identity.q2_protocol = source->protocol;
}

/* TS q3Message's baseline closure over the prepared game state. */
static qa_q3_entity_state_record *q3_baseline(void *ctx, int64_t number) {
  const qa_q3_gamestate *state = ctx;
  for (size_t i = 0; i < state->entry_count; i++)
    if (state->entries[i].kind == QA_Q3_GS_BASELINE && state->entries[i].number == number)
      return &state->entries[i].entity;
  return NULL;
}
static uint8_t *q3_message(qa_app_local_demo_recording *recording, const qa_app_local_recording_source *source,
    const qa_q3_gamestate *state, const qa_q3_server_operation *operations, size_t count, size_t *out_length) {
  qa_q3_server_message_context context;
  memset(&context, 0, sizeof context);
  context.product = source->q3_host->product != NULL ? *(const qa_q3_product *)source->q3_host->product : QA_Q3_PRODUCT_BASEQ3;
  context.message_number = (int64_t)recording->sequence;
  context.reliable_sequence = 0;
  context.server_command_sequence = recording->command_sequence;
  context.parse_entities_number = 0;
  context.baseline = q3_baseline;
  context.baseline_ctx = (void *)state;
  context.history = NULL;
  context.history_ctx = NULL;
  uint8_t *bytes = NULL;
  if (!qa_q3_encode_server_message(0, operations, count, &context, NULL, &bytes, out_length))
    qa_range_error("Out of memory");
  return bytes;
}
static void seed_q3(qa_app_local_demo_recording *recording, const qa_app_local_recording_source *source, prepared *next) {
  qa_app_q3_server_host *host = source->q3_host;
  const qa_q3_gamestate *state = host->game_state(host->ctx, source->q3_player, source->server_id);
  next->q3_state = state;
  recording->command_sequence = state->command_sequence;
  qa_q3_server_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.kind = QA_Q3_OP_GAMESTATE;
  operation.gamestate = *state;
  size_t length = 0;
  uint8_t *bytes = arena_keep(&next->arena, q3_message(recording, source, state, &operation, 1, &length));
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_Q3;
  packet.sequence = recording->sequence;
  recording->sequence += 1;
  packet.message = bytes;
  packet.message_length = length;
  arena_push(&next->arena, packet);
  next->identity.kind = QA_APP_RECORDING_Q3;
  next->identity.protocol = 68;
}

static const void *source_support(const qa_app_local_recording_source *source) {
  switch (source->kind) {
    case QA_APP_LOCAL_RECORDING_Q1:
      return source->q1_host->supports_source_wire == NULL ? NULL : source->q1_host->supports_source_wire(source->q1_host->ctx);
    case QA_APP_LOCAL_RECORDING_QW:
      return source->qw_host->supports_source_wire == NULL ? NULL : source->qw_host->supports_source_wire(source->qw_host->ctx);
    case QA_APP_LOCAL_RECORDING_Q2:
      return source->q2_host->supports_source_wire == NULL ? NULL : source->q2_host->supports_source_wire(source->q2_host->ctx);
    default:
      return source->q3_host->supports_source_wire == NULL ? NULL : source->q3_host->supports_source_wire(source->q3_host->ctx);
  }
}

qa_app_demo_recording_seed qa_app_local_demo_recording_seed(qa_app_local_demo_recording *recording) {
  qa_app_local_recording_source source = recording->read(recording->read_ctx);
  require_supported(source_support(&source));
  prepared *next = calloc(1, sizeof *next);
  if (next == NULL) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, prepared_release, next);
  next->source = source;
  if (source.kind == QA_APP_LOCAL_RECORDING_QW) seed_qw(recording, &source, next);
  else if (source.kind == QA_APP_LOCAL_RECORDING_Q1) seed_q1(&source, next);
  else if (source.kind == QA_APP_LOCAL_RECORDING_Q2) seed_q2(&source, next);
  else seed_q3(recording, &source, next);
  qa_cleanup_pop(&guard);
  prepared_destroy(recording->prepared);
  recording->prepared = next;
  qa_app_demo_recording_seed seed;
  seed.identity = next->identity;
  seed.packets = next->arena.packets;
  seed.packet_count = next->arena.packet_count;
  return seed;
}

qa_app_local_demo_recording_attachment qa_app_local_demo_recording_attach(
    qa_app_local_demo_recording *recording, qa_app_demo_recording_sink sink) {
  if (recording->has_sink || recording->prepared == NULL)
    qa_raise("Error", 0, 0, "Local recording requires an unattached prepared seed");
  recording->has_sink = true;
  recording->sink = sink;
  qa_app_local_demo_recording_attachment attachment = {recording, sink};
  return attachment;
}
void qa_app_local_demo_recording_detach(const qa_app_local_demo_recording_attachment *attachment) {
  qa_app_local_demo_recording *recording = attachment->recording;
  if (recording->has_sink && recording->sink.ctx == attachment->sink.ctx
      && recording->sink.append == attachment->sink.append) recording->has_sink = false;
}

/* --- publish --- */

static void append(qa_app_demo_recording_sink sink, qa_app_demo_recording_packet packet) {
  const char *error = sink.append(sink.ctx, packet);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
/* Appends a packet whose message buffer this call owns; the buffer is
 * freed on return and on a raise. */
static void append_owned(qa_app_demo_recording_sink sink, qa_app_demo_recording_packet packet, uint8_t *owned) {
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, owned);
  append(sink, packet);
  qa_cleanup_pop(&guard);
  free(owned);
}

/* TS JSON.stringify(identity) equality: kind, then the kind's fields. */
static bool same_identity(const qa_app_demo_recording_identity *a, const qa_app_demo_recording_identity *b) {
  if (a->kind != b->kind) return false;
  if (a->kind == QA_APP_RECORDING_Q2) {
    const qa_q2_protocol_identity *x = &a->q2_protocol, *y = &b->q2_protocol;
    if (strcmp(x->kind, y->kind) != 0 || x->version != y->version) return false;
    bool revised = strcmp(x->kind, "q2-r1q2") == 0 || strcmp(x->kind, "q2-q2pro") == 0;
    return !revised || x->revision == y->revision;
  }
  if (a->protocol != b->protocol) return false;
  return a->kind != QA_APP_RECORDING_Q1 || a->track == b->track;
}

typedef struct q1_publish {
  qa_app_q1_server_host *host;
  const qa_app_q1_frame *frame;
  const prepared *prepared;
} q1_publish;
static void write_q1_frame(qa_size_buf *buffer, void *ctx) {
  q1_publish *publish = ctx;
  const qa_app_q1_frame *frame = publish->frame;
  qa_q1_protocol_identity protocol = publish->host->protocol;
  qa_netquake_message time;
  memset(&time, 0, sizeof time);
  time.kind = "time";
  time.seconds = frame->seconds;
  qa_write_netquake_message(buffer, protocol, &time, "known-retail", true);
  const qa_netquake_message *groups[3] = {(const qa_netquake_message *)frame->reliable,
      (const qa_netquake_message *)frame->messages, (const qa_netquake_message *)frame->datagram};
  size_t counts[3] = {frame->reliable_count, frame->message_count, frame->datagram_count};
  for (int g = 0; g < 3; g++)
    for (size_t i = 0; i < counts[g]; i++)
      qa_write_netquake_message(buffer, protocol, &groups[g][i], "known-retail", true);
  const qa_q1_extended_entity_state *entities = (const qa_q1_extended_entity_state *)frame->entities;
  const qa_app_q1_baselines *baselines = publish->prepared->q1_state.baselines;
  for (size_t i = 0; i < frame->entity_count; i++) {
    const qa_q1_extended_entity_state *entity = &entities[i];
    const qa_q1_extended_entity_state *baseline = NULL;
    for (size_t k = 0; baselines != NULL && k < baselines->count; k++)
      if (baselines->entries[k].number == entity->base.number) baseline = &baselines->entries[k].state;
    qa_q1_extended_entity_state fallback;
    if (baseline == NULL) {
      qa_q1_wire_entity_state wire;
      qa_q1_wire_entity_state_init(&wire);
      fallback = qa_nq_entity_state_from_wire(entity->base.number, &wire);
      baseline = &fallback;
    }
    qa_write_netquake_entity(buffer, protocol, entity, baseline, frame->seconds);
  }
}

typedef struct qw_write { qa_quakeworld_profile profile; const qa_quakeworld_message *message; } qw_write;
static void write_qw_message(qa_size_buf *buffer, void *ctx) {
  qw_write *write = ctx;
  qa_write_quakeworld_message(buffer, write->profile, write->message);
}
typedef struct qw_entities {
  qa_quakeworld_profile profile;
  const qa_qw_entity_state *states; size_t count;
  const prepared *prepared;
} qw_entities;
static void write_qw_entities(qa_size_buf *buffer, void *ctx) {
  qw_entities *write = ctx;
  qa_write_quakeworld_entities(buffer, write->profile, write->states, write->count,
      write->prepared->qw_baselines, write->prepared->qw_baseline_count, NULL);
}
static void append_qw_payload(qa_app_local_demo_recording *recording, qa_app_demo_recording_sink sink, uint8_t *payload, size_t length, double seconds) {
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, payload);
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_QW;
  uint8_t *message = qw_packet(recording, payload, length, seconds, &packet.record);
  qa_cleanup_pop(&guard);
  free(payload);
  append_owned(sink, packet, message);
}
static void publish_qw(qa_app_local_demo_recording *recording, qa_app_demo_recording_sink sink, const qa_app_local_recording_source *source,
    const prepared *current, const void *output) {
  qa_app_qw_server_host *host = source->qw_host;
  qa_app_qw_frame frame = host->frame(host->ctx, source->qw_player, output);
  qa_quakeworld_profile protocol = qa_quakeworld_profile_make(28, 0);
  qa_quakeworld_message set_angle;
  memset(&set_angle, 0, sizeof set_angle);
  set_angle.kind = "set-angle";
  set_angle.angles = source->view_angles;
  size_t total = frame.reliable_count + frame.message_count + 1;
  for (size_t i = 0; i < total; i++) {
    const qa_quakeworld_message *message = i < frame.reliable_count ? frame.reliable[i]
        : i < frame.reliable_count + frame.message_count ? frame.messages[i - frame.reliable_count] : &set_angle;
    qw_write write = {protocol, message};
    size_t length = 0;
    uint8_t *payload = encode_size_buf(1442, write_qw_message, &write, &length);
    append_qw_payload(recording, sink, payload, length, source->seconds);
  }
  qa_qw_entity_state *states = xmalloc(frame.entity_count * sizeof *states);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, states);
  for (size_t i = 0; i < frame.entity_count; i++) {
    const qa_quakeworld_entity *entity = frame.entities[i];
    qa_qw_wire_entity_from_extended(&entity->state, (qa_js_to_int32(entity->quakeworld_flags) & QA_QW_U_SOLID) != 0, &states[i]);
  }
  qw_entities write = {protocol, states, frame.entity_count, current};
  size_t length = 0;
  uint8_t *payload = encode_size_buf(1442, write_qw_entities, &write, &length);
  qa_cleanup_pop(&guard);
  free(states);
  append_qw_payload(recording, sink, payload, length, source->seconds);
}

static void append_q2(qa_app_demo_recording_sink sink, const uint8_t *bytes, size_t length) {
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_Q2;
  packet.message = bytes;
  packet.message_length = length;
  append(sink, packet);
}
static void publish_q2(qa_app_demo_recording_sink sink, const qa_app_local_recording_source *source,
    prepared *current, const void *output, const qa_app_sim_presentation_event *const *events, size_t event_count) {
  qa_app_q2_server_host *host = source->q2_host;
  size_t raw_count = 0;
  const qa_app_q2_raw_message *raw = host->raw_messages == NULL ? NULL : host->raw_messages(host->ctx, source->q2_player, &raw_count);
  for (size_t i = 0; i < raw_count; i++) if (raw[i].reliable) append_q2(sink, raw[i].bytes, raw[i].length);
  size_t host_event_count = 0;
  const qa_app_q2_server_event *host_events = host->events == NULL ? NULL
      : host->events(host->ctx, source->q2_player, output, (const void *const *)events, event_count, &host_event_count);
  for (size_t i = 0; i < host_event_count; i++) {
    size_t length = 0;
    uint8_t *bytes = qa_q2_encode_server_event(&current->wire, host_events[i].event, &length);
    if (bytes == NULL) qa_range_error("Out of memory");
    qa_app_demo_recording_packet packet;
    memset(&packet, 0, sizeof packet);
    packet.kind = QA_APP_RECORDING_Q2;
    packet.message = bytes;
    packet.message_length = length;
    append_owned(sink, packet, bytes);
  }
  const qa_q2_wire_frame *frame = host->frame(host->ctx, source->q2_player, output, &source->protocol);
  size_t length = 0;
  uint8_t *bytes = qa_q2_encode_frame(&current->wire, frame, NULL, current->q2_state.baselines, host->max_clients, &length);
  if (bytes == NULL) qa_range_error("Out of memory");
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_Q2;
  packet.message = bytes;
  packet.message_length = length;
  append_owned(sink, packet, bytes);
  for (size_t i = 0; i < raw_count; i++) if (!raw[i].reliable) append_q2(sink, raw[i].bytes, raw[i].length);
}

typedef struct q3_operations {
  qa_q3_server_operation *items; size_t count, cap;
  char **owned; size_t owned_count, owned_cap;
  qa_q3_entity_state_record *entities;
} q3_operations;
static void q3_operations_release(void *arg) {
  q3_operations *ops = arg;
  for (size_t i = 0; i < ops->owned_count; i++) free(ops->owned[i]);
  free(ops->owned); free(ops->items); free(ops->entities);
}
static void q3_push(q3_operations *ops, qa_q3_server_operation operation) {
  if (ops->count == ops->cap) {
    ops->cap = ops->cap ? ops->cap * 2 : 8;
    ops->items = xrealloc(ops->items, ops->cap * sizeof *ops->items);
  }
  ops->items[ops->count++] = operation;
}
static void q3_command(qa_app_local_demo_recording *recording, q3_operations *ops, char *text) {
  qa_q3_server_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.kind = QA_Q3_OP_COMMAND;
  recording->command_sequence += 1;
  operation.sequence = recording->command_sequence;
  operation.text = text;
  q3_push(ops, operation);
}
static void publish_q3(qa_app_local_demo_recording *recording, qa_app_demo_recording_sink sink, const qa_app_local_recording_source *source,
    const prepared *current, const qa_app_sim_presentation_event *const *events, size_t event_count) {
  q3_operations ops;
  memset(&ops, 0, sizeof ops);
  qa_cleanup guard;
  qa_cleanup_push(&guard, q3_operations_release, &ops);
  for (size_t i = 0; i < event_count; i++) {
    const qa_app_sim_presentation_event *item = events[i];
    if (item->kind == NULL || strcmp(item->kind, "q3-source") != 0) continue;
    if (item->has_recipient && !qa_same_actor(item->recipient, source->q3_player.actor)) continue;
    const qa_app_q3_source_event *event = item->q3_source;
    if (event == NULL) continue;
    if (event->kind == QA_APP_Q3_EVENT_SERVER_COMMAND
        && (event->client == -1 || event->client == source->q3_player.source_entity)) {
      q3_command(recording, &ops, (char *)(event->text != NULL ? event->text : ""));
    } else if (event->kind == QA_APP_Q3_EVENT_CONFIGSTRING) {
      char **commands = NULL;
      size_t count = 0;
      if (!qa_q3_configstring_commands((int64_t)event->index, event->value != NULL ? event->value : "", &commands, &count))
        qa_range_error("Out of memory");
      for (size_t k = 0; k < count; k++) {
        if (ops.owned_count == ops.owned_cap) {
          ops.owned_cap = ops.owned_cap ? ops.owned_cap * 2 : 8;
          ops.owned = xrealloc(ops.owned, ops.owned_cap * sizeof *ops.owned);
        }
        ops.owned[ops.owned_count++] = commands[k];
      }
      for (size_t k = 0; k < count; k++) q3_command(recording, &ops, commands[k]);
      free(commands);
    }
  }
  qa_app_q3_server_host *host = source->q3_host;
  qa_app_q3_snapshot frame = host->snapshot(host->ctx, source->q3_player);
  qa_q3_server_operation snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.kind = QA_Q3_OP_SNAPSHOT;
  snapshot.validity.kind = QA_Q3_SNAP_VALID;
  snapshot.snapshot.message_number = (int64_t)recording->sequence;
  snapshot.snapshot.server_time = (int64_t)host->time(host->ctx);
  snapshot.snapshot.delta_number = -1;
  snapshot.snapshot.flags = source->snapshot_server_bit;
  snapshot.snapshot.server_command_number = recording->command_sequence;
  snapshot.snapshot.parse_entities_number = 0;
  size_t mask = frame.area_mask_length < sizeof snapshot.snapshot.area_mask ? frame.area_mask_length : sizeof snapshot.snapshot.area_mask;
  if (mask != 0) memcpy(snapshot.snapshot.area_mask, frame.area_mask, mask);
  snapshot.snapshot.area_mask_len = mask;
  snapshot.snapshot.player_state = *(const qa_q3_player_state_record *)frame.player;
  ops.entities = xmalloc(frame.entity_count * sizeof *ops.entities);
  for (size_t i = 0; i < frame.entity_count; i++) ops.entities[i] = *(const qa_q3_entity_state_record *)frame.entities[i];
  snapshot.snapshot.entities = ops.entities;
  snapshot.snapshot.entity_count = frame.entity_count;
  q3_push(&ops, snapshot);
  size_t length = 0;
  uint8_t *bytes = q3_message(recording, source, current->q3_state, ops.items, ops.count, &length);
  qa_cleanup_pop(&guard);
  q3_operations_release(&ops);
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_Q3;
  packet.sequence = recording->sequence;
  recording->sequence += 1;
  packet.message = bytes;
  packet.message_length = length;
  append_owned(sink, packet, bytes);
}

void qa_app_local_demo_recording_publish(qa_app_local_demo_recording *recording,
    const void *output, const qa_app_sim_presentation_event *const *events, size_t event_count) {
  if (!recording->has_sink) return;
  qa_app_demo_recording_sink sink = recording->sink;
  qa_app_local_recording_source source = recording->read(recording->read_ctx);
  prepared *current = recording->prepared;
  if (current == NULL) qa_raise("Error", 0, 0, "Local recording lost its seed");
  if (current->source.world != source.world) {
    qa_app_demo_recording_identity identity = current->identity;
    qa_app_demo_recording_seed seed = qa_app_local_demo_recording_seed(recording);
    if (!same_identity(&seed.identity, &identity)) qa_raise("Error", 0, 0, "Local recording source protocol changed");
    for (size_t i = 0; i < seed.packet_count; i++) append(sink, seed.packets[i]);
    current = recording->prepared;
  }
  qa_app_local_recording_kind kind = current->source.kind;
  if (source.kind == QA_APP_LOCAL_RECORDING_Q1 && kind == QA_APP_LOCAL_RECORDING_Q1) {
    qa_app_q1_server_host *host = source.q1_host;
    qa_app_q1_frame frame = host->frame(host->ctx, source.q1_player, output);
    q1_publish write = {host, &frame, current};
    size_t length = 0;
    uint8_t *bytes = encode_size_buf(64000, write_q1_frame, &write, &length);
    qa_app_demo_recording_packet packet;
    memset(&packet, 0, sizeof packet);
    packet.kind = QA_APP_RECORDING_Q1;
    packet.view_angles = source.view_angles;
    packet.message = bytes;
    packet.message_length = length;
    append_owned(sink, packet, bytes);
  } else if (source.kind == QA_APP_LOCAL_RECORDING_QW && kind == QA_APP_LOCAL_RECORDING_QW) {
    publish_qw(recording, sink, &source, current, output);
  } else if (source.kind == QA_APP_LOCAL_RECORDING_Q2 && kind == QA_APP_LOCAL_RECORDING_Q2) {
    publish_q2(sink, &source, current, output, events, event_count);
  } else if (source.kind == QA_APP_LOCAL_RECORDING_Q3 && kind == QA_APP_LOCAL_RECORDING_Q3) {
    publish_q3(recording, sink, &source, current, events, event_count);
  } else qa_raise("Error", 0, 0, "Local recording source changed without a world boundary");
}
