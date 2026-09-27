#ifndef QA_CONFIGURATION_INTERNAL_H
#define QA_CONFIGURATION_INTERNAL_H
#include "qa/launch.h"
#include <stdlib.h>
#include <string.h>

struct qa_launch_draft {
    qa_catalog *catalog;
    qa_strings *strings;
    qa_launch_choices choices;
    size_t provider_capacity, binding_capacity, mod_capacity, mode_capacity;
    size_t equipment_capacity, seat_capacity, loadout_capacity, monster_capacity;
    size_t behavior_capacity;
};
bool launch_grow(void **, size_t *, size_t, size_t, qa_error *);
const char *launch_text(qa_launch_draft *, const char *, qa_error *);
bool launch_scope_equal(qa_launch_scope, qa_launch_scope);
bool launch_scope_valid(qa_launch_scope);
const qa_launch_provider *launch_provider(const qa_launch_choices *, const char *);
bool launch_defaults(qa_launch_draft *, qa_product_id, const char *, qa_error *);
bool launch_empty(qa_catalog *, qa_launch_draft **, qa_error *);
#endif
