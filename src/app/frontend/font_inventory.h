#ifndef QA_FRONTEND_FONT_INVENTORY_H
#define QA_FRONTEND_FONT_INVENTORY_H
#include "internal.h"
bool frontend_font_encode(void *, const qa_font *, uint64_t *, qa_error *);
bool frontend_font_decode(void *, uint64_t, const qa_font **, qa_error *);
#endif
