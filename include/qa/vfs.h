#ifndef QA_VFS_H
#define QA_VFS_H

#include "qa/archive.h"
#include "qa/filesystem.h"
#include "qa/hash.h"

typedef struct qa_resource_pool qa_resource_pool;
typedef struct qa_resource qa_resource;
typedef struct qa_vfs qa_vfs;
typedef struct qa_vfs_file qa_vfs_file;
typedef uint64_t qa_mount_id;

/* Shared resource admission without opening a file. The owned result uses
 * forward slashes and rejects absolute, empty, dot and parent components. */
char *qa_vfs_normalize_path(const char *path, qa_error *error);

/* A pool shares immutable file versions and decoded members across VFS views.
 * Mutable mount orders remain private to each view. Calls require one owner
 * thread or external synchronization. Destroy releases the caller's reference;
 * existing views and acquired resources retain their own lifetimes. */
qa_resource_pool *qa_resource_pool_create(qa_error *error);
void qa_resource_pool_retain(qa_resource_pool *pool);
void qa_resource_pool_destroy(qa_resource_pool *pool);
/* Drops cached resources/packages with no live readers or mounts. */
void qa_resource_pool_trim(qa_resource_pool *pool);
/* Observe an existing version by its pool-local ID without loading content.
 * Borrowed until pool trimming/destruction; retain it across those operations. */
const qa_resource *qa_resource_pool_find(const qa_resource_pool *, uint64_t id);
qa_vfs *qa_vfs_create(qa_resource_pool *pool, qa_error *error);
qa_resource_pool *qa_vfs_resources(const qa_vfs *vfs);
/* Copies mount/order/policy state while sharing immutable resources. Neither
 * view depends on the other's lifetime or subsequent configuration changes. */
qa_vfs *qa_vfs_clone(const qa_vfs *vfs, qa_error *error);
/* Pure conservative comparison of retained lookup authorities and complete
 * search policy. Clones qualify; independently reopened native authorities
 * may differ. Read journals, reference flags and allocation counters do not
 * affect this comparison. No native I/O, loading or mutation occurs. */
bool qa_vfs_lookup_equal(const qa_vfs *, const qa_vfs *);
void qa_vfs_destroy(qa_vfs *vfs);
bool qa_vfs_retain(qa_vfs *vfs, qa_error *error);

typedef struct qa_vfs_mount_info {
    qa_mount_id id;
    bool is_archive;
    qa_archive_kind format;
    qa_archive_comparison comparison;
    bool writable;
    bool user_overlay;
    bool referenced;
    const qa_sha256_digest *digest;
    bool q3_demo;
} qa_vfs_mount_info;
size_t qa_vfs_mount_count(const qa_vfs *vfs);
/* Pure allocation-lineage proof, including genuinely retired mount IDs.
 * Clones and restored views preserve the real contiguous issuance counter. */
bool qa_vfs_mount_id_was_issued(const qa_vfs *, qa_mount_id);
/* Info follows default search order. Its digest is borrowed until unmount. */
bool qa_vfs_mount_at(const qa_vfs *vfs, size_t index, qa_vfs_mount_info *out);
typedef struct qa_vfs_resource_origin {
    const char *mount_path;
    qa_fs_identity mount_identity;
    qa_fs_object_reference root_reference;
    qa_sha256_digest archive_digest;
    qa_archive_kind format;
    qa_archive_comparison comparison;
    bool archive;
} qa_vfs_resource_origin;
/* Actual successful acquisition lineage, including retired mounts. This
 * receipt conveys no native handle or authority to reopen a retired mount. */
bool qa_vfs_resource_origin_read(const qa_vfs *, qa_mount_id, const qa_resource *,
    qa_vfs_resource_origin *);
/* Diagnostic borrows remain valid until their mount/rule is removed or changed.
 * Pool resource borrows remain valid until trim or pool teardown; retain a
 * resource before keeping it across mutations. Includes cached resources with
 * zero external readers, and resources shared with other views of this pool. */
const char *qa_vfs_mount_path(const qa_vfs *, qa_mount_id);
/* Borrow the actual retained directory authority for a loose mount. Archives
 * and absent mount IDs return NULL. The view retains it until unmount/destroy;
 * consumers that outlive that association must retain the root themselves. */
qa_fs_root *qa_vfs_mount_root(const qa_vfs *, qa_mount_id);
/* A loose child mount retains its parent's real authority. The normalized
 * prefix is borrowed until unmount; ordinary loose mounts return "". */
const char *qa_vfs_mount_root_prefix(const qa_vfs *, qa_mount_id);
/* Genuine native roots admitted for this same loose mount across physical
 * restore mappings. Pure borrowed inventory, preserved by retained mounts and
 * clones; valid until unmount/import/destruction. Archives return false. */
bool qa_vfs_mount_root_references(const qa_vfs *, qa_mount_id,
    const qa_fs_object_reference **, size_t *);
size_t qa_vfs_prefix_count(const qa_vfs *);
bool qa_vfs_prefix_at(const qa_vfs *, size_t index, const char **prefix,
                       const qa_mount_id **order, size_t *count);
/* Pure current link policy, in the actual first-match order. Strings borrow
 * until link replacement/removal or view destruction. */
size_t qa_vfs_link_count(const qa_vfs *);
bool qa_vfs_link_at(const qa_vfs *, size_t, const char **source_prefix,
    qa_mount_id *target_mount, const char **target_prefix);
size_t qa_vfs_resource_count(const qa_vfs *);
const qa_resource *qa_vfs_resource_at(const qa_vfs *, size_t index, size_t *readers);
void qa_vfs_clear_references(qa_vfs *vfs);
/* Candidate-only accounting restoration. Caller first qualifies the complete
 * mount inventory/order; flags follow qa_vfs_mount_at search order. No file is
 * opened or marked through a synthetic acquisition. */
bool qa_vfs_restore_references(qa_vfs *,const bool *,size_t count,qa_error *);
typedef struct qa_vfs_read_opening {
    int64_t rank;
    const qa_mount_id *order;
    size_t order_count;
    const char *prefix;
    bool user_overlay;
} qa_vfs_read_opening;
typedef struct qa_vfs_read_reference {
    qa_mount_id mount;
    const qa_resource *resource;
    const char *path;
    const char *lookup_path;
    const char *link_source, *link_target;
    qa_vfs_read_opening opening;
} qa_vfs_read_reference;
/* Successful reads in this exact view retain their genuine resource and mount
 * provenance once per pair and complete opening recipe. Distinct genuine
 * alias openings remain separate rows. Probes do not enter this journal. Entries
 * remain borrowed until references clear, their mount is removed, or the view closes.
 * A journal generation changes when records are removed; append keeps it. */
uint64_t qa_vfs_read_generation(const qa_vfs *);
size_t qa_vfs_read_count(const qa_vfs *);
bool qa_vfs_read_at(const qa_vfs *, size_t, qa_vfs_read_reference *);
/* First successful opening of this journal row, independent of later order,
 * overlay and link changes. Borrowed until that row is removed. */
bool qa_vfs_read_opening_at(const qa_vfs *, size_t, qa_vfs_read_opening *);
/* First successful complete recipes for still-retained immutable resources.
 * These historical rows survive reference clears and mount retirement. */
size_t qa_vfs_retained_read_count(const qa_vfs *);
bool qa_vfs_retained_read_at(const qa_vfs *, size_t, qa_vfs_read_reference *);

/* New mounts append at lowest priority. Paths are native filesystem paths.
 * Repeated archive mounts share storage when file identity and format agree.
 * A writable directory permits explicit replacement/removal through this API;
 * its read precedence is still selected by the caller's order. */
bool qa_vfs_mount_archive(qa_vfs *vfs, const char *path, qa_archive_kind kind,
                          qa_archive_comparison comparison, qa_mount_id *out,
                          qa_error *error);
bool qa_vfs_mount_directory(qa_vfs *vfs, const char *path,
                            qa_archive_comparison comparison, bool writable,
                            qa_mount_id *out, qa_error *error);
/* Admit a loose search directory below an existing held parent without
 * opening or creating the child. Reads observe its actual later existence;
 * writes retain ordinary nofollow traversal and operation-specific creation. */
/* Admit a package through the actual contained file of a held parent root. */
bool qa_vfs_mount_archive_from(qa_vfs *,qa_fs_root *,const char *relative,
    qa_archive_kind,qa_archive_comparison,qa_mount_id *,qa_error *);
bool qa_vfs_mount_child(qa_vfs *,qa_fs_root *parent,const char *relative,
    qa_archive_comparison,bool writable,qa_mount_id *,qa_error *);
/* Build a normal new scoped mount from retained native authority. Archives
 * require the same pool and must match their complete immutable snapshot;
 * loose directories retain their actual root across resource pools.
 * Logical path labels remain unchanged; no source path is reopened. */
bool qa_vfs_mount_retained(qa_vfs *, const qa_vfs *, qa_mount_id,
    qa_archive_comparison, bool writable, qa_mount_id *, qa_error *);
/* Pure qualification of a scoped retained-mount recipe. IDs in ordered_ids
 * belong to retained; the scoped view has IDs 1..count in that exact order.
 * Mounts keep native identities, immutable packages, paths and writable flags;
 * comparison is replaced and demo admission is retained or forced. The view
 * must have no overlays, links, prefixes or global restrictions. Accounting
 * and read journals are ignored. No allocation, native I/O or mutation occurs. */
bool qa_vfs_retained_recipe_matches(const qa_vfs *view, const qa_vfs *retained,
    const qa_mount_id *ordered_ids, size_t count, qa_archive_comparison comparison,
    bool force_mount_q3_demo);
/* Pure proof that a Source search path still ends in its retained fixed base.
 * Added game mounts precede the base; existing base IDs and policy survive. */
bool qa_vfs_retained_base_matches(const qa_vfs *,const qa_vfs *base);
bool qa_vfs_unmount(qa_vfs *vfs, qa_mount_id mount, qa_error *error);
/* Orders include every current mount exactly once, highest priority first.
 * A prefix is a relative directory without its trailing separator. The first
 * matching prefix rule wins. Setting an existing prefix replaces its order;
 * NULL order removes that rule. New/unmounted IDs update all existing orders. */
bool qa_vfs_set_order(qa_vfs *vfs, const qa_mount_id *order, size_t count,
                      qa_error *error);
bool qa_vfs_set_prefix_order(qa_vfs *vfs, const char *prefix,
                             const qa_mount_id *order, size_t count,
                             qa_error *error);
/* User overlays precede resource links and ordinary orders. */
bool qa_vfs_set_user_overlay(qa_vfs *vfs, qa_mount_id mount, bool enabled,
                             qa_error *error);
/* Required archive digests must be mounted. Listed archives gain priority;
 * other archives and nonconfiguration loose assets are excluded. Zero digests
 * disables pure restrictions. Priority changes persist until an explicit new
 * order is selected. Demo mode validates source PK3 checksums and
 * restricts loose assets. User overlays remain available in both modes. */
bool qa_vfs_set_restrictions(qa_vfs *vfs, const qa_sha256_digest *archives,
                              size_t count, bool q3_demo, qa_error *error);
/* Borrows actual current policy without native I/O or journal mutation. The
 * digest array remains valid until policy replacement or view destruction. */
bool qa_vfs_restrictions_read(const qa_vfs *, const qa_sha256_digest **archives,
    size_t *count, bool *q3_demo);
/* Demo admission belongs to this genuine Q3 mount in a mixed-family view.
 * Archives require the source PK3 checksum; loose media follows files.c's
 * configuration exceptions. Retained mounts and clones preserve the flag. */
bool qa_vfs_set_mount_q3_demo(qa_vfs *, qa_mount_id, bool, qa_error *);
const qa_sha256_digest *qa_vfs_archive_digest(const qa_vfs *vfs, qa_mount_id mount);
/* Borrow the already decoded archive for complete source entry enumeration.
 * Null for loose or missing mounts; valid until that mount is removed. */
const qa_archive *qa_vfs_archive(const qa_vfs *vfs, qa_mount_id mount);
/* Whole immutable bytes from this real mounted archive. Borrowed until unmount
 * or view teardown; consumers retain an owned copy across source replacement. */
bool qa_vfs_archive_bytes(const qa_vfs *, qa_mount_id, qa_bytes *, qa_error *);
bool qa_vfs_archive_checksums(qa_vfs *vfs, qa_mount_id mount, uint32_t feed,
                               uint32_t *checksum, uint32_t *pure_checksum,
                               qa_error *error);
/* First matching link wins and does not fall through on a miss. Prefixes retain
 * their trailing separator when supplied. The target must be a loose mount;
 * an empty target prefix refers to its root. NULL target removes the link. */
bool qa_vfs_set_link(qa_vfs *vfs, const char *source_prefix, qa_mount_id mount,
                     const char *target_prefix, qa_error *error);

typedef struct qa_vfs_listing {
    char **names;
    size_t count;
} qa_vfs_listing;
/* Source Q3 unfiltered listing: ordered mounts, at most 4095 distinct folded
 * names, archive descendants at the source depth, immediate loose entries.
 * Extension "/" requests directories. Empty directory selects the root. */
bool qa_vfs_list(qa_vfs *vfs, const char *directory, const char *extension,
                  qa_vfs_listing *out, qa_error *error);
void qa_vfs_listing_free(qa_vfs_listing *listing);

/* Resource requests normalize backslashes and reject absolute paths, empty
 * components, dot/parent components and colon. Failure sets *out to NULL.
 * PAK duplicate names use the first record; ZIP-family names use the last.
 * A changed archive fails future acquisition; held bytes remain valid. Changed
 * loose files acquire a new immutable version. out_mount is optional. */
bool qa_vfs_acquire(qa_vfs *vfs, const char *path, qa_resource **out,
                    qa_mount_id *out_mount, qa_error *error);
/* Owned receipt of this genuine acquisition's complete journal opening recipe.
 * Strings belong to the receipt, independently of journal lifetime. */
typedef struct qa_vfs_acquisition {
    qa_mount_id mount;
    uint64_t resource_id;
    char *path, *lookup_path, *link_source, *link_target;
    /* This acquisition's actual lookup order, owned independently of the
     * first-read journal. Restored legacy receipts may omit this snapshot. */
    qa_vfs_read_opening opening;
    bool opening_present;
} qa_vfs_acquisition;
bool qa_vfs_acquire_receipt(qa_vfs *, const char *, qa_resource **,
    qa_vfs_acquisition *empty_receipt, qa_error *);
void qa_vfs_acquisition_dispose(qa_vfs_acquisition *);
/* Copy the issued receipt, including its owned original opening order. */
bool qa_vfs_acquisition_copy(const qa_vfs_acquisition *, qa_vfs_acquisition *empty,
    qa_error *);
struct qa_source_save_io;
/* Preserve a held acquisition's actual order snapshot after its recipe fields
 * have been decoded. Historical issued IDs remain valid after unmount. */
bool qa_vfs_acquisition_opening_codec(struct qa_source_save_io *, const qa_vfs *, qa_vfs_acquisition *);
/* Match the complete actual opening recipe to its genuine journal resource and
 * mounted native identity. No admission, byte read or journal mutation. */
bool qa_vfs_acquisition_valid(const qa_vfs *, const qa_vfs_acquisition *, qa_error *);
/* Match a held immutable acquisition to its complete historical recipe and
 * resource origin, including after clear/unmount, without reopening a path.
 * Immutable checkpoint capture/restore uses
 * this recipe; live native-file admission uses acquisition_valid. */
bool qa_vfs_acquisition_retained(const qa_vfs *, const qa_vfs_acquisition *, qa_error *);
/* Ordinary lookup and policy admission without producing a read reference. */
bool qa_vfs_probe(qa_vfs *, const char *, bool *found, uint64_t *size, qa_error *);
typedef bool (*qa_vfs_accept_mount)(qa_mount_id mount, void *context);
bool qa_vfs_acquire_filtered_receipt(qa_vfs *, const char *, qa_vfs_accept_mount,
    void *context, qa_resource **, qa_vfs_acquisition *empty_receipt, qa_error *);
/* The filter must not mutate this VFS during acquisition. A rejected link
 * destination counts as a miss without falling through to ordinary mounts. */
bool qa_vfs_acquire_filtered(qa_vfs *vfs, const char *path,
                              qa_vfs_accept_mount accept, void *context,
                              qa_resource **out, qa_mount_id *out_mount,
                              qa_error *error);
bool qa_vfs_acquire_from(qa_vfs *vfs, qa_mount_id mount, const char *path,
                         qa_resource **out, qa_error *error);
void qa_resource_retain(qa_resource *resource);
void qa_resource_release(qa_resource *resource);
qa_bytes qa_resource_bytes(const qa_resource *resource);
/* IDs are unique within their resource pool. Paths retain source spelling. */
uint64_t qa_resource_id(const qa_resource *resource);
const char *qa_resource_path(const qa_resource *resource);
/* Persistent content identity, independent of process-local numeric handles. */
const qa_sha256_digest *qa_resource_digest(const qa_resource *resource);
bool qa_resource_archive_origin(const qa_resource *resource,
                                  qa_sha256_digest *archive_digest,
                                  size_t *member_ordinal);

/* Replace writes a complete sibling temporary file, then renames it atomically.
 * Parent directories are created within the selected writable root. Write paths
 * forbid symlinks. Existing acquired byte versions are never modified. */
bool qa_vfs_write(qa_vfs *vfs, qa_mount_id mount, const char *path,
                  qa_bytes bytes, qa_error *error);
bool qa_vfs_write_exclusive(qa_vfs *, qa_mount_id, const char *, qa_bytes,
                            bool *created, qa_error *);
bool qa_vfs_write_private(qa_vfs *, qa_mount_id, const char *, qa_bytes, qa_error *);
bool qa_vfs_remove(qa_vfs *vfs, qa_mount_id mount, const char *path,
                   qa_error *error);

typedef enum qa_vfs_write_mode {
    QA_VFS_WRITE,
    QA_VFS_APPEND,
    QA_VFS_APPEND_SYNC
} qa_vfs_write_mode;
typedef enum qa_vfs_seek_origin {
    QA_VFS_SEEK_SET,
    QA_VFS_SEEK_CURRENT,
    QA_VFS_SEEK_END
} qa_vfs_seek_origin;
typedef struct qa_vfs_file_state {
    const char *path;
    qa_vfs_write_mode mode;
    uint64_t position;
} qa_vfs_file_state;

/* Native writable descriptors survive VFS teardown. WRITE truncates only on a
 * fresh open; resume preserves bytes. Append modes create a missing target.
 * APPEND_SYNC preserves the source fflush contract with unbuffered writes. */
bool qa_vfs_file_open(qa_vfs *vfs, qa_mount_id mount, const char *path,
                       qa_vfs_write_mode mode, qa_vfs_file **out, qa_error *error);
bool qa_vfs_file_resume(qa_vfs *vfs, qa_mount_id mount,
                         const qa_vfs_file_state *state, qa_vfs_file **out,
                         qa_error *error);
bool qa_vfs_file_write(qa_vfs_file *file, qa_bytes bytes, size_t *written,
                        qa_error *error);
bool qa_vfs_file_seek(qa_vfs_file *file, int64_t offset,
                       qa_vfs_seek_origin origin, qa_error *error);
/* The returned path is borrowed until close; copy it into owned save state. */
qa_vfs_file_state qa_vfs_file_capture(const qa_vfs_file *file);
void qa_vfs_file_close(qa_vfs_file *file);

#endif
