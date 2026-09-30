#ifndef QA_Q3_SERVER_PRIVATE_H
#define QA_Q3_SERVER_PRIVATE_H
#include "peer_internal.h"

typedef struct queued_message {
    struct queued_message *next;
    size_t size;
    char key_command[QA_Q3_COMMAND_CHARS];
    uint8_t data[QA_Q3_MESSAGE_BYTES];
} queued_message;
struct qa_q3_server_peer {
    qa_q3_identity identity;
    qa_q3_product product;
    qa_net_address remote;
    int32_t challenge;
    qa_q3_channel *channel;
    qa_q3_server_hooks hooks;
    qa_q3_server_state state;
    qa_q3_reliable reliable;
    char last_command[QA_Q3_COMMAND_CHARS];
    qa_q3_gamestate gamestate;
    qa_q3_snapshot_slot history[QA_Q3_PACKET_BACKUP];
    uint64_t entity_number;
    queued_message *queue_first, *queue_last;
    unsigned queue_count;
};
#endif
