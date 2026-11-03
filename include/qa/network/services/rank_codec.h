/* Port of src/network/services/rank-codec.ts (encoders only, as TS). debugPrint -> fn+ctx.
 * Destinations are caller buffers with explicit capacities; writes past
 * capacity abort like the TS DataView checked writes. GPL-2.0-or-later. */
#ifndef QA_NETWORK_SERVICES_RANK_CODEC_H
#define QA_NETWORK_SERVICES_RANK_CODEC_H
#include <stddef.h>
#include <stdint.h>
typedef void (*qa_rank_print_fn)(void *ctx, const char *text);
size_t qa_rank_ascii_encode(uint8_t *dest, size_t dest_len, const uint8_t *source, size_t source_len);
void qa_rank_encode_game_id(uint64_t game_id, uint8_t *dest, size_t dest_len, qa_rank_print_fn print, void *ctx);
#endif
