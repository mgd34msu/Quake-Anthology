/*
 * Port of src/input/midi.ts: win32/win_input.c IN_StartupMIDI,
 * IN_ShutdownMIDI and MidiInfo_f over Linux ALSA raw MIDI.
 * C accommodations: node:fs becomes POSIX syscalls behind the
 * same MidiFileIo boundary; thrown errors cross the boundary as
 * (code, message) pairs because SourceMidiInput catches device
 * errors, while the standalone boundary entry points abort with
 * the TS messages. GPL-2.0-or-later.
 */
#ifndef QA_INPUT_MIDI_H
#define QA_INPUT_MIDI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/core/commands/text.h"
#include "qa/core/cvars/index.h"
#include "qa/input/support.h"

/** TS MidiDevice; strings are owned by the list. */
typedef struct qa_midi_device {
  char *name;
  char *path;
} qa_midi_device;

void qa_midi_devices_free(qa_midi_device *devices, size_t count);

/** A caught TS error at the file boundary (owned strings). */
typedef struct qa_midi_error {
  char *code;    /* NULL when the TS error carries no string code */
  char *message;
} qa_midi_error;

void qa_midi_error_dispose(qa_midi_error *error);

/** TS MidiFileIo. False returns fill error. */
typedef struct qa_midi_file_io {
  void *ctx;
  bool (*read_directory)(void *ctx, char ***out_names, size_t *out_count,
                         qa_midi_error *error);
  bool (*open)(void *ctx, const char *path, int flags, int *out_fd,
               qa_midi_error *error);
  bool (*is_character_device)(void *ctx, int fd, bool *out, qa_midi_error *error);
  bool (*read)(void *ctx, int fd, uint8_t *bytes, size_t length,
               int64_t *out_count, qa_midi_error *error);
  void (*close)(void *ctx, int fd);
} qa_midi_file_io;

/** TS MidiInputHandle. read fills error_message (malloc'd) and
 * returns false where the TS throws; destroy frees ctx. */
typedef struct qa_midi_input_handle {
  void *ctx;
  bool (*read)(void *ctx, uint8_t *bytes, size_t length, int64_t *out_count,
               char **out_error_message);
  void (*close)(void *ctx);
  void (*destroy)(void *ctx);
} qa_midi_input_handle;

/** TS MidiInputBoundary. list/open return false with a malloc'd
 * error message where the TS throws. */
typedef struct qa_midi_input_boundary {
  void *ctx;
  bool (*list)(void *ctx, qa_midi_device **out_devices, size_t *out_count,
               char **out_error_message);
  bool (*open)(void *ctx, const qa_midi_device *device,
               qa_midi_input_handle *out_handle, char **out_error_message);
  void (*destroy)(void *ctx);
} qa_midi_input_boundary;

typedef struct qa_linux_midi_input_boundary qa_linux_midi_input_boundary;

/* io NULL selects the system files. NULL only on OOM. */
qa_linux_midi_input_boundary *qa_linux_midi_input_boundary_create(
    const qa_midi_file_io *io);
void qa_linux_midi_input_boundary_destroy(qa_linux_midi_input_boundary *boundary);
bool qa_linux_midi_input_boundary_list(qa_linux_midi_input_boundary *boundary,
                                       qa_midi_device **out_devices,
                                       size_t *out_count,
                                       char **out_error_message);
bool qa_linux_midi_input_boundary_open(qa_linux_midi_input_boundary *boundary,
                                       const qa_midi_device *device,
                                       qa_midi_input_handle *out_handle,
                                       char **out_error_message);
/* Boundary view borrowing the Linux boundary (destroy NULL). */
qa_midi_input_boundary qa_midi_input_boundary_from_linux(
    qa_linux_midi_input_boundary *boundary);

/** TS SourceMidiOptions. */
typedef struct qa_source_midi_options {
  qa_cvar_registry *cvars;
  qa_command_print_fn print;
  void *print_ctx;
} qa_source_midi_options;

typedef struct qa_source_midi_input qa_source_midi_input;

/* Construction performs no device access. boundary NULL creates
 * an owned Linux boundary. NULL only on allocation failure. */
qa_source_midi_input *qa_source_midi_input_create(
    qa_source_midi_options options, const qa_midi_input_boundary *boundary);
void qa_source_midi_input_destroy(qa_source_midi_input *input);
bool qa_source_midi_input_connected(const qa_source_midi_input *input);
/* Malloc'd devices; aborts where the TS list() throws. */
qa_midi_device *qa_source_midi_input_available_devices(
    qa_source_midi_input *input, size_t *out_count);
void qa_source_midi_input_initialize(qa_source_midi_input *input);
void qa_source_midi_input_restart(qa_source_midi_input *input);
/* time mirrors the TS default of 0. */
void qa_source_midi_input_frame(qa_source_midi_input *input,
                                qa_queue_key_fn queue_key, void *queue_ctx,
                                double time);
void qa_source_midi_input_info(qa_source_midi_input *input);
void qa_source_midi_input_release(qa_source_midi_input *input, double time,
                                  qa_queue_key_fn queue_key, void *queue_ctx);
void qa_source_midi_input_close(qa_source_midi_input *input);

#endif
