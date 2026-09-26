#ifndef QA_NETWORK_Q2_MVD_H
#define QA_NETWORK_Q2_MVD_H
#include "qa/network_q2.h"

#define QA_Q2_MVD_MESSAGE_BYTES 32768u
#define QA_Q2_MVD_MAGIC UINT32_C(0x3244564d)
typedef struct qa_q2_mvd_profile {
    uint16_t revision, flags;
    qa_net_protocol_id protocol;
    bool rerelease, extended, v2, fog;
    uint16_t max_configstrings, max_clients_index, max_entities;
} qa_q2_mvd_profile;
bool qa_q2_mvd_profile_init(uint16_t revision, uint16_t flags, qa_q2_mvd_profile *, qa_error *);
typedef enum qa_q2_mvd_opcode {
    QA_MVD_BAD, QA_MVD_NOP, QA_MVD_DISCONNECT, QA_MVD_RECONNECT,
    QA_MVD_SERVERDATA, QA_MVD_CONFIGSTRING, QA_MVD_FRAME, QA_MVD_FRAME_NODELTA,
    QA_MVD_UNICAST, QA_MVD_UNICAST_RELIABLE,
    QA_MVD_ALL, QA_MVD_PHS, QA_MVD_PVS,
    QA_MVD_ALL_RELIABLE, QA_MVD_PHS_RELIABLE, QA_MVD_PVS_RELIABLE,
    QA_MVD_SOUND, QA_MVD_PRINT, QA_MVD_STUFFTEXT
} qa_q2_mvd_opcode;
typedef struct qa_q2_mvd_command { qa_q2_mvd_opcode opcode; uint8_t extra; } qa_q2_mvd_command;
bool qa_q2_mvd_read_command(qa_net_reader *, qa_q2_mvd_command *);
bool qa_q2_mvd_write_command(qa_net_writer *, qa_q2_mvd_command);
typedef struct qa_q2_mvd_config { uint16_t index; char *value; } qa_q2_mvd_config;
typedef struct qa_q2_mvd_header {
    qa_q2_mvd_profile profile;
    int32_t servercount;
    int16_t dummy;
    uint16_t max_clients;
    char gamedir[1024];
    qa_q2_mvd_config *configstrings;
    size_t config_count;
} qa_q2_mvd_header;
/* Read starts after SERVERDATA's command byte. The owned result is published
 * only on success. Free it before replacing; write includes the command byte. */
bool qa_q2_mvd_read_header(qa_net_reader *, qa_q2_mvd_command, qa_q2_mvd_header *);
bool qa_q2_mvd_write_header(qa_net_writer *, const qa_q2_mvd_header *);
void qa_q2_mvd_header_free(qa_q2_mvd_header *);
bool qa_q2_mvd_read_player(const qa_q2_mvd_profile *, qa_net_reader *, uint8_t number,
                            const qa_q2_player *, bool *removed, qa_q2_player *);
bool qa_q2_mvd_write_player(const qa_q2_mvd_profile *, qa_net_writer *, uint8_t number,
                             const qa_q2_player *, const qa_q2_player *, bool force);
typedef struct qa_q2_mvd_player { uint8_t number; qa_q2_player state; } qa_q2_mvd_player;
typedef struct qa_q2_mvd_frame {
    uint8_t portal_bits[255], portal_bytes;
    qa_q2_mvd_player *players;
    size_t player_count;
    qa_q2_entity *entities;
    size_t entity_count;
} qa_q2_mvd_frame;
/* Body starts at portal byte count. Use NULL previous for a full gamestate.
 * Decode owns compact, source-number-sorted arrays and commits only on success.
 * It performs the original player-origin/entity-angle projection, except dummy.
 * max_clients and dummy come from the accepted header. Encoder arrays must be
 * sorted with unique source numbers; routed records follow the frame body. */
bool qa_q2_mvd_read_frame(const qa_q2_mvd_profile *, qa_net_reader *,
                           const qa_q2_mvd_frame *previous, uint16_t max_clients, int16_t dummy,
                           qa_q2_mvd_frame *);
bool qa_q2_mvd_write_frame(const qa_q2_mvd_profile *, qa_net_writer *,
                            const qa_q2_mvd_frame *previous, const qa_q2_mvd_frame *);
void qa_q2_mvd_frame_free(qa_q2_mvd_frame *);
typedef enum qa_q2_mvd_recipient { QA_MVD_RECIPIENT_ALL, QA_MVD_RECIPIENT_PLAYER, QA_MVD_RECIPIENT_PVS, QA_MVD_RECIPIENT_PHS } qa_q2_mvd_recipient;
typedef struct qa_q2_mvd_record {
    qa_q2_mvd_command command;
    union {
        struct { uint16_t index; qa_bytes text; } config;
        struct { uint8_t level; qa_bytes text; } print;
        struct { qa_q2_mvd_recipient recipient; uint16_t target; bool reliable; qa_bytes payload; } route;
        struct { uint8_t flags; uint16_t index, entity; uint8_t channel;
                 float volume, attenuation, delay_seconds; bool global; } sound;
        qa_bytes text;
    } data;
} qa_q2_mvd_record;
/* Text and payload spans borrow the input. Frames and gamestates use the
 * specialized functions above; visibility decisions belong to the demo owner. */
bool qa_q2_mvd_read_record(const qa_q2_mvd_profile *, qa_net_reader *,
                            qa_q2_mvd_command, qa_q2_mvd_record *);
bool qa_q2_mvd_write_record(const qa_q2_mvd_profile *, qa_net_writer *, const qa_q2_mvd_record *);

typedef struct qa_q2_mvd_framer qa_q2_mvd_framer;
bool qa_q2_mvd_framer_create(bool read_magic, size_t message_limit, qa_q2_mvd_framer **, qa_error *);
void qa_q2_mvd_framer_destroy(qa_q2_mvd_framer *);
/* Consume at most one message, preserving any unconsumed input for the caller.
 * This permits an exact plain-hello to compressed-GTV transition in one read. */
bool qa_q2_mvd_framer_push(qa_q2_mvd_framer *, qa_bytes, size_t *consumed,
                            bool *present, qa_bytes *message, qa_error *);
bool qa_q2_mvd_framer_identified(const qa_q2_mvd_framer *);
bool qa_q2_mvd_framer_finished(const qa_q2_mvd_framer *);
bool qa_q2_mvd_framer_finish(const qa_q2_mvd_framer *, bool require_terminator, qa_error *);
bool qa_q2_mvd_write_magic(qa_net_writer *);
bool qa_q2_mvd_write_framed(qa_net_writer *, qa_bytes);
bool qa_q2_mvd_write_terminator(qa_net_writer *);

enum { QA_GTV_PROTOCOL = 0xed04, QA_GTV_DEFLATE = 1, QA_GTV_STRINGCMDS = 2 };
typedef enum qa_gtv_server_opcode { QA_GTV_HELLO, QA_GTV_PONG, QA_GTV_START, QA_GTV_STOP, QA_GTV_DATA,
    QA_GTV_ERROR, QA_GTV_BAD_REQUEST, QA_GTV_NO_ACCESS, QA_GTV_DISCONNECT, QA_GTV_RECONNECT } qa_gtv_server_opcode;
typedef enum qa_gtv_request_kind { QA_GTV_REQUEST_HELLO, QA_GTV_REQUEST_PING, QA_GTV_REQUEST_START, QA_GTV_REQUEST_STOP, QA_GTV_REQUEST_COMMAND } qa_gtv_request_kind;
typedef struct qa_gtv_identity { const char *username, *password, *version; } qa_gtv_identity;
typedef struct qa_gtv_request {
    qa_gtv_request_kind kind;
    uint32_t flags;
    uint16_t buffered_packets;
    char username[256], password[256], version[256], command[256];
} qa_gtv_request;
/* Read consumes one unframed message returned by the MVD framer. Write returns
 * an owned packet including its two-byte length; release with qa_buffer_free. */
bool qa_gtv_request_read(qa_bytes, qa_gtv_request *, qa_error *);
bool qa_gtv_request_write(const qa_gtv_request *, qa_buffer *, qa_error *);
typedef enum qa_gtv_event_kind { QA_GTV_EVENT_HELLO, QA_GTV_EVENT_DATA, QA_GTV_EVENT_PONG,
    QA_GTV_EVENT_STARTED, QA_GTV_EVENT_STOPPED, QA_GTV_EVENT_SUSPENDED, QA_GTV_EVENT_RESUMED, QA_GTV_EVENT_CLOSED } qa_gtv_event_kind;
typedef struct qa_gtv_event { qa_gtv_event_kind kind; uint32_t flags; int reason; qa_bytes payload; } qa_gtv_event;
/* Send borrows bytes until return: a TCP queue must copy before returning. */
typedef bool (*qa_gtv_send_fn)(void *, qa_bytes, qa_error *);
typedef bool (*qa_gtv_event_fn)(void *, const qa_gtv_event *, qa_error *);
typedef struct qa_gtv_client qa_gtv_client;
bool qa_gtv_client_create(const qa_gtv_identity *, uint32_t requested_flags,
                            qa_gtv_send_fn, void *, qa_gtv_client **, qa_error *);
void qa_gtv_client_destroy(qa_gtv_client *);
bool qa_gtv_client_start(qa_gtv_client *, qa_error *);
bool qa_gtv_client_request_start(qa_gtv_client *, uint16_t buffered_packets, qa_error *);
bool qa_gtv_client_request_stop(qa_gtv_client *, qa_error *);
bool qa_gtv_client_ping(qa_gtv_client *, qa_error *);
bool qa_gtv_client_command(qa_gtv_client *, const char *, qa_error *);
bool qa_gtv_client_receive(qa_gtv_client *, qa_bytes, qa_gtv_event_fn, void *, qa_error *);
bool qa_gtv_client_closed(const qa_gtv_client *);
void qa_gtv_client_close(qa_gtv_client *);
/* Callbacks borrow payloads and may request start/stop/commands. They must not
 * reenter receive or destroy/close the client until the callback returns. */
typedef struct qa_gtv_server_stream qa_gtv_server_stream;
/* The TCP owner first validates and echoes MVD magic. hello produces a plain
 * framed packet; subsequent messages retain one negotiated zlib dictionary.
 * Each output buffer is owned, and its bytes must be queued in call order. */
bool qa_gtv_server_stream_create(qa_gtv_server_stream **, qa_error *);
void qa_gtv_server_stream_destroy(qa_gtv_server_stream *);
bool qa_gtv_server_hello(qa_gtv_server_stream *, uint32_t negotiated_flags, qa_buffer *, qa_error *);
bool qa_gtv_server_message(qa_gtv_server_stream *, qa_gtv_server_opcode, qa_bytes body, qa_buffer *, qa_error *);

#endif
