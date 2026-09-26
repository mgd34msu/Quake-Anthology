#ifndef QA_NETWORK_Q2_KEX_GAME_H
#define QA_NETWORK_Q2_KEX_GAME_H
#include "qa/network_q2.h"

typedef struct qa_q2_kex_damage {
    uint8_t damage;
    bool health,armor,shield;
    float direction[3];
} qa_q2_kex_damage;
typedef struct qa_q2_kex_poi {
    uint16_t key,time,image;
    float position[3];
    uint8_t color,flags;
} qa_q2_kex_poi;
typedef struct qa_q2_kex_help_path {
    bool start;
    float position[3],direction[3];
} qa_q2_kex_help_path;
typedef struct qa_q2_kex_muzzleflash {int16_t entity;uint16_t weapon;} qa_q2_kex_muzzleflash;
typedef struct qa_q2_kex_locprint {
    uint8_t flags;
    char base[2048],args[8][2048];
    size_t arg_count;
} qa_q2_kex_locprint;
typedef struct qa_q2_kex_sound {
    uint8_t flags;
    uint16_t index;
    float volume,attenuation,time_offset;
    uint32_t entity;
    uint8_t channel;
    bool has_position;
    float position[3];
} qa_q2_kex_sound;

/* Payload functions begin immediately after their service opcode. Damage reads
 * consume every wire indicator, retaining the first four as the donor does. */
bool qa_q2_kex_read_damage(qa_net_reader *,qa_q2_kex_damage out[4],size_t *count);
bool qa_q2_kex_write_damage(qa_net_writer *,const qa_q2_kex_damage *,size_t count);
bool qa_q2_kex_read_poi(qa_net_reader *,qa_q2_kex_poi *);
bool qa_q2_kex_write_poi(qa_net_writer *,const qa_q2_kex_poi *);
bool qa_q2_kex_read_help_path(qa_net_reader *,qa_q2_kex_help_path *);
bool qa_q2_kex_write_help_path(qa_net_writer *,const qa_q2_kex_help_path *);
bool qa_q2_kex_read_muzzleflash(qa_net_reader *,qa_q2_kex_muzzleflash *);
bool qa_q2_kex_write_muzzleflash(qa_net_writer *,const qa_q2_kex_muzzleflash *);
bool qa_q2_kex_read_locprint(qa_net_reader *,qa_q2_kex_locprint *);
bool qa_q2_kex_write_locprint(qa_net_writer *,const qa_q2_kex_locprint *);
bool qa_q2_kex_read_sound(qa_q2_codec *,qa_net_reader *,qa_q2_kex_sound *);
bool qa_q2_kex_write_sound(qa_q2_codec *,qa_net_writer *,const qa_q2_kex_sound *);
bool qa_q2_kex_read_achievement(qa_net_reader *,char *,size_t capacity);
bool qa_q2_kex_write_achievement(qa_net_writer *,const char *);
bool qa_q2_kex_read_splitclient(qa_net_reader *,uint8_t *);
bool qa_q2_kex_write_splitclient(qa_net_writer *,uint8_t);

/* Blast records are borrowed only during the callback. max_inflated is an
 * application budget, required for the zlib envelope whose second word is not
 * authoritative in KEX demos. Callbacks may reject a record through error. */
typedef bool (*qa_q2_kex_config_fn)(void *,uint16_t,const char *,qa_error *);
typedef bool (*qa_q2_kex_baseline_fn)(void *,const qa_q2_entity *,qa_error *);
bool qa_q2_kex_read_configblast(qa_net_reader *,size_t max_inflated,qa_q2_kex_config_fn,void *);
bool qa_q2_kex_read_baselineblast(qa_q2_codec *,qa_net_reader *,size_t max_inflated,qa_q2_kex_baseline_fn,void *);

/* Q2repro duplicates are 0..2 (one to three frames), with 5-bit command counts.
 * Body begins after clc_move_batched / clc_move_nodelta. Read consumes padding
 * through the last byte; write pads it with zero. Classic4038 retains upmove
 * and the batch light level; 1038 follows its restricted movement fields. */
#define QA_Q2_REPRO_BATCH_FRAMES 3
#define QA_Q2_BATCH_COMMANDS 31
typedef struct qa_q2_batch_frame {
    size_t count;
    qa_q2_usercmd commands[QA_Q2_BATCH_COMMANDS];
} qa_q2_batch_frame;
typedef struct qa_q2_repro_batch {
    int32_t last_frame;
    size_t frame_count;
    qa_q2_batch_frame frames[QA_Q2_REPRO_BATCH_FRAMES];
} qa_q2_repro_batch;
bool qa_q2_repro_read_batch(qa_q2_codec *,qa_net_reader *,bool nodelta,qa_q2_repro_batch *);
bool qa_q2_repro_write_batch(qa_q2_codec *,qa_net_writer *,bool nodelta,const qa_q2_repro_batch *);
#endif
