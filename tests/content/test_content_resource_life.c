/*
 * Ownership of ResolvedResourceReference values (content-life batch). TS
 * references are plain values: one kept by a recipe, a loaded content or a
 * catalog result stays valid after the mount set or reader that resolved it
 * is closed. These checks hold the C port to that: an owned copy
 * (qa_resolved_resource_copy) survives its mount set and the plan strings it
 * was built from, reads back through a fresh mount set of the same identity
 * (src/content/mounts/index.ts #read identity checks), and a mount set keeps
 * its own copy of a reference handed to read(). Run under ASan with leak
 * detection: every case must finish clean.
 * GPL-2.0-or-later.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/content/mounts.h"
#include "qa/contracts/content.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                    \
  do {                                                                                 \
    checks++;                                                                          \
    if (!(cond)) {                                                                     \
      failures++;                                                                      \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
    }                                                                                  \
  } while (0)

enum { MEMBER_COUNT = 24 };

static char *heap_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

static void put_u32(uint8_t *at, uint32_t value) {
  at[0] = (uint8_t)value; at[1] = (uint8_t)(value >> 8);
  at[2] = (uint8_t)(value >> 16); at[3] = (uint8_t)(value >> 24);
}

/* A PAK with MEMBER_COUNT members "files/mNN.txt" holding "member NN". */
static bool write_pak(const char *path) {
  uint8_t data[MEMBER_COUNT * 16];
  uint8_t directory[MEMBER_COUNT * 64];
  memset(directory, 0, sizeof directory);
  size_t data_length = 0;
  for (int i = 0; i < MEMBER_COUNT; i++) {
    char body[16];
    int length = snprintf(body, sizeof body, "member %02d", i);
    uint8_t *entry = directory + i * 64;
    (void)snprintf((char *)entry, 56, "files/m%02d.txt", i);
    put_u32(entry + 56, (uint32_t)(12 + data_length));
    put_u32(entry + 60, (uint32_t)length);
    memcpy(data + data_length, body, (size_t)length);
    data_length += (size_t)length;
  }
  uint8_t header[12];
  memcpy(header, "PACK", 4);
  put_u32(header + 4, (uint32_t)(12 + data_length));
  put_u32(header + 8, (uint32_t)sizeof directory);
  FILE *out = fopen(path, "wb");
  if (out == NULL) return false;
  bool ok = fwrite(header, 1, sizeof header, out) == sizeof header &&
            fwrite(data, 1, data_length, out) == data_length &&
            fwrite(directory, 1, sizeof directory, out) == sizeof directory;
  return fclose(out) == 0 && ok;
}

/* Plan strings live on the heap so a test can free them while a copied
 * reference is still in use (TS values do not borrow their mount object). */
typedef struct fixture_plan {
  qa_content_mount mount;
  char *mount_id, *content, *path, *digest, *plan_id;
  qa_mount_id order[1];
  qa_resolved_mount_plan plan;
} fixture_plan;

static bool plan_init(fixture_plan *f, const char *pak, const char *loose_root) {
  memset(f, 0, sizeof *f);
  f->mount_id = heap_text(loose_root != NULL ? "mount:life:loose" : "mount:life:pak0");
  f->content = heap_text("q1:classic:id1:installed");
  f->plan_id = heap_text("mount-plan:life:1");
  if (loose_root != NULL) {
    f->path = heap_text(loose_root);
    f->mount.kind = QA_CONTENT_MOUNT_LOOSE;
    f->mount.as.loose.identity = qa_create_mount_identity(f->mount_id, f->content, 3);
    f->mount.as.loose.root_path = f->path;
  } else {
    f->path = heap_text(pak);
    f->digest = qa_digest_file(pak);
    f->mount.kind = QA_CONTENT_MOUNT_ARCHIVE;
    f->mount.as.archive.identity = qa_create_mount_identity(f->mount_id, f->content, 3);
    f->mount.as.archive.format = QA_ARCHIVE_FORMAT_PAK;
    f->mount.as.archive.archive_path = f->path;
    f->mount.as.archive.archive_digest = f->digest;
  }
  f->order[0] = f->mount_id;
  f->plan.id = f->plan_id;
  f->plan.mounts = &f->mount;
  f->plan.mount_count = 1;
  f->plan.default_order = f->order;
  f->plan.default_order_count = 1;
  return f->mount_id != NULL && f->content != NULL && f->plan_id != NULL && f->path != NULL &&
         (loose_root != NULL || f->digest != NULL);
}

/* Overwrites the strings before freeing them, so a reference still pointing
 * at them reads garbage even without ASan. */
static void scrub_free(char *text) {
  if (text == NULL) return;
  memset(text, 'X', strlen(text));
  free(text);
}
static void plan_free(fixture_plan *f) {
  scrub_free(f->mount_id); scrub_free(f->content); scrub_free(f->path);
  scrub_free(f->digest); scrub_free(f->plan_id);
  memset(f, 0, sizeof *f);
}

/* The bug: a reference copied out of a mount set that is then destroyed must
 * still pass the archive identity checks of a later mount set's read. */
static void test_copy_outlives_mount_set(const char *pak, const char *loose_root) {
  fixture_plan first;
  CHECK(plan_init(&first, pak, loose_root));
  qa_content_mounted *mounted = qa_open_mount_plan(&first.plan, NULL);
  CHECK(mounted != NULL);
  if (mounted == NULL) { plan_free(&first); return; }
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  CHECK(qa_content_mounted_open(mounted, "files/m07.txt", NULL, NULL, &opened));
  CHECK(opened.byte_length == 9 && memcmp(opened.bytes, "member 07", 9) == 0);
  free(opened.bytes);
  qa_resolved_resource_reference kept;
  CHECK(qa_resolved_resource_copy(opened.reference, &kept));
  /* Every string and the provenance mount are the copy's own. */
  CHECK(kept.id != opened.reference->id && strcmp(kept.id, opened.reference->id) == 0);
  CHECK(kept.requested_path != opened.reference->requested_path);
  CHECK(kept.digest != opened.reference->digest);
  CHECK(kept.provenance.member_path != opened.reference->provenance.member_path);
  CHECK(kept.resolution.plan != opened.reference->resolution.plan);
  if (loose_root == NULL) {
    CHECK(kept.provenance.archive_mount != opened.reference->provenance.archive_mount);
    CHECK(kept.provenance.archive_mount->identity.id != first.mount_id);
    CHECK(kept.provenance.archive_mount->archive_path != first.path);
    CHECK(kept.provenance.member_index == 7);
  } else {
    CHECK(kept.provenance.loose_mount != opened.reference->provenance.loose_mount);
    CHECK(kept.provenance.loose_mount->root_path != first.path);
  }
  qa_content_mounted_destroy(mounted);
  plan_free(&first);

  fixture_plan second;
  CHECK(plan_init(&second, pak, loose_root));
  mounted = qa_open_mount_plan(&second.plan, NULL);
  CHECK(mounted != NULL);
  if (mounted == NULL) { qa_resolved_resource_release(&kept); plan_free(&second); return; }
  size_t length = 0;
  uint8_t *bytes = qa_content_mounted_read_reference(mounted, &kept, &length);
  CHECK(length == 9 && bytes != NULL && memcmp(bytes, "member 07", 9) == 0);
  free(bytes);
  CHECK(strcmp(kept.requested_path, "files/m07.txt") == 0);
  CHECK(strcmp(kept.resolution.plan, "mount-plan:life:1") == 0);
  /* TS read() stores the caller's value; the C set keeps a copy, so
   * releasing the caller's copy leaves the set intact. */
  char *id = heap_text(kept.id);
  qa_resolved_resource_release(&kept);
  CHECK(kept.id == NULL && kept.provenance.archive_mount == NULL && kept.provenance.loose_mount == NULL);
  size_t count = 0;
  const qa_resolved_resource_reference *owned = qa_content_mounted_opened_resources(mounted, &count);
  CHECK(count == 1);
  if (count == 1) {
    CHECK(strcmp(owned[0].id, id) == 0);
    CHECK(strcmp(owned[0].requested_path, "files/m07.txt") == 0);
    if (loose_root == NULL)
      CHECK(strcmp(owned[0].provenance.archive_mount->identity.id, "mount:life:pak0") == 0);
    bytes = qa_content_mounted_read_reference(mounted, &owned[0], &length);
    CHECK(length == 9 && bytes != NULL && memcmp(bytes, "member 07", 9) == 0);
    free(bytes);
  }
  free(id);
  qa_content_mounted_destroy(mounted);
  plan_free(&second);
}

/* A reference open() hands out stays put while later opens grow the set
 * (the records were once a realloc'd array, so earlier pointers moved). */
static void test_open_pointer_stable(const char *pak) {
  fixture_plan f;
  CHECK(plan_init(&f, pak, NULL));
  qa_content_mounted *mounted = qa_open_mount_plan(&f.plan, NULL);
  CHECK(mounted != NULL);
  if (mounted == NULL) { plan_free(&f); return; }
  qa_mounted_content_reader *reader = qa_content_mounted_borrow_ordered_reader(mounted, "mount-plan:life:reader", f.order, 1, NULL, 0);
  CHECK(reader != NULL);
  const qa_resolved_resource_reference *first[2] = {NULL, NULL};
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < MEMBER_COUNT; i++) {
      char path[32];
      (void)snprintf(path, sizeof path, "files/m%02d.txt", i);
      qa_content_opened_resource opened;
      memset(&opened, 0, sizeof opened);
      bool found = pass == 0 ? qa_content_mounted_open(mounted, path, NULL, NULL, &opened)
                             : qa_mounted_content_reader_open(reader, path, NULL, NULL, &opened);
      CHECK(found);
      free(opened.bytes);
      if (i == 0) first[pass] = opened.reference;
    }
    CHECK(first[pass] != NULL && strcmp(first[pass]->requested_path, "files/m00.txt") == 0);
    CHECK(first[pass] != NULL && first[pass]->provenance.member_index == 0);
  }
  CHECK(strcmp(first[1]->resolution.plan, "mount-plan:life:reader") == 0);
  /* Reopening an unchanged resource keeps the stored value (and its
   * strings) that earlier callers hold. */
  const char *digest = first[0]->digest;
  qa_content_opened_resource again;
  memset(&again, 0, sizeof again);
  CHECK(qa_content_mounted_open(mounted, "files/m00.txt", NULL, NULL, &again));
  free(again.bytes);
  CHECK(again.reference == first[0] && first[0]->digest == digest);
  qa_mounted_content_reader_destroy(reader);
  qa_content_mounted_destroy(mounted);
  plan_free(&f);
}

/* Copy/release symmetry with a hand-built value holding every optional
 * field (prefix-order and link resolutions, loose provenance). */
static void test_copy_release_fields(void) {
  qa_loose_mount loose;
  memset(&loose, 0, sizeof loose);
  loose.identity = qa_create_mount_identity("mount:life:loose", "q2:classic:baseq2:installed", 9);
  loose.root_path = "/content/root";
  qa_resolved_resource_reference value;
  memset(&value, 0, sizeof value);
  value.id = "resource:example";
  value.requested_path = "maps/a.bsp";
  value.provenance.kind = QA_CONTENT_MOUNT_LOOSE;
  value.provenance.loose_mount = &loose;
  value.provenance.member_path = "maps/A.bsp";
  value.digest = "sha256:0000000000000000000000000000000000000000000000000000000000000000";
  value.byte_length = 12;
  value.resolution.kind = QA_RESOURCE_RESOLUTION_LINK;
  value.resolution.plan = "mount-plan:life:2";
  value.resolution.source_prefix = "maps/";
  value.resolution.target_path = "maps/A.bsp";
  qa_resolved_resource_reference copy;
  CHECK(qa_resolved_resource_copy(&value, &copy));
  CHECK(copy.provenance.kind == QA_CONTENT_MOUNT_LOOSE && copy.provenance.archive_mount == NULL);
  CHECK(copy.provenance.loose_mount != &loose);
  CHECK(strcmp(copy.provenance.loose_mount->identity.id, "mount:life:loose") == 0);
  CHECK(strcmp(copy.provenance.loose_mount->identity.content, "q2:classic:baseq2:installed") == 0);
  CHECK(copy.provenance.loose_mount->identity.generation == 9);
  CHECK(strcmp(copy.provenance.loose_mount->root_path, "/content/root") == 0);
  CHECK(copy.resolution.kind == QA_RESOURCE_RESOLUTION_LINK && copy.resolution.prefix == NULL);
  CHECK(strcmp(copy.resolution.source_prefix, "maps/") == 0 && copy.resolution.source_prefix != value.resolution.source_prefix);
  CHECK(strcmp(copy.resolution.target_path, "maps/A.bsp") == 0);
  CHECK(copy.byte_length == 12);
  /* A copy of a copy, then copying onto itself, are both independent. */
  qa_resolved_resource_reference second;
  CHECK(qa_resolved_resource_copy(&copy, &second));
  qa_resolved_resource_release(&copy);
  CHECK(strcmp(second.requested_path, "maps/a.bsp") == 0);
  qa_resolved_resource_reference same = second;
  CHECK(qa_resolved_resource_copy(&second, &second));
  CHECK(second.id != same.id && strcmp(second.id, same.id) == 0);
  qa_resolved_resource_release(&same);
  value.resolution.kind = QA_RESOURCE_RESOLUTION_PREFIX_ORDER;
  value.resolution.prefix = "maps/";
  value.resolution.source_prefix = NULL;
  value.resolution.target_path = NULL;
  value.resolution.rank = 4;
  qa_resolved_resource_release(&second);
  CHECK(qa_resolved_resource_copy(&value, &second));
  CHECK(second.resolution.rank == 4 && strcmp(second.resolution.prefix, "maps/") == 0);
  CHECK(second.resolution.source_prefix == NULL && second.resolution.target_path == NULL);
  qa_resolved_resource_release(&second);
  qa_resolved_resource_release(&second); /* zeroed: a second release is a no-op */
}

int main(void) {
  const char *base = getenv("TMPDIR");
  char root[512];
  (void)snprintf(root, sizeof root, "%s/qa-resource-lifeXXXXXX", base != NULL && base[0] != '\0' ? base : "/tmp");
  char *made = mkdtemp(root);
  CHECK(made != NULL);
  if (made == NULL) return 1;
  char pak[600], loose[600], files[640], member[700];
  (void)snprintf(pak, sizeof pak, "%s/pak0.pak", root);
  (void)snprintf(loose, sizeof loose, "%s/loose", root);
  (void)snprintf(files, sizeof files, "%s/files", loose);
  CHECK(write_pak(pak));
  CHECK(mkdir(loose, 0700) == 0 && mkdir(files, 0700) == 0);
  (void)snprintf(member, sizeof member, "%s/m07.txt", files);
  FILE *out = fopen(member, "wb");
  CHECK(out != NULL);
  if (out != NULL) { (void)fwrite("member 07", 1, 9, out); (void)fclose(out); }

  test_copy_outlives_mount_set(pak, NULL);
  test_copy_outlives_mount_set(NULL, loose);
  test_open_pointer_stable(pak);
  test_copy_release_fields();

  (void)unlink(member);
  (void)rmdir(files);
  (void)rmdir(loose);
  (void)unlink(pak);
  (void)rmdir(root);
  printf("%d checks, %d failures\n", checks, failures);
  return failures != 0;
}
