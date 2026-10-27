/*
 * Port of src/network/q2/codecs/kexdemo.ts (createKexContext).
 * C accommodations: Float32Array fields are float[3]; record lists
 * are malloc'd (free helpers below); the edict solid map is a fixed
 * table keyed by entity number (TS Map<number,boolean>).
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CODECS_KEXDEMO_H
#define QA_NETWORK_Q2_CODECS_KEXDEMO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/codecs/kex_write.h"
#include "qa/network/q2/codecs/q2repro.h"
#include "qa/network/q2/message.h"

#define QA_Q2_KEX_PROTOCOL_DEMOS 2022
#define QA_Q2_KEX_PROTOCOL 2023
#define QA_Q2_KEX_HI_OWNER 4
#define QA_Q2_KEX_HI_OLDFRAME 8
#define QA_Q2_KEX_GUNBIT_GUNRATE (1 << 6)

/* TS KexDamageIndicatorT / KexPoiT / KexHelpPathT / KexMuzzleflash3T. */
typedef struct qa_q2_kex_damage_indicator {
  double damage;
  bool health, armor, shield;
  float direction[3];
} qa_q2_kex_damage_indicator;
typedef struct qa_q2_kex_poi {
  double key, time;
  float pos[3];
  double image, color, flags;
} qa_q2_kex_poi;
typedef struct qa_q2_kex_help_path {
  bool start;
  float pos[3];
  float dir[3];
} qa_q2_kex_help_path;
typedef struct qa_q2_kex_muzzleflash3 {
  double entity, weapon;
} qa_q2_kex_muzzleflash3;

/* TS KexLocprintT (at most 8 args). */
#define QA_Q2_KEX_LOCPRINT_ARGS_MAX 8
typedef struct qa_q2_kex_locprint {
  double flags;
  char base[2048];
  char args[QA_Q2_KEX_LOCPRINT_ARGS_MAX][2048];
  size_t arg_count;
} qa_q2_kex_locprint;

/* TS KexSoundT (has_pos false is TS pos null). */
typedef struct qa_q2_kex_sound {
  double flags, index, volume, attenuation, timeofs, entity, channel;
  bool has_pos;
  float pos[3];
} qa_q2_kex_sound;

/* TS KexConfigstringRecordT list entry (malloc'd list). */
typedef struct qa_q2_kex_configstring_record {
  double index;
  char *value; /* owned */
} qa_q2_kex_configstring_record;

/* TS spawnbaselineblast entry. */
typedef struct qa_q2_kex_baseline_entry {
  double entnum;
  qa_q2_wire_entity state;
} qa_q2_kex_baseline_entry;

/* TS createKexContext(net_message) result. The writer binding is the
 * first member so kex-write slots can read the protocol through it. */
typedef struct qa_q2_kex_context {
  qa_q2_kex_writer_binding writer_binding;
  qa_q2_sizebuf *net_message;
  qa_q2repro_context rerelease;
  qa_q2_protocol_codec codec; /* TS KEX_DEMO_CODEC */
  double protocol;            /* TS kexServerProtocol */
  double split_players;
  bool edict_solid_known[QA_Q2_MAX_EDICTS];
  bool edict_solid[QA_Q2_MAX_EDICTS];
} qa_q2_kex_context;

void qa_q2_kex_context_init(qa_q2_kex_context *ctx, qa_q2_sizebuf *net_message);
/* TS setKexProtocol / splitPlayerCount / isKexDemoProtocol. */
void qa_q2_kex_set_protocol(qa_q2_kex_context *ctx, double protocol);
double qa_q2_kex_split_player_count(const qa_q2_kex_context *ctx);
bool qa_q2_kex_is_demo_protocol(const qa_q2_kex_context *ctx);

/* TS context readers. List readers malloc; NULL/false only on OOM. */
bool qa_q2_kex_read_damage(qa_q2_kex_context *ctx,
                           qa_q2_kex_damage_indicator **out, size_t *out_count);
void qa_q2_kex_read_poi(qa_q2_kex_context *ctx, qa_q2_kex_poi *out);
void qa_q2_kex_read_help_path(qa_q2_kex_context *ctx, qa_q2_kex_help_path *out);
void qa_q2_kex_read_muzzleflash3(qa_q2_kex_context *ctx,
                                 qa_q2_kex_muzzleflash3 *out);
size_t qa_q2_kex_read_achievement(qa_q2_kex_context *ctx, char *out,
                                  size_t out_capacity);
void qa_q2_kex_read_locprint(qa_q2_kex_context *ctx, qa_q2_kex_locprint *out);
double qa_q2_kex_read_splitclient(qa_q2_kex_context *ctx);
void qa_q2_kex_read_sound(qa_q2_kex_context *ctx, qa_q2_kex_sound *out);
bool qa_q2_kex_read_configblast(qa_q2_kex_context *ctx,
                                qa_q2_kex_configstring_record **out,
                                size_t *out_count);
void qa_q2_kex_configblast_free(qa_q2_kex_configstring_record *records,
                                size_t count);
bool qa_q2_kex_read_spawnbaselineblast(qa_q2_kex_context *ctx,
                                       qa_q2_kex_baseline_entry **out,
                                       size_t *out_count);

#endif
