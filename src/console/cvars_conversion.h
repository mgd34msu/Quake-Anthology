#ifndef QA_CVARS_CONVERSION_H
#define QA_CVARS_CONVERSION_H
#include "internal.h"
#include "cvar_catalog_generated.h"

typedef struct qac_cvar_conversion_input {
    const qa_cvar_options *options;
    const qa_cvar_catalog_conversion *conversion;
    const qa_cvar_catalog_binding *binding;
    const char *value, *current, *detail;
    void *user;
    const char *(*operand)(void *, uint16_t row);
} qac_cvar_conversion_input;
typedef struct qac_cvar_change {
    uint16_t row;
    const char *value;
    char text[64];
} qac_cvar_change;
typedef struct qac_cvar_conversion_output {
    const char *value, *detail_value;
    char *allocated_value;
    char text[64];
    qac_cvar_change changes[32];
    size_t change_count;
    bool detail;
} qac_cvar_conversion_output;
/* Conversion is pure: composite writes are retained assignments, never
 * callbacks or partially published changes. Strings borrow the input except
 * allocated_value, which the admitting caller releases. */
bool qac_cvar_read_conversion(const qac_cvar_conversion_input *,
    qac_cvar_conversion_output *, qa_error *);
bool qac_cvar_write_conversion(const qac_cvar_conversion_input *,
    qac_cvar_conversion_output *, qa_error *);
#endif
