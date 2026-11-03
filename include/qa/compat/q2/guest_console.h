/*
 * Port of src/compat/q2/guest-console.ts: `AddCommandString` runs with
 * server-console authority, so a game module only gets the commands real
 * modules need (map, gamemap, kick, say, echo, exec, wait). GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_Q2_GUEST_CONSOLE_H
#define QA_COMPAT_Q2_GUEST_CONSOLE_H

#include <stddef.h>

typedef struct qa_compat_q2_guest_console_filter {
  char *text;      /* permitted commands joined by "\n" with a trailing "\n", or "" (owned) */
  char **refused;  /* refused command names, lowercased (owned) */
  size_t refused_count;
} qa_compat_q2_guest_console_filter;

/* TS filterGuestConsole. Release with qa_compat_q2_guest_console_filter_free. */
qa_compat_q2_guest_console_filter qa_compat_q2_filter_guest_console(const char *text);
/* "a, b" of the refused names (TS refused.join(", ")); malloc'd. */
char *qa_compat_q2_guest_console_refused_list(const qa_compat_q2_guest_console_filter *filter);
void qa_compat_q2_guest_console_filter_free(qa_compat_q2_guest_console_filter *filter);

#endif
