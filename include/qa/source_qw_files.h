#ifndef QA_SOURCE_QW_FILES_H
#define QA_SOURCE_QW_FILES_H
#include "qa/catalog.h"
char *qa_source_files_child_path(const char *, const char *, qa_error *);
/* Opens an empty base or reuses its already qualified current owner. */
bool qa_source_qw_files_base(const qa_catalog *, qa_product_id,
    qa_vfs **base, qa_product_id *base_product, char **directory,
    char **home_prefix, qa_error *);
bool qa_source_qw_files_change(const qa_vfs *base, qa_vfs *authority,
    qa_mount_id family, qa_mount_id home, const char *home_prefix,
    const char *directory, qa_vfs **content, qa_mount_id *writable,
    char **write_child, qa_error *);
#endif
