#include "qa/catalog_write.h"
#include "qa/catalog_save.h"
#include <stdlib.h>
#include <string.h>

struct qa_catalog_write_resolver {
    qa_catalog *catalog;
    qa_product_id product;
    qa_mount_id mount;
    qa_fs_root *root;
};

static bool fail(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message); return false;
}

static bool mapped_root(void *context, const qa_fs_stream_reference *reference,
    qa_fs_root **out, qa_error *error)
{
    qa_catalog_write_resolver *resolver = context;
    if (out) *out = NULL;
    if (!resolver || !out || !qa_fs_stream_reference_valid(reference, error)) return false;
    const qa_catalog_mount *mount = qa_catalog_product_write_mount(resolver->catalog, resolver->product);
    qa_fs_root *root = qa_catalog_product_write_root(resolver->catalog, resolver->product);
    const qa_fs_object_reference *origins = NULL; size_t count = 0;
    if (!mount || mount->id != resolver->mount || root != resolver->root ||
        !qa_vfs_mount_root_references(qa_catalog_files(resolver->catalog), mount->id, &origins, &count))
        return fail(error, "Writable resolver lost its exact selected product root");
    bool found = false;
    for (size_t i = 0; i < count; ++i)
        if (origins[i].platform == reference->root.platform &&
            !memcmp(origins[i].words, reference->root.words, sizeof(reference->root.words))) {
            found = true; break;
        }
    if (!found) return fail(error, "Writable source root is outside the selected product's admitted lineage");
    qa_fs_root_retain(root); *out = root; return true;
}

bool qa_catalog_write_resolver_create(qa_catalog *catalog, qa_product_id product,
    qa_catalog_write_resolver **out, qa_error *error)
{
    const qa_catalog_mount *mount = qa_catalog_product_write_mount(catalog, product);
    qa_fs_root *root = qa_catalog_product_write_root(catalog, product);
    const qa_fs_object_reference *origins = NULL; size_t count = 0;
    if (!catalog || !out || *out || !mount || !root ||
        !qa_vfs_mount_root_references(qa_catalog_files(catalog), mount->id, &origins, &count) || !count)
        return fail(error, "Writable resolver requires the actual selected catalog product root");
    qa_catalog_write_resolver *resolver = calloc(1, sizeof(*resolver));
    if (!resolver) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining selected product writable resolver"); return false;
    }
    qa_catalog_retain(catalog);
    *resolver = (qa_catalog_write_resolver){catalog, product, mount->id, root};
    *out = resolver; return true;
}

void qa_catalog_write_resolver_destroy(qa_catalog_write_resolver *resolver)
{
    if (!resolver) return;
    qa_catalog_release(resolver->catalog); free(resolver);
}

qa_fs_root *qa_catalog_write_resolver_root(const qa_catalog_write_resolver *resolver)
{
    return resolver ? resolver->root : NULL;
}

qa_fs_stream_resolver qa_catalog_write_resolver_services(qa_catalog_write_resolver *resolver)
{
    return resolver ? (qa_fs_stream_resolver){resolver, mapped_root} : (qa_fs_stream_resolver){0};
}
