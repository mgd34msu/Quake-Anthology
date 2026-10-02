#ifndef QA_NETWORK_Q2_H
#define QA_NETWORK_Q2_H
#include "qa/network.h"
#define QA_Q2_MAX_STATS 64
#define QA_Q2_MAX_SEATS 8
#define QA_Q2_MAX_AREABITS 8192

typedef struct qa_q2_pmove {
    int32_t type, origin[3], velocity[3], flags, time, gravity;
    int16_t delta_angles[3];
    float origin_f[3], velocity_f[3], delta_angles_f[3];
    bool float_delta_angles;
    int32_t viewheight;
} qa_q2_pmove;
typedef struct qa_q2_usercmd {
    int32_t server_frame;
    uint8_t msec, buttons, impulse, lightlevel;
    int16_t angles[3];
    float forwardmove, sidemove, upmove;
} qa_q2_usercmd;
typedef struct qa_q2_entity {
    uint32_t number;
    float origin[3], angles[3], old_origin[3];
    uint32_t modelindex, modelindex2, modelindex3, modelindex4;
    uint32_t frame, skinnum, renderfx, solid, sound, event;
    uint64_t effects;
    float alpha, scale, loop_volume, loop_attenuation;
    uint32_t instance_bits, owner, old_frame, morefx;
} qa_q2_entity;
typedef struct qa_q2_player_fog {
    uint8_t color[3], height_start_color[3], height_end_color[3];
    uint16_t density, sky_factor, height_density, height_falloff;
    int32_t height_start_distance, height_end_distance;
} qa_q2_player_fog;
typedef struct qa_q2_player {
    int32_t clientnum;
    bool clientnum_present; /* This decoded delta carried CLIENT_NUMBER, including zero. */
    qa_q2_player_fog fog;
    qa_q2_pmove pmove;
    float viewangles[3], viewoffset[3], kick_angles[3], gunangles[3], gunoffset[3];
    uint32_t gunindex, gunskin, gunframe, gunrate;
    float blend[4], damage_blend[4], fov;
    uint32_t rdflags;
    int16_t stats[QA_Q2_MAX_STATS];
    uint8_t team_id;
} qa_q2_player;
typedef struct qa_q2_serverdata {
    int32_t servercount;
    bool attractloop;
    char gamedir[1024], levelname[2048];
    int32_t clientnum, clientnums[QA_Q2_MAX_SEATS];
    size_t client_count;
    uint32_t server_state, server_fps, wire_flags, protocol_revision;
    bool strafejump_hack, qw_mode, waterjump_hack;
} qa_q2_serverdata;
typedef struct qa_q2_frame_header {
    int32_t serverframe, deltaframe;
    uint8_t suppress_count;
    uint8_t areabits[QA_Q2_MAX_AREABITS];
    size_t areabytes;
} qa_q2_frame_header;
/* Mutable per-connection codec state. It must not be shared by connections. */
typedef struct qa_q2_codec {
    qa_net_protocol_id protocol;
    uint32_t wire_flags, frame_extra;
    bool demo26, frame_player_pending;
    int32_t server_clientnum;
    bool has_server_clientnum;
    size_t split_players;
    uint8_t kex_nonzero_solid[8192];
} qa_q2_codec;
typedef bool (*qa_q2_write_entities_fn)(void *, qa_net_writer *, qa_error *);
bool qa_q2_codec_init(qa_q2_codec *, qa_net_protocol_id, qa_error *);
typedef struct qa_q2_config_layout {
    uint32_t models, sounds, images, lights, items, player_skins;
    uint32_t map_checksum, max_clients, air_accelerate;
    uint32_t max_models, max_sounds, max_images, max_configs;
    bool extended;
    uint32_t max_entities;
} qa_q2_config_layout;
/* Decoded SERVERDATA flags authorize Q2PRO extensions; kind alone does not. */
bool qa_q2_config_layout_read(const qa_q2_codec *, qa_q2_config_layout *, qa_error *);
uint32_t qa_q2_protocol_version(qa_net_protocol_id);
/* Negotiates native connect offers, including Q2PRO minor 1016 -> 1015.
 * codec_init instead requires an exact supported identity. */
bool qa_q2_protocol_from_version(uint32_t, uint32_t minor, qa_net_protocol_id *, qa_error *);
uint8_t qa_q2_service_opcode(qa_q2_codec *, uint8_t);
/* Read begins after svc_serverdata and the wire protocol word. Write includes
 * both. A successful R1Q2 read narrows the selected revision to its offer. */
bool qa_q2_read_serverdata(qa_q2_codec *, qa_net_reader *, qa_q2_serverdata *);
bool qa_q2_write_serverdata(qa_q2_codec *, qa_net_writer *, const qa_q2_serverdata *);
/* Delta readers/writers accept a null previous state as an all-zero base. */
bool qa_q2_read_entity_header(qa_q2_codec *, qa_net_reader *, uint32_t *, uint64_t *);
bool qa_q2_read_entity(qa_q2_codec *, qa_net_reader *, const qa_q2_entity *, uint32_t, uint64_t, qa_q2_entity *);
bool qa_q2_write_entity(qa_q2_codec *, qa_net_writer *, const qa_q2_entity *, const qa_q2_entity *, bool force, bool new_entity);
bool qa_q2_write_entity_remove(qa_q2_codec *, qa_net_writer *, uint32_t);
bool qa_q2_write_entity_end(qa_q2_codec *, qa_net_writer *);
bool qa_q2_write_baseline(qa_q2_codec *, qa_net_writer *, const qa_q2_entity *);
bool qa_q2_read_player(qa_q2_codec *, qa_net_reader *, const qa_q2_player *, qa_q2_player *);
bool qa_q2_write_player(qa_q2_codec *, qa_net_writer *, const qa_q2_player *, const qa_q2_player *);
bool qa_q2_read_frame_header(qa_q2_codec *, qa_net_reader *, qa_q2_frame_header *);
bool qa_q2_write_frame(qa_q2_codec *, qa_net_writer *, const qa_q2_frame_header *, const qa_q2_player *, const qa_q2_player *, qa_q2_write_entities_fn, void *);
bool qa_q2_read_entities_begin(qa_q2_codec *, qa_net_reader *);
bool qa_q2_write_entities_begin(qa_q2_codec *, qa_net_writer *);
bool qa_q2_read_usercmd(qa_q2_codec *, qa_net_reader *, const qa_q2_usercmd *, qa_q2_usercmd *);
bool qa_q2_write_usercmd(qa_q2_codec *, qa_net_writer *, const qa_q2_usercmd *, const qa_q2_usercmd *);
/* CRC byte used by classic clc_move, bounded to the first 60 bytes. */
uint8_t qa_q2_sequence_checksum(qa_bytes, uint32_t sequence);
/* zpacket payload starts after its opcode. Output is owned on success. */
bool qa_q2_zpacket_read(qa_net_reader *, qa_buffer *);
bool qa_q2_zpacket_wrap(qa_bytes, size_t max_output, uint8_t opcode, qa_buffer *, bool *wrapped, qa_error *);

/* Q2 frame transport. The KEX game channel is nested inside the KEX LAN channel. */
typedef struct qa_q2_channel qa_q2_channel;
typedef enum qa_q2_sequence_recording { QA_Q2_SEQUENCE_DEFAULT, QA_Q2_SEQUENCE_ID, QA_Q2_SEQUENCE_Q2PRO } qa_q2_sequence_recording;
typedef struct qa_q2_channel_options {
    qa_net_protocol_id protocol;
    bool server, new_channel, compress;
    uint16_t qport;
    size_t payload_bytes, message_bytes, datagram_bytes;
    qa_q2_sequence_recording sequence_recording;
} qa_q2_channel_options;
typedef enum qa_q2_receive_kind { QA_Q2_MESSAGE, QA_Q2_FRAGMENT, QA_Q2_REJECTED } qa_q2_receive_kind;
typedef enum qa_q2_reject { QA_Q2_REJECT_NONE, QA_Q2_REJECT_SHORT, QA_Q2_REJECT_SEQUENCE, QA_Q2_REJECT_FRAGMENT_ORDER, QA_Q2_REJECT_FRAGMENT_SIZE, QA_Q2_REJECT_QPORT } qa_q2_reject;
typedef struct qa_q2_received {
    qa_q2_receive_kind kind;
    qa_q2_reject rejected;
    uint32_t sequence, acknowledged, dropped;
    size_t received_bytes;
    qa_bytes payload;
} qa_q2_received;
bool qa_q2_channel_create(const qa_q2_channel_options *, qa_q2_channel **, qa_error *);
void qa_q2_channel_destroy(qa_q2_channel *);
bool qa_q2_channel_queue(qa_q2_channel *, qa_bytes, qa_error *);
/* Low-level encoding advances the channel. Production transport owners should
 * use channel_send so KEX reliable acceptance follows transport acceptance. */
bool qa_q2_channel_transmit(qa_q2_channel *, qa_bytes unreliable, uint64_t now_ns, qa_net_writer *, bool *unreliable_included);
/* Sends queued KEX reliable data and unreliable data separately; retains the
 * reliable queue when the transport fails. */
bool qa_q2_channel_send(qa_q2_channel *, qa_net_transport *, const qa_net_address *, qa_bytes, uint64_t now_ns, bool *unreliable_included, qa_error *);
bool qa_q2_channel_receive(qa_q2_channel *, qa_bytes, uint64_t now_ns, qa_q2_received *, qa_error *);
typedef struct qa_q2_channel_status {
    uint32_t incoming, outgoing, acknowledged;
    bool reliable_pending, can_reliable, fragment_pending, acknowledgement_pending;
    uint64_t sent_ns, received_ns;
    size_t capacity, payload_bytes;
} qa_q2_channel_status;
bool qa_q2_channel_get_status(const qa_q2_channel *, qa_q2_channel_status *);
bool qa_q2_channel_pending(const qa_q2_channel *);
bool qa_q2_channel_fragment_pending(const qa_q2_channel *);
bool qa_q2_channel_should_update(const qa_q2_channel *, uint64_t now_ns);
uint32_t qa_q2_channel_incoming(const qa_q2_channel *);
uint32_t qa_q2_channel_outgoing(const qa_q2_channel *);
#define QA_Q2_MAX_OOB_ARGS 64
#define QA_Q2_MAX_OOB_TEXT 16384
typedef struct qa_q2_oob {
    char text[QA_Q2_MAX_OOB_TEXT], tokens[QA_Q2_MAX_OOB_TEXT];
    size_t body_offset, argc;
    const char *command, *argv[QA_Q2_MAX_OOB_ARGS];
} qa_q2_oob;
typedef struct qa_q2_connect_request {
    qa_net_protocol_id protocol;
    uint16_t qport;
    int32_t challenge;
    char userinfo[8193], social_ids[QA_Q2_MAX_SEATS][512];
    size_t social_count, payload_bytes;
    bool new_channel, compression;
} qa_q2_connect_request;
bool qa_q2_oob_read(qa_bytes, bool utf8, qa_q2_oob *, bool *recognized, qa_error *);
bool qa_q2_oob_write(qa_net_writer *, const char *);
bool qa_q2_connect_read(const qa_q2_oob *, qa_q2_connect_request *, qa_error *);
bool qa_q2_connect_write(qa_net_writer *, const qa_q2_connect_request *);
bool qa_q2_kex_seat_userinfo(const char *, unsigned seat, char *, size_t, qa_error *);
bool qa_q2_kex_client_userinfo(const char *, char *, size_t, qa_error *);
typedef struct qa_q2_challenges qa_q2_challenges;
typedef uint32_t (*qa_q2_random_fn)(void *);
bool qa_q2_challenges_create(size_t, qa_q2_random_fn, void *, qa_q2_challenges **, qa_error *);
void qa_q2_challenges_destroy(qa_q2_challenges *);
bool qa_q2_challenge_reply(qa_q2_challenges *, const qa_net_address *, uint64_t now_ns, const qa_net_protocol_id *, size_t, qa_net_writer *);
bool qa_q2_challenge_validate(const qa_q2_challenges *, const qa_net_address *, int32_t);
typedef enum qa_q2_handshake_phase { QA_Q2_CHALLENGING, QA_Q2_CONNECTING, QA_Q2_CONNECTED, QA_Q2_REFUSED } qa_q2_handshake_phase;
typedef struct qa_q2_handshake {
    qa_q2_handshake_phase phase;
    qa_net_address remote;
    qa_net_protocol_id preferences[8];
    size_t preference_count;
    uint16_t qport;
    qa_q2_connect_request request;
    uint64_t last_sent_ns, retry_ns;
    bool sent;
    char download_server[512], refusal[256];
} qa_q2_handshake;
bool qa_q2_handshake_init(qa_q2_handshake *, const qa_net_address *, const qa_net_protocol_id *, size_t, uint16_t qport, const char *userinfo, const char *social_id, size_t payload_bytes, qa_error *);
bool qa_q2_handshake_poll(qa_q2_handshake *, uint64_t now_ns, qa_net_writer *, bool *present);
bool qa_q2_handshake_receive(qa_q2_handshake *, const qa_net_address *, const qa_q2_oob *, bool *accepted, qa_error *);
typedef struct qa_q2_status_player { int32_t score, ping; const char *name; } qa_q2_status_player;
typedef struct qa_q2_status { const char *server_info; const qa_q2_status_player *players; size_t player_count; } qa_q2_status;
typedef bool (*qa_q2_status_player_fn)(void *, int32_t score, int32_t ping, const char *name, qa_error *);
/* Status messages use original byte strings and stop at the packet budget. */
bool qa_q2_status_write(qa_net_writer *, const qa_q2_status *, size_t body_limit);
bool qa_q2_status_read(const qa_q2_oob *, char *server_info, size_t, qa_q2_status_player_fn, void *, bool *recognized, qa_error *);
bool qa_q2_info_write(qa_net_writer *, const char *name, const char *map, uint32_t players, uint32_t maximum, const qa_net_protocol_id *, size_t, uint32_t offered_version, bool *present);
/* Binary master replies must be examined before NUL-terminated OOB parsing. */
bool qa_q2_master_read(qa_bytes, qa_net_address *, size_t capacity, size_t *count, bool *recognized, qa_error *);
#endif
