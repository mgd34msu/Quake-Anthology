/* Port of src/formats/bsp-kind.ts. GPL-2.0-or-later. */
#include "qa/formats/bsp_kind.h"

#include "qa/core/binary/index.h"

const char *qa_classify_bsp(const uint8_t *data, size_t length,
                            const char *source) {
  const char *name = source != NULL ? source : "<bsp>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)data, length, name)) {
    return NULL;
  }
  uint32_t magic = qa_binary_reader_u32(&reader);
  const char *result = NULL;
  if (magic == 29 || magic == 0x32505342u || magic == 0x42535032u ||
      magic == 0x51363420u) {
    result = "q1";
  } else if (magic == 0x50534249u || magic == 0x50534251u) {
    uint32_t version = qa_binary_reader_u32(&reader);
    if (version == 38) {
      result = "q2";
    } else if (magic == 0x50534249u && (version == 44 || version == 46)) {
      result = "q3";
    } else {
      qa_binary_reader_destroy(&reader);
      qa_binary_error(name, 4, "unsupported BSP version %llu",
                      (unsigned long long)version);
    }
  } else {
    qa_binary_reader_destroy(&reader);
    qa_binary_error(name, 0, "unsupported BSP identifier %llu",
                    (unsigned long long)magic);
  }
  qa_binary_reader_destroy(&reader);
  return result;
}
