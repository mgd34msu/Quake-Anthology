#ifndef QA_BOTS_SAVE_H
#define QA_BOTS_SAVE_H

#include "qa/bots.h"

typedef struct qa_bots_save_requirements {
    qa_bot_runtime_options runtime;
    bool library_reload_characters, population;
    uint32_t action_capacity, population_client_capacity, population_actor_capacity;
} qa_bots_save_requirements;

/* Real constructor configuration, separate from private continuation owners.
 * Mutable global definitions belong to the sole runtime state codec.
 * Decode runs no services and returns owned include/date/time strings. Supply
 * candidate callbacks before runtime construction and retain this configuration
 * until that runtime is destroyed. Complete validation precedes output. */
bool qa_bots_save_requirements_capture(const qa_bot_runtime *, const qa_bots *, qa_buffer *, qa_error *);
bool qa_bots_save_requirements_read(qa_bytes, qa_bots_save_requirements *, qa_error *);
void qa_bots_save_requirements_free(qa_bots_save_requirements *);

#endif
