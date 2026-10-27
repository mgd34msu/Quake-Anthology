/*
 * Port of src/platform/theora.ts: libtheoradec decoding over
 * TypeScript-framed Ogg packets, with the BT.601 limited-range
 * YCbCr->RGBA conversion in double precision exactly as the source.
 * Linked directly instead of through Bun FFI. Constructor/decode
 * throws become false plus an optional message buffer.
 * GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_THEORA_H
#define QA_PLATFORM_THEORA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Mirror of OggPacket from src/media/ogg.ts (TS owner; no C home
 * yet). Named with the platform prefix to stay collision-proof
 * until the media port lands.
 */
typedef struct qa_platform_ogg_packet {
  const uint8_t *data;
  size_t length;
  bool first;
  bool last;
  int64_t granule;
  int64_t index;
} qa_platform_ogg_packet;

typedef struct qa_theora_picture {
  int64_t width;
  int64_t height;
  uint8_t *rgba; /* owned, width * height * 4 bytes */
  size_t rgba_length;
} qa_theora_picture;

typedef struct qa_theora_decoder {
  void *context; /* th_dec_ctx*; NULL when closed */
  int64_t width;
  int64_t height;
  double frame_milliseconds;
  int64_t crop_x;
  int64_t crop_y;
  int pixel_format;
} qa_theora_decoder;

/* headers must be the three Theora header packets. */
bool qa_theora_decoder_init(qa_theora_decoder *decoder,
                            const qa_platform_ogg_packet *headers, size_t count,
                            char *error, size_t error_size);
bool qa_theora_decoder_decode(qa_theora_decoder *decoder,
                              const qa_platform_ogg_packet *packet,
                              qa_theora_picture *out, char *error,
                              size_t error_size);
void qa_theora_decoder_close(qa_theora_decoder *decoder);
void qa_theora_picture_dispose(qa_theora_picture *picture);

#endif
