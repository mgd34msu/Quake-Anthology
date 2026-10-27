/* Port of src/content/q1/foundation/precache.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_FOUNDATION_PRECACHE_H
#define QA_CONTENT_Q1_FOUNDATION_PRECACHE_H

#include "qa/content/q1/foundation/types.h"

typedef struct qa_q1_precache_registry qa_q1_precache_registry;
qa_q1_precache_registry *qa_q1_precache_registry_create(void);
void qa_q1_precache_registry_destroy(qa_q1_precache_registry *registry);
const char *qa_q1_precache_phase(const qa_q1_precache_registry *registry);
const char *const *qa_q1_precache_models(const qa_q1_precache_registry *registry, size_t *out_count);
const char *const *qa_q1_precache_sounds(const qa_q1_precache_registry *registry, size_t *out_count);
void qa_q1_precache_begin_world(qa_q1_precache_registry *registry, const char *path, double inline_models);
const char *qa_q1_precache_model(qa_q1_precache_registry *registry, const char *path);
const char *qa_q1_precache_sound(qa_q1_precache_registry *registry, const char *path);
void qa_q1_precache_freeze(qa_q1_precache_registry *registry);
void qa_q1_precache_restore(qa_q1_precache_registry *registry, qa_q1_precache_tables tables);

#endif
