#ifndef APPLICATION_CLIENT_OUTPUTS_H
#define APPLICATION_CLIENT_OUTPUTS_H

#include "qa/application.h"
#include "qa/movement.h"

typedef enum application_client_output_channel {
    APPLICATION_CLIENT_VIEW_OFFSET,
    APPLICATION_CLIENT_MOVEMENT_MODE,
    APPLICATION_CLIENT_STANCE,
    APPLICATION_CLIENT_BODY_SHAPE,
    APPLICATION_CLIENT_OUTPUT_COUNT
} application_client_output_channel;

typedef struct application_client_outputs {
    bool has_view_offset;
    qa_vec3 view_offset;
    bool has_mode;
    qa_movement_mode mode;
    bool has_stance, crouched;
    bool has_body_bounds;
    qa_bounds body_bounds;
} application_client_outputs;

/* Copies only publications owned by live, attached source leases. */
bool application_qc_control_outputs(const qa_application *, qa_actor_id,
                                    application_client_outputs *, qa_error *);
/* Qualifies the selected movement's actual original output consumers. */
bool application_control_output_admit(const qa_application *, qa_actor_id,
                                      uint8_t channels, qa_error *);

#endif
