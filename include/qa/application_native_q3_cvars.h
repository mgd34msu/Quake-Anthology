#ifndef QA_APPLICATION_NATIVE_Q3_CVARS_H
#define QA_APPLICATION_NATIVE_Q3_CVARS_H
#include "qa/game_q3.h"
#include "qa/launch.h"
#include "qa/console.h"

typedef struct qa_native_q3_cvar_definition {
    const char *symbol, *name, *reset;
    uint32_t flags;
} qa_native_q3_cvar_definition;
size_t qa_native_q3_cvar_definition_count(qa_q3_product);
bool qa_native_q3_cvar_definition_at(qa_q3_product,size_t,qa_native_q3_cvar_definition *);
/* Declares the actual CLIENT baseline before configuration scripts. This
 * owns no CGAME cache, CHARACTER lease or source initialization receipt. */
bool qa_native_q3_client_defaults(const qa_launch_instance *, qa_cvars *,
    const qa_command_context *, const char *configured_model, qa_error *);
#endif
