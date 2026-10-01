#ifndef QA_TOOLS_H
#define QA_TOOLS_H
#include "qa/camera.h"
#include "qa/console.h"
#include "qa/native.h"

typedef enum qa_capture_format { QA_CAPTURE_TGA, QA_CAPTURE_PNG, QA_CAPTURE_JPEG } qa_capture_format;
typedef struct qa_capture_result { char *path; uint32_t width, height; size_t bytes; } qa_capture_result;
void qa_capture_result_free(qa_capture_result *);
bool qa_capture_encode(const qa_image *, qa_capture_format, int quality, qa_buffer *, qa_error *);
bool qa_capture_levelshot(const qa_image *, const uint8_t gamma[256], qa_image *, qa_error *);
/* Saves an already owned frame; NULL name selects atomic exclusive shotNNNN.
 * Names and maps are validated by the shared secure VFS writer. */
bool qa_capture_save(qa_vfs *, qa_mount_id, const qa_image *, qa_capture_format,
                      const char *name, int quality, qa_capture_result *, qa_error *);
bool qa_capture_save_levelshot(qa_vfs *, qa_mount_id, const qa_image *, const char *map,
                                const uint8_t gamma[256], qa_capture_result *, qa_error *);
typedef struct qa_capture_clock { double milliseconds; bool capture; } qa_capture_clock;
bool qa_capture_frame_time(double elapsed_ms, double fps, float timescale,
                            bool active, bool force, qa_capture_clock *, qa_error *);

typedef struct qa_profiler qa_profiler;
typedef struct qa_timer_report {
    const char *name;
    uint64_t calls;
    double total_ms, self_ms, maximum_ms;
} qa_timer_report;
typedef struct qa_timer_stamp { const char *name; double milliseconds; } qa_timer_stamp;
bool qa_profiler_create(double (*clock_ms)(void *), void *context,
                        size_t stamp_capacity, qa_profiler **, qa_error *);
bool qa_profiler_destroy(qa_profiler *, qa_error *);
bool qa_profiler_enable(qa_profiler *, bool enabled, qa_error *);
bool qa_profiler_reset(qa_profiler *, qa_error *);
bool qa_profiler_push(qa_profiler *, const char *name, qa_error *);
bool qa_profiler_pop(qa_profiler *, qa_error *);
bool qa_profiler_stamp(qa_profiler *, const char *name, qa_error *);
/* Detached snapshots (including copied labels) live in scratch. */
bool qa_profiler_report(const qa_profiler *, qa_arena *, const qa_timer_report **,
                        size_t *, qa_error *);
bool qa_profiler_stamps(const qa_profiler *, qa_arena *, const qa_timer_stamp **,
                        size_t *, qa_error *);
bool qa_profiler_enabled(const qa_profiler *);
bool qa_profiler_idle(const qa_profiler *);

typedef struct qa_debug_line { qa_vec3 start, end; qa_scene_vec4 color; bool depth_test; } qa_debug_line;
typedef enum qa_debug_shape_kind { QA_DEBUG_LINE, QA_DEBUG_POINT, QA_DEBUG_CIRCLE, QA_DEBUG_SPHERE,
    QA_DEBUG_BOUNDS, QA_DEBUG_CYLINDER, QA_DEBUG_ARROW, QA_DEBUG_RAY } qa_debug_shape_kind;
typedef struct qa_debug_shape {
    qa_debug_shape_kind kind;
    union {
        struct { qa_vec3 start, end; } line;
        struct { qa_vec3 origin; float size; } point;
        struct { qa_vec3 origin; float radius; } round;
        qa_bounds bounds;
        struct { qa_vec3 origin; float half_height, radius; } cylinder;
        struct { qa_vec3 start, end; float size; qa_scene_vec4 cap_color; } arrow;
        struct { qa_vec3 origin, direction; float length, size; } ray;
    } data;
} qa_debug_shape;
bool qa_debug_shape_lines(const qa_debug_shape *, qa_scene_vec4, bool depth_test,
                           qa_arena *, const qa_debug_line **, size_t *, qa_error *);
/* Decodes actual API2023 Draw_* imports through checked direct/runner memory.
 * Unknown imports return handled=false. Geometry is detached into scratch;
 * the source caller submits it to the shared store with its server clock. */
bool qa_debug_native_q2(qa_native_instance *, const qa_native_import_call *, qa_arena *,
                         const qa_debug_line **, size_t *, uint32_t *lifetime_ms,
                         bool *handled, qa_error *);
typedef struct qa_debug_store qa_debug_store;
bool qa_debug_store_create(size_t capacity, qa_debug_store **, qa_error *);
void qa_debug_store_destroy(qa_debug_store *);
void qa_debug_store_clear(qa_debug_store *);
bool qa_debug_store_submit(qa_debug_store *, const qa_debug_line *, size_t,
                            double server_ms, uint32_t lifetime_ms, qa_error *);
/* Zero lifetime means one presentation frame, shared by all seats. Detached
 * snapshots live in scratch and can outlive mutation/retirement of the store. */
bool qa_debug_store_snapshot(qa_debug_store *, double server_ms, uint64_t frame,
                              qa_arena *, const qa_debug_line **, size_t *, qa_error *);
bool qa_debug_draw(qa_scene_frame *, const qa_scene_view *, const qa_scene_image *white,
                    const qa_debug_line *, size_t, float width, qa_error *);

typedef struct qa_tools qa_tools;
typedef struct qa_tools_options {
    qa_vfs *files;
    qa_mount_id output_mount;
    uint64_t owner; /* Callback lifetime owner; tool dispatch uses engine owner zero. */
    void *context;
    double (*milliseconds)(void *);
    double (*profiler_milliseconds)(void *); /* Native work clock; NULL uses milliseconds. */
    /* Ordinary frontend capture of the just-presented drawable, top-down owned
     * RGBA8. Called once per pending batch after presentation, never dispatch. */
    bool (*read_frame)(void *, qa_image *, qa_error *);
    bool (*context_active)(void *, const qa_command_context *);
    const char *(*map_name)(void *);
    void (*print)(void *, const qa_command_context *, const char *);
    bool (*forward)(void *, const qa_command_invocation *, qa_error *);
    /* Actual application/renderer/resource owner reports. Return owned UTF-8
     * text, NUL terminated with size excluding NUL; freed after console print.
     * Called synchronously during dispatch, without running resource queues. */
    bool (*diagnostic)(void *, const qa_command_invocation *, qa_buffer *, qa_error *);
    /* Optional source-specific resource/output route. Borrowed owners remain
     * alive throughout this callback scope. Zero output mount permits reads. */
    bool (*files_for_context)(void *, const qa_command_context *, qa_vfs **,
                               qa_mount_id *, qa_error *);
    bool (*capture_context)(void *, const qa_command_context *, qa_command_context *, qa_error *);
} qa_tools_options;
/* Borrowed services/console owners outlive tools or are explicitly detached.
 * Tools retain authored cameras and pending capture requests, not game state. */
bool qa_tools_create(const qa_tools_options *, qa_tools **, qa_error *);
/* Independent profiler/debug/camera owners for detached source construction.
 * Files and clock context remain borrowed. No console dispatch or capture
 * output service is installed; those admissions reject this owner. */
bool qa_tools_create_diagnostics(qa_vfs *, uint64_t owner, double (*milliseconds)(void *),
                                  void *context, qa_tools **, qa_error *);
bool qa_tools_destroy(qa_tools *, qa_error *);
bool qa_tools_callbacks_idle(const qa_tools *);
bool qa_tools_attach_console(qa_tools *, qa_console *, qa_error *);
bool qa_tools_detach_console(qa_tools *, qa_console *, qa_error *);
/* Travel cancels requests that have not seen their requested frame and resets
 * camera playback/document. Completed writes have already returned. */
bool qa_tools_before_world_change(qa_tools *, qa_error *);
bool qa_tools_set_files(qa_tools *, qa_vfs *, qa_mount_id output_mount, qa_error *);
bool qa_tools_set_camera(qa_tools *, qa_camera_document *, qa_error *);
bool qa_tools_start_camera(qa_tools *, qa_error *);
bool qa_tools_stop_camera(qa_tools *, qa_error *);
bool qa_tools_apply_camera(qa_tools *, const qa_scene_view *, bool portal,
                            qa_arena *, qa_scene_view *, qa_error *);
bool qa_tools_capture_frame(qa_tools *, const qa_command_context *, qa_error *);
/* The source callback queues the actual current map's levelshot after four
 * command-wait boundaries, retaining the admitted source context and each
 * request's remaining presented-frame delay through ordinary save/delivery. */
bool qa_tools_capture_levelshot(qa_tools *, const qa_command_context *, qa_error *);
bool qa_tools_pending_capture(const qa_tools *);
/* Call after backend presentation and before travel. Each queued request saves
 * the same immutable presented frame, with ordinary context generation checks. */
bool qa_tools_after_present(qa_tools *, qa_error *);
qa_profiler *qa_tools_profiler(qa_tools *);
qa_debug_store *qa_tools_debug(qa_tools *);
#endif
