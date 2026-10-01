#ifndef QA_INPUT_PLATFORM_H
#define QA_INPUT_PLATFORM_H
#include "qa/display.h"
#include "qa/input.h"
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
