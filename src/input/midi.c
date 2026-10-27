/* Port of src/input/midi.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/input/midi.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/input/device_settings.h"
#include "qa/input/source_midi.h"

void qa_midi_devices_free(qa_midi_device *devices, size_t count) {
  if (devices == NULL) return;
  for (size_t i = 0; i < count; i++) {
    free(devices[i].name);
    free(devices[i].path);
  }
  free(devices);
}

void qa_midi_error_dispose(qa_midi_error *error) {
  free(error->code);
  free(error->message);
  memset(error, 0, sizeof *error);
}

static void set_error(qa_midi_error *error, const char *code, const char *message) {
  error->code = code != NULL ? qa_input_dup(code) : NULL;
  error->message = qa_input_dup(message != NULL ? message : "");
}

static const char *errno_code(int value) {
  switch (value) {
    case ENOENT: return "ENOENT";
    case EAGAIN: return "EAGAIN";
    case EINTR: return "EINTR";
    case EACCES: return "EACCES";
    case ENOTDIR: return "ENOTDIR";
    case EINVAL: return "EINVAL";
    case EIO: return "EIO";
    default: return "EIO";
  }
}

static bool system_read_directory(void *ctx, char ***out_names, size_t *out_count,
                                  qa_midi_error *error) {
  (void)ctx;
  DIR *directory = opendir("/dev/snd");
  if (directory == NULL) {
    set_error(error, errno_code(errno), strerror(errno));
    return false;
  }
  char **names = NULL;
  size_t count = 0, capacity = 0;
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    if (count == capacity) {
      size_t grown = capacity == 0 ? 16 : capacity * 2;
      char **next = (char **)realloc(names, grown * sizeof *next);
      if (next == NULL) break;
      names = next;
      capacity = grown;
    }
    names[count] = qa_input_dup(entry->d_name);
    if (names[count] == NULL) break;
    count++;
  }
  closedir(directory);
  *out_names = names;
  *out_count = count;
  return true;
}

static bool system_open(void *ctx, const char *path, int flags, int *out_fd,
                        qa_midi_error *error) {
  (void)ctx;
  int fd = open(path, flags);
  if (fd < 0) {
    set_error(error, errno_code(errno), strerror(errno));
    return false;
  }
  *out_fd = fd;
  return true;
}

static bool system_is_character_device(void *ctx, int fd, bool *out,
                                       qa_midi_error *error) {
  (void)ctx;
  struct stat info;
  if (fstat(fd, &info) != 0) {
    set_error(error, errno_code(errno), strerror(errno));
    return false;
  }
  *out = S_ISCHR(info.st_mode);
  return true;
}

static bool system_read(void *ctx, int fd, uint8_t *bytes, size_t length,
                        int64_t *out_count, qa_midi_error *error) {
  (void)ctx;
  ssize_t count = read(fd, bytes, length);
  if (count < 0) {
    set_error(error, errno_code(errno), strerror(errno));
    return false;
  }
  *out_count = count;
  return true;
}

static void system_close_file(void *ctx, int fd) {
  (void)ctx;
  (void)close(fd);
}

static const qa_midi_file_io system_midi_files = {
    NULL, system_read_directory, system_open, system_is_character_device,
    system_read, system_close_file};

typedef struct raw_midi_handle {
  int fd;
  qa_midi_file_io io;
  bool closed;
  bool has_pending;
  int pending;
} raw_midi_handle;

static bool raw_read(void *ctx, uint8_t *bytes, size_t length, int64_t *out_count,
                     char **out_error_message) {
  raw_midi_handle *handle = (raw_midi_handle *)ctx;
  if (handle->closed) {
    *out_error_message = qa_input_dup("MIDI input handle is closed");
    return false;
  }
  if (length == 0) {
    *out_error_message = qa_input_dup("MIDI read requires a nonempty buffer");
    return false;
  }
  if (handle->has_pending) {
    bytes[0] = (uint8_t)handle->pending;
    handle->has_pending = false;
    *out_count = 1;
    return true;
  }
  qa_midi_error error;
  memset(&error, 0, sizeof error);
  int64_t count = 0;
  if (!handle->io.read(handle->io.ctx, handle->fd, bytes, length, &count, &error)) {
    const char *code = error.code;
    if (code != NULL && (strcmp(code, "EAGAIN") == 0 ||
                         strcmp(code, "EWOULDBLOCK") == 0 ||
                         strcmp(code, "EINTR") == 0)) {
      qa_midi_error_dispose(&error);
      *out_count = 0;
      return true;
    }
    *out_error_message = error.message != NULL ? error.message : qa_input_dup("");
    free(error.code);
    return false;
  }
  if (count < 0 || count > (int64_t)length) {
    *out_error_message = qa_input_dup("Invalid MIDI read length");
    return false;
  }
  if (count == 0) {
    *out_error_message = qa_input_dup("MIDI input reached end of stream");
    return false;
  }
  *out_count = count;
  return true;
}

static void raw_close(void *ctx) {
  raw_midi_handle *handle = (raw_midi_handle *)ctx;
  if (handle->closed) return;
  handle->closed = true;
  handle->io.close(handle->io.ctx, handle->fd);
}

static void raw_destroy(void *ctx) {
  raw_midi_handle *handle = (raw_midi_handle *)ctx;
  raw_close(handle);
  free(handle);
}

struct qa_linux_midi_input_boundary {
  qa_midi_file_io io;
};

qa_linux_midi_input_boundary *qa_linux_midi_input_boundary_create(
    const qa_midi_file_io *io) {
  qa_linux_midi_input_boundary *boundary =
      (qa_linux_midi_input_boundary *)calloc(1, sizeof *boundary);
  if (boundary == NULL) return NULL;
  boundary->io = io != NULL ? *io : system_midi_files;
  return boundary;
}

void qa_linux_midi_input_boundary_destroy(qa_linux_midi_input_boundary *boundary) {
  free(boundary);
}

typedef struct device_entry {
  double card;
  double device;
  const char *name;
} device_entry;

static int compare_devices(const void *left, const void *right) {
  const device_entry *a = (const device_entry *)left;
  const device_entry *b = (const device_entry *)right;
  if (a->card != b->card) return a->card < b->card ? -1 : 1;
  if (a->device != b->device) return a->device < b->device ? -1 : 1;
  return 0;
}

static bool parse_midi_name(const char *name, double *out_card, double *out_device) {
  /* /^midiC([0-9]+)D([0-9]+)$/ */
  if (strncmp(name, "midiC", 5) != 0) return false;
  const char *at = name + 5;
  if (*at < '0' || *at > '9') return false;
  double card = 0;
  while (*at >= '0' && *at <= '9') card = card * 10 + (*at++ - '0');
  if (*at != 'D') return false;
  at++;
  if (*at < '0' || *at > '9') return false;
  double device = 0;
  while (*at >= '0' && *at <= '9') device = device * 10 + (*at++ - '0');
  if (*at != '\0') return false;
  *out_card = card;
  *out_device = device;
  return true;
}

static bool safe_integer(double value) {
  return value == trunc(value) && value <= 9007199254740991.0 &&
         value >= -9007199254740991.0;
}

bool qa_linux_midi_input_boundary_list(qa_linux_midi_input_boundary *boundary,
                                       qa_midi_device **out_devices,
                                       size_t *out_count,
                                       char **out_error_message) {
  *out_devices = NULL;
  *out_count = 0;
#if !defined(__linux__)
  (void)boundary;
  *out_error_message =
      qa_input_dup("MIDI input currently requires Linux ALSA raw MIDI");
  return false;
#else
  qa_midi_error error;
  memset(&error, 0, sizeof error);
  char **names = NULL;
  size_t name_count = 0;
  if (!boundary->io.read_directory(boundary->io.ctx, &names, &name_count, &error)) {
    if (error.code != NULL && strcmp(error.code, "ENOENT") == 0) {
      qa_midi_error_dispose(&error);
      return true;
    }
    *out_error_message = error.message != NULL ? error.message : qa_input_dup("");
    free(error.code);
    return false;
  }
  device_entry *entries = (device_entry *)calloc(
      name_count != 0 ? name_count : 1, sizeof *entries);
  size_t entry_count = 0;
  if (entries != NULL) {
    for (size_t i = 0; i < name_count; i++) {
      double card, device;
      if (!parse_midi_name(names[i], &card, &device)) continue;
      if (!safe_integer(card) || !safe_integer(device)) continue;
      entries[entry_count].card = card;
      entries[entry_count].device = device;
      entries[entry_count].name = names[i];
      entry_count++;
    }
    qsort(entries, entry_count, sizeof *entries, compare_devices);
  }
  qa_midi_device *devices = NULL;
  if (entry_count != 0) {
    devices = (qa_midi_device *)calloc(entry_count, sizeof *devices);
    if (devices != NULL) {
      for (size_t i = 0; i < entry_count; i++) {
        char display[256], path[256];
        snprintf(display, sizeof display, "ALSA %s", entries[i].name);
        snprintf(path, sizeof path, "/dev/snd/%s", entries[i].name);
        devices[i].name = qa_input_dup(display);
        devices[i].path = qa_input_dup(path);
      }
    }
  }
  free(entries);
  for (size_t i = 0; i < name_count; i++) free(names[i]);
  free(names);
  *out_devices = devices;
  *out_count = entry_count;
  return true;
#endif
}

static bool valid_device_path(const char *path) {
  /* /^\/dev\/snd\/midiC[0-9]+D[0-9]+$/ */
  if (strncmp(path, "/dev/snd/", 9) != 0) return false;
  double card, device;
  return parse_midi_name(path + 9, &card, &device);
}

bool qa_linux_midi_input_boundary_open(qa_linux_midi_input_boundary *boundary,
                                       const qa_midi_device *device,
                                       qa_midi_input_handle *out_handle,
                                       char **out_error_message) {
#if !defined(__linux__)
  (void)boundary;
  (void)device;
  (void)out_handle;
  *out_error_message =
      qa_input_dup("MIDI input currently requires Linux ALSA raw MIDI");
  return false;
#else
  if (!valid_device_path(device->path)) {
    *out_error_message = qa_input_dup("Invalid ALSA MIDI device path");
    return false;
  }
  qa_midi_error error;
  memset(&error, 0, sizeof error);
  int fd = -1;
  /* Linux sound/core/rawmidi.c: O_NONBLOCK applies to both open
   * and read; read starts input capture. */
  if (!boundary->io.open(boundary->io.ctx, device->path,
                         O_RDONLY | O_NONBLOCK | O_NOFOLLOW, &fd, &error)) {
    *out_error_message = error.message != NULL ? error.message : qa_input_dup("");
    free(error.code);
    return false;
  }
  bool character = false;
  if (!boundary->io.is_character_device(boundary->io.ctx, fd, &character, &error) ||
      !character) {
    boundary->io.close(boundary->io.ctx, fd);
    if (error.message != NULL) {
      *out_error_message = error.message;
      free(error.code);
    } else {
      *out_error_message = qa_input_dup("ALSA MIDI path is not a character device");
    }
    return false;
  }
  raw_midi_handle *handle = (raw_midi_handle *)calloc(1, sizeof *handle);
  if (handle == NULL) {
    boundary->io.close(boundary->io.ctx, fd);
    *out_error_message = qa_input_dup("MIDI input allocation failed");
    return false;
  }
  handle->fd = fd;
  handle->io = boundary->io;
  /* start(): the first read begins capture. */
  uint8_t first = 0;
  int64_t count = 0;
  char *message = NULL;
  if (!raw_read(handle, &first, 1, &count, &message)) {
    boundary->io.close(boundary->io.ctx, fd);
    free(handle);
    *out_error_message = message;
    return false;
  }
  if (count != 0) {
    handle->has_pending = true;
    handle->pending = first;
  }
  out_handle->ctx = handle;
  out_handle->read = raw_read;
  out_handle->close = raw_close;
  out_handle->destroy = raw_destroy;
  return true;
#endif
}

static bool linux_list(void *ctx, qa_midi_device **out_devices, size_t *out_count,
                       char **out_error_message) {
  return qa_linux_midi_input_boundary_list(
      (qa_linux_midi_input_boundary *)ctx, out_devices, out_count,
      out_error_message);
}

static bool linux_open(void *ctx, const qa_midi_device *device,
                       qa_midi_input_handle *out_handle, char **out_error_message) {
  return qa_linux_midi_input_boundary_open(
      (qa_linux_midi_input_boundary *)ctx, device, out_handle, out_error_message);
}

qa_midi_input_boundary qa_midi_input_boundary_from_linux(
    qa_linux_midi_input_boundary *boundary) {
  qa_midi_input_boundary view = {boundary, linux_list, linux_open, NULL};
  return view;
}

struct qa_source_midi_input {
  qa_source_midi_options options;
  qa_midi_input_boundary boundary;
  qa_linux_midi_input_boundary *owned_boundary;
  qa_source_midi_decoder decoder;
  uint8_t buffer[4096];
  qa_midi_device *devices;
  size_t device_count;
  bool has_handle;
  qa_midi_input_handle handle;
  bool closed;
  double held[64];
  size_t held_count;
  double released[64];
  size_t released_count;
  bool has_channel;
  double channel;
};

qa_source_midi_input *qa_source_midi_input_create(
    qa_source_midi_options options, const qa_midi_input_boundary *boundary) {
  qa_source_midi_input *input =
      (qa_source_midi_input *)calloc(1, sizeof *input);
  if (input == NULL) return NULL;
  input->options = options;
  qa_source_midi_decoder_init(&input->decoder);
  if (boundary != NULL) {
    input->boundary = *boundary;
  } else {
    input->owned_boundary = qa_linux_midi_input_boundary_create(NULL);
    if (input->owned_boundary == NULL) {
      free(input);
      return NULL;
    }
    input->boundary = qa_midi_input_boundary_from_linux(input->owned_boundary);
  }
  return input;
}

static void stop_input(qa_source_midi_input *input);

void qa_source_midi_input_destroy(qa_source_midi_input *input) {
  if (input == NULL) return;
  if (!input->closed) {
    input->closed = true;
    stop_input(input);
  }
  qa_midi_devices_free(input->devices, input->device_count);
  if (input->boundary.destroy != NULL) input->boundary.destroy(input->boundary.ctx);
  qa_linux_midi_input_boundary_destroy(input->owned_boundary);
  free(input);
}

bool qa_source_midi_input_connected(const qa_source_midi_input *input) {
  return input->has_handle;
}

qa_midi_device *qa_source_midi_input_available_devices(qa_source_midi_input *input,
                                                       size_t *out_count) {
  qa_midi_device *devices = NULL;
  size_t count = 0;
  char *message = NULL;
  if (!input->boundary.list(input->boundary.ctx, &devices, &count, &message)) {
    qa_input_error("%s", message != NULL ? message : "");
  }
  *out_count = count;
  return devices;
}

static bool cvar_snapshot(qa_source_midi_input *input, const char *name,
                          qa_cvar_snapshot *out) {
  memset(out, 0, sizeof *out);
  if (!qa_cvar_registry_get(input->options.cvars, name, out)) {
    qa_input_error("MIDI cvar %s no longer exists", name);
  }
  return true;
}

static void require_open(const qa_source_midi_input *input) {
  if (input->closed) qa_input_error("Source MIDI input is closed");
}

static void stop_input(qa_source_midi_input *input) {
  for (size_t i = 0; i < input->held_count; i++) {
    bool present = false;
    for (size_t j = 0; j < input->released_count; j++) {
      present = present || input->released[j] == input->held[i];
    }
    if (!present && input->released_count < 64) {
      input->released[input->released_count++] = input->held[i];
    }
  }
  input->held_count = 0;
  bool had_handle = input->has_handle;
  input->has_handle = false;
  qa_midi_devices_free(input->devices, input->device_count);
  input->devices = NULL;
  input->device_count = 0;
  qa_source_midi_decoder_reset(&input->decoder);
  if (had_handle) {
    input->handle.close(input->handle.ctx);
    input->handle.destroy(input->handle.ctx);
  }
}

void qa_source_midi_input_initialize(qa_source_midi_input *input) {
  require_open(input);
  qa_register_midi_settings(input->options.cvars);
  stop_input(input);
  qa_cvar_snapshot snapshot;
  cvar_snapshot(input, "in_midi", &snapshot);
  double enabled = snapshot.numeric_value;
  qa_cvar_read_free(&snapshot);
  if (enabled == 0) return;
  cvar_snapshot(input, "in_mididevice", &snapshot);
  double selected = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  char *message = NULL;
  qa_midi_device *devices = NULL;
  size_t count = 0;
  char failure[1024];
  bool failed = false;
  if (!input->boundary.list(input->boundary.ctx, &devices, &count, &message)) {
    snprintf(failure, sizeof failure, "%s", message != NULL ? message : "");
    failed = true;
  } else {
    input->devices = devices;
    input->device_count = count;
    if (selected != trunc(selected) || selected < 0 ||
        selected >= (double)count) {
      char *count_text = qa_input_js_number((double)count);
      snprintf(failure, sizeof failure,
               "device index is outside %s available devices",
               count_text != NULL ? count_text : "");
      free(count_text);
      failed = true;
    } else {
      qa_midi_input_handle handle;
      memset(&handle, 0, sizeof handle);
      if (!input->boundary.open(input->boundary.ctx, &devices[(size_t)selected],
                                &handle, &message)) {
        snprintf(failure, sizeof failure, "%s", message != NULL ? message : "");
        failed = true;
      } else {
        input->handle = handle;
        input->has_handle = true;
      }
    }
  }
  free(message);
  if (failed) {
    char *selected_text = qa_input_js_number(selected);
    size_t length = strlen(failure) + 128;
    char *line = (char *)malloc(length);
    if (line != NULL) {
      snprintf(line, length, "WARNING: could not open MIDI device %s: %s\n",
               selected_text != NULL ? selected_text : "", failure);
      input->options.print(input->options.print_ctx, line);
      free(line);
    }
    free(selected_text);
  }
}

void qa_source_midi_input_restart(qa_source_midi_input *input) {
  qa_source_midi_input_initialize(input);
}

typedef struct feed_ctx {
  qa_source_midi_input *input;
  qa_queue_key_fn queue_key;
  void *queue_ctx;
} feed_ctx;

static void feed_key(void *ctx, double key, bool down, double time) {
  feed_ctx *feed = (feed_ctx *)ctx;
  qa_source_midi_input *input = feed->input;
  if (down) {
    bool present = false;
    for (size_t i = 0; i < input->held_count; i++) {
      present = present || input->held[i] == key;
    }
    if (!present && input->held_count < 64) {
      input->held[input->held_count++] = key;
    }
  } else {
    for (size_t i = 0; i < input->held_count; i++) {
      if (input->held[i] == key) {
        memmove(&input->held[i], &input->held[i + 1],
                (input->held_count - i - 1) * sizeof *input->held);
        input->held_count--;
        break;
      }
    }
  }
  feed->queue_key(feed->queue_ctx, key, down, time);
}

void qa_source_midi_input_release(qa_source_midi_input *input, double time,
                                  qa_queue_key_fn queue_key, void *queue_ctx) {
  double keys[128];
  size_t count = 0;
  for (size_t i = 0; i < input->held_count; i++) keys[count++] = input->held[i];
  for (size_t i = 0; i < input->released_count; i++) {
    bool present = false;
    for (size_t j = 0; j < count; j++) present = present || keys[j] == input->released[i];
    if (!present) keys[count++] = input->released[i];
  }
  for (size_t i = 0; i < count; i++) queue_key(queue_ctx, keys[i], false, time);
  input->held_count = 0;
  input->released_count = 0;
  qa_source_midi_decoder_reset(&input->decoder);
}

void qa_source_midi_input_frame(qa_source_midi_input *input,
                                qa_queue_key_fn queue_key, void *queue_ctx,
                                double time) {
  require_open(input);
  qa_cvar_snapshot snapshot;
  cvar_snapshot(input, "in_midichannel", &snapshot);
  double channel = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  if (!input->has_channel || channel != input->channel) {
    qa_source_midi_input_release(input, time, queue_key, queue_ctx);
    input->channel = channel;
    input->has_channel = true;
  }
  for (size_t i = 0; i < input->released_count; i++) {
    queue_key(queue_ctx, input->released[i], false, time);
  }
  input->released_count = 0;
  /* Bound one frame's reads even when a virtual MIDI producer
   * never becomes idle. */
  for (int reads = 0; reads < 16; reads++) {
    if (!input->has_handle) return;
    int64_t count = 0;
    char *message = NULL;
    if (!input->handle.read(input->handle.ctx, input->buffer, sizeof input->buffer,
                            &count, &message)) {
      stop_input(input);
      qa_source_midi_input_release(input, time, queue_key, queue_ctx);
      size_t length = (message != NULL ? strlen(message) : 0) + 64;
      char *line = (char *)malloc(length);
      if (line != NULL) {
        snprintf(line, length, "WARNING: MIDI input stopped: %s\n",
                 message != NULL ? message : "");
        input->options.print(input->options.print_ctx, line);
        free(line);
      }
      free(message);
      return;
    }
    free(message);
    if (count < 0 || count > (int64_t)sizeof input->buffer) {
      qa_input_error("Invalid MIDI input boundary read length");
    }
    if (count == 0) return;
    feed_ctx feed = {input, queue_key, queue_ctx};
    qa_source_midi_decoder_feed(&input->decoder, input->buffer, (size_t)count,
                                channel, time, feed_key, &feed);
  }
}

void qa_source_midi_input_info(qa_source_midi_input *input) {
  require_open(input);
  qa_command_print_fn print = input->options.print;
  void *ctx = input->options.print_ctx;
  char line[1024];
  qa_cvar_snapshot snapshot;
  cvar_snapshot(input, "in_midi", &snapshot);
  double enabled = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  snprintf(line, sizeof line, "\nMIDI control:       %s\n",
           enabled != 0 ? "enabled" : "disabled");
  print(ctx, line);
  cvar_snapshot(input, "in_midiport", &snapshot);
  {
    char *text = qa_input_js_number(snapshot.integer_value);
    snprintf(line, sizeof line, "port:               %s\n",
             text != NULL ? text : "");
    free(text);
  }
  qa_cvar_read_free(&snapshot);
  print(ctx, line);
  cvar_snapshot(input, "in_midichannel", &snapshot);
  {
    char *text = qa_input_js_number(snapshot.integer_value);
    snprintf(line, sizeof line, "channel:            %s\n",
             text != NULL ? text : "");
    free(text);
  }
  qa_cvar_read_free(&snapshot);
  print(ctx, line);
  cvar_snapshot(input, "in_mididevice", &snapshot);
  double selected_integer = snapshot.integer_value;
  double selected_numeric = snapshot.numeric_value;
  {
    char *text = qa_input_js_number(selected_integer);
    snprintf(line, sizeof line, "current device:     %s\n",
             text != NULL ? text : "");
    free(text);
  }
  qa_cvar_read_free(&snapshot);
  print(ctx, line);
  {
    char *text = qa_input_js_number((double)input->device_count);
    snprintf(line, sizeof line, "number of devices:  %s\n",
             text != NULL ? text : "");
    free(text);
  }
  print(ctx, line);
  for (size_t i = 0; i < input->device_count; i++) {
    char *index_text = qa_input_js_number((double)i);
    char padded[32];
    const char *digits = index_text != NULL ? index_text : "";
    size_t length = strlen(digits);
    if (length < 2) snprintf(padded, sizeof padded, "%*s%s", (int)(2 - length), "", digits);
    else snprintf(padded, sizeof padded, "%s", digits);
    snprintf(line, sizeof line, "%sdevice %s:       %s\n",
             (double)i == selected_numeric ? "***" : "...", padded,
             input->devices[i].name);
    print(ctx, line);
    free(index_text);
    snprintf(line, sizeof line,
             "...system path:     %s\n...manufacturer/product IDs: unavailable "
             "through Linux raw MIDI\n\n",
             input->devices[i].path);
    print(ctx, line);
  }
}

void qa_source_midi_input_close(qa_source_midi_input *input) {
  if (input->closed) return;
  input->closed = true;
  stop_input(input);
}
