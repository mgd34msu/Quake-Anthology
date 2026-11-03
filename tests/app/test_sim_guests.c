/* C checks for the native guest sources, services and worlds
 * (src/app/bootstrap/simulation/classic-guest-*, rerelease-guest-*,
 * native-q2-*). Ports tests/compat/q2/classic/{services,world,host}.test.ts,
 * tests/compat/q2/rerelease/{native,services}.test.ts cases over the
 * retail DLLs and tests/app/q2-native-save.test.ts. The retail cases run
 * when ~/qfiles (or $QA_CORPUS laid out as ~/qfiles) has the DLLs, as the
 * TS suite skips without its corpus. GPL-2.0-or-later. */
#include <errno.h>
#include <libgen.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/app/bootstrap/simulation/classic_guest_files.h"
#include "qa/app/bootstrap/simulation/classic_guest_services.h"
#include "qa/app/bootstrap/simulation/classic_guest_source.h"
#include "qa/app/bootstrap/simulation/classic_guest_world.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_world.h"
#include "qa/app/bootstrap/simulation/native_q2_rerelease_save.h"
#include "qa/app/bootstrap/simulation/native_q2_travel.h"
#include "qa/app/bootstrap/simulation/native_primary_weapons.h"
#include "qa/app/bootstrap/native_q2_client.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/compat/q2/native_primary_weapon_profile.h"
#include "qa/persistence/q2_classic_guest.h"
#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/compat/q2/classic/records.h"
#include "qa/content/archive.h"
#include "qa/core/common_parse.h"
#include "qa/core/cvars/index.h"
#include "qa/core/numeric.h"
#include "qa/formats/q2_map.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_ABORTS(expr) do { checks++; fflush(NULL); pid_t pid_ = fork(); \
    if (pid_ == 0) { prctl(PR_SET_DUMPABLE, 0); if (freopen("/dev/null", "w", stderr) == NULL) _exit(3); expr; _exit(0); } \
    int status_ = 0; waitpid(pid_, &status_, 0); \
    if (!(WIFSIGNALED(status_) || (WIFEXITED(status_) && WEXITSTATUS(status_) != 0))) { failures++; fprintf(stderr, "%s:%d: expected abort: %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* Every callback below checks the context its author installed. */
static int sentinel_storage;
#define SENTINEL ((void *)&sentinel_storage)
#define CTX_OK(ctx) do { if ((ctx) != SENTINEL) { fprintf(stderr, "%s:%d: wrong callback ctx\n", __FILE__, __LINE__); abort(); } } while (0)

static char corpus_root[512];
static const char *corpus(const char *relative, char *out, size_t size) {
  snprintf(out, size, "%s/%s", corpus_root, relative); return out;
}
static bool exists(const char *path) { struct stat st; return stat(path, &st) == 0; }
static inline uint8_t *read_file(const char *path, size_t *length) {
  FILE *f = fopen(path, "rb"); if (f == NULL) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *bytes = malloc((size_t)n + 1); if (bytes == NULL || fread(bytes, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(bytes); return NULL; }
  fclose(f); *length = (size_t)n; return bytes;
}
static void remove_tree(const char *path) { char command[1200]; snprintf(command, sizeof command, "rm -rf '%s'", path); if (system(command) != 0) fprintf(stderr, "cleanup failed: %s\n", path); }
static char *temp_dir(const char *prefix) {
  const char *base = getenv("TMPDIR"); char *path = malloc(1024);
  snprintf(path, 1024, "%s/%sXXXXXX", base != NULL && base[0] != '\0' ? base : "/tmp", prefix);
  if (mkdtemp(path) == NULL) { perror("mkdtemp"); abort(); }
  if (path[0] != '/') { char cwd[512], absolute[1024]; if (getcwd(cwd, sizeof cwd) == NULL) abort(); snprintf(absolute, sizeof absolute, "%s/%s", cwd, path); strcpy(path, absolute); }
  return path;
}

/* ---------------- classic-guest-files ---------------- */
typedef struct raise_call { qa_windows_file *file; int op; } raise_call;
static void run_raise(void *ctx) {
  raise_call *c = ctx; uint8_t byte = 1;
  if (c->op == 0) (void)c->file->size(c->file->ctx);
  else (void)c->file->write(c->file->ctx, 0, &byte, 1);
}
static bool raises(qa_windows_file *file, int op, const char *message) {
  raise_call call = {file, op}; qa_caught_error caught;
  if (qa_try(run_raise, &call, &caught)) return false;
  return strstr(caught.message, message) != NULL;
}
static void test_classic_files(void) {
  char *root = temp_dir("classic-files-");
  static const uint8_t settings[] = {7, 8, 9};
  qa_app_sim_classic_guest_resource resources[] = {{"settings.cfg", settings, 3}};
  qa_app_sim_classic_guest_files *files = qa_app_sim_classic_guest_files_create(root, resources, 1);
  qa_windows_file *file = qa_app_sim_classic_guest_files_open(files, "save\\game.sav", true, true, 2);
  CHECK(file != NULL);
  static const uint8_t data[] = {10, 20};
  CHECK(file->write(file->ctx, 3, data, 2) == 2);
  uint8_t out[8] = {0}; CHECK(file->read(file->ctx, 0, out, 5) == 5);
  static const uint8_t expected[] = {0, 0, 0, 10, 20}; CHECK(memcmp(out, expected, 5) == 0);
  file->flush(file->ctx); file->close(file->ctx); file->close(file->ctx);
  CHECK(raises(file, 0, "closed"));
  CHECK(qa_app_sim_classic_guest_files_open(files, "save/game.sav", true, true, 1) == NULL);
  qa_windows_file *resumed = qa_app_sim_classic_guest_files_open(files, "save/game.sav", true, true, 3);
  CHECK(resumed != NULL && resumed->size(resumed->ctx) == 5);
  resumed->truncate(resumed->ctx, 2); resumed->close(resumed->ctx);
  char path[1200]; snprintf(path, sizeof path, "%s/save/game.sav", root); struct stat st; CHECK(stat(path, &st) == 0 && st.st_size == 2);
  qa_windows_file *mounted = qa_app_sim_classic_guest_files_open(files, "settings.cfg", true, false, 3);
  CHECK(mounted != NULL);
  memset(out, 0, sizeof out); CHECK(mounted->read(mounted->ctx, 1, out, 3) == 2 && out[0] == 8 && out[1] == 9);
  CHECK(raises(mounted, 1, "read-only")); mounted->close(mounted->ctx);
  CHECK_ABORTS((void)qa_app_sim_classic_guest_files_open(files, "../escape", true, true, 2));
  char link[1200]; snprintf(link, sizeof link, "%s/outside", root);
  const char *tmp = getenv("TMPDIR"); CHECK(symlink(tmp != NULL ? tmp : "/tmp", link) == 0);
  CHECK(qa_app_sim_classic_guest_files_open(files, "outside/native-test", true, true, 2) == NULL);
  qa_app_sim_classic_guest_files_destroy(files);
  remove_tree(root); free(root);
}
static void test_classic_files_quota(void) {
  char *root = temp_dir("classic-files-quota-");
  qa_app_sim_classic_guest_files *files = qa_app_sim_classic_guest_files_create(root, NULL, 0);
  static const char *const refused[] = {"autoexec.cfg", "pak9.pak", "game.dll", "vm\\qagame.qvm", "x.PK3"};
  for (size_t i = 0; i < 5; i++) CHECK(qa_app_sim_classic_guest_files_open(files, refused[i], false, true, 2) == NULL);
  char path[1200]; snprintf(path, sizeof path, "%s/autoexec.cfg", root); CHECK(!exists(path));
  qa_windows_file *file = qa_app_sim_classic_guest_files_open(files, "save/game.sav", true, true, 2);
  static const uint8_t three[] = {1, 2, 3}, one[] = {1};
  CHECK(file != NULL && file->write(file->ctx, 0, three, 3) == 3);
  CHECK(file->write(file->ctx, 64 * 1024 * 1024, one, 1) == 0);
  CHECK(file->size(file->ctx) == 3);
  file->close(file->ctx);
  CHECK_ABORTS((void)qa_app_sim_classic_guest_files_open(files, "a.sav", true, true, 9));
  qa_app_sim_classic_guest_files_destroy(files);
  remove_tree(root); free(root);
}

/* ---------------- shared fixture pieces ---------------- */
typedef struct mounted_dll {
  qa_mount_identity identity; qa_content_mount mount; qa_resolved_mount_plan plan; qa_mount_id order[1];
  qa_content_mounted *mounted; const qa_resolved_resource_reference *artifact; qa_resolved_resource_reference artifact_copy; qa_resolved_execution_module execution;
} mounted_dll;
static bool mount_dll(mounted_dll *m, const char *dll, const char *content, const char *provider, bool rerelease) {
  memset(m, 0, sizeof m[0]);
  static char root[1200]; snprintf(root, sizeof root, "%s", dll); char *dir = dirname(root);
  m->identity = qa_create_mount_identity("mount:test:native", content, 1);
  m->mount.kind = QA_CONTENT_MOUNT_LOOSE; m->mount.as.loose.identity = m->identity; m->mount.as.loose.root_path = dir;
  m->order[0] = m->identity.id;
  m->plan.id = "mount-plan:test:native"; m->plan.mounts = &m->mount; m->plan.mount_count = 1; m->plan.default_order = m->order; m->plan.default_order_count = 1;
  qa_open_mount_options options; memset(&options, 0, sizeof options);
  m->mounted = qa_open_mount_plan(&m->plan, &options);
  if (m->mounted == NULL) return false;
  const char *name = rerelease ? "game_x64.dll" : "gamex86.dll";
  if (!qa_content_mounted_resolve(m->mounted, name, &m->artifact) || m->artifact == NULL) return false;
  /* The mounter's resolved-reference array can move as later opens append; keep a copy. */
  m->artifact_copy = *m->artifact; m->artifact_copy.digest = strdup(m->artifact->digest); m->artifact_copy.requested_path = strdup(m->artifact->requested_path);
  m->artifact_copy.id = strdup(m->artifact->id); m->artifact = &m->artifact_copy;
  m->execution.kind = QA_EXECUTION_MODULE_NATIVE; m->execution.owner.provider = provider; m->execution.owner.content = m->identity.content;
  m->execution.role = "server-game"; m->execution.api.kind = QA_GAME_API_Q2_GAME;
  m->execution.api.as.q2_game.kind = rerelease ? QA_Q2_GAME_API_RERELEASE : QA_Q2_GAME_API_CLASSIC;
  m->execution.api.as.q2_game.version = rerelease ? 2023 : 3;
  qa_native_abi abi = rerelease ? (qa_native_abi){"windows-x86-64", "pe32+", 8, "microsoft-x64"} : (qa_native_abi){"windows-i386", "pe32", 4, "cdecl"};
  m->execution.profile = abi; m->execution.artifact = m->artifact;
  return true;
}
static void unmount_dll(mounted_dll *m) { free((char *)m->artifact_copy.digest); free((char *)m->artifact_copy.requested_path); free((char *)m->artifact_copy.id); qa_content_mounted_destroy(m->mounted); }

/* ---------------- classic host lifecycle (host.test.ts over the source) ---------------- */
typedef struct save_file { char path[64]; uint8_t *bytes; size_t length; bool exists; } save_file;
typedef struct handle { qa_windows_file iface; save_file *file; bool write, closed; } handle;
typedef struct host_fixture {
  qa_identity_owner identity; qa_session_actor_registry *actors; qa_actor_callback_table *callbacks; qa_shared_body_table *bodies;
  qa_gameplay_authority *combat; qa_shared_inventory_table *inventory; qa_cvar_registry *cvars; qa_q2_foundation_host engine;
  char prints[65536]; size_t print_length; char *configuration[2080];
  char *resources[3][256]; size_t resource_counts[3];
  save_file files[8]; size_t file_count; handle handles[64]; size_t handle_count;
} host_fixture;
static host_fixture *host_ctx;
static void unavailable(void) { fprintf(stderr, "This startup check does not provide a loaded collision world, clients, or network transport\n"); abort(); }
static save_file *find_save(host_fixture *f, const char *path) { for (size_t i = 0; i < f->file_count; i++) if (strcmp(f->files[i].path, path) == 0) return &f->files[i]; return NULL; }
static size_t h_read(void *ctx, size_t offset, uint8_t *out, size_t length) {
  handle *h = ctx; if (h->closed) unavailable();
  if (offset >= h->file->length) return 0;
  size_t n = h->file->length - offset < length ? h->file->length - offset : length; memcpy(out, h->file->bytes + offset, n); return n;
}
static int64_t h_write(void *ctx, size_t offset, const uint8_t *bytes, size_t length) {
  handle *h = ctx; if (h->closed || !h->write) unavailable();
  if (offset + length > h->file->length) { h->file->bytes = realloc(h->file->bytes, offset + length); memset(h->file->bytes + h->file->length, 0, offset + length - h->file->length); h->file->length = offset + length; }
  memcpy(h->file->bytes + offset, bytes, length); return (int64_t)length;
}
static int64_t h_size(void *ctx) { handle *h = ctx; return (int64_t)h->file->length; }
static void h_truncate(void *ctx, size_t length) { handle *h = ctx; h->file->bytes = realloc(h->file->bytes, length + 1); if (length > h->file->length) memset(h->file->bytes + h->file->length, 0, length - h->file->length); h->file->length = length; }
static void h_flush(void *ctx) { (void)ctx; }
static void h_close(void *ctx) { ((handle *)ctx)->closed = true; }
static qa_windows_file *saves_open(void *ctx, const char *path, bool read, bool write, int creation) {
  CTX_OK(ctx); host_fixture *f = host_ctx; (void)read;
  save_file *file = find_save(f, path);
  if (creation == 2) { if (file == NULL) { file = &f->files[f->file_count++]; snprintf(file->path, sizeof file->path, "%s", path); } free(file->bytes); file->bytes = NULL; file->length = 0; file->exists = true; }
  if (file == NULL || !file->exists) return NULL;
  handle *h = &f->handles[f->handle_count++]; memset(h, 0, sizeof *h);
  h->file = file; h->write = write; h->iface.ctx = h;
  h->iface.read = h_read; h->iface.write = h_write; h->iface.size = h_size; h->iface.truncate = h_truncate; h->iface.flush = h_flush; h->iface.close = h_close;
  return &h->iface;
}
static void fx_print(void *ctx, const char *destination, bool has_entity, qa_guest_address entity, double level, const char *text) {
  CTX_OK(ctx); (void)destination; (void)has_entity; (void)entity; (void)level;
  size_t n = strlen(text); if (host_ctx->print_length + n < sizeof host_ctx->prints) { memcpy(host_ctx->prints + host_ctx->print_length, text, n + 1); host_ctx->print_length += n; }
}
static void fx_configstring(void *ctx, double index, const char *value) { CTX_OK(ctx); free(host_ctx->configuration[(size_t)index]); host_ctx->configuration[(size_t)index] = strdup(value); }
static double fx_resource_index(void *ctx, const char *kind, const char *name) {
  CTX_OK(ctx); if (name[0] == '\0') return 0;
  int k = strcmp(kind, "model") == 0 ? 0 : strcmp(kind, "sound") == 0 ? 1 : 2;
  for (size_t i = 0; i < host_ctx->resource_counts[k]; i++) if (strcmp(host_ctx->resources[k][i], name) == 0) return (double)i + 1;
  host_ctx->resources[k][host_ctx->resource_counts[k]++] = strdup(name); return (double)host_ctx->resource_counts[k];
}
static void fx_bind(void *ctx, qa_raw_entity_view record, qa_owned_actor actor) { CTX_OK(ctx); (void)record; (void)actor; }
static const char *const *fx_command(void *ctx, size_t *count, const char **args) { CTX_OK(ctx); *count = 0; *args = ""; return NULL; }
static void fx_sound(void *ctx, const qa_vec3 *o, bool h, qa_guest_address e, double c, double i, double v, double a, double t) { (void)ctx; (void)o; (void)h; (void)e; (void)c; (void)i; (void)v; (void)a; (void)t; unavailable(); }
static bool fx_areas(void *ctx, double a, double b) { (void)ctx; (void)a; (void)b; unavailable(); return false; }
static qa_compat_q2_classic_world_link fx_link(void *ctx, qa_bounds b) { (void)ctx; (void)b; unavailable(); qa_compat_q2_classic_world_link l; memset(&l, 0, sizeof l); return l; }
static const qa_actor_id *fx_box(void *ctx, qa_bounds b, const char *k, size_t *n) { (void)ctx; (void)b; (void)k; (void)n; unavailable(); return NULL; }
static void fx_message(void *ctx, const char *o, const qa_guest_call_value *v, size_t n, const qa_guest_call_context *c) { (void)ctx; (void)o; (void)v; (void)n; (void)c; unavailable(); }
static void fx_add(void *ctx, const char *t) { (void)ctx; (void)t; unavailable(); }
static void fx_graph(void *ctx, double v, double c) { (void)ctx; (void)v; (void)c; unavailable(); }
static void fx_pmove(void *ctx, qa_guest_address a, qa_compat_q2_classic_guest_host *h) { (void)ctx; (void)a; (void)h; unavailable(); }
static qa_bounds fx_bounds(void *ctx, qa_owned_actor actor, qa_body_state state) { CTX_OK(ctx); return qa_translated_body_bounds(actor, state); }
static void fx_on_link(void *ctx, qa_linked_body body) { (void)ctx; (void)body; unavailable(); }
static void fx_on_unlink(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; unavailable(); }
static void fx_services(void *ctx, qa_compat_q2_memory *memory, qa_compat_q2_classic_engine_services *out) {
  CTX_OK(ctx); (void)memory; host_fixture *f = host_ctx;
  memset(out, 0, sizeof *out);
  out->ctx = SENTINEL; out->engine = &f->engine; out->cvars = f->cvars; out->bind_ctx = SENTINEL; out->bind_entity = fx_bind;
  out->print = fx_print; out->configstring = fx_configstring; out->resource_index = fx_resource_index; out->sound = fx_sound;
  out->areas_connected = fx_areas; out->world_link = fx_link; out->box_edicts = fx_box; out->message = fx_message;
  out->command = fx_command; out->add_command = fx_add; out->debug_graph = fx_graph; out->pmove_ctx = SENTINEL; out->pmove = fx_pmove;
}
static void cvar_register(qa_cvar_registry *cvars, const char *name, const char *value) { qa_cvar_snapshot out; qa_cvar_registry_register(cvars, name, value, 0, &out); qa_cvar_read_free(&out); }
static qa_cvar_registry *make_cvars(qa_session_id session, qa_command_dialect dialect) {
  qa_cvar_registry_options options; memset(&options, 0, sizeof options);
  options.dialect = dialect; options.context.session = session; options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  return qa_cvar_registry_create(&options);
}
static void test_classic_host_lifecycle(void) {
  char dll[1024]; corpus("Quake 2/ctf/gamex86.dll", dll, sizeof dll);
  if (!exists(dll)) { printf("skip: classic CTF DLL absent\n"); return; }
  host_fixture *f = calloc(1, sizeof *f); host_ctx = f;
  CHECK(qa_identity_owner_create(&f->identity, "classic-native-check"));
  f->actors = qa_session_actor_registry_create(&f->identity, 4096); f->callbacks = qa_actor_callback_table_create(f->actors);
  qa_body_link_hooks hooks = {SENTINEL, fx_bounds, fx_on_link, fx_on_unlink};
  f->bodies = qa_shared_body_table_create(f->actors, hooks);
  qa_gameplay_hooks gameplay; memset(&gameplay, 0, sizeof gameplay);
  f->combat = qa_gameplay_authority_create(f->actors, f->callbacks, gameplay); f->inventory = qa_shared_inventory_table_create(f->actors);
  f->cvars = make_cvars(f->identity.session, QA_COMMAND_DIALECT_Q2_CLASSIC);
  f->engine.ctx = SENTINEL; f->engine.actors = f->actors; f->engine.callbacks = f->callbacks; f->engine.bodies = f->bodies; f->engine.combat = f->combat; f->engine.inventory = f->inventory;
  mounted_dll m; CHECK(mount_dll(&m, dll, "q2:classic:ctf:installed", "q2:ctf-native", false));
  qa_app_sim_prepared_classic_guest prepared; qa_app_sim_prepare_classic_guest(&m.execution, m.mounted, &prepared);
  CHECK(strcmp(prepared.edition, "classic") == 0 && prepared.length > 100000);
  qa_app_sim_classic_guest_source_options options; memset(&options, 0, sizeof options);
  options.services_ctx = SENTINEL; options.services = fx_services;
  options.capabilities.ctx = SENTINEL; options.capabilities.open_file = saves_open;
  options.has_instruction_budget = true; options.instruction_budget = 2000000;
  qa_app_sim_classic_guest_source *source = qa_app_sim_classic_guest_source_create(&prepared, &options);
  qa_compat_q2_classic_guest_host *host = qa_app_sim_classic_guest_source_host(source);
  qa_app_sim_classic_guest_source_init(source);
  qa_compat_q2_classic_edicts *edicts = qa_compat_q2_classic_guest_host_get_edicts(host);
  CHECK(qa_compat_q2_classic_edicts_descriptor(edicts).count == 5);
  CHECK(qa_compat_q2_classic_edicts_descriptor(edicts).capacity == 1024);
  CHECK(qa_cvar_registry_variable_value(f->cvars, "deathmatch") == 1);
  qa_compat_q2_classic_guest_host_spawn_entities(host, "native_check", "{ \"classname\" \"worldspawn\" \"message\" \"Native API check\" }", "");
  qa_raw_entity_view world = qa_compat_q2_classic_edicts_at(edicts, 0);
  CHECK(qa_compat_q2_view_i32(world.bytes.bytes, 88) == 1);
  CHECK(world.current_actor(world.ctx).has_value);
  CHECK(f->configuration[0] != NULL && strcmp(f->configuration[0], "Native API check") == 0);
  CHECK(strstr(f->prints, "0 entities inhibited") != NULL);
  qa_compat_q2_classic_client_connect admission = qa_compat_q2_classic_guest_host_client_connect(host, 1, "\\name\\Native Guest\\skin\\male/grunt\\ip\\127.0.0.1");
  CHECK(admission.allowed); free(admission.userinfo);
  qa_compat_q2_classic_edicts_set_client_ping(edicts, 1, 42);
  const uint8_t *client = qa_compat_q2_classic_edicts_client_prefix(edicts, 1);
  CHECK(client != NULL && qa_compat_q2_view_i32(client, 184) == 42);
  qa_compat_q2_classic_guest_host_run_frame(host);
  qa_compat_q2_classic_guest_host_save(host, "WriteGame", "save/game.sav", false);
  CHECK(find_save(f, "save/game.sav") != NULL && find_save(f, "save/game.sav")->length > 1000);
  qa_compat_q2_classic_guest_host_save(host, "WriteLevel", "save/level.sav", false);
  CHECK(find_save(f, "save/level.sav") != NULL && find_save(f, "save/level.sav")->length > 1000);
  qa_compat_q2_classic_guest_host_save(host, "ReadGame", "save/game.sav", false);
  CHECK(qa_compat_q2_classic_edicts_descriptor(edicts).capacity == 1024);
  printf("classic CTF: %llu guest instructions\n", (unsigned long long)qa_app_sim_native_guest_instructions(qa_app_sim_classic_guest_source_guest(source)));
  qa_app_sim_classic_guest_source_close(source);
  size_t owned = 0; (void)qa_session_actor_registry_owned_by(f->actors, "q2:ctf-native", &owned); CHECK(owned == 0);
  CHECK(qa_app_sim_native_guest_mapping_count(qa_app_sim_classic_guest_source_guest(source)) == 0);
  qa_app_sim_classic_guest_source_destroy(source);
  qa_app_sim_prepared_classic_guest_free(&prepared); unmount_dll(&m);
  for (size_t i = 0; i < 2080; i++) free(f->configuration[i]);
  for (int k = 0; k < 3; k++) for (size_t i = 0; i < f->resource_counts[k]; i++) free(f->resources[k][i]);
  for (size_t i = 0; i < f->file_count; i++) free(f->files[i].bytes);
  qa_cvar_registry_destroy(f->cvars); qa_shared_inventory_table_destroy(f->inventory); qa_gameplay_authority_destroy(f->combat);
  qa_shared_body_table_destroy(f->bodies); qa_actor_callback_table_destroy(f->callbacks); qa_session_actor_registry_destroy(f->actors);
  free(f); host_ctx = NULL;
}

/* ---------------- candidate fixture (world.test.ts candidate()) ---------------- */
static qa_q2_decoded_map base1; static bool base1_loaded;
static bool load_base1(void) {
  if (base1_loaded) return true;
  char pak[1024]; corpus("Quake 2/baseq2/pak0.pak", pak, sizeof pak);
  if (!exists(pak)) return false;
  qa_archive_handle *archive = qa_open_archive(pak, false, QA_ARCHIVE_FORMAT_PAK);
  const qa_archive_entry *entry = NULL;
  if (qa_archive_find_entries(archive, "maps/base1.bsp", (qa_archive_path_comparison)0, &entry, 1) == 0) return false;
  size_t length = 0; uint8_t *bytes = qa_archive_read_entry_entry(archive, entry, &length);
  qa_decode_q2_map(bytes, length, "base1", NULL, &base1); free(bytes); qa_archive_destroy(archive);
  base1_loaded = true; return true;
}
static const char *entity_get(const qa_entity_record *record, const char *key) {
  const char *found = NULL; for (size_t i = 0; i < record->pair_count; i++) if (strcmp(record->pairs[i].key, key) == 0) found = record->pairs[i].value;
  return found;
}
static char *base1_spawn_origin(void) {
  qa_entities entities; CHECK(qa_parse_entities(base1.world.entities, strlen(base1.world.entities), NULL, &entities));
  char *origin = strdup("0 0 128");
  for (size_t i = 0; i < entities.record_count; i++) {
    const char *classname = entity_get(&entities.records[i], "classname");
    if (classname != NULL && strcmp(classname, "info_player_start") == 0) { const char *value = entity_get(&entities.records[i], "origin"); if (value != NULL) { free(origin); origin = strdup(value); } break; }
  }
  qa_entities_free(&entities); return origin;
}
typedef struct candidate {
  qa_identity_owner identity; qa_session_actor_registry *actors; qa_actor_callback_table *callbacks; qa_shared_scene_queries *scene;
  qa_app_sim_shared_physics *physics; qa_gameplay_authority *combat; qa_shared_inventory_table *inventory; qa_cvar_registry *cvars; bool owns_cvars;
  qa_q2_foundation_host engine; const char *provider;
  double portals[64]; bool portal_open[64]; size_t portal_count;
  char prints[65536]; size_t print_length; size_t commands; size_t models;
  qa_app_sim_classic_guest_services_options services;
} candidate;
static void cand_print_text(candidate *c, const char *text) { size_t n = strlen(text); if (c->print_length + n < sizeof c->prints) { memcpy(c->prints + c->print_length, text, n + 1); c->print_length += n; } }
static double cand_order(void *ctx, qa_actor_id a, qa_actor_id b) { (void)ctx; return (double)(a.slot - b.slot); }
static bool cand_world_actor(void *ctx, qa_actor_id *out) { candidate *c = ctx; qa_owned_actor world; if (!qa_session_actor_registry_at_source(c->actors, c->provider, 0, &world)) { fprintf(stderr, "Missing native world fixture value\n"); abort(); } *out = world.id; return true; }
static void cand_blocked(void *ctx, qa_owned_actor pusher, qa_actor_id other) { (void)ctx; (void)pusher; (void)other; }
static qa_trace_result cand_trace(void *ctx, qa_q2_trace_request request) { candidate *c = ctx; return qa_app_sim_shared_physics_trace(c->physics, request, "q2"); }
static qa_point_contents_query point_query(qa_vec3 point) {
  qa_point_contents_query q; memset(&q, 0, sizeof q); q.point = point; q.policy.kind = "q2"; q.policy.contents_mask = -1; q.policy.leaf_contents = "merged"; q.numeric = qa_q2_donor_profile; return q;
}
static double cand_contents(void *ctx, qa_vec3 point) { candidate *c = ctx; qa_point_contents_result r = qa_shared_point_contents(c->scene, point_query(point)); return strcmp(r.kind, "q2") == 0 ? r.merged : r.contents; }
static bool cand_visible(candidate *c, qa_vec3 a, qa_vec3 b, const char *kind) {
  return qa_shared_cluster_visible(c->scene, qa_shared_leaf_cluster(c->scene, qa_shared_point_leaf(c->scene, a)), qa_shared_leaf_cluster(c->scene, qa_shared_point_leaf(c->scene, b)), kind);
}
static bool cand_pvs(void *ctx, qa_vec3 a, qa_vec3 b) { return cand_visible(ctx, a, b, "pvs"); }
static bool cand_phs(void *ctx, qa_vec3 a, qa_vec3 b) { return cand_visible(ctx, a, b, "phs"); }
static void cand_portal(void *ctx, double portal, bool open) {
  candidate *c = ctx; size_t i = 0; while (i < c->portal_count && c->portals[i] != portal) i++;
  if (i == c->portal_count && c->portal_count < 64) c->portals[c->portal_count++] = portal;
  if (i < 64) c->portal_open[i] = open;
  qa_shared_set_area_portal_state(c->scene, portal, open);
}
static void cand_set_solid(void *ctx, qa_owned_actor actor, const char *solid, const double *model) {
  candidate *c = ctx; qa_app_sim_shared_solid value; memset(&value, 0, sizeof value);
  value.solid = solid; value.family = "q2"; if (model != NULL) { value.has_model = true; value.model = *model; }
  qa_app_sim_shared_physics_set_solid(c->physics, actor, value);
}
static qa_bounds cand_model_bounds(void *ctx, double model) { candidate *c = ctx; return qa_shared_model_bounds(c->scene, model); }
static void cand_emit(void *ctx, qa_q2_presentation_event event) {
  candidate *c = ctx; if (strcmp(event.kind, "print") == 0 && event.text != NULL) cand_print_text(c, event.text);
  if (strcmp(event.kind, "model") == 0) c->models++;
}
static void cand_admit(void *ctx, qa_raw_entity_view record, qa_owned_actor actor) { CTX_OK(ctx); (void)record; (void)actor; }
static candidate *cand_owner;
static void cand_collision(void *ctx, qa_owned_actor actor, qa_actor_collision collision) { CTX_OK(ctx); qa_app_sim_shared_physics_set_collision(cand_owner->physics, actor, collision); }
static void cand_print(void *ctx, const char *text) { CTX_OK(ctx); cand_print_text(cand_owner, text); }
static const char *const *cand_command(void *ctx, size_t *count, const char **args) { CTX_OK(ctx); *count = 0; *args = ""; return NULL; }
static void cand_add_command(void *ctx, const char *text) { CTX_OK(ctx); (void)text; cand_owner->commands++; }
static void cand_graph(void *ctx, double value, double color) { CTX_OK(ctx); (void)value; (void)color; cand_print_text(cand_owner, "graph"); }
static void cand_init(candidate *c, const char *provider, bool deathmatch, qa_cvar_registry *shared_cvars) {
  memset(c, 0, sizeof *c); cand_owner = c; c->provider = provider;
  CHECK(qa_identity_owner_create(&c->identity, "classic-original-candidate"));
  c->actors = qa_session_actor_registry_create(&c->identity, 4096); c->callbacks = qa_actor_callback_table_create(c->actors);
  qa_decoded_world world; memset(&world, 0, sizeof world); world.kind = QA_DECODED_WORLD_Q2; world.as.q2 = &base1.world;
  c->scene = qa_create_scene_queries(world);
  qa_app_sim_shared_physics_options physics; memset(&physics, 0, sizeof physics);
  physics.ctx = c; physics.actors = c->actors; physics.callbacks = c->callbacks; physics.scene = c->scene; physics.numeric = qa_q2_donor_profile;
  physics.source_order = cand_order; physics.world_actor = cand_world_actor; physics.on_blocked = cand_blocked; physics.q2_edition = strstr(provider, "rerelease") != NULL ? "rerelease" : "classic";
  c->physics = qa_app_sim_shared_physics_create(physics);
  qa_gameplay_hooks hooks; memset(&hooks, 0, sizeof hooks);
  c->combat = qa_gameplay_authority_create(c->actors, c->callbacks, hooks); c->inventory = qa_shared_inventory_table_create(c->actors);
  if (shared_cvars != NULL) c->cvars = shared_cvars;
  else {
    c->cvars = make_cvars(c->identity.session, QA_COMMAND_DIALECT_Q2_CLASSIC); c->owns_cvars = true;
    cvar_register(c->cvars, "ctf", deathmatch ? "1" : "0"); cvar_register(c->cvars, "deathmatch", deathmatch ? "1" : "0"); cvar_register(c->cvars, "coop", "0");
  }
  qa_q2_foundation_host *e = &c->engine; e->ctx = c; e->actors = c->actors; e->callbacks = c->callbacks; e->bodies = qa_app_sim_shared_physics_bodies(c->physics);
  e->combat = c->combat; e->inventory = c->inventory; e->trace = cand_trace; e->point_contents = cand_contents; e->in_pvs = cand_pvs; e->in_phs = cand_phs;
  e->set_area_portal = cand_portal; e->set_solid = cand_set_solid; e->inline_model_bounds = cand_model_bounds; e->emit = cand_emit;
  qa_app_sim_classic_guest_services_options *o = &c->services;
  o->numeric = *qa_create_numeric_operations(&qa_q2_donor_profile); o->scene = c->scene; o->cvars = c->cvars; o->max_clients = 1; o->map_path = "maps/base1.bsp";
  o->admit = (qa_app_sim_guest_admit){SENTINEL, cand_admit}; o->collision = (qa_app_sim_guest_collision){SENTINEL, cand_collision};
  o->print = (qa_app_sim_guest_print){SENTINEL, cand_print}; o->command = (qa_app_sim_guest_command){SENTINEL, cand_command};
  o->add_command = (qa_app_sim_guest_add_command){SENTINEL, cand_add_command}; o->debug_graph = (qa_app_sim_guest_debug_graph){SENTINEL, cand_graph};
  o->engine = &c->engine;
}
static void cand_free(candidate *c) {
  qa_app_sim_shared_physics_destroy(c->physics); qa_shared_scene_queries_destroy(c->scene);
  if (c->owns_cvars) qa_cvar_registry_destroy(c->cvars);
  qa_shared_inventory_table_destroy(c->inventory); qa_gameplay_authority_destroy(c->combat);
  qa_actor_callback_table_destroy(c->callbacks); qa_session_actor_registry_destroy(c->actors);
}
static qa_app_sim_classic_guest_map_services cand_binding(candidate *c) {
  qa_app_sim_classic_guest_map_services b; memset(&b, 0, sizeof b);
  b.engine = &c->engine; b.scene = c->scene; b.map_path = "maps/base1.bsp"; b.admit = c->services.admit; b.collision = c->services.collision; b.print = c->services.print;
  b.command = c->services.command; b.add_command = c->services.add_command; b.debug_graph = c->services.debug_graph;
  return b;
}
static void next_frame(void *ctx) { CTX_OK(ctx); }

/* services.test.ts: actual DLL shares BSP collision, borrowed actors, player state, wire messages and disk lifetime. */
static qa_app_sim_classic_guest_services *services_under_test;
static void build_services_fixture(void *ctx, qa_compat_q2_memory *memory, qa_compat_q2_classic_engine_services *out) {
  CTX_OK(ctx); candidate *c = cand_owner; c->services.max_clients = 4;
  services_under_test = qa_app_sim_classic_guest_services_create(memory, &c->services);
  *out = qa_app_sim_classic_guest_services_engine(services_under_test);
}
static void test_classic_services_real(void) {
  char dll[1024]; corpus("Quake 2/ctf/gamex86.dll", dll, sizeof dll);
  if (!exists(dll) || !load_base1()) { printf("skip: CTF DLL or base1 absent\n"); return; }
  candidate *c = calloc(1, sizeof *c); cand_init(c, "q2:ctf-native", false, NULL);
  cvar_register(c->cvars, "maxclients", "4");
  char *root = temp_dir("classic-services-");
  qa_app_sim_classic_guest_files *files = qa_app_sim_classic_guest_files_create(root, NULL, 0);
  mounted_dll m; CHECK(mount_dll(&m, dll, "q2:classic:ctf:installed", "q2:ctf-native", false));
  qa_app_sim_prepared_classic_guest prepared; qa_app_sim_prepare_classic_guest(&m.execution, m.mounted, &prepared);
  qa_app_sim_classic_guest_source_options options; memset(&options, 0, sizeof options);
  options.services_ctx = SENTINEL; options.services = build_services_fixture; options.capabilities.ctx = files; options.capabilities.open_file = qa_app_sim_classic_guest_files_open;
  options.has_instruction_budget = true; options.instruction_budget = 8000000;
  qa_app_sim_classic_guest_source *source = qa_app_sim_classic_guest_source_create(&prepared, &options);
  qa_app_sim_classic_guest_services *adapter = services_under_test;
  qa_compat_q2_classic_guest_host *host = qa_app_sim_classic_guest_source_host(source);
  qa_app_sim_classic_guest_services_bind_host(adapter, host, NULL); qa_app_sim_classic_guest_source_init(source);
  char *spawn = base1_spawn_origin(); char entities[512];
  snprintf(entities, sizeof entities, "{ \"classname\" \"worldspawn\" \"message\" \"Real native shared scene\" } { \"classname\" \"info_player_deathmatch\" \"origin\" \"%s\" }", spawn); free(spawn);
  qa_compat_q2_classic_guest_host_spawn_entities(host, "base1", entities, "");
  qa_app_sim_classic_guest_services_complete_spawn(adapter);
  const char *sky = qa_app_sim_classic_guest_services_configstring(adapter, 5); CHECK(sky != NULL && strlen(sky) > 64);
  CHECK(qa_app_sim_classic_guest_services_resource_index(adapter, "model", "*1") == 2);
  qa_compat_q2_classic_client_connect connect = qa_compat_q2_classic_guest_host_client_connect(host, 1, "\\name\\Native\\skin\\male/grunt\\ip\\127.0.0.1");
  CHECK(connect.allowed); free(connect.userinfo);
  qa_compat_q2_classic_guest_host_client_event(host, "ClientBegin", 1);
  for (int frame = 0; frame < 4; frame++) { uint8_t command[16] = {0}; command[0] = 100; command[8] = 100; qa_compat_q2_classic_guest_host_client_think(host, 1, command, 16); qa_compat_q2_classic_guest_host_run_frame(host); }
  qa_app_sim_classic_guest_services_publish_entities(adapter);
  qa_compat_q2_classic_edicts *edicts = qa_compat_q2_classic_guest_host_get_edicts(host);
  qa_raw_entity_view player = qa_compat_q2_classic_edicts_at(edicts, 1); qa_actor_id_result actor = player.current_actor(player.ctx); CHECK(actor.has_value);
  qa_linked_body_result linked = qa_shared_body_table_linked(qa_app_sim_shared_physics_bodies(c->physics), actor.value); CHECK(linked.has_value);
  CHECK(qa_app_sim_classic_guest_services_player_state(adapter, 1).view.fov > 0);
  CHECK(linked.has_value && linked.value.absolute_bounds.min.x == qa_compat_q2_view_f32(player.bytes.bytes, 212));
  qa_spatial_actor spatial; CHECK(qa_shared_linked_actor(c->scene, actor.value, &spatial) && spatial.body.link_count == linked.value.link_count);
  size_t count = 0; const qa_app_sim_classic_guest_message *messages = qa_app_sim_classic_guest_services_drain_messages(adapter, &count);
  bool some = false; for (size_t i = 0; i < count; i++) some = some || messages[i].length > 0; CHECK(some);
  CHECK(c->models > 0);
  double sound = qa_app_sim_classic_guest_services_resource_index(adapter, "sound", "world/test.wav"); (void)qa_app_sim_classic_guest_services_drain_messages(adapter, &count);
  qa_app_sim_classic_guest_services_sound(adapter, NULL, true, player.address, 16, sound, 0.5, 0, 0.05);
  messages = qa_app_sim_classic_guest_services_drain_messages(adapter, &count);
  const uint8_t expected_sound[] = {9, 27, (uint8_t)sound, 127, 0, 50, 8, 0};
  CHECK(count >= 1 && messages[0].length == 8 && memcmp(messages[0].bytes, expected_sound, 8) == 0 && messages[0].reliable);
  qa_compat_q2_memory *memory = qa_app_sim_classic_guest_source_memory(source);
  qa_guest_call_context context; memset(&context, 0, sizeof context); context.module = memory->module;
  context.callback.kind = QA_GUEST_CALLBACK_NATIVE_GUEST; context.callback.module = memory->module; context.callback.address = qa_compat_q2_classic_guest_host_get_imports(host); context.callback.abi = qa_compat_q2_classic_abi;
  qa_guest_call_value long_value = qa_compat_q2_call_int32(0x12345678);
  (void)qa_compat_q2_classic_guest_host_import_call(host, "WriteLong", &context, &long_value, 1);
  qa_guest_call_value unicast_null[2] = {qa_compat_q2_call_pointer(false, player.address), qa_compat_q2_call_int32(1)};
  (void)qa_compat_q2_classic_guest_host_import_call(host, "unicast", &context, unicast_null, 2);
  (void)qa_app_sim_classic_guest_services_drain_messages(adapter, &count); CHECK(count == 0);
  qa_guest_call_value unicast_player[2] = {qa_compat_q2_call_pointer(true, player.address), qa_compat_q2_call_int32(1)};
  (void)qa_compat_q2_classic_guest_host_import_call(host, "unicast", &context, unicast_player, 2);
  messages = qa_app_sim_classic_guest_services_drain_messages(adapter, &count);
  const uint8_t long_bytes[] = {0x78, 0x56, 0x34, 0x12}; CHECK(count == 1 && messages[0].length == 4 && memcmp(messages[0].bytes, long_bytes, 4) == 0);
  qa_raw_entity_view brush = qa_compat_q2_classic_edicts_at(edicts, 5);
  qa_compat_q2_view_set_i32((uint8_t *)brush.bytes.bytes, 248, 3); qa_compat_q2_view_set_f32((uint8_t *)brush.bytes.bytes, 20, 45);
  qa_guest_call_value setmodel[2] = {qa_compat_q2_call_pointer(true, brush.address), qa_compat_q2_call_pointer(true, qa_compat_q2_allocate_classic_string(memory, "*1"))};
  (void)qa_compat_q2_classic_guest_host_import_call(host, "setmodel", &context, setmodel, 2);
  qa_actor_id_result brush_actor = brush.current_actor(brush.ctx); CHECK(brush_actor.has_value);
  qa_linked_body_result brush_link = qa_shared_body_table_linked(qa_app_sim_shared_physics_bodies(c->physics), brush_actor.value); CHECK(brush_link.has_value);
  CHECK(brush_link.value.absolute_bounds.max.x == qa_compat_q2_view_f32(brush.bytes.bytes, 224));
  double radius = 0; for (int i = 0; i < 6; i++) { double v = fabs(qa_compat_q2_view_f32(brush.bytes.bytes, 188 + (size_t)i * 4)); if (v > radius) radius = v; }
  CHECK(brush_link.value.absolute_bounds.max.x == (double)(float)((double)(float)(qa_compat_q2_view_f32(brush.bytes.bytes, 4) + radius) + 1));
  qa_compat_q2_classic_guest_host_save(host, "WriteGame", "save/game.sav", false);
  char path[1200]; snprintf(path, sizeof path, "%s/save/game.sav", root); struct stat st; CHECK(stat(path, &st) == 0 && st.st_size > 1000);
  qa_app_sim_classic_guest_source_close(source);
  size_t owned = 0; (void)qa_session_actor_registry_owned_by(c->actors, "q2:ctf-native", &owned); CHECK(owned == 0);
  CHECK(qa_app_sim_native_guest_mapping_count(qa_app_sim_classic_guest_source_guest(source)) == 0);
  qa_app_sim_classic_guest_source_destroy(source); qa_app_sim_classic_guest_services_destroy(adapter);
  qa_app_sim_prepared_classic_guest_free(&prepared); unmount_dll(&m); qa_app_sim_classic_guest_files_destroy(files);
  remove_tree(root); free(root); cand_free(c); free(c);
}

static void test_classic_user_command(void) {
  qa_q2_user_command command; memset(&command, 0, sizeof command);
  command.milliseconds = 100; command.angle_shorts.values[0] = -32768; command.angle_shorts.values[1] = 32767; command.angle_shorts.values[2] = 65535;
  command.forward_move = -20; command.light_level = 128;
  uint8_t bytes[16]; qa_app_sim_classic_guest_user_command(command, bytes);
  const uint8_t expected[16] = {100, 0, 0, 128, 255, 127, 255, 255, 236, 255, 0, 0, 0, 0, 0, 128};
  CHECK(memcmp(bytes, expected, 16) == 0);
}
static qa_q2_user_command walk_command(void) {
  qa_q2_user_command command; memset(&command, 0, sizeof command); command.milliseconds = 100; command.forward_move = 100; command.light_level = 128; return command;
}
static qa_app_sim_classic_guest_world *make_world(candidate *c, const qa_app_sim_prepared_classic_guest *prepared, qa_classic_original_save_files *files) {
  qa_app_sim_classic_guest_world_options options; memset(&options, 0, sizeof options);
  options.prepared = prepared; options.services = c->services; options.has_instruction_budget = true; options.instruction_budget = 8000000;
  options.capabilities.ctx = files; options.capabilities.open_file = (qa_windows_file *(*)(void *, const char *, bool, bool, int))qa_classic_original_save_files_open_file;
  return qa_app_sim_classic_guest_world_create(&options);
}
typedef struct revisit_ctx { qa_app_sim_configstring *configstrings; size_t count; candidate *from; } revisit_ctx;
static void restore_revisit(void *ctx, qa_app_sim_classic_guest_world *world) {
  revisit_ctx *r = ctx; qa_app_sim_classic_guest_world_restore_configstrings(world, r->configstrings, r->count);
  for (size_t i = 0; i < r->from->portal_count; i++) r->from->engine.set_area_portal(r->from->engine.ctx, r->from->portals[i], r->from->portal_open[i]);
}
typedef struct world_op { qa_app_sim_classic_guest_world *world; int op; } world_op;
static void run_world_op(void *ctx) {
  world_op *o = ctx; size_t n;
  switch (o->op) {
    case 0: (void)qa_app_sim_classic_guest_world_source(o->world); break;
    case 1: (void)qa_app_sim_classic_guest_world_services(o->world); break;
    case 2: (void)qa_app_sim_classic_guest_world_clients(o->world, &n); break;
    case 3: (void)qa_app_sim_classic_guest_world_raw_messages(o->world, &n); break;
    case 4: (void)qa_app_sim_classic_guest_world_configstrings(o->world, &n); break;
    case 5: (void)qa_app_sim_classic_guest_world_player_state(o->world, 1); break;
    default: qa_app_sim_classic_guest_world_frame(o->world, 100); break;
  }
}
/* world.test.ts: actual retained Xatrix gamemap keeps DLL globals and revisits level files without game reload. */
static void test_classic_world_travel(void) {
  char dll[1024]; corpus("Quake 2/xatrix/gamex86.dll", dll, sizeof dll);
  if (!exists(dll) || !load_base1()) { printf("skip: Xatrix DLL or base1 absent\n"); return; }
  char *directory = temp_dir("q2-retained-travel-");
  qa_app_sim_classic_guest_files *disk = qa_app_sim_classic_guest_files_create(directory, NULL, 0);
  qa_classic_original_save_files *files = qa_classic_original_save_files_create((qa_q2_classic_open_file_fn)qa_app_sim_classic_guest_files_open, disk);
  mounted_dll m; CHECK(mount_dll(&m, dll, "q2:classic:xatrix:installed", "q2:classic-native", false));
  qa_app_sim_prepared_classic_guest prepared; qa_app_sim_prepare_classic_guest(&m.execution, m.mounted, &prepared);
  char *origin = base1_spawn_origin(); char entities[512];
  snprintf(entities, sizeof entities, "{ \"classname\" \"worldspawn\" \"message\" \"Retained module\" } { \"classname\" \"info_player_start\" \"origin\" \"%s\" }", origin); free(origin);
  candidate *first = calloc(1, sizeof *first); cand_init(first, "q2:classic-native", false, NULL);
  qa_app_sim_classic_guest_world *w1 = make_world(first, &prepared, files);
  qa_app_sim_classic_guest_world_init_loading(w1, next_frame, SENTINEL);
  qa_app_sim_classic_guest_world_spawn_loading(w1, "base1", entities, next_frame, SENTINEL, "");
  CHECK(qa_app_sim_classic_guest_world_admit(w1, 1, "\\name\\Traveler\\skin\\male/grunt").allowed);
  const char *give[] = {"give", "health", "137"}; qa_app_sim_classic_guest_world_command(w1, 1, give, 3, "health 137"); qa_app_sim_classic_guest_world_frame(w1, 100);
  CHECK(qa_app_sim_classic_guest_world_player_state(w1, 1).view.stats.values[1] == 137);
  qa_app_sim_classic_guest_source *retained_source = qa_app_sim_classic_guest_world_source(w1);
  qa_cvar_registry *cvars = first->cvars;
  qa_actor_id old_client; CHECK(qa_app_sim_classic_guest_world_actor(w1, 1, &old_client));
  size_t config_count = 0; const qa_app_sim_configstring *table = qa_app_sim_classic_guest_world_configstrings(w1, &config_count);
  qa_app_sim_configstring *configstrings = calloc(config_count + 1, sizeof *configstrings);
  for (size_t i = 0; i < config_count; i++) { configstrings[i].index = table[i].index; configstrings[i].value = strdup(table[i].value); }
  qa_app_sim_classic_guest_world_write_travel_level(w1, "base1.sav");
  char path[1200]; snprintf(path, sizeof path, "%s/base1.sav", directory); struct stat st; CHECK(stat(path, &st) == 0 && st.st_size > 1000);
  CHECK(qa_session_actor_registry_is_live(first->actors, old_client));
  CHECK(qa_compat_q2_view_i32(qa_compat_q2_classic_edicts_at(qa_compat_q2_classic_guest_host_get_edicts(qa_app_sim_classic_guest_source_host(retained_source)), 1).bytes.bytes, 88) == 1);
  candidate *second = calloc(1, sizeof *second); cand_init(second, "q2:classic-native", false, cvars);
  qa_app_sim_classic_guest_map_services binding = cand_binding(second);
  qa_app_sim_classic_guest_map travel_map = {"base1_travel", entities, ""};
  qa_app_sim_classic_guest_world *w2 = qa_app_sim_classic_guest_world_travel_loading(w1, travel_map, &binding, next_frame, SENTINEL, NULL);
  CHECK(qa_app_sim_classic_guest_world_source(w2) == retained_source);
  CHECK(qa_app_sim_classic_guest_services_options_of(qa_app_sim_classic_guest_world_services(w2))->cvars == cvars);
  CHECK(!qa_session_actor_registry_is_live(first->actors, old_client));
  size_t client_count = 0; const qa_app_sim_classic_guest_client *clients = qa_app_sim_classic_guest_world_clients(w2, &client_count);
  CHECK(client_count == 1 && strcmp(clients[0].phase, "connected") == 0);
  for (int op = 0; op < 7; op++) { world_op o = {w1, op}; qa_caught_error caught; CHECK(!qa_try(run_world_op, &o, &caught)); }
  qa_app_sim_classic_guest_world_close(w1); qa_app_sim_classic_guest_world_discard(w1);
  CHECK(qa_app_sim_native_guest_mapping_count(qa_app_sim_classic_guest_source_guest(retained_source)) > 0);
  qa_app_sim_classic_guest_world_begin(w2, 1); qa_app_sim_classic_guest_world_frame(w2, 100);
  CHECK(qa_app_sim_classic_guest_world_player_state(w2, 1).view.stats.values[1] == 137);
  const char *give2[] = {"give", "health", "149"}; qa_app_sim_classic_guest_world_command(w2, 1, give2, 3, "health 149"); qa_app_sim_classic_guest_world_frame(w2, 100);
  candidate *third = calloc(1, sizeof *third); cand_init(third, "q2:classic-native", false, cvars);
  qa_app_sim_classic_guest_map_services binding3 = cand_binding(third);
  revisit_ctx revisit_state = {configstrings, config_count, first};
  qa_app_sim_classic_guest_revisit revisit = {"base1.sav", &revisit_state, restore_revisit};
  qa_app_sim_classic_guest_map home = {"base1", entities, ""};
  qa_app_sim_classic_guest_world *w3 = qa_app_sim_classic_guest_world_travel_loading(w2, home, &binding3, next_frame, SENTINEL, &revisit);
  CHECK(qa_app_sim_classic_guest_world_source(w3) == retained_source);
  clients = qa_app_sim_classic_guest_world_clients(w3, &client_count); CHECK(client_count == 1 && strcmp(clients[0].phase, "connected") == 0);
  qa_app_sim_classic_guest_world_begin(w3, 1); qa_q2_user_command walk = walk_command(); qa_app_sim_classic_guest_world_think(w3, 1, walk, NULL); qa_app_sim_classic_guest_world_frame(w3, 100);
  qa_q2_player_state returned_state = qa_app_sim_classic_guest_world_player_state(w3, 1);
  CHECK(returned_state.view.stats.values[1] == 149); CHECK(returned_state.movement.type == 0);
  qa_actor_id returned_actor; CHECK(qa_app_sim_classic_guest_world_actor(w3, 1, &returned_actor));
  CHECK(qa_shared_body_table_linked(qa_app_sim_shared_physics_bodies(third->physics), returned_actor).has_value);
  qa_app_sim_classic_guest_world_close(w2); qa_app_sim_classic_guest_world_discard(w2);
  CHECK(qa_app_sim_native_guest_mapping_count(qa_app_sim_classic_guest_source_guest(retained_source)) > 0);
  qa_app_sim_classic_guest_world_close(w3);
  CHECK(qa_app_sim_native_guest_mapping_count(qa_app_sim_classic_guest_source_guest(retained_source)) == 0);
  qa_app_sim_classic_guest_world_destroy(w1); qa_app_sim_classic_guest_world_destroy(w2); qa_app_sim_classic_guest_world_destroy(w3);
  for (size_t i = 0; i < config_count; i++) free((char *)configstrings[i].value);
  free(configstrings);
  qa_app_sim_prepared_classic_guest_free(&prepared); unmount_dll(&m);
  qa_classic_original_save_files_destroy(files); qa_app_sim_classic_guest_files_destroy(disk);
  cand_free(third); free(third); cand_free(second); free(second); cand_free(first); free(first);
  remove_tree(directory); free(directory);
}

/* Rerelease world over the retail API2023 DLL (rerelease native.test.ts lifecycle cases through the world). */
static qa_actor_id cand_world_id(void *ctx) { qa_actor_id id; cand_world_actor(ctx, &id); return id; }
static char *rr_localize(void *ctx, const char *key, const char *const *arguments, size_t count) {
  CTX_OK(ctx); size_t n = strlen(key) + 1; for (size_t i = 0; i < count; i++) n += strlen(arguments[i]) + 1;
  char *out = malloc(n); strcpy(out, key); for (size_t i = 0; i < count; i++) { strcat(out, ":"); strcat(out, arguments[i]); } return out;
}
static void rr_shapes(void *ctx, qa_compat_q2_rerelease_debug_shapes_event event) { CTX_OK(ctx); (void)event; }
static void rr_text(void *ctx, qa_compat_q2_rerelease_world_text_event event) { CTX_OK(ctx); (void)event; }
static void *rr_runtime(void *ctx) { CTX_OK(ctx); return NULL; }
static qa_compat_q2_rerelease_goal_status rr_move(void *ctx, qa_actor_id a, qa_vec3 p, double t) { CTX_OK(ctx); (void)a; (void)p; (void)t; return 0; }
static qa_compat_q2_rerelease_goal_status rr_follow(void *ctx, qa_actor_id a, qa_actor_id b) { CTX_OK(ctx); (void)a; (void)b; return 0; }
static bool rr_now(void *ctx, double *out) { CTX_OK(ctx); *out = 1700000000000.0; return true; }
static bool rr_counter(void *ctx, int64_t *out) { CTX_OK(ctx); *out = 12345678; return true; }
static bool primary_selected(void *ctx, qa_actor_id actor) { CTX_OK(ctx); (void)actor; return true; }
static void primary_completed(void *ctx, qa_actor_id actor, bool reached) { CTX_OK(ctx); (void)actor; (void)reached; }
static void primary_spawned(void *ctx, qa_actor_id actor) { CTX_OK(ctx); (void)actor; }
typedef struct native_client_args { qa_app_sim_q2_native_world world; qa_actor_id actor; } native_client_args;
static void native_client_create_body(void *ctx) {
  native_client_args *args = ctx;
  qa_app_native_q2_client_destroy(qa_app_native_q2_client_create(args->world, 1, args->actor, "q2:rerelease:baseq2:installed"));
}
static void test_rerelease_world(void) {
  char dll[1024]; corpus("Quake 2/rerelease/baseq2/game_x64.dll", dll, sizeof dll);
  if (!exists(dll) || !load_base1()) { printf("skip: rerelease DLL or base1 absent\n"); return; }
  candidate *c = calloc(1, sizeof *c); cand_init(c, "q2:rerelease-native-host", false, NULL);
  c->engine.world_actor = cand_world_id;
  mounted_dll m; CHECK(mount_dll(&m, dll, "q2-rerelease-baseq2", "q2:rerelease-native-host", true));
  CHECK(strcmp(m.artifact->digest, "sha256:045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd") == 0);
  qa_app_sim_prepared_rerelease_guest prepared; qa_app_sim_prepare_rerelease_guest(&m.execution, m.mounted, &prepared);
  CHECK(strcmp(prepared.edition, "rerelease") == 0);
  qa_app_sim_rerelease_guest_world_options options; memset(&options, 0, sizeof options);
  options.prepared = &prepared; options.services.base = c->services; options.services.base.max_clients = 2; options.services.frame_milliseconds = 25;
  options.services.localize = (qa_app_sim_rerelease_localize){SENTINEL, rr_localize};
  options.services.debug_shapes = (qa_app_sim_rerelease_debug_shapes){SENTINEL, rr_shapes}; options.services.world_text = (qa_app_sim_rerelease_world_text){SENTINEL, rr_text};
  options.services.navigation.ctx = SENTINEL; options.services.navigation.runtime = rr_runtime; options.services.navigation.move_to_point = rr_move; options.services.navigation.follow_actor = rr_follow;
  options.capabilities.ctx = SENTINEL; options.capabilities.now_milliseconds = rr_now; options.capabilities.performance_counter = rr_counter;
  options.capabilities.has_performance_frequency = true; options.capabilities.performance_frequency = 10000000;
  qa_app_sim_rerelease_guest_world *world = qa_app_sim_rerelease_guest_world_create(&options);
  qa_app_sim_rerelease_guest_world_init_loading(world, next_frame, SENTINEL);
  char *origin = base1_spawn_origin(); char entities[512];
  snprintf(entities, sizeof entities, "{ \"classname\" \"worldspawn\" \"message\" \"Rerelease native\" } { \"classname\" \"info_player_start\" \"origin\" \"%s\" }", origin); free(origin);
  qa_app_sim_rerelease_guest_world_spawn_loading(world, "base1", entities, next_frame, SENTINEL, "");
  qa_app_sim_rerelease_guest_services *services = qa_app_sim_rerelease_guest_world_services(world);
  CHECK(qa_app_sim_rerelease_guest_services_resource_index(services, "model", "*1") == 2);
  qa_app_sim_classic_guest_connect admitted = qa_app_sim_rerelease_guest_world_admit(world, 1, "\\name\\Native\\skin\\male/grunt\\ip\\127.0.0.1", "", false);
  CHECK(admitted.allowed);
  qa_actor_id actor; CHECK(qa_app_sim_rerelease_guest_world_actor(world, 1, &actor));
  qa_q2_rerelease_user_command command; memset(&command, 0, sizeof command); command.milliseconds = 25; command.forward_move = 200;
  qa_body_state_result before = qa_shared_body_table_read(qa_app_sim_shared_physics_bodies(c->physics), actor); CHECK(before.has_value);
  for (int frame = 0; frame < 8; frame++) { qa_app_sim_rerelease_guest_world_think(world, 1, command, NULL); qa_app_sim_rerelease_guest_world_frame(world, 25); }
  qa_body_state_result after = qa_shared_body_table_read(qa_app_sim_shared_physics_bodies(c->physics), actor); CHECK(after.has_value);
  qa_q2_rerelease_player_state player = qa_app_sim_rerelease_guest_world_player_state(world, 1);
  CHECK(player.view.fov > 0);
  CHECK(after.has_value && before.has_value && (after.value.origin.x != before.value.origin.x || after.value.origin.y != before.value.origin.y));
  CHECK(qa_app_sim_rerelease_guest_world_entity_info(world, 1).active);
  size_t count = 0; (void)qa_app_sim_rerelease_guest_world_entity_states(world, &count); CHECK(count >= 1);
  const qa_app_sim_rerelease_guest_message *messages = qa_app_sim_rerelease_guest_world_raw_messages(world, &count);
  bool dialect = count > 0; for (size_t i = 0; i < count; i++) dialect = dialect && strcmp(messages[i].source_dialect, "q2-multicast-float") == 0; CHECK(dialect);
  size_t raw_count = count;
  CHECK(c->models > 0);
  { /* NativePrimaryWeapons over the real API2023 guest; expected values from the TS class under bun on the same world. */
    const qa_compat_q2_native_primary_weapon_profile *profile = qa_compat_q2_get_native_primary_weapon_profile(m.artifact->digest);
    CHECK(profile != NULL);
    if (profile != NULL) {
      qa_app_sim_native_primary_weapon_context context; qa_app_sim_q2_native_world native = {true, NULL, world};
      qa_compat_q2_native_primary_weapon_hooks hooks = {SENTINEL, primary_selected, primary_completed, primary_spawned};
      qa_compat_q2_native_primary_weapons *weapons = qa_app_sim_bind_native_primary_weapons(&context, native, c->actors, profile, hooks);
      qa_compat_q2_native_primary_weapon_input input = qa_compat_q2_native_primary_weapons_input(weapons, actor);
      CHECK(input.buttons == 0 && input.latched_buttons == 0 && input.view_angles.x == 0 && input.view_angles.y == 0 && input.view_angles.z == 0);
      CHECK(input.view_height == 22 && input.water_level == 0 && input.max_health == 100);
      qa_animation_state animation = qa_compat_q2_native_primary_weapons_animation(weapons, actor);
      CHECK(strcmp(animation.kind, "q2") == 0 && animation.frame == 65 && animation.end_frame == 65 && animation.priority == 3 && !animation.duck && !animation.run);
      CHECK(qa_compat_q2_native_primary_weapons_damage_factor(weapons, actor) == 1);
      qa_compat_q2_native_primary_weapons_destroy(weapons);
    }
  }
  { /* NativeQ2ClientPresentation (native-q2-client.ts) over this world: the admitted-player guard, the copied
     * configstrings, receive() over the world's recipient-filtered messages, and print()'s q2-player event. */
    qa_app_sim_q2_native_world native = {true, NULL, world};
    qa_actor_id stranger = actor;
    stranger.generation += 1;
    native_client_args args = {native, stranger};
    qa_caught_error caught;
    CHECK(!qa_try(native_client_create_body, &args, &caught));
    CHECK(strcmp(caught.name, "Error") == 0 && strcmp(caught.message, "Native client presentation requires its admitted source player") == 0);
    qa_app_native_q2_client *client = qa_app_native_q2_client_create(native, 1, actor, "q2:rerelease:baseq2:installed");
    size_t world_count = 0, client_count = 0;
    const qa_app_sim_configstring *world_rows = qa_app_sim_rerelease_guest_world_configstrings(world, &world_count);
    const qa_app_sim_configstring *client_rows = qa_app_native_q2_client_configstrings(client, &client_count);
    bool same = world_count == client_count;
    for (size_t i = 0; same && i < world_count; i++)
      same = world_rows[i].index == client_rows[i].index && strcmp(world_rows[i].value, client_rows[i].value) == 0;
    CHECK(same && client_count > 0);
    /* The world's messages of the last frame (read above for the dialect check). */
    size_t message_count = raw_count;
    const qa_app_sim_rerelease_guest_message *raw = messages;
    CHECK(message_count > 0);
    qa_app_native_q2_message *rows = calloc(message_count + 1, sizeof *rows);
    for (size_t i = 0; i < message_count; i++) { rows[i].bytes = raw[i].bytes; rows[i].length = raw[i].length; }
    qa_q2_server_record *remaining = NULL;
    size_t remaining_count = qa_app_native_q2_client_receive(client, rows, message_count, 0.2, &remaining);
    qa_q2_server_records_free(remaining, remaining_count);
    free(rows);
    qa_app_sim_presentation_event *events = NULL;
    size_t event_count = qa_app_native_q2_client_take_events(client, &events);
    for (size_t i = 0; i < event_count; i++) qa_app_sim_presentation_event_release(&events[i]);
    free(events);
    qa_app_native_q2_client_print(client, 2, "hello\n", 0.25);
    event_count = qa_app_native_q2_client_take_events(client, &events);
    CHECK(event_count == 1);
    if (event_count == 1) {
      /* q2ServicePrint stamps no recipient; the print targets the player. */
      CHECK(strcmp(events[0].kind, "q2-player") == 0 && !events[0].has_recipient);
      CHECK(events[0].q2_player->has_target && qa_same_actor(events[0].q2_player->target, actor));
      CHECK(strcmp(events[0].content, "q2:rerelease:baseq2:installed") == 0 && events[0].seconds == 0.25);
      CHECK(events[0].has_source_entity && events[0].source_entity == 1);
      CHECK(strcmp(events[0].q2_player->level, "high") == 0 && strcmp(events[0].q2_player->text, "hello\n") == 0);
    }
    for (size_t i = 0; i < event_count; i++) qa_app_sim_presentation_event_release(&events[i]);
    free(events);
    CHECK(qa_app_native_q2_client_take_events(client, &events) == 0);
    free(events);
    qa_app_native_q2_player_state state = qa_app_native_q2_client_player_state(client);
    CHECK(state.rerelease && state.rerelease_state.view.fov > 0);
    printf("native q2 client: %zu configstrings, %zu messages, %zu records left\n", client_count, message_count, remaining_count);
    qa_app_native_q2_client_destroy(client);
  }
  qa_app_sim_rerelease_guest_save save = qa_app_sim_rerelease_guest_world_write_save(world, false);
  CHECK(save.game.native_length > 100 && save.level.native_length > 100 && save.game.native[0] == '{');
  printf("rerelease API2023: %llu guest instructions\n", (unsigned long long)qa_app_sim_native_guest_instructions(qa_app_sim_rerelease_guest_source_guest(qa_app_sim_rerelease_guest_world_source(world))));
  free(save.game.native); free(save.level.native);
  qa_compat_q2_rerelease_deferred_damage_free_saves((qa_compat_q2_rerelease_deferred_damage_save *)save.game.deferred_damage, save.game.deferred_damage_count);
  qa_compat_q2_rerelease_deferred_damage_free_saves((qa_compat_q2_rerelease_deferred_damage_save *)save.level.deferred_damage, save.level.deferred_damage_count);
  qa_app_sim_rerelease_guest_world_disconnect(world, 1);
  qa_app_sim_rerelease_guest_source *source = qa_app_sim_rerelease_guest_world_source(world);
  qa_app_sim_rerelease_guest_world_close(world);
  CHECK(qa_app_sim_native_guest_mapping_count(qa_app_sim_rerelease_guest_source_guest(source)) == 0);
  qa_app_sim_rerelease_guest_world_destroy(world);
  qa_app_sim_prepared_rerelease_guest_free(&prepared); unmount_dll(&m); cand_free(c); free(c);
}

/* tests/app/q2-native-save.test.ts */
static qa_app_sim_q2_rerelease_native_save save_fixture(qa_compat_q2_rerelease_source_save source, qa_compat_q2_rerelease_projection_save *projection,
                                                        qa_app_sim_q2_rerelease_configstring *configstring, qa_app_sim_q2_rerelease_portal *portal,
                                                        qa_app_sim_q2_rerelease_visited_level *visited, uint8_t **cvars, size_t *cvar_length) {
  qa_app_sim_q2_rerelease_native_save save; memset(&save, 0, sizeof save);
  save.module.id = "q2:native-test"; save.module.artifact_path = "game_x64.dll";
  save.module.digest = "sha256:1212121212121212121212121212121212121212121212121212121212121212"; save.module.revision = "r1";
  save.map = "maps/base1.bsp";
  qa_save_value *dialect = qa_save_value_object(); qa_save_value_set(dialect, "dialect", qa_save_value_string("q2-rerelease"));
  *cvars = qa_encode_checkpoint_value(dialect, cvar_length); qa_save_value_free(dialect);
  save.server_cvars.bytes = *cvars; save.server_cvars.length = *cvar_length;
  configstring->index = 12447; configstring->value = "tail"; save.server.configstrings = configstring; save.server.configstring_count = 1;
  portal->portal = 7; portal->open = true; save.server.portals = portal; save.server.portal_count = 1;
  save.game = source; save.level = source;
  projection->slot = 9; projection->actor.slot = 30; projection->actor.generation = 8; save.level.projections = projection; save.level.projection_count = 1;
  memset(visited, 0, sizeof *visited); visited->version = 1; visited->map = "maps/base2.bsp"; visited->level = source;
  save.visited_levels = visited; save.visited_level_count = 1;
  return save;
}
static void test_native_rerelease_save(void) {
  static const char json[] = "{\"native_field\":{\"unknown_to_engine\":42}}";
  qa_compat_q2_rerelease_source_save source; memset(&source, 0, sizeof source); source.native = (uint8_t *)json; source.native_length = strlen(json);
  qa_compat_q2_rerelease_projection_save projection; qa_app_sim_q2_rerelease_configstring configstring; qa_app_sim_q2_rerelease_portal portal; qa_app_sim_q2_rerelease_visited_level visited;
  uint8_t *cvars = NULL; size_t cvar_length = 0;
  qa_app_sim_q2_rerelease_native_save save = save_fixture(source, &projection, &configstring, &portal, &visited, &cvars, &cvar_length);
  qa_provider_checkpoint checkpoint; qa_app_sim_encode_q2_rerelease_native_save(&save, &checkpoint);
  qa_app_sim_q2_rerelease_native_identity identity = {save.module, save.map};
  qa_app_sim_q2_rerelease_native_save restored; qa_app_sim_decode_q2_rerelease_native_save(&checkpoint, identity, &restored);
  CHECK(strcmp(restored.module.id, save.module.id) == 0 && strcmp(restored.map, save.map) == 0 && !restored.autosave);
  CHECK(restored.server.configstring_count == 1 && restored.server.configstrings[0].index == 12447 && strcmp(restored.server.configstrings[0].value, "tail") == 0);
  CHECK(restored.server.portal_count == 1 && restored.server.portals[0].portal == 7 && restored.server.portals[0].open);
  CHECK(restored.server_cvars.length == cvar_length && memcmp(restored.server_cvars.bytes, cvars, cvar_length) == 0);
  CHECK(restored.level.native_length == strlen(json) && memcmp(restored.level.native, json, strlen(json)) == 0);
  CHECK(restored.level.projection_count == 1 && restored.level.projections[0].slot == 9 && restored.level.projections[0].actor.slot == 30 && restored.level.projections[0].actor.generation == 8);
  CHECK(restored.visited_level_count == 1 && strcmp(restored.visited_levels[0].map, "maps/base2.bsp") == 0 && restored.visited_levels[0].level.native_length == strlen(json));
  qa_app_sim_q2_rerelease_native_identity other = identity; other.module.digest = "sha256:3434343434343434343434343434343434343434343434343434343434343434";
  qa_app_sim_q2_rerelease_native_save scratch;
  CHECK_ABORTS(qa_app_sim_decode_q2_rerelease_native_save(&checkpoint, other, &scratch));
  qa_provider_checkpoint wrong = checkpoint; wrong.schema = "q2:classic-native-original";
  CHECK_ABORTS(qa_app_sim_decode_q2_rerelease_native_save(&wrong, identity, &scratch));
  qa_app_sim_q2_rerelease_configstring duplicates[2] = {{1, "a"}, {1, "b"}};
  qa_app_sim_q2_rerelease_native_save duplicate = save; duplicate.server.configstrings = duplicates; duplicate.server.configstring_count = 2;
  qa_provider_checkpoint out; CHECK_ABORTS(qa_app_sim_encode_q2_rerelease_native_save(&duplicate, &out));
  qa_app_sim_q2_rerelease_native_save broken = save; broken.game.native = (uint8_t *)"broken"; broken.game.native_length = 6;
  CHECK_ABORTS(qa_app_sim_encode_q2_rerelease_native_save(&broken, &out));
  qa_app_sim_q2_rerelease_visited_level same_map = visited; same_map.map = save.map;
  qa_app_sim_q2_rerelease_native_save revisit = save; revisit.visited_levels = &same_map;
  CHECK_ABORTS(qa_app_sim_encode_q2_rerelease_native_save(&revisit, &out));
  qa_app_sim_q2_rerelease_native_save_free(&restored); free((void *)checkpoint.bytes.bytes); free(cvars);
}

static const char *only;
#define RUN(test) do { if (only == NULL || strstr(#test, only) != NULL) test(); } while (0)
int main(int argc, char **argv) {
  only = argc > 1 ? argv[1] : NULL;
  const char *configured = getenv("QA_CORPUS"), *home = getenv("HOME");
  snprintf(corpus_root, sizeof corpus_root, "%s", configured != NULL ? configured : home != NULL ? home : "");
  if (configured == NULL) strncat(corpus_root, "/qfiles", sizeof corpus_root - strlen(corpus_root) - 1);
  RUN(test_classic_files);
  RUN(test_classic_files_quota);
  RUN(test_classic_host_lifecycle);
  RUN(test_classic_services_real);
  RUN(test_classic_user_command);
  RUN(test_classic_world_travel);
  RUN(test_rerelease_world);
  RUN(test_native_rerelease_save);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
