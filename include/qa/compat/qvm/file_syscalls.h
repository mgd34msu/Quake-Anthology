/* Port of src/compat/qvm/file-syscalls.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_FILE_SYSCALLS_H
#define QA_COMPAT_QVM_FILE_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/content/mounts.h"
#include "qa/persistence/value.h"
#include "qa/platform/files/writable.h"

typedef struct qa_qvm_file_handle_checkpoint {
  double slot;
  bool is_write;
  /* write kind */
  qa_writable_file_checkpoint file;
  /* read kind: reference strings borrowed from the checkpoint tree or mounter */
  qa_resolved_resource_reference reference;
  uint8_t *bytes; /* owned copy */
  size_t byte_length;
  double position;
} qa_qvm_file_handle_checkpoint;

typedef struct qa_qvm_files_checkpoint {
  qa_qvm_file_handle_checkpoint *handles; /* owned */
  size_t handle_count;
} qa_qvm_files_checkpoint;

void qa_qvm_files_checkpoint_destroy(qa_qvm_files_checkpoint *checkpoint);

typedef struct qa_qvm_file_services {
  qa_content_mounted *mounts;
  qa_user_file_store *writable; /* NULL is the TS null */
  void *ctx;
  void (*print)(void *ctx, const char *text); /* optional */
  void (*assert_current)(void *ctx);
} qa_qvm_file_services;

typedef struct qa_qvm_files qa_qvm_files;

qa_qvm_files *qa_qvm_files_create(qa_qvm_file_services services);
void qa_qvm_files_destroy(qa_qvm_files *files);
qa_qvm_file_services qa_qvm_files_get_services(const qa_qvm_files *files);

/* False only on OOM. */
bool qa_qvm_files_capture_checkpoint(qa_qvm_files *files, qa_qvm_files_checkpoint *out);
void qa_qvm_files_restore_checkpoint(qa_qvm_files *files, const qa_save_value *value);
/* TS captureCheckpoint() as its saved value (owned tree; defined with the
 * QVM provider codecs in mod_provider.c). */
qa_save_value *qa_qvm_files_checkpoint_value(qa_qvm_files *files);
void qa_qvm_files_assert_current(qa_qvm_files *files);
void qa_qvm_files_close_all(qa_qvm_files *files);

typedef struct qa_qvm_file_publish {
  void *ctx;
  void (*call)(void *ctx, double slot);
} qa_qvm_file_publish;

double qa_qvm_files_open_write(qa_qvm_files *files, const char *path, qa_writable_file_mode mode,
                               qa_qvm_file_publish publish);
double qa_qvm_files_write(qa_qvm_files *files, double slot, const uint8_t *bytes, size_t length);
/* publish NULL (call == NULL) is the TS null publish. */
double qa_qvm_files_open(qa_qvm_files *files, const char *path,
                         const qa_qvm_file_publish *publish);
typedef struct qa_qvm_file_destination {
  void *ctx;
  size_t length;
  void (*set)(void *ctx, const uint8_t *bytes, size_t length);
} qa_qvm_file_destination;
void qa_qvm_files_read(qa_qvm_files *files, double slot, qa_qvm_file_destination destination);
void qa_qvm_files_close(qa_qvm_files *files, double slot);
double qa_qvm_files_seek(qa_qvm_files *files, double slot, double offset, double origin);
/* Malloc'd name list; free with qa_content_free_string_list. NULL only on OOM. */
char **qa_qvm_files_list(qa_qvm_files *files, const char *path, const char *extension,
                         size_t *out_count);

bool qa_qvm_file_syscall(const qa_qvm_host_call *call, qa_qvm_files *files,
                         qa_qvm_host_result *out);

#endif
