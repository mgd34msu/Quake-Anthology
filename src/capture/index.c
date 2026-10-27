/* Port of src/capture/index.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/capture/index.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/formats/images/jpeg_encoder.h"
#include "qa/formats/images/png_encoder.h"
#include "qa/formats/images/tga.h"
#include "qa/settings/config.h"

/* TS throws plain Error in this module; abort with identical message. */
static noreturn void capture_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "Error: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  abort();
}

static void check_frame(const qa_image_level *image) {
  if (!isfinite(image->width) || !isfinite(image->height) ||
      image->width != trunc(image->width) ||
      image->height != trunc(image->height) || image->width < 1 ||
      image->height < 1 ||
      (double)image->pixels.length != image->width * image->height * 4) {
    capture_error("Renderer returned invalid RGBA frame dimensions");
  }
}

uint8_t *qa_encode_screenshot(const qa_image_level *image,
                              qa_screenshot_format format, int64_t quality,
                              size_t *out_length) {
  check_frame(image);
  switch (format) {
    case QA_SCREENSHOT_TGA:
      return qa_encode_tga(image, out_length);
    case QA_SCREENSHOT_PNG:
      return qa_encode_png(image->width, image->height, image->pixels.bytes,
                           image->pixels.length, out_length);
    case QA_SCREENSHOT_JPG:
      return qa_encode_jpeg(image, quality, NULL, out_length);
  }
  *out_length = 0;
  return NULL;
}

/** R_LevelShot samples a 512x384 grid, averaging twelve samples into each 128x128 texel. */
bool qa_make_levelshot(const qa_image_level *image, const uint8_t *gamma,
                       size_t gamma_length, qa_image_level *out) {
  check_frame(image);
  if (gamma != NULL && gamma_length != 256) {
    capture_error("Gamma table must contain 256 entries");
  }
  uint8_t *pixels = (uint8_t *)malloc(128 * 128 * 4);
  if (pixels == NULL) return false;
  /* The TS views pixels through a DataView at its byteOffset; the C
   * byte view already points at the first visible byte. */
  const uint8_t *input = image->pixels.bytes;
  /* Math.fround staging: compute in double, cast to float. */
  const float x_scale = (float)(image->width / 512);
  const float y_scale = (float)(image->height / 384);
  const int64_t width = (int64_t)image->width;
  const int64_t height = (int64_t)image->height;
  for (int64_t y = 0; y < 128; y++) {
    for (int64_t x = 0; x < 128; x++) {
      int r = 0, g = 0, b = 0;
      for (int64_t yy = 0; yy < 3; yy++) {
        for (int64_t xx = 0; xx < 4; xx++) {
          int64_t sx = (int64_t)trunc((float)((double)(x * 4 + xx) * (double)x_scale));
          int64_t sy = (int64_t)trunc((float)((double)(y * 3 + yy) * (double)y_scale));
          if (sx > width - 1) sx = width - 1;
          if (sy > height - 1) sy = height - 1;
          const size_t offset = (size_t)(sy * width + sx) * 4;
          r += input[offset];
          g += input[offset + 1];
          b += input[offset + 2];
        }
      }
      const size_t offset = (size_t)(y * 128 + x) * 4;
      pixels[offset] = gamma != NULL ? gamma[r / 12] : (uint8_t)(r / 12);
      pixels[offset + 1] = gamma != NULL ? gamma[g / 12] : (uint8_t)(g / 12);
      pixels[offset + 2] = gamma != NULL ? gamma[b / 12] : (uint8_t)(b / 12);
      pixels[offset + 3] = 255;
    }
  }
  out->width = 128;
  out->height = 128;
  out->pixels.bytes = pixels;
  out->pixels.length = 128 * 128 * 4;
  return true;
}

/* --- FrameCapture (synchronous; the TS write chain is call order) --- */

bool qa_frame_capture_init(qa_frame_capture *capture, const char *root,
                           qa_frame_readback readback) {
  capture->root = strdup(root);
  if (capture->root == NULL) return false;
  capture->readback = readback;
  return true;
}

void qa_frame_capture_destroy(qa_frame_capture *capture) {
  if (capture == NULL) return;
  free(capture->root);
  memset(capture, 0, sizeof *capture);
}

void qa_capture_result_free(qa_capture_result *result) {
  if (result == NULL) return;
  free(result->path);
  memset(result, 0, sizeof *result);
}

static const char *format_extension(qa_screenshot_format format) {
  switch (format) {
    case QA_SCREENSHOT_TGA: return "tga";
    case QA_SCREENSHOT_PNG: return "png";
    case QA_SCREENSHOT_JPG: return "jpg";
  }
  return "";
}

/* TS mkdir(dirname, {recursive:true}) for an absolute directory. */
static bool make_directories(char *dir) {
  for (size_t i = 1; dir[i] != '\0'; i++) {
    if (dir[i] != '/') continue;
    dir[i] = '\0';
    bool ok = mkdir(dir, 0777) == 0 || errno == EEXIST;
    dir[i] = '/';
    if (!ok) return false;
  }
  return mkdir(dir, 0777) == 0 || errno == EEXIST;
}

/* settingsPath(root, relative) with a formatted relative name;
 * NULL only on OOM (qa_settings_path aborts on escape, as TS throws). */
static char *capture_path(const qa_frame_capture *capture, const char *relative_format,
                          const char *middle, const char *extension) {
  size_t length = strlen(relative_format) + strlen(middle) + strlen(extension) + 1;
  char *relative = (char *)malloc(length);
  if (relative == NULL) return NULL;
  /* relative_format carries exactly two %s slots. */
  (void)snprintf(relative, length, relative_format, middle, extension);
  char *path = qa_settings_path(capture->root, relative);
  free(relative);
  return path;
}

bool qa_frame_capture_screenshot(qa_frame_capture *capture,
                                 qa_screenshot_format format, const char *name,
                                 int64_t quality, qa_capture_result *out) {
  memset(out, 0, sizeof *out);
  const char *extension = format_extension(format);
  qa_image_level image = capture->readback.read_rgba(capture->readback.ctx);
  size_t byte_length = 0;
  uint8_t *bytes = qa_encode_screenshot(&image, format, quality, &byte_length);
  if (bytes == NULL) return false;
  char *path = NULL;
  if (name != NULL) {
    path = capture_path(capture, "screenshots/%s.%s", name, extension);
    if (path == NULL || !qa_write_atomic(path, bytes, byte_length)) {
      free(path);
      free(bytes);
      return false;
    }
  } else {
    for (int index = 0; index < 10000 && path == NULL; index++) {
      char stem[16];
      (void)snprintf(stem, sizeof stem, "shot%04d", index);
      char *candidate = capture_path(capture, "screenshots/%s.%s", stem, extension);
      if (candidate == NULL) {
        free(bytes);
        return false;
      }
      const char *slash = strrchr(candidate, '/');
      char *dir = strndup(candidate, (size_t)(slash - candidate));
      bool made = dir != NULL && make_directories(dir);
      free(dir);
      if (!made) {
        free(candidate);
        free(bytes);
        return false;
      }
      int fd = open(candidate, O_WRONLY | O_CREAT | O_EXCL, 0666);
      if (fd < 0) {
        int error = errno;
        free(candidate);
        if (error == EEXIST) continue;
        free(bytes);
        return false;
      }
      bool ok = true;
      size_t written = 0;
      while (written < byte_length) {
        ssize_t chunk = write(fd, bytes + written, byte_length - written);
        if (chunk <= 0) {
          ok = false;
          break;
        }
        written += (size_t)chunk;
      }
      if (close(fd) != 0) ok = false; /* TS finally close */
      if (!ok) {
        free(candidate);
        free(bytes);
        return false;
      }
      path = candidate;
    }
    if (path == NULL) {
      free(bytes);
      capture_error("No free screenshot filename between shot0000 and shot9999");
    }
  }
  free(bytes);
  out->path = path;
  out->width = image.width;
  out->height = image.height;
  out->byte_length = (double)byte_length;
  return true;
}

bool qa_frame_capture_levelshot(qa_frame_capture *capture, const char *map_name,
                                const uint8_t *gamma, size_t gamma_length,
                                qa_capture_result *out) {
  memset(out, 0, sizeof *out);
  const char *map = map_name;
  if (strncmp(map, "maps/", 5) == 0) map += 5;
  size_t map_length = strlen(map);
  if (map_length >= 4 && strcmp(map + map_length - 4, ".bsp") == 0) map_length -= 4;
  char *stripped = strndup(map, map_length);
  if (stripped == NULL) return false;
  qa_image_level frame = capture->readback.read_rgba(capture->readback.ctx);
  qa_image_level image;
  if (!qa_make_levelshot(&frame, gamma, gamma_length, &image)) {
    free(stripped);
    return false;
  }
  size_t byte_length = 0;
  uint8_t *bytes = qa_encode_tga(&image, &byte_length);
  free((uint8_t *)image.pixels.bytes);
  if (bytes == NULL) {
    free(stripped);
    return false;
  }
  char *path = capture_path(capture, "levelshots/%s.%s", stripped, "tga");
  free(stripped);
  if (path == NULL || !qa_write_atomic(path, bytes, byte_length)) {
    free(path);
    free(bytes);
    return false;
  }
  free(bytes);
  out->path = path;
  out->width = image.width;
  out->height = image.height;
  out->byte_length = (double)byte_length;
  return true;
}
