#ifndef QA_NETWORK_Q1_H
#define QA_NETWORK_Q1_H

#include "qa/network.h"

enum {
    QA_Q1_SHORTANGLE = 1u << 1, QA_Q1_FLOATANGLE = 1u << 2,
    QA_Q1_COORD24 = 1u << 3, QA_Q1_FLOATCOORD = 1u << 4,
    QA_Q1_EDICTSCALE = 1u << 5, QA_Q1_INT32COORD = 1u << 7,
    QA_Q1_SUPPORTED_FLAGS = 190,
    QA_Q1_ALPHA_DEFAULT = 0, QA_Q1_SCALE_DEFAULT = 16,
    QA_QW_UPDATE_BACKUP = 64, QA_QW_MAX_PACKET_ENTITIES = 64
};
/* emit borrows its bytes only for the call. A false return means the message
 * was not queued. The owner closes a failed connection before retrying a
 * multi-message command whose earlier emissions may already be queued. */
typedef bool (*qa_q1_emit_fn)(void *, qa_bytes, qa_error *);

typedef struct qa_q1_entity {
    uint32_t number, model, frame, colormap, skin, effects;
    float origin[3], angles[3];
    uint8_t alpha, scale;
    bool step;
    float lerp_finish;
    uint32_t qw_flags;
} qa_q1_entity;
typedef struct qa_q1_clientdata {
    float viewheight, idealpitch, punch[3], velocity[3];
    uint32_t items;
    bool onground, inwater;
    uint32_t weapon_frame, armor, weapon_model;
    int32_t health;
    uint32_t ammo, shells, nails, rockets, cells, weapon;
    uint8_t weapon_alpha;
} qa_q1_clientdata;
typedef struct qa_q1_command {
    float time, angles[3];
    int16_t forward, side, up;
    uint8_t buttons, impulse;
} qa_q1_command;
typedef struct qa_qw_command {
    float angles[3];
    int16_t forward, side, up;
    uint8_t msec, buttons, impulse;
} qa_qw_command;
typedef struct qa_qw_move {
    qa_qw_command oldest, previous, current;
    uint8_t loss;
} qa_qw_move;
typedef struct qa_qw_player {
    uint8_t slot, msec;
    uint16_t flags;
    float origin[3], velocity[3];
    uint32_t frame, model, skin, effects, weapon_frame;
    qa_qw_command command;
} qa_qw_player;
typedef struct qa_qw_movevars {
    float gravity, stop_speed, max_speed, spectator_max_speed;
    float accelerate, air_accelerate, water_accelerate;
    float friction, water_friction, entity_gravity;
} qa_qw_movevars;
typedef struct qa_qw_serverdata {
    qa_net_protocol_id protocol;
    int32_t server_count;
    const char *game_directory, *level;
    uint8_t player_slot, cd_track;
    bool spectator;
    qa_qw_movevars movement;
} qa_qw_serverdata;
typedef struct qa_q1_sound {
    uint32_t entity, channel, index;
    uint8_t volume;
    float attenuation, origin[3];
} qa_q1_sound;
typedef enum qa_q1_temp_kind { QA_Q1_TEMP_POINT, QA_Q1_TEMP_BEAM, QA_Q1_TEMP_COLORS } qa_q1_temp_kind;
typedef struct qa_q1_temp {
    qa_q1_temp_kind kind;
    uint8_t type, count, color_start, color_length;
    uint16_t entity;
    float origin[3], end[3];
} qa_q1_temp;

bool qa_q1_profile(uint32_t version, uint32_t flags, qa_net_protocol_id *, qa_error *);
bool qa_q1_profile_valid(qa_net_protocol_id, qa_error *);
bool qa_q1_is_qw(qa_net_protocol_id);
uint32_t qa_q1_version(qa_net_protocol_id);
void qa_q1_entity_init(qa_q1_entity *);
float qa_q1_read_coord(qa_net_reader *, qa_net_protocol_id);
float qa_q1_read_angle(qa_net_reader *, qa_net_protocol_id);
bool qa_q1_write_coord(qa_net_writer *, qa_net_protocol_id, float);
bool qa_q1_write_angle(qa_net_writer *, qa_net_protocol_id, float);
bool qa_q1_read_protocol(qa_net_reader *, bool quakeworld, qa_net_protocol_id *);
bool qa_q1_write_protocol(qa_net_writer *, qa_net_protocol_id);
bool qa_q1_read_cstring(qa_net_reader *, const char **);
bool qa_q1_token(const char **cursor, bool quakeworld, char *out, size_t capacity,
                  bool *present, qa_error *);
bool qa_q1_read_temp(qa_net_reader *, qa_net_protocol_id, qa_q1_temp *);
bool qa_q1_write_temp(qa_net_writer *, qa_net_protocol_id, const qa_q1_temp *);
bool qa_q1_read_move(qa_net_reader *, qa_net_protocol_id, qa_q1_command *);
bool qa_q1_write_move(qa_net_writer *, qa_net_protocol_id, const qa_q1_command *);
bool qa_qw_read_delta_command(qa_net_reader *, const qa_qw_command *, qa_qw_command *);
bool qa_qw_write_delta_command(qa_net_writer *, const qa_qw_command *, const qa_qw_command *);
uint8_t qa_qw_checksum(qa_bytes, uint32_t sequence);
bool qa_qw_map_checksum2(qa_bytes, uint32_t *, qa_error *);

typedef enum qa_q1_client_op {
    QA_Q1_CLC_NOP = 1, QA_Q1_CLC_DISCONNECT = 2, QA_Q1_CLC_MOVE = 3,
    QA_Q1_CLC_STRING = 4, QA_Q1_CLC_DELTA = 5, QA_Q1_CLC_TELEPORT = 6,
    QA_Q1_CLC_UPLOAD = 7
} qa_q1_client_op;
typedef struct qa_q1_client_message {
    qa_q1_client_op op;
    union {
        qa_q1_command nq_move;
        qa_qw_move qw_move;
        const char *text;
        uint8_t delta;
        float teleport[3];
        struct { qa_bytes bytes; uint8_t percent; } upload;
    } data;
} qa_q1_client_message;
/* Strings and uploaded data borrow packet bytes. A reader remains valid only
 * for the lifetime of its packet. moved enforces one QW move per packet. */
bool qa_q1_client_read(qa_net_reader *, qa_net_protocol_id, uint32_t sequence,
                       bool *moved, qa_q1_client_message *);
bool qa_q1_client_write(qa_net_writer *, qa_net_protocol_id, uint32_t sequence,
                        const qa_q1_client_message *);
typedef bool (*qa_qw_command_fn)(void *, const qa_qw_command *, qa_error *);
bool qa_qw_replay_commands(qa_qw_command *last, const qa_qw_move *, uint32_t dropped,
                           bool paused, qa_qw_command_fn, void *, qa_error *);
bool qa_qw_split_command(const qa_qw_command *, qa_qw_command_fn, void *, qa_error *);

typedef struct qa_qw_history_frame {
    uint32_t sequence;
    double sent_seconds;
    qa_qw_command command;
    bool valid;
} qa_qw_history_frame;
typedef struct qa_qw_history {
    qa_qw_history_frame frames[QA_QW_UPDATE_BACKUP];
    double latency_seconds;
} qa_qw_history;
void qa_qw_history_init(qa_qw_history *);
bool qa_qw_history_record(qa_qw_history *, uint32_t, double, const qa_qw_command *, qa_error *);
const qa_qw_history_frame *qa_qw_history_get(const qa_qw_history *, uint32_t);
void qa_qw_history_acknowledge(qa_qw_history *, uint32_t, double received_seconds);
bool qa_qw_history_bundle(const qa_qw_history *, uint32_t, uint8_t loss, qa_qw_move *, qa_error *);
bool qa_qw_history_replayable(const qa_qw_history *, uint32_t acknowledged, uint32_t outgoing);
double qa_qw_prediction_time(const qa_qw_history *, double realtime, double push_latency_ms);
void qa_qw_prediction_interpolate(const float previous_origin[3], const float previous_velocity[3],
                                   const float next_origin[3], const float next_velocity[3],
                                   double previous_time, double next_time, double target,
                                   float origin[3], float velocity[3]);

typedef struct qa_q1_demo_record { float angles[3]; qa_bytes message; } qa_q1_demo_record;
bool qa_q1_demo_read_header(qa_net_reader *, int32_t *forced_track);
bool qa_q1_demo_write_header(qa_net_writer *, int32_t forced_track);
bool qa_q1_demo_read_record(qa_net_reader *, size_t max_message, qa_q1_demo_record *);
bool qa_q1_demo_write_record(qa_net_writer *, const qa_q1_demo_record *);
typedef enum qa_qw_demo_kind { QA_QW_DEMO_COMMAND, QA_QW_DEMO_PACKET, QA_QW_DEMO_SEQUENCES } qa_qw_demo_kind;
typedef struct qa_qw_demo_record {
    qa_qw_demo_kind kind;
    float seconds;
    union {
        struct { qa_qw_command command; float angles[3]; } input;
        qa_bytes packet;
        struct { uint32_t outgoing, incoming; } sequences;
    } data;
} qa_qw_demo_record;
bool qa_qw_demo_read_record(qa_net_reader *, size_t max_message, qa_qw_demo_record *);
bool qa_qw_demo_write_record(qa_net_writer *, const qa_qw_demo_record *);

#endif
