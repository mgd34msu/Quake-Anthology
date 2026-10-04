#include "internal.h"
#include "qa/filesystem.h"

#include <ctype.h>
#include <stdio.h>

typedef struct directory_entry { const char *name, *path; bool directory, regular; } directory_entry;
typedef struct directory { directory_entry *entries; size_t count, capacity; } directory;

static const char *join(qa_catalog *c, const char *a, const char *b, qa_error *error)
{
    size_t x = strlen(a), y = strlen(b);
    if (x > SIZE_MAX - y - 2) { qa_error_set(error, QA_ERROR_MEMORY, 0, "content path too long"); return NULL; }
    char *path = malloc(x + y + 2);
    if (!path) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate content path"); return NULL; }
    memcpy(path, a, x); path[x] = '/'; memcpy(path + x + 1, b, y + 1);
    const char *result = catalog_string(c, path, error);
    free(path); return result;
}

static int name_order(const void *a, const void *b)
{ return strcmp(((const directory_entry *)a)->name, ((const directory_entry *)b)->name); }

static bool managed_addon(const char *name)
{
    if (strlen(name) != 44 || strncmp(name, "qd_", 3) || name[23] != '_') return false;
    for (size_t i = 3; i < 44; ++i)
        if (i != 23 && !((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
    return true;
}

static bool read_directory(qa_catalog *c, const char *path, directory *out, qa_error *error)
{
    qa_fs_listing listing = {0};
    qa_error local = {0};
    if (!qa_fs_path_list(path, &listing, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = local;
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < listing.count; ++i) {
        const qa_fs_entry *entry = &listing.entries[i];
        const char *name = catalog_string(c, entry->name, error);
        const char *full = name ? join(c, path, name, error) : NULL;
        if (!full) { ok = false; break; }
        if (!catalog_grow((void **)&out->entries, &out->capacity, out->count + 1, sizeof(*out->entries), error)) { ok = false; break; }
        out->entries[out->count++] = (directory_entry){
            name, full, entry->kind == QA_FS_DIRECTORY,
            entry->kind == QA_FS_REGULAR
        };
    }
    qa_fs_listing_free(&listing);
    if (ok && out->count > 1) qsort(out->entries, out->count, sizeof(*out->entries), name_order);
    return ok;
}

static bool catalog_name_equal(const char *left, const char *right,
                               void *context)
{
    (void)context;
    return catalog_ascii_equal(left, right);
}

bool catalog_physical_path(qa_catalog *c, const char *root, const char *relative,
                   const char **out, qa_error *error)
{
    *out = NULL;
    if (!*relative) {
        qa_fs_entry_kind kind;
        if (!qa_fs_path_status(root,true,&kind,NULL,error)) return false;
        if (kind==QA_FS_DIRECTORY) {
            qa_fs_root *opened=NULL; char *path=NULL;
            bool ok=qa_fs_root_open(root,&opened,error) && qa_fs_root_join(opened,"",&path,error);
            if (ok) *out=catalog_string(c,path,error);
            free(path); qa_fs_root_close(opened);
            if (!ok) return false;
        }
        return kind!=QA_FS_DIRECTORY || *out!=NULL;
    }
    char *normalized = qa_archive_normalize_path(relative, error);
    if (!normalized) return false;
    qa_error local = {0};
    qa_fs_entry_kind root_kind;
    if (!qa_fs_path_status(root, true, &root_kind, NULL, &local)) {
        free(normalized);
        if (error) *error = local;
        return false;
    }
    if (root_kind != QA_FS_DIRECTORY) {
        free(normalized);
        return true;
    }
    qa_fs_root *opened = NULL;
    if (!qa_fs_root_open(root, &opened, &local)) {
        free(normalized);
        if (local.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = local;
        return false;
    }
    char *resolved = NULL;
    if (!qa_fs_root_resolve(opened, normalized, catalog_name_equal, NULL,
                            false, &resolved, &local)) {
        qa_fs_root_close(opened);
        free(normalized);
        if (local.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = local;
        return false;
    }
    qa_fs_entry_kind kind;
    bool valid = qa_fs_root_status(opened, resolved, &kind, NULL, &local);
    char *native = NULL;
    if (valid && kind != QA_FS_MISSING)
        valid = qa_fs_root_join(opened, resolved, &native, &local);
    qa_fs_root_close(opened);
    free(normalized);
    free(resolved);
    if (!valid) {
        if (error) *error = local;
        return false;
    }
    if (kind == QA_FS_MISSING) return true;
    *out = catalog_string(c, native, error);
    free(native);
    return *out != NULL;
}

bool catalog_path(qa_catalog *c,const char *root,const char *relative,const char **out,qa_error *error)
{
    if (!strcmp(root,c->root)) {
        size_t longest=0;
        for (size_t i=0;i<c->location_count;++i) {
            size_t length=strlen(c->locations[i].logical);
            if (length>longest && !strncmp(relative,c->locations[i].logical,length) &&
                (!relative[length] || relative[length]=='/')) longest=length;
        }
        for (size_t length=longest;length;--length) for (size_t i=0;i<c->location_count;++i) {
            const catalog_location *location=c->locations+i;
            if (strlen(location->logical)!=length || strncmp(relative,location->logical,length) ||
                (relative[length] && relative[length]!='/')) continue;
            const char *tail=relative+length;
            if (*tail=='/') ++tail;
            if (!catalog_physical_path(c,location->path,tail,out,error)) return false;
            if (*out) return true;
        }
    }
    return catalog_physical_path(c,root,relative,out,error);
}

static bool regular_file(const char *path, bool *regular, qa_error *error)
{
    *regular = false;
    if (path == NULL) return true;
    qa_fs_entry_kind kind;
    if (!qa_fs_path_status(path, true, &kind, NULL, error)) return false;
    *regular = kind == QA_FS_REGULAR;
    return true;
}

static bool append_mount(catalog_product *p, qa_mount_id id, qa_error *error)
{
    for (size_t i = 0; i < p->own_count; ++i) if (p->own_mounts[i] == id) return true;
    qa_mount_id *next = realloc(p->own_mounts, (p->own_count + 1) * sizeof(*next));
    if (!next) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain product mounts"); return false; }
    p->own_mounts = next; next[p->own_count++] = id; return true;
}

static bool mount_file(qa_catalog *c, catalog_product *p, const char *path,
                        qa_archive_kind kind, bool writable, qa_error *error)
{
    for (size_t i = 0; i < c->physical_count; ++i) if (c->physical[i].view.id != c->corpus_mount &&
        !strcmp(c->physical[i].view.path, path))
        return !p || append_mount(p, c->physical[i].view.id, error);
    qa_mount_id id;
    qa_error reason = {0};
    bool ok = kind == QA_ARCHIVE_AUTO ? qa_vfs_mount_directory(c->mounts, path, QA_ARCHIVE_CASE_INSENSITIVE, writable, &id, &reason)
        : qa_vfs_mount_archive(c->mounts, path, kind, QA_ARCHIVE_CASE_INSENSITIVE, &id, &reason);
    if (!ok) {
        if (!p || reason.code == QA_ERROR_MEMORY) { if (error) *error = reason; return false; }
        return catalog_requirement(c, p, reason.message, error);
    }
    if (!catalog_grow((void **)&c->physical, &c->physical_capacity, c->physical_count + 1, sizeof(*c->physical), error)) return false;
    qa_mount_id identity = id;
    catalog_physical *physical = &c->physical[c->physical_count++];
    *physical = (catalog_physical){.view = {identity, path, kind, writable, NULL}};
    if (kind != QA_ARCHIVE_AUTO) {
        const qa_archive *archive = qa_vfs_archive(c->mounts, id);
        size_t count = qa_archive_count(archive);
        physical->members = calloc(count ? count : 1, sizeof(*physical->members));
        if (!physical->members) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain package metadata"); return false; }
        for (size_t i = 0; i < count; ++i) {
            const qa_archive_entry *entry = qa_archive_entry_at(archive, i);
            if (entry->is_directory) continue;
            const char *name = catalog_string(c, entry->path, error);
            if (!name) return false;
            physical->members[physical->member_count++] = (catalog_member){name, i};
        }
    }
    for (size_t i = 0; i < c->physical_count; ++i)
        c->physical[i].view.digest = qa_vfs_archive_digest(c->mounts, c->physical[i].view.id);
    return !p || append_mount(p, identity, error);
}

static int folded_order(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return x < y ? -1 : 1;
    }
    return *a ? 1 : *b ? -1 : 0;
}

static int q3_order(const void *a, const void *b)
{
    const directory_entry *x = a, *y = b;
    int value = folded_order(y->name, x->name);
    return value ? value : strcmp(y->name, x->name);
}

static int q2_order(const void *a, const void *b)
{
    const char *x = ((const directory_entry *)a)->name, *y = ((const directory_entry *)b)->name;
    bool xp = strlen(x) >= 3 && tolower((unsigned char)x[0]) == 'p' && tolower((unsigned char)x[1]) == 'a' && tolower((unsigned char)x[2]) == 'k';
    bool yp = strlen(y) >= 3 && tolower((unsigned char)y[0]) == 'p' && tolower((unsigned char)y[1]) == 'a' && tolower((unsigned char)y[2]) == 'k';
    if (xp != yp) return xp ? 1 : -1;
    if (!xp) return q3_order(a, b);
    char *xe, *ye;
    unsigned long long xn = strtoull(x + 3, &xe, 10), yn = strtoull(y + 3, &ye, 10);
    if (xn != yn) return xn > yn ? -1 : 1;
    int value = folded_order(ye, xe);
    return value ? value : strcmp(y, x);
}

static bool scan_directory(qa_catalog *c, catalog_product *p, const char *root,
                            bool writable, bool corpus, qa_error *error)
{
    const char *path;
    if (corpus) {
        if (!catalog_product_directory(c,p,&path,error)) return false;
        p->installed_directory=path;
    } else if (!catalog_physical_path(c, root, p->view.directory, &path, error)) return false;
    if (!path) return true;
    directory dir = {0};
    if (!read_directory(c, path, &dir, error)) { free(dir.entries); return false; }
    bool ok = true;
    if (p->view.family == QA_GAME_Q1) {
        for (size_t i = dir.count; i-- > 0;) {
            directory_entry *e = &dir.entries[i];
            if (e->regular && (catalog_suffix(e->name, ".pk3") || catalog_suffix(e->name, ".kpf")) &&
                !mount_file(c, p, e->path, qa_archive_kind_for_path(e->name), false, error)) { ok = false; goto done; }
        }
        size_t contiguous = 0;
        for (;;) {
            char name[40]; snprintf(name, sizeof(name), "pak%zu.pak", contiguous);
            bool found = false;
            for (size_t i = 0; i < dir.count; ++i) if (dir.entries[i].regular && catalog_ascii_equal(name, dir.entries[i].name)) { found = true; break; }
            if (!found) break;
            ++contiguous;
        }
        while (contiguous) {
            char name[40]; snprintf(name, sizeof(name), "pak%zu.pak", --contiguous);
            for (size_t i = 0; i < dir.count; ++i) if (dir.entries[i].regular && catalog_ascii_equal(name, dir.entries[i].name)) {
                if (!mount_file(c, p, dir.entries[i].path, QA_ARCHIVE_PAK, false, error)) { ok = false; goto done; }
                break;
            }
        }
    } else {
        if (dir.count > 1) qsort(dir.entries, dir.count, sizeof(*dir.entries), p->view.family == QA_GAME_Q2 ? q2_order : q3_order);
        for (size_t i = 0; i < dir.count; ++i) {
            directory_entry *e = &dir.entries[i];
            if (!e->regular) continue;
            qa_archive_kind kind = QA_ARCHIVE_AUTO;
            if (p->view.family == QA_GAME_Q3 && catalog_suffix(e->name, ".pk3")) kind = QA_ARCHIVE_PK3;
            if (p->view.family == QA_GAME_Q2) {
                if (catalog_suffix(e->name, ".pak")) kind = QA_ARCHIVE_PAK;
                else if (catalog_suffix(e->name, ".pkz")) kind = QA_ARCHIVE_ZIP;
            }
            if (kind != QA_ARCHIVE_AUTO && !mount_file(c, p, e->path, kind, false, error)) { ok = false; goto done; }
        }
    }
    ok = mount_file(c, p, path, QA_ARCHIVE_AUTO, writable, error);
    if (ok) for (size_t i = 0; i < p->own_count; ++i) {
        const qa_catalog_mount *mount = catalog_mount(c, p->own_mounts[i]);
        if (mount && mount->format == QA_ARCHIVE_AUTO && !strcmp(mount->path, path)) {
            if (writable && mount->writable) p->write_mount = mount->id;
            if (corpus) p->loose_mount = mount->id;
        }
    }
done:
    free(dir.entries); return ok;
}

static bool content_directory(qa_catalog *c, const char *path, bool *result, qa_error *error)
{
    directory dir = {0}; *result = false;
    if (!read_directory(c, path, &dir, error)) { free(dir.entries); return false; }
    for (size_t i = 0; i < dir.count; ++i) {
        directory_entry *e = &dir.entries[i];
        if ((e->directory && (catalog_ascii_equal(e->name, "maps") || catalog_ascii_equal(e->name, "models") || catalog_ascii_equal(e->name, "vm"))) ||
            (e->regular && (qa_archive_kind_for_path(e->name) != QA_ARCHIVE_AUTO || catalog_suffix(e->name, ".pkz") ||
              catalog_ascii_equal(e->name, "progs.dat") || catalog_ascii_equal(e->name, "qwprogs.dat") ||
              (strstr(e->name, "game") == e->name && (catalog_suffix(e->name, ".dll") || catalog_suffix(e->name, ".so")))))) {
            *result = true; break;
        }
    }
    free(dir.entries); return true;
}

static bool discover_directory(qa_catalog *c, const char *root, const char *parent,
                               qa_product_id base_id,const char *physical, qa_error *error)
{
    const char *path=physical;
    if (!path && !catalog_path(c, root, parent, &path, error)) return false;
    if (!path) return true;
    directory dir = {0};
    if (!read_directory(c, path, &dir, error)) { free(dir.entries); return false; }
    bool ok = false;
    for (size_t i = 0; i < dir.count; ++i) {
        directory_entry *e = &dir.entries[i];
        if (!e->directory || catalog_ascii_equal(e->name, "rerelease")) continue;
        const char *relative = join(c, parent, e->name, error);
        if (!relative) goto done;
        const qa_product *base = qa_catalog_product(c, base_id);
        bool known = false;
        for (size_t j = 0; j < c->product_count; ++j)
            if (c->products[j].view.edition == base->edition && catalog_ascii_equal(c->products[j].view.directory, relative)) { known = true; break; }
        for (size_t j=0;!known && j<c->location_count;++j) if (!strcmp(c->locations[j].path,e->path))
            for (size_t k=0;k<c->product_count;++k)
                if (c->products[k].view.builtin && c->products[k].view.family==base->family &&
                    !strcmp(c->products[k].view.directory,c->locations[j].logical)) { known=true; break; }
        if (known) continue;
        bool has_content;
        if (!content_directory(c, e->path, &has_content, error)) goto done;
        if (!has_content) continue;
        if (!catalog_safe_name(e->name)) continue;
        if (managed_addon(e->name)) {
            const char *removed = join(c, ".addons/removed", relative, error), *marker;
            if (!removed || !catalog_path(c, root, removed, &marker, error)) goto done;
            if (marker) continue;
        }
        static const char *families[] = { "q1", "q2", "q3" };
        static const char *editions[] = { "classic", "rerelease", "quakeworld", "demo" };
        const char *family = families[base->family], *edition = editions[base->edition];
        size_t bytes = strlen(e->name) + strlen(family) + strlen(edition) + 32;
        char *key = malloc(bytes);
        if (!key) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate mod identity"); goto done; }
        snprintf(key, bytes, "%s-%s-%s", family, edition, e->name);
        qa_product view = {.base = base_id, .family = base->family, .edition = base->edition,
            .directory = relative, .campaign = e->name, .title = e->name,
            .key = catalog_string(c, key, error)};
        snprintf(key, bytes, "%s:%s:%s:installed", family, edition, e->name);
        view.identity = catalog_string(c, key, error); free(key);
        catalog_product *p;
        if (!view.key || !view.identity || !catalog_add_product(c, &view, &p, error)) goto done;
        if (physical) p->installed_directory=e->path;
    }
    ok = true;
done:
    free(dir.entries); return ok;
}

static bool finalize_product(qa_catalog *c, catalog_product *p, unsigned depth, qa_error *error)
{
    if (p->mounts) return true;
    if (depth > c->product_count) { qa_error_set(error, QA_ERROR_FORMAT, 0, "cyclic base product"); return false; }
    catalog_product *base = p->view.base ? &c->products[p->view.base - 1] : NULL;
    if (base && !finalize_product(c, base, depth + 1, error)) return false;
    size_t count = p->own_count + (base ? base->mount_count : 0);
    p->mounts = malloc((count ? count : 1) * sizeof(*p->mounts));
    if (!p->mounts) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain product mount order"); return false; }
    for (size_t i = 0; i < count; ++i) {
        qa_mount_id id = i < p->own_count ? p->own_mounts[i] : base->mounts[i - p->own_count];
        bool seen = false;
        for (size_t j = 0; j < p->mount_count; ++j) if (p->mounts[j] == id) { seen = true; break; }
        if (!seen) p->mounts[p->mount_count++] = id;
    }
    if (base && base->view.availability != QA_CONTENT_INSTALLED && !catalog_requirement(c, p, base->view.key, error)) return false;
    return true;
}

static bool quakeworld_variants(qa_catalog *c, qa_error *error)
{
    size_t initial_count = c->product_count;
    qa_product_id base = qa_catalog_find(c, "q1-quakeworld")->id;
    for (size_t i = 0; i < initial_count; ++i) {
        catalog_product *p = &c->products[i];
        if (p->view.family != QA_GAME_Q1 || p->view.edition != QA_EDITION_CLASSIC) continue;
        bool found = false;
        for (size_t j = 0; j < p->own_count; ++j) {
            const catalog_physical *physical = catalog_package(c, p->own_mounts[j]);
            if (physical->view.format == QA_ARCHIVE_AUTO) {
                const char *program;
                if (!catalog_path(c, physical->view.path, "qwprogs.dat", &program, error)) return false;
                if (program) found = true;
            } else {
                for (size_t k = 0; k < physical->member_count; ++k)
                    if (catalog_ascii_equal(physical->members[k].path, "qwprogs.dat")) { found = true; break; }
            }
            if (found) break;
        }
        if (!found) continue;
        size_t size = strlen(p->view.campaign) + 48;
        char *text = malloc(size);
        if (!text) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate QuakeWorld variant identity"); return false; }
        qa_product view = p->view;
        view.edition = QA_EDITION_QUAKEWORLD; view.base = base; view.builtin = false;
        view.program_product = 0; view.program = NULL;
        view.requirements = NULL; view.requirement_count = 0;
        snprintf(text, size, "q1-quakeworld-%s", p->view.campaign);
        view.key = catalog_string(c, text, error);
        snprintf(text, size, "q1:quakeworld:%s:installed", p->view.campaign);
        /* The ordinary qw product already owns the id1 identity. */
        if (!strcmp(p->view.campaign, "id1")) snprintf(text, size, "q1:quakeworld:mod-id1:installed");
        view.identity = catalog_string(c, text, error); free(text);
        if (!view.key || !view.identity) return false;
        size_t own_count = p->own_count;
        qa_mount_id *own = malloc((own_count ? own_count : 1) * sizeof(*own));
        if (!own) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain QuakeWorld content mounts"); return false; }
        memcpy(own, p->own_mounts, own_count * sizeof(*own));
        qa_mount_id write_mount = p->write_mount, loose_mount = p->loose_mount;
        const char *installed_directory=p->installed_directory;
        catalog_product *variant;
        if (!catalog_add_product(c, &view, &variant, error)) { free(own); return false; }
        variant->own_mounts = own; variant->own_count = own_count;
        variant->write_mount = write_mount; variant->loose_mount = loose_mount;
        variant->installed_directory=installed_directory;
    }
    return true;
}

static bool remote_product(qa_catalog *c, const char *base_key,
    const char *requested, qa_product_id *selected, qa_error *error)
{
    const qa_product *base = qa_catalog_find(c, base_key);
    if (!base || (base->family != QA_GAME_Q3 && base->family != QA_GAME_Q2 && base->family != QA_GAME_Q1)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Remote base is not a configured Quake product"); return false;
    }
    const char *leaf = strrchr(base->directory, '/');
    if (!leaf) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Remote base lacks its family directory"); return false; }
    const char *name = *requested ? requested : leaf + 1;
    if (!catalog_remote_name(name)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Remote game directory must be one safe name"); return false;
    }
    size_t length = strlen(name), parent = (size_t)(leaf - base->directory + 1);
    if (!length || length > SIZE_MAX - parent - 1 || length > SIZE_MAX - 64) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Remote game directory is too long"); return false;
    }
    char *relative = malloc(parent + length + 1);
    char *identity = malloc(length + 64);
    if (!relative || !identity) {
        free(relative); free(identity); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining remote game directory"); return false;
    }
    memcpy(relative, base->directory, parent);
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)name[i];
        relative[parent + i] = (char)(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
    }
    relative[parent + length] = 0;
    bool ok = false;
    for (size_t i = 0; i < c->product_count; ++i) {
        catalog_product *product = &c->products[i];
        if (product->view.family != base->family || product->view.edition != base->edition ||
            !catalog_ascii_equal(product->view.directory, relative)) continue;
        const qa_product *ancestor = &product->view;
        for (size_t j = 0; ancestor && j <= c->product_count; ++j) {
            if (ancestor->id == base->id) {
                product->remote_directory = product->view.id != base->id;
                *selected = product->view.id; ok = true; goto done;
            }
            ancestor = qa_catalog_product(c, ancestor->base);
        }
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Remote product does not inherit the requested base"); goto done;
    }
    static const char *editions[] = {"classic", "rerelease", "quakeworld", "demo"};
    const char *family = base->family == QA_GAME_Q1 ? "q1" : base->family == QA_GAME_Q2 ? "q2" : "q3";
    qa_product view = {.base = base->id, .family = base->family, .edition = base->edition,
        .directory = catalog_string(c, relative, error), .campaign = catalog_string(c, relative + parent, error)};
    view.title = view.campaign;
    snprintf(identity, length + 64, "%s-%s-%s", family, editions[view.edition], relative + parent);
    view.key = catalog_string(c, identity, error);
    snprintf(identity, length + 64, "%s:%s:%s:installed", family, editions[view.edition], relative + parent);
    view.identity = catalog_string(c, identity, error);
    catalog_product *product;
    if (!view.directory || !view.campaign || !view.key || !view.identity ||
        !catalog_add_product(c, &view, &product, error)) goto done;
    product->remote_directory = true; *selected = product->view.id; ok = true;
done:
    free(relative); free(identity); return ok;
}

static bool user_product_directories(qa_catalog *c, qa_error *error)
{
    if (!c->user) return true;
    qa_fs_root *root = NULL;
    if (!qa_fs_path_create_directory(c->user, error) || !qa_fs_root_open(c->user, &root, error)) return false;
    bool ok = true;
    for (size_t i = 0; i < c->product_count; ++i) {
        const qa_product *product = &c->products[i].view;
        if (product->family == QA_GAME_Q3) {
            const char *slash = strrchr(product->directory, '/');
            char *family = NULL, *resolved = NULL;
            qa_error issue = {0};
            if (!qa_fs_root_resolve(root, "q3a", catalog_name_equal, NULL, false, &family, &issue)) {
                if (issue.code != QA_ERROR_NOT_FOUND || !qa_fs_root_create_directory(root, "q3a", error) ||
                    !qa_fs_root_resolve(root, "q3a", catalog_name_equal, NULL, false, &family, error)) {
                    if (issue.code != QA_ERROR_NOT_FOUND && error) *error = issue;
                    ok = false; break;
                }
            }
            const char *relative = slash ? join(c, family, slash + 1, error) : NULL;
            free(family);
            issue = (qa_error){0};
            if (!relative) { ok = false; break; }
            if (!qa_fs_root_resolve(root, relative, catalog_name_equal, NULL, false, &resolved, &issue) &&
                (issue.code != QA_ERROR_NOT_FOUND || !qa_fs_root_create_directory(root, relative, error))) {
                if (issue.code != QA_ERROR_NOT_FOUND && error) *error = issue;
                free(resolved); ok = false; break;
            }
            free(resolved);
            continue;
        }
        const char *path;
        if (!catalog_path(c, c->user, c->products[i].view.directory, &path, error)
            || (!path && !qa_fs_root_create_directory(root,
                         c->products[i].view.directory, error))) {
            ok = false; break;
        }
    }
    qa_fs_root_close(root);
    return ok;
}

static bool product_content_directory(qa_catalog *c, const catalog_product *p,
                                       bool *found, qa_error *error)
{
    *found = false;
    for (size_t i = 0; i < p->own_count; ++i) {
        const qa_catalog_mount *mount = catalog_mount(c, p->own_mounts[i]);
        if (mount->format != QA_ARCHIVE_AUTO || !mount->writable) {
            *found = true; return true;
        }
        if (!content_directory(c, mount->path, found, error)) return false;
        if (*found) return true;
    }
    return true;
}

static bool corpus_directory(qa_catalog *c, qa_error *error)
{
    qa_fs_entry_kind kind;
    if (!qa_fs_path_status(c->root, true, &kind, NULL, error)) return false;
    if (kind != QA_FS_DIRECTORY) return true;
    qa_fs_root *root = NULL; char *path = NULL;
    bool ok = qa_fs_root_open(c->root, &root, error) && qa_fs_root_join(root, "", &path, error);
    const char *native = ok ? catalog_string(c, path, error) : NULL;
    if (ok) ok = native && mount_file(c, NULL, native, QA_ARCHIVE_AUTO, false, error);
    if (ok) for (size_t i = 0; i < c->physical_count; ++i)
        if (!strcmp(c->physical[i].view.path, native)) c->corpus_mount = c->physical[i].view.id;
    free(path); qa_fs_root_close(root); return ok;
}

bool catalog_scan(qa_catalog *c, bool mods, const char *remote_base,
    const char *remote_directory, qa_product_id *selected, qa_error *error)
{
    if (mods) {
        static const struct { const char *directory, *base; } roots[] = {
            {"q1", "q1-classic-id1"}, {"q1/rerelease", "q1-rerelease-id1"},
            {"q2", "q2-classic-baseq2"}, {"q2/rerelease", "q2-rerelease-baseq2"}, {"q3a", "q3-baseq3"}
        };
        for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
            qa_product_id base = qa_catalog_find(c, roots[i].base)->id;
            bool mapped=false;
            for (size_t j=0;j<c->location_count;++j) if (!strcmp(c->locations[j].logical,roots[i].directory)) {
                if (!discover_directory(c,c->root,roots[i].directory,base,c->locations[j].path,error)) return false;
                mapped=true;
            }
            if ((!mapped && !discover_directory(c, c->root, roots[i].directory, base, NULL,error)) ||
                (c->user && !discover_directory(c, c->user, roots[i].directory, base,NULL,error))) return false;
        }
    }
    if (remote_base && !remote_product(c, remote_base, remote_directory, selected, error)) return false;
    if (!user_product_directories(c, error) || !corpus_directory(c, error)) return false;
    for (size_t i = 0; i < c->product_count; ++i) {
        catalog_product *p = &c->products[i];
        if ((c->user && strcmp(c->user, c->root) && !scan_directory(c, p, c->user, true, false, error)) ||
            !scan_directory(c, p, c->root,
                            c->user && !strcmp(c->user, c->root), true, error)) return false;
        for (size_t j = 0; !p->remote_directory && j < p->required_count; ++j) {
            const char *path;
            const char *leaf=strrchr(p->required[j],'/');
            path=NULL;
            if (p->installed_directory && !catalog_physical_path(c,p->installed_directory,
                leaf?leaf+1:p->required[j],&path,error)) return false;
            /* A user package is an independent complete directory, not an
             * archive-by-archive repair of another installed package. */
            if (!p->installed_directory && c->user && !catalog_physical_path(c,c->user,p->required[j],&path,error)) return false;
            bool regular;
            if (!regular_file(path, &regular, error)) return false;
            if (!regular && !catalog_requirement(c, p, p->required[j], error)) return false;
        }
        bool content_present;
        if (!product_content_directory(c, p, &content_present, error)) return false;
        if (!content_present && !p->remote_directory && p->view.edition != QA_EDITION_QUAKEWORLD
            && !catalog_requirement(c, p, p->view.directory, error)) return false;
        if (p->view.edition == QA_EDITION_RERELEASE && !p->view.base) {
            const char *archive = p->view.family == QA_GAME_Q1 ? "q1/rerelease/QuakeEX.kpf" : "q2/rerelease/Q2Game.kpf", *path;
            path=NULL;
            const char *parent=p->installed_directory?catalog_native_parent(c,p->installed_directory,error):NULL;
            if (!parent && error && error->code==QA_ERROR_MEMORY) return false;
            if (parent) {
                const char *leaf=strrchr(archive,'/');
                bool ok=catalog_physical_path(c,parent,leaf?leaf+1:archive,&path,error);
                if (!ok) return false;
            }
            if (path && !mount_file(c, p, path, QA_ARCHIVE_KPF, false, error)) return false;
        }
    }
    if (mods && !quakeworld_variants(c, error)) return false;
    for (size_t i = 0; i < c->product_count; ++i) if (!finalize_product(c, &c->products[i], 0, error)) return false;
    for (size_t i = 0; i < c->product_count; ++i) {
        catalog_product *product = c->products + i;
        if (product->view.family != QA_GAME_Q1) continue;
        catalog_product *installed = product;
        size_t depth = 0;
        while (!installed->installed_directory && installed->view.base) {
            if (++depth > c->product_count) {
                qa_error_set(error, QA_ERROR_FORMAT, 0, "Q1 install family has cyclic ancestry"); return false;
            }
            installed = c->products + installed->view.base - 1;
        }
        if (!installed->installed_directory) continue;
        const char *family = catalog_native_parent(c, installed->installed_directory, error);
        if (!family) return false;
        bool retained = false;
        for (size_t j = 0; j < c->physical_count; ++j)
            if (c->physical[j].view.format == QA_ARCHIVE_AUTO && !c->physical[j].view.writable &&
                !strcmp(c->physical[j].view.path, family)) retained = true;
        if (!retained) {
            qa_mount_id id;
            if (!qa_vfs_mount_directory(c->mounts, family, QA_ARCHIVE_CASE_INSENSITIVE, false, &id, error) ||
                !catalog_grow((void **)&c->physical, &c->physical_capacity,
                    c->physical_count + 1, sizeof(*c->physical), error)) return false;
            c->physical[c->physical_count++] = (catalog_physical){.view = {id, family, QA_ARCHIVE_AUTO, false, NULL}};
            for (size_t j = 0; j < c->physical_count; ++j)
                c->physical[j].view.digest = qa_vfs_archive_digest(c->mounts, c->physical[j].view.id);
        }
        for (size_t j = 0; j < c->physical_count; ++j) {
            const qa_catalog_mount *mount = &c->physical[j].view;
            if (mount->format == QA_ARCHIVE_AUTO && !mount->writable && !strcmp(mount->path, family)) {
                product->family_mount = mount->id;
                product->family_product = installed->view.id;
                break;
            }
        }
    }
    const char *installed_q3=NULL;
    const qa_product *q3_base=qa_catalog_find(c,"q3-baseq3");
    const char *q3_data=q3_base?c->products[q3_base->id-1].installed_directory:NULL;
    if (!q3_data) {
        q3_base=qa_catalog_find(c,"q3-demota");
        q3_data=q3_base?c->products[q3_base->id-1].installed_directory:NULL;
    }
    if (q3_data) installed_q3=catalog_native_parent(c,q3_data,error);
    if (!installed_q3 && error && error->code==QA_ERROR_MEMORY) return false;
    if (!installed_q3 && !catalog_path(c,c->root,"q3a",&installed_q3,error)) return false;
    if (installed_q3) {
        if (!mount_file(c,NULL,installed_q3,QA_ARCHIVE_AUTO,false,error)) return false;
        for (size_t i=0;i<c->physical_count;++i)
            if (c->physical[i].view.format==QA_ARCHIVE_AUTO && !strcmp(c->physical[i].view.path,installed_q3) &&
                !c->physical[i].view.writable) c->q3_install_mount=c->physical[i].view.id;
    }
    if (c->user) {
        const char *family;
        if (!catalog_path(c, c->user, "q3a", &family, error) || !family ||
            !mount_file(c, NULL, family, QA_ARCHIVE_AUTO, true, error)) return false;
        for (size_t i = 0; i < c->physical_count; ++i)
            if (!strcmp(c->physical[i].view.path, family)) c->q3_download_mount = c->physical[i].view.id;
        static const char *q2_families[2] = {"q2", "q2/rerelease"};
        for (size_t edition = 0; edition < 2; ++edition) {
            if (!catalog_path(c, c->user, q2_families[edition], &family, error) || !family ||
                !mount_file(c, NULL, family, QA_ARCHIVE_AUTO, true, error)) return false;
            for (size_t i = 0; i < c->physical_count; ++i)
                if (!strcmp(c->physical[i].view.path, family)) c->q2_download_mount[edition] = c->physical[i].view.id;
        }
    }
    return true;
}

typedef struct ranked_map { qa_catalog_map view; const char *folded; size_t order; } ranked_map;
typedef struct map_index { ranked_map *maps; size_t count, capacity, order; } map_index;

static bool add_map(qa_catalog *c, map_index *index, const char *path,
                    qa_mount_id mount, size_t member, bool archived, qa_error *error)
{
    if (strlen(path) < 5 || tolower((unsigned char)path[0]) != 'm' || tolower((unsigned char)path[1]) != 'a' ||
        tolower((unsigned char)path[2]) != 'p' || tolower((unsigned char)path[3]) != 's' || path[4] != '/' || !catalog_suffix(path, ".bsp")) return true;
    size_t length = strlen(path);
    char *folded = malloc(length + 1);
    if (!folded) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot index map name"); return false; }
    for (size_t i = 0; i <= length; ++i) folded[i] = path[i] >= 'A' && path[i] <= 'Z' ? (char)(path[i] + ('a' - 'A')) : path[i];
    const char *key = catalog_string(c, folded, error); free(folded);
    const char *name = catalog_string(c, path, error);
    if (!key || !name || !catalog_grow((void **)&index->maps, &index->capacity, index->count + 1, sizeof(*index->maps), error)) return false;
    index->maps[index->count++] = (ranked_map){{name, mount, member, archived}, key, index->order++};
    return true;
}

static bool loose_maps(qa_catalog *c, map_index *index, const char *root,
                       const char *relative, qa_mount_id mount, qa_error *error)
{
    const char *path = join(c, root, relative, error);
    if (!path) return false;
    directory dir = {0};
    if (!read_directory(c, path, &dir, error)) { free(dir.entries); return false; }
    bool ok = true;
    for (size_t i = 0; i < dir.count; ++i) {
        directory_entry *e = &dir.entries[i];
        const char *name = join(c, relative, e->name, error);
        if (!name || (e->directory && !loose_maps(c, index, root, name, mount, error)) ||
            (e->regular && !add_map(c, index, name, mount, 0, false, error))) { ok = false; break; }
    }
    free(dir.entries); return ok;
}

static int map_order(const void *a, const void *b)
{
    const ranked_map *x = a, *y = b;
    int result = strcmp(x->folded, y->folded);
    return result ? result : x->order < y->order ? -1 : x->order > y->order;
}

bool catalog_has_path(qa_catalog *c, const catalog_product *p, const char *path,
                       bool own, bool *found, qa_error *error)
{
    const qa_mount_id *mounts = own ? p->own_mounts : p->mounts;
    size_t count = own ? p->own_count : p->mount_count;
    *found = false;
    for (size_t i = 0; i < count; ++i) {
        const catalog_physical *physical = catalog_package(c, mounts[i]);
        if (physical->view.format == QA_ARCHIVE_AUTO) {
            const char *file;
            if (!catalog_path(c, physical->view.path, path, &file, error)) return false;
            bool regular;
            if (!regular_file(file, &regular, error)) return false;
            if (regular) { *found = true; return true; }
        } else {
            const qa_archive *archive = qa_vfs_archive(c->mounts, physical->view.id);
            size_t start = 0;
            for (;;) {
                const qa_archive_entry *entry = NULL;
                if (!qa_archive_find_normalized(archive, path, QA_ARCHIVE_ASCII_INSENSITIVE,
                    start, &entry, error)) return false;
                if (!entry) break;
                if (!entry->is_directory) { *found = true; return true; }
                start = entry->ordinal + 1;
            }
        }
    }
    return true;
}

static bool program_metadata(qa_catalog *c, catalog_product *p, qa_error *error)
{
    const char *paths[10]; size_t count = 0;
    if (p->view.family == QA_GAME_Q1) paths[count++] = p->view.edition == QA_EDITION_QUAKEWORLD ? "qwprogs.dat" : "progs.dat";
    else if (p->view.family == QA_GAME_Q3) paths[count++] = "vm/qagame.qvm";
    else {
        paths[count++] = p->view.edition == QA_EDITION_RERELEASE ? "game_x64.dll" : "gamex86.dll";
        paths[count++] = "game.so"; paths[count++] = "gamex86_64.so"; paths[count++] = "gamei386.so";
        paths[count++] = "game_aarch64.so"; paths[count++] = "game_x86_64.so";
    }
    for (size_t i = 0; i < count; ++i) {
        bool found;
        if (!catalog_has_path(c, p, paths[i], true, &found, error)) return false;
        if (!found) continue;
        p->view.program = catalog_string(c, paths[i], error);
        if (!p->view.builtin)
            p->view.program_kind = p->view.family == QA_GAME_Q1 ? QA_PROGRAM_QUAKEC : p->view.family == QA_GAME_Q3 ? QA_PROGRAM_QVM : QA_PROGRAM_NATIVE;
        p->view.program_product = p->view.id;
        return p->view.program != NULL;
    }
    if (p->view.builtin) return true;
    const qa_product *base = qa_catalog_product(c, p->view.base);
    if (base) {
        p->view.program = base->program; p->view.program_kind = base->program_kind;
        p->view.program_product = base->program_product;
    }
    return true;
}

bool catalog_index_maps(qa_catalog *c, catalog_product *p, bool archives_only, qa_error *error)
{
    map_index index = {0}; bool ok = false;
    for (size_t i = 0; i < p->mount_count; ++i) {
        const qa_catalog_mount *mount = catalog_mount(c, p->mounts[i]);
        const catalog_physical *physical = catalog_package(c, mount->id);
        if (mount->format != QA_ARCHIVE_AUTO) {
            size_t count = physical->member_count;
            for (size_t j = 0; j < count; ++j) {
                size_t ordinal = mount->format == QA_ARCHIVE_PAK ? j : count - j - 1;
                const catalog_member *entry = &physical->members[ordinal];
                if (!add_map(c, &index, entry->path, mount->id, entry->ordinal, true, error)) goto done;
            }
        } else if (!archives_only) {
            const char *maps;
            if (!catalog_path(c, mount->path, "maps", &maps, error)) goto done;
            if (maps) {
                const char *leaf = strrchr(maps, '/');
                if (!loose_maps(c, &index, mount->path, leaf ? leaf + 1 : maps, mount->id, error)) goto done;
            }
        }
    }
    if (index.count > 1) qsort(index.maps, index.count, sizeof(*index.maps), map_order);
    p->maps = malloc((index.count ? index.count : 1) * sizeof(*p->maps));
    if (!p->maps) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain installed maps"); goto done; }
    for (size_t i = 0; i < index.count; ++i)
        if (!i || strcmp(index.maps[i].folded, index.maps[i - 1].folded)) p->maps[p->map_count++] = index.maps[i].view;
    ok = true;
done:
    free(index.maps); return ok;
}

bool catalog_index_product(qa_catalog *c, catalog_product *p, qa_error *error)
{
    if (!catalog_index_maps(c, p, false, error)) return false;
    bool ok = false;
    if (p->witness && !p->remote_directory) {
        bool found;
        if (!catalog_has_path(c, p, p->witness, true, &found, error)) goto done;
        if (!found && !catalog_requirement(c, p, p->witness, error)) goto done;
    }
    const qa_product *base = qa_catalog_product(c, p->view.base);
    if (base && base->availability != QA_CONTENT_INSTALLED && !catalog_requirement(c, p, base->key, error)) goto done;
    if (!program_metadata(c, p, error)) goto done;
    if (!p->view.builtin && managed_addon(p->view.campaign)) {
        for (size_t i = 0; i < p->own_count; ++i) {
            const qa_catalog_mount *mount = catalog_mount(c, p->own_mounts[i]);
            if (mount->format != QA_ARCHIVE_AUTO) continue;
            const char *path;
            if (!catalog_path(c, mount->path, ".quaddicted.json", &path, error)) goto done;
            if (!path) continue;
            qa_buffer bytes = {0}; qa_json_document *doc = NULL;
            if (!qa_file_read_all(path, &bytes, error)) goto done;
            if (!qa_json_parse((qa_bytes){bytes.data, bytes.size}, &doc, error)) { qa_buffer_free(&bytes); goto done; }
            qa_json_id title = qa_json_get(doc, qa_json_root(doc), "title");
            if (qa_json_type(doc, title) == QA_JSON_STRING) p->view.title = catalog_json_string(c, doc, title, NULL, error);
            qa_json_destroy(doc); qa_buffer_free(&bytes);
            if (!p->view.title) goto done;
            break;
        }
    }
    if (!p->view.builtin && p->view.family == QA_GAME_Q3) {
        qa_resource *description = NULL;
        for (size_t i = 0; i < p->own_count; ++i) {
            if (catalog_mount(c, p->own_mounts[i])->format != QA_ARCHIVE_AUTO) continue;
            qa_vfs *view;
            if (!catalog_view(c, &p->own_mounts[i], 1, &view, error)) goto done;
            qa_error issue = {0};
            bool acquired = qa_vfs_acquire(view, "description.txt", &description, NULL, &issue);
            qa_vfs_destroy(view);
            if (acquired) break;
            if (issue.code != QA_ERROR_NOT_FOUND) { if (error) *error = issue; goto done; }
        }
        if (description) {
            qa_bytes bytes = qa_resource_bytes(description); char title[97]; size_t used = 0;
            size_t length = bytes.size < 48 ? bytes.size : 48;
            for (size_t i = 0; i < length && bytes.data[i]; ++i) {
                unsigned char byte = bytes.data[i];
                if (byte >= 128) { title[used++] = (char)(0xc0 | (byte >> 6)); title[used++] = (char)(0x80 | (byte & 63)); }
                else title[used++] = (char)byte;
            }
            title[used] = 0;
            if (used) p->view.title = catalog_string(c, title, error);
            qa_resource_release(description);
            if (!p->view.title) goto done;
        }
    }
    ok = true;
done:
    return ok;
}
