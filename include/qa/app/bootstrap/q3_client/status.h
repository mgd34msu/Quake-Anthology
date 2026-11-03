/* Port of src/app/bootstrap/q3-client/status.ts. qvmCvarSyscall runs over
 * the abstract QvmCvarServices (qa_qvm_cvar_table_syscall); words are the
 * live syscall word bytes (DataView). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_STATUS_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_STATUS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/q3_client/cvars.h"
#include "qa/compat/qvm/memory.h"
/* TS effectiveStatusCvar: returns a snapshot; *out owns its strings
 * (a copy is made even when unchanged). */
void qa_app_effective_status_cvar(const qa_cvar_snapshot *value, bool visible, qa_cvar_snapshot *out);
/* TS cgameStatusCvars(source, visible): heap wrapper whose services
 * view masks cg_drawstatus. */
typedef struct qa_app_cgame_status_cvars qa_app_cgame_status_cvars;
qa_app_cgame_status_cvars *qa_app_cgame_status_cvars_create(
    qa_app_qvm_cvar_services source, bool (*visible)(void *ctx), void *visible_ctx);
void qa_app_cgame_status_cvars_destroy(qa_app_cgame_status_cvars *cvars);
qa_app_qvm_cvar_services qa_app_cgame_status_cvars_services(qa_app_cgame_status_cvars *cvars);
/* TS CgameStatusView. */
typedef struct qa_app_cgame_status_view qa_app_cgame_status_view;
qa_app_cgame_status_view *qa_app_cgame_status_view_create(
    qa_app_qvm_cvar_services source, bool (*visible)(void *ctx), void *visible_ctx);
void qa_app_cgame_status_view_destroy(qa_app_cgame_status_view *view);
/* TS cvars field. */
qa_app_qvm_cvar_services qa_app_cgame_status_view_cvars(qa_app_cgame_status_view *view);
/* TS syscall(): false is the TS null. words_length >= 8. */
bool qa_app_cgame_status_view_syscall(qa_app_cgame_status_view *view, uint8_t *words,
                                      size_t words_length, qa_qvm_memory *memory, double *out_result);
void qa_app_cgame_status_view_refresh(qa_app_cgame_status_view *view);
#endif
