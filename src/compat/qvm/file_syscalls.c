/* Port of src/compat/qvm/file-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/file_syscalls.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/content/mounts.h"
#include "qa/content/roots.h"
#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/persistence/recipe.h"
#include "qa/platform/files/contained.h"

typedef struct file_handle {
  bool occupied, is_write;
  /* read */
  uint8_t *bytes; /* owned */
  size_t byte_length;
  int64_t position;
  bool is_pk3_archive;
  qa_resolved_resource_reference reference; /* strings borrowed */
  /* write */
  qa_writable_binary_file file;
} file_handle;

struct qa_qvm_files {
  qa_qvm_file_services services;
  file_handle handles[64]; /* slots 1..63 */
  bool closed;
};

static bool is_integer(double v) { return isfinite(v) && v == trunc(v); }

static void writable_print(const char *text, void *ctx) {
  qa_qvm_files *files = ctx;
  if (files->services.print != NULL) files->services.print(files->services.ctx, text);
  qa_qvm_files_assert_current(files);
}

static bool reference_is_pk3(const qa_resolved_resource_reference *reference) {
  return reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE &&
         reference->provenance.archive_mount != NULL &&
         reference->provenance.archive_mount->format == QA_ARCHIVE_FORMAT_PK3;
}

static void validate_read_identity(const qa_resolved_resource_reference *reference,
                                   const uint8_t *bytes, size_t length, double position,
                                   const qa_save_reader *failure_reader) {
  char *digest = qa_digest_bytes(bytes, length);
  if (digest == NULL) qa_range_error("out of memory");
  bool mismatch = (int64_t)length != reference->byte_length ||
                  strcmp(digest, reference->digest) != 0;
  free(digest);
  if (mismatch) {
    if (failure_reader != NULL)
      qa_save_reader_fail(failure_reader, "opened bytes differ from resource identity");
    qa_range_error("opened bytes differ from resource identity");
  }
  if (reference_is_pk3(reference) && position > (double)length) {
    if (failure_reader != NULL) qa_save_reader_fail(failure_reader, "ZIP cursor exceeds opened bytes");
    qa_range_error("ZIP cursor exceeds opened bytes");
  }
}

qa_qvm_files *qa_qvm_files_create(qa_qvm_file_services services) {
  qa_qvm_files *files = calloc(1, sizeof *files);
  if (files == NULL) return NULL;
  files->services = services;
  return files;
}

void qa_qvm_files_destroy(qa_qvm_files *files) {
  if (files == NULL) return;
  for (int i = 1; i < 64; i++) {
    if (!files->handles[i].occupied) continue;
    if (files->handles[i].is_write) qa_writable_binary_file_close(&files->handles[i].file);
    free(files->handles[i].bytes);
  }
  free(files);
}

qa_qvm_file_services qa_qvm_files_get_services(const qa_qvm_files *files) { return files->services; }

void qa_qvm_files_assert_current(qa_qvm_files *files) {
  if (files->closed) qa_range_error("QVM files have been closed");
  files->services.assert_current(files->services.ctx);
}

void qa_qvm_files_checkpoint_destroy(qa_qvm_files_checkpoint *checkpoint) {
  if (checkpoint == NULL) return;
  for (size_t i = 0; i < checkpoint->handle_count; i++) {
    if (checkpoint->handles[i].is_write)
      qa_writable_file_checkpoint_dispose(&checkpoint->handles[i].file);
    free(checkpoint->handles[i].bytes);
  }
  free(checkpoint->handles);
  checkpoint->handles = NULL;
  checkpoint->handle_count = 0;
}

bool qa_qvm_files_capture_checkpoint(qa_qvm_files *files, qa_qvm_files_checkpoint *out) {
  qa_qvm_files_assert_current(files);
  out->handles = NULL;
  out->handle_count = 0;
  size_t count = 0;
  for (int i = 1; i < 64; i++) count += files->handles[i].occupied ? 1 : 0;
  if (count > 0) {
    out->handles = calloc(count, sizeof *out->handles);
    if (out->handles == NULL) return false;
  }
  size_t index = 0;
  for (int slot = 1; slot < 64; slot++) {
    file_handle *handle = &files->handles[slot];
    if (!handle->occupied) continue;
    qa_qvm_file_handle_checkpoint *entry = &out->handles[index];
    entry->slot = slot;
    entry->is_write = handle->is_write;
    if (handle->is_write) {
      if (!qa_writable_binary_file_capture_checkpoint(&handle->file, &entry->file)) {
        out->handle_count = index;
        qa_qvm_files_checkpoint_destroy(out);
        return false;
      }
      size_t part_count = 0;
      char **parts = qa_contained_file_parts(entry->file.path, &part_count);
      if (parts == NULL) {
        out->handle_count = index + 1;
        qa_qvm_files_checkpoint_destroy(out);
        return false;
      }
      qa_contained_file_parts_free(parts, part_count);
    } else {
      entry->reference = handle->reference;
      entry->bytes = malloc(handle->byte_length == 0 ? 1 : handle->byte_length);
      if (entry->bytes == NULL) {
        out->handle_count = index;
        qa_qvm_files_checkpoint_destroy(out);
        return false;
      }
      memcpy(entry->bytes, handle->bytes, handle->byte_length);
      entry->byte_length = handle->byte_length;
      entry->position = (double)handle->position;
      validate_read_identity(&entry->reference, entry->bytes, entry->byte_length,
                             entry->position, NULL);
    }
    index++;
  }
  out->handle_count = index;
  return true;
}

static void reader_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}

static bool read_files_checkpoint(const qa_save_value *value, qa_qvm_files_checkpoint *out) {
  qa_save_reader reader, handles;
  if (!qa_save_reader_init(&reader, value, "qvm-files")) return false;
  reader_field(&reader, "handles", &handles);
  size_t count = qa_save_reader_list_count(&handles);
  out->handles = NULL;
  out->handle_count = 0;
  if (count > 0) {
    out->handles = calloc(count, sizeof *out->handles);
    if (out->handles == NULL) {
      qa_save_reader_destroy(&handles);
      qa_save_reader_destroy(&reader);
      return false;
    }
  }
  bool seen[64] = {false};
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item, slot_reader, kind_reader;
    if (!qa_save_reader_item(&handles, i, &item)) qa_range_error("out of memory");
    reader_field(&item, "slot", &slot_reader);
    double slot = qa_save_reader_integer_min(&slot_reader, 1);
    qa_save_reader_destroy(&slot_reader);
    if (slot > 63 || seen[(size_t)slot])
      qa_save_reader_fail(&item, "invalid or duplicate file handle slot");
    seen[(size_t)slot] = true;
    reader_field(&item, "kind", &kind_reader);
    const char *choices[] = {"read", "write"};
    const char *kind = qa_save_reader_choice_string(&kind_reader, choices, 2);
    qa_save_reader_destroy(&kind_reader);
    qa_qvm_file_handle_checkpoint *entry = &out->handles[out->handle_count];
    entry->slot = slot;
    if (strcmp(kind, "write") == 0) {
      entry->is_write = true;
      qa_save_reader file, path_reader, mode_reader, position_reader;
      reader_field(&item, "file", &file);
      reader_field(&file, "path", &path_reader);
      const char *path = qa_save_reader_string(&path_reader);
      size_t part_count = 0;
      char **parts = qa_contained_file_parts(path, &part_count);
      if (parts == NULL) qa_range_error("out of memory");
      qa_contained_file_parts_free(parts, part_count);
      entry->file.path = malloc(strlen(path) + 1);
      if (entry->file.path == NULL) qa_range_error("out of memory");
      strcpy(entry->file.path, path);
      qa_save_reader_destroy(&path_reader);
      reader_field(&file, "mode", &mode_reader);
      const char *modes[] = {"write", "append", "append-sync"};
      const char *mode = qa_save_reader_choice_string(&mode_reader, modes, 3);
      entry->file.mode = strcmp(mode, "write") == 0 ? QA_WRITABLE_WRITE
                         : strcmp(mode, "append") == 0 ? QA_WRITABLE_APPEND
                                                        : QA_WRITABLE_APPEND_SYNC;
      entry->file.mode_valid = true;
      qa_save_reader_destroy(&mode_reader);
      reader_field(&file, "position", &position_reader);
      entry->file.position = (int64_t)qa_save_reader_integer_min(&position_reader, 0);
      qa_save_reader_destroy(&position_reader);
      qa_save_reader_destroy(&file);
    } else {
      qa_save_reader resource, reference_reader, bytes_reader, position_reader;
      reader_field(&item, "resource", &resource);
      reader_field(&resource, "reference", &reference_reader);
      entry->reference = qa_read_resource(&reference_reader);
      qa_save_reader_destroy(&reference_reader);
      reader_field(&resource, "bytes", &bytes_reader);
      size_t length = 0;
      const uint8_t *bytes = qa_save_reader_bytes(&bytes_reader, &length);
      entry->bytes = malloc(length == 0 ? 1 : length);
      if (entry->bytes == NULL) qa_range_error("out of memory");
      memcpy(entry->bytes, bytes, length);
      entry->byte_length = length;
      qa_save_reader_destroy(&bytes_reader);
      reader_field(&item, "position", &position_reader);
      entry->position = qa_save_reader_integer_min(&position_reader, 0);
      qa_save_reader_destroy(&position_reader);
      validate_read_identity(&entry->reference, entry->bytes, entry->byte_length,
                             entry->position, &resource);
      qa_save_reader_destroy(&resource);
    }
    out->handle_count++;
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&handles);
  qa_save_reader_destroy(&reader);
  return true;
}

void qa_qvm_files_restore_checkpoint(qa_qvm_files *files, const qa_save_value *value) {
  qa_qvm_files_assert_current(files);
  for (int i = 1; i < 64; i++)
    if (files->handles[i].occupied) qa_range_error("QVM file restore requires an empty owner");
  qa_qvm_files_checkpoint checkpoint;
  if (!read_files_checkpoint(value, &checkpoint)) qa_range_error("out of memory");
  for (size_t i = 0; i < checkpoint.handle_count; i++) {
    qa_qvm_file_handle_checkpoint *entry = &checkpoint.handles[i];
    file_handle *handle = &files->handles[(size_t)entry->slot];
    handle->occupied = true;
    handle->is_write = entry->is_write;
    if (!entry->is_write) {
      handle->bytes = entry->bytes;
      entry->bytes = NULL;
      handle->byte_length = entry->byte_length;
      handle->position = (int64_t)entry->position;
      handle->reference = entry->reference;
      handle->is_pk3_archive = reference_is_pk3(&entry->reference);
    } else {
      if (files->services.writable == NULL)
        qa_range_error("QVM filesystem has no writable owner");
      if (!qa_user_file_store_resume(files->services.writable, &entry->file, writable_print,
                                     files, &handle->file))
        qa_range_error("QVM filesystem has no writable owner");
    }
  }
  qa_qvm_files_assert_current(files);
  qa_qvm_files_checkpoint_destroy(&checkpoint);
}

void qa_qvm_files_close_all(qa_qvm_files *files) {
  files->closed = true;
  for (int i = 1; i < 64; i++) {
    if (files->handles[i].occupied && files->handles[i].is_write)
      qa_writable_binary_file_close(&files->handles[i].file);
    free(files->handles[i].bytes);
    files->handles[i].bytes = NULL;
    files->handles[i].occupied = false;
  }
}

static int free_slot(qa_qvm_files *files) {
  int slot = 1;
  while (slot < 64 && files->handles[slot].occupied) slot++;
  if (slot == 64) qa_common_error(QA_COMMON_ERROR_DROP, "FS_HandleForFile: none free");
  return slot;
}

double qa_qvm_files_open_write(qa_qvm_files *files, const char *path, qa_writable_file_mode mode,
                               qa_qvm_file_publish publish) {
  qa_qvm_files_assert_current(files);
  if (files->services.writable == NULL) qa_range_error("QVM filesystem has no writable owner");
  int slot = free_slot(files);
  char *normalized = malloc(strlen(path) + 1);
  if (normalized == NULL) qa_range_error("out of memory");
  strcpy(normalized, path);
  for (char *p = normalized; *p != '\0'; p++)
    if (*p == '\\') *p = '/';
  qa_writable_binary_file file;
  bool opened = qa_user_file_store_open(files->services.writable, normalized, mode, writable_print,
                                        files, &file);
  free(normalized);
  if (!opened) {
    publish.call(publish.ctx, 0);
    return -1;
  }
  files->handles[slot].occupied = true;
  files->handles[slot].is_write = true;
  files->handles[slot].file = file;
  publish.call(publish.ctx, slot);
  return 0;
}

static file_handle *get_handle(qa_qvm_files *files, double slot) {
  qa_qvm_files_assert_current(files);
  if (!is_integer(slot) || slot < 1 || slot > 63)
    qa_common_error(QA_COMMON_ERROR_DROP, "FS_FileForHandle: out of range");
  file_handle *handle = &files->handles[(size_t)slot];
  if (!handle->occupied) qa_common_error(QA_COMMON_ERROR_DROP, "FS_FileForHandle: NULL");
  return handle;
}

double qa_qvm_files_write(qa_qvm_files *files, double slot, const uint8_t *bytes, size_t length) {
  qa_qvm_files_assert_current(files);
  if (slot == 0) return 0;
  file_handle *handle = get_handle(files, slot);
  if (!handle->is_write) {
    if (files->services.print != NULL)
      files->services.print(files->services.ctx, "FS_Write: 0 bytes written\n");
    qa_qvm_files_assert_current(files);
    return 0;
  }
  int64_t written = qa_writable_binary_file_write(&handle->file, bytes, length);
  qa_qvm_files_assert_current(files);
  return (double)written;
}

double qa_qvm_files_open(qa_qvm_files *files, const char *path,
                         const qa_qvm_file_publish *publish) {
  qa_qvm_files_assert_current(files);
  const char *stripped = path;
  if (stripped[0] == '/' || stripped[0] == '\\') stripped++;
  if (strstr(stripped, "..") != NULL || strstr(stripped, "::") != NULL ||
      strstr(stripped, "q3key") != NULL) {
    if (publish != NULL) publish->call(publish->ctx, 0);
    return publish == NULL ? 0 : -1;
  }
  qa_content_opened_resource resource;
  bool found = qa_content_mounted_open(files->services.mounts, stripped, NULL, NULL, &resource);
  qa_qvm_files_assert_current(files);
  if (publish == NULL) {
    if (found) free(resource.bytes);
    return found ? 1 : 0;
  }
  if (!found) {
    publish->call(publish->ctx, 0);
    return -1;
  }
  int slot = free_slot(files);
  file_handle *handle = &files->handles[slot];
  handle->occupied = true;
  handle->is_write = false;
  handle->bytes = resource.bytes;
  handle->byte_length = resource.byte_length;
  handle->position = 0;
  handle->reference = *resource.reference;
  handle->is_pk3_archive = reference_is_pk3(resource.reference);
  publish->call(publish->ctx, slot);
  return (double)resource.byte_length;
}

void qa_qvm_files_read(qa_qvm_files *files, double slot, qa_qvm_file_destination destination) {
  qa_qvm_files_assert_current(files);
  if (slot == 0) return;
  file_handle *handle = get_handle(files, slot);
  if (handle->is_write) qa_common_error(QA_COMMON_ERROR_FATAL, "FS_Read: file is not readable");
  int64_t remaining = (int64_t)handle->byte_length - handle->position;
  size_t count = remaining < 0 ? 0
                               : (destination.length < (size_t)remaining ? destination.length
                                                                          : (size_t)remaining);
  destination.set(destination.ctx,
                  handle->position >= 0 && handle->position <= (int64_t)handle->byte_length
                      ? handle->bytes + handle->position
                      : handle->bytes,
                  count);
  handle->position += (int64_t)count;
}

void qa_qvm_files_close(qa_qvm_files *files, double slot) {
  qa_qvm_files_assert_current(files);
  if (slot == 0) return;
  if (!is_integer(slot) || slot < 1 || slot > 63)
    qa_common_error(QA_COMMON_ERROR_DROP, "FS_FileForHandle: out of range");
  file_handle *handle = &files->handles[(size_t)slot];
  bool was_write = handle->occupied && handle->is_write;
  if (was_write) {
    qa_writable_binary_file file = handle->file;
    handle->occupied = false;
    qa_writable_binary_file_close(&file);
  } else {
    free(handle->bytes);
    handle->bytes = NULL;
    handle->occupied = false;
  }
}

double qa_qvm_files_seek(qa_qvm_files *files, double slot, double offset, double origin) {
  file_handle *handle = get_handle(files, slot);
  if (handle->is_write) {
    switch ((int64_t)origin) {
      case 0: return qa_writable_binary_file_seek(&handle->file, (int64_t)offset, QA_WRITABLE_SEEK_CURRENT);
      case 1: return qa_writable_binary_file_seek(&handle->file, (int64_t)offset, QA_WRITABLE_SEEK_END);
      case 2: return qa_writable_binary_file_seek(&handle->file, (int64_t)offset, QA_WRITABLE_SEEK_SET);
      default: qa_common_error(QA_COMMON_ERROR_FATAL, "Bad origin in FS_Seek\n");
    }
  }
  if (handle->is_pk3_archive) {
    if (offset >= 65536) qa_common_error(QA_COMMON_ERROR_FATAL, "ZIP FILE FSEEK NOT YET IMPLEMENTED\n");
    if (offset < 0) qa_range_error("Negative ZIP seek exceeds the source scratch buffer");
    handle->position = offset < (double)handle->byte_length ? (int64_t)offset
                                                             : (int64_t)handle->byte_length;
    return offset == 0 && origin == 2 ? 0 : (double)handle->position;
  }
  double result = 0;
  for (int pass = 0; pass < 2; pass++) {
    int64_t position;
    switch ((int64_t)origin) {
      case 0: position = handle->position + (int64_t)offset; break;
      case 1: position = (int64_t)handle->byte_length + (int64_t)offset; break;
      case 2: position = (int64_t)offset; break;
      default: qa_common_error(QA_COMMON_ERROR_FATAL, "Bad origin in FS_Seek\n");
    }
    if (position < 0) {
      result = -1;
    } else {
      handle->position = position;
      result = 0;
    }
  }
  return result;
}

char **qa_qvm_files_list(qa_qvm_files *files, const char *path, const char *extension,
                         size_t *out_count) {
  qa_qvm_files_assert_current(files);
  bool modlist = true;
  const char *expected = "$modlist";
  for (size_t i = 0; expected[i] != '\0'; i++)
    modlist = modlist && tolower((unsigned char)path[i]) == expected[i];
  modlist = modlist && path[strlen(expected)] == '\0';
  if (modlist) qa_range_error("QVM $modlist requires an installed-mod catalog owner");
  char **names = qa_content_mounted_list_files(files->services.mounts, path, extension, out_count);
  qa_qvm_files_assert_current(files);
  return names;
}

typedef struct publish_ctx {
  qa_qvm_files *files;
  const qa_qvm_host_call *call;
  int32_t destination;
} publish_ctx;

static void publish_slot(void *ctx, double slot) {
  publish_ctx *publish = ctx;
  qa_qvm_files_assert_current(publish->files);
  qa_qvm_writable_view view;
  qa_qvm_memory_view(publish->call->guest, publish->destination, 4, 0, &view);
  qa_qvm_writable_view_set_int32(&view, 0, qa_int32(slot), true);
}

typedef struct read_ctx {
  const qa_qvm_host_call *call;
  int64_t offset;
} read_ctx;

static void read_set(void *ctx, const uint8_t *bytes, size_t length) {
  read_ctx *read = ctx;
  if (length != 0) qa_qvm_memory_write_bytes(read->call->guest, read->offset, bytes, length);
}

bool qa_qvm_file_syscall(const qa_qvm_host_call *call, qa_qvm_files *files,
                         qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0) return false;
  bool ui = strcmp(call->role, "ui") == 0;
  bool game = strcmp(call->role, "qagame") == 0;
  int32_t trap = call->code;
  int32_t open = ui ? QA_QVM_UI_IMPORT_UI_FS_FOPENFILE
                    : game ? QA_QVM_GAME_IMPORT_G_FS_FOPEN_FILE
                           : QA_QVM_CGAME_IMPORT_CG_FS_FOPENFILE;
  int32_t seek = ui ? QA_QVM_UI_IMPORT_UI_FS_SEEK
                    : game ? QA_QVM_GAME_IMPORT_G_FS_SEEK : QA_QVM_CGAME_IMPORT_CG_FS_SEEK;
  bool has_list = ui || game;
  int32_t list = ui ? QA_QVM_UI_IMPORT_UI_FS_GETFILELIST : QA_QVM_GAME_IMPORT_G_FS_GETFILELIST;
  if (trap != open && trap != open + 1 && trap != open + 2 && trap != open + 3 && trap != seek &&
      !(has_list && trap == list))
    return false;
  qa_qvm_files_assert_current(files);
  qa_qvm_memory *guest = call->guest;
  if (trap == open) {
    int32_t path_word = qa_qvm_host_call_word(call, 1);
    int32_t destination = qa_qvm_host_call_word(call, 2);
    int32_t mode = qa_qvm_host_call_word(call, 3);
    if (mode < 0 || mode > 3) qa_common_error(QA_COMMON_ERROR_FATAL, "FSH_FOpenFile: bad mode");
    if (game && destination != 0) {
      qa_qvm_writable_view view;
      qa_qvm_memory_view(guest, destination, 4, 0, &view);
    }
    if (game && destination == 0 && mode != 0)
      qa_range_error("Writable file open requires a nonnull handle pointer");
    if (path_word == 0)
      qa_common_error(QA_COMMON_ERROR_FATAL, "FS_FOpenFileRead: NULL 'filename' parameter passed\n");
    if (!game && destination != 0) {
      qa_qvm_writable_view view;
      qa_qvm_memory_view(guest, destination, 4, 0, &view);
    }
    publish_ctx pctx = {files, call, destination};
    qa_qvm_file_publish publish = {&pctx, publish_slot};
    char *path = qa_qvm_memory_read_string(guest, path_word);
    if (path == NULL) qa_range_error("out of memory");
    double result;
    if (mode == 0) {
      result = qa_qvm_files_open(files, path, destination == 0 ? NULL : &publish);
    } else {
      if (destination == 0) {
        free(path);
        qa_range_error("Writable file open requires a nonnull handle pointer");
      }
      result = qa_qvm_files_open_write(
          files, path,
          mode == 1 ? QA_WRITABLE_WRITE : mode == 2 ? QA_WRITABLE_APPEND : QA_WRITABLE_APPEND_SYNC,
          publish);
    }
    free(path);
    *out = qa_int32(result);
    return true;
  }
  if (trap == open + 1 || trap == open + 2) {
    int32_t pointer = qa_qvm_host_call_word(call, 1);
    int32_t length = qa_qvm_host_call_word(call, 2);
    int32_t slot = qa_qvm_host_call_word(call, 3);
    if (slot == 0) {
      *out = 0;
      return true;
    }
    qa_qvm_memory_span span = {0, 0};
    bool empty = pointer == 0 && length == 0;
    if (!empty) span = qa_qvm_memory_get_span(guest, pointer, length, 0);
    if (trap == open + 2) {
      qa_qvm_files_write(files, slot, empty ? guest->bytes : guest->bytes + span.byte_offset,
                         empty ? 0 : (size_t)span.byte_length);
    } else {
      read_ctx rctx = {call, span.byte_offset};
      qa_qvm_file_destination destination = {&rctx, empty ? 0 : (size_t)span.byte_length, read_set};
      qa_qvm_files_read(files, slot, destination);
    }
    *out = 0;
    return true;
  }
  if (trap == open + 3) {
    qa_qvm_files_close(files, qa_qvm_host_call_word(call, 1));
    *out = 0;
    return true;
  }
  if (trap == seek) {
    *out = qa_int32(qa_qvm_files_seek(files, qa_qvm_host_call_word(call, 1),
                                      qa_qvm_host_call_word(call, 2),
                                      qa_qvm_host_call_word(call, 3)));
    return true;
  }
  char *path = qa_qvm_memory_read_string(guest, qa_qvm_host_call_word(call, 1));
  char *extension = qa_qvm_memory_read_string(guest, qa_qvm_host_call_word(call, 2));
  if (path == NULL || extension == NULL) qa_range_error("out of memory");
  int32_t pointer = qa_qvm_host_call_word(call, 3);
  int32_t length = qa_qvm_host_call_word(call, 4);
  if (length == 0) {
    free(path);
    free(extension);
    qa_range_error("File listing requires a nonempty destination");
  }
  (void)qa_qvm_memory_get_span(guest, pointer, length, 0);
  size_t name_count = 0;
  char **names = qa_qvm_files_list(files, path, extension, &name_count);
  free(path);
  free(extension);
  if (names == NULL) qa_range_error("out of memory");
  qa_qvm_files_assert_current(files);
  qa_qvm_writable_view destination;
  qa_qvm_memory_view(guest, pointer, length, 0, &destination);
  qa_qvm_writable_view_set_uint8(&destination, 0, 0);
  int64_t offset = 0;
  int32_t count = 0;
  for (size_t i = 0; i < name_count; i++) {
    size_t name_length = strlen(names[i]);
    if (offset + (int64_t)name_length + 2 >= destination.length) break;
    for (size_t j = 0; j < name_length; j++)
      qa_qvm_writable_view_set_uint8(&destination, offset++, (uint8_t)names[i][j]);
    qa_qvm_writable_view_set_uint8(&destination, offset++, 0);
    count++;
  }
  qa_content_free_string_list(names, name_count);
  *out = count;
  return true;
}
