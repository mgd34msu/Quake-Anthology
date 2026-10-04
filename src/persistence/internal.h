#ifndef QA_PERSISTENCE_INTERNAL_H
#define QA_PERSISTENCE_INTERNAL_H

#include "qa/save.h"
#include "qa/network.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

struct qa_save_image {
    qa_save_metadata metadata;
    qa_save_record *records;
    size_t count;
    qa_native_resource_inventory *native_resources;
    qa_save_native_release_fn native_release;
    bool retiring;
};

static inline bool persistence_fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message);
    return false;
}

static inline bool persistence_io_fail(qa_source_save_io *io, qa_status code, const char *text)
{
    if (io && !io->failed) qa_error_set(io->error, code, io->offset, "%s", text);
    if (io) io->failed = true;
    return false;
}

bool persistence_owner_valid(const qa_save_owner *, qa_error *);
bool persistence_owner_set(const qa_save_record *, size_t, qa_error *);
bool persistence_image_create_owned(const qa_save_metadata *, qa_save_record *, size_t,
                                    qa_save_image **, qa_error *);

#endif
