/* Port of src/network/q3/server-message.ts. GPL-2.0-or-later. */
#include "qa/network/q3/server_message.h"
#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ENTITY_SENTINEL 1023
#define MAX_CONFIGSTRINGS 1024
#define MAX_GAMESTATE_CHARS 16000
#define MAX_AREA_BYTES 32
#define MAX_DOWNLOAD_BLOCK 2048
static const char *const OPCODE_NAMES[8] = { "svc_bad", "svc_nop", "svc_gamestate", "svc_configstring", "svc_baseline", "svc_serverCommand", "svc_download", "svc_snapshot" };
void qa_q3_wire_snapshot_destroy(qa_q3_wire_snapshot *s) { free(s->entities); s->entities = NULL; s->entity_count = 0; }
void qa_q3_gamestate_entry_destroy(qa_q3_gamestate_entry *e) { free(e->value); e->value = NULL; }
void qa_q3_gamestate_destroy(qa_q3_gamestate *g) {
  for (size_t i = 0; i < g->entry_count; i++) qa_q3_gamestate_entry_destroy(&g->entries[i]);
  free(g->entries); g->entries = NULL; g->entry_count = 0;
}
void qa_q3_download_block_destroy(qa_q3_download_block *d) { free(d->data); free(d->message); d->data = NULL; d->message = NULL; d->data_len = 0; }
void qa_q3_server_operation_destroy(qa_q3_server_operation *o) {
  free(o->text); o->text = NULL;
  qa_q3_gamestate_destroy(&o->gamestate);
  qa_q3_download_block_destroy(&o->download);
  qa_q3_wire_snapshot_destroy(&o->snapshot);
}
void qa_q3_server_message_destroy(qa_q3_server_message *m) {
  for (size_t i = 0; i < m->operation_count; i++) qa_q3_server_operation_destroy(&m->operations[i]);
  free(m->operations); m->operations = NULL; m->operation_count = 0;
}
void qa_q3_server_message_step_destroy(qa_q3_server_message_step *s) {
  qa_q3_gamestate_entry_destroy(&s->entry);
  qa_q3_server_operation_destroy(&s->operation);
}
bool qa_q3_wire_snapshot_copy(qa_q3_wire_snapshot *dst, const qa_q3_wire_snapshot *src) {
  *dst = *src;
  dst->entities = malloc((src->entity_count ? src->entity_count : 1) * sizeof *dst->entities);
  if (!dst->entities) return false;
  memcpy(dst->entities, src->entities, src->entity_count * sizeof *dst->entities);
  return true;
}
static void fail(qa_q3_message_reader *r, const char *msg) {
  qa_binary_error(qa_q3_message_reader_source(r), qa_q3_message_reader_read_count(r), "%s", msg);
}
static void show_net(qa_q3_message_reader *r, const char *label, const qa_q3_delta_message_diagnostics *d) {
  if (d && d->shownet && d->shownet(d->shownet_ctx) >= 2 && d->print) {
    char buf[256]; snprintf(buf, sizeof buf, "%3lld:%s\n", (long long)(qa_q3_message_reader_read_count(r) + d->offset - 1), label);
    d->print(d->print_ctx, buf);
  }
}
static void show_packet_entity(qa_q3_message_reader *r, const char *label, int64_t number, const qa_q3_delta_message_diagnostics *d) {
  if (d && d->shownet && d->shownet(d->shownet_ctx) == 3 && d->print) {
    char buf[160]; snprintf(buf, sizeof buf, "%3lld:  %s: %lld\n", (long long)(qa_q3_message_reader_read_count(r) + d->offset), label, (long long)number);
    d->print(d->print_ctx, buf);
  }
}
static void validate_entities(const qa_q3_entity_state_record *entities, size_t count) {
  if (count > ENTITY_SENTINEL) qa_range_error("Too many snapshot entities");
  int64_t previous = -1;
  for (size_t i = 0; i < count; i++) {
    if (entities[i].number <= previous || entities[i].number >= ENTITY_SENTINEL)
      qa_range_error("Snapshot entities must have sorted unique numbers below 1023");
    previous = entities[i].number;
  }
}
typedef struct { qa_q3_entity_state_record *items; size_t count, cap; } ent_list_t;
static bool ent_push(ent_list_t *l, const qa_q3_entity_state_record *e) {
  if (l->count == l->cap) {
    size_t nc = l->cap ? l->cap * 2 : 16;
    qa_q3_entity_state_record *ni = realloc(l->items, nc * sizeof *ni);
    if (!ni) return false;
    l->items = ni; l->cap = nc;
  }
  l->items[l->count++] = *e; return true;
}
static bool read_packet_entities(qa_q3_message_reader *r, const qa_q3_wire_snapshot *previous,
                                 const qa_q3_server_message_context *ctx,
                                 const qa_q3_delta_message_diagnostics *diag,
                                 qa_q3_source_parse_entities *pe, ent_list_t *out) {
  /* oldState(index) */
  size_t old_index = 0;
  qa_q3_entity_state_record old_copy; bool has_old = false; int64_t old_number = 99999;
  qa_q3_entity_state_record *old_ptr = NULL;
  #define REFRESH_OLD() do { \
    has_old = false; old_ptr = NULL; \
    if (previous && old_index < previous->entity_count) { \
      if (pe) old_ptr = qa_q3_source_parse_entities_at(pe, (int32_t)(previous->parse_entities_number + (int64_t)old_index)); \
      else { old_copy = previous->entities[old_index]; old_ptr = &old_copy; } \
      has_old = true; \
    } \
    old_number = has_old ? old_ptr->number : 99999; \
  } while (0)
  REFRESH_OLD();
  bool ok = true;
  while (ok) {
    int64_t number = qa_q3_message_reader_read_bits(r, 10);
    if (number == ENTITY_SENTINEL) break;
    size_t dlen; qa_q3_message_reader_data(r, &dlen);
    if (qa_q3_message_reader_read_count(r) > (int64_t)dlen) fail(r, "CL_ParsePacketEntities: end of message");
    while (ok && has_old && old_number < number) {
      show_packet_entity(r, "unchanged", old_number, diag);
      /* deltaEntity(oldNumber, old, unchanged=true) */
      qa_q3_entity_state_record target;
      if (pe) {
        qa_q3_entity_state_record *cell = qa_q3_source_parse_entities_at(pe, qa_q3_source_parse_entities_number(pe));
        *cell = *old_ptr; target = *cell;
        if (target.number != ENTITY_SENTINEL) { qa_q3_source_parse_entities_advance(pe); ok = ent_push(out, &target); }
      } else { target = *old_ptr; if (target.number != ENTITY_SENTINEL) ok = ent_push(out, &target); }
      old_index++; REFRESH_OLD();
    }
    if (!ok) break;
    if (has_old && old_number == number) {
      show_packet_entity(r, "delta", number, diag);
      if (pe) {
        qa_q3_entity_state_record *cell = qa_q3_source_parse_entities_at(pe, qa_q3_source_parse_entities_number(pe));
        qa_q3_entity_state_record from_copy = *old_ptr;
        qa_q3_read_delta_entity(r, &from_copy, number, diag, cell);
        if (cell->number != ENTITY_SENTINEL) { qa_q3_source_parse_entities_advance(pe); ok = ent_push(out, cell); }
      } else {
        qa_q3_entity_state_record target, from_copy = *old_ptr;
        qa_q3_read_delta_entity(r, &from_copy, number, diag, &target);
        if (target.number != ENTITY_SENTINEL) ok = ent_push(out, &target);
      }
      old_index++; REFRESH_OLD();
    } else {
      show_packet_entity(r, "baseline", number, diag);
      qa_q3_entity_state_record *base = ctx->baseline ? ctx->baseline(ctx->baseline_ctx, number) : NULL;
      qa_q3_entity_state_record zero; qa_q3_entity_state_record_init(&zero, 0);
      qa_q3_entity_state_record from_copy = base ? *base : zero;
      if (pe) {
        qa_q3_entity_state_record *cell = qa_q3_source_parse_entities_at(pe, qa_q3_source_parse_entities_number(pe));
        qa_q3_read_delta_entity(r, &from_copy, number, diag, cell);
        if (cell->number != ENTITY_SENTINEL) { qa_q3_source_parse_entities_advance(pe); ok = ent_push(out, cell); }
      } else {
        qa_q3_entity_state_record target;
        qa_q3_read_delta_entity(r, &from_copy, number, diag, &target);
        if (target.number != ENTITY_SENTINEL) ok = ent_push(out, &target);
      }
    }
  }
  while (ok && has_old) {
    show_packet_entity(r, "unchanged", old_number, diag);
    qa_q3_entity_state_record target;
    if (pe) {
      qa_q3_entity_state_record *cell = qa_q3_source_parse_entities_at(pe, qa_q3_source_parse_entities_number(pe));
      *cell = *old_ptr; target = *cell;
      if (target.number != ENTITY_SENTINEL) { qa_q3_source_parse_entities_advance(pe); ok = ent_push(out, &target); }
    } else { target = *old_ptr; if (target.number != ENTITY_SENTINEL) ok = ent_push(out, &target); }
    old_index++; REFRESH_OLD();
  }
  #undef REFRESH_OLD
  return ok;
}
static void write_packet_entities(qa_q3_message_writer *w, const qa_q3_entity_state_record *previous, size_t prev_count,
                                  const qa_q3_entity_state_record *current, size_t cur_count,
                                  qa_q3_baseline_fn baseline, void *baseline_ctx) {
  validate_entities(previous, prev_count);
  validate_entities(current, cur_count);
  size_t oi = 0, ni = 0;
  while (oi < prev_count || ni < cur_count) {
    bool has_old = oi < prev_count, has_new = ni < cur_count;
    if (has_old && has_new && previous[oi].number == current[ni].number) {
      qa_q3_write_delta_entity(w, &previous[oi], &current[ni], false); oi++; ni++;
    } else if (has_new && (!has_old || current[ni].number < previous[oi].number)) {
      qa_q3_entity_state_record *base = baseline ? baseline(baseline_ctx, current[ni].number) : NULL;
      qa_q3_write_delta_entity(w, base, &current[ni], true); ni++;
    } else if (has_old) {
      qa_q3_write_delta_entity(w, &previous[oi], NULL, true); oi++;
    } else qa_range_error("Missing packet entity during merge");
  }
  qa_q3_message_writer_write_bits(w, ENTITY_SENTINEL, 10);
}
typedef struct { const qa_q3_gamestate *gs; } gs_baseline_t;
static qa_q3_entity_state_record *gs_baseline_fn(void *ctx, int64_t number) {
  gs_baseline_t *g = ctx;
  if (!g->gs) return NULL;
  for (size_t i = 0; i < g->gs->entry_count; i++)
    if (g->gs->entries[i].kind == QA_Q3_GS_BASELINE && g->gs->entries[i].number == number)
      return (qa_q3_entity_state_record *)&g->gs->entries[i].entity;
  return NULL;
}
static bool read_download(qa_q3_message_reader *r, qa_q3_publish_download_size_fn publish, void *pub_ctx, qa_q3_download_block *out) {
  memset(out, 0, sizeof *out);
  int64_t number = qa_q3_message_reader_read_short(r);
  int64_t file_size = 0;
  if (number == 0) {
    file_size = qa_q3_message_reader_read_long(r);
    if (publish) file_size = publish(pub_ctx, file_size);
    if (file_size < 0) {
      out->kind = QA_Q3_DOWNLOAD_ERROR; out->file_size = file_size;
      return qa_q3_message_reader_read_string(r, &out->message);
    }
  }
  int64_t size = qa_q3_message_reader_read_short(r);
  if (size < 0 || size > QA_Q3_MAX_MESSAGE_LENGTH) {
    char buf[96]; snprintf(buf, sizeof buf, "Invalid download block length %lld", (long long)size); fail(r, buf);
  }
  if (!qa_q3_message_reader_read_data(r, size, &out->data)) return false;
  out->data_len = (size_t)size;
  if (number == 0) { out->kind = QA_Q3_DOWNLOAD_START; out->file_size = file_size; }
  else { out->kind = QA_Q3_DOWNLOAD_CHUNK; out->number = number; }
  return true;
}
static bool read_snapshot(qa_q3_message_reader *r, int64_t server_time, int64_t delta_number, int64_t flags,
                          const qa_q3_server_message_context *ctx, const qa_q3_delta_message_diagnostics *diag,
                          qa_q3_source_parse_entities *pe, qa_q3_server_operation *out) {
  memset(out, 0, sizeof *out);
  out->kind = QA_Q3_OP_SNAPSHOT;
  const qa_q3_snapshot_history_entry *slot = NULL;
  if (delta_number > 0 && ctx->history) slot = ctx->history(ctx->history_ctx, delta_number);
  qa_q3_snapshot_validity validity = { QA_Q3_SNAP_VALID, QA_Q3_SNAP_REASON_NONE };
  if (delta_number > 0) {
    int64_t pe_num = pe ? qa_q3_source_parse_entities_number(pe) : ctx->parse_entities_number;
    if (!slot) { validity.kind = QA_Q3_SNAP_INVALID; validity.reason = QA_Q3_SNAP_MISSING_DELTA; }
    else if (!slot->valid) { validity.kind = QA_Q3_SNAP_INVALID; validity.reason = QA_Q3_SNAP_INVALID_DELTA; }
    else if (slot->snapshot.message_number != delta_number) { validity.kind = QA_Q3_SNAP_INVALID; validity.reason = QA_Q3_SNAP_STALE_DELTA; }
    else if ((int32_t)(pe_num - slot->snapshot.parse_entities_number) > QA_Q3_MAX_PARSE_ENTITIES - 128) { validity.kind = QA_Q3_SNAP_INVALID; validity.reason = QA_Q3_SNAP_STALE_ENTITIES; }
  }
  if (validity.kind == QA_Q3_SNAP_INVALID && diag && diag->print) {
    const char *msg = validity.reason == QA_Q3_SNAP_STALE_DELTA ? "Delta frame too old.\n"
      : validity.reason == QA_Q3_SNAP_STALE_ENTITIES ? "Delta parseEntitiesNum too old.\n"
      : "Delta from invalid frame (not supposed to happen!).\n";
    diag->print(diag->print_ctx, msg);
  }
  int64_t length = qa_q3_message_reader_read_byte(r);
  if (length > MAX_AREA_BYTES) fail(r, "Snapshot area mask exceeds 32 bytes");
  uint8_t *area = NULL;
  if (!qa_q3_message_reader_read_data(r, length, &area)) return false;
  show_net(r, "playerstate", diag);
  qa_q3_player_state_record player;
  qa_q3_read_delta_player_state(r, slot ? &slot->snapshot.player_state : NULL, ctx->product, diag, &player);
  show_net(r, "packet entities", diag);
  int64_t pe_number = pe ? qa_q3_source_parse_entities_number(pe) : ctx->parse_entities_number;
  ent_list_t entities = {0};
  if (!read_packet_entities(r, slot ? &slot->snapshot : NULL, ctx, diag, pe, &entities)) { free(area); free(entities.items); return false; }
  out->validity = validity;
  qa_q3_wire_snapshot *snap = &out->snapshot;
  memset(snap, 0, sizeof *snap);
  snap->message_number = ctx->message_number; snap->server_time = server_time;
  snap->delta_number = delta_number; snap->flags = flags;
  snap->server_command_number = ctx->server_command_sequence;
  snap->parse_entities_number = pe_number;
  memcpy(snap->area_mask, area, (size_t)length); snap->area_mask_len = (size_t)length;
  free(area);
  snap->player_state = player;
  snap->entities = entities.items; snap->entity_count = entities.count;
  return true;
}
enum phase { PH_HEADER, PH_OPCODE, PH_SEQUENCE, PH_ENTRIES, PH_CLIENT, PH_CHECKSUM, PH_GS_END, PH_END };
struct qa_q3_server_message_cursor {
  qa_q3_message_reader *reader;
  qa_q3_delta_message_diagnostics diag; bool has_diag;
  enum phase phase;
  qa_q3_server_message_terminal terminal;
  int64_t data_count; bool exhausted;
  bool has_snap_header; int64_t snap_server_time, snap_delta, snap_flags;
  int64_t read_offset;
  qa_q3_source_parse_entities *pe;
  qa_q3_publish_download_size_fn publish; void *publish_ctx;
  uint8_t *owned;
};
bool qa_q3_server_message_cursor_init(qa_q3_server_message_cursor **out, const uint8_t *bytes, size_t len,
                                      const char *source, const qa_q3_server_message_diagnostics *diagnostics,
                                      int64_t read_offset, qa_q3_source_parse_entities *parse_entities,
                                      qa_q3_publish_download_size_fn publish_download_size, void *publish_ctx) {
  if (read_offset < 0) qa_range_error("Invalid server message read offset");
  qa_q3_server_message_cursor *c = calloc(1, sizeof *c);
  if (!c) return false;
  c->owned = malloc(len ? len : 1);
  if (!c->owned) { free(c); return false; }
  memcpy(c->owned, bytes, len);
  if (!qa_q3_message_reader_init(&c->reader, c->owned, len, QA_Q3_MESSAGE_BITSTREAM, source ? source : "<server-message>")) {
    free(c->owned); free(c); return false;
  }
  if (diagnostics) {
    c->has_diag = true;
    c->diag.shownet = diagnostics->shownet; c->diag.shownet_ctx = diagnostics->shownet_ctx;
    c->diag.print = diagnostics->print; c->diag.print_ctx = diagnostics->print_ctx;
    c->diag.offset = read_offset;
  }
  c->phase = PH_HEADER; c->terminal = QA_Q3_TERMINAL_EOF; c->data_count = 1;
  c->read_offset = read_offset; c->pe = parse_entities;
  c->publish = publish_download_size; c->publish_ctx = publish_ctx;
  *out = c; return true;
}
void qa_q3_server_message_cursor_destroy(qa_q3_server_message_cursor *c) {
  if (!c) return;
  qa_q3_message_reader_destroy(c->reader); free(c->owned); free(c);
}
static int64_t source_long(qa_q3_server_message_cursor *c) {
  if (c->exhausted) return -1;
  size_t len; qa_q3_message_reader_data(c->reader, &len);
  /* TS catches BinaryError at end-of-data; C abort cannot be caught, so pre-check
   * the fully-consumed case. Mid-symbol truncation aborts (noted unresolved). */
  if (qa_q3_message_reader_bit_position(c->reader) >= (int64_t)len * 8) { c->exhausted = true; return -1; }
  int64_t value = qa_q3_message_reader_read_long(c->reader);
  if (qa_q3_message_reader_read_count(c->reader) > (int64_t)len) c->exhausted = true;
  return value;
}
bool qa_q3_server_message_cursor_next(qa_q3_server_message_cursor *c, const qa_q3_server_message_context *ctx, qa_q3_server_message_step *out) {
  memset(out, 0, sizeof *out);
  qa_q3_message_reader *r = c->reader;
  const qa_q3_delta_message_diagnostics *diag = c->has_diag ? &c->diag : NULL;
  if (c->has_snap_header) {
    c->has_snap_header = false;
    out->kind = QA_Q3_STEP_OPERATION;
    return read_snapshot(r, c->snap_server_time, c->snap_delta, c->snap_flags, ctx, diag, c->pe, &out->operation);
  }
  switch (c->phase) {
    case PH_HEADER: {
      if (diag && diag->shownet && diag->print) {
        int64_t sn = diag->shownet(diag->shownet_ctx);
        size_t len; qa_q3_message_reader_data(r, &len);
        if (sn == 1) { char b[48]; snprintf(b, sizeof b, "%lld ", (long long)((int64_t)len + c->read_offset)); diag->print(diag->print_ctx, b); }
        else if (sn >= 2) diag->print(diag->print_ctx, "------------------\n");
      }
      int64_t wire = source_long(c);
      c->phase = PH_OPCODE;
      out->kind = QA_Q3_STEP_ACKNOWLEDGE;
      out->sequence = wire < ctx->reliable_sequence - 64 ? ctx->reliable_sequence : wire;
      return true;
    }
    case PH_SEQUENCE: {
      int64_t seq = source_long(c);
      c->data_count = 1; c->phase = PH_ENTRIES;
      out->kind = QA_Q3_STEP_GAMESTATE_SEQUENCE; out->sequence = seq; return true;
    }
    case PH_ENTRIES: {
      int64_t opcode = c->exhausted ? -1 : qa_q3_message_reader_read_byte(r);
      if (opcode == QA_Q3_SVC_EOF) { c->phase = PH_CLIENT; return qa_q3_server_message_cursor_next(c, ctx, out); }
      if (opcode == QA_Q3_SVC_CONFIGSTRING) {
        int64_t index = qa_q3_message_reader_read_short(r);
        if (index < 0 || index >= MAX_CONFIGSTRINGS) fail(r, "Invalid configstring index");
        char *value = NULL;
        if (!qa_q3_message_reader_read_big_string(r, &value)) return false;
        if (c->data_count + (int64_t)strlen(value) + 1 > MAX_GAMESTATE_CHARS) { free(value); fail(r, "Gamestate string storage exceeded"); }
        c->data_count += (int64_t)strlen(value) + 1;
        out->kind = QA_Q3_STEP_GAMESTATE_ENTRY;
        out->entry.kind = QA_Q3_GS_CONFIGSTRING; out->entry.index = index; out->entry.value = value;
        return true;
      }
      if (opcode == QA_Q3_SVC_BASELINE) {
        int64_t number = qa_q3_message_reader_read_bits(r, 10);
        qa_q3_entity_state_record *target = ctx->baseline ? ctx->baseline(ctx->baseline_ctx, number) : NULL;
        qa_q3_entity_state_record local; qa_q3_entity_state_record_init(&local, 0);
        qa_q3_entity_state_record *tgt = target ? target : &local;
        qa_q3_entity_state_record zero; qa_q3_entity_state_record_init(&zero, 0);
        qa_q3_read_delta_entity(r, &zero, number, diag, tgt);
        out->kind = QA_Q3_STEP_GAMESTATE_ENTRY;
        out->entry.kind = QA_Q3_GS_BASELINE; out->entry.number = number; out->entry.entity = *tgt;
        return true;
      }
      char buf[96]; snprintf(buf, sizeof buf, "Invalid gamestate opcode %lld", (long long)opcode); fail(r, buf);
      return false;
    }
    case PH_CLIENT: {
      int64_t n = source_long(c); c->phase = PH_CHECKSUM;
      out->kind = QA_Q3_STEP_GAMESTATE_CLIENT; out->number = n; return true;
    }
    case PH_CHECKSUM: {
      int64_t sum = source_long(c); c->phase = PH_GS_END;
      out->kind = QA_Q3_STEP_GAMESTATE_CHECKSUM; out->checksum = sum; return true;
    }
    case PH_GS_END: c->phase = PH_OPCODE; out->kind = QA_Q3_STEP_GAMESTATE_END; return true;
    case PH_END: out->kind = QA_Q3_STEP_END; out->terminal = c->terminal; return true;
    case PH_OPCODE: break;
  }
  for (;;) {
    size_t len; qa_q3_message_reader_data(r, &len);
    if (c->exhausted || qa_q3_message_reader_read_count(r) > (int64_t)len)
      fail(r, "CL_ParseServerMessage: read past end of server message");
    int64_t opcode = qa_q3_message_reader_read_byte(r);
    if (opcode == QA_Q3_SVC_EOF) {
      show_net(r, "END OF MESSAGE", diag);
      c->phase = PH_END; out->kind = QA_Q3_STEP_END; out->terminal = QA_Q3_TERMINAL_EOF; return true;
    }
    if (diag && diag->shownet && diag->shownet(diag->shownet_ctx) >= 2 && diag->print) {
      if (opcode < 0 || opcode >= 8) {
        char b[96]; snprintf(b, sizeof b, "%3lld:BAD CMD %lld\n", (long long)(qa_q3_message_reader_read_count(r) + c->read_offset - 1), (long long)opcode);
        diag->print(diag->print_ctx, b);
      } else show_net(r, OPCODE_NAMES[opcode], diag);
    }
    switch (opcode) {
      case QA_Q3_SVC_NOP:
        out->kind = QA_Q3_STEP_OPERATION; out->operation.kind = QA_Q3_OP_NOP; return true;
      case QA_Q3_SVC_COMMAND: {
        int64_t seq = qa_q3_message_reader_read_long(r);
        char *text = NULL;
        if (!qa_q3_message_reader_read_string(r, &text)) return false;
        if (seq <= ctx->server_command_sequence) { free(text); break; }
        out->kind = QA_Q3_STEP_OPERATION; out->operation.kind = QA_Q3_OP_COMMAND;
        out->operation.sequence = seq; out->operation.text = text; return true;
      }
      case QA_Q3_SVC_GAMESTATE:
        c->phase = PH_SEQUENCE; out->kind = QA_Q3_STEP_GAMESTATE_START; return true;
      case QA_Q3_SVC_DOWNLOAD: {
        out->kind = QA_Q3_STEP_OPERATION; out->operation.kind = QA_Q3_OP_DOWNLOAD;
        if (!read_download(r, c->publish, c->publish_ctx, &out->operation.download)) return false;
        if (out->operation.download.kind == QA_Q3_DOWNLOAD_ERROR) { c->phase = PH_END; c->terminal = QA_Q3_TERMINAL_DOWNLOAD_ERROR; }
        return true;
      }
      case QA_Q3_SVC_SNAPSHOT: {
        int64_t server_time = qa_q3_message_reader_read_long(r);
        int64_t distance = qa_q3_message_reader_read_byte(r);
        int64_t delta = distance == 0 ? -1 : (int32_t)(ctx->message_number - distance);
        int64_t flags = qa_q3_message_reader_read_byte(r);
        c->has_snap_header = true; c->snap_server_time = server_time; c->snap_delta = delta; c->snap_flags = flags;
        out->kind = QA_Q3_STEP_SNAPSHOT_HEADER; out->delta_number = delta; return true;
      }
      default: {
        char buf[96]; snprintf(buf, sizeof buf, "Invalid server opcode %lld", (long long)opcode); fail(r, buf);
      }
    }
  }
}
static qa_q3_entity_state_record *null_baseline(void *ctx, int64_t n) { (void)ctx; (void)n; return NULL; }
static const qa_q3_snapshot_history_entry *null_history(void *ctx, int64_t n) { (void)ctx; (void)n; return NULL; }
bool qa_q3_decode_server_message(const uint8_t *bytes, size_t len, const qa_q3_server_message_context *context,
                                 const char *source, qa_q3_server_message *out) {
  memset(out, 0, sizeof *out);
  qa_q3_server_message_cursor *cursor = NULL;
  if (!qa_q3_server_message_cursor_init(&cursor, bytes, len, source, NULL, 0, NULL, NULL, NULL)) return false;
  int64_t reliable_ack = 0, command_seq = context->server_command_sequence, pe_number = context->parse_entities_number;
  qa_q3_baseline_fn baseline = context->baseline; void *baseline_ctx = context->baseline_ctx;
  qa_q3_history_fn history = context->history; void *history_ctx = context->history_ctx;
  gs_baseline_t gs_ctx; gs_ctx.gs = NULL;
  qa_q3_gamestate pending; memset(&pending, 0, sizeof pending);
  size_t pending_cap = 0;
  bool ok = true;
  for (;;) {
    qa_q3_server_message_context ctx = *context;
    ctx.server_command_sequence = command_seq; ctx.parse_entities_number = pe_number;
    ctx.baseline = baseline; ctx.baseline_ctx = baseline_ctx;
    ctx.history = history; ctx.history_ctx = history_ctx;
    qa_q3_server_message_step step;
    if (!qa_q3_server_message_cursor_next(cursor, &ctx, &step)) { ok = false; break; }
    bool done = false;
    switch (step.kind) {
      case QA_Q3_STEP_ACKNOWLEDGE: reliable_ack = step.sequence; break;
      case QA_Q3_STEP_GAMESTATE_START:
        qa_q3_gamestate_destroy(&pending); memset(&pending, 0, sizeof pending); pending_cap = 0;
        pe_number = 0; baseline = null_baseline; baseline_ctx = NULL; history = null_history; history_ctx = NULL;
        break;
      case QA_Q3_STEP_GAMESTATE_SEQUENCE: pending.command_sequence = step.sequence; command_seq = step.sequence; break;
      case QA_Q3_STEP_GAMESTATE_ENTRY: {
        if (pending.entry_count == pending_cap) {
          size_t nc = pending_cap ? pending_cap * 2 : 16;
          qa_q3_gamestate_entry *ne = realloc(pending.entries, nc * sizeof *ne);
          if (!ne) { qa_q3_server_message_step_destroy(&step); ok = false; done = true; break; }
          pending.entries = ne; pending_cap = nc;
        }
        pending.entries[pending.entry_count++] = step.entry;
        memset(&step.entry, 0, sizeof step.entry);
        break;
      }
      case QA_Q3_STEP_GAMESTATE_CLIENT: pending.client_number = step.number; break;
      case QA_Q3_STEP_GAMESTATE_CHECKSUM: pending.checksum_feed = step.checksum; break;
      case QA_Q3_STEP_SNAPSHOT_HEADER: break;
      case QA_Q3_STEP_GAMESTATE_END: {
        qa_q3_server_operation *ops = realloc(out->operations, (out->operation_count + 1) * sizeof *ops);
        if (!ops) { qa_q3_server_message_step_destroy(&step); ok = false; done = true; break; }
        out->operations = ops;
        qa_q3_server_operation *op = &out->operations[out->operation_count++];
        memset(op, 0, sizeof *op); op->kind = QA_Q3_OP_GAMESTATE; op->gamestate = pending;
        memset(&pending, 0, sizeof pending); pending_cap = 0;
        gs_ctx.gs = &op->gamestate;
        baseline = gs_baseline_fn; baseline_ctx = &gs_ctx;
        break;
      }
      case QA_Q3_STEP_OPERATION: {
        qa_q3_server_operation *ops = realloc(out->operations, (out->operation_count + 1) * sizeof *ops);
        if (!ops) { qa_q3_server_message_step_destroy(&step); ok = false; done = true; break; }
        out->operations = ops;
        /* gs_ctx.gs may dangle after realloc; rebind below by scanning. */
        out->operations[out->operation_count] = step.operation;
        memset(&step.operation, 0, sizeof step.operation);
        qa_q3_server_operation *op = &out->operations[out->operation_count++];
        if (op->kind == QA_Q3_OP_COMMAND) command_seq = op->sequence;
        if (op->kind == QA_Q3_OP_SNAPSHOT) pe_number = (int32_t)(pe_number + (int64_t)op->snapshot.entity_count);
        if (gs_ctx.gs) {
          gs_ctx.gs = NULL;
          for (size_t i = 0; i < out->operation_count; i++)
            if (out->operations[i].kind == QA_Q3_OP_GAMESTATE) gs_ctx.gs = &out->operations[i].gamestate;
        }
        break;
      }
      case QA_Q3_STEP_END:
        out->reliable_acknowledge = reliable_ack;
        out->server_command_sequence = command_seq;
        out->parse_entities_number = pe_number;
        out->terminal = step.terminal;
        done = true; break;
    }
    qa_q3_server_message_step_destroy(&step);
    if (done) break;
  }
  qa_q3_gamestate_destroy(&pending);
  qa_q3_server_message_cursor_destroy(cursor);
  if (!ok) qa_q3_server_message_destroy(out);
  return ok;
}
static void write_gamestate(qa_q3_message_writer *w, const qa_q3_gamestate *gs) {
  qa_q3_message_writer_write_byte(w, QA_Q3_SVC_GAMESTATE);
  qa_q3_message_writer_write_long(w, gs->command_sequence);
  int64_t data_count = 1;
  for (size_t i = 0; i < gs->entry_count; i++) {
    const qa_q3_gamestate_entry *e = &gs->entries[i];
    if (e->kind == QA_Q3_GS_CONFIGSTRING) {
      if (e->index < 0 || e->index >= MAX_CONFIGSTRINGS) qa_range_error("Invalid configstring index");
      size_t length = e->value ? strlen(e->value) : 0;
      if (length >= 8192) qa_range_error("Configstring exceeds BIG_INFO_STRING");
      data_count += (int64_t)length + 1;
      if (data_count > MAX_GAMESTATE_CHARS) qa_range_error("Gamestate string storage exceeded");
      qa_q3_message_writer_write_byte(w, QA_Q3_SVC_CONFIGSTRING);
      qa_q3_message_writer_write_short(w, e->index);
      qa_q3_message_writer_write_big_string(w, e->value);
    } else {
      if (e->number < 0 || e->number > ENTITY_SENTINEL) qa_range_error("Invalid baseline number");
      qa_q3_message_writer_write_byte(w, QA_Q3_SVC_BASELINE);
      if (e->entity.number == ENTITY_SENTINEL && e->number != ENTITY_SENTINEL) {
        qa_q3_entity_state_record removed; qa_q3_entity_state_record_init(&removed, 0);
        removed.number = (int32_t)e->number;
        qa_q3_write_delta_entity(w, &removed, NULL, false);
      } else {
        if (e->entity.number != e->number) qa_range_error("Baseline index differs from entity number");
        qa_q3_entity_state_record zero; qa_q3_entity_state_record_init(&zero, 0);
        qa_q3_write_delta_entity(w, &zero, &e->entity, true);
      }
    }
  }
  qa_q3_message_writer_write_byte(w, QA_Q3_SVC_EOF);
  qa_q3_message_writer_write_long(w, gs->client_number);
  qa_q3_message_writer_write_long(w, gs->checksum_feed);
}
static void write_download(qa_q3_message_writer *w, const qa_q3_download_block *b) {
  qa_q3_message_writer_write_byte(w, QA_Q3_SVC_DOWNLOAD);
  if (b->kind == QA_Q3_DOWNLOAD_ERROR) {
    if (b->file_size < -((int64_t)0x80000000) || b->file_size >= 0) qa_range_error("Invalid download error file size");
    qa_q3_message_writer_write_short(w, 0); qa_q3_message_writer_write_long(w, b->file_size);
    qa_q3_message_writer_write_string(w, b->message);
    return;
  }
  if (b->data_len > MAX_DOWNLOAD_BLOCK) qa_range_error("Download block exceeds 2048 bytes");
  if (b->kind == QA_Q3_DOWNLOAD_START) {
    if (b->file_size < 0 || b->file_size > 0x7fffffff) qa_range_error("Invalid download file size");
    qa_q3_message_writer_write_short(w, 0); qa_q3_message_writer_write_long(w, b->file_size);
  } else {
    if (b->number == 0 || b->number < -32768 || b->number > 32767) qa_range_error("Invalid download block number");
    qa_q3_message_writer_write_short(w, b->number);
  }
  qa_q3_message_writer_write_short(w, (int64_t)b->data_len);
  qa_q3_message_writer_write_data(w, b->data, b->data_len);
}
static void write_snapshot(qa_q3_message_writer *w, const qa_q3_wire_snapshot *snap, const qa_q3_server_message_context *ctx,
                           qa_q3_baseline_fn baseline, void *bctx, qa_q3_history_fn history, void *hctx) {
  if (snap->message_number != ctx->message_number) qa_range_error("Snapshot message number differs from envelope");
  if (snap->player_state.product != ctx->product) qa_range_error("Snapshot product differs from envelope");
  if (snap->message_number < -((int64_t)0x80000000) || snap->message_number > 0x7fffffff
      || snap->delta_number < -((int64_t)0x80000000) || snap->delta_number > 0x7fffffff)
    qa_range_error("Snapshot sequence fields must be int32");
  int64_t distance = snap->delta_number <= 0 ? 0 : (int32_t)(snap->message_number - snap->delta_number);
  if (distance < 0 || distance > 255 || (distance == 0 && snap->delta_number > 0)) qa_range_error("Invalid snapshot delta distance");
  const qa_q3_snapshot_history_entry *old = distance == 0 ? NULL : (history ? history(hctx, snap->delta_number) : NULL);
  if (distance != 0 && (!old || !old->valid || old->snapshot.message_number != snap->delta_number))
    qa_range_error("Missing valid snapshot baseline for encoding");
  if (snap->area_mask_len > MAX_AREA_BYTES) qa_range_error("Snapshot area mask exceeds 32 bytes");
  if (snap->flags < 0 || snap->flags > 255) qa_range_error("Invalid snapshot flags");
  qa_q3_message_writer_write_byte(w, QA_Q3_SVC_SNAPSHOT);
  qa_q3_message_writer_write_long(w, snap->server_time);
  qa_q3_message_writer_write_byte(w, distance);
  qa_q3_message_writer_write_byte(w, snap->flags);
  qa_q3_message_writer_write_byte(w, (int64_t)snap->area_mask_len);
  qa_q3_message_writer_write_data(w, snap->area_mask, snap->area_mask_len);
  qa_q3_write_delta_player_state(w, old ? &old->snapshot.player_state : NULL, &snap->player_state);
  write_packet_entities(w, old ? old->snapshot.entities : NULL, old ? old->snapshot.entity_count : 0,
                        snap->entities, snap->entity_count, baseline, bctx);
}
void qa_q3_write_server_message(qa_q3_message_writer *w, int64_t reliable_acknowledge,
                                const qa_q3_server_operation *operations, size_t operation_count,
                                const qa_q3_server_message_context *context) {
  qa_q3_message_writer_write_long(w, reliable_acknowledge);
  qa_q3_baseline_fn baseline = context->baseline; void *bctx = context->baseline_ctx;
  qa_q3_history_fn history = context->history; void *hctx = context->history_ctx;
  gs_baseline_t gs_ctx; gs_ctx.gs = NULL;
  bool download_error = false;
  for (size_t i = 0; i < operation_count; i++) {
    const qa_q3_server_operation *op = &operations[i];
    if (download_error) qa_range_error("Operations cannot follow a terminal download error");
    switch (op->kind) {
      case QA_Q3_OP_NOP: qa_q3_message_writer_write_byte(w, QA_Q3_SVC_NOP); break;
      case QA_Q3_OP_COMMAND:
        if (op->text && strlen(op->text) >= 1024) qa_range_error("Server command exceeds MAX_STRING_CHARS");
        qa_q3_message_writer_write_byte(w, QA_Q3_SVC_COMMAND);
        qa_q3_message_writer_write_long(w, op->sequence);
        qa_q3_message_writer_write_string(w, op->text);
        break;
      case QA_Q3_OP_GAMESTATE:
        write_gamestate(w, &op->gamestate);
        gs_ctx.gs = &op->gamestate; baseline = gs_baseline_fn; bctx = &gs_ctx;
        history = null_history; hctx = NULL;
        break;
      case QA_Q3_OP_DOWNLOAD:
        write_download(w, &op->download);
        download_error = op->download.kind == QA_Q3_DOWNLOAD_ERROR;
        break;
      case QA_Q3_OP_SNAPSHOT:
        if (op->validity.kind != QA_Q3_SNAP_VALID) qa_range_error("Cannot encode an invalid decoded snapshot");
        write_snapshot(w, &op->snapshot, context, baseline, bctx, history, hctx);
        break;
    }
  }
}
bool qa_q3_encode_server_message(int64_t reliable_acknowledge, const qa_q3_server_operation *operations, size_t operation_count,
                                 const qa_q3_server_message_context *context, qa_q3_source_message_state *source_state,
                                 uint8_t **out, size_t *out_len) {
  qa_q3_message_writer *w = NULL;
  if (!qa_q3_message_writer_init(&w, QA_Q3_MESSAGE_BITSTREAM, QA_Q3_MAX_MESSAGE_LENGTH, source_state)) return false;
  qa_q3_write_server_message(w, reliable_acknowledge, operations, operation_count, context);
  qa_q3_message_writer_write_byte(w, QA_Q3_SVC_EOF);
  if (qa_q3_message_writer_overflowed(w)) { qa_q3_message_writer_destroy(w); qa_range_error("Server message exceeds MAX_MSGLEN"); }
  bool ok = qa_q3_message_writer_to_bytes(w, out, out_len);
  qa_q3_message_writer_destroy(w);
  return ok;
}
