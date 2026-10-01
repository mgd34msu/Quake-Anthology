#ifndef QA_Q3_CLIENT_PRIVATE_H
#define QA_Q3_CLIENT_PRIVATE_H
#include "peer_internal.h"

typedef struct sent_packet { uint64_t command_number; int32_t server_time, real_time; } sent_packet;
struct qa_q3_client_peer {
    qa_q3_identity identity;
    qa_q3_product product;
    qa_net_address remote;
    int32_t challenge;
    qa_q3_channel *channel;
    qa_q3_client_hooks hooks;
    uint64_t generation;
    qa_q3_reliable reliable;
    char server_commands[QA_Q3_RELIABLE][QA_Q3_COMMAND_CHARS];
    int32_t server_message_sequence, server_command_sequence, last_executed_server_command;
    int32_t server_id, last_packet_sent_time, receive_time;
    bool demo_waiting, disconnected, demo;
    bool disconnect_started;
    uint8_t disconnect_packets;
    uint16_t transmit_size;
    uint8_t transmit_packet[QA_Q3_FRAGMENT_BYTES + 10];
    uint16_t receive_size;
    bool receive_running;
    uint8_t receive_packet[QA_Q3_MESSAGE_BYTES];
    qa_q3_server_cursor receive_cursor;
    qa_q3_gamestate gamestate;
    qa_q3_snapshot_slot history[QA_Q3_PACKET_BACKUP];
    int32_t latest_snapshot;
    bool has_snapshot;
    uint64_t parse_entities_number, command_number;
    qa_q3_entity scratch[QA_Q3_ENTITY_NONE];
    qa_q3_usercmd commands[64];
    sent_packet packets[QA_Q3_PACKET_BACKUP];
    char big_configstring[8192];
};
#endif
