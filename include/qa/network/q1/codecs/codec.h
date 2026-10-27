/* Port of src/network/q1/codecs/codec.ts. ProtocolCodec interface ->
 * fn-pointer table bound to a borrowed MessageReader (the TS codec
 * closures capture the reader). Optional QW members are NULL when the
 * TS codec omits them. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_CODECS_CODEC_H
#define QA_NETWORK_Q1_CODECS_CODEC_H
#include <stdbool.h>
#include "qa/network/q1/constants.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/qw_constants.h"
#include "qa/network/q1/wire_types.h"
typedef struct qa_q1_entity_update {
  qa_wire_vector origin, angles;
  double modelindex, frame, colormap, skin, effects, alpha, scale;
  bool movetype_step, sendinterval; double lerpfinish;
  qa_q1_wire_entity_state baseline;
} qa_q1_entity_update;
void qa_q1_entity_update_init(qa_q1_entity_update *u);
typedef struct qa_q1_clientdata_codec {
  double viewheight, idealpitch; qa_wire_vector punchangle, velocity;
  double items; bool onground, inwater;
  double weaponframe, armorvalue, weaponmodelindex, health, currentammo;
  double ammo_shells, ammo_nails, ammo_rockets, ammo_cells, weapon;
  bool standard_quake; double alpha;
} qa_q1_clientdata_codec;
void qa_q1_clientdata_codec_init(qa_q1_clientdata_codec *c);
typedef struct qa_q1_sound_message {
  double ent, channel, sound_num, volume, attenuation; qa_wire_vector origin;
} qa_q1_sound_message;
void qa_q1_sound_message_init(qa_q1_sound_message *s);
typedef struct qa_q1_entity_update_tail {
  bool has_alpha; double alpha; bool has_scale; double scale;
  bool has_frame2; double frame_high; bool has_model2; double model_high;
  bool has_lerpfinish; double lerpfinish;
} qa_q1_entity_update_tail;
void qa_q1_entity_update_tail_init(qa_q1_entity_update_tail *t);
void qa_q1_entity_update_tail_clear(qa_q1_entity_update_tail *t);
typedef struct qa_q1_clientdata_tail {
  double weapon_high, armor_high, ammo_high, shells_high, nails_high;
  double rockets_high, cells_high, weaponframe_high, weaponalpha;
} qa_q1_clientdata_tail;
void qa_q1_clientdata_tail_init(qa_q1_clientdata_tail *t);
void qa_q1_clientdata_tail_clear(qa_q1_clientdata_tail *t);
typedef struct qa_q1_sound_header { double ent, channel, sound_num; } qa_q1_sound_header;
void qa_q1_sound_header_init(qa_q1_sound_header *h);
typedef struct qa_qw_entity_word { double bits, ext, number; bool remove; } qa_qw_entity_word;
void qa_qw_entity_word_init(qa_qw_entity_word *w);
void qa_qw_entity_word_clear(qa_qw_entity_word *w);

typedef struct qa_protocol_codec qa_protocol_codec;
struct qa_protocol_codec {
  qa_message_reader *reader; /* borrowed */
  double protocol; const char *name;
  double max_msglen, max_datagram, max_precache, default_flags;
  bool has_max_entity_number, has_max_packet_entities;
  double max_entity_number, max_packet_entities;
  bool is_rmq; /* wide codec closure capture */
  void (*write_coord)(qa_protocol_codec *c, qa_size_buf *sb, double f, double flags);
  void (*write_angle)(qa_protocol_codec *c, qa_size_buf *sb, double f, double flags);
  double (*read_coord)(qa_protocol_codec *c, double flags);
  double (*read_angle)(qa_protocol_codec *c, double flags);
  void (*write_protocol)(qa_protocol_codec *c, qa_size_buf *sb, double flags);
  void (*write_entity_update)(qa_protocol_codec *c, qa_size_buf *sb, double e, const qa_q1_entity_update *u, double flags);
  /* Baseline is mutable: TS writeBaseline sanitizes it in place. */
  void (*write_baseline)(qa_protocol_codec *c, qa_size_buf *sb, double entnum, qa_q1_wire_entity_state *b, double flags);
  bool (*write_static)(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_wire_entity_state *s, double flags);
  bool (*write_static_sound)(qa_protocol_codec *c, qa_size_buf *sb, const qa_wire_vector *org, double snd, double vol, double atten, double flags);
  bool (*write_sound)(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_sound_message *s, double flags);
  void (*write_clientdata)(qa_protocol_codec *c, qa_size_buf *sb, const qa_q1_clientdata_codec *cd, double flags);
  double (*read_protocol_flags)(qa_protocol_codec *c);
  double (*read_entity_bits)(qa_protocol_codec *c, double bits);
  void (*read_entity_update_tail)(qa_protocol_codec *c, double bits, qa_q1_entity_update_tail *out);
  void (*read_baseline)(qa_protocol_codec *c, qa_q1_wire_entity_state *b, double version, double flags);
  double (*read_clientdata_bits)(qa_protocol_codec *c);
  void (*read_clientdata_tail)(qa_protocol_codec *c, double bits, qa_q1_clientdata_tail *out);
  void (*read_sound_header)(qa_protocol_codec *c, double field_mask, qa_q1_sound_header *out);
  double (*read_static_sound_index)(qa_protocol_codec *c, double version);
  void (*write_delta_usercmd)(qa_protocol_codec *c, qa_size_buf *sb, const qa_qw_usercmd *from, const qa_qw_usercmd *cmd);
  void (*read_delta_usercmd)(qa_protocol_codec *c, const qa_qw_usercmd *from, qa_qw_usercmd *move);
  bool (*write_delta_entity)(qa_protocol_codec *c, qa_size_buf *sb, const qa_qw_entity_state *from, const qa_qw_entity_state *to, bool force, double flags);
  void (*write_remove_entity)(qa_protocol_codec *c, qa_size_buf *sb, double entnum);
  void (*write_packet_entities_end)(qa_protocol_codec *c, qa_size_buf *sb);
  void (*read_delta_entity_header)(qa_protocol_codec *c, double word, qa_qw_entity_word *out);
  void (*read_delta_entity)(qa_protocol_codec *c, const qa_qw_entity_state *from, qa_qw_entity_state *to, const qa_qw_entity_word *hdr, double flags);
  void (*write_qw_baseline)(qa_protocol_codec *c, qa_size_buf *sb, const qa_qw_entity_state *es, double flags);
  void (*read_qw_baseline)(qa_protocol_codec *c, qa_qw_entity_state *es, double flags);
  void (*write_model_index)(qa_protocol_codec *c, qa_size_buf *sb, double n);
  double (*read_model_index)(qa_protocol_codec *c);
  void (*write_sound_index)(qa_protocol_codec *c, qa_size_buf *sb, double n);
  double (*read_sound_index)(qa_protocol_codec *c);
  void (*write_precache_count)(qa_protocol_codec *c, qa_size_buf *sb, double n);
  double (*read_precache_count)(qa_protocol_codec *c);
};
typedef qa_protocol_codec qa_qw_protocol_codec;
#endif
