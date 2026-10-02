#include "native_q2_publication.h"
#include "guest_native_q2_private.h"
#include "unified_output_json.h"
#include "qa/application_native_q2_presentation.h"

struct application_native_q2_publication {
    struct application_native_q2 *engine;
    qa_launch_instance_lease *lease;
    const qa_catalog_mod *metadata;
    qa_unified_document *identity;
    qa_sha256_digest identity_digest;
    qa_actor_owner owner;
    uint64_t activation_generation, generation;
    application_native_q2_hud_mode hud;
    bool camera, active;
};
struct application_native_q2_publication_restore {
    application_native_q2_publication *owner;
    qa_actor_owner namespace;
    uint64_t activation_generation, generation;
    bool active;
};

static bool storage(const application_native_q2_publication *p)
{
    const struct application_native_q2 *n = p ? p->engine : NULL;
    const application_provider *v = n ? n->provider : NULL;
    const qa_launch_instance *held = p && p->lease ? qa_launch_instance_lease_view(p->lease) : NULL;
    return n && v && v->state.native.q2_engine == n && held && v->launch &&
        held->storage == v->launch->storage && n->declaration && v->state.native.module &&
        held->artifact == v->launch->artifact && held->declaration == v->launch->declaration &&
        p->metadata && p->identity &&
        qa_sha256_equal(qa_resource_digest(held->artifact), &p->metadata->program_digest) &&
        qa_sha256_equal(qa_resource_digest(held->declaration), &p->metadata->declaration_digest);
}

static bool encoded(application_unified_json *j, const char *s, qa_error *e)
{
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || strchr("-_.!~*'()", *p);
        char escape[3] = {'%', hex[*p >> 4], hex[*p & 15]};
        if (!application_unified_json_append(j, plain ? (qa_bytes){p, 1} :
                (qa_bytes){(const uint8_t *)escape, 3}, e)) return false;
    }
    return true;
}

static bool identity(application_native_q2_publication *p, qa_error *e)
{
    const qa_catalog_mod *m = p->metadata;
    const qa_launch_instance *d = qa_launch_instance_lease_view(p->lease);
    const qa_product *product = qa_catalog_product(qa_launch_instance_catalog(d), m->product);
    if (!product || !product->key || !product->identity || !m->id)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native component lost its discovered identity");
    application_unified_json module = {0}, j = {0};
    char declaration[72] = "sha256:", program[72] = "sha256:";
    qa_sha256_hex(&m->declaration_digest, declaration + 7); qa_sha256_hex(&m->program_digest, program + 7);
    bool ok = application_unified_json_text(&module, "mod:", e) && encoded(&module, product->key, e) &&
        application_unified_json_text(&module, "%2F", e) && encoded(&module, m->id, e) &&
        application_unified_json_append(&module, (qa_bytes){(const uint8_t *)"", 1}, e);
#define TEXT(s) application_unified_json_text(&j, (s), e)
#define STRING(s) application_unified_json_string(&j, (s), e)
    if (ok) ok = TEXT("{\"selection\":{\"product\":") && STRING(product->key) && TEXT(",\"id\":") && STRING(m->id) &&
        TEXT("},\"source\":{\"content\":") && STRING(product->identity) && TEXT(",\"provider\":") && STRING(d->selection.instance) &&
        TEXT("},\"declarationDigest\":") && STRING(declaration) && TEXT(",\"modules\":[{\"id\":") && STRING((char *)module.bytes.data) &&
        TEXT(",\"artifactPath\":") && STRING(m->program_path) && TEXT(",\"digest\":") && STRING(program) &&
        TEXT(",\"revision\":") && STRING(program) && TEXT("}],\"providers\":[{\"provider\":") && STRING((char *)module.bytes.data) &&
        TEXT(",\"schema\":\"native:mod\",\"version\":1}]}") &&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){j.bytes.data, j.bytes.size}, &p->identity, e);
#undef TEXT
#undef STRING
    if (ok) qa_sha256((qa_bytes){j.bytes.data, j.bytes.size}, &p->identity_digest);
    application_unified_json_dispose(&module); application_unified_json_dispose(&j);
    return ok;
}

static bool namespace_text(const application_native_q2_publication *p, uint64_t generation,
    application_unified_json *j, qa_error *e)
{
    const qa_launch_instance *d = qa_launch_instance_lease_view(p->lease);
    char digest[65], suffix[40]; qa_sha256_hex(&p->metadata->declaration_digest, digest);
    snprintf(suffix, sizeof(suffix), ":%llu", (unsigned long long)generation);
    return application_unified_json_text(j, "native-component:", e) && encoded(j, d->selection.instance, e) &&
        application_unified_json_text(j, ":", e) && encoded(j, p->metadata->key, e) &&
        application_unified_json_text(j, ":", e) && application_unified_json_text(j, digest, e) &&
        application_unified_json_text(j, suffix, e);
}

bool application_native_q2_publication_create(struct application_native_q2 *n,
    application_native_q2_publication **out, qa_error *e)
{
    if (!n || !out || *out || !n->provider || !n->provider->launch)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native publication requires its physical factory owner");
    const qa_launch_instance *d = n->provider->launch;
    if (!d->selection.component || !*d->selection.component || n->profile == QA_NATIVE_Q2_CGAME_API2023) return true;
    const qa_catalog_mod *m = qa_catalog_mod_find(qa_launch_instance_catalog(d), d->selection.component);
    qa_native_module_info module = qa_native_module_describe(n->provider->state.native.module);
    if (!m || m->unavailable || m->runtime != QA_PROGRAM_NATIVE || !n->declaration || !d->declaration ||
        m->product != d->selection.product || strcmp(m->program_path, d->selection.artifact) ||
        !qa_sha256_equal(&module.image.digest, &m->program_digest) ||
        !qa_sha256_equal(qa_native_declaration_digest(n->declaration), &m->declaration_digest))
        return application_fail(e, QA_ERROR_FORMAT, "Native publication differs from its admitted component declaration");
    application_native_q2_publication *p = calloc(1, sizeof(*p));
    if (!p) return application_fail(e, QA_ERROR_MEMORY, "Retaining native component registration");
    *out = p; p->engine = n; p->metadata = m;
    if (!qa_launch_instance_retain_metadata(d, &p->lease, e) || !identity(p, e)) return false;
    qa_json_document *json = NULL;
    if (!qa_json_parse(qa_resource_bytes(d->declaration), &json, e)) return false;
    qa_json_id presentation = qa_json_get(json, qa_json_root(json), "clientPresentation");
    bool ok = true;
    if (presentation != QA_JSON_NONE) {
        qa_json_id hud = qa_json_get(json, presentation, "hud"), view = qa_json_get(json, presentation, "view");
        if (qa_json_type(json, presentation) != QA_JSON_OBJECT) ok = false;
        else if (qa_json_string_equal(json, hud, "layout-overlay")) p->hud = APPLICATION_NATIVE_Q2_HUD_OVERLAY;
        else if (qa_json_string_equal(json, hud, "replace-status")) p->hud = APPLICATION_NATIVE_Q2_HUD_REPLACE;
        else if (!qa_json_string_equal(json, hud, "none")) ok = false;
        if (qa_json_string_equal(json, view, "playerstate")) p->camera = true;
        else if (!qa_json_string_equal(json, view, "none")) ok = false;
    }
    qa_json_destroy(json);
    if (!ok) return application_fail(e, QA_ERROR_FORMAT, "Native component has an invalid client presentation admission");
    if (n->provider->application->operation == APPLICATION_PERSISTING) return true;
    qa_strings *strings = qa_session_strings(n->provider->application->session);
    for (uint64_t generation = 1; generation <= QA_UNIFIED_SAFE_INTEGER; ++generation) {
        application_unified_json name = {0};
        ok = namespace_text(p, generation, &name, e);
        if (ok && !qa_strings_find(strings, (qa_bytes){name.bytes.data, name.bytes.size})) {
            ok = qa_strings_intern(strings, (qa_bytes){name.bytes.data, name.bytes.size}, &p->owner, e);
            if (ok) p->activation_generation = generation;
        }
        application_unified_json_dispose(&name);
        if (!ok || p->owner) return ok;
    }
    return application_fail(e, QA_ERROR_FORMAT, "Native component registration generations are exhausted");
}

bool application_native_q2_publication_activate(application_native_q2_publication *p, qa_error *e)
{
    if (!p) return true;
    if (!storage(p) || !p->owner || !p->activation_generation || !p->engine->initialized ||
        !p->engine->map_ready || !p->engine->provider->state.native.host || p->engine->shutting_down)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native component activation lacks its completed GAME initialization");
    p->active = true;
    return true;
}
bool application_native_q2_publication_retire(application_native_q2_publication *p, qa_error *e)
{
    if (!p || !p->active) return true;
    if (p->generation == QA_UNIFIED_SAFE_INTEGER)
        return application_fail(e, QA_ERROR_FORMAT, "Native component presentation generation is exhausted");
    ++p->generation; p->active = false;
    return true;
}
void application_native_q2_publication_destroy(application_native_q2_publication **out)
{
    if (!out || !*out) return;
    application_native_q2_publication *p = *out;
    qa_unified_document_destroy(p->identity); qa_launch_instance_lease_release(p->lease); free(p); *out = NULL;
}

bool application_native_q2_publication_read(qa_application *app, const application_unified_source *source,
    application_native_q2_publication_view *out, bool *found, qa_error *e)
{
    if (!out || !found || !application_unified_source_current(app, source))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native component publication requires its completed physical Source");
    *found = false;
    qa_application_native_q2_presentation cut; bool selected;
    if (!qa_application_native_q2_presentation_selected(app, &cut, &selected, e)) return false;
    if (!selected || cut.kind != QA_APPLICATION_NATIVE_Q2_ORIGINAL) return true;
    application_provider *v = application_world_provider(app, QA_ROLE_ENTITIES, "");
    struct application_native_q2 *n = v ? v->state.native.q2_engine : NULL;
    application_native_q2_publication *p = n ? n->publication : NULL;
    if (!p || (!p->camera && p->hud == APPLICATION_NATIVE_Q2_HUD_NONE)) return true;
    if (!storage(p) || !p->active || !p->owner || v->owner != source->owner || cut.source_owner != source->owner ||
        cut.launch != v->launch || !qa_application_native_q2_presentation_current(app, &cut))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native component lost its registered physical GAME activation");
    *out = (application_native_q2_publication_view){.registration = p,
        .descriptor = qa_launch_instance_lease_view(p->lease), .metadata = p->metadata, .identity = p->identity,
        .owner = p->owner, .source_owner = v->owner, .activation_generation = p->activation_generation,
        .generation = p->generation, .hud = p->hud, .camera = p->camera};
    *found = true;
    return true;
}
bool application_native_q2_publication_current(qa_application *app, const application_unified_source *source,
    const application_native_q2_publication_view *held)
{
    application_native_q2_publication_view v; bool found;
    return held && application_native_q2_publication_read(app, source, &v, &found, NULL) && found &&
        v.registration == held->registration && v.descriptor == held->descriptor && v.metadata == held->metadata &&
        v.identity == held->identity && v.owner == held->owner && v.source_owner == held->source_owner &&
        v.activation_generation == held->activation_generation && v.generation == held->generation &&
        v.hud == held->hud && v.camera == held->camera;
}

bool application_native_q2_publication_capture(struct application_native_q2 *n, qa_buffer *out, qa_error *e)
{
    application_native_q2_publication *p = n ? n->publication : NULL;
    if (!n || !out || (p && (!storage(p) || !p->owner || !p->activation_generation ||
        p->activation_generation > QA_UNIFIED_SAFE_INTEGER || p->generation > QA_UNIFIED_SAFE_INTEGER ||
        p->active != n->map_ready)))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native publication capture lost its registration");
    const char *name = p ? qa_strings_cstr(qa_session_strings(n->provider->application->session), p->owner) : "";
    if (!name || (p && !*name))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native publication lost its actual namespace string");
    size_t length = name ? strlen(name) : 0;
    if (length > 65535) return application_fail(e, QA_ERROR_FORMAT, "Native registration namespace is too long");
    qa_buffer bytes = {.data = calloc(1, 96 + length), .size = 96 + length};
    if (!bytes.data) return application_fail(e, QA_ERROR_MEMORY, "Retaining native publication continuation");
    memcpy(bytes.data, "NQ2P", 4); qa_store_u32le(bytes.data + 4, 1); bytes.data[8] = p != NULL;
    bytes.data[9] = p && p->active;
    if (p) {
        qa_store_u64le(bytes.data + 12, p->activation_generation); qa_store_u64le(bytes.data + 20, p->generation);
        memcpy(bytes.data + 28, p->identity_digest.bytes, 32);
        memcpy(bytes.data + 60, qa_launch_instance_lease_view(p->lease)->identity.bytes, 32);
    }
    qa_store_u32le(bytes.data + 92, (uint32_t)length); if (length) memcpy(bytes.data + 96, name, length);
    *out = bytes;
    return true;
}
bool application_native_q2_publication_restore_prepare(struct application_native_q2 *n, qa_bytes bytes, bool map_ready,
    application_native_q2_publication_restore **out, qa_error *e)
{
    if (!n || !out || *out || !bytes.data || bytes.size < 96 || memcmp(bytes.data, "NQ2P", 4) ||
        qa_load_u32le(bytes.data + 4) != 1 || bytes.data[8] > 1 || bytes.data[9] > 1 ||
        bytes.data[10] || bytes.data[11] || qa_load_u32le(bytes.data + 92) != bytes.size - 96 ||
        bytes.size - 96 > 65535 || memchr(bytes.data + 96, 0, bytes.size - 96) ||
        (bytes.data[8] != 0) != (n->publication != NULL))
        return application_fail(e, QA_ERROR_FORMAT, "Native publication continuation differs from its declared owner");
    application_native_q2_publication *p = n->publication;
    if (p && (bytes.data[9] != 0) != map_ready)
        return application_fail(e, QA_ERROR_FORMAT, "Native publication activation differs from its saved GAME lifecycle");
    uint64_t activation = qa_load_u64le(bytes.data + 12), generation = qa_load_u64le(bytes.data + 20);
    qa_actor_owner namespace = 0;
    if (p) {
        application_unified_json expected = {0};
        bool ok = activation && activation <= QA_UNIFIED_SAFE_INTEGER && generation <= QA_UNIFIED_SAFE_INTEGER && storage(p) &&
            !memcmp(bytes.data + 28, p->identity_digest.bytes, 32) &&
            !memcmp(bytes.data + 60, qa_launch_instance_lease_view(p->lease)->identity.bytes, 32) &&
            namespace_text(p, activation, &expected, e) && expected.bytes.size == bytes.size - 96 &&
            !memcmp(expected.bytes.data, bytes.data + 96, expected.bytes.size);
        if (ok) namespace = qa_strings_find(qa_session_strings(n->provider->application->session),
            (qa_bytes){bytes.data + 96, bytes.size - 96});
        application_unified_json_dispose(&expected);
        if (!ok || !namespace) return application_fail(e, QA_ERROR_FORMAT, "Native publication lost its saved registration namespace");
    } else {
        for (size_t i = 9; i < bytes.size; ++i)
            if (bytes.data[i]) return application_fail(e, QA_ERROR_FORMAT, "Absent native publication contains state");
    }
    application_native_q2_publication_restore *r = calloc(1, sizeof(*r));
    if (!r) return application_fail(e, QA_ERROR_MEMORY, "Preparing native publication continuation");
    *r = (application_native_q2_publication_restore){.owner = p, .namespace = namespace,
        .activation_generation = activation, .generation = generation, .active = bytes.data[9] != 0};
    *out = r;
    return true;
}
void application_native_q2_publication_restore_commit(application_native_q2_publication_restore *r)
{
    if (!r) return;
    if (r->owner) {
        r->owner->owner = r->namespace; r->owner->activation_generation = r->activation_generation;
        r->owner->generation = r->generation; r->owner->active = r->active;
    }
    free(r);
}
void application_native_q2_publication_restore_abort(application_native_q2_publication_restore *r)
{ free(r); }
