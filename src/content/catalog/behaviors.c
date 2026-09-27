#include "internal.h"
#include <stdio.h>

size_t qa_catalog_weapon_behavior_count(const qa_catalog *c) { return c ? c->behavior_count : 0; }
const qa_catalog_weapon_behavior *qa_catalog_weapon_behavior_at(const qa_catalog *c, size_t i)
{ return c && i < c->behavior_count ? &c->behaviors[i] : NULL; }
const qa_catalog_weapon_behavior *qa_catalog_weapon_behavior_find(const qa_catalog *c, qa_product_id product, const char *id)
{
    if (c && id) for (size_t i = 0; i < c->behavior_count; ++i)
        if (c->behaviors[i].product == product && !strcmp(c->behaviors[i].id, id)) return &c->behaviors[i];
    return NULL;
}
static bool optional(qa_vfs *view, const char *path, qa_resource **out, qa_error *error)
{
    qa_error reason = {0};
    if (qa_vfs_acquire(view, path, out, NULL, &reason)) return true;
    if (reason.code == QA_ERROR_NOT_FOUND) { *out = NULL; return true; }
    if (error) *error = reason;
    return false;
}
static bool artifact(qa_catalog *c, qa_vfs *view, const qa_json_document *doc,
                      qa_json_id row, const char *default_path,
                      qa_catalog_weapon_behavior *behavior, qa_error *error)
{
    const char *path = catalog_json_string(c, doc, qa_json_get(doc, row, "artifactPath"), default_path, error);
    const char *digest_text = catalog_json_string(c, doc, qa_json_get(doc, row, "artifactDigest"), NULL, error);
    qa_sha256_digest digest;
    if (!path || !digest_text || !qa_sha256_parse(digest_text, &digest, error)) return false;
    char *normalized = qa_archive_normalize_path(path, error);
    if (!normalized) return false;
    behavior->artifact_path = catalog_string(c, normalized, error); free(normalized);
    qa_resource *program;
    if (!behavior->artifact_path || !qa_vfs_acquire(view, behavior->artifact_path, &program, NULL, error)) return false;
    behavior->artifact_digest = *qa_resource_digest(program);
    qa_resource_release(program);
    if (!qa_sha256_equal(&digest, &behavior->artifact_digest)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "trajectory executable differs from its declaration"); return false;
    }
    return true;
}
static bool read_document(qa_catalog *c, catalog_product *p, qa_vfs *view,
                           const char *path, qa_program_kind runtime, bool *found, qa_error *error)
{
    qa_resource *resource;
    if (!optional(view, path, &resource, error)) return false;
    *found = resource != NULL;
    if (!resource) return true;
    qa_json_document *doc = NULL; bool ok = false;
    if (!qa_json_parse(qa_resource_bytes(resource), &doc, error)) goto done;
    qa_json_id root = qa_json_root(doc), entries = qa_json_get(doc, root, runtime == QA_PROGRAM_QUAKEC ? "behaviors" : "profiles");
    uint64_t version;
    if (!qa_json_u64(doc, qa_json_get(doc, root, "version"), &version, error)) goto done;
    if (version != 1 || qa_json_type(doc, entries) != QA_JSON_ARRAY) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid trajectory declaration document"); goto done;
    }
    const char *default_path = p->view.program;
    if (runtime == QA_PROGRAM_QUAKEC) {
        default_path = catalog_json_string(c, doc, qa_json_get(doc, root, "artifactPath"),
            default_path ? default_path : "progs.dat", error);
        if (!default_path) goto done;
    }
    static const char *roles[] = {"rocket", "grenade", "nail", "bolt", "plasma", "energy", "grapple"};
    for (size_t i = 0; i < qa_json_size(doc, entries); ++i) {
        qa_json_id row = qa_json_at(doc, entries, i);
        const char *id = catalog_json_string(c, doc, qa_json_get(doc, row, "id"), NULL, error);
        const char *title = catalog_json_string(c, doc, qa_json_get(doc, row, "title"), NULL, error);
        if (!id || !*id || !title) goto done;
        if (runtime == QA_PROGRAM_QVM) {
            size_t bytes = strlen(id) + 5;
            char *name = malloc(bytes);
            if (!name) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain trajectory identity"); goto done; }
            snprintf(name, bytes, "qvm:%s", id); id = catalog_string(c, name, error); free(name);
            if (!id) goto done;
        }
        if (!strchr(id, ':') || qa_catalog_weapon_behavior_find(c, p->view.id, id)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid or repeated trajectory identity: %s", id); goto done;
        }
        size_t role = 0;
        while (role < sizeof(roles) / sizeof(roles[0]) && !qa_json_string_equal(doc, qa_json_get(doc, row, "role"), roles[role])) ++role;
        if (role == sizeof(roles) / sizeof(roles[0]) || !qa_json_string_equal(doc, qa_json_get(doc, row, "aspect"), "trajectory")) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "unknown trajectory role or aspect"); goto done;
        }
        if (!catalog_grow((void **)&c->behaviors, &c->behavior_capacity, c->behavior_count + 1, sizeof(*c->behaviors), error)) goto done;
        qa_catalog_weapon_behavior *b = &c->behaviors[c->behavior_count++];
        qa_bytes entry = qa_json_source(doc, row);
        uint8_t *copy = malloc(entry.size ? entry.size : 1);
        if (!copy) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain trajectory declaration metadata"); --c->behavior_count; goto done; }
        memcpy(copy, entry.data, entry.size);
        *b = (qa_catalog_weapon_behavior){.product = p->view.id, .id = id, .title = title,
            .runtime = runtime, .role = (qa_builtin_projectile_role)role,
            .declaration_path = path, .declaration_digest = *qa_resource_digest(resource),
            .entry = {copy, entry.size}};
        qa_error issue = {0};
        if (!artifact(c, view, doc, row, default_path, b, &issue)) {
            if (issue.code == QA_ERROR_MEMORY) { if (error) *error = issue; goto done; }
            b->unavailable = catalog_string(c, issue.message, error);
            if (!b->unavailable) goto done;
        }
    }
    ok = true;
done:
    qa_json_destroy(doc); qa_resource_release(resource); return ok;
}

bool catalog_read_behaviors(qa_catalog *c, catalog_product *p, qa_error *error)
{
    if (p->view.availability != QA_CONTENT_INSTALLED) return true;
    static const char *documents[] = {"weapon-behaviors.json", "qvm-weapon-behaviors.json", "native-weapon-behaviors.json"};
    bool declared = false;
    for (size_t i = 0; i < sizeof(documents) / sizeof(documents[0]); ++i) {
        bool found;
        if (!catalog_has_path(c, p, documents[i], false, &found, error)) return false;
        declared |= found;
    }
    bool implicit = !p->view.builtin && p->view.family == QA_GAME_Q2 && p->view.edition == QA_EDITION_RERELEASE;
    if (!declared && !implicit) return true;
    qa_vfs *view;
    if (!qa_catalog_open(c, p->view.id, &view, error)) return false;
    bool qc, qvm, native;
    bool ok = read_document(c, p, view, "weapon-behaviors.json", QA_PROGRAM_QUAKEC, &qc, error) &&
        read_document(c, p, view, "qvm-weapon-behaviors.json", QA_PROGRAM_QVM, &qvm, error) &&
        read_document(c, p, view, "native-weapon-behaviors.json", QA_PROGRAM_NATIVE, &native, error);
    if (ok && !native && !p->view.builtin && p->view.family == QA_GAME_Q2 && p->view.edition == QA_EDITION_RERELEASE) {
        qa_resource *program;
        ok = optional(view, "game_x64.dll", &program, error);
        if (ok && program) {
            qa_sha256_digest known;
            ok = qa_sha256_parse("b60b79f7fb6f115218681a9cbab8765267e34f72466975526df05ad288925dde", &known, error);
            if (ok && qa_sha256_equal(&known, qa_resource_digest(program))) {
                ok = catalog_grow((void **)&c->behaviors, &c->behavior_capacity, c->behavior_count + 1, sizeof(*c->behaviors), error);
                if (ok) {
                    c->behaviors[c->behavior_count++] = (qa_catalog_weapon_behavior){.product = p->view.id,
                        .id = "native:rocket-trajectory", .title = "Faster rockets", .artifact_path = "game_x64.dll",
                        .runtime = QA_PROGRAM_NATIVE, .role = QA_BUILTIN_ROCKET, .artifact_digest = *qa_resource_digest(program)};
                }
            }
            qa_resource_release(program);
        }
    }
    qa_vfs_destroy(view); return ok;
}
