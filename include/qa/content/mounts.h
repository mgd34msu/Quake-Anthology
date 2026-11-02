/*
 * Port of src/content/mounts/index.ts: SHA-256 content digests (with
 * the stat-fingerprint disk cache), MountedContent, openMountPlan,
 * canDownloadResource, and MountPreparationScope.
 * C accommodations:
 *  - The TS class is qa_content_mounted (qa_mounted_content is the
 *    text module's borrowed service shape). Instances are heap
 *    objects; close() mirrors TS close, destroy() also frees.
 *  - Async methods are synchronous. Returned bytes are malloc'd
 *    (NULL only on OOM; 0-length may be NULL). References returned
 *    by open/resolve are owned by the mounted instance (or reader)
 *    and stay valid until its close/destroy.
 *  - Plan structs are copied at open; mount identity/path strings
 *    stay borrowed from the caller's plan mounts, as the TS retains
 *    the mount objects. Resolved order arrays are owned copies.
 *  - The digest disk cache saves synchronously (best-effort) where
 *    the TS defers 250 ms on a timer; cache contents and hit rules
 *    are identical. QTS_NO_DIGEST_CACHE is read per call.
 *  - q3ArchiveChecksums (src/network/q3/pure.ts) is
 *    qa_q3_archive_checksums in qa/network/q3/pure.h.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MOUNTS_H
#define QA_CONTENT_MOUNTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/archive.h"
#include "qa/content/mounts/paths.h"
#include "qa/contracts/content.h"

/* Malloc'd `sha256:<hex>` digest; NULL only on OOM. */
char *qa_digest_bytes(const uint8_t *bytes, size_t length);
char *qa_digest_file(const char *path);

typedef struct qa_resource_link {
  const char *source_prefix;
  qa_mount_id mount;
  const char *target_prefix;
} qa_resource_link;

typedef struct qa_pure_mount_policy {
  const qa_content_digest *archives;
  size_t archive_count;
} qa_pure_mount_policy;

typedef struct qa_open_mount_options {
  bool has_pure;
  qa_pure_mount_policy pure;
  bool q3_restriction_demo; /* TS q3Restriction === "demo" */
  const qa_resource_link *links;
  size_t link_count;
  bool has_loose_comparison;
  qa_content_path_comparison loose_comparison;
} qa_open_mount_options;

typedef struct qa_content_opened_resource {
  const qa_resolved_resource_reference *reference; /* owned by the mounter */
  uint8_t *bytes; /* caller frees */
  size_t byte_length;
} qa_content_opened_resource;

/* open() acceptMount callback (TS default: accept every mount). */
typedef bool (*qa_mount_accept_fn)(void *ctx, const qa_content_mount *mount);

typedef struct qa_content_mounted qa_content_mounted;

/* TS openMountPlan (NULL only on OOM; TS throws abort). */
qa_content_mounted *qa_open_mount_plan(const qa_resolved_mount_plan *plan,
                                       const qa_open_mount_options *options);

const qa_resolved_mount_plan *qa_content_mounted_plan(
    const qa_content_mounted *mounted);
const qa_open_mount_options *qa_content_mounted_options(
    const qa_content_mounted *mounted);
/* Borrowed arrays owned by the instance. */
const qa_resolved_resource_reference *qa_content_mounted_opened_resources(
    const qa_content_mounted *mounted, size_t *out_count);
const qa_archive_mount *qa_content_mounted_referenced_archives(
    const qa_content_mounted *mounted, size_t *out_count);

void qa_content_mounted_assert_open(const qa_content_mounted *mounted);
/* NULL return is the TS null (or OOM). */
qa_content_mounted *qa_content_mounted_borrow_mount_plan(
    qa_content_mounted *mounted, const qa_resolved_mount_plan *plan,
    const qa_open_mount_options *options);
qa_content_mounted *qa_content_mounted_borrow_with_loose_mount(
    qa_content_mounted *mounted, qa_loose_mount mount, qa_mount_plan_id id);

/* False is the TS null. accept may be NULL (accept every mount). */
bool qa_content_mounted_open(qa_content_mounted *mounted, const char *path,
                             qa_mount_accept_fn accept, void *accept_ctx,
                             qa_content_opened_resource *out);
bool qa_content_mounted_resolve(qa_content_mounted *mounted, const char *path,
                                const qa_resolved_resource_reference **out);
bool qa_content_mounted_exists(qa_content_mounted *mounted, const char *path);
/* Malloc'd array of malloc'd names; free with qa_content_free_string_list
 * (roots.h). NULL only on OOM. */
char **qa_content_mounted_list_files(qa_content_mounted *mounted,
                                     const char *path, const char *extension,
                                     size_t *out_count);
uint8_t *qa_content_mounted_read_path(qa_content_mounted *mounted,
                                      const char *path, size_t *out_length);
uint8_t *qa_content_mounted_read_reference(
    qa_content_mounted *mounted, const qa_resolved_resource_reference *resource,
    size_t *out_length);
void qa_content_mounted_invalidate_listings(qa_content_mounted *mounted);
void qa_content_mounted_close(qa_content_mounted *mounted);
void qa_content_mounted_destroy(qa_content_mounted *mounted);

/* TS MountedContentReader: a borrowed ordered view of an instance.
 * Its plan copy and opened references are owned by the reader. */
typedef struct qa_mounted_content_reader qa_mounted_content_reader;
qa_mounted_content_reader *qa_content_mounted_borrow_ordered_reader(
    qa_content_mounted *mounted, qa_mount_plan_id id,
    const qa_mount_id *default_order, size_t default_order_count,
    const qa_prefix_mount_order *prefix_orders, size_t prefix_order_count);
const qa_resolved_mount_plan *qa_mounted_content_reader_plan(
    const qa_mounted_content_reader *reader);
bool qa_mounted_content_reader_open(qa_mounted_content_reader *reader,
                                    const char *path,
                                    qa_mount_accept_fn accept, void *accept_ctx,
                                    qa_content_opened_resource *out);
bool qa_mounted_content_reader_resolve(
    qa_mounted_content_reader *reader, const char *path,
    const qa_resolved_resource_reference **out);
uint8_t *qa_mounted_content_reader_read_path(qa_mounted_content_reader *reader,
                                             const char *path, size_t *out_length);
uint8_t *qa_mounted_content_reader_read_reference(
    qa_mounted_content_reader *reader,
    const qa_resolved_resource_reference *resource, size_t *out_length);
void qa_mounted_content_reader_destroy(qa_mounted_content_reader *reader);

bool qa_can_download_resource(const qa_resolved_resource_reference *resource);

/* TS MountPlanOpener closure shape. */
typedef struct qa_mount_plan_opener {
  void *ctx;
  qa_content_mounted *(*open)(void *ctx, const qa_resolved_mount_plan *plan,
                              const qa_open_mount_options *options);
} qa_mount_plan_opener;

typedef struct qa_mount_preparation_scope qa_mount_preparation_scope;
qa_mount_preparation_scope *qa_mount_preparation_scope_create(void);
void qa_mount_preparation_scope_assert_open(
    const qa_mount_preparation_scope *scope);
qa_content_mounted *qa_mount_preparation_scope_open(
    qa_mount_preparation_scope *scope, const qa_resolved_mount_plan *plan,
    const qa_open_mount_options *options);
qa_mount_plan_opener qa_mount_preparation_scope_opener(
    qa_mount_preparation_scope *scope);
/* TS asyncDispose, synchronous in C; also frees on destroy. */
void qa_mount_preparation_scope_dispose(qa_mount_preparation_scope *scope);
void qa_mount_preparation_scope_destroy(qa_mount_preparation_scope *scope);

#endif
