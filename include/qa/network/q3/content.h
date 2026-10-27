/* Port of src/network/q3/content.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_CONTENT_H
#define QA_NETWORK_Q3_CONTENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/archive.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/network/q3/pak_references.h"
/* TS Q3MountedPak. Strings inside pack borrow game/basename arguments and mount. */
typedef struct qa_q3_mounted_pak {
  qa_archive_mount mount;
  qa_q3_pak_catalog_entry pack;
} qa_q3_mounted_pak;
qa_q3_mounted_pak qa_q3_register_pak(const qa_archive_mount *mount, const qa_archive_handle *archive,
                                     const char *game, const char *basename, int64_t checksum_feed);
typedef struct qa_q3_content_references qa_q3_content_references;
/* packs array is borrowed and must outlive the references (identity map by mount id). */
bool qa_q3_content_references_init(qa_q3_content_references **out, qa_q3_mounted_pak *packs, size_t pack_count,
                                   int64_t checksum_feed, qa_q3_pak_random_fn random, void *random_ctx);
void qa_q3_content_references_destroy(qa_q3_content_references *r);
qa_q3_pak_references *qa_q3_content_references_get_references(const qa_q3_content_references *r);
qa_q3_mounted_pak *qa_q3_content_references_packs(const qa_q3_content_references *r, size_t *count);
void qa_q3_content_references_opened(qa_q3_content_references *r, const qa_resolved_resource_reference *reference);
/* pureMountPolicy: archives points into out_storage array (caller frees array). */
bool qa_q3_content_references_pure_mount_policy(const qa_q3_content_references *r, const int64_t *server_checksums, size_t count,
                                                qa_pure_mount_policy *out, qa_content_digest **out_storage);
char *qa_q3_content_references_referenced_pure_command(const qa_q3_content_references *r, int64_t server_id);
#endif
