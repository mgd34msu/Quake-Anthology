#ifndef QA_FRONTEND_SAVE_COMMANDS_H
#define QA_FRONTEND_SAVE_COMMANDS_H
#include "qa/frontend.h"
#include "qa/source_save.h"

bool frontend_save_commands_create(qa_frontend *, qa_error *);
bool frontend_save_commands_destroy(qa_frontend *, qa_error *);
bool frontend_save_commands_idle(const qa_frontend *);
bool frontend_save_commands_capture_ready(const qa_frontend *);
uint64_t frontend_save_commands_registry(const qa_frontend *);
/* Borrowed actual queue directory authority; no path is reopened. */
qa_fs_root *frontend_save_commands_root(const qa_frontend *);
bool frontend_save_commands_pending(const qa_frontend *);
/* A genuine original-source startup operation holds the active native cut. */
bool frontend_save_commands_restoring(const qa_frontend *);
/* Owned contained path using the exact command queue admission rules. */
char *frontend_save_commands_slot_path(const qa_frontend *, const char *, qa_error *);
bool frontend_save_commands_queue(qa_frontend *, const qa_command_invocation *, qa_error *);
/* Runs once at a real driver boundary; a pending original startup suspends
 * ordinary steps. Publishes through the driver's actual slot.
 * Original and shared signature selection is final. Every displaced/failed
 * heap remains owned for ordinary cleanup, including after publication. */
bool frontend_save_commands_drain(qa_frontend **slot, qa_error *);
bool frontend_save_commands_autosave(qa_frontend *, qa_error *);
bool frontend_save_commands_recovery_available(qa_frontend *,bool *,qa_error *);
bool frontend_save_commands_recovery_queue(qa_frontend *,bool resume,qa_error *);
void frontend_save_commands_recovery_abandon(qa_frontend *);
/* Prepare a real departure; a cached visit is published by the existing
 * driver-slot drain. handled also covers an already queued save operation. */
bool frontend_save_commands_campaign(qa_frontend *, uint64_t revision, bool *handled, qa_error *);
bool frontend_save_commands_checkpoint(qa_frontend *, qa_buffer *, qa_error *);
/* The candidate's real tools root and core command generation must exist.
 * Import creates its own owner; no active manager pointer transfers. */
bool frontend_save_commands_restore(qa_frontend *, qa_bytes, qa_error *);
#endif
