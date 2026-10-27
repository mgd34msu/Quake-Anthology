/* Port of src/network/q1/commands.ts. GPL-2.0-or-later. */
#include "qa/network/q1/commands.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/checksum.h"
#include "qa/network/q1/codecs/codec.h"
#include "qa/network/q1/quakeworld.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
void qa_quakeworld_client_message_destroy(qa_quakeworld_client_message *m) {
  free(m->text); free(m->bytes); memset(m, 0, sizeof *m);
}
void qa_write_quakeworld_move(qa_size_buf *sb, qa_quakeworld_profile profile, const qa_quakeworld_move *bundle, double sequence) {
  qa_message_reader empty; qa_message_reader_init(&empty, NULL, 0);
  qa_protocol_codec codec; qa_create_quakeworld_codec(profile, &empty, &codec);
  qa_msg_write_byte(sb, 3);
  long long checksum_offset = sb->cursize;
  qa_msg_write_byte(sb, 0);
  qa_msg_write_byte(sb, bundle->loss_percent);
  qa_qw_usercmd a, b, c, zero;
  qa_qw_wire_command_from_contract(&bundle->oldest, &a);
  qa_qw_wire_command_from_contract(&bundle->previous, &b);
  qa_qw_wire_command_from_contract(&bundle->current, &c);
  qa_qw_usercmd_init(&zero);
  codec.write_delta_usercmd(&codec, sb, &zero, &a);
  codec.write_delta_usercmd(&codec, sb, &a, &b);
  codec.write_delta_usercmd(&codec, sb, &b, &c);
  sb->data[checksum_offset] = (uint8_t)qa_quake_world_checksum(sb->data + checksum_offset + 1,
      (size_t)(sb->cursize - checksum_offset - 1), sequence);
}
qa_quakeworld_client_message *qa_decode_quakeworld_client(const uint8_t *bytes, size_t length,
                                                          qa_quakeworld_profile profile, double sequence, size_t *out_count) {
  qa_message_reader r; qa_message_reader_init(&r, bytes, length);
  qa_protocol_codec codec; qa_create_quakeworld_codec(profile, &r, &codec);
  size_t cap = 4, count = 0, dummy; bool moved = false;
  qa_quakeworld_client_message *out = calloc(cap, sizeof *out);
  while (qa_message_reader_remaining(&r) > 0) {
    qa_quakeworld_client_message m; memset(&m, 0, sizeof m);
    switch ((int)qa_message_reader_byte(&r)) {
      case 1: m.kind = "nop"; break;
      case 4: m.kind = "string-command"; m.text = qa_message_reader_string(&r, 2047, false, &dummy); break;
      case 5: m.kind = "delta"; m.sequence = qa_message_reader_byte(&r); break;
      case 6: {
        m.kind = "spectator-teleport";
        double flags = qa_q1_protocol_flags(profile);
        m.origin.x = codec.read_coord(&codec, flags);
        m.origin.y = codec.read_coord(&codec, flags);
        m.origin.z = codec.read_coord(&codec, flags);
        break;
      }
      case 7: {
        double size = qa_message_reader_short(&r), percent = qa_message_reader_byte(&r);
        m.kind = "upload"; m.percent = percent;
        m.bytes = qa_message_reader_bytes(&r, (long long)size, &m.length);
        break;
      }
      case 3: {
        if (moved) qa_q1_packet_error("Multiple moves in one packet", r.offset - 1);
        moved = true;
        double check = qa_message_reader_byte(&r);
        long long start = r.offset;
        double loss = qa_message_reader_byte(&r);
        qa_qw_usercmd a, b, c, zero;
        qa_qw_usercmd_init(&a); qa_qw_usercmd_init(&b); qa_qw_usercmd_init(&c); qa_qw_usercmd_init(&zero);
        codec.read_delta_usercmd(&codec, &zero, &a);
        codec.read_delta_usercmd(&codec, &a, &b);
        codec.read_delta_usercmd(&codec, &b, &c);
        qa_message_reader_finish(&r);
        if (qa_quake_world_checksum(bytes + start, (size_t)(r.offset - start), sequence) != check)
          qa_q1_packet_error("Invalid movement checksum", start - 1);
        m.kind = "move";
        m.bundle.oldest = qa_qw_command_from_wire(&a);
        m.bundle.previous = qa_qw_command_from_wire(&b);
        m.bundle.current = qa_qw_command_from_wire(&c);
        m.bundle.loss_percent = loss;
        break;
      }
      default: qa_q1_packet_error("Unknown QuakeWorld client message", r.offset - 1);
    }
    qa_message_reader_finish(&r);
    if (count == cap) { cap *= 2; out = realloc(out, cap * sizeof *out); }
    out[count++] = m;
  }
  *out_count = count; return out;
}
void qa_write_quakeworld_upload(qa_size_buf *sb, const uint8_t *bytes, size_t length, double percent) {
  if (length > 768) qa_range_error("QuakeWorld upload block too large");
  qa_msg_write_byte(sb, 7); qa_msg_write_short(sb, (double)length); qa_msg_write_byte(sb, percent);
  qa_sz_write(sb, bytes, (long long)length, (long long)length);
}
void qa_quakeworld_command_replay_init(qa_quakeworld_command_replay *r) {
  qa_qw_usercmd zero; qa_qw_usercmd_init(&zero);
  r->last = qa_qw_command_from_wire(&zero);
}
void qa_quakeworld_command_replay_run(qa_quakeworld_command_replay *r, const qa_quakeworld_move *bundle,
                                      double dropped, bool paused, qa_qw_apply_fn apply, void *ctx) {
  if (!paused) {
    if (dropped < 20) {
      for (double pending = dropped; pending > 2; pending--) apply(ctx, &r->last);
      if (dropped > 1) apply(ctx, &bundle->oldest);
      if (dropped > 0) apply(ctx, &bundle->previous);
    }
    apply(ctx, &bundle->current);
  }
  r->last = bundle->current; r->last.buttons = 0;
}
void qa_split_quakeworld_command(const qa_qw_user_command *command, qa_qw_apply_fn apply, void *ctx) {
  if (command->milliseconds > 50) {
    qa_qw_user_command half = *command;
    half.milliseconds = trunc(command->milliseconds / 2);
    qa_split_quakeworld_command(&half, apply, ctx);
    qa_split_quakeworld_command(&half, apply, ctx);
  } else apply(ctx, command);
}
