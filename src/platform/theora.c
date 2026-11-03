/* Port of src/platform/theora.ts. GPL-2.0-or-later. */
#include "qa/platform/theora.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogg/ogg.h>
#include <theora/theoradec.h>

static bool fail(char *error, size_t error_size, const char *format, ...) {
  if (error != NULL && error_size != 0) {
    va_list args;
    va_start(args, format);
    vsnprintf(error, error_size, format, args);
    va_end(args);
  }
  return false;
}

static void native_packet(const qa_platform_ogg_packet *packet, ogg_packet *out) {
  out->packet = packet->length == 0 ? NULL : (unsigned char *)packet->data;
  out->bytes = (long)packet->length;
  out->b_o_s = packet->first ? 1 : 0;
  out->e_o_s = packet->last ? 1 : 0;
  out->granulepos = (ogg_int64_t)packet->granule;
  out->packetno = (ogg_int64_t)packet->index;
}

bool qa_theora_decoder_init(qa_theora_decoder *decoder,
                            const qa_platform_ogg_packet *headers, size_t count,
                            char *error, size_t error_size) {
  memset(decoder, 0, sizeof *decoder);
  th_info info;
  th_comment comments;
  th_setup_info *setup = NULL;
  th_info_init(&info);
  th_comment_init(&comments);
  bool ok = false;
  if (count != 3) {
    fail(error, error_size, "Theora requires three header packets");
    goto done;
  }
  for (size_t i = 0; i < count; i++) {
    ogg_packet packet;
    native_packet(&headers[i], &packet);
    int result = th_decode_headerin(&info, &comments, &setup, &packet);
    if (result <= 0) {
      fail(error, error_size, "Invalid Theora header: %d", result);
      goto done;
    }
  }
  {
    int64_t frame_width = (int64_t)info.frame_width;
    int64_t frame_height = (int64_t)info.frame_height;
    decoder->width = (int64_t)info.pic_width;
    decoder->height = (int64_t)info.pic_height;
    decoder->crop_x = (int64_t)info.pic_x;
    decoder->crop_y = (int64_t)info.pic_y;
    uint32_t numerator = info.fps_numerator;
    uint32_t denominator = info.fps_denominator;
    decoder->pixel_format = (int)info.pixel_fmt;
    if (decoder->width < 1 || decoder->height < 1 ||
        frame_width * frame_height > 0x1000000 ||
        decoder->crop_x + decoder->width > frame_width ||
        decoder->crop_y + decoder->height > frame_height || numerator == 0 ||
        denominator == 0 ||
        !(decoder->pixel_format == 0 || decoder->pixel_format == 2 ||
          decoder->pixel_format == 3)) {
      fail(error, error_size, "Unsupported Theora dimensions, rate or pixel format");
      goto done;
    }
    decoder->frame_milliseconds = (double)denominator * 1000.0 / (double)numerator;
    if (decoder->frame_milliseconds < 1 || decoder->frame_milliseconds > 10000) {
      fail(error, error_size, "Theora frame rate outside supported limits");
      goto done;
    }
  }
  if (setup == NULL) {
    fail(error, error_size, "Theora setup allocation failed");
    goto done;
  }
  decoder->context = th_decode_alloc(&info, setup);
  if (decoder->context == NULL) {
    fail(error, error_size, "Theora decoder allocation failed");
    goto done;
  }
  ok = true;
done:
  if (setup != NULL) th_setup_free(setup);
  th_comment_clear(&comments);
  th_info_clear(&info);
  if (!ok && decoder->context != NULL) {
    th_decode_free((th_dec_ctx *)decoder->context);
    decoder->context = NULL;
  }
  return ok;
}

static uint8_t byte_of(double value) {
  double rounded = qa_js_round(value);
  if (rounded < 0) return 0;
  if (rounded > 255) return 255;
  return (uint8_t)rounded;
}

typedef struct plane_copy {
  uint8_t *bytes;
  int64_t width;
  int64_t height;
  int64_t stride;
} plane_copy;

static bool copy_plane(const th_img_plane *plane, plane_copy *out, char *error,
                       size_t error_size) {
  int64_t width = (int64_t)plane->width;
  int64_t height = (int64_t)plane->height;
  int64_t stride = (int64_t)plane->stride;
  if (plane->data == NULL || width < 1 || height < 1 || stride < width ||
      stride * height > 128 * 1024 * 1024)
    return fail(error, error_size, "Invalid Theora output plane");
  out->bytes = (uint8_t *)malloc((size_t)(stride * height));
  if (out->bytes == NULL) return fail(error, error_size, "out of memory");
  memcpy(out->bytes, plane->data, (size_t)(stride * height));
  out->width = width;
  out->height = height;
  out->stride = stride;
  return true;
}

bool qa_theora_decoder_decode(qa_theora_decoder *decoder,
                              const qa_platform_ogg_packet *packet,
                              qa_theora_picture *out, char *error,
                              size_t error_size) {
  memset(out, 0, sizeof *out);
  if (decoder->context == NULL)
    return fail(error, error_size, "Theora decoder is closed");
  ogg_packet native;
  native_packet(packet, &native);
  ogg_int64_t granule = 0;
  int result =
      th_decode_packetin((th_dec_ctx *)decoder->context, &native, &granule);
  if (result < 0) return fail(error, error_size, "Invalid Theora frame: %d", result);
  th_ycbcr_buffer planes;
  if (th_decode_ycbcr_out((th_dec_ctx *)decoder->context, planes) != 0)
    return fail(error, error_size, "Theora frame planes unavailable");
  plane_copy y_plane = {NULL, 0, 0, 0};
  plane_copy cb = {NULL, 0, 0, 0};
  plane_copy cr = {NULL, 0, 0, 0};
  bool ok = copy_plane(&planes[0], &y_plane, error, error_size) &&
            copy_plane(&planes[1], &cb, error, error_size) &&
            copy_plane(&planes[2], &cr, error, error_size);
  uint8_t *rgba = NULL;
  if (ok) {
    int shift_x = decoder->pixel_format == 3 ? 0 : 1;
    int shift_y = decoder->pixel_format == 0 ? 1 : 0;
    if (decoder->crop_x + decoder->width > y_plane.width ||
        decoder->crop_y + decoder->height > y_plane.height ||
        ((decoder->crop_x + decoder->width - 1) >> shift_x) >= cb.width ||
        ((decoder->crop_y + decoder->height - 1) >> shift_y) >= cb.height ||
        cb.width != cr.width || cb.height != cr.height) {
      ok = fail(error, error_size, "Theora crop exceeds decoded planes");
    } else {
      size_t length = (size_t)(decoder->width * decoder->height * 4);
      rgba = (uint8_t *)malloc(length);
      if (rgba == NULL) ok = fail(error, error_size, "out of memory");
    }
    if (ok) {
      for (int64_t y = 0; y < decoder->height; y++) {
        for (int64_t x = 0; x < decoder->width; x++) {
          int64_t source_x = decoder->crop_x + x;
          int64_t source_y = decoder->crop_y + y;
          double luma =
              (double)y_plane.bytes[source_y * y_plane.stride + source_x];
          double blue = (double)cb.bytes[(source_y >> shift_y) * cb.stride +
                                          (source_x >> shift_x)];
          double red = (double)cr.bytes[(source_y >> shift_y) * cr.stride +
                                         (source_x >> shift_x)];
          double yy = 1.1643835616438356 * (luma - 16);
          double u = blue - 128;
          double v = red - 128;
          size_t offset = (size_t)((y * decoder->width + x) * 4);
          rgba[offset] = byte_of(yy + 1.5960267857142858 * v);
          rgba[offset + 1] =
              byte_of(yy - 0.39176229009491365 * u - 0.8129676472377708 * v);
          rgba[offset + 2] = byte_of(yy + 2.017232142857143 * u);
          rgba[offset + 3] = 255;
        }
      }
    }
  }
  free(y_plane.bytes);
  free(cb.bytes);
  free(cr.bytes);
  if (!ok) {
    free(rgba);
    return false;
  }
  out->width = decoder->width;
  out->height = decoder->height;
  out->rgba = rgba;
  out->rgba_length = (size_t)(decoder->width * decoder->height * 4);
  return true;
}

void qa_theora_decoder_close(qa_theora_decoder *decoder) {
  if (decoder->context != NULL) {
    th_decode_free((th_dec_ctx *)decoder->context);
    decoder->context = NULL;
  }
}

void qa_theora_picture_dispose(qa_theora_picture *picture) {
  free(picture->rgba);
  picture->rgba = NULL;
  picture->rgba_length = 0;
}
