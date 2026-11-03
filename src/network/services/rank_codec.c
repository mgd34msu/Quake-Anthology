/* Port of src/network/services/rank-codec.ts. GPL-2.0-or-later. */
#include "qa/network/services/rank_codec.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <string.h>
static const char *const ENC = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ[]";
static void checked_put(uint8_t *dest, size_t dest_len, size_t at, uint8_t v) {
  if (at >= dest_len) qa_range_error("Offset is outside the bounds of the DataView");
  dest[at] = v;
}
size_t qa_rank_ascii_encode(uint8_t *dest, size_t dest_len, const uint8_t *source, size_t source_len) {
  size_t length = 0;
  for (size_t i = 0; i < source_len; i += 3) {
    unsigned first = source[i];
    unsigned second = i + 1 < source_len ? source[i+1] : 0;
    unsigned third = i + 2 < source_len ? source[i+2] : 0;
    unsigned text[4] = { first >> 2, ((first << 4) | (second >> 4)) & 63, ((second << 2) | (third >> 6)) & 63, third & 63 };
    size_t count = i + 2 < source_len ? 4 : (source_len - i) * 4 / 3 + 1;
    for (size_t k = 0; k < count; k++) checked_put(dest, dest_len, length++, (uint8_t)ENC[text[k]]);
  }
  checked_put(dest, dest_len, length, 0);
  return length;
}
void qa_rank_encode_game_id(uint64_t game_id, uint8_t *dest, size_t dest_len, qa_rank_print_fn print, void *ctx) {
  if (dest_len < 12) {
    print(ctx, "SV_RankEncodeGameID: result buffer too small\n");
    checked_put(dest, dest_len, 0, 0); return;
  }
  uint8_t bytes[8];
  for (int i = 0; i < 8; i++) bytes[i] = (uint8_t)(game_id >> (8 * i));
  (void)qa_rank_ascii_encode(dest, dest_len, bytes, 8);
}
