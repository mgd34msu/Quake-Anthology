/* Port of src/formats/images/limits.ts. GPL-2.0-or-later. */
#include "qa/formats/images/limits.h"

#include <math.h>

#include "qa/foundation/support.h"

static bool safe_integer(double value) {
  return isfinite(value) && trunc(value) == value && fabs(value) <= (double)QA_MAX_SAFE_INTEGER;
}

bool qa_image_size_allowed(double width, double height) {
  return safe_integer(width) && safe_integer(height) && width > 0 && height > 0 &&
         width <= QA_MAX_IMAGE_DIMENSION && height <= QA_MAX_IMAGE_DIMENSION &&
         width * height <= QA_MAX_IMAGE_PIXELS;
}
