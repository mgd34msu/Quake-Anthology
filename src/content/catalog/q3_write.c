#include "qa/catalog_write.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

struct qa_catalog_write_resolver {
    qa_catalog *catalog;
    qa_product_id product;
    qa_mount_id mount;
    qa_fs_root *root;
    qa_vfs *files;
    bool component;
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
    if (resolver->component) {
        qa_fs_root_retain(resolver->root); *out = resolver->root; return true;
    }
    const qa_catalog_mount *mount = qa_catalog_product_write_mount(resolver->catalog, resolver->product);
    qa_fs_root *root = qa_catalog_product_write_root(resolver->catalog, resolver->product);
    const qa_fs_object_reference *origins = NULL; size_t count = 0;
    if (!mount || root != resolver->root ||
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

static char *name_write(char *out, const char *name)
{
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '+') *out++ = (char)*p;
        else { *out++ = '%'; *out++ = hex[*p >> 4]; *out++ = hex[*p & 15]; }
    }
    return out;
}

static char *component_path(const char *product, const char *component, qa_error *error)
{
    size_t a = strlen(product), b = strlen(component);
    if (a > (SIZE_MAX - 8) / 3 || b > (SIZE_MAX - 8) / 3 - a) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Component write path is too long"); return NULL;
    }
    char *path = malloc(8 + 3 * (a + b));
    if (!path) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining component write path"); return NULL;
    }
    memcpy(path, ".mods/", 6);
    char *at = name_write(path + 6, product);
    *at++ = '/'; at = name_write(at, component); *at = 0;
    return path;
}

bool qa_catalog_write_resolver_create(qa_catalog *catalog, qa_product_id product,
    const char *component, const qa_vfs *installed,
    qa_catalog_write_resolver **out, qa_error *error)
{
    const qa_product *selected = qa_catalog_product(catalog, product);
    qa_fs_root *root = qa_catalog_product_write_root(catalog, product);
    if (!selected || !installed || !out || *out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Writable resolver requires a selected product and installed files");
        return false;
    }
    qa_catalog_write_resolver *resolver = calloc(1, sizeof(*resolver));
    if (!resolver) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining selected product writable resolver"); return false;
    }
    qa_catalog_retain(catalog);
    resolver->catalog = catalog; resolver->product = product; resolver->root = root;
    resolver->component = component && *component;
    if (!resolver->component || (!root && (!catalog->user || !*catalog->user))) {
        if (!qa_vfs_retain((qa_vfs *)installed, error)) goto failed;
        resolver->files = (qa_vfs *)installed;
        for (size_t i = 0; root && i < qa_vfs_mount_count(installed); ++i) {
            qa_vfs_mount_info mount;
            if (qa_vfs_mount_at(installed, i, &mount) && mount.writable && !mount.is_archive &&
                qa_fs_root_same_object(root, qa_vfs_mount_root(installed, mount.id))) {
                resolver->mount = mount.id; break;
            }
        }
        *out = resolver; return true;
    }
    resolver->files = qa_vfs_clone(installed, error);
    if (!resolver->files) goto failed;
    const qa_catalog_mod *mod = qa_catalog_mod_find(catalog, component);
    char *relative = component_path(selected->key, mod ? mod->id : component, error);
    char *path = NULL;
    qa_fs_root *parent = NULL;
    bool opened = relative != NULL;
    if (opened && catalog->user && *catalog->user)
        opened = qa_fs_path_create_directory(catalog->user, error) && qa_fs_root_open(catalog->user, &parent, error);
    else if (opened) { parent = root; qa_fs_root_retain(parent); }
    bool mounted = opened && qa_fs_root_create_directory(parent, relative, error) &&
        qa_fs_root_join(parent, relative, &path, error) &&
        qa_vfs_mount_directory(resolver->files, path, QA_ARCHIVE_ASCII_INSENSITIVE, true, &resolver->mount, error) &&
        qa_vfs_set_user_overlay(resolver->files, resolver->mount, true, error);
    free(path); free(relative); qa_fs_root_close(parent);
    if (!mounted) goto failed;
    resolver->root = qa_vfs_mount_root(resolver->files, resolver->mount);
    *out = resolver; return true;
failed:
    qa_catalog_write_resolver_destroy(resolver); return false;
}

void qa_catalog_write_resolver_destroy(qa_catalog_write_resolver *resolver)
{
    if (!resolver) return;
    qa_vfs_destroy(resolver->files);
    qa_catalog_release(resolver->catalog); free(resolver);
}

qa_fs_root *qa_catalog_write_resolver_root(const qa_catalog_write_resolver *resolver)
{
    return resolver ? resolver->root : NULL;
}

qa_vfs *qa_catalog_write_resolver_files(const qa_catalog_write_resolver *resolver)
{
    return resolver ? resolver->files : NULL;
}

qa_mount_id qa_catalog_write_resolver_mount(const qa_catalog_write_resolver *resolver)
{
    return resolver ? resolver->mount : 0;
}

qa_fs_stream_resolver qa_catalog_write_resolver_services(qa_catalog_write_resolver *resolver)
{
    return resolver && resolver->root ? (qa_fs_stream_resolver){resolver, mapped_root} : (qa_fs_stream_resolver){0};
}
