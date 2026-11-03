/* Port of src/text/localization-key.ts: the versioned carrier for a rerelease
 * localization key plus its arguments inside a print string.
 * Layout: RS "QLK" <version digit> US <key> (US <argument>)*.
 * Returned strings are malloc'd and owned by the caller; allocation failure
 * aborts. GPL-2.0-or-later. */
#ifndef QA_TEXT_LOCALIZATION_KEY_H
#define QA_TEXT_LOCALIZATION_KEY_H
#include <stdbool.h>
#include <stddef.h>
#define QA_LOCALIZATION_KEY_VERSION 1
/* TS LocalizationKeyText. */
typedef struct qa_localization_key_text {
  char *key;
  char **args; size_t arg_count;
} qa_localization_key_text;
char *qa_encode_localization_key(const char *key, const char *const *args, size_t arg_count);
bool qa_is_localization_key_text(const char *text);
/* TS decodeLocalizationKey: false is TS null (ordinary text, or a version
 * this reader does not define); true fills *out, released with
 * qa_localization_key_text_release. */
bool qa_decode_localization_key(const char *text, qa_localization_key_text *out);
void qa_localization_key_text_release(qa_localization_key_text *text);
/* TS plainLocalizationText: ordinary text is returned unchanged (copied). */
char *qa_plain_localization_text(const char *text);
#endif
