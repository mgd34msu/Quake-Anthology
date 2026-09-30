#ifndef QA_NETWORK_Q3_H
#define QA_NETWORK_Q3_H
#include "qa/network.h"

#define QA_Q3_PROTOCOL 68
#define QA_Q3_MESSAGE_BYTES 16384
#define QA_Q3_FRAGMENT_BYTES 1300
#define QA_Q3_ENTITIES 1024
#define QA_Q3_ENTITY_WORLD 1022
#define QA_Q3_ENTITY_NONE 1023
#define QA_Q3_CONFIGSTRINGS 1024
#define QA_Q3_GAMESTATE_CHARS 16000
#define QA_Q3_RELIABLE 64
#define QA_Q3_COMMAND_CHARS 1024
#define QA_Q3_PACKET_BACKUP 32
#define QA_Q3_PARSE_ENTITIES 2048
#define QA_Q3_USERCMDS 32
#define QA_Q3_DOWNLOAD_BYTES 2048
#define QA_Q3_DOWNLOAD_WINDOW 8

typedef enum qa_q3_product { QA_Q3_ARENA, QA_Q3_TEAM_ARENA } qa_q3_product;
typedef struct qa_q3_trajectory {
    int32_t type, time, duration;
    float base[3], delta[3];
} qa_q3_trajectory;
typedef struct qa_q3_entity {
    int32_t number, eType, eFlags;
    qa_q3_trajectory pos, apos;
    int32_t time, time2;
    float origin[3], origin2[3], angles[3], angles2[3];
    int32_t otherEntityNum, otherEntityNum2, groundEntityNum, constantLight;
    int32_t loopSound, modelindex, modelindex2, clientNum, frame, solid;
    int32_t event, eventParm, powerups, weapon, legsAnim, torsoAnim, generic1;
} qa_q3_entity;
typedef struct qa_q3_player {
    qa_q3_product product;
    int32_t commandTime, pmType, bobCycle, pmFlags, pmTime;
    float origin[3], velocity[3];
    int32_t weaponTime, gravity, speed, deltaAngles[3], groundEntityNum;
    int32_t legsTimer, legsAnim, torsoTimer, torsoAnim, movementDir;
    float grapplePoint[3];
    int32_t eFlags, eventSequence, events[2], eventParms[2];
    int32_t externalEvent, externalEventParm, externalEventTime, clientNum;
    int32_t weapon, weaponState;
    float viewangles[3];
    int32_t viewheight, damageEvent, damageYaw, damagePitch, damageCount;
    int32_t stats[16], persistant[16], powerups[16], ammo[16];
    int32_t generic1, loopSound, jumppadEnt, ping, pmoveFramecount;
    int32_t jumppadFrame, entityEventSequence;
} qa_q3_player;
typedef struct qa_q3_usercmd {
    int32_t serverTime, angles[3];
    int8_t forwardmove, rightmove, upmove;
    int32_t buttons;
    uint8_t weapon;
} qa_q3_usercmd;

/* Static protocol Huffman and adaptive connect compression have separate state.
 * Cursors own no storage. Every failing operation sets a sticky error. */
typedef struct qa_q3_reader { qa_net_reader raw; bool oob; } qa_q3_reader;
typedef struct qa_q3_writer { qa_net_writer raw; bool oob; size_t size; } qa_q3_writer;
void qa_q3_reader_init(qa_q3_reader *, qa_bytes, bool oob, qa_error *);
void qa_q3_writer_init(qa_q3_writer *, void *, size_t, bool oob, qa_error *);
uint32_t qa_q3_read_bits(qa_q3_reader *, int bits);
bool qa_q3_write_bits(qa_q3_writer *, uint32_t, int bits);
bool qa_q3_read_string(qa_q3_reader *, char *, size_t, bool big);
bool qa_q3_write_string(qa_q3_writer *, const char *, bool big);
bool qa_q3_read_data(qa_q3_reader *, void *, size_t);
bool qa_q3_write_data(qa_q3_writer *, qa_bytes);
size_t qa_q3_writer_size(const qa_q3_writer *);
bool qa_q3_huffman_compress(qa_bytes, qa_buffer *, qa_error *);
bool qa_q3_huffman_decompress(qa_bytes, size_t maximum, qa_buffer *, qa_error *);
bool qa_q3_write_entity(qa_q3_writer *, const qa_q3_entity *from,
                         const qa_q3_entity *to, bool force);
bool qa_q3_read_entity(qa_q3_reader *, const qa_q3_entity *from,
                        int32_t number, qa_q3_entity *);
bool qa_q3_write_player(qa_q3_writer *, const qa_q3_player *from, const qa_q3_player *to);
bool qa_q3_read_player(qa_q3_reader *, const qa_q3_player *from, qa_q3_product, qa_q3_player *);
bool qa_q3_write_usercmd(qa_q3_writer *, const qa_q3_usercmd *, const qa_q3_usercmd *, const uint32_t *key);
bool qa_q3_read_usercmd(qa_q3_reader *, const qa_q3_usercmd *, const uint32_t *key, qa_q3_usercmd *);
uint32_t qa_q3_command_hash(const char *, size_t);

typedef enum qa_q3_role { QA_Q3_CLIENT, QA_Q3_SERVER } qa_q3_role;
typedef struct qa_q3_channel qa_q3_channel;
typedef enum qa_q3_receive_kind { QA_Q3_PACKET_STALE, QA_Q3_PACKET_FRAGMENT, QA_Q3_PACKET_MESSAGE } qa_q3_receive_kind;
typedef struct qa_q3_packet {
    qa_q3_receive_kind kind;
    uint32_t sequence, dropped;
    uint16_t qport;
    qa_bytes payload;
} qa_q3_packet;
bool qa_q3_channel_create(qa_q3_role, uint16_t qport, qa_q3_channel **, qa_error *);
void qa_q3_channel_destroy(qa_q3_channel *);
bool qa_q3_channel_create_source_zero(qa_q3_role, uint16_t qport, qa_q3_channel **, qa_error *);
size_t qa_q3_channel_remaining(const qa_q3_channel *);
uint32_t qa_q3_channel_outgoing(const qa_q3_channel *);
uint32_t qa_q3_channel_incoming(const qa_q3_channel *);
bool qa_q3_channel_pending(const qa_q3_channel *);
/* Payload is copied by begin. next returns a borrowed datagram until next call. */
bool qa_q3_channel_begin(qa_q3_channel *, qa_bytes, qa_error *);
bool qa_q3_channel_next(qa_q3_channel *, bool *present, qa_bytes *, qa_error *);
bool qa_q3_channel_receive(qa_q3_channel *, qa_bytes, qa_q3_packet *, qa_error *);
typedef const char *(*qa_q3_command_lookup)(void *, int32_t sequence);
bool qa_q3_xor_client(uint8_t *, size_t, int32_t challenge, qa_q3_command_lookup, void *, qa_error *);
void qa_q3_xor_server(uint8_t *, size_t, int32_t challenge, uint32_t sequence, const char *command);

typedef struct qa_q3_command { int32_t sequence; char text[QA_Q3_COMMAND_CHARS]; } qa_q3_command;
typedef struct qa_q3_reliable {
    int32_t sequence, acknowledged;
    char text[QA_Q3_RELIABLE][QA_Q3_COMMAND_CHARS];
} qa_q3_reliable;
void qa_q3_reliable_init(qa_q3_reliable *);
bool qa_q3_reliable_add(qa_q3_reliable *, qa_q3_role, const char *, qa_error *);
const char *qa_q3_reliable_lookup(const qa_q3_reliable *, int32_t);
bool qa_q3_reliable_ack(qa_q3_reliable *, qa_q3_role, int32_t, bool *clamped, qa_error *);

typedef struct qa_q3_gamestate {
    int32_t command_sequence, client_number, checksum_feed;
    uint16_t config_offsets[QA_Q3_CONFIGSTRINGS];
    char strings[QA_Q3_GAMESTATE_CHARS];
    size_t string_bytes;
    bool baseline_present[QA_Q3_ENTITIES];
    qa_q3_entity baselines[QA_Q3_ENTITIES];
} qa_q3_gamestate;
void qa_q3_gamestate_init(qa_q3_gamestate *);
const char *qa_q3_configstring(const qa_q3_gamestate *, unsigned index);
bool qa_q3_configstring_set(qa_q3_gamestate *, unsigned, const char *, qa_error *);
typedef struct qa_q3_snapshot {
    bool valid;
    int32_t message_number, server_time, delta_number, server_command_number;
    uint64_t parse_entities_number;
    uint8_t flags, area_bytes, area_mask[32];
    qa_q3_player player;
    size_t entity_count;
    const qa_q3_entity *entities;
} qa_q3_snapshot;
typedef enum qa_q3_snapshot_validity {
    QA_Q3_SNAPSHOT_VALID, QA_Q3_SNAPSHOT_MISSING_DELTA,
    QA_Q3_SNAPSHOT_INVALID_DELTA, QA_Q3_SNAPSHOT_STALE_DELTA,
    QA_Q3_SNAPSHOT_STALE_ENTITIES
} qa_q3_snapshot_validity;
typedef struct qa_q3_download {
    uint16_t block;
    int32_t file_size;
    size_t size;
    uint8_t data[QA_Q3_MESSAGE_BYTES];
    char error[QA_Q3_COMMAND_CHARS];
} qa_q3_download;

typedef const qa_q3_snapshot *(*qa_q3_snapshot_lookup)(void *, int32_t);
typedef struct qa_q3_server_decode {
    qa_q3_product product;
    int32_t message_number, reliable_sequence, server_command_sequence;
    uint64_t parse_entities_number;
    qa_q3_gamestate *gamestate;
    qa_q3_snapshot_lookup history;
    void *history_context;
    qa_q3_entity *entity_scratch;
    size_t entity_scratch_capacity;
    bool (*download_size)(void *, int32_t wire_size, int32_t *effective_size, qa_error *);
    void *download_context;
} qa_q3_server_decode;
typedef enum qa_q3_server_event_kind {
    QA_Q3_EVENT_ACK, QA_Q3_EVENT_GAMESTATE_START, QA_Q3_EVENT_GAMESTATE, QA_Q3_EVENT_COMMAND,
    QA_Q3_EVENT_SNAPSHOT, QA_Q3_EVENT_DOWNLOAD
} qa_q3_server_event_kind;
typedef struct qa_q3_server_event {
    qa_q3_server_event_kind kind;
    union {
        int32_t acknowledge;
        const qa_q3_gamestate *gamestate;
        const qa_q3_command *command;
        struct { const qa_q3_snapshot *value; qa_q3_snapshot_validity validity; } snapshot;
        const qa_q3_download *download;
    } value;
} qa_q3_server_event;
/* Events borrow decoder scratch storage, valid only during the callback. */
typedef bool (*qa_q3_server_event_fn)(void *, const qa_q3_server_event *, qa_error *);
bool qa_q3_decode_server(qa_bytes, qa_q3_server_decode *, qa_q3_server_event_fn, void *, qa_error *);
bool qa_q3_server_begin(qa_q3_writer *, int32_t reliable_acknowledge);
bool qa_q3_server_command(qa_q3_writer *, int32_t sequence, const char *);
bool qa_q3_server_gamestate(qa_q3_writer *, const qa_q3_gamestate *);
bool qa_q3_server_snapshot(qa_q3_writer *, const qa_q3_snapshot *from,
                           const qa_q3_snapshot *to, const qa_q3_gamestate *);
bool qa_q3_server_download(qa_q3_writer *, const qa_q3_download *);
bool qa_q3_server_end(qa_q3_writer *);

typedef struct qa_q3_client_header { int32_t server_id, message_acknowledge, reliable_acknowledge; } qa_q3_client_header;
typedef struct qa_q3_client_message {
    qa_q3_client_header header;
    size_t command_count;
    qa_q3_command commands[QA_Q3_RELIABLE + 1];
    bool movement, no_delta;
    size_t usercmd_count;
    qa_q3_usercmd usercmds[QA_Q3_USERCMDS];
} qa_q3_client_message;
typedef struct qa_q3_client_cursor {
    qa_q3_reader reader;
    qa_q3_client_header header;
    unsigned phase;
} qa_q3_client_cursor;
typedef enum qa_q3_client_part { QA_Q3_CLIENT_END, QA_Q3_CLIENT_COMMAND, QA_Q3_CLIENT_MOVE, QA_Q3_CLIENT_MOVE_NO_DELTA } qa_q3_client_part;
bool qa_q3_client_cursor_init(qa_q3_client_cursor *, qa_bytes, qa_error *);
bool qa_q3_client_cursor_next(qa_q3_client_cursor *, qa_q3_client_part *, qa_q3_command *);
bool qa_q3_client_cursor_movement(qa_q3_client_cursor *, int32_t checksum_feed, const char *server_command,
                                  qa_q3_usercmd commands[QA_Q3_USERCMDS], size_t *count);
bool qa_q3_client_cursor_end(qa_q3_client_cursor *);
bool qa_q3_encode_client(qa_q3_writer *, const qa_q3_client_message *, int32_t checksum_feed, const char *server_command);


/* Connectionless dispatch leaves query/admin payloads intact for B29 consumers. */
typedef struct qa_q3_tokens { size_t count; bool truncated; uint16_t offsets[1024]; char text[9216]; } qa_q3_tokens;
bool qa_q3_tokenize(const char *, qa_q3_tokens *, qa_error *);
const char *qa_q3_token(const qa_q3_tokens *, size_t);
bool qa_q3_info_value(const char *, const char *, char *, size_t, qa_error *);
/* Remove the first exact-case key. Small strings prepend the new pair; the
 * 8192-byte source variant appends. Lookup remains ASCII-insensitive. */
bool qa_q3_info_set(char *, size_t, const char *, const char *, qa_error *);
bool qa_q3_is_lan(const qa_net_address *);
typedef struct qa_q3_connectionless {
    char line[1024];
    qa_q3_tokens tokens;
    bool compressed;
    size_t payload_size;
    uint8_t payload[QA_Q3_MESSAGE_BYTES];
} qa_q3_connectionless;
bool qa_q3_connectionless_encode(const char *, qa_buffer *, qa_error *);
bool qa_q3_connect_encode(const char *userinfo, qa_buffer *, qa_error *);
bool qa_q3_connectionless_decode(qa_bytes, qa_q3_role receiver, qa_q3_connectionless *, qa_error *);
typedef bool (*qa_q3_send_fn)(void *, const qa_net_address *, qa_bytes, qa_error *);
typedef enum qa_q3_admission_phase { QA_Q3_DISCONNECTED, QA_Q3_CONNECTING, QA_Q3_CHALLENGING, QA_Q3_ADMITTED } qa_q3_admission_phase;
typedef struct qa_q3_client_admission {
    qa_q3_admission_phase phase;
    qa_net_address address;
    uint16_t qport;
    int32_t challenge;
    int64_t connect_time, last_packet_time;
    uint32_t connect_packets;
} qa_q3_client_admission;
typedef enum qa_q3_admission_result {
    QA_Q3_ADMISSION_IGNORED, QA_Q3_ADMISSION_HANDLED,
    QA_Q3_ADMISSION_CONNECTED, QA_Q3_ADMISSION_QUERY, QA_Q3_ADMISSION_SEQUENCED
} qa_q3_admission_result;
void qa_q3_client_admission_begin(qa_q3_client_admission *, const qa_net_address *, uint16_t);
bool qa_q3_client_admission_resend(qa_q3_client_admission *, int64_t now_ms, const char *userinfo,
                                   qa_q3_send_fn, void *, qa_error *);
bool qa_q3_client_admission_receive(qa_q3_client_admission *, const qa_net_address *, qa_bytes,
                                    int64_t now_ms, qa_q3_admission_result *, qa_q3_connectionless *, qa_error *);
typedef enum qa_q3_connection_phase { QA_Q3_FREE, QA_Q3_ZOMBIE, QA_Q3_CONNECTED, QA_Q3_PRIMED, QA_Q3_ACTIVE } qa_q3_connection_phase;
typedef struct qa_q3_admission_slot {
    uint32_t slot;
    qa_q3_connection_phase phase;
    qa_net_address address;
    bool bot;
    uint16_t qport;
    int64_t last_connect_time;
} qa_q3_admission_slot;
typedef struct qa_q3_accepted_connect {
    uint32_t slot;
    qa_net_address address;
    uint16_t qport;
    int32_t challenge;
    char userinfo[1024];
} qa_q3_accepted_connect;
typedef struct qa_q3_challenge {
    bool present, connected;
    qa_net_address address;
    int32_t challenge;
    int64_t time, first_time, ping_time;
} qa_q3_challenge;
typedef struct qa_q3_admission_options {
    uint32_t private_clients;
    const char *private_password;
    int32_t reconnect_limit_seconds, minimum_ping, maximum_ping;
    bool demo_restricted;
    const qa_net_address *authorize_address;
} qa_q3_admission_options;
typedef struct qa_q3_admission_hooks {
    void *context;
    uint32_t (*random)(void *);
    bool (*authorize)(void *, const qa_q3_challenge *, qa_error *);
    qa_q3_send_fn send;
    /* The shared connection authority allocates the client and seat here.
     * A nonempty rejection is sent to the peer. false is a local failure. */
    bool (*admit)(void *, const qa_q3_accepted_connect *, char rejection[1024], qa_error *);
    bool (*drop_bot)(void *, uint32_t, qa_error *);
    bool (*query)(void *, const qa_net_address *, const qa_q3_connectionless *, qa_error *);
} qa_q3_admission_hooks;
typedef struct qa_q3_server_admission qa_q3_server_admission;
bool qa_q3_server_admission_create(const qa_q3_admission_hooks *, qa_q3_server_admission **, qa_error *);
void qa_q3_server_admission_destroy(qa_q3_server_admission *);
bool qa_q3_server_admission_receive(qa_q3_server_admission *, const qa_q3_admission_options *,
                                    const qa_q3_admission_slot *, size_t slot_count,
                                    const qa_net_address *, qa_bytes, int64_t now_ms, qa_error *);
void qa_q3_server_admission_disconnect(qa_q3_server_admission *, const qa_net_address *);
const qa_q3_admission_slot *qa_q3_route(const qa_net_address *, qa_bytes,
                                      const qa_q3_admission_slot *, size_t);
bool qa_q3_authorize_server_packet(const qa_q3_challenge *, const char *game_directory,
                                    const char *strict_auth, qa_buffer *, qa_error *);
bool qa_q3_authorize_client_packet(const char *key, bool demo, int32_t anonymous,
                                    qa_buffer *, qa_error *);

typedef struct qa_q3_pure_server {
    bool enabled, has_cgame, has_ui;
    int32_t checksum_feed, checksum_feed_server_id, cgame_checksum, ui_checksum;
    const uint32_t *loaded;
    size_t loaded_count;
} qa_q3_pure_server;
typedef enum qa_q3_pure_result { QA_Q3_PURE_DISABLED, QA_Q3_PURE_OUTDATED, QA_Q3_PURE_AUTHENTIC, QA_Q3_PURE_REJECTED } qa_q3_pure_result;
bool qa_q3_verify_pure(const qa_q3_pure_server *, const qa_q3_tokens *, qa_q3_pure_result *, qa_error *);
/* CRC list order is the ZIP central directory order; omit zero-length files. */
bool qa_q3_package_checksums(const uint32_t *crc, const uint64_t *sizes, size_t count,
                              uint32_t feed, uint32_t *checksum, uint32_t *pure_checksum, qa_error *);
bool qa_q3_download_name(const char *, qa_error *);
/* 0 custom, 1 baseq3, 2 missionpack. */
unsigned qa_q3_stock_package(const char *);
typedef struct qa_q3_package { const char *name; uint32_t checksum; } qa_q3_package;
bool qa_q3_compare_packages(const qa_q3_package *, size_t, const uint32_t *, size_t,
                             bool (*exists)(void *, const char *), void *, bool download,
                             char *, size_t, qa_error *);


/* Retained packet state belongs to one already-admitted shared connection.
 * Callbacks may enqueue/send responses but must not destroy peers or recurse
 * into packet reception. Peers borrow callback contexts and own neither the
 * shared connection nor transport. */
typedef struct qa_q3_identity { qa_net_client_id client; bool has_seat; qa_net_seat_id seat; } qa_q3_identity;
typedef struct qa_q3_server_world {
    uint64_t generation;
    int32_t server_id, restarted_server_id, checksum_feed, time;
    bool pure, client_running, flood_protect, downloading;
} qa_q3_server_world;
typedef struct qa_q3_server_hooks {
    void *context;
    qa_q3_server_world (*world)(void *);
    bool (*command)(void *, const qa_q3_command *, bool client_ok, qa_error *);
    bool (*enter_world)(void *, const qa_q3_usercmd *, qa_error *);
    bool (*think)(void *, const qa_q3_usercmd *, qa_error *);
    bool (*resend_gamestate)(void *, qa_error *);
    bool (*pure_rejected_snapshot)(void *, qa_error *);
    bool (*drop)(void *, const char *, qa_error *);
    qa_q3_send_fn send;
} qa_q3_server_hooks;
typedef struct qa_q3_server_rate {
    uint32_t bytes_per_second, maximum_rate, snapshot_ms;
    bool local, lan, force_lan;
} qa_q3_server_rate;
typedef struct qa_q3_server_state {
    qa_q3_connection_phase phase;
    int32_t last_client_command, message_acknowledge, delta_message, gamestate_message_number;
    int32_t reliable_sent;
    int64_t next_snapshot_time, next_reliable_time;
    bool pure_authentic, got_pure_command, rate_delayed;
    qa_q3_usercmd last_usercmd;
} qa_q3_server_state;
typedef struct qa_q3_server_peer qa_q3_server_peer;
bool qa_q3_server_peer_create(qa_q3_identity, qa_q3_product, const qa_net_address *,
                               int32_t challenge, uint16_t qport, const qa_q3_server_hooks *,
                               qa_q3_server_peer **, qa_error *);
void qa_q3_server_peer_destroy(qa_q3_server_peer *);
qa_q3_server_state *qa_q3_server_peer_state(qa_q3_server_peer *);
const qa_q3_identity *qa_q3_server_peer_identity(const qa_q3_server_peer *);
bool qa_q3_server_peer_rebind(qa_q3_server_peer *, const qa_net_address *, qa_error *);
bool qa_q3_server_peer_command(qa_q3_server_peer *, const char *, qa_error *);
bool qa_q3_server_peer_receive(qa_q3_server_peer *, qa_bytes, qa_q3_receive_kind *, qa_error *);
bool qa_q3_server_peer_gamestate(qa_q3_server_peer *, const qa_q3_gamestate *, const qa_q3_server_rate *, qa_error *);
bool qa_q3_server_peer_snapshot(qa_q3_server_peer *, const qa_q3_snapshot *, const qa_q3_server_rate *,
                                const qa_q3_download *, qa_error *);
bool qa_q3_server_peer_snapshot_downloads(qa_q3_server_peer *, const qa_q3_snapshot *,
                                          const qa_q3_server_rate *, const qa_q3_download *,
                                          size_t download_count, qa_error *);
bool qa_q3_server_peer_fragment(qa_q3_server_peer *, bool *sent, qa_error *);
bool qa_q3_server_peer_pure(qa_q3_server_peer *, const qa_q3_pure_server *, const qa_q3_tokens *,
                            qa_q3_pure_result *, qa_error *);
bool qa_q3_server_peer_configstring(qa_q3_server_peer *, unsigned, const char *, qa_error *);

typedef struct qa_q3_client_hooks {
    void *context;
    uint64_t (*generation)(void *);
    bool (*clear_active)(void *, qa_error *);
    bool (*download_size)(void *, int32_t wire_size, int32_t *effective_size, qa_error *);
    bool (*gamestate)(void *, const qa_q3_gamestate *, qa_error *);
    bool (*system_info)(void *, const char *, qa_error *);
    bool (*snapshot)(void *, const qa_q3_snapshot *, int32_t ping, qa_error *);
    bool (*download)(void *, const qa_q3_download *, qa_error *);
    bool (*command)(void *, int32_t sequence, const qa_q3_tokens *, qa_error *);
    bool (*map_restart)(void *, qa_error *);
    bool (*disconnect)(void *, const char *, qa_error *);
    bool (*level_shot)(void *, qa_error *);
    bool (*local_server_running)(void *);
    qa_q3_send_fn send;
} qa_q3_client_hooks;
typedef struct qa_q3_client_send {
    int32_t real_time;
    unsigned packet_dup;
    bool no_delta;
} qa_q3_client_send;
typedef struct qa_q3_client_readiness {
    int32_t real_time;
    unsigned maximum_packets;
    bool cinematic, downloading, active, primed, local, lan;
} qa_q3_client_readiness;
typedef struct qa_q3_clock_options {
    bool paused, demo, freeze_demo, timedemo;
    int32_t time_nudge;
    float timescale;
} qa_q3_clock_options;
typedef struct qa_q3_client_clock {
    int32_t time, delta, old_time, old_frame_server_time, snapshot_time;
    uint8_t snapshot_flags;
    bool pending, extrapolated, active, has_snapshot;
    int32_t demo_base_time, demo_frames, demo_start;
} qa_q3_client_clock;
void qa_q3_clock_clear(qa_q3_client_clock *);
void qa_q3_clock_publish(qa_q3_client_clock *, const qa_q3_snapshot *);
bool qa_q3_clock_advance(qa_q3_client_clock *, int32_t real_time, const qa_q3_clock_options *,
                          bool *active, int32_t *server_time, qa_error *);
bool qa_q3_clock_needs_demo_message(const qa_q3_client_clock *);
bool qa_q3_clock_demo_timing(const qa_q3_client_clock *, int32_t real_time, bool *present,
                              int32_t *frames, int32_t *elapsed_ms, qa_error *);
typedef struct qa_q3_client_peer qa_q3_client_peer;
bool qa_q3_client_peer_create(qa_q3_identity, qa_q3_product, const qa_net_address *,
                               int32_t challenge, uint16_t qport, const qa_q3_client_hooks *,
                               qa_q3_client_peer **, qa_error *);
bool qa_q3_client_peer_create_demo(qa_q3_identity, qa_q3_product,
                                    const qa_q3_client_hooks *, qa_q3_client_peer **, qa_error *);
void qa_q3_client_peer_destroy(qa_q3_client_peer *);
const qa_q3_identity *qa_q3_client_peer_identity(const qa_q3_client_peer *);
qa_q3_product qa_q3_client_peer_product(const qa_q3_client_peer *);
/* Returned views remain borrowed until the next receive or history mutation. */
const qa_q3_gamestate *qa_q3_client_peer_gamestate(const qa_q3_client_peer *);
const qa_q3_snapshot *qa_q3_client_peer_snapshot(const qa_q3_client_peer *);
/* Null denotes a missing, invalid or expired source history entry. */
const qa_q3_snapshot *qa_q3_client_peer_snapshot_at(const qa_q3_client_peer *, int32_t);
bool qa_q3_client_peer_command(qa_q3_client_peer *, const char *, qa_error *);
bool qa_q3_client_peer_usercmd(qa_q3_client_peer *, const qa_q3_usercmd *, qa_error *);
uint64_t qa_q3_client_peer_usercmd_number(const qa_q3_client_peer *);
const qa_q3_usercmd *qa_q3_client_peer_usercmd_at(const qa_q3_client_peer *, uint64_t);
int32_t qa_q3_client_peer_server_command_sequence(const qa_q3_client_peer *);
bool qa_q3_client_peer_receive(qa_q3_client_peer *, qa_bytes, int32_t real_time, qa_q3_receive_kind *, qa_error *);
/* Demo records supply plaintext protocol messages without a netchannel. */
bool qa_q3_client_peer_message(qa_q3_client_peer *, int32_t sequence, qa_bytes, int32_t real_time, qa_error *);
bool qa_q3_client_peer_execute(qa_q3_client_peer *, int32_t server_command_sequence, bool demo, qa_error *);
bool qa_q3_client_peer_send(qa_q3_client_peer *, const qa_q3_client_send *, qa_error *);
bool qa_q3_client_peer_ready(const qa_q3_client_peer *, const qa_q3_client_readiness *);
bool qa_q3_client_peer_disconnect(qa_q3_client_peer *, const qa_q3_client_send *, qa_error *);


enum qa_q3_server_entity_flags {
    QA_Q3_SVF_NOCLIENT = 1, QA_Q3_SVF_CLIENTMASK = 2, QA_Q3_SVF_BOT = 8,
    QA_Q3_SVF_BROADCAST = 32, QA_Q3_SVF_PORTAL = 64, QA_Q3_SVF_USE_CURRENT_ORIGIN = 128,
    QA_Q3_SVF_SINGLECLIENT = 256, QA_Q3_SVF_NOSERVERINFO = 512, QA_Q3_SVF_NOTSINGLECLIENT = 2048
};
typedef struct qa_q3_visibility_entity {
    const qa_q3_entity *state;
    bool linked;
    uint32_t flags;
    int32_t single_client, area, area2, last_cluster;
    const int32_t *clusters;
    size_t cluster_count;
} qa_q3_visibility_entity;
typedef struct qa_q3_visibility_world {
    void *context;
    bool (*point)(void *, const float origin[3], int32_t *area, int32_t *cluster, qa_error *);
    /* OR the connected-area bits into the supplied accumulator. */
    bool (*area_bits)(void *, int32_t area, uint8_t accumulator[32], size_t *bytes, qa_error *);
    bool (*areas_connected)(void *, int32_t, int32_t);
    bool (*cluster_visible)(void *, int32_t from, int32_t to);
} qa_q3_visibility_world;
typedef struct qa_q3_visible_entities {
    uint8_t area_mask[32], area_bytes;
    size_t count;
    bool capacity_reached;
    qa_q3_entity entities[256];
} qa_q3_visible_entities;
bool qa_q3_select_snapshot_entities(const qa_q3_player *, const qa_q3_visibility_entity *, size_t,
                                     const qa_q3_visibility_world *, bool dead,
                                     qa_q3_visible_entities *, qa_error *);

#define QA_Q3_SEARCH_PATHS 4096
#define QA_Q3_BIG_INFO_CHARS 8192
enum qa_q3_pak_reference_flags {
    QA_Q3_PAK_GENERAL = 1, QA_Q3_PAK_UI = 2, QA_Q3_PAK_CGAME = 4, QA_Q3_PAK_QAGAME = 8
};
/* Entries and strings are immutable mount identities retained by the caller
 * until the reference tracker is destroyed. Equal checksums are not identities. */
typedef struct qa_q3_pak_entry {
    const char *game, *basename, *archive_path;
    uint32_t checksum, pure_checksum;
} qa_q3_pak_entry;
typedef struct qa_q3_pak_reference { const qa_q3_pak_entry *pack; unsigned flags; } qa_q3_pak_reference;
typedef struct qa_q3_pak_references qa_q3_pak_references;
typedef double (*qa_q3_pak_random_fn)(void *);
bool qa_q3_pak_references_create(const qa_q3_pak_entry *const *, size_t, uint32_t checksum_feed,
                                  qa_q3_pak_random_fn, void *, qa_q3_pak_references **, qa_error *);
void qa_q3_pak_references_destroy(qa_q3_pak_references *);
bool qa_q3_pak_prepend(qa_q3_pak_references *, const qa_q3_pak_entry *, qa_error *);
bool qa_q3_pak_reorder(qa_q3_pak_references *, const qa_q3_pak_entry *const *, size_t, qa_error *);
void qa_q3_pak_retain_loose(qa_q3_pak_references *, const qa_q3_pak_references *previous);
bool qa_q3_pak_record_packed(qa_q3_pak_references *, const qa_q3_pak_entry *, const char *, qa_error *);
bool qa_q3_pak_record_loose(qa_q3_pak_references *, const char *, qa_error *);
bool qa_q3_pak_clear(qa_q3_pak_references *, unsigned flags, qa_error *);
size_t qa_q3_pak_reference_count(const qa_q3_pak_references *);
bool qa_q3_pak_reference_at(const qa_q3_pak_references *, size_t, qa_q3_pak_reference *);
uint32_t qa_q3_pak_checksum_feed(const qa_q3_pak_references *);
typedef enum qa_q3_pak_report_kind {
    QA_Q3_PAK_LOADED_CHECKSUMS, QA_Q3_PAK_LOADED_NAMES, QA_Q3_PAK_LOADED_PURE_CHECKSUMS,
    QA_Q3_PAK_REFERENCED_CHECKSUMS, QA_Q3_PAK_REFERENCED_NAMES,
    QA_Q3_PAK_REFERENCED_PURE_CHECKSUMS, QA_Q3_PAK_GAME_CHECKSUM
} qa_q3_pak_report_kind;
/* Source reports truncate to 8191 bytes, or capacity-1 when the caller asks for
 * a shorter prefix. A successful call always terminates the output. */
bool qa_q3_pak_report(const qa_q3_pak_references *, qa_q3_pak_report_kind, char *, size_t, qa_error *);
bool qa_q3_pure_loose_path(const char *);
bool qa_q3_pak_is_pure(uint32_t, const uint32_t *server_checksums, size_t);
typedef enum qa_q3_pure_path_kind { QA_Q3_PURE_DIRECTORY, QA_Q3_PURE_PACKAGE } qa_q3_pure_path_kind;
typedef struct qa_q3_pure_path { qa_q3_pure_path_kind kind; void *value; uint32_t checksum; } qa_q3_pure_path;
bool qa_q3_reorder_pure_paths(qa_q3_pure_path *, size_t, const uint32_t *, size_t, qa_error *);

/* Checksum and name cells are independent: setting checksums does not reset
 * names. Names survive outside the explicitly cleared prefix. */
typedef struct qa_q3_server_pak_set qa_q3_server_pak_set;
bool qa_q3_server_pak_set_create(qa_q3_server_pak_set **, qa_error *);
void qa_q3_server_pak_set_destroy(qa_q3_server_pak_set *);
bool qa_q3_server_pak_set_checksums(qa_q3_server_pak_set *, const char *, qa_error *);
/* SIZE_MAX selects the current checksum count, as in the donor default. */
bool qa_q3_server_pak_set_names(qa_q3_server_pak_set *, const char *, size_t clear_count, qa_error *);
const uint32_t *qa_q3_server_pak_set_sums(const qa_q3_server_pak_set *, size_t *count);
bool qa_q3_server_pak_set_at(const qa_q3_server_pak_set *, size_t, qa_q3_package *);

#endif
