#ifndef QA_FRONTEND_REMOTE_INPUT_H
#define QA_FRONTEND_REMOTE_INPUT_H

#include "qa/application_q3_client.h"
#include "qa/input.h"
#include "qa/network.h"

typedef struct frontend_remote_input frontend_remote_input;
typedef struct frontend_remote_input_source {
    qa_net_client_id connection;
    uint64_t epoch;
    qa_application_q3_client_context receiver;
    /* Actual private input/mouse registry admitted for this source and seat;
     * it may be distinct from the CGAME module registry. */
    const qa_cvars *input_settings;
    /* Actual Q3-view speed registry. Foreign selected movement may have a
     * distinct view owner while both builders share this same mouse owner. */
    const qa_cvars *movement_settings;
    qa_input_command_frame frame;
    qa_vec3 initial_angles;
    bool has_initial_angles;
} frontend_remote_input_source;
typedef struct frontend_remote_input_options {
    void *context;
    /* Pure actual network-owner read. Inactive input returns present=false.
     * The Q3 frame supplies the real command clock, weapon and sensitivity;
     * initial angles come from actual snapshot viewangles minus deltaAngles. */
    bool (*source_read)(void *, frontend_remote_input_source *, bool *present, qa_error *);
    /* Qualify the retained connection/epoch, complete receiver lifetime,
     * private input registry and command clock before adopting a command. */
    bool (*source_current)(void *, const frontend_remote_input_source *);
} frontend_remote_input_options;

bool frontend_remote_input_create(const frontend_remote_input_options *, frontend_remote_input **, qa_error *);
void frontend_remote_input_destroy(frontend_remote_input *);
/* Called only by the genuine network clear-active producer. */
void frontend_remote_input_clear(frontend_remote_input *);
/* Consumes an already sampled physical receipt. This owner neither samples
 * input nor sends packets. Failure leaves the builder and output unchanged. */
bool frontend_remote_input_build(frontend_remote_input *, const qa_seat_input_sample *,
    double source_frame_ms, qa_movement_command *, bool *present, qa_error *);
/* Exact logical builder continuation; no source read, clock or callback. The
 * enclosing network codec reconstructs its actual candidate bindings. */
bool frontend_remote_input_checkpoint(const frontend_remote_input *, qa_buffer *, qa_error *);
bool frontend_remote_input_restore(frontend_remote_input *, qa_bytes, qa_error *);

#endif
