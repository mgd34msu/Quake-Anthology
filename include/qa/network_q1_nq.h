#ifndef QA_NETWORK_Q1_NQ_H
#define QA_NETWORK_Q1_NQ_H
#include "qa/network_q1.h"

typedef struct qa_nq_options { bool standard_quake, private_rerelease; } qa_nq_options;
typedef enum qa_nq_svc {
    QA_NQ_NOP=1, QA_NQ_DISCONNECT=2, QA_NQ_STAT=3, QA_NQ_VERSION=4,
    QA_NQ_SETVIEW=5, QA_NQ_SOUND=6, QA_NQ_TIME=7, QA_NQ_PRINT=8,
    QA_NQ_STUFFTEXT=9, QA_NQ_SETANGLE=10, QA_NQ_SERVERINFO=11,
    QA_NQ_LIGHTSTYLE=12, QA_NQ_NAME=13, QA_NQ_FRAGS=14,
    QA_NQ_CLIENTDATA=15, QA_NQ_STOPSOUND=16, QA_NQ_COLORS=17,
    QA_NQ_PARTICLE=18, QA_NQ_DAMAGE=19, QA_NQ_STATIC=20,
    QA_NQ_BASELINE=22, QA_NQ_TEMPENTITY=23, QA_NQ_PAUSE=24,
    QA_NQ_SIGNON=25, QA_NQ_CENTERPRINT=26, QA_NQ_KILLEDMONSTER=27,
    QA_NQ_FOUNDSECRET=28, QA_NQ_STATICSOUND=29, QA_NQ_INTERMISSION=30,
    QA_NQ_FINALE=31, QA_NQ_CDTRACK=32, QA_NQ_SELLSCREEN=33,
    QA_NQ_CUTSCENE=34, QA_NQ_SKYBOX=37, QA_NQ_BOTCHAT=38,
    QA_NQ_SPAWNEDMONSTER=39, QA_NQ_BONUSFLASH=40, QA_NQ_FOG=41,
    QA_NQ_SETVIEWS=45, QA_NQ_PING=46, QA_NQ_SOCIAL=47,
    QA_NQ_PLAYERINFO=48, QA_NQ_RAWPRINT=49, QA_NQ_SERVERVARS=50,
    QA_NQ_SEQUENCE=51, QA_NQ_ACHIEVEMENT=52, QA_NQ_CHAT=53,
    QA_NQ_LEVELCOMPLETED=54, QA_NQ_BACKTOLOBBY=55, QA_NQ_LOCALSOUND=56,
    QA_NQ_PROMPT=57, QA_NQ_ENTITY=128
} qa_nq_svc;
typedef struct qa_nq_serverinfo {
    qa_net_protocol_id protocol;
    uint8_t max_clients, game_type;
    const char *level;
    const char *const *models, *const *sounds;
    size_t model_count, sound_count;
} qa_nq_serverinfo;
typedef struct qa_nq_message {
    qa_nq_svc op;
    union {
        const char *text;
        float seconds;
        uint32_t value;
        struct { uint8_t index; int32_t value; } indexed;
        struct { uint8_t index; const char *text; } indexed_text;
        float angles[3];
        qa_nq_serverinfo serverinfo;
        qa_q1_clientdata clientdata;
        qa_q1_entity entity;
        qa_q1_sound sound;
        struct { uint16_t entity; uint8_t channel; } stop_sound;
        struct { float origin[3], direction[3]; uint8_t count, color; } particle;
        struct { uint8_t armor, blood; float origin[3]; } damage;
        qa_q1_temp temporary;
        struct { uint8_t track, loop; } cd;
        struct { float density, color[3], seconds; } fog;
        struct { uint8_t operation, value; const char *text; } prompt;
    } data;
} qa_nq_message;
typedef struct qa_nq_decoder qa_nq_decoder;
bool qa_nq_decoder_create(qa_net_protocol_id, qa_nq_options, qa_nq_decoder **, qa_error *);
void qa_nq_decoder_destroy(qa_nq_decoder *);
void qa_nq_decoder_reset(qa_nq_decoder *);
qa_net_protocol_id qa_nq_decoder_protocol(const qa_nq_decoder *);
float qa_nq_decoder_time(const qa_nq_decoder *);
const qa_q1_entity *qa_nq_decoder_baseline(const qa_nq_decoder *, uint32_t);
bool qa_nq_decoder_set_baseline(qa_nq_decoder *, const qa_q1_entity *, qa_error *);
bool qa_nq_decoder_set_time(qa_nq_decoder *, float, qa_error *);
/* Strings borrow input. Serverinfo name arrays are decoder scratch, valid
 * until the next read. Each successful read commits one complete service. */
bool qa_nq_read(qa_nq_decoder *, qa_net_reader *, qa_nq_message *);
bool qa_nq_decoder_save(qa_net_writer *, const qa_nq_decoder *);
bool qa_nq_decoder_restore(qa_net_reader *, qa_nq_decoder *);
/* Entity writes use the supplied baseline and absolute server time. Other
 * services ignore baseline. The writer is sticky; discard failed packets. */
bool qa_nq_write(qa_net_writer *, qa_net_protocol_id, qa_nq_options,
                  const qa_nq_message *, const qa_q1_entity *baseline, float server_time);
/* Converts original Source15 services at the foreign wire boundary. Sorted
 * baselines borrow the actual source peer; writer scratch emits one complete
 * service at a time, and no decoder lifetime is retained. */
bool qa_nq_transcode_original(qa_net_reader *, qa_net_writer *, qa_net_protocol_id destination,
    qa_nq_options, const qa_q1_entity *baselines, size_t baseline_count, float source_time,
    qa_q1_emit_fn, void *context);
bool qa_nq_write_entity(qa_net_writer *, qa_net_protocol_id, const qa_q1_entity *,
                         const qa_q1_entity *baseline, float server_time);
bool qa_nq_write_clientdata(qa_net_writer *, qa_net_protocol_id, const qa_q1_clientdata *, bool standard_quake);
/* Original NQ15 damage centers remain binary64 until fixed-coordinate byte
 * conversion, as in the source host's origin + half mins/maxs calculation. */
bool qa_nq_write_damage(qa_net_writer *, uint8_t armor, uint8_t blood, const double origin[3]);
/* One counter per local seat, reset on map change. The first two moves are
 * suppressed as in NetQuake. Demo playback leaves the counter unchanged. */
bool qa_nq_move_send(uint32_t *sent_count, bool demo_playback, qa_net_writer *,
                      qa_net_protocol_id, const qa_q1_command *, bool *present);
#endif
