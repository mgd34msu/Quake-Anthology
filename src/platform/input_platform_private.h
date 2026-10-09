#ifndef QA_INPUT_PLATFORM_PRIVATE_H
#define QA_INPUT_PLATFORM_PRIVATE_H
#include "qa/input_platform.h"
#include "qa/vfs.h"

typedef struct input_motor_output {
    bool requested, applied;
    uint16_t low, high;
    uint32_t duration;
    uint64_t ticks;
} input_motor_output;
typedef struct input_sensor_output {
    bool requested, applied, enabled;
} input_sensor_output;
struct device {
    SDL_GameController *handle;
    qa_controller_info info;
    input_motor_output rumble, triggers;
    input_sensor_output sensor_output[6];
};
struct seat_route {
    qa_input_platform *platform;
    unsigned slot;
    qa_input_seat *seat;
    qa_controller_selection selection;
    int32_t instance, haptic_instance, calibration_instance;
    bool calibration_sensor;
    qa_haptic_player haptic;
};
typedef enum input_native_startup {
    INPUT_NATIVE_READY,
    INPUT_NATIVE_PENDING,
    INPUT_NATIVE_FAILED
} input_native_startup;
struct qa_input_platform {
    qa_input_platform_options options;
    const qa_cvars_edit *constructor_edit;
    const qa_input_platform_settings *constructor_settings;
    struct device *devices;
    size_t device_count, device_capacity;
    struct seat_route seats[4];
    int keyboard;
    int32_t keys[SDL_NUM_SCANCODES];
    uint32_t window;
    bool old_relative, old_text, old_grab, capture;
    int old_cursor;
    int old_controller_events, old_joystick_events;
    qa_haptic_cache *haptics;
    SDL_Joystick *joystick;
    input_motor_output joystick_rumble;
    int32_t joystick_instance;
    qa_source_joystick source;
    bool windows_joystick, mouse_available;
    bool joystick_enabled, midi_enabled;
    int requested_midi_device;
    int source_slot, midi_slot, midi_fd, midi_channel;
    qa_midi_decoder midi;
    bool midi_pending;
    uint8_t midi_byte;
    uint64_t midi_reads, midi_generation;
    bool midi_held[256];
    qa_midi_device *midi_devices;
    size_t midi_count;
    double now, retry_at;
    bool native_owned;
    input_native_startup native_startup;
    bool native_initializing;
    qa_input_platform_settings_ticket *settings_ticket;
    struct qa_input_platform_restore_guard *restore_abort;
};
bool input_platform_restore_abort_pending(qa_input_platform *, qa_error *);
bool input_platform_modes_apply(SDL_Window *, bool relative, bool grab, bool text, qa_error *);
bool input_platform_haptic_bindings_ready(const qa_input_platform *);
void input_platform_route_contexts_rebind(qa_input_platform *);
bool input_platform_fresh_routes(qa_input_platform *, qa_input_seat *const [4],
    const qa_controller_selection [4], int, double, qa_error *);
#endif
