#ifndef QA_IMAGE_INTERNAL_H
#define QA_IMAGE_INTERNAL_H
#include "qa/binary.h"
#include "qa/image.h"
#include "qa/q3_color.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool qa_img_fail(qa_error *error, qa_status status, size_t offset, const char *message);
bool qa_img_size(uint32_t w, uint32_t h, size_t channels, size_t *size, qa_error *error);
bool qa_img_alloc(qa_buffer *out, size_t size, qa_error *error);
bool qa_img_copy(qa_bytes bytes, qa_buffer *out, qa_error *error);
bool qa_img_input(qa_bytes bytes, size_t minimum, const void *out, qa_error *error);
bool qa_img_rgba(const qa_image *image, qa_error *error);
bool qa_img_palette(qa_bytes rgb, qa_image *image, qa_error *error);
bool qa_img_new(uint32_t w, uint32_t h, qa_image *out, qa_error *error);
bool qa_img_q3_color_valid(const qa_q3_color_inputs *, qa_error *);
static inline bool qa_img_range(qa_bytes b, size_t offset, size_t length) {
    return offset <= b.size && length <= b.size - offset;
}
#endif
