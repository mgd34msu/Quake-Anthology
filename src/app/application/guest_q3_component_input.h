#ifndef QA_APPLICATION_GUEST_Q3_COMPONENT_INPUT_H
#define QA_APPLICATION_GUEST_Q3_COMPONENT_INPUT_H
#include "guest_q3_component.h"

typedef struct application_q3_component_input application_q3_component_input;
/* The canonical caller supplies its actual working command, absolute aim,
 * full control impulse and source frame in the ModInput value representation.
 * The output callback applies each source SET/consume to that same command. */
typedef application_q3_mod_input_values_fn application_q3_component_input_values;
typedef bool (*application_q3_component_input_output)(void *,
    const application_q3_mod_output *,qa_error *);
/* A failed begin can retain an opened scope in out. Abort consumes only its
 * genuine returned application; callers keep it when checked close refuses. */
bool application_q3_component_input_begin(application_q3_component *,qa_actor_id,
    bool movement_slice,application_q3_component_input_values,
    application_q3_component_input_output,void *,application_q3_component_input **,qa_error *);
/* Only a completed canonical body runs the authored after bindings. Values
 * are borrowed afresh at that boundary; no output callback is retained.
 * Complete the roster in declaration order, then abort in reverse order to
 * retire the nested applications. */
bool application_q3_component_input_complete(application_q3_component_input *,
    bool completed,application_q3_component_input_values,void *,qa_error *);
bool application_q3_component_input_abort(application_q3_component_input **,qa_error *);
#endif
