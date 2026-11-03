/* Port of src/compat/q2/guest-strings.ts: ReusableGuestString, one guest
 * string rewritten in place that moves only when a value outgrows its pages,
 * and CommandGuestStrings, command text valid until the next command.
 * C strings are bytes: latin1Bytes and TextEncoder agree with them for the
 * byte strings the hosts pass. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_GUEST_STRINGS_H
#define QA_COMPAT_Q2_GUEST_STRINGS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/q2/classic/layout.h"

typedef struct qa_compat_q2_reusable_guest_string {
  const qa_compat_q2_memory *memory;
  char *label; /* owned */
  bool has_address; qa_guest_address address; double capacity;
  char *text; /* owned; NULL is the TS null */
} qa_compat_q2_reusable_guest_string;
void qa_compat_q2_reusable_guest_string_init(qa_compat_q2_reusable_guest_string *s, const qa_compat_q2_memory *memory, const char *label);
void qa_compat_q2_reusable_guest_string_destroy(qa_compat_q2_reusable_guest_string *s);
/* TS setText(text, encode). */
qa_guest_address qa_compat_q2_reusable_guest_string_set_text(qa_compat_q2_reusable_guest_string *s, const char *text);
/* TS set(bytes): bytes are terminated in the guest. */
qa_guest_address qa_compat_q2_reusable_guest_string_set(qa_compat_q2_reusable_guest_string *s, const uint8_t *bytes, size_t length);

typedef struct qa_compat_q2_command_guest_strings qa_compat_q2_command_guest_strings;
qa_compat_q2_command_guest_strings *qa_compat_q2_command_guest_strings_create(const qa_compat_q2_memory *memory, const char *label);
/* Frees host storage; the guest blocks go with the memory. */
void qa_compat_q2_command_guest_strings_destroy(qa_compat_q2_command_guest_strings *s);
/* TS get(command, text): command keys may hold NUL separators, so they carry a length. */
qa_guest_address qa_compat_q2_command_guest_strings_get(qa_compat_q2_command_guest_strings *s, const char *command, size_t command_length, const char *text);
#endif
