/* Port of src/network/q2/server-write.ts. GPL-2.0-or-later. */
#include "qa/network/q2/server_write.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/codecs/q2pro_fields.h"
#include "qa/network/q2/constants.h"
#include "qa/network/q2/temp_types.h"

static bool kind_is(const qa_q2_protocol_identity *p, const char *kind) {
  return p->kind != NULL && strcmp(p->kind, kind) == 0;
}

void qa_q2_write_fog(qa_q2_sizebuf *m, const qa_q2_svc_fog_data *fog) {
  int32_t bits = (int32_t)fog->bits |
                 (((int32_t)fog->bits & 0xff00) != 0 ? QA_Q2_SVC_FOG_BIT_MORE_BITS : 0);
  qa_q2_msg_write_byte(m, (double)bits);
  if (bits & QA_Q2_SVC_FOG_BIT_MORE_BITS) qa_q2_msg_write_byte(m, (double)((uint32_t)bits >> 8));
  if (bits & QA_Q2_SVC_FOG_BIT_DENSITY) {
    qa_q2_msg_write_float(m, fog->density);
    qa_q2_msg_write_byte(m, fog->skyfactor);
  }
  if (bits & QA_Q2_SVC_FOG_BIT_R) qa_q2_msg_write_byte(m, fog->red);
  if (bits & QA_Q2_SVC_FOG_BIT_G) qa_q2_msg_write_byte(m, fog->green);
  if (bits & QA_Q2_SVC_FOG_BIT_B) qa_q2_msg_write_byte(m, fog->blue);
  if (bits & QA_Q2_SVC_FOG_BIT_TIME) qa_q2_msg_write_short(m, fog->time);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_FALLOFF) qa_q2_msg_write_float(m, fog->hf_falloff);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_DENSITY) qa_q2_msg_write_float(m, fog->hf_density);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_R) qa_q2_msg_write_byte(m, fog->hf_start_r);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_G) qa_q2_msg_write_byte(m, fog->hf_start_g);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_B) qa_q2_msg_write_byte(m, fog->hf_start_b);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_DIST) qa_q2_msg_write_long(m, fog->hf_start_dist);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_R) qa_q2_msg_write_byte(m, fog->hf_end_r);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_G) qa_q2_msg_write_byte(m, fog->hf_end_g);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_B) qa_q2_msg_write_byte(m, fog->hf_end_b);
  if (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_DIST) qa_q2_msg_write_long(m, fog->hf_end_dist);
}

static void float_vector(qa_q2_sizebuf *m, const float value[3]) {
  for (int i = 0; i < 3; i++) qa_q2_msg_write_float(m, (double)value[i]);
}

static void params_from_result(const qa_q2_server_data_result *r,
                               qa_q2_server_data_params *p) {
  memset(p, 0, sizeof *p);
  p->has_server_fps = r->has_server_fps;
  p->server_fps = r->server_fps;
  p->has_wire_flags = r->has_wire_flags;
  p->wire_flags = r->wire_flags;
  p->has_protocol_revision = r->has_protocol_revision;
  p->protocol_revision = r->protocol_revision;
  p->servercount = r->servercount;
  p->attractloop = r->attractloop;
  p->gamedir = r->gamedir;
  p->clientnum = r->clientnum;
  p->has_clientnums = r->has_clientnums;
  p->clientnums = r->clientnums;
  p->clientnum_count = r->clientnum_count;
  p->levelname = r->levelname;
  p->server_state = r->server_state;
  p->has_r1q2_version = r->has_r1q2_version;
  p->r1q2_version = r->r1q2_version;
  p->has_r1q2_strafejump_hack = r->has_r1q2_strafejump_hack;
  p->r1q2_strafejump_hack = r->r1q2_strafejump_hack;
  p->has_q2pro_version = r->has_q2pro_version;
  p->q2pro_version = r->q2pro_version;
  p->has_q2pro_strafejump_hack = r->has_q2pro_strafejump_hack;
  p->q2pro_strafejump_hack = r->q2pro_strafejump_hack;
  p->has_q2pro_qw_mode = r->has_q2pro_qw_mode;
  p->q2pro_qw_mode = r->q2pro_qw_mode;
  p->has_q2pro_waterjump_hack = r->has_q2pro_waterjump_hack;
  p->q2pro_waterjump_hack = r->q2pro_waterjump_hack;
}

uint8_t *qa_q2_encode_server_event(qa_q2_wire_codec *wire, const qa_q2_server_event *event,
                                   size_t *out_length) {
  qa_q2_protocol_codec *codec = wire->selected_codec;
  bool kex = kind_is(&wire->selected_protocol, "q2-kex") ||
             kind_is(&wire->selected_protocol, "q2-kex-demo");
  bool rerelease = kind_is(&wire->selected_protocol, "q2-rerelease") ||
                   kind_is(&wire->selected_protocol, "q2-private-classic");
  bool service = event->kind == QA_Q2_SERVER_ACHIEVEMENT || event->kind == QA_Q2_SERVER_FOG ||
                 event->kind == QA_Q2_SERVER_DAMAGE || event->kind == QA_Q2_SERVER_POI ||
                 event->kind == QA_Q2_SERVER_HELP_PATH ||
                 event->kind == QA_Q2_SERVER_LOCALIZED_PRINT;
  if (service && !kex && !rerelease) {
    qa_q2_throw_error("Q2 rerelease service message cannot fit selected protocol");
  }
  if (event->kind == QA_Q2_SERVER_SETTING &&
      !kind_is(&wire->selected_protocol, "q2-r1q2") &&
      !kind_is(&wire->selected_protocol, "q2-q2pro") && !rerelease) {
    qa_q2_throw_error("Selected Q2 protocol has no server setting message");
  }
  qa_q2_sizebuf m;
  if (!qa_q2_message_create(&m, 32768)) return NULL;
  switch (event->kind) {
    case QA_Q2_SERVER_NOP: qa_q2_msg_write_byte(&m, 6); break;
    case QA_Q2_SERVER_DISCONNECT: qa_q2_msg_write_byte(&m, 7); break;
    case QA_Q2_SERVER_RECONNECT: qa_q2_msg_write_byte(&m, 8); break;
    case QA_Q2_SERVER_LEVEL_RESTART:
      if (!kex) {
        qa_q2_message_destroy(&m);
        qa_q2_throw_error("Level-restart opcode requires KEX wire");
      }
      qa_q2_msg_write_byte(&m, 24);
      break;
    case QA_Q2_SERVER_DATA: {
      qa_q2_server_data_params params;
      params_from_result(&event->data, &params);
      codec->write_server_data(codec, &m, &params);
      break;
    }
    case QA_Q2_SERVER_PRINT:
      qa_q2_msg_write_byte(&m, 10);
      qa_q2_msg_write_byte(&m, event->level);
      qa_q2_msg_write_string(&m, event->text);
      break;
    case QA_Q2_SERVER_CENTER_PRINT:
      qa_q2_msg_write_byte(&m, 15);
      qa_q2_msg_write_string(&m, event->text);
      break;
    case QA_Q2_SERVER_COMMAND_TEXT:
      qa_q2_msg_write_byte(&m, 11);
      qa_q2_msg_write_string(&m, event->text);
      break;
    case QA_Q2_SERVER_LAYOUT:
      qa_q2_msg_write_byte(&m, 4);
      qa_q2_msg_write_string(&m, event->text);
      break;
    case QA_Q2_SERVER_ACHIEVEMENT:
      qa_q2_msg_write_byte(&m, 33);
      qa_q2_msg_write_string(&m, event->text);
      break;
    case QA_Q2_SERVER_CONFIG_STRING:
      qa_q2_msg_write_byte(&m, 13);
      qa_q2_msg_write_short(&m, event->index);
      qa_q2_msg_write_string(&m, event->text);
      break;
    case QA_Q2_SERVER_BASELINE:
      codec->write_spawn_baseline(codec, &m, &event->entity);
      break;
    case QA_Q2_SERVER_TEMPORARY_ENTITY: {
      const qa_q2_temp_entity *entity = &event->temp;
      qa_q2_msg_write_byte(&m, 3);
      qa_q2_msg_write_byte(&m, entity->type);
      for (size_t i = 0; i < entity->field_count; i++) {
        const qa_q2_temp_field *field = &entity->fields[i];
        if (field->kind == QA_Q2_TEMP_FIELD_INTEGER) {
          if (field->name == QA_Q2_TEMP_NAME_TIME) qa_q2_msg_write_long(&m, field->integer_value);
          else if (field->name == QA_Q2_TEMP_NAME_ENTITY1 ||
                   field->name == QA_Q2_TEMP_NAME_ENTITY2 ||
                   entity->type == QA_Q2_TE_Q2PRO_DAMAGE_DEALT) qa_q2_msg_write_short(&m, field->integer_value);
          else qa_q2_msg_write_byte(&m, field->integer_value);
        } else {
          float value[3] = {(float)field->vector_value.x, (float)field->vector_value.y,
                            (float)field->vector_value.z};
          if (field->name == QA_Q2_TEMP_NAME_DIRECTION) {
            qa_q2_msg_write_dir(&m, value);
          } else if (qa_q2_wire_codec_q2pro_extended_v2(wire)) {
            for (int k = 0; k < 3; k++) {
              qa_q2pro_write_int23(&m, (int32_t)trunc((double)value[k] * 8.0), 0);
            }
          } else if (qa_q2_wire_codec_floating_coordinates(wire)) {
            float_vector(&m, value);
          } else {
            qa_q2_msg_write_pos(&m, value);
          }
        }
      }
      break;
    }
    case QA_Q2_SERVER_INVENTORY:
      qa_q2_msg_write_byte(&m, 5);
      for (size_t i = 0; i < event->count_count; i++) qa_q2_msg_write_short(&m, event->counts[i]);
      break;
    case QA_Q2_SERVER_DOWNLOAD:
      qa_q2_msg_write_byte(&m, 16);
      qa_q2_msg_write_short(&m, event->has_bytes ? (double)event->bytes_length : -1);
      qa_q2_msg_write_byte(&m, event->percent);
      if (event->has_bytes) {
        qa_q2_sz_write(&m, event->bytes, event->bytes_length, (int64_t)event->bytes_length);
      }
      break;
    case QA_Q2_SERVER_SETTING:
      qa_q2_msg_write_byte(&m, wire->selected_protocol.version == 1038 ||
                                       wire->selected_protocol.version == 4038
                                   ? 37
                                   : 24);
      qa_q2_msg_write_long(&m, event->index);
      qa_q2_msg_write_long(&m, event->value_number);
      break;
    case QA_Q2_SERVER_SEAT:
      if (!kex) {
        qa_q2_message_destroy(&m);
        qa_q2_throw_error("Source seat marker requires KEX wire");
      }
      qa_q2_msg_write_byte(&m, 21);
      qa_q2_msg_write_byte(&m, event->seat);
      break;
    case QA_Q2_SERVER_MUZZLE_FLASH: {
      int32_t entity = (int32_t)event->flash_entity;
      int32_t flash = (int32_t)event->flash;
      if (event->monster && flash > 255) {
        if (kex || kind_is(&wire->selected_protocol, "q2-rerelease") ||
            kind_is(&wire->selected_protocol, "q2-private-classic")) {
          qa_q2_msg_write_byte(&m, 32);
          qa_q2_msg_write_short(&m, (double)entity);
          qa_q2_msg_write_short(&m, (double)flash);
          break;
        }
        if (qa_q2_wire_codec_q2pro_extended(wire)) {
          entity |= (flash & 0x700) << 5;
          flash &= 255;
        } else {
          qa_q2_message_destroy(&m);
          qa_q2_throw_error("Monster muzzleflash cannot fit selected Q2 protocol");
        }
      }
      qa_q2_msg_write_byte(&m, event->monster ? 2 : 1);
      qa_q2_msg_write_short(&m, (double)entity);
      qa_q2_msg_write_byte(&m, (double)(flash | (event->silenced ? 128 : 0)));
      break;
    }
    case QA_Q2_SERVER_SOUND: {
      const qa_q2_sound_message *s = &event->sound;
      int32_t flags = (int32_t)s->flags;
      if (!kex && s->index > 255) {
        if (!rerelease && !qa_q2_wire_codec_q2pro_extended(wire)) {
          qa_q2_message_destroy(&m);
          qa_q2_throw_error("Q2 sound index needs extended game layout");
        }
        flags |= 32;
      }
      if (s->has_position) flags |= 4;
      if (s->entity != 0 || s->channel != 0) flags |= 8;
      if (s->volume != 1) flags |= 1;
      if (s->attenuation != 1) flags |= 2;
      if (s->delay_seconds != 0) flags |= 16;
      int32_t channel = ((int32_t)s->entity << 3) | (int32_t)s->channel;
      if (kex && (uint32_t)channel > 65535u) flags |= 64;
      qa_q2_msg_write_byte(&m, 9);
      qa_q2_msg_write_byte(&m, (double)flags);
      if (kex || (flags & 32) != 0) qa_q2_msg_write_short(&m, s->index);
      else qa_q2_msg_write_byte(&m, s->index);
      if (flags & 1) qa_q2_msg_write_byte(&m, trunc(s->volume * 255.0));
      if (flags & 2) qa_q2_msg_write_byte(&m, trunc(s->attenuation * 64.0));
      if (flags & 16) qa_q2_msg_write_byte(&m, trunc(s->delay_seconds * 1000.0));
      if (flags & 8) {
        if (kex && (flags & 64) != 0) qa_q2_msg_write_long(&m, (double)channel);
        else qa_q2_msg_write_short(&m, (double)channel);
      }
      if (flags & 4) {
        if (!s->has_position) {
          qa_q2_message_destroy(&m);
          qa_q2_throw_error("Positioned Q2 sound has no position");
        }
        float pos[3] = {(float)s->position.x, (float)s->position.y, (float)s->position.z};
        if (qa_q2_wire_codec_q2pro_extended_v2(wire)) {
          for (int k = 0; k < 3; k++) qa_q2pro_write_int23(&m, (int32_t)trunc((double)pos[k] * 8.0), 0);
        } else if (qa_q2_wire_codec_floating_coordinates(wire)) {
          float_vector(&m, pos);
        } else {
          qa_q2_msg_write_pos(&m, pos);
        }
      }
      break;
    }
    case QA_Q2_SERVER_FOG:
      qa_q2_msg_write_byte(&m, 27);
      qa_q2_write_fog(&m, &event->fog);
      break;
    case QA_Q2_SERVER_DAMAGE:
      qa_q2_msg_write_byte(&m, 25);
      qa_q2_msg_write_byte(&m, (double)event->indicator_count);
      for (size_t i = 0; i < event->indicator_count; i++) {
        const qa_q2_kex_damage_indicator *d = &event->indicators[i];
        int32_t v = ((int32_t)d->damage & 31) | (d->health ? 32 : 0) | (d->armor ? 64 : 0) |
                    (d->shield ? 128 : 0);
        qa_q2_msg_write_byte(&m, (double)v);
        qa_q2_msg_write_dir(&m, d->direction);
      }
      break;
    case QA_Q2_SERVER_POI: {
      const qa_q2_kex_poi *p = &event->poi;
      qa_q2_msg_write_byte(&m, 30);
      qa_q2_msg_write_short(&m, p->key);
      qa_q2_msg_write_short(&m, p->time);
      float_vector(&m, p->pos);
      qa_q2_msg_write_short(&m, p->image);
      qa_q2_msg_write_byte(&m, p->color);
      qa_q2_msg_write_byte(&m, p->flags);
      break;
    }
    case QA_Q2_SERVER_HELP_PATH:
      qa_q2_msg_write_byte(&m, 31);
      qa_q2_msg_write_byte(&m, event->help_path.start ? 1 : 0);
      float_vector(&m, event->help_path.pos);
      qa_q2_msg_write_dir(&m, event->help_path.dir);
      break;
    case QA_Q2_SERVER_LOCALIZED_PRINT:
      qa_q2_msg_write_byte(&m, 26);
      qa_q2_msg_write_byte(&m, event->localized.flags);
      qa_q2_msg_write_string(&m, event->localized.base);
      qa_q2_msg_write_byte(&m, (double)event->localized.arg_count);
      for (size_t i = 0; i < event->localized.arg_count; i++) {
        qa_q2_msg_write_string(&m, event->localized.args[i]);
      }
      break;
    case QA_Q2_SERVER_FRAME:
    case QA_Q2_SERVER_PRIVATE:
      break;
  }
  uint8_t *out = qa_q2_message_bytes(&m, out_length);
  qa_q2_message_destroy(&m);
  return out;
}

void qa_q2_download_sender_init(qa_q2_download_sender *sender,
                                qa_download_source source, bool has_offset,
                                double offset, bool has_block_bytes, double block_bytes) {
  memset(sender, 0, sizeof *sender);
  double off = has_offset ? offset : 0;
  double block = has_block_bytes ? block_bytes : 1024;
  bool safe_off = off == trunc(off) && off >= 0 && off <= 9007199254740991.0;
  if (!safe_off || off < 0 || off > source.byte_length || block != trunc(block) ||
      block < 1 || block > 32767) {
    qa_range_error("Invalid Q2 download range");
  }
  sender->source = source;
  sender->offset = off;
  sender->block_bytes = block;
  sender->ended = false;
}

qa_q2_server_event *qa_q2_download_sender_next(qa_q2_download_sender *sender) {
  if (sender->ended) return NULL;
  double remaining = sender->source.byte_length - sender->offset;
  double want = sender->block_bytes < remaining ? sender->block_bytes : remaining;
  size_t length = 0;
  uint8_t *bytes = sender->source.read(sender->source.ctx, sender->offset, want, &length);
  sender->offset += (double)length;
  double percent = floor(sender->offset * 100.0 / (sender->source.byte_length != 0
                                                        ? sender->source.byte_length
                                                        : 1.0));
  if (sender->offset == sender->source.byte_length) {
    sender->ended = true;
    if (sender->source.close != NULL) sender->source.close(sender->source.ctx);
  }
  qa_q2_server_event *event = (qa_q2_server_event *)calloc(1, sizeof *event);
  if (event == NULL) {
    free(bytes);
    return NULL;
  }
  event->kind = QA_Q2_SERVER_DOWNLOAD;
  event->has_bytes = true;
  event->bytes = bytes;
  event->bytes_length = length;
  event->percent = percent;
  return event;
}

void qa_q2_download_sender_close(qa_q2_download_sender *sender) {
  if (!sender->ended) {
    sender->ended = true;
    if (sender->source.close != NULL) sender->source.close(sender->source.ctx);
  }
}
