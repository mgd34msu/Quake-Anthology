#include "internal.h"
#include <stdio.h>
#include <ctype.h>

static bool optional_resource(qa_vfs *view, const char *path, qa_resource **out, qa_error *error)
{
    qa_error reason = {0};
    if (qa_vfs_acquire(view, path, out, NULL, &reason)) return true;
    if (reason.code == QA_ERROR_NOT_FOUND) { *out = NULL; return true; }
    if (error) *error = reason;
    return false;
}

static bool strings(qa_catalog *c, const qa_json_document *doc, qa_json_id array,
                     const char *const **out, size_t *count, qa_error *error)
{
    if (qa_json_type(doc, array) != QA_JSON_ARRAY) { qa_error_set(error, QA_ERROR_FORMAT, 0, "component dependencies must be arrays"); return false; }
    size_t length = qa_json_size(doc, array);
    const char **items = calloc(length ? length : 1, sizeof(*items));
    if (!items) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain component dependencies"); return false; }
    for (size_t i = 0; i < length; ++i) {
        items[i] = catalog_json_string(c, doc, qa_json_at(doc, array, i), NULL, error);
        if (!items[i] || !qa_catalog_mod_key(items[i])) {
            bool decoded = items[i] != NULL;
            free(items);
            if (decoded) qa_error_set(error, QA_ERROR_FORMAT, 0, "component dependency must be PRODUCT/COMPONENT_ID");
            return false;
        }
    }
    *out = items; *count = length; return true;
}

static const char *resource_path(qa_catalog *c, const qa_json_document *doc,
                                  qa_json_id field, qa_error *error)
{
    const char *text = catalog_json_string(c, doc, field, NULL, error);
    if (!text) return NULL;
    char *path = qa_archive_normalize_path(text, error);
    if (!path) return NULL;
    const char *result = catalog_string(c, path, error);
    free(path); return result;
}

static bool component(qa_catalog *c, qa_vfs *view, const qa_json_document *doc,
                       qa_json_id row, qa_catalog_mod *mod, qa_error *error)
{
    mod->declaration_path = resource_path(c, doc, qa_json_get(doc, row, "callbacks"), error);
    if (!mod->declaration_path ||
        !strings(c, doc, qa_json_get(doc, row, "requires"), &mod->requires, &mod->requires_count, error) ||
        !strings(c, doc, qa_json_get(doc, row, "conflicts"), &mod->conflicts, &mod->conflicts_count, error)) return false;
    qa_resource *declaration;
    if (!qa_vfs_acquire(view, mod->declaration_path, &declaration, NULL, error)) return false;
    mod->declaration_digest = *qa_resource_digest(declaration);
    qa_bytes bytes = qa_resource_bytes(declaration);
    uint8_t *copy = malloc(bytes.size ? bytes.size : 1);
    if (!copy) { qa_resource_release(declaration); qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain component declaration metadata"); return false; }
    memcpy(copy, bytes.data, bytes.size);
    mod->declaration = (qa_bytes){copy, bytes.size};
    qa_json_document *decl;
    qa_resource_release(declaration);
    if (!qa_json_parse(mod->declaration, &decl, error)) return false;
    bool ok = false;
    qa_json_id root = qa_json_root(decl), runtime = qa_json_get(decl, root, "runtime");
    if (qa_json_string_equal(decl, runtime, "quakec")) mod->runtime = QA_PROGRAM_QUAKEC;
    else if (qa_json_string_equal(decl, runtime, "qvm")) mod->runtime = QA_PROGRAM_QVM;
    else if (qa_json_string_equal(decl, runtime, "native")) mod->runtime = QA_PROGRAM_NATIVE;
    else { qa_error_set(error, QA_ERROR_FORMAT, 0, "unknown component execution runtime"); goto done; }
    qa_json_id program = qa_json_get(decl, root, "program");
    mod->program_path = resource_path(c, decl, qa_json_get(decl, program, "path"), error);
    const char *digest_text = catalog_json_string(c, decl, qa_json_get(decl, program, "digest"), NULL, error);
    qa_sha256_digest expected;
    if (!mod->program_path || !digest_text || !qa_sha256_parse(digest_text, &expected, error)) goto done;
    qa_resource *artifact;
    if (!qa_vfs_acquire(view, mod->program_path, &artifact, NULL, error)) goto done;
    mod->program_digest = *qa_resource_digest(artifact);
    qa_resource_release(artifact);
    if (!qa_sha256_equal(&expected, &mod->program_digest)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "component executable differs from its callback declaration"); goto done;
    }
    ok = true;
done:
    qa_json_destroy(decl); return ok;
}

bool catalog_read_mods(qa_catalog *c, catalog_product *p, qa_error *error)
{
    if (p->view.availability != QA_CONTENT_INSTALLED) return true;
    bool present;
    if (!catalog_has_path(c, p, "gameplay-mods.json", true, &present, error)) return false;
    if (!present) return true;
    qa_vfs *view;
    if (!catalog_view(c, p->own_mounts, p->own_count, &view, error)) return false;
    qa_resource *resource = NULL;
    qa_error reason = {0};
    if (!qa_vfs_acquire(view, "gameplay-mods.json", &resource, NULL, &reason)) {
        qa_vfs_destroy(view);
        if (reason.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = reason;
        return false;
    }
    qa_vfs_destroy(view);
    if (!qa_catalog_open(c, p->view.id, &view, error)) { qa_resource_release(resource); return false; }
    qa_json_document *doc = NULL;
    bool ok = false;
    if (!qa_json_parse(qa_resource_bytes(resource), &doc, error)) goto done;
    qa_json_id root = qa_json_root(doc), components = qa_json_get(doc, root, "components");
    uint64_t version;
    if (!qa_json_u64(doc, qa_json_get(doc, root, "version"), &version, error)) goto done;
    if (version != 1 || qa_json_type(doc, components) != QA_JSON_ARRAY) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid gameplay-mods.json version or component array"); goto done;
    }
    for (size_t i = 0; i < qa_json_size(doc, components); ++i) {
        qa_json_id row = qa_json_at(doc, components, i);
        const char *id = catalog_json_string(c, doc, qa_json_get(doc, row, "id"), NULL, error);
        const char *title = catalog_json_string(c, doc, qa_json_get(doc, row, "title"), NULL, error);
        qa_json_id purpose = qa_json_get(doc, row, "purpose");
        bool addition = qa_json_string_equal(doc, purpose, "addition"), game_type = qa_json_string_equal(doc, purpose, "game-type");
        if (!id || !title) goto done;
        if (!addition && !game_type) { qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid component purpose"); goto done; }
        size_t bytes = strlen(p->view.key) + strlen(id) + 2;
        char *key = malloc(bytes);
        if (!key) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate component identity"); goto done; }
        snprintf(key, bytes, "%s/%s", p->view.key, id);
        if (!qa_catalog_mod_key(key) || qa_catalog_mod_find(c, key)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid or repeated mod component: %s", key); free(key); goto done;
        }
        const char *retained_key = catalog_string(c, key, error); free(key);
        if (!retained_key || !catalog_grow((void **)&c->mods, &c->mod_capacity, c->mod_count + 1, sizeof(*c->mods), error)) goto done;
        qa_catalog_mod *mod = &c->mods[c->mod_count++];
        *mod = (qa_catalog_mod){.product = p->view.id, .key = retained_key, .id = id,
            .title = title, .purpose = addition ? QA_MOD_ADDITION : QA_MOD_GAME_TYPE};
        qa_error issue = {0};
        if (!component(c, view, doc, row, mod, &issue)) {
            if (issue.code == QA_ERROR_MEMORY) { if (error) *error = issue; goto done; }
            mod->unavailable = catalog_string(c, issue.message, error);
            if (!mod->unavailable) goto done;
        }
    }
    ok = true;
done:
    qa_json_destroy(doc); qa_resource_release(resource); qa_vfs_destroy(view); return ok;
}

static bool optional_bool(const qa_json_document *doc, qa_json_id row, const char *name,
                           bool *out, qa_error *error)
{
    qa_json_id id = qa_json_get(doc, row, name);
    if (id == QA_JSON_NONE) { *out = false; return true; }
    return qa_json_bool(doc, id, out, error);
}

bool catalog_read_starts(qa_catalog *c, catalog_product *p, qa_error *error)
{
    if (p->view.family != QA_GAME_Q2 || p->view.edition != QA_EDITION_RERELEASE ||
        p->view.availability != QA_CONTENT_INSTALLED) return true;
    bool present;
    if (!catalog_has_path(c, p, "mapdb.json", false, &present, error)) return false;
    if (!present) return true;
    qa_vfs *view;
    if (!qa_catalog_open(c, p->view.id, &view, error)) return false;
    qa_resource *resource;
    if (!optional_resource(view, "mapdb.json", &resource, error)) { qa_vfs_destroy(view); return false; }
    qa_vfs_destroy(view);
    if (!resource) return true;
    qa_json_document *doc = NULL; bool ok = false;
    if (!qa_json_parse(qa_resource_bytes(resource), &doc, error)) goto done;
    qa_json_id root = qa_json_root(doc), episodes = qa_json_get(doc, root, "episodes"), maps = qa_json_get(doc, root, "maps");
    if ((episodes != QA_JSON_NONE && qa_json_type(doc, episodes) != QA_JSON_ARRAY) ||
        (maps != QA_JSON_NONE && qa_json_type(doc, maps) != QA_JSON_ARRAY)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid mapdb episode or map list"); goto done;
    }
    bool ctf = !strcmp(p->view.campaign, "ctf");
    const char *episode_id = ctf ? "baseq2" : p->view.campaign;
    for (size_t i = 0; i < qa_json_size(doc, episodes); ++i) {
        qa_json_id row = qa_json_at(doc, episodes, i);
        if (!qa_json_string_equal(doc, qa_json_get(doc, row, "id"), episode_id)) continue;
        p->episode.id = catalog_string(c, episode_id, error);
        p->episode.command = catalog_json_string(c, doc, qa_json_get(doc, row, "command"), "", error);
        p->episode.name = catalog_json_string(c, doc, qa_json_get(doc, row, "name"), "", error);
        p->episode.activity = catalog_json_string(c, doc, qa_json_get(doc, row, "activity"), "", error);
        if (!p->episode.id || !p->episode.command || !p->episode.name || !p->episode.activity ||
            !optional_bool(doc, row, "needsSkillSelect", &p->episode.needs_skill_select, error)) goto done;
        p->has_episode = true; break;
    }
    size_t count = qa_json_size(doc, maps);
    p->starts = calloc(count ? count : 1, sizeof(*p->starts));
    if (!p->starts) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain authored start maps"); goto done; }
    for (size_t i = 0; i < count; ++i) {
        qa_json_id row = qa_json_at(doc, maps, i);
        if (!qa_json_string_equal(doc, qa_json_get(doc, row, "episode"), episode_id)) continue;
        qa_catalog_start start = {0};
        if (!optional_bool(doc, row, "sp", &start.singleplayer, error) ||
            !optional_bool(doc, row, "coop", &start.cooperative, error) ||
            !optional_bool(doc, row, "ctf", &start.capture_the_flag, error)) goto done;
        if (!(ctf ? start.capture_the_flag : start.singleplayer)) continue;
        start.episode = catalog_string(c, episode_id, error);
        start.bsp = catalog_json_string(c, doc, qa_json_get(doc, row, "bsp"), "", error);
        start.title = catalog_json_string(c, doc, qa_json_get(doc, row, "title"), "", error);
        start.start_items = catalog_json_string(c, doc, qa_json_get(doc, row, "start_items"), "", error);
        if (!start.episode || !start.bsp || !start.title || !start.start_items) goto done;
        const char *name = strrchr(start.bsp, '+'); name = name ? name + 1 : start.bsp;
        if (*name == '*') ++name;
        bool valid = *name != 0;
        for (const char *it = name; *it; ++it) if (isspace((unsigned char)*it) || strchr("+*$;", *it)) valid = false;
        if (!valid) { qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid authored start BSP: %s", start.bsp); goto done; }
        size_t bytes = strlen(name) + 10;
        char *path = malloc(bytes);
        if (!path) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate start map path"); goto done; }
        snprintf(path, bytes, "maps/%s.bsp", name);
        char *normalized = qa_archive_normalize_path(path, error); free(path);
        if (!normalized) goto done;
        start.path = catalog_string(c, normalized, error); free(normalized);
        if (!start.path) goto done;
        p->starts[p->start_count++] = start;
    }
    ok = true;
done:
    qa_json_destroy(doc); qa_resource_release(resource); return ok;
}
