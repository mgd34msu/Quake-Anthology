#ifndef QA_LOCALIZATION_H
#define QA_LOCALIZATION_H

#include "qa/vfs.h"

typedef struct qa_localization qa_localization;
typedef struct qa_localization_pool qa_localization_pool;
typedef enum qa_localization_profile {
  QA_LOCALIZATION_Q1_RERELEASE,
  QA_LOCALIZATION_Q2_RERELEASE
} qa_localization_profile;
typedef struct qa_localization_options {
  qa_localization_profile profile;
  const char *platform;
  void *context;
  void (*warning)(void *context, const char *key, const char *message);
} qa_localization_options;
typedef struct qa_localization_argument {
  uint16_t start, end;
  uint8_t index;
} qa_localization_argument;
typedef struct qa_localization_entry {
  const char *format;
  qa_localization_argument arguments[8];
  uint8_t argument_count;
} qa_localization_entry;

/* Immutable compiled catalogs may be shared by seats and server consumers.
 * Later layers override earlier layers; duplicate rules apply within each file.
 * Callbacks only report diagnostics and must not mutate catalog owners. */
bool qa_localization_create(const qa_bytes *layers, size_t count,
                            const qa_localization_options *options,
                            qa_localization **out, qa_error *error);
void qa_localization_retain(qa_localization *catalog);
void qa_localization_release(qa_localization *catalog);
size_t qa_localization_count(const qa_localization *catalog);
const qa_localization_entry *
qa_localization_find(const qa_localization *catalog, const char *key);
/* Returns bytes written excluding NUL. Zero capacity writes nothing. Raw mode
 * preserves guest byte truncation; presentation mode ends each append at a
 * UTF-8 boundary. Argument substitutions themselves use the source 1024 limit.
 */
size_t qa_localize(const qa_localization *catalog, const char *base,
                   const char *const *arguments, size_t argument_count,
                   bool allow_in_place, bool raw_bytes, char *out,
                   size_t capacity);
const char *qa_localization_language(const char *locale);

qa_localization_pool *qa_localization_pool_create(qa_error *error);
void qa_localization_pool_destroy(qa_localization_pool *pool);
void qa_localization_pool_trim(qa_localization_pool *pool);
/* Resolve through the caller's mounts first, then share by ordered content
 * digests, profile and platform. English, English mod, selected, selected mod.
 * Held catalogs survive mount changes and pool destruction. */
bool qa_localization_acquire(qa_localization_pool *pool, qa_vfs *view,
                             const char *language,
                             const qa_localization_options *options,
                             qa_localization **out, qa_error *error);

#endif
