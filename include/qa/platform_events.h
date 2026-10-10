#ifndef QA_PLATFORM_EVENTS_H
#define QA_PLATFORM_EVENTS_H

#include "qa/common.h"
#include "qa/platform_services.h"
#include "qa/network.h"

enum {
    QA_PLATFORM_EVENT_CAPACITY = 1024,
    QA_PLATFORM_EVENT_BYTE_CAPACITY = 1024 * 1024
};

typedef enum qa_sys_event_kind {
    QA_PLATFORM_EVENT_TIME,
    QA_PLATFORM_EVENT_KEY,
    QA_PLATFORM_EVENT_CHAR,
    QA_PLATFORM_EVENT_MOUSE,
    QA_PLATFORM_EVENT_CONTROLLER_AXIS,
    QA_PLATFORM_EVENT_CONTROLLER_BUTTON,
    QA_PLATFORM_EVENT_CONSOLE_LINE,
    QA_PLATFORM_EVENT_PACKET,
    QA_PLATFORM_EVENT_WINDOW,
    QA_PLATFORM_EVENT_QUIT,
    QA_PLATFORM_EVENT_DEVICE,
    QA_PLATFORM_EVENT_INPUT_FRAME,
    QA_PLATFORM_EVENT_USERCMD,
    QA_PLATFORM_EVENT_CONSOLE_COMMAND,
    QA_PLATFORM_EVENT_KIND_COUNT
} qa_sys_event_kind;

typedef enum qa_sys_mouse_action { QA_SYS_MOUSE_MOVE, QA_SYS_MOUSE_BUTTON, QA_SYS_MOUSE_WHEEL } qa_sys_mouse_action;
typedef enum qa_sys_controller_action {
    QA_SYS_CONTROLLER_AXIS, QA_SYS_CONTROLLER_BUTTON, QA_SYS_CONTROLLER_SENSOR,
    QA_SYS_CONTROLLER_TOUCH, QA_SYS_CONTROLLER_HAT
} qa_sys_controller_action;
typedef enum qa_sys_device_action { QA_SYS_DEVICE_ADDED, QA_SYS_DEVICE_REMOVED, QA_SYS_DEVICE_REMAPPED } qa_sys_device_action;
typedef enum qa_sys_window_action { QA_SYS_WINDOW_OTHER, QA_SYS_WINDOW_FOCUS_GAINED, QA_SYS_WINDOW_FOCUS_LOST, QA_SYS_WINDOW_RESIZED } qa_sys_window_action;

/* SDL and socket records convert here at physical intake. Payload bytes live
 * in the queue's fixed arena until this event is consumed. */
typedef struct qa_sys_event {
    uint64_t time_ns;
    qa_sys_event_kind kind;
    union {
        struct { uint32_t window, code; int32_t symbol; uint16_t modifiers; bool down, repeat; } key;
        struct { uint32_t window; } character;
        struct { uint32_t window, device, buttons; qa_sys_mouse_action action;
            int32_t x, y, dx, dy; float wheel_x, wheel_y; uint8_t button, clicks; bool down, flipped; } mouse;
        struct { int32_t device; qa_sys_controller_action action; uint8_t axis, button, hat;
            int16_t value; bool joystick, down; int32_t sensor, touchpad, finger;
            uint64_t sensor_time_ns; float values[3], x, y, pressure; } controller;
        struct { uint32_t id; qa_sys_window_action action; int32_t width, height; } window;
        struct { int32_t id; qa_sys_device_action action; bool controller; } device;
        struct { uint64_t source_id; qa_net_address from; uint32_t source, route;
            qa_net_poll_kind result; int32_t destination; } packet;
        struct { int32_t operation; } input_frame;
        struct { uint32_t seat; } usercmd;
    } data;
    uint32_t offset, length;
} qa_sys_event;

typedef struct qa_platform_events qa_platform_events;

typedef enum qa_platform_event_result {
    QA_PLATFORM_EVENT_ACCEPTED, QA_PLATFORM_EVENT_FULL, QA_PLATFORM_EVENT_OVERSIZE
} qa_platform_event_result;
typedef struct qa_platform_event_stats {
    uint64_t accepted, consumed, full_records, full_bytes;
    uint64_t deferred[QA_PLATFORM_EVENT_KIND_COUNT], oversize[QA_PLATFORM_EVENT_KIND_COUNT];
    uint32_t records, bytes, peak_records, peak_bytes;
} qa_platform_event_stats;

/* One caller thread owns the cold-sized FIFO. Admission precedes destructive
 * source reads; a full store retains every accepted event and its bytes. */
qa_platform_events *qa_platform_events_create(qa_error *);
void qa_platform_events_destroy(qa_platform_events *);
void qa_platform_events_reset(qa_platform_events *);
qa_platform_event_result qa_platform_events_admit(qa_platform_events *,
    qa_sys_event_kind, size_t maximum_payload);
qa_platform_event_result qa_platform_events_push(qa_platform_events *, const qa_sys_event *,
    qa_bytes payload, qa_bytes tail);
bool qa_platform_events_pending(const qa_platform_events *, qa_sys_event_kind, int32_t value);
qa_platform_event_stats qa_platform_events_statistics(const qa_platform_events *);
/* False means empty. The head remains queued until consumed, so a decoder
 * waiting for a previous packet can resume without losing this one. Returned
 * bytes remain valid until their own consume, reset or destroy. */
bool qa_platform_events_peek(const qa_platform_events *, qa_sys_event *, qa_bytes *);
void qa_platform_events_consume(qa_platform_events *);
/* Quit remains observable while a consumer waits for a retained packet or
 * native settings. Reset clears the request. */
bool qa_platform_events_quit_requested(const qa_platform_events *);
void qa_platform_events_latch_quit(qa_platform_events *);
qa_platform_event_result qa_platform_events_frame(qa_platform_events *, uint64_t time_ns);

#endif
