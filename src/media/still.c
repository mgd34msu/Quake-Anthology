/* Port of src/media/still.ts. GPL-2.0-or-later. */
#include "qa/media/still.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/formats/images/indexed.h"
#include "qa/media/cin.h"

static noreturn void media_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

bool qa_cinematic_pcx(const uint8_t *bytes, size_t length,
                      const char *source, qa_cinematic_source *out) {
  memset(out, 0, sizeof *out);
  const char *name = source == NULL ? "<cinematic PCX>" : source;
  qa_pcx_image image;
  if (!qa_decode_pcx(bytes, length, name, &image)) return false;
  if (image.palette == NULL) {
    qa_pcx_image_destroy(&image);
    media_error("A cinematic PCX needs its own palette");
  }
  uint8_t *rgba = qa_cin_rgba(image.indices, image.indices_length,
                              image.palette, image.palette_length);
  if (rgba == NULL) {
    qa_pcx_image_destroy(&image);
    return false;
  }
  out->format = QA_CINEMATIC_FORMAT_IMAGE;
  out->source = name;
  out->width = image.width;
  out->height = image.height;
  out->rgba = rgba;
  out->rgba_length = image.indices_length * 4;
  out->rgba_owned = true;
  qa_pcx_image_destroy(&image);
  return true;
}
