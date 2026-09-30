#ifndef QA_CONSOLE_SAVE_FIELDS_H
#define QA_CONSOLE_SAVE_FIELDS_H
#include "qa/source_save.h"
#include "qa/console.h"
bool qac_save_text(qa_source_save_io *, const char **);
bool qac_save_documentation(qa_source_save_io *, const qa_console_documentation **);
#endif
