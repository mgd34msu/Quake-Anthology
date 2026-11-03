/*
 * C checks for shared src/content rows: mods/catalog.ts settles a component
 * whose declaration throws as unavailable (the TS try/catch) instead of
 * aborting discovery; mods/callbacks.ts readProtectionAdmission and the
 * protection field read order; roots.ts steamRootDirectories and the
 * libraryfolders.vdf "path" regex (whitespace required, value nonempty);
 * mounts/index.ts scheduleDiskDigestSave pruning and MountPreparationScope
 * openVerified failures cached per key. Expected messages from the TypeScript run
 * under bun on the same inputs. GPL-2.0-or-later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/content/mods/catalog.h"
#include "qa/content/mods/declaration.h"
#include "qa/content/roots.h"
#include "qa/content/mounts.h"
#include "qa/persistence/value.h"
#include "qa/foundation/support.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define DIGEST "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define HEAD "{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"progs.dat\",\"digest\":\"" DIGEST "\"},\"actorFields\":[],\"callbacks\":[]"
#define ABSORB "\"absorb\":{\"kind\":\"function\",\"call\":{\"function\":\"f\",\"arguments\":[],\"globals\":[]}}"
#define FLAGS "\"flags\":{\"noArmor\":1,\"noPowerArmor\":2,\"noRegularArmor\":4,\"energy\":8,\"radius\":16}"
#define PROTECTION(admission) HEAD ",\"protection\":[{\"id\":\"q1:armor\",\"channel\":\"regular\",\"admission\":" admission "," ABSORB "," FLAGS ",\"storage\":{\"points\":\"armorvalue\",\"item\":null}}]}"

static int sentinel;
static const char *const FILES[][2] = {
  {"gameplay-mods.json", "{\"version\":1,\"components\":["
    "{\"id\":\"broken\",\"title\":\"b\",\"purpose\":\"addition\",\"callbacks\":\"broken.json\",\"requires\":[],\"conflicts\":[]},"
    "{\"id\":\"badprog\",\"title\":\"p\",\"purpose\":\"addition\",\"callbacks\":\"badprog.json\",\"requires\":[],\"conflicts\":[]},"
    "{\"id\":\"badpath\",\"title\":\"x\",\"purpose\":\"addition\",\"callbacks\":\"a/../b.json\",\"requires\":[],\"conflicts\":[]},"
    "{\"id\":\"badadmit\",\"title\":\"y\",\"purpose\":\"addition\",\"callbacks\":\"badadmit.json\",\"requires\":[],\"conflicts\":[]},"
    "{\"id\":\"healthy\",\"title\":\"h\",\"purpose\":\"addition\",\"callbacks\":\"healthy.json\",\"requires\":[],\"conflicts\":[]}]}"},
  {"broken.json", "{"},
  {"badprog.json", "{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"C:/progs.dat\",\"digest\":\"" DIGEST "\"},\"actorFields\":[],\"callbacks\":[]}"},
  {"badadmit.json", PROTECTION("{\"kind\":\"steal\"}")},
  {"healthy.json", HEAD "}"},
  {"progs.dat", "x"},
};

static bool fake_open(void *ctx, const char *path, const char *filter, qa_mod_catalog_file *out) {
  (void)filter;
  CHECK(ctx == &sentinel);
  memset(out, 0, sizeof *out);
  for (size_t i = 0; i < sizeof FILES / sizeof FILES[0]; i++) {
    if (strcmp(FILES[i][0], path) != 0) continue;
    out->bytes = (const uint8_t *)FILES[i][1];
    out->length = strlen(FILES[i][1]);
    out->digest = strcmp(path, "progs.dat") == 0 ? DIGEST : "sha256:c";
    return true;
  }
  return false;
}

static void test_catalog_recovers_component_errors(void) {
  qa_mod_mounted_content mounted = {&sentinel, fake_open};
  qa_mod_catalog_product product;
  memset(&product, 0, sizeof product);
  product.id = "q1:classic:id1:installed";
  product.expectation.id = "q1-classic-id1";
  product.expectation.title = "Quake";
  product.expectation.family = "q1";
  product.expectation.edition = "classic";
  size_t count = 0;
  qa_discovered_gameplay_mod *found = qa_discover_gameplay_mods(&product, &mounted, &count);
  CHECK(count == 5);
  if (count != 5) return;
  /* bun: "JSON Parse error: Expected '}'" (engine text; C reports its own parser message). */
  CHECK(!found[0].is_available && strcmp(found[0].description.selection.id, "broken") == 0);
  CHECK(found[0].description.unavailable_reason != NULL && found[0].description.unavailable_reason[0] != '\0');
  CHECK(!found[1].is_available);
  CHECK(strcmp(found[1].description.unavailable_reason, "Invalid relative resource path: C:/progs.dat") == 0);
  CHECK(!found[2].is_available);
  CHECK(strcmp(found[2].description.unavailable_reason, "Invalid relative resource path: a/../b.json") == 0);
  CHECK(!found[3].is_available);
  CHECK(strcmp(found[3].description.unavailable_reason,
      "save.protection[0].admission.kind: expected claim or replace-primary or replace-current-primary") == 0);
  CHECK(found[4].is_available && strcmp(found[4].mod.selection.id, "healthy") == 0);
}

static const char *admission_kind(const char *json, const char **owner) {
  qa_mod_declaration d = qa_parse_gameplay_mod_declaration((const uint8_t *)json, strlen(json));
  CHECK(d.runtime == QA_MOD_RUNTIME_QUAKEC && d.as.quakec != NULL && d.as.quakec->protection_count == 1);
  *owner = d.as.quakec->protection[0].admission.owner;
  return d.as.quakec->protection[0].admission.kind;
}

/* bun: {"kind":"claim"} (owner ignored), {"kind":"replace-primary","owner":"q1:armor_red"},
 * {"kind":"replace-current-primary"}. */
static void test_protection_admission(void) {
  const char *owner = NULL;
  CHECK(strcmp(admission_kind(PROTECTION("{\"kind\":\"claim\",\"owner\":\"q1:x\"}"), &owner), "claim") == 0);
  CHECK(owner == NULL);
  CHECK(strcmp(admission_kind(PROTECTION("{\"kind\":\"replace-primary\",\"owner\":\"q1:armor_red\"}"), &owner), "replace-primary") == 0);
  CHECK(owner != NULL && strcmp(owner, "q1:armor_red") == 0);
  CHECK(strcmp(admission_kind(PROTECTION("{\"kind\":\"replace-current-primary\"}"), &owner), "replace-current-primary") == 0);
  CHECK(owner == NULL);
}

static const char *parse_text;
static void parse_body(void *ctx) {
  (void)ctx;
  (void)qa_parse_gameplay_mod_declaration((const uint8_t *)parse_text, strlen(parse_text));
}
static void expect_failure(const char *json, const char *message) {
  parse_text = json;
  qa_caught_error caught;
  bool ok = qa_try(parse_body, NULL, &caught);
  CHECK(!ok);
  if (!ok && strcmp(caught.message, message) != 0) {
    fprintf(stderr, "  got: %s\n", caught.message);
    CHECK(strcmp(caught.message, message) == 0);
  }
}

/* bun: the first failing field follows the TS read order. */
static void test_protection_read_order(void) {
  expect_failure(HEAD ",\"protection\":[{\"id\":5,\"absorb\":{\"kind\":\"nope\"}}]}",
      "save.protection[0].storage: expected a record");
  expect_failure(HEAD ",\"protection\":[{\"id\":\"q1:armor\"," ABSORB "," FLAGS ",\"channel\":\"regular\",\"storage\":{\"selection\":{\"field\":3}}}]}",
      "save.protection[0].storage.points: expected a string");
  expect_failure(HEAD ",\"protection\":[{\"id\":\"q1:armor\"," ABSORB "," FLAGS ",\"channel\":\"powered\",\"storage\":{\"cells\":\"c\",\"kind\":\"bad\",\"selection\":{\"field\":3}}}]}",
      "save.protection[0].storage.kind: expected screen or shield");
}

static void make_dirs(const char *root, const char *relative) {
  char path[1024]; snprintf(path, sizeof path, "%s/%s", root, relative);
  for (char *p = path + strlen(root) + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0'; (void)mkdir(path, 0700); *p = '/';
  }
  (void)mkdir(path, 0700);
}

/* bun steamCommonDirectories(home) over the same tree:
 * [.local/share/Steam, .steam/steam, lib/one, lib/three]/steamapps/common. */
static void test_steam_roots(void) {
  const char *tmp = getenv("TMPDIR");
  char home[512]; snprintf(home, sizeof home, "%s/qa_steamXXXXXX", tmp != NULL ? tmp : "/tmp");
  if (mkdtemp(home) == NULL) { CHECK(!"mkdtemp"); return; }
  make_dirs(home, ".steam/steam/steamapps/common");
  make_dirs(home, ".local/share/Steam/steamapps/common");
  make_dirs(home, "lib/one/steamapps/common");
  make_dirs(home, "lib/two/steamapps/common");
  make_dirs(home, "lib/three/steamapps/common");
  char vdf[1024]; snprintf(vdf, sizeof vdf, "%s/.steam/steam/steamapps/libraryfolders.vdf", home);
  FILE *file = fopen(vdf, "wb");
  if (file == NULL) { CHECK(!"fopen"); return; }
  fprintf(file, "\"libraryfolders\"\n{\n \"0\"\n {\n\t\"path\"\t\t\"%s/lib/one\"\n\t\"path\"\"%s/lib/two\"\n"
      "\t\"path\" \"\"\n\t\"path\"\v\"%s/lib/three\"\n }\n}\n", home, home, home);
  fclose(file);
  char **dirs = NULL; size_t count = 0;
  CHECK(qa_steam_common_directories(home, &dirs, &count));
  static const char *const expected[] = {"/.local/share/Steam/steamapps/common", "/.steam/steam/steamapps/common",
      "/lib/one/steamapps/common", "/lib/three/steamapps/common"};
  CHECK(count == 4);
  for (size_t i = 0; i < count && i < 4; i++) {
    char want[1024]; snprintf(want, sizeof want, "%s%s", home, expected[i]);
    CHECK(strcmp(dirs[i], want) == 0);
  }
  qa_content_free_string_list(dirs, count);
  unlink(vdf);
}

static bool write_bytes(const char *path, const void *data, size_t length) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) return false;
  bool ok = fwrite(data, 1, length, file) == length;
  return fclose(file) == 0 && ok;
}

/* bun digestFile(~/new.pak) with a cache holding a vanished path, an existing
 * unverified path and a malformed entry: the saved cache keeps
 * ["~/keep.pak", "~/new.pak"], keep.pak's fingerprint unchanged, and no
 * temporary file remains. */
static void test_digest_cache_prune(const char *home) {
  char dir[1024], file[1100], keep[1024], fresh[1024], json[4096];
  snprintf(dir, sizeof dir, "%s/.local/share/quake-typescript/cache", home);
  make_dirs(home, ".local/share/quake-typescript/cache");
  snprintf(keep, sizeof keep, "%s/keep.pak", home);
  snprintf(fresh, sizeof fresh, "%s/new.pak", home);
  CHECK(write_bytes(keep, "keep", 4) && write_bytes(fresh, "new", 3));
  snprintf(json, sizeof json, "{\"/nonexistent/qa/x.pak\":{\"size\":1,\"mtimeMs\":2,\"ctimeMs\":3,\"ino\":4,\"digest\":\"aa\"},"
      "\"%s\":{\"size\":1,\"mtimeMs\":2,\"ctimeMs\":3,\"ino\":4,\"digest\":\"bb\"},\"bad\":{\"size\":\"1\"}}", keep);
  snprintf(file, sizeof file, "%s/archive-digests.json", dir);
  CHECK(write_bytes(file, json, strlen(json)));
  char *digest = qa_digest_file(fresh);
  CHECK(digest != NULL && strcmp(digest, "sha256:11507a0e2f5e69d5dfa40a62a1bd7b6ee57e6bcd85c67c9b8431b36fff21c437") == 0);
  free(digest);
  FILE *handle = fopen(file, "rb");
  CHECK(handle != NULL);
  if (handle == NULL) return;
  size_t length = fread(json, 1, sizeof json - 1, handle);
  fclose(handle);
  json[length] = '\0';
  qa_save_value *parsed = qa_parse_json_value((const uint8_t *)json, length, true);
  CHECK(parsed != NULL && parsed->kind == QA_SAVE_OBJECT && parsed->entry_count == 2);
  if (parsed != NULL && parsed->kind == QA_SAVE_OBJECT && parsed->entry_count == 2) {
    CHECK(strcmp(parsed->entries[0].key, keep) == 0);
    CHECK(strcmp(parsed->entries[1].key, fresh) == 0);
    const qa_save_value *kept = parsed->entries[0].value, *kept_digest = qa_save_value_get(kept, "digest");
    CHECK(kept_digest != NULL && kept_digest->kind == QA_SAVE_STRING && strcmp(kept_digest->text, "bb") == 0);
  }
  qa_save_value_free(parsed);
  size_t entries = 0;
  DIR *listing = opendir(dir);
  if (listing != NULL) {
    for (struct dirent *entry; (entry = readdir(listing)) != NULL;)
      if (entry->d_name[0] != '.') entries++;
    closedir(listing);
  }
  /* Only the cache file remains: the temporary was renamed or removed. */
  CHECK(entries == 1);
}

static qa_mount_preparation_scope *scope_under_test;
static qa_resolved_mount_plan *plan_under_test;
static void scope_open_body(void *ctx) {
  (void)ctx;
  qa_content_mounted *mounted = qa_mount_preparation_scope_open(scope_under_test, plan_under_test, NULL);
  qa_content_mounted_destroy(mounted);
}

/* TS archive(): a failed openVerified stays cached as a rejected promise, so
 * the same key rethrows the first error even after the file appears. */
static void test_scope_caches_failure(const char *home) {
  char path[1024]; snprintf(path, sizeof path, "%s/late.pak", home);
  uint8_t pak[12 + 4 + 64]; memset(pak, 0, sizeof pak);
  memcpy(pak, "PACK", 4); pak[4] = 16; pak[8] = 64;
  memcpy(pak + 12, "late", 4); memcpy(pak + 16, "maps/late.bsp", 13); pak[16 + 56] = 12; pak[16 + 60] = 4;
  char *digest = qa_digest_bytes(pak, sizeof pak);
  qa_content_mount mount; memset(&mount, 0, sizeof mount);
  mount.kind = QA_CONTENT_MOUNT_ARCHIVE;
  mount.as.archive.identity = qa_create_mount_identity("mount:late", "q1:classic:id1:installed", 0);
  mount.as.archive.format = QA_ARCHIVE_FORMAT_PAK;
  mount.as.archive.archive_path = path;
  mount.as.archive.archive_digest = digest;
  qa_mount_id order[1] = {"mount:late"};
  qa_resolved_mount_plan plan; memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:late"; plan.mounts = &mount; plan.mount_count = 1;
  plan.default_order = order; plan.default_order_count = 1;
  scope_under_test = qa_mount_preparation_scope_create();
  plan_under_test = &plan;
  qa_caught_error first, second;
  CHECK(!qa_try(scope_open_body, NULL, &first));
  CHECK(write_bytes(path, pak, sizeof pak));
  CHECK(!qa_try(scope_open_body, NULL, &second));
  CHECK(strcmp(first.name, second.name) == 0 && strcmp(first.message, second.message) == 0);
  CHECK(strstr(first.message, path) != NULL);
  qa_mount_preparation_scope_destroy(scope_under_test);
  /* A fresh scope verifies again and opens the now-present archive. */
  scope_under_test = qa_mount_preparation_scope_create();
  qa_content_mounted *mounted = qa_mount_preparation_scope_open(scope_under_test, &plan, NULL);
  CHECK(mounted != NULL);
  CHECK(qa_content_mounted_exists(mounted, "maps/late.bsp"));
  qa_content_mounted_destroy(mounted);
  qa_mount_preparation_scope_destroy(scope_under_test);
  free(digest);
}

int main(void) {
  test_catalog_recovers_component_errors();
  test_protection_admission();
  test_protection_read_order();
  test_steam_roots();
  {
    const char *tmp = getenv("TMPDIR");
    char home[512]; snprintf(home, sizeof home, "%s/qa_digestXXXXXX", tmp != NULL ? tmp : "/tmp");
    if (mkdtemp(home) != NULL) {
      setenv("HOME", home, 1);
      unsetenv("QTS_NO_DIGEST_CACHE");
      test_digest_cache_prune(home);
      test_scope_caches_failure(home);
    } else CHECK(!"mkdtemp");
  }
  printf("test_content_rows_2: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
