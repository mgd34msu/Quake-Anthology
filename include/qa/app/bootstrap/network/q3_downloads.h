/* Port of src/app/bootstrap/network/q3-downloads.ts
 * (Q3ApplicationPackages). Mount-plan/catalog enumeration is
 * supplied by the caller (app content owner); archive opening,
 * registration, member sets, pure checksums and the guarded
 * download file stay here. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q3_DOWNLOADS_H
#define QA_APP_BOOTSTRAP_NETWORK_Q3_DOWNLOADS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/network/q3/content.h"
#include "qa/network/q3/download.h"
/* One plan mount in defaultOrder sequence. */
typedef struct qa_app_q3_package_mount {
  bool is_archive; qa_archive_mount mount;   /* format must be PK3 */
  const char *product_family;                /* expectation.family */
  const char *content_directory;             /* expectation.contentDirectory */
} qa_app_q3_package_mount;
typedef struct qa_app_q3_packages qa_app_q3_packages;
/* NULL only on OOM. Owns packs/references; mount strings borrowed. */
qa_app_q3_packages *qa_app_q3_packages_open(const qa_app_q3_package_mount *mounts,
    size_t mount_count, double checksum_feed);
void qa_app_q3_packages_destroy(qa_app_q3_packages *packages);
const qa_q3_mounted_pak *qa_app_q3_packages_packs(const qa_app_q3_packages *packages, size_t *out_count);
qa_q3_content_references *qa_app_q3_packages_references(qa_app_q3_packages *packages);
/* TS collect(): each opened reference whose product family is q3
 * (per is_q3) is reported once. */
void qa_app_q3_packages_collect(qa_app_q3_packages *packages,
    const qa_resolved_resource_reference *const *references, size_t count,
    bool (*is_q3)(void *ctx, const qa_resolved_resource_reference *reference), void *ctx);
/* false = TS undefined. */
bool qa_app_q3_packages_pure_checksum(const qa_app_q3_packages *packages,
    const char *path, double *out);
/* false = TS null. On success out is a landed read file whose ctx
 * the caller releases through its close(). */
bool qa_app_q3_packages_open_download(qa_app_q3_packages *packages,
    const char *name, qa_q3_download_read_file *out);
#endif
