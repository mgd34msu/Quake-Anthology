/* C checks for tests/compat/qvm client-file, file-checkpoint, client-script,
 * game-async, cgame-body and the QvmUi export table, over real mounts, a
 * real user file store and a fake interpreter runtime. Every leaf checks a
 * sentinel ctx. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/cgame_body.h"
#include "qa/compat/qvm/client_script_syscalls.h"
#include "qa/compat/qvm/file_syscalls.h"
#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/module.h"
#include "qa/compat/qvm/ui.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)
#define CHECK_ABORTS(...)                                             \
  do {                                                                \
    checks++;                                                         \
    pid_t pid = fork();                                               \
    if (pid == 0) {                                                   \
      (void)prctl(PR_SET_DUMPABLE, 0);                                \
      (void)!freopen("/dev/null", "w", stderr);                       \
      __VA_ARGS__;                                                    \
      _exit(0);                                                       \
    }                                                                 \
    int status = 0;                                                   \
    waitpid(pid, &status, 0);                                         \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {      \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__, __LINE__); \
    }                                                                 \
  } while (0)

static int sentinel_value;
#define SENTINEL ((void *)&sentinel_value)
#define LEAF() CHECK(ctx == SENTINEL)

static uint8_t guest_bytes[16384];
static qa_qvm_memory guest;
static uint8_t word_bytes[64];

static void put_i32(uint8_t *bytes, size_t offset, int32_t value) {
  uint32_t bits = (uint32_t)value;
  for (int i = 0; i < 4; i++) bytes[offset + (size_t)i] = (uint8_t)(bits >> (8 * i));
}
static int32_t get_i32(const uint8_t *bytes, size_t offset) {
  uint32_t bits = 0;
  for (int i = 0; i < 4; i++) bits |= (uint32_t)bytes[offset + (size_t)i] << (8 * i);
  return (int32_t)bits;
}
static void put_text(size_t offset, const char *text) {
  memcpy(guest_bytes + offset, text, strlen(text) + 1);
}
static void reset_guest(void) {
  memset(guest_bytes, 0, sizeof guest_bytes);
  qa_qvm_memory_init(&guest, guest_bytes, sizeof guest_bytes, NULL, NULL);
}
static qa_qvm_host_call make_call(const char *role, int32_t code, const int32_t *args, size_t count) {
  memset(word_bytes, 0, sizeof word_bytes);
  put_i32(word_bytes, 0, code);
  for (size_t i = 0; i < count; i++) put_i32(word_bytes, (i + 1) * 4, args[i]);
  qa_qvm_host_call call;
  memset(&call, 0, sizeof call);
  call.kind = "engine";
  call.role = role;
  call.code = code;
  call.guest = &guest;
  call.base.words = word_bytes;
  call.base.words_length = sizeof word_bytes;
  call.base.guest = &guest;
  return call;
}
#define CALL(role, code, ...)                                       \
  make_call(role, code, (const int32_t[]){__VA_ARGS__},             \
            sizeof((const int32_t[]){__VA_ARGS__}) / sizeof(int32_t))

static bool write_text(const char *root, const char *name, const char *text) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", root, name);
  FILE *file = fopen(path, "w");
  if (file == NULL) return false;
  bool ok = fwrite(text, 1, strlen(text), file) == strlen(text);
  fclose(file);
  return ok;
}
static char *read_text(const char *root, const char *name) {
  static char text[256];
  char path[512];
  snprintf(path, sizeof path, "%s/%s", root, name);
  FILE *file = fopen(path, "r");
  if (file == NULL) return NULL;
  size_t length = fread(text, 1, sizeof text - 1, file);
  fclose(file);
  text[length] = 0;
  return text;
}

typedef struct mounted_fixture {
  qa_content_mount mount;
  qa_mount_id order[1];
  qa_resolved_mount_plan plan;
  qa_content_mounted *mounts;
} mounted_fixture;

static void open_loose(mounted_fixture *f, char *root) {
  memset(f, 0, sizeof *f);
  f->mount.kind = QA_CONTENT_MOUNT_LOOSE;
  f->mount.as.loose.identity =
      qa_create_mount_identity("mount:depth:loose", "q3:classic:baseq3:installed", 1);
  f->mount.as.loose.root_path = root;
  f->order[0] = "mount:depth:loose";
  f->plan.id = "mount-plan:depth:1";
  f->plan.mounts = &f->mount;
  f->plan.mount_count = 1;
  f->plan.default_order = f->order;
  f->plan.default_order_count = 1;
  f->mounts = qa_open_mount_plan(&f->plan, NULL);
}

/* ---- files ---- */

static int file_asserts;
static void file_assert(void *ctx) { LEAF(); file_asserts++; }
static void file_print(void *ctx, const char *text) { LEAF(); (void)text; }

static qa_save_value *write_handle(double slot, const char *path, const char *mode, double position) {
  qa_save_value *row = qa_save_value_object(), *file = qa_save_value_object();
  qa_save_value_set(row, "slot", qa_save_value_number(slot));
  qa_save_value_set(row, "kind", qa_save_value_string("write"));
  qa_save_value_set(file, "path", qa_save_value_string(path));
  qa_save_value_set(file, "mode", qa_save_value_string(mode));
  qa_save_value_set(file, "position", qa_save_value_number(position));
  qa_save_value_set(row, "file", file);
  return row;
}
static qa_save_value *handles_value(qa_save_value *a, qa_save_value *b) {
  qa_save_value *root = qa_save_value_object(), *list = qa_save_value_array();
  if (a != NULL) qa_save_value_push(list, a);
  if (b != NULL) qa_save_value_push(list, b);
  qa_save_value_set(root, "handles", list);
  return root;
}

static void test_files(void) {
  char read_template[] = "/tmp/qvm-depth-read-XXXXXX";
  char write_template[] = "/tmp/qvm-depth-write-XXXXXX";
  char *root = mkdtemp(read_template), *user = mkdtemp(write_template);
  CHECK(root != NULL && user != NULL);
  if (root == NULL || user == NULL) return;
  CHECK(write_text(root, "read.cfg", "duplicate"));
  CHECK(write_text(root, "nums.txt", "0123456789"));
  mounted_fixture fixture;
  open_loose(&fixture, root);
  CHECK(fixture.mounts != NULL);
  qa_user_file_store store;
  CHECK(qa_user_file_store_init(&store, user));
  qa_qvm_file_services services;
  memset(&services, 0, sizeof services);
  services.mounts = fixture.mounts;
  services.writable = &store;
  services.ctx = SENTINEL;
  services.print = file_print;
  services.assert_current = file_assert;
  qa_qvm_files *files = qa_qvm_files_create(services);
  reset_guest();
  qa_qvm_host_result out = -9;

  put_text(128, "read.cfg");
  qa_qvm_host_call call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_FOPENFILE, 128, 256, 0);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == 9 && get_i32(guest_bytes, 256) == 1);
  memset(guest_bytes + 512, 99, 16);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_READ, 512, 12, 1);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == 0);
  CHECK(memcmp(guest_bytes + 512, "duplicate", 9) == 0 && guest_bytes[521] == 99 && guest_bytes[523] == 99);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_FCLOSEFILE, 1);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == 0);
  CHECK_ABORTS(qa_qvm_file_destination d = {NULL, 0, NULL}; qa_qvm_files_read(files, 1, d));
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == 0);
  put_text(128, "missing.cfg");
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_FOPENFILE, 128, 256, 0);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == -1 && get_i32(guest_bytes, 256) == 0);
  put_text(128, "../read.cfg");
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_FOPENFILE, 128, 0, 0);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == 0);
  put_text(128, "/read.cfg");
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_FOPENFILE, 128, 0, 0);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == 1);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_READ, 0, -1, 0);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == 0);
  qa_qvm_host_call bad = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_FOPENFILE, 128, 256, 4);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_file_syscall(&bad, files, &o));
  bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_FS_FOPEN_FILE, 128, 0, 1);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_file_syscall(&bad, files, &o));

  /* Streamed seeks repeat: SEEK_CUR accumulates twice, failure keeps the cursor. */
  put_text(128, "nums.txt");
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_FOPENFILE, 128, 256, 0);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == 10);
  double loose = get_i32(guest_bytes, 256);
  uint8_t data[2];
  qa_qvm_file_destination destination;
  CHECK(qa_qvm_files_seek(files, loose, -1, 0) == -1);
  memset(guest_bytes + 600, 0, 4);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_READ, 600, 1, (int32_t)loose);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && guest_bytes[600] == '0');
  CHECK(qa_qvm_files_seek(files, loose, 1, 0) == 0);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_READ, 600, 2, (int32_t)loose);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && memcmp(guest_bytes + 600, "34", 2) == 0);
  CHECK(qa_qvm_files_seek(files, loose, -1, 1) == 0);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_READ, 600, 2, (int32_t)loose);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && guest_bytes[600] == '9');
  CHECK(qa_qvm_files_seek(files, loose, 20, 2) == 0);
  (void)data;
  (void)destination;
  CHECK_ABORTS((void)qa_qvm_files_seek(files, loose, 0, 3));

  /* Listing: zero capacity rejects; $modlist needs a catalog owner. */
  put_text(700, "");
  put_text(720, ".cfg");
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_GETFILELIST, 700, 720, 800, 64);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == 1);
  CHECK(strcmp((char *)guest_bytes + 800, "read.cfg") == 0);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_GETFILELIST, 700, 720, 800, 10);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == 0 && guest_bytes[800] == 0);
  bad = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_GETFILELIST, 700, 720, 800, 0);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_file_syscall(&bad, files, &o));
  /* After the drift $modlist lists no mods (TS file-syscalls.ts): count 0 and an empty buffer. */
  put_text(740, "$ModList");
  bad = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_GETFILELIST, 740, 720, 800, 64);
  guest_bytes[800] = 0x55;
  CHECK(qa_qvm_file_syscall(&bad, files, &out) && out == 0 && guest_bytes[800] == 0);
  call = CALL("cgame", QA_QVM_UI_IMPORT_UI_FS_GETFILELIST, 700, 720, 800, 64);
  CHECK(!qa_qvm_file_syscall(&call, files, &out) || out == 0);

  /* Writes, then capture order follows insertion, not slot number. */
  put_text(900, "a.txt");
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_FOPENFILE, 900, 256, 1);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == 0);
  double write_slot = get_i32(guest_bytes, 256);
  CHECK(write_slot == 2); /* nums.txt holds slot 1 */
  put_text(920, "b.txt");
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_FOPENFILE, 920, 256, 2);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && get_i32(guest_bytes, 256) == 3);
  put_text(940, "abc");
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_WRITE, 940, 3, (int32_t)write_slot);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && out == 0);
  /* Writing a read handle reports zero bytes. */
  CHECK(qa_qvm_files_write(files, loose, (const uint8_t *)"x", 1) == 0);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_FCLOSEFILE, (int32_t)loose);
  CHECK(qa_qvm_file_syscall(&call, files, &out));
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_FOPENFILE, 128, 256, 0);
  CHECK(qa_qvm_file_syscall(&call, files, &out) && get_i32(guest_bytes, 256) == 1);
  qa_qvm_files_checkpoint checkpoint;
  CHECK(qa_qvm_files_capture_checkpoint(files, &checkpoint));
  CHECK(checkpoint.handle_count == 3);
  if (checkpoint.handle_count == 3)
    CHECK(checkpoint.handles[0].slot == 2 && checkpoint.handles[1].slot == 3 && checkpoint.handles[2].slot == 1);
  qa_qvm_files_checkpoint_destroy(&checkpoint);
  qa_qvm_files_close_all(files);
  CHECK(strcmp(read_text(user, "a.txt"), "abc") == 0);

  /* Restore write handles into a fresh owner. */
  qa_qvm_files *restored = qa_qvm_files_create(services);
  qa_save_value *value = handles_value(write_handle(2, "a.txt", "write", 1), NULL);
  qa_qvm_files_restore_checkpoint(restored, value);
  qa_save_value_free(value);
  CHECK(qa_qvm_files_write(restored, 2, (const uint8_t *)"Z", 1) == 1);
  CHECK_ABORTS(qa_save_value *v = handles_value(write_handle(2, "a.txt", "write", 0), NULL);
               qa_qvm_files_restore_checkpoint(restored, v));
  qa_qvm_files_close_all(restored);
  CHECK(strcmp(read_text(user, "a.txt"), "aZc") == 0);
  qa_qvm_files_destroy(restored);
  qa_qvm_files *empty = qa_qvm_files_create(services);
  CHECK_ABORTS(qa_qvm_files_restore_checkpoint(
      empty, handles_value(write_handle(2, "a.txt", "write", 0), write_handle(2, "b.txt", "write", 0))));
  CHECK_ABORTS(qa_qvm_files_restore_checkpoint(empty, handles_value(write_handle(64, "a.txt", "write", 0), NULL)));
  CHECK_ABORTS(qa_qvm_files_restore_checkpoint(empty, handles_value(write_handle(1, "../escape", "write", 0), NULL)));
  CHECK_ABORTS(qa_qvm_files_restore_checkpoint(empty, handles_value(write_handle(1, "a.txt", "write", -1), NULL)));
  services.writable = NULL;
  qa_qvm_files *readonly = qa_qvm_files_create(services);
  CHECK_ABORTS(qa_qvm_files_restore_checkpoint(readonly, handles_value(write_handle(1, "a.txt", "write", 0), NULL)));
  put_text(900, "c.txt");
  bad = CALL("ui", QA_QVM_UI_IMPORT_UI_FS_FOPENFILE, 900, 256, 1);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_file_syscall(&bad, readonly, &o));
  qa_qvm_files_destroy(readonly);
  qa_qvm_files_destroy(empty);
  qa_qvm_files_destroy(files);
  qa_user_file_store_destroy(&store);
  qa_content_mounted_destroy(fixture.mounts);
}

/* ---- scripts ---- */

static char script_printed[1024];
static void script_assert(void *ctx) { LEAF(); }
static void script_print(void *ctx, const char *text) {
  LEAF();
  strncat(script_printed, text, sizeof script_printed - strlen(script_printed) - 1);
}
static void ignore_publish(void *ctx, qa_ui_script_source_reader *reader) {
  LEAF();
  (void)reader;
}

static void test_scripts(void) {
  char template[] = "/tmp/qvm-depth-scripts-XXXXXX";
  char *root = mkdtemp(template);
  CHECK(root != NULL);
  if (root == NULL) return;
  char sub[512];
  snprintf(sub, sizeof sub, "%s/sub", root);
  CHECK(mkdir(sub, 0700) == 0);
  CHECK(write_text(root, "inc.menu", "#include \"sub\\\\bad.inc\"\n"));
  CHECK(write_text(root, "sub/bad.inc", "#include \"absent2.inc\"\n"));
  CHECK(write_text(root, "plain.menu", "a b c\n"));
  mounted_fixture fixture;
  open_loose(&fixture, root);
  qa_ui_script_global_defines *globals = qa_ui_script_global_defines_create(NULL, NULL, NULL);
  qa_qvm_client_script_services services;
  memset(&services, 0, sizeof services);
  services.mounts = fixture.mounts;
  services.globals = globals;
  services.ctx = SENTINEL;
  services.assert_current = script_assert;
  services.print = script_print;
  qa_qvm_client_scripts *scripts = qa_qvm_client_scripts_create(services);

  /* An include's diagnostics carry its normalized canonical path. */
  double handle = qa_qvm_client_scripts_load(scripts, "inc.menu");
  CHECK(handle == 1);
  qa_qvm_script_publish publish = {SENTINEL, ignore_publish};
  script_printed[0] = 0;
  CHECK(qa_qvm_client_scripts_read(scripts, handle, publish) == 0);
  CHECK(strstr(script_printed, "file sub/bad.inc, line 1") != NULL);
  CHECK(strstr(script_printed, "absent2.inc") != NULL);
  CHECK(qa_qvm_client_scripts_free(scripts, handle) == 1);

  /* Checkpoint rows follow reader insertion order. */
  CHECK(qa_qvm_client_scripts_load(scripts, "plain.menu") == 1);
  CHECK(qa_qvm_client_scripts_load(scripts, "plain.menu") == 2);
  CHECK(qa_qvm_client_scripts_load(scripts, "plain.menu") == 3);
  CHECK(qa_qvm_client_scripts_free(scripts, 1) == 1);
  CHECK(qa_qvm_client_scripts_load(scripts, "plain.menu") == 1);
  qa_save_value *value = qa_qvm_client_scripts_capture(scripts);
  CHECK(value != NULL);
  if (value != NULL) {
    qa_save_reader reader, rows;
    CHECK(qa_save_reader_init(&reader, value, "client-scripts"));
    CHECK(qa_save_reader_field(&reader, "readers", &rows));
    CHECK(qa_save_reader_list_count(&rows) == 3);
    const double expected[3] = {2, 3, 1};
    for (size_t i = 0; i < 3 && i < qa_save_reader_list_count(&rows); i++) {
      qa_save_reader row, slot;
      CHECK(qa_save_reader_item(&rows, i, &row));
      CHECK(qa_save_reader_field(&row, "handle", &slot));
      CHECK(qa_save_reader_integer_min(&slot, 1) == expected[i]);
      qa_save_reader_destroy(&slot);
      qa_save_reader_destroy(&row);
    }
    qa_save_reader_destroy(&rows);
    qa_save_reader_destroy(&reader);
    qa_save_value_free(value);
  }
  qa_qvm_client_scripts_close_all(scripts);
  CHECK_ABORTS((void)qa_qvm_client_scripts_load(scripts, "plain.menu"));
  qa_qvm_client_scripts_destroy(scripts);
  qa_ui_script_global_defines_destroy(globals);
  qa_content_mounted_destroy(fixture.mounts);
}

/* ---- fake interpreter runtime for QvmGame / QvmUi / body hooks ---- */

typedef struct fake_runtime {
  qa_qvm_module *module;
  int32_t exports[16];
  size_t export_count;
  qa_qvm_function_hook_fn hook;
  void *hook_ctx;
  int64_t hook_index;
  size_t binds, unbinds;
} fake_runtime;

static fake_runtime runtime_state;

static bool fake_active(void *ctx) { (void)ctx; return false; }
static int64_t fake_stack(void *ctx) { (void)ctx; return 16384; }
static qa_qvm_host_result fake_syscall(fake_runtime *runtime, const int32_t *words, size_t count) {
  static uint8_t bytes[64];
  memset(bytes, 0, sizeof bytes);
  for (size_t i = 0; i < count; i++) put_i32(bytes, i * 4, words[i]);
  qa_qvm_module_syscall call;
  memset(&call, 0, sizeof call);
  call.words = bytes;
  call.words_length = sizeof bytes;
  call.memory = guest_bytes;
  call.memory_length = sizeof guest_bytes;
  call.guest = &guest;
  return qa_qvm_system_call_invoke(&runtime->module->source_system_call, &call);
}
static int32_t fake_invoke(void *ctx, const int32_t *args, size_t count, int64_t index,
                           const qa_qvm_read_only_evaluation *evaluation) {
  fake_runtime *runtime = ctx;
  (void)count;
  (void)index;
  (void)evaluation;
  if (runtime->export_count < 16) runtime->exports[runtime->export_count++] = args[0];
  const char *role = runtime->module->options.artifact->role;
  if (strcmp(role, "ui") == 0) return args[0] == QA_QVM_UI_EXPORT_UI_GETAPIVERSION ? 6 : args[0] == QA_QVM_UI_EXPORT_UI_IS_FULLSCREEN;
  if (args[0] == QA_QVM_GAME_EXPORT_GAME_INIT) {
    /* The guest publishes its tables, then prints: only the print reaches the host. */
    const int32_t locate[] = {QA_QVM_GAME_IMPORT_G_LOCATE_GAME_DATA, 1024, 2, 1024, 4096, 1024};
    (void)fake_syscall(runtime, locate, 6);
    const int32_t print[] = {QA_QVM_GAME_IMPORT_G_PRINT, 64};
    (void)fake_syscall(runtime, print, 2);
    return 0;
  }
  if (args[0] == QA_QVM_GAME_EXPORT_GAME_CLIENT_CONNECT) {
    put_text(512 + 8192, "guest denied");
    return args[1] == 2 ? 512 + 8192 : 0;
  }
  return args[0] == QA_QVM_GAME_EXPORT_GAME_CONSOLE_COMMAND ? 1 : 0;
}
static int32_t fake_invoke_async(void *ctx, const int32_t *args, size_t count, int64_t index,
                                 qa_qvm_validate_fn validate, void *validate_ctx) {
  if (validate != NULL) validate(validate_ctx);
  int32_t result = fake_invoke(ctx, args, count, index, NULL);
  if (validate != NULL) validate(validate_ctx);
  return result;
}
static size_t fake_bind(void *ctx, int64_t index, qa_qvm_function_hook_fn hook, void *hook_ctx) {
  fake_runtime *runtime = ctx;
  runtime->hook = hook;
  runtime->hook_ctx = hook_ctx;
  runtime->hook_index = index;
  return ++runtime->binds;
}
static void fake_unbind(void *ctx, size_t id) {
  fake_runtime *runtime = ctx;
  (void)id;
  runtime->unbinds++;
}

static qa_qvm_interpreter_runtime fake_interpreter(void) {
  qa_qvm_interpreter_runtime runtime;
  memset(&runtime, 0, sizeof runtime);
  runtime.ctx = &runtime_state;
  runtime.address_space = &guest;
  runtime.is_active = fake_active;
  runtime.stack_pointer = fake_stack;
  runtime.invoke = fake_invoke;
  runtime.invoke_async = fake_invoke_async;
  runtime.bind_function = fake_bind;
  runtime.unbind = fake_unbind;
  return runtime;
}

static qa_resolved_qvm_artifact fake_artifact(const char *role) {
  qa_resolved_qvm_artifact artifact;
  memset(&artifact, 0, sizeof artifact);
  artifact.role = role;
  artifact.module.id = "test:depth";
  artifact.module.artifact_path = "vm/test.qvm";
  artifact.module.digest = "sha256:0000";
  artifact.module.revision = "test";
  return artifact;
}

static int host_prints;
static qa_qvm_host_result game_host(void *ctx, const qa_qvm_host_call *call) {
  LEAF();
  CHECK(call->code != QA_QVM_GAME_IMPORT_G_LOCATE_GAME_DATA);
  if (call->code == QA_QVM_GAME_IMPORT_G_PRINT) host_prints++;
  return 0;
}

static void test_game(void) {
  reset_guest();
  memset(&runtime_state, 0, sizeof runtime_state);
  qa_qvm_interpreter_runtime runtime = fake_interpreter();
  qa_resolved_qvm_artifact artifact = fake_artifact("qagame");
  qa_qvm_module_options options;
  memset(&options, 0, sizeof options);
  options.artifact = &artifact;
  options.host = game_host;
  options.host_ctx = SENTINEL;
  options.interpreter = &runtime;
  qa_qvm_game game;
  qa_qvm_game_init(&game, &options);
  runtime_state.module = &game.module;
  CHECK(qa_qvm_game_get_api(&game).version == 8);
  qa_qvm_game_initialize_async(&game, 100, 42, false);
  CHECK(host_prints == 1);
  CHECK(qa_qvm_game_data_get_num_entities(&game.data) == 2);
  CHECK(qa_qvm_game_data_number_from_pointer(&game.data, 1024 + 1024) == 1);
  char *denied = qa_qvm_game_client_connect_async(&game, 2, true, false);
  CHECK(denied != NULL && strcmp(denied, "guest denied") == 0);
  free(denied);
  CHECK(qa_qvm_game_client_connect(&game, 0, true, false) == NULL);
  const char *const arguments[] = {"status"};
  CHECK(qa_qvm_game_console_command_async(&game, arguments, 1));
  qa_qvm_game_run_frame(&game, 150);
  CHECK(runtime_state.export_count == 5);
  CHECK(runtime_state.exports[0] == QA_QVM_GAME_EXPORT_GAME_INIT &&
        runtime_state.exports[4] == QA_QVM_GAME_EXPORT_GAME_RUN_FRAME);
  qa_qvm_game_retire(&game);
  CHECK_ABORTS(qa_qvm_game_run_frame(&game, 200));
  qa_qvm_game_destroy(&game);
  qa_resolved_qvm_artifact wrong = fake_artifact("cgame");
  options.artifact = &wrong;
  CHECK_ABORTS(qa_qvm_game game2; qa_qvm_game_init(&game2, &options));
}

static int ui_asserts;
static void ui_assert(void *ctx) { LEAF(); ui_asserts++; }

static void test_ui_exports(void) {
  reset_guest();
  memset(&runtime_state, 0, sizeof runtime_state);
  qa_qvm_interpreter_runtime runtime = fake_interpreter();
  qa_resolved_qvm_artifact artifact = fake_artifact("ui");
  qa_qvm_module_options options;
  memset(&options, 0, sizeof options);
  options.artifact = &artifact;
  options.host_ctx = SENTINEL;
  options.interpreter = &runtime;
  qa_qvm_module module;
  runtime_state.module = &module;
  qa_qvm_module_init(&module, &options);
  qa_qvm_ui ui;
  qa_qvm_ui_init_module(&ui, (qa_seat_id){0}, &module, ui_assert, SENTINEL);
  qa_q3_ui_exports exports = qa_qvm_ui_as_exports(&ui);
  CHECK(exports.ctx == &ui && exports.api.kind == QA_Q3_API_UI && exports.api.version == 6);
  runtime_state.export_count = 0;
  exports.refresh(exports.ctx, 120);
  CHECK(exports.is_fullscreen(exports.ctx));
  exports.set_active_menu(exports.ctx, "postgame");
  CHECK(runtime_state.export_count == 3 && runtime_state.exports[0] == QA_QVM_UI_EXPORT_UI_REFRESH);
  CHECK(runtime_state.exports[2] == QA_QVM_UI_EXPORT_UI_SET_ACTIVE_MENU);
  CHECK(ui_asserts > 0);
  CHECK_ABORTS(exports.set_active_menu(exports.ctx, "bogus"));
  qa_qvm_ui_retire(&ui);
  CHECK_ABORTS(exports.shutdown(exports.ctx));
  qa_qvm_module_destroy(&module);
}

/* ---- cgame body submissions ---- */

static bool body_hidden(void *ctx, double entity) { LEAF(); return entity == 5; }
static int body_proceeds;
static bool body_inside_suppressed, body_outside_suppressed;
static qa_qvm_body_submissions *body_active;
static int32_t body_proceed(void *ctx) {
  LEAF();
  body_proceeds++;
  if (body_active != NULL) {
    qa_qvm_host_call inside = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_R_ADDREFENTITYTOSCENE, 8000 - 200);
    body_inside_suppressed = qa_qvm_body_submissions_suppress(body_active, &inside);
    qa_qvm_host_call outside = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_R_ADDREFENTITYTOSCENE, 9000);
    body_outside_suppressed = qa_qvm_body_submissions_suppress(body_active, &outside);
  }
  return 7;
}

static int32_t run_body_hook(int32_t when_word) {
  uint8_t *words = guest_bytes + 8000;
  put_i32(words, 0, 2048); /* actor argument word: centity pointer */
  put_i32(words, 4, when_word);
  qa_qvm_module_function_call call;
  memset(&call, 0, sizeof call);
  call.instruction_index = 1;
  call.execution = "asynchronous";
  call.words = words;
  call.words_length = 64;
  call.words_offset = 8000;
  call.memory = guest_bytes;
  call.memory_length = sizeof guest_bytes;
  call.guest = &guest;
  call.call_ctx = SENTINEL;
  call.proceed_async = body_proceed;
  return runtime_state.hook(runtime_state.hook_ctx, &call);
}

static void test_body(void) {
  reset_guest();
  memset(&runtime_state, 0, sizeof runtime_state);
  qa_qvm_interpreter_runtime runtime = fake_interpreter();
  qa_resolved_qvm_artifact artifact = fake_artifact("cgame");
  qa_qvm_module_options options;
  memset(&options, 0, sizeof options);
  options.artifact = &artifact;
  options.interpreter = &runtime;
  qa_qvm_module module;
  runtime_state.module = &module;
  qa_qvm_module_init(&module, &options);
  qa_qvm_instruction instructions[3];
  memset(instructions, 0, sizeof instructions);
  instructions[1].opcode = QA_QVM_OP_ENTER;
  instructions[1].operand = 256;
  instructions[2].opcode = QA_QVM_OP_ENTER;
  instructions[2].operand = 4;
  qa_qvm_image image;
  memset(&image, 0, sizeof image);
  image.instructions = instructions;
  image.instruction_count = 3;
  qa_qvm_body_submission declaration;
  memset(&declaration, 0, sizeof declaration);
  declaration.entry = 1;
  declaration.actor_argument = 0;
  declaration.entity_number_offset = 4;
  declaration.has_when = true;
  declaration.when_argument = 1;
  declaration.when_equals = 1.5; /* a word never equals a fraction */
  qa_qvm_body_hidden_fn hidden = {SENTINEL, body_hidden};

  CHECK_ABORTS((void)qa_qvm_body_submissions_create(&module, &image, false, &declaration, 1, hidden, NULL));
  qa_qvm_body_submission invalid = declaration;
  invalid.entry = 2;
  CHECK_ABORTS((void)qa_qvm_body_submissions_create(&module, &image, true, &invalid, 1, hidden, NULL));
  invalid = declaration;
  invalid.entity_number_offset = 6;
  CHECK_ABORTS((void)qa_qvm_body_submissions_create(&module, &image, true, &invalid, 1, hidden, NULL));
  invalid = declaration;
  invalid.actor_argument = 0.5;
  CHECK_ABORTS((void)qa_qvm_body_submissions_create(&module, &image, true, &invalid, 1, hidden, NULL));
  qa_qvm_body_submission pair[2] = {declaration, declaration};
  CHECK_ABORTS((void)qa_qvm_body_submissions_create(&module, &image, true, pair, 2, hidden, NULL));

  qa_qvm_body_submissions *submissions =
      qa_qvm_body_submissions_create(&module, &image, true, &declaration, 1, hidden, NULL);
  CHECK(submissions != NULL);
  CHECK(!qa_qvm_body_submissions_get_captures_player_meshes(submissions));
  qa_qvm_body_submissions_enable(submissions, true);
  qa_qvm_body_submissions_enable(submissions, true);
  CHECK(runtime_state.binds == 1 && runtime_state.hook_index == 1);
  put_i32(guest_bytes, 2048 + 4, 5); /* hidden entity number */
  body_active = submissions;
  /* `when` never matches: the call proceeds without a body scope. */
  CHECK(run_body_hook(1) == 7 && body_proceeds == 1 && !body_inside_suppressed);
  declaration.when_equals = 1;
  CHECK(run_body_hook(1) == 7 && body_proceeds == 2);
  CHECK(body_inside_suppressed && !body_outside_suppressed);
  /* Outside the scope nothing is suppressed. */
  qa_qvm_host_call after = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_R_ADDREFENTITYTOSCENE, 8000 - 200);
  CHECK(!qa_qvm_body_submissions_suppress(submissions, &after));
  /* Visible entities proceed directly. */
  put_i32(guest_bytes, 2048 + 4, 6);
  body_inside_suppressed = true;
  CHECK(run_body_hook(1) == 7 && !body_inside_suppressed);
  body_active = NULL;
  qa_qvm_body_submissions_close(submissions);
  CHECK(runtime_state.unbinds == 1);
  qa_qvm_body_submissions_destroy(submissions);
  qa_qvm_module_destroy(&module);
}

int main(void) {
  test_files();
  test_scripts();
  test_game();
  test_ui_exports();
  test_body();
  printf("test_qvm_client_depth: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
