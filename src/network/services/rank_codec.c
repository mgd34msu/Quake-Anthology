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
static int decode_char(const char *source, size_t source_len, size_t index) {
  if (index >= source_len) return 0;
  unsigned code = (uint8_t)source[index];
  /* C bytes are always <=255; the TS >255 case cannot occur. */
  if (code >= 128) qa_range_error("Ranking text at %zu has an undefined signed-char index", index);
  const char *p = strchr(ENC, source[index]);
  return p ? (int)(p - ENC) : -1;
}
static size_t decode_ascii(uint8_t *dest, size_t dest_len, const char *source, size_t source_len, int *valid) {
  size_t length = 0; *valid = 1;
  for (size_t i = 0; i < source_len; i += 4) {
    int text[4] = {0,0,0,0};
    for (int c = 0; c < 4; c++) {
      int v = decode_char(source, source_len, i + (size_t)c);
      if (v < 0) { *valid = 0; return length; }
      text[c] = v;
    }
    uint8_t bytes[3] = { (uint8_t)((text[0] << 2) | (text[1] >> 4)), (uint8_t)((text[1] << 4) | (text[2] >> 2)), (uint8_t)((text[2] << 6) | text[3]) };
    size_t count = i + 3 < source_len ? 3 : (source_len - i) * 3 / 4;
    for (size_t k = 0; k < count; k++) checked_put(dest, dest_len, length++, bytes[k]);
  }
  return length;
}
size_t qa_rank_ascii_decode(uint8_t *dest, size_t dest_len, const char *source, size_t source_len) {
  int valid; size_t written = decode_ascii(dest, dest_len, source, source_len, &valid);
  return valid ? written : 0;
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
uint64_t qa_rank_decode_player_id(const char *source, qa_rank_print_fn print, void *ctx) {
  size_t len = strlen(source);
  char msg[96]; snprintf(msg, sizeof msg, "SV_RankDecodePlayerID: string length %zu\n", len);
  print(ctx, msg);
  uint8_t buffer[9]; memset(buffer, 0, sizeof buffer);
  int valid; size_t written = decode_ascii(buffer, sizeof buffer, source, len, &valid);
  (void)valid;
  if (written < 8) qa_range_error("Ranking player ID does not contain eight initialized bytes");
  uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | buffer[i];
  return v;
}
