#ifndef QA_INPUT_PLATFORM_H
#define QA_INPUT_PLATFORM_H
#include "qa/display.h"
#include "qa/display_settings.h"
#include "qa/input.h"
#include "qa/input_release.h"
#include "qa/console_cvars_prepare.h"
#include <SDL2/SDL.h>

#define QA_INPUT_LOCAL_SEATS 4

typedef enum qa_controller_selection_kind {
    QA_CONTROLLER_AUTO,
    QA_CONTROLLER_NONE,
    QA_CONTROLLER_GUID,
    QA_CONTROLLER_SERIAL
} qa_controller_selection_kind;
typedef struct qa_controller_selection {
    qa_controller_selection_kind kind;
    char guid[33];
    unsigned ordinal;
    const char *serial;
} qa_controller_selection;
typedef struct qa_controller_info {
    int32_t instance;
    const char *name, *serial;
    char guid[33];
    unsigned ordinal;
    bool virtual_device, rumble, trigger_rumble, led;
    bool axes[QA_AXIS_COUNT], buttons[21], sensors[6];
    bool sensor_enabled[6];
    float sensor_rate[6];
    unsigned touchpads;
} qa_controller_info;
typedef struct qa_input_platform qa_input_platform;
typedef struct qa_input_platform_options {
    qa_cvars *cvars;
    void *user;
    /* Notifications inspect current state. Queue routing or lifetime changes
     * until the platform call returns. */
    void (*print)(void *, const char *);
    void (*device_changed)(void *, int32_t instance, bool connected);
    void (*assignment_changed)(void *, unsigned slot, int32_t previous, int32_t instance);
} qa_input_platform_options;
/* App owns SDL global subsystems and the single event pump. This owner retains
 * native device handles, never calls SDL_PollEvent/Init/Quit, and survives
 * window replacement. It must be destroyed before SDL shuts down. */
qa_input_platform *qa_input_platform_create(const qa_input_platform_options *, qa_error *);
/* Complete checked settings abort/publication or entered retirement before destruction. A retained
 * nonterminal settings ticket keeps its native owner alive. */
void qa_input_platform_destroy(qa_input_platform *);
/* Atomic validation/copy of selections precedes release. Slots remain stable
 * when a seat is removed; a NULL seat makes that slot unavailable. */
bool qa_input_platform_routes(qa_input_platform *, qa_input_seat *const seats[4],
                              const qa_controller_selection selections[4], int keyboard_slot,
                              double time_ms, qa_error *);
bool qa_input_platform_retain(qa_input_platform *, unsigned retained_mask, int keyboard_slot,
                              double time_ms, qa_error *);
bool qa_input_platform_keyboard(qa_input_platform *, int slot, double time_ms, qa_error *);
/* Pass NULL before destroying a window. The new display can then be attached.
 */
bool qa_input_platform_window(qa_input_platform *, const qa_display *, double time_ms, qa_error *);
bool qa_input_platform_event(qa_input_platform *, const SDL_Event *, double now_ms, bool *handled,
                             qa_error *);
bool qa_input_platform_frame(qa_input_platform *, double now_ms, qa_error *);
bool qa_input_platform_restart(qa_input_platform *, double now_ms, qa_error *);
typedef struct qa_input_platform_settings {
    bool mouse_available, no_grab, joystick_enabled, windows_joystick, midi_enabled, restart_requested;
    int joystick_seat, midi_seat, midi_device, midi_channel;
    float joystick_threshold, joystick_ball_scale;
} qa_input_platform_settings;
/* The enclosing bootstrap proves its actual completed images phase. This
 * constructor verifies the exact returned edit/registry and projected rows,
 * creates native devices from them, and returns without retaining the edit.
 * Joystick/profile latch application belongs to that mutable preparation. */
qa_input_platform *qa_input_platform_create_prepared(const qa_input_platform_options *,
    const qa_cvars_edit *,const qa_input_platform_settings *,qa_error *);
/* First physical routes/window use the same genuine preparation. These
 * reject existing routes/held input or an already attached display. */
bool qa_input_platform_routes_prepared(qa_input_platform *,qa_input_seat *const [4],
    const qa_controller_selection [4],int,double,const qa_cvars_edit *,
    const qa_input_platform_settings *,qa_error *);
bool qa_input_platform_window_prepared(qa_input_platform *,const qa_display *,double,
    const qa_cvars_edit *,const qa_input_platform_settings *,qa_error *);
typedef struct qa_input_platform_settings_ticket qa_input_platform_settings_ticket;
typedef enum qa_input_platform_settings_outcome {
    QA_INPUT_PLATFORM_SETTINGS_UNENTERED,
    QA_INPUT_PLATFORM_SETTINGS_ENTERED,
    QA_INPUT_PLATFORM_SETTINGS_PUBLISHED,
    QA_INPUT_PLATFORM_SETTINGS_ABORTED,
    QA_INPUT_PLATFORM_SETTINGS_RETIRED
} qa_input_platform_settings_outcome;
typedef struct qa_input_platform_settings_requirements {
    bool source_changed, midi_changed;
    int source_slot, next_source_slot, midi_slot, next_midi_slot;
    int32_t joystick_instance, next_joystick_instance;
    unsigned controller_routes;
} qa_input_platform_settings_requirements;
/* Desired scalar values come from the admitted canonical settings ticket.
 * Preparation retains active native endpoints and the exact physical routes;
 * configuration entries are the actual prepared same-seat owners, or their
 * unchanged active owners. Keep them alive through readiness and termination.
 * Preparation never writes cvars or dispatches input/console callbacks. A returned
 * ticket, including on preparation failure, requires checked disposition before
 * destruction. Readiness must precede publication under the held boundary. */
bool qa_input_platform_settings_prepare(qa_input_platform *, const qa_input_platform_settings *,
    qa_input_seat *const configuration[4], double now_ms, qa_input_platform_settings_ticket **, qa_error *);
/* Copy all four physical selectors and resolve them against the retained native
 * controller inventory. Changed assignments remap prepared seat configurations;
 * an unchanged active configuration is cloned and owned by the ticket if another
 * selector displaces its controller. Active routes and old release identities
 * remain intact until publication. Checked cleanup disposes owned copies. */
bool qa_input_platform_settings_prepare_selected(qa_input_platform *, const qa_input_platform_settings *,
    qa_input_seat *const configuration[4], const qa_controller_selection selections[4], double now_ms,
    qa_input_platform_settings_ticket **, qa_error *);
/* Poll the retained 1000ms clock using actual active settings and physical
 * seats. A successful NULL ticket means no disconnected endpoint needs an
 * attempt. A returned ticket uses the same checked release and publication
 * boundary as settings changes, without requesting a full input restart. */
bool qa_input_platform_reconnect_prepare(qa_input_platform *, double now_ms,
    qa_input_platform_settings_ticket **, qa_error *);
/* Native capture, lifetime changes and ordinary input dispatch require the
 * returned owner to have no retained settings preparation. */
bool qa_input_platform_settings_idle(const qa_input_platform *);
/* Pure actual lease/route association, including failed partial preparation.
 * This does not admit native publication, cleanup or source consumption. */
bool qa_input_platform_settings_retained(const qa_input_platform *,
    const qa_input_platform_settings_ticket *, qa_error *);
bool qa_input_platform_settings_requirements_read(const qa_input_platform_settings_ticket *,
    qa_input_platform_settings_requirements *, qa_error *);
const qa_input_platform *qa_input_platform_settings_owner(const qa_input_platform_settings_ticket *);
qa_input_seat *qa_input_platform_settings_seat(const qa_input_platform_settings_ticket *, unsigned slot);
/* Exact retained source or MIDI pressed keys, used by the real release
 * continuation. No key events are dispatched and no native decoder changes. */
bool qa_input_platform_settings_keys(const qa_input_platform_settings_ticket *, bool midi,
    int *keys, size_t capacity, size_t *count, qa_error *);
bool qa_input_platform_settings_release_scope(const qa_input_platform_settings_ticket *, unsigned slot,
    qa_input_release_scope *, int *keys, size_t capacity, qa_error *);
/* Borrowed requested-device diagnostic; emit only after publication, outside
 * its handoff, and before ticket destruction. Aborted warnings are discarded. */
const char *qa_input_platform_settings_diagnostic(const qa_input_platform_settings_ticket *);
/* Only an admitted completed source release can retire an exclusive active
 * MIDI endpoint. ENTERED remains observable on failure; abort then refuses to
 * claim restoration of that retired native stream and retains its parents. */
bool qa_input_platform_settings_enter(qa_input_platform_settings_ticket *,
    const qa_input_release *const release[4], qa_input_platform_settings_outcome *, qa_error *);
qa_input_platform_settings_outcome qa_input_platform_settings_result(const qa_input_platform_settings_ticket *);
bool qa_input_platform_settings_ready(qa_input_platform_settings_ticket *,
    const qa_input_release *const release[4], qa_error *);
/* Pure retained-owner witness after successful settings_ready. Requires the
 * same platform and completed physical releases, with unchanged ticket state,
 * routes, configurations and native resource identities. Entry, window staging,
 * cleanup and publication invalidate it. Keep native/display/source parents
 * alive and hold their dispatch boundary; hardware and source currentness are
 * checked by settings_ready, never polled by this witness. */
bool qa_input_platform_settings_ready_is(const qa_input_platform_settings_ticket *,
    const qa_input_platform *, const qa_input_release *const release[4]);
/* Borrow the actual staged surface while both native windows remain alive.
 * Every occupied physical seat must retain a completed ALL release before
 * candidate capture is entered. A failed stage may retain native changes;
 * keep the surface ticket through checked input cleanup. Readiness precedes
 * input publication, which must precede surface publication. Restore the
 * surface first on abort, then input capture/outputs, before destroying its
 * candidate. No input events, source commands or endpoint restart occur here.
 * Candidate native focus must match the actual retained physical seats. */
bool qa_input_platform_settings_window_stage(qa_input_platform_settings_ticket *,
    const qa_display_surface_ticket *, const qa_input_release *const release[4], qa_error *);
bool qa_input_platform_settings_abort(qa_input_platform_settings_ticket *, qa_error *);
/* Dispose an entered replacement only while every required actual source
 * release proof remains completed and retained. Checked cleanup restores the
 * retained capture/output modes and disposes candidate endpoints, preserving
 * the old routes and joystick. The already retired old MIDI stream stays
 * closed; its decoder/held history is retired without dispatch or replay.
 * A refusal retains the ticket and proofs for cleanup retry and excludes
 * entry/publication. A terminal close error returns false with RETIRED, and
 * the ticket is destroyable. This disposition never claims source rollback. */
bool qa_input_platform_settings_retire_entered(qa_input_platform_settings_ticket *,
    const qa_input_release *const release[4], qa_error *);
/* Checked native disposal when every actual retained release is completed
 * and contains no authored/dormant source record. Physical configuration and
 * scope proofs remain mandatory; no active source is asserted or called. */
bool qa_input_platform_settings_retire_entered_empty(qa_input_platform_settings_ticket *,
    const qa_input_release *const release[4], qa_error *);
/* The same native cleanup under actual actor/source retirement authority.
 * Exact retained physical scope and every captured source history qualify
 * before cleanup; completion/current publication is not substituted for
 * retirement. Keep all release tickets until terminal RETIRED, including a
 * terminal close error. Cleanup refusals retain them for the next retry. */
bool qa_input_platform_settings_retire_entered_disposition(qa_input_platform_settings_ticket *,
    const qa_input_release *const release[4], qa_console_release_disposition,
    qa_console_release_retirement_fn, void *, qa_error *);
void qa_input_platform_settings_publish(qa_input_platform_settings_ticket *);
bool qa_input_platform_settings_ticket_destroy(qa_input_platform_settings_ticket *, qa_error *);
size_t qa_input_platform_device_count(const qa_input_platform *);
bool qa_input_platform_device(const qa_input_platform *, size_t, qa_controller_info *);
typedef struct qa_controller_snapshot {
    int16_t axes[QA_AXIS_COUNT];
    bool buttons[21];
} qa_controller_snapshot;
bool qa_input_platform_snapshot(qa_input_platform *, int32_t instance, qa_controller_snapshot *,
                                qa_error *);
int32_t qa_input_platform_controller(const qa_input_platform *, unsigned slot);
/* Actual configured selection, independent of the currently assigned device.
 * The serial is borrowed until route replacement or owner destruction. */
bool qa_input_platform_selection(const qa_input_platform *, unsigned slot, qa_controller_selection *);
bool qa_input_platform_mapping(qa_input_platform *, const char *mapping, qa_error *);
bool qa_input_platform_rumble(qa_input_platform *, int32_t, float low, float high,
                              uint32_t duration_ms, qa_error *);
bool qa_input_platform_trigger_rumble(qa_input_platform *, int32_t, float left, float right,
                                      uint32_t duration_ms, qa_error *);
bool qa_input_platform_led(qa_input_platform *, int32_t, uint8_t red, uint8_t green, uint8_t blue,
                           qa_error *);
bool qa_input_platform_sensor(qa_input_platform *, int32_t, SDL_SensorType, bool enabled,
                              qa_error *);
bool qa_input_platform_gyro(qa_input_platform *, unsigned slot, bool enabled, qa_error *);
bool qa_input_platform_calibrate(qa_input_platform *, unsigned slot, qa_error *);
bool qa_input_platform_calibration_cancel(qa_input_platform *, unsigned slot, bool reset,
                                          qa_error *);
qa_haptic_player *qa_input_platform_haptics(qa_input_platform *, unsigned slot);
bool qa_input_platform_tactile(qa_input_platform *, unsigned slot, qa_vfs *, const char *sound,
                               double time_ms, qa_error *);
void qa_input_platform_tactile_invalidate(qa_input_platform *);
/* Metadata is owned by caller; enumeration does not open hardware. */
typedef struct qa_midi_device {
    unsigned card, device;
    char name[64], path[128];
} qa_midi_device;
bool qa_input_midi_devices(qa_midi_device **, size_t *, qa_error *);
void qa_input_platform_midi_info(qa_input_platform *);
#endif
