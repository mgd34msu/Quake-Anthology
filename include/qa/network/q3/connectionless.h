/* Port of src/network/q3/connectionless.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_CONNECTIONLESS_H
#define QA_NETWORK_Q3_CONNECTIONLESS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum qa_q3_connectionless_receiver { QA_Q3_CONNLESS_CLIENT, QA_Q3_CONNLESS_SERVER } qa_q3_connectionless_receiver;
typedef enum qa_q3_connectionless_compression { QA_Q3_COMPRESSION_NONE, QA_Q3_COMPRESSION_ADAPTIVE } qa_q3_connectionless_compression;
typedef enum qa_q3_line_ending { QA_Q3_LINE_NEWLINE, QA_Q3_LINE_NUL, QA_Q3_LINE_END, QA_Q3_LINE_LIMIT } qa_q3_line_ending;
/* TS ConnectionlessPacket. command borrows arguments[0] storage or "" literal. */
typedef struct qa_q3_connectionless_packet {
  const char *command;
  char **arguments; size_t argument_count; /* owned argv[1..] copies + argv storage */
  char *line;
  uint8_t *payload; size_t payload_len;
  qa_q3_connectionless_compression compression;
  qa_q3_line_ending line_ending;
  int64_t read_count;
  char **argv_storage; size_t argv_count; /* internal token storage */
} qa_q3_connectionless_packet;
bool qa_q3_encode_connectionless_text(const char *text, uint8_t **out, size_t *out_len);
bool qa_q3_encode_connect(const char *userinfo, uint8_t **out, size_t *out_len);
bool qa_q3_decode_connectionless(const uint8_t *packet, size_t len, qa_q3_connectionless_receiver receiver,
                                 const char *source, qa_q3_connectionless_packet *out);
void qa_q3_connectionless_packet_destroy(qa_q3_connectionless_packet *p);
#endif
