/* Port of src/app/bootstrap/simulation/q3/guest-bots.ts. Q3GuestBots owns the
 * engine-side botlib for a QVM qagame: it constructs the BotLibrary, the
 * AasBspEntities and the SourceBotNavigation, and answers the bot imports through
 * qvmBotLibrarySyscall and qvmBotNavigationSyscall over them.
 * C accommodations: the BotUserCommand import runs synchronously (TS may return a
 * Promise); the clients' console_message returns a malloc'd string the syscall
 * frees; destroy releases what close leaves (TS GC). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_BOTS_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_BOTS_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/bots.h"
#include "qa/app/bootstrap/simulation/q3/guest_records.h"
#include "qa/app/bootstrap/simulation/q3/guest_spatial.h"
#include "qa/bots/behavior/director.h"
#include "qa/bots/behavior/library/bsp_entities.h"
#include "qa/bots/behavior/library/log.h"
#include "qa/bots/behavior/q3/library.h"
#include "qa/bots/behavior/q3/navigation.h"
#include "qa/compat/qvm/bot_library_syscalls.h"
#include "qa/compat/qvm/bot_navigation_syscalls.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/persistence/value.h"

/* TS Omit<SourceBotLibraryOptions, "clientCommand"> (Q3GuestBotPreparation.library).
 * files: the BotSourceFiles (files.files.read with files.files.ctx; provenance NULL is
 * the absent member). random keeps its own ctx. The other callbacks receive ctx;
 * resume_log NULL is the absent member. Borrowed for the bots' lifetime. */
typedef struct qa_app_q3_guest_bot_library {
  qa_source_bot_files files;
  qa_bot_random random;
  bool debug;
  void *ctx;
  double (*milliseconds)(void *ctx);
  void (*print)(void *ctx, int severity, const char *text);
  qa_bot_log_open_result (*open_log)(void *ctx, const char *filename);
  qa_bot_log_open_result (*resume_log)(void *ctx, const char *filename, int64_t position);
} qa_app_q3_guest_bot_library;

/* TS Q3GuestBotOptions["clients"] (Pick of QvmBotLibraryServices). Every member
 * receives ctx. console_message returns a malloc'd string (NULL is TS null). */
typedef struct qa_app_q3_guest_bot_clients {
  void *ctx;
  double (*allocate_client)(void *ctx);
  void (*free_client)(void *ctx, double client);
  double (*snapshot_entity)(void *ctx, double client, double sequence);
  char *(*console_message)(void *ctx, double client);
  void (*user_command)(void *ctx, double client, qa_qvmx_wire_user_command command);
} qa_app_q3_guest_bot_clients;

/* TS Q3GuestBotOptions. library.clientCommand is client_command with command_ctx.
 * selected is the ApplicationBotNavigation (checkpoint/restore_checkpoint required);
 * selected, records and spatial are borrowed; entities and map_name are copied. */
typedef struct qa_app_q3_guest_bot_options {
  qa_app_q3_guest_bot_library library;
  void *command_ctx;
  void (*client_command)(void *ctx, int64_t client, const char *command);
  const qa_app_sim_bot_navigation *selected;
  qa_app_q3_guest_records *records;
  qa_app_q3_guest_spatial *spatial;
  const char *entities;
  const char *map_name;
  qa_app_q3_guest_bot_clients clients;
} qa_app_q3_guest_bot_options;

typedef struct qa_app_q3_guest_bots qa_app_q3_guest_bots;
/* TS new Q3GuestBots(options). NULL only on OOM. */
qa_app_q3_guest_bots *qa_app_q3_guest_bots_create(const qa_app_q3_guest_bot_options *options);
/* C-only: frees the bots (after close, or instead of it when never set up). */
void qa_app_q3_guest_bots_destroy(qa_app_q3_guest_bots *bots);
/* TS readonly library / navigation / bspEntities. */
qa_bot_library_bots_a *qa_app_q3_guest_bots_library(qa_app_q3_guest_bots *bots);
qa_source_bot_navigation_bots_a *qa_app_q3_guest_bots_navigation(qa_app_q3_guest_bots *bots);
qa_aas_bsp_entities *qa_app_q3_guest_bots_bsp_entities(qa_app_q3_guest_bots *bots);
/* TS syscall(call): false is TS null (another owner); true stores the result. */
bool qa_app_q3_guest_bots_syscall(qa_app_q3_guest_bots *bots, const qa_qvm_host_call *call, qa_qvm_host_result *out);
/* TS checkpoint(): owned tree. Raises when the files carry no provenance. */
qa_save_value *qa_app_q3_guest_bots_checkpoint(qa_app_q3_guest_bots *bots);
/* TS restore(value). Malformed input raises SaveFormatError; mismatches raise Error. */
void qa_app_q3_guest_bots_restore(qa_app_q3_guest_bots *bots, const qa_save_value *value);
/* TS close(): services.shutdown(). */
void qa_app_q3_guest_bots_close(qa_app_q3_guest_bots *bots);
#endif
