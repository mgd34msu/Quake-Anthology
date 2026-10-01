#ifndef QA_INPUT_PLATFORM_PRIVATE_H
#define QA_INPUT_PLATFORM_PRIVATE_H
#include "qa/input_platform.h"
#include "qa/vfs.h"

struct device {
    SDL_GameController *handle;
    qa_controller_info info;
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
    struct device *devices;
    size_t device_count, device_capacity;
    struct seat_route seats[4];
    int keyboard;
    int32_t keys[SDL_NUM_SCANCODES];
    uint32_t window;
    bool old_relative, old_text, old_grab, capture;
    int old_controller_events, old_joystick_events;
    qa_haptic_cache *haptics;
    SDL_Joystick *joystick;
    int32_t joystick_instance;
    qa_source_joystick source;
    bool windows_joystick, mouse_available;
    int source_slot, midi_slot, midi_fd, midi_channel;
    qa_midi_decoder midi;
    bool midi_held[256];
    qa_midi_device *midi_devices;
    size_t midi_count;
    double now;
    bool native_owned;
    input_native_startup native_startup;
    bool native_initializing;
};
bool input_platform_haptic_bindings_ready(const qa_input_platform *);
void input_platform_route_contexts_rebind(qa_input_platform *);
bool input_platform_fresh_routes(qa_input_platform *, qa_input_seat *const [4],
    const qa_controller_selection [4], int, double, qa_error *);
#endif
