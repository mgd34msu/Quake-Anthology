#ifndef QA_NETWORK_Q2_MESSAGES_H
#define QA_NETWORK_Q2_MESSAGES_H

#include "qa/network_q2_batch.h"
#include "qa/network_q2_kex_game.h"

typedef struct qa_q2_entity_span { const qa_q2_entity *data; size_t count; } qa_q2_entity_span;
typedef struct qa_q2_frame_player { qa_q2_player player; qa_bytes area_bits; } qa_q2_frame_player;
typedef struct qa_q2_wire_frame {
    bool valid;
    int32_t server_frame, delta_frame;
    uint8_t suppressed_count;
    size_t player_count;
    qa_q2_frame_player players[QA_Q2_MAX_SEATS];
    qa_q2_entity *entities;
    size_t entity_count;
} qa_q2_wire_frame;

/* Clone/free own all area spans and entities. Frames passed to writers may borrow
 * their spans; free only caller-owned clones, never borrowed history frames. */
bool qa_q2_frame_clone(const qa_q2_wire_frame *, qa_q2_wire_frame *, qa_error *);
void qa_q2_frame_free(qa_q2_wire_frame *);
typedef struct qa_q2_frame_history qa_q2_frame_history;
bool qa_q2_frame_history_create(size_t capacity, qa_q2_frame_history **, qa_error *);
void qa_q2_frame_history_destroy(qa_q2_frame_history *);
void qa_q2_frame_history_clear(qa_q2_frame_history *);
const qa_q2_wire_frame *qa_q2_frame_history_get(const qa_q2_frame_history *, int32_t);
const qa_q2_wire_frame *qa_q2_frame_history_latest(const qa_q2_frame_history *);
bool qa_q2_frame_history_accept(qa_q2_frame_history *, const qa_q2_wire_frame *, qa_error *);
/* Baselines and frame entities must be unique and sorted by entity number.
 * Read returns a frame borrowed until that history slot is replaced or cleared. */
bool qa_q2_frame_history_read(qa_q2_frame_history *, qa_q2_codec *, qa_net_reader *,
                              qa_q2_entity_span baselines, const qa_q2_wire_frame **);
bool qa_q2_packet_entities_write(qa_q2_codec *, qa_net_writer *, qa_q2_entity_span old,
                                  qa_q2_entity_span current, qa_q2_entity_span baselines,
                                  uint32_t max_clients);
bool qa_q2_frame_write(qa_q2_codec *, qa_net_writer *, const qa_q2_wire_frame *,
                        const qa_q2_wire_frame *old, qa_q2_entity_span baselines, uint32_t max_clients);

bool qa_q2_read_dir(qa_net_reader *, float[3]);
bool qa_q2_write_dir(qa_net_writer *, const float[3]);
bool qa_q2_game_position_read(qa_q2_codec *, qa_net_reader *, float[3]);
bool qa_q2_game_position_write(qa_q2_codec *, qa_net_writer *, const float[3]);
typedef enum qa_q2_temp_type {
    QA_Q2_TE_GUNSHOT, QA_Q2_TE_BLOOD, QA_Q2_TE_BLASTER, QA_Q2_TE_RAILTRAIL,
    QA_Q2_TE_SHOTGUN, QA_Q2_TE_EXPLOSION1, QA_Q2_TE_EXPLOSION2,
    QA_Q2_TE_ROCKET_EXPLOSION, QA_Q2_TE_GRENADE_EXPLOSION, QA_Q2_TE_SPARKS,
    QA_Q2_TE_SPLASH, QA_Q2_TE_BUBBLETRAIL, QA_Q2_TE_SCREEN_SPARKS,
    QA_Q2_TE_SHIELD_SPARKS, QA_Q2_TE_BULLET_SPARKS, QA_Q2_TE_LASER_SPARKS,
    QA_Q2_TE_PARASITE_ATTACK, QA_Q2_TE_ROCKET_EXPLOSION_WATER,
    QA_Q2_TE_GRENADE_EXPLOSION_WATER, QA_Q2_TE_MEDIC_CABLE_ATTACK,
    QA_Q2_TE_BFG_EXPLOSION, QA_Q2_TE_BFG_BIGEXPLOSION, QA_Q2_TE_BOSSTPORT,
    QA_Q2_TE_BFG_LASER, QA_Q2_TE_GRAPPLE_CABLE, QA_Q2_TE_WELDING_SPARKS,
    QA_Q2_TE_GREENBLOOD, QA_Q2_TE_BLUEHYPERBLASTER, QA_Q2_TE_PLASMA_EXPLOSION,
    QA_Q2_TE_TUNNEL_SPARKS, QA_Q2_TE_BLASTER2, QA_Q2_TE_RAILTRAIL2,
    QA_Q2_TE_FLAME, QA_Q2_TE_LIGHTNING, QA_Q2_TE_DEBUGTRAIL,
    QA_Q2_TE_PLAIN_EXPLOSION, QA_Q2_TE_FLASHLIGHT, QA_Q2_TE_FORCEWALL,
    QA_Q2_TE_HEATBEAM, QA_Q2_TE_MONSTER_HEATBEAM, QA_Q2_TE_STEAM,
    QA_Q2_TE_BUBBLETRAIL2, QA_Q2_TE_MOREBLOOD, QA_Q2_TE_HEATBEAM_SPARKS,
    QA_Q2_TE_HEATBEAM_STEAM, QA_Q2_TE_CHAINFIST_SMOKE, QA_Q2_TE_ELECTRIC_SPARKS,
    QA_Q2_TE_TRACKER_EXPLOSION, QA_Q2_TE_TELEPORT_EFFECT, QA_Q2_TE_DBALL_GOAL,
    QA_Q2_TE_WIDOWBEAMOUT, QA_Q2_TE_NUKEBLAST, QA_Q2_TE_WIDOWSPLASH,
    QA_Q2_TE_EXPLOSION1_BIG, QA_Q2_TE_EXPLOSION1_NP, QA_Q2_TE_FLECHETTE,
    QA_Q2_TE_BLUEHYPERBLASTER_2, QA_Q2_TE_BFG_ZAP, QA_Q2_TE_BERSERK_SLAM,
    QA_Q2_TE_GRAPPLE_CABLE_2, QA_Q2_TE_POWER_SPLASH, QA_Q2_TE_LIGHTNING_BEAM,
    QA_Q2_TE_EXPLOSION1_NL, QA_Q2_TE_EXPLOSION2_NL, QA_Q2_TE_Q2PRO_DAMAGE_DEALT = 128
} qa_q2_temp_type;
typedef enum qa_q2_temp_field_kind { QA_Q2_TEMP_INTEGER, QA_Q2_TEMP_VECTOR } qa_q2_temp_field_kind;
typedef enum qa_q2_temp_field_name {
    QA_Q2_TEMP_ENTITY1, QA_Q2_TEMP_ENTITY2, QA_Q2_TEMP_COUNT, QA_Q2_TEMP_COLOR,
    QA_Q2_TEMP_TIME, QA_Q2_TEMP_POSITION1, QA_Q2_TEMP_POSITION2,
    QA_Q2_TEMP_DIRECTION, QA_Q2_TEMP_OFFSET
} qa_q2_temp_field_name;
typedef struct qa_q2_temp_field {
    qa_q2_temp_field_kind kind;
    qa_q2_temp_field_name name;
    union { int32_t integer; float vector[3]; } value;
} qa_q2_temp_field;
typedef struct qa_q2_temp_entity {
    uint8_t type;
    size_t field_count;
    qa_q2_temp_field fields[7];
    qa_bytes raw;
} qa_q2_temp_entity;
bool qa_q2_temp_entity_read(qa_q2_codec *, qa_net_reader *, bool extended_types, qa_q2_temp_entity *);
bool qa_q2_temp_entity_write(qa_q2_codec *, qa_net_writer *, bool extended_types, const qa_q2_temp_entity *);

typedef struct qa_q2_fog {
    uint16_t bits;
    float density, height_falloff, height_density;
    uint8_t sky_factor, color[3], height_start_color[3], height_end_color[3];
    uint16_t time;
    int32_t height_start_distance, height_end_distance;
} qa_q2_fog;
bool qa_q2_fog_read(qa_net_reader *, qa_q2_fog *);
bool qa_q2_fog_write(qa_net_writer *, const qa_q2_fog *);

typedef enum qa_q2_server_event_kind {
    QA_Q2_SVC_NOP, QA_Q2_SVC_DISCONNECT, QA_Q2_SVC_RECONNECT, QA_Q2_SVC_LEVEL_RESTART,
    QA_Q2_SVC_SERVERDATA, QA_Q2_SVC_PRINT, QA_Q2_SVC_CENTERPRINT, QA_Q2_SVC_COMMAND,
    QA_Q2_SVC_LAYOUT, QA_Q2_SVC_ACHIEVEMENT, QA_Q2_SVC_CONFIGSTRING, QA_Q2_SVC_BASELINE,
    QA_Q2_SVC_FRAME, QA_Q2_SVC_SOUND, QA_Q2_SVC_TEMP_ENTITY, QA_Q2_SVC_MUZZLEFLASH,
    QA_Q2_SVC_INVENTORY, QA_Q2_SVC_DOWNLOAD, QA_Q2_SVC_SETTING, QA_Q2_SVC_SEAT,
    QA_Q2_SVC_DAMAGE, QA_Q2_SVC_LOCALIZED_PRINT, QA_Q2_SVC_FOG, QA_Q2_SVC_POI,
    QA_Q2_SVC_HELP_PATH, QA_Q2_SVC_PRIVATE
} qa_q2_server_event_kind;
typedef struct qa_q2_server_event {
    qa_q2_server_event_kind kind;
    union {
        qa_q2_serverdata serverdata;
        struct { uint8_t level; const char *text; } print;
        struct { uint16_t index; const char *value; } config;
        qa_q2_entity baseline;
        const qa_q2_wire_frame *frame;
        qa_q2_kex_sound sound;
        qa_q2_temp_entity temporary;
        struct { uint32_t entity, flash; bool monster, silenced; } muzzle;
        struct { const int16_t *counts; size_t count; } inventory;
        struct { uint8_t percent; bool missing; qa_bytes bytes; } download;
        struct { int32_t index, value; } setting;
        uint8_t seat;
        struct { qa_q2_kex_damage indicators[4]; size_t count; } damage;
        qa_q2_kex_locprint localized;
        qa_q2_fog fog;
        qa_q2_kex_poi poi;
        qa_q2_kex_help_path help_path;
        struct { const char *name; qa_bytes payload; } private_message;
    } data;
} qa_q2_server_event;
typedef struct qa_q2_server_record {
    uint8_t seat, opcode;
    qa_bytes raw;
    qa_q2_server_event event;
} qa_q2_server_record;
typedef bool (*qa_q2_server_emit_fn)(void *, const qa_q2_server_record *, qa_error *);
typedef bool (*qa_q2_private_read_fn)(void *, uint8_t opcode, qa_q2_codec *, qa_net_reader *, qa_q2_server_event *);
typedef struct qa_q2_message_options {
    size_t config_strings, inventory_slots, history_capacity, max_inflated_bytes;
    bool demo, override_extended_temps, extended_temps;
    uint8_t private_opcodes[32];
    qa_q2_private_read_fn private_read;
    void *private_user;
} qa_q2_message_options;
typedef struct qa_q2_messages qa_q2_messages;
bool qa_q2_messages_create(qa_net_protocol_id, const qa_q2_message_options *, qa_q2_messages **, qa_error *);
void qa_q2_messages_destroy(qa_q2_messages *);
void qa_q2_messages_reset(qa_q2_messages *);
qa_q2_codec *qa_q2_messages_codec(qa_q2_messages *);
const char *qa_q2_messages_config(const qa_q2_messages *, uint16_t);
const qa_q2_entity *qa_q2_messages_baseline(const qa_q2_messages *, uint32_t);
const qa_q2_wire_frame *qa_q2_messages_latest(const qa_q2_messages *, uint8_t seat);
/* Records, raw bytes and event pointers are borrowed during the callback only.
 * Callbacks may query the decoder but must not mutate, reenter or destroy it.
 * Streamed config/baseline and raw-deflate download state persists across reads. */
bool qa_q2_messages_read(qa_q2_messages *, qa_bytes, qa_q2_server_emit_fn, void *, qa_error *);
bool qa_q2_messages_accept(qa_q2_messages *, const qa_q2_server_record *, qa_error *);
bool qa_q2_server_event_write(qa_q2_codec *, qa_net_writer *, const qa_q2_server_event *);

typedef enum qa_q2_client_event_kind {
    QA_Q2_CLC_NOP, QA_Q2_CLC_USERINFO, QA_Q2_CLC_COMMAND, QA_Q2_CLC_USERINFO_DELTA,
    QA_Q2_CLC_SETTING, QA_Q2_CLC_MOVE, QA_Q2_CLC_BATCH
} qa_q2_client_event_kind;
typedef struct qa_q2_client_event {
    qa_q2_client_event_kind kind;
    union {
        const char *text;
        struct { const char *name, *value; } userinfo_delta;
        struct { int16_t index, value; } setting;
        struct { int32_t last_frame; qa_q2_usercmd commands[3]; } move;
        qa_q2_repro_batch batch;
    } data;
} qa_q2_client_event;
typedef struct qa_q2_client_record { uint8_t seat; qa_bytes raw; qa_q2_client_event event; } qa_q2_client_record;
typedef bool (*qa_q2_client_emit_fn)(void *, const qa_q2_client_record *, qa_error *);
bool qa_q2_client_messages_read(qa_q2_codec *, qa_bytes, uint32_t sequence, size_t seats,
                                qa_q2_client_emit_fn, void *, qa_error *);
bool qa_q2_client_event_write(qa_q2_codec *, qa_net_writer *, const qa_q2_client_event *, uint8_t seat);
bool qa_q2_client_move_write(qa_q2_codec *, qa_net_writer *, uint32_t sequence, int32_t last_frame,
                              const qa_q2_usercmd (*commands)[3], size_t seats);
typedef struct qa_q2_command_replay { qa_q2_usercmd previous; int32_t last_frame; } qa_q2_command_replay;
typedef bool (*qa_q2_think_fn)(void *, const qa_q2_usercmd *, qa_error *);
void qa_q2_command_replay_init(qa_q2_command_replay *);
bool qa_q2_command_replay_run(qa_q2_command_replay *, const qa_q2_client_event *, uint32_t dropped,
                              qa_q2_think_fn, void *, qa_error *);
typedef struct qa_q2_rate_window { uint32_t sizes[10], suppressed; } qa_q2_rate_window;
bool qa_q2_rate_drop(qa_q2_rate_window *, uint32_t server_frame, uint32_t bytes_per_second, bool loopback);
void qa_q2_rate_sent(qa_q2_rate_window *, uint32_t server_frame, uint32_t bytes);
uint32_t qa_q2_rate_take_suppressed(qa_q2_rate_window *);

typedef bool (*qa_q2_download_read_fn)(void *, size_t offset, size_t length, qa_buffer *, qa_error *);
typedef void (*qa_q2_download_close_fn)(void *);
typedef struct qa_q2_download_sender {
    void *user;
    qa_q2_download_read_fn read;
    qa_q2_download_close_fn close;
    size_t size, offset, block_bytes;
    bool ended;
} qa_q2_download_sender;
bool qa_q2_download_sender_init(qa_q2_download_sender *, size_t size, size_t offset, size_t block_bytes,
                                qa_q2_download_read_fn, qa_q2_download_close_fn, void *, qa_error *);
bool qa_q2_download_sender_next(qa_q2_download_sender *, qa_buffer *, uint8_t *percent, bool *present, qa_error *);
void qa_q2_download_sender_close(qa_q2_download_sender *);

#endif
