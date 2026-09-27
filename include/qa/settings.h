#ifndef QA_SETTINGS_H
#define QA_SETTINGS_H

#include "qa/input_platform.h"

typedef struct qa_seat_settings {
    qa_input_binding *bindings;
    size_t binding_count;
    qa_gamepad_tuning gamepad;
    qa_mouse_tuning mouse;
    char **history;
    size_t history_count;
    qa_controller_selection controller;
    bool has_always_run, always_run, rumble;
    float rumble_strength;
} qa_seat_settings;
/* Parsed documents own every string/array. Encoding borrows an ordinary typed
 * view; do not free caller-owned input with these destruction functions. */
bool qa_seat_settings_parse(qa_bytes, qa_seat_settings *, qa_error *);
bool qa_seat_settings_encode(const qa_seat_settings *, qa_buffer *, qa_error *);
void qa_seat_settings_free(qa_seat_settings *);

typedef struct qa_gyro_profile {
    bool device, enabled;
    char guid[33];
    const char *serial;
    qa_gyro_yaw_axis yaw_axis;
    float yaw_sensitivity, pitch_sensitivity;
} qa_gyro_profile;
bool qa_gyro_profile_parse(qa_bytes, qa_gyro_profile *, qa_error *);
bool qa_gyro_profile_encode(const qa_gyro_profile *, qa_buffer *, qa_error *);
void qa_gyro_profile_free(qa_gyro_profile *);

/* The selected mount is the writable settings root. Reads cannot accidentally
 * load an identically named archive entry; writes use VFS atomic replacement. */
typedef struct qa_settings_store {
    qa_vfs *vfs;
    qa_mount_id mount;
} qa_settings_store;
bool qa_settings_read(qa_settings_store, const char *path, qa_resource **out, bool *found,
                      qa_error *);
bool qa_settings_write(qa_settings_store, const char *path, qa_bytes, qa_error *);
bool qa_settings_load_seat(qa_settings_store, const char *, qa_seat_settings *, bool *found,
                           qa_error *);
bool qa_settings_save_seat(qa_settings_store, const char *, const qa_seat_settings *, qa_error *);
bool qa_settings_load_gyro(qa_settings_store, const char *, qa_gyro_profile *, bool *found,
                           qa_error *);
bool qa_settings_save_gyro(qa_settings_store, const char *, const qa_gyro_profile *, qa_error *);
bool qa_settings_load_routing(qa_settings_store, const char *, int *keyboard_seat, bool *found,
                              qa_error *);
bool qa_settings_save_routing(qa_settings_store, const char *, int keyboard_seat, qa_error *);
bool qa_settings_save_config(qa_settings_store, const char *, const qa_cvars *,
                             const qa_input_seat *, bool controllers, qa_error *);
bool qa_settings_execute(qa_settings_store, const char *, qa_console *, const qa_command_context *,
                         qa_error *);

typedef struct qa_cvar_archive_entry {
    char *name, *value;
} qa_cvar_archive_entry;
typedef struct qa_cvar_archive {
    qa_cvar_archive_entry *entries;
    size_t count;
} qa_cvar_archive;
void qa_cvar_archive_free(qa_cvar_archive *);
bool qa_settings_load_cvars(qa_settings_store, const char *const *owner, size_t owner_count,
                            qa_console_dialect, qa_cvar_archive *, qa_error *);
bool qa_settings_save_cvars(qa_settings_store, const char *const *owner, size_t owner_count,
                            const qa_cvars *, qa_error *);
/* Apply typed values directly, never interpreting semicolons/newlines as commands.
 * Startup applies archives before source defaults to preserve console-created flags. */
bool qa_cvar_archive_apply(qa_cvars *, const qa_cvar_archive *, qa_error *);

typedef enum qa_restart_kind {
    QA_RESTART_VIDEO,
    QA_RESTART_INPUT,
    QA_RESTART_AUDIO
} qa_restart_kind;
typedef struct qa_restart_service {
    void *context;
    bool (*restart)(void *, qa_error *);
    const char *const *latched;
    size_t latched_count;
} qa_restart_service;
typedef struct qa_restart_controls qa_restart_controls;
qa_restart_controls *qa_restart_create(qa_cvars *, const qa_restart_service services[3],
                                       qa_error *);
/* Controls are destroyed before their bound console and cvar registry. */
bool qa_restart_destroy(qa_restart_controls *, qa_error *);
bool qa_restart_request(qa_restart_controls *, qa_restart_kind, qa_error *);
bool qa_restart_drain(qa_restart_controls *, qa_error *);
bool qa_restart_register(qa_restart_controls *, qa_console *, uint64_t owner, qa_error *);
#endif
