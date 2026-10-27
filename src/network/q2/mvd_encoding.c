/* Port of src/network/q2/mvd-encoding.ts. GPL-2.0-or-later. */
#include "qa/network/q2/mvd_encoding.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/codec.h"
#include "qa/network/q2/codecs/mvd.h"
#include "qa/network/q2/codecs/q2pro_fields.h"
#include "qa/network/q2/constants.h"
#include "qa/network/q2/message.h"
#include "qa/network/q2/mvd_profile.h"

static char *dup_str(const char *s) {
  size_t n = strlen(s != NULL ? s : "");
  char *out = (char *)malloc(n + 1);
  if (out != NULL) memcpy(out, s != NULL ? s : "", n + 1);
  return out;
}
static bool differs3(const float *a, const float *b) {
  for (int i = 0; i < 3; i++) {
    if ((double)a[i] != (double)b[i]) return true;
  }
  return false;
}
static bool differs4(const float *a, const float *b) {
  for (int i = 0; i < 4; i++) {
    if ((double)a[i] != (double)b[i]) return true;
  }
  return false;
}
static double origin_at(const qa_q2_wire_player *p, bool rr, int axis) {
  return rr ? (double)p->pmove.origin_f[axis] : (double)p->pmove.origin[axis];
}
static void write_vector(qa_q2_sizebuf *m, const float *v, double scale,
                         bool as_short) {
  for (int i = 0; i < 3; i++) {
    double packed = trunc((double)v[i] * scale);
    if (as_short) qa_q2_msg_write_short(m, packed);
    else qa_q2_msg_write_char(m, packed);
  }
}

static void player_delta(qa_q2_sizebuf *message, const qa_q2_wire_player *from,
                         const qa_q2_wire_player *to, double number,
                         const qa_q2_mvd_profile *profile) {
  if (!qa_q2_msg_valid_mvd_client_number(number)) {
    qa_range_error("Invalid MVD player number");
  }
  if (!profile->extended) {
    qa_q2_msg_write_delta_mvd_playerstate(message, from, to, number, from == NULL);
    return;
  }
  if (to == NULL) {
    qa_q2_msg_write_byte(message, number);
    qa_q2_msg_write_short(message, QA_Q2_PPS_MOREBITS);
    if (profile->fog) qa_q2_msg_write_byte(message, 1);
    return;
  }
  qa_q2_wire_player empty;
  qa_q2_wire_player_init(&empty);
  const qa_q2_wire_player *prev = from != NULL ? from : &empty;
  bool rr = profile->rerelease;
  int32_t bits = 0;
  if (to->pmove.pm_type != prev->pmove.pm_type) bits |= QA_Q2_PPS_M_TYPE;
  if (origin_at(to, rr, 0) != origin_at(prev, rr, 0) ||
      origin_at(to, rr, 1) != origin_at(prev, rr, 1)) bits |= QA_Q2_PPS_M_ORIGIN;
  if (origin_at(to, rr, 2) != origin_at(prev, rr, 2)) bits |= QA_Q2_PPS_M_ORIGIN2;
  if (differs3(to->viewoffset, prev->viewoffset)) bits |= QA_Q2_PPS_VIEWOFFSET;
  if (qa_q2_angle2short((double)to->viewangles[0]) !=
          qa_q2_angle2short((double)prev->viewangles[0]) ||
      qa_q2_angle2short((double)to->viewangles[1]) !=
          qa_q2_angle2short((double)prev->viewangles[1])) bits |= QA_Q2_PPS_VIEWANGLES;
  if (qa_q2_angle2short((double)to->viewangles[2]) !=
      qa_q2_angle2short((double)prev->viewangles[2])) bits |= QA_Q2_PPS_VIEWANGLE2;
  if (differs3(to->kick_angles, prev->kick_angles)) bits |= QA_Q2_PPS_KICKANGLES;
  if (to->gunindex != prev->gunindex || to->gunskin != prev->gunskin) {
    bits |= QA_Q2_PPS_WEAPONINDEX;
  }
  if (to->gunframe != prev->gunframe) bits |= QA_Q2_PPS_WEAPONFRAME;
  if (differs3(to->gunoffset, prev->gunoffset)) bits |= QA_Q2_PPS_GUNOFFSET;
  if (differs3(to->gunangles, prev->gunangles)) bits |= QA_Q2_PPS_GUNANGLES;
  if (differs4(to->blend, prev->blend) ||
      ((profile->v2 || rr) && differs4(to->damage_blend, prev->damage_blend))) {
    bits |= QA_Q2_PPS_BLEND;
  }
  int32_t fog = profile->fog ? qa_q2pro_fog_bits(&prev->q2pro_fog, &to->q2pro_fog) : 0;
  if (fog != 0) bits |= QA_Q2_PPS_MOREBITS | (1 << 17);
  if (to->fov != prev->fov) bits |= QA_Q2_PPS_FOV;
  if (to->rdflags != prev->rdflags) bits |= QA_Q2_PPS_RDFLAGS;
  uint64_t stats = 0;
  int stat_count = (profile->v2 || rr) ? 64 : 32;
  for (int i = 0; i < stat_count; i++) {
    if (to->stats[i] != prev->stats[i]) stats |= (uint64_t)1 << i;
  }
  if (stats != 0) bits |= QA_Q2_PPS_STATS;
  if (bits == 0 && from != NULL) return;
  qa_q2_msg_write_byte(message, number);
  qa_q2_msg_write_short(message, (double)bits);
  if (bits & QA_Q2_PPS_MOREBITS) {
    qa_q2_msg_write_byte(message, (double)((uint32_t)bits >> 16));
  }
  if (bits & QA_Q2_PPS_M_TYPE) qa_q2_msg_write_byte(message, to->pmove.pm_type);
  for (int axis = 0; axis < 3; axis++) {
    if (bits & (axis == 2 ? QA_Q2_PPS_M_ORIGIN2 : QA_Q2_PPS_M_ORIGIN)) {
      double value = origin_at(to, rr, axis);
      if (rr) qa_q2_msg_write_float(message, value);
      else if (profile->v2) {
        qa_q2pro_write_int23(message, (int32_t)value,
                             (int32_t)origin_at(prev, rr, axis));
      } else qa_q2_msg_write_short(message, value);
    }
  }
  if (bits & QA_Q2_PPS_VIEWOFFSET) {
    write_vector(message, to->viewoffset, rr ? 16 : 4, rr);
  }
  if (bits & QA_Q2_PPS_VIEWANGLES) {
    qa_q2_msg_write_short(message,
                          (double)qa_q2_angle2short((double)to->viewangles[0]));
    qa_q2_msg_write_short(message,
                          (double)qa_q2_angle2short((double)to->viewangles[1]));
  }
  if (bits & QA_Q2_PPS_VIEWANGLE2) {
    qa_q2_msg_write_short(message,
                          (double)qa_q2_angle2short((double)to->viewangles[2]));
  }
  if (bits & QA_Q2_PPS_KICKANGLES) {
    write_vector(message, to->kick_angles, rr ? 1024 : 4, rr);
  }
  if (bits & QA_Q2_PPS_WEAPONINDEX) {
    /* TS: to.gunindex | to.gunskin << 13 (ToInt32 then writeShort). */
    int32_t packed = (int32_t)to->gunindex | ((int32_t)to->gunskin << 13);
    qa_q2_msg_write_short(message, (double)packed);
  }
  if (bits & QA_Q2_PPS_WEAPONFRAME) {
    if (rr) qa_q2_msg_write_short(message, to->gunframe);
    else qa_q2_msg_write_byte(message, to->gunframe);
  }
  if (bits & QA_Q2_PPS_GUNOFFSET) {
    write_vector(message, to->gunoffset, rr ? 512 : 8, true);
  }
  if (bits & QA_Q2_PPS_GUNANGLES) {
    write_vector(message, to->gunangles, rr ? 4096 : 65536.0 / 360.0, true);
  }
  if (bits & QA_Q2_PPS_BLEND) {
    if (profile->v2 || rr) {
      int blend = 0;
      for (int i = 0; i < 4; i++) {
        if ((double)to->blend[i] != (double)prev->blend[i]) blend |= 1 << i;
        if ((double)to->damage_blend[i] != (double)prev->damage_blend[i]) {
          blend |= 16 << i;
        }
      }
      qa_q2_msg_write_byte(message, (double)blend);
      for (int i = 0; i < 4; i++) {
        if (blend & (1 << i)) {
          qa_q2_msg_write_byte(message, trunc((double)to->blend[i] * 255));
        }
      }
      for (int i = 0; i < 4; i++) {
        if (blend & (16 << i)) {
          qa_q2_msg_write_byte(message, trunc((double)to->damage_blend[i] * 255));
        }
      }
    } else {
      for (int i = 0; i < 4; i++) {
        qa_q2_msg_write_byte(message, trunc((double)to->blend[i] * 255));
      }
    }
  }
  if (fog != 0) qa_q2pro_write_fog(message, fog, &to->q2pro_fog);
  if (bits & QA_Q2_PPS_FOV) qa_q2_msg_write_byte(message, to->fov);
  if (bits & QA_Q2_PPS_RDFLAGS) qa_q2_msg_write_byte(message, to->rdflags);
  if (bits & QA_Q2_PPS_STATS) {
    if (rr) qa_q2_msg_write_long64(message, (int64_t)stats);
    else if (profile->v2) qa_q2pro_write_var64(message, stats);
    else qa_q2_msg_write_long(message, (double)(uint32_t)stats);
    for (int i = 0; i < 64; i++) {
      if (stats & ((uint64_t)1 << i)) qa_q2_msg_write_short(message, to->stats[i]);
    }
  }
}

uint8_t *qa_q2_encode_mvd_emission(const qa_q2_mvd_emission *event,
                                   size_t *out_length) {
  if (event->length < 1 || event->length >= 2048) {
    qa_range_error("MVD routed payload must contain1..2047bytes");
  }
  qa_q2_sizebuf message;
  if (!qa_q2_message_create(&message, (int64_t)event->length + 5)) return NULL;
  double high = (double)(event->length >> 8);
  double opcode;
  if (event->recipient.kind == QA_Q2_MVD_RECIPIENT_PLAYER) {
    opcode = event->reliable ? QA_Q2_MVD_UNICAST_R : QA_Q2_MVD_UNICAST;
  } else {
    opcode = (event->reliable ? QA_Q2_MVD_MULTICAST_ALL_R : QA_Q2_MVD_MULTICAST_ALL) +
             (event->recipient.kind == QA_Q2_MVD_RECIPIENT_PHS ? 1
              : event->recipient.kind == QA_Q2_MVD_RECIPIENT_PVS ? 2 : 0);
  }
  qa_q2_msg_write_mvd_cmd(&message, opcode, high);
  qa_q2_msg_write_byte(&message, (double)event->length);
  if (event->recipient.kind == QA_Q2_MVD_RECIPIENT_PLAYER) {
    if (!qa_q2_msg_valid_mvd_client_number(event->recipient.number)) {
      qa_range_error("Invalid MVD recipient");
    }
    qa_q2_msg_write_byte(&message, event->recipient.number);
  } else if (event->recipient.kind != QA_Q2_MVD_RECIPIENT_ALL) {
    double leaf = event->recipient.leaf;
    if (leaf != trunc(leaf) || leaf < 0 || leaf >= 65535) {
      qa_range_error("Invalid MVD leaf");
    }
    qa_q2_msg_write_short(&message, leaf);
  }
  qa_q2_sz_write(&message, event->bytes, event->length, (int64_t)event->length);
  uint8_t *out = qa_q2_message_bytes(&message, out_length);
  qa_q2_message_destroy(&message);
  return out;
}

void qa_q2_mvd_encoder_init(qa_q2_mvd_encoder *encoder) {
  memset(encoder, 0, sizeof *encoder);
}
void qa_q2_mvd_encoder_reset(qa_q2_mvd_encoder *encoder) {
  for (size_t i = 0; i < encoder->config_count; i++) {
    free((void *)encoder->configs[i].value);
  }
  free(encoder->configs);
  free(encoder->players);
  free(encoder->entities);
  memset(encoder, 0, sizeof *encoder);
}
void qa_q2_mvd_encoder_destroy(qa_q2_mvd_encoder *encoder) {
  qa_q2_mvd_encoder_reset(encoder);
}

static const char *config_get(const qa_q2_mvd_capture_config *list, size_t count,
                              double index) {
  for (size_t i = 0; i < count; i++) {
    if (list[i].index == index) return list[i].value;
  }
  return NULL;
}
static const qa_q2_wire_player *player_get(const qa_q2_mvd_capture_player *list,
                                           size_t count, double number) {
  for (size_t i = 0; i < count; i++) {
    if (list[i].number == number) return &list[i].player;
  }
  return NULL;
}
static const qa_q2_wire_entity *entity_get(const qa_q2_wire_entity *list,
                                           size_t count, double number) {
  for (size_t i = 0; i < count; i++) {
    if (list[i].number == number) return &list[i];
  }
  return NULL;
}
/* Deep-copy capture as the new previous; false on OOM. */
static bool remember(qa_q2_mvd_encoder *e, const qa_q2_mvd_capture *c) {
  qa_q2_mvd_capture_config *configs = NULL;
  if (c->config_string_count > 0) {
    configs = (qa_q2_mvd_capture_config *)calloc(c->config_string_count,
                                                 sizeof *configs);
    if (configs == NULL) return false;
    for (size_t i = 0; i < c->config_string_count; i++) {
      configs[i].index = c->config_strings[i].index;
      configs[i].value = dup_str(c->config_strings[i].value);
      if (configs[i].value == NULL) {
        for (size_t k = 0; k < i; k++) free((void *)configs[k].value);
        free(configs);
        return false;
      }
    }
  }
  qa_q2_mvd_capture_player *players = NULL;
  if (c->player_count > 0) {
    players = (qa_q2_mvd_capture_player *)malloc(c->player_count * sizeof *players);
    if (players == NULL) {
      for (size_t k = 0; k < c->config_string_count; k++) {
        free((void *)configs[k].value);
      }
      free(configs);
      return false;
    }
    memcpy(players, c->players, c->player_count * sizeof *players);
  }
  qa_q2_wire_entity *entities = NULL;
  if (c->entity_count > 0) {
    entities = (qa_q2_wire_entity *)malloc(c->entity_count * sizeof *entities);
    if (entities == NULL) {
      for (size_t k = 0; k < c->config_string_count; k++) {
        free((void *)configs[k].value);
      }
      free(configs);
      free(players);
      return false;
    }
    memcpy(entities, c->entities, c->entity_count * sizeof *entities);
  }
  qa_q2_mvd_encoder_reset(e);
  e->has_previous = true;
  e->revision = c->revision;
  e->flags = c->flags;
  e->servercount = c->servercount;
  e->configs = configs;
  e->config_count = c->config_string_count;
  e->players = players;
  e->player_count = c->player_count;
  e->entities = entities;
  e->entity_count = c->entity_count;
  return true;
}

bool qa_q2_mvd_encoder_capture(qa_q2_mvd_encoder *encoder,
                               const qa_q2_mvd_capture *capture,
                               qa_q2_mvd_byte_block **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  qa_q2_mvd_profile profile;
  qa_q2_get_mvd_profile(capture->revision, capture->flags, &profile);
  bool changed = !encoder->has_previous ||
                 encoder->servercount != capture->servercount ||
                 encoder->revision != capture->revision ||
                 encoder->flags != capture->flags;
  qa_q2_wire_codec wire;
  if (!qa_q2_wire_codec_init(&wire, profile.protocol)) return false;
  if (strcmp(profile.protocol.kind, "q2-q2pro") == 0) {
    qa_q2_wire_codec_accept_q2pro_features(&wire, (double)profile.protocol.revision,
                                           profile.v2 ? 24 : 8);
  }
  qa_q2_sizebuf message;
  if (!qa_q2_message_create(&message, QA_Q2_MVD_MAX_MESSAGE)) {
    qa_q2_wire_codec_destroy(&wire);
    return false;
  }
  if (changed) {
    qa_q2_msg_write_mvd_cmd(&message, QA_Q2_MVD_SERVERDATA,
                            capture->revision < 2012 || profile.rerelease
                                ? capture->flags
                                : 0);
    qa_q2_msg_write_long(&message, 37);
    qa_q2_msg_write_short(&message, capture->revision);
    if (profile.revision >= 2012 && !profile.rerelease) {
      qa_q2_msg_write_short(&message, capture->flags);
    }
    qa_q2_msg_write_long(&message, capture->servercount);
    qa_q2_msg_write_string(&message, capture->gamedir);
    qa_q2_msg_write_short(&message, capture->dummy);
    for (size_t i = 0; i < capture->config_string_count; i++) {
      double index = capture->config_strings[i].index;
      if (index < 0 || index >= profile.max_config_strings) {
        qa_range_error("Invalid MVD configstring");
      }
      qa_q2_msg_write_short(&message, index);
      qa_q2_msg_write_string(&message, capture->config_strings[i].value);
    }
    qa_q2_msg_write_short(&message, profile.max_config_strings);
  } else {
    for (size_t i = 0; i < capture->config_string_count; i++) {
      const char *old = config_get(encoder->configs, encoder->config_count,
                                   capture->config_strings[i].index);
      const char *value = capture->config_strings[i].value;
      if (old == NULL || strcmp(old, value != NULL ? value : "") != 0) {
        qa_q2_msg_write_byte(&message, QA_Q2_MVD_CONFIGSTRING);
        qa_q2_msg_write_short(&message, capture->config_strings[i].index);
        qa_q2_msg_write_string(&message, value);
      }
    }
    for (size_t i = 0; i < encoder->config_count; i++) {
      if (config_get(capture->config_strings, capture->config_string_count,
                     encoder->configs[i].index) == NULL) {
        qa_q2_msg_write_byte(&message, QA_Q2_MVD_CONFIGSTRING);
        qa_q2_msg_write_short(&message, encoder->configs[i].index);
        qa_q2_msg_write_string(&message, "");
      }
    }
    qa_q2_msg_write_byte(&message, QA_Q2_MVD_FRAME);
  }
  if (capture->portal_bits_length > 255) {
    qa_range_error("MVD portal state too large");
  }
  qa_q2_msg_write_byte(&message, (double)capture->portal_bits_length);
  qa_q2_sz_write(&message, capture->portal_bits, capture->portal_bits_length,
                 (int64_t)capture->portal_bits_length);
  const qa_q2_mvd_capture_player *prev_players = changed ? NULL : encoder->players;
  size_t prev_player_count = changed ? 0 : encoder->player_count;
  for (size_t i = 0; i < capture->player_count; i++) {
    player_delta(&message,
                 player_get(prev_players, prev_player_count,
                            capture->players[i].number),
                 &capture->players[i].player, capture->players[i].number,
                 &profile);
  }
  for (size_t i = 0; i < prev_player_count; i++) {
    if (player_get(capture->players, capture->player_count,
                   prev_players[i].number) == NULL) {
      player_delta(&message, &prev_players[i].player, NULL,
                   prev_players[i].number, &profile);
    }
  }
  qa_q2_msg_write_byte(&message, 255);
  const qa_q2_wire_entity *prev_entities = changed ? NULL : encoder->entities;
  size_t prev_entity_count = changed ? 0 : encoder->entity_count;
  qa_q2_protocol_codec *codec = qa_q2_wire_codec_get_codec(&wire);
  for (size_t i = 0; i < capture->entity_count; i++) {
    const qa_q2_wire_entity *entity = &capture->entities[i];
    bool duplicate = false;
    for (size_t k = 0; k < i; k++) {
      if (capture->entities[k].number == entity->number) duplicate = true;
    }
    if (entity->number < 1 || entity->number >= profile.max_entities || duplicate) {
      qa_range_error("Invalid MVD entity identity");
    }
    const qa_q2_wire_entity *prev =
        entity_get(prev_entities, prev_entity_count, entity->number);
    qa_q2_wire_entity empty;
    qa_q2_wire_entity_init(&empty);
    codec->write_delta_entity(codec, &message, prev != NULL ? prev : &empty, entity,
                              prev == NULL, true);
  }
  for (size_t i = 0; i < prev_entity_count; i++) {
    if (entity_get(capture->entities, capture->entity_count,
                   prev_entities[i].number) == NULL) {
      codec->write_entity_remove(codec, &message, prev_entities[i].number);
    }
  }
  codec->write_packet_entities_end(codec, &message);
  size_t total = 1 + capture->message_count;
  qa_q2_mvd_byte_block *blocks =
      (qa_q2_mvd_byte_block *)calloc(total, sizeof *blocks);
  if (blocks == NULL) {
    qa_q2_message_destroy(&message);
    qa_q2_wire_codec_destroy(&wire);
    return false;
  }
  blocks[0].bytes = qa_q2_message_bytes(&message, &blocks[0].length);
  qa_q2_message_destroy(&message);
  qa_q2_wire_codec_destroy(&wire);
  if (blocks[0].bytes == NULL) {
    free(blocks);
    return false;
  }
  for (size_t i = 0; i < capture->message_count; i++) {
    blocks[1 + i].bytes =
        qa_q2_encode_mvd_emission(&capture->messages[i], &blocks[1 + i].length);
    if (blocks[1 + i].bytes == NULL) {
      qa_q2_mvd_byte_blocks_free(blocks, 1 + i);
      return false;
    }
  }
  if (!remember(encoder, capture)) {
    qa_q2_mvd_byte_blocks_free(blocks, total);
    return false;
  }
  *out = blocks;
  *out_count = total;
  return true;
}
