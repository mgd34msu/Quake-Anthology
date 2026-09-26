#ifndef QA_NETWORK_Q1_QW_H
#define QA_NETWORK_Q1_QW_H

#include "qa/network_q1.h"

enum {
    QA_QW_PF_MSEC = 1, QA_QW_PF_COMMAND = 2,
    QA_QW_PF_VELOCITY1 = 4, QA_QW_PF_VELOCITY2 = 8, QA_QW_PF_VELOCITY3 = 16,
    QA_QW_PF_MODEL = 32, QA_QW_PF_SKIN = 64, QA_QW_PF_EFFECTS = 128,
    QA_QW_PF_WEAPONFRAME = 256, QA_QW_PF_DEAD = 512,
    QA_QW_PF_GIB = 1024, QA_QW_PF_NOGRAV = 2048,
    QA_QW_ENTITY_SOLID = 64, QA_QW_MAX_NAILS = 255,
    QA_QW_DOWNLOAD_BLOCK = 768
};
/* Channel sequences use the 31-bit QuakeWorld sequence space. Entries are
 * strictly ordered by entity number; entity zero is the wire terminator. */
typedef struct qa_qw_frame {
    uint32_t sequence;
    size_t count;
    qa_q1_entity entities[QA_QW_MAX_PACKET_ENTITIES];
} qa_qw_frame;
typedef struct qa_qw_nail { float origin[3], pitch, yaw; } qa_qw_nail;
typedef enum qa_qw_service_kind {
    QA_QW_NOP, QA_QW_DISCONNECT, QA_QW_STAT, QA_QW_SET_VIEW, QA_QW_SOUND,
    QA_QW_PRINT, QA_QW_STUFFTEXT, QA_QW_SET_ANGLE, QA_QW_SERVER_DATA,
    QA_QW_LIGHT_STYLE, QA_QW_FRAGS, QA_QW_STOP_SOUND, QA_QW_DAMAGE,
    QA_QW_STATIC, QA_QW_BASELINE, QA_QW_TEMPORARY_ENTITY, QA_QW_PAUSE,
    QA_QW_CENTER_PRINT, QA_QW_KILLED_MONSTER, QA_QW_FOUND_SECRET,
    QA_QW_STATIC_SOUND, QA_QW_INTERMISSION, QA_QW_FINALE, QA_QW_CD_TRACK,
    QA_QW_SELL_SCREEN, QA_QW_KICK, QA_QW_PING, QA_QW_ENTER_TIME,
    QA_QW_MUZZLE_FLASH, QA_QW_USERINFO, QA_QW_DOWNLOAD, QA_QW_PLAYER,
    QA_QW_NAILS, QA_QW_CHOKE_COUNT, QA_QW_MODEL_LIST, QA_QW_SOUND_LIST,
    QA_QW_PACKET_ENTITIES, QA_QW_INVALID_DELTA, QA_QW_MAX_SPEED,
    QA_QW_ENTITY_GRAVITY, QA_QW_SET_INFO, QA_QW_SERVER_INFO, QA_QW_PACKET_LOSS
} qa_qw_service_kind;
typedef struct qa_qw_service {
    qa_qw_service_kind kind;
    union {
        struct { uint8_t index; int32_t value; } stat;
        uint16_t entity;
        qa_q1_sound sound;
        struct { uint8_t level; const char *value; } text;
        float angles[3];
        qa_qw_serverdata server;
        struct { uint8_t index; const char *value; } light_style;
        struct { uint8_t slot; int16_t value; } score;
        struct { uint8_t slot; float seconds; } enter_time;
        struct { uint8_t slot, percent; } packet_loss;
        struct { uint16_t entity; uint8_t channel; } stop_sound;
        struct { uint8_t armor, blood; float origin[3]; } damage;
        qa_q1_entity baseline;
        qa_q1_temp temporary;
        bool paused;
        struct { float origin[3], angles[3]; } intermission;
        uint8_t byte;
        int8_t kick;
        struct { uint8_t slot; int32_t user_id; const char *value; } userinfo;
        struct { bool missing; uint8_t percent; qa_bytes bytes; } download;
        qa_qw_player player;
        struct { size_t count; qa_qw_nail items[QA_QW_MAX_NAILS]; } nails;
        struct { uint32_t first, next; size_t count; const char *const *names; } list;
        struct { qa_qw_frame frame; bool delta; uint32_t from_sequence; } packet;
        struct { uint32_t sequence; uint8_t requested; } invalid_delta;
        float scalar;
        struct { uint8_t slot; const char *key, *value; } info;
    } data;
} qa_qw_service;

typedef struct qa_qw_decoder qa_qw_decoder;
qa_qw_decoder *qa_qw_decoder_create(qa_net_protocol_id, qa_error *);
void qa_qw_decoder_destroy(qa_qw_decoder *);
bool qa_qw_decoder_reset(qa_qw_decoder *, qa_net_protocol_id, qa_error *);
qa_net_protocol_id qa_qw_decoder_protocol(const qa_qw_decoder *);
uint32_t qa_qw_decoder_player_model(const qa_qw_decoder *);
bool qa_qw_decoder_set_player_model(qa_qw_decoder *, uint32_t, qa_error *);
bool qa_qw_decoder_set_baseline(qa_qw_decoder *, const qa_q1_entity *, qa_error *);
const qa_q1_entity *qa_qw_decoder_baseline(const qa_qw_decoder *, uint32_t);
const qa_qw_frame *qa_qw_decoder_frame(const qa_qw_decoder *, uint32_t);
/* Baseline pointers may move on insertion. Frame pointers last until their
 * history slot is replaced. Reset/restore/destroy invalidates either kind. */
bool qa_qw_decoder_store_frame(qa_qw_decoder *, const qa_qw_frame *, qa_error *);
void qa_qw_decoder_delta_request(qa_qw_decoder *, uint32_t command_sequence,
                                bool has_base, uint32_t base_sequence);
/* Read one service at the current cursor. Strings/downloads borrow packet
 * bytes; list pointer storage lasts until the next service read or reset.
 * Missing delta history produces QA_QW_INVALID_DELTA after consuming its wire
 * payload. A failed cursor must be discarded. Successful earlier services
 * remain applied; no partial failing service is committed to history. */
bool qa_qw_service_read(qa_net_reader *, qa_qw_decoder *, uint32_t sequence,
                        qa_qw_service *);
/* The optional state supplies packet baselines and the requested delta frame.
 * Writers never mutate it; store the transmitted frame explicitly afterwards.
 * Other service kinds do not need state. Invalid-delta is a receive-only event. */
bool qa_qw_service_write(qa_net_writer *, qa_net_protocol_id,
                         const qa_qw_service *, const qa_qw_decoder *state);
/* Payload helpers exclude service opcode and baseline entity number. */
bool qa_qw_read_baseline(qa_net_reader *, qa_net_protocol_id, qa_q1_entity *);
bool qa_qw_write_baseline(qa_net_writer *, qa_net_protocol_id, const qa_q1_entity *);
bool qa_qw_read_player(qa_net_reader *, qa_net_protocol_id, uint32_t player_model,
                       qa_qw_player *);
/* Without PF_MODEL, model is inferred from the receiver's player.mdl precache;
 * the model member is inactive. Other omitted payload members must be zero. */
bool qa_qw_write_player(qa_net_writer *, qa_net_protocol_id, const qa_qw_player *);
/* Versioned native decoder checkpoint, including baselines, frame history,
 * and delta requests. Restore is transactional and publishes only on success. */
bool qa_qw_decoder_save(qa_net_writer *, const qa_qw_decoder *);
bool qa_qw_decoder_restore(qa_net_reader *, qa_qw_decoder *);

#endif
