/* Port of src/app/bootstrap/save-requests.ts. Plain TS Errors abort
 * with identical message (qa_range_error). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SAVE_REQUESTS_H
#define QA_APP_BOOTSTRAP_SAVE_REQUESTS_H
#include <stddef.h>
#include "qa/core/commands/documentation.h"
/* TS ApplicationSaveFormat: "shared"|"v5"|"v6" (borrowed literal). */
typedef const char *qa_app_save_format;
typedef struct qa_app_save_request { const char *name; qa_app_save_format format; } qa_app_save_request;
typedef struct qa_app_load_request { const char *name; const char *source_product; /* NULL absent */ } qa_app_load_request;
qa_app_save_request qa_app_parse_save_request(const char *const *args, size_t count);
qa_app_load_request qa_app_parse_load_request(const char *const *args, size_t count);
/* NULL for TS undefined. Returned doc is static/borrowed. */
const qa_command_documentation *qa_app_save_command_documentation(const char *name);
#endif
