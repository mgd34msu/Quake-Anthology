#ifndef QA_TEXT_SAVE_PRIVATE_H
#define QA_TEXT_SAVE_PRIVATE_H
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>
static inline bool qa_text_save_header(qa_source_save_io *io, const char name[4])
{
    uint8_t magic[4]; memcpy(magic, name, 4);
    return qa_source_save_bytes(io, magic, 4) && !memcmp(magic, name, 4);
}
#endif
