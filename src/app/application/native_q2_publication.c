#include "native_q2_publication.h"
#include "guest_native_q2_private.h"
#include "native_q2_callbacks.h"
#include "unified_output_json.h"
#include "qa/application_native_q2_presentation.h"

struct application_native_q2_publication {
    struct application_native_q2 *engine;
    qa_launch_instance_lease *lease;
    const qa_catalog_mod *metadata;
    qa_unified_component_identity identity;
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
        p->metadata && p->identity.module.id &&
        qa_sha256_equal(qa_resource_digest(held->artifact), &p->metadata->program_digest) &&
        qa_sha256_equal(qa_resource_digest(held->declaration), &p->metadata->declaration_digest);
}

static bool identity(application_native_q2_publication *p,qa_error *e)
{
    const qa_launch_instance *d=qa_launch_instance_lease_view(p->lease);
    const qa_product *product=qa_catalog_product(qa_launch_instance_catalog(d),p->metadata->product);
    if (!application_unified_component_identity_create(p->metadata,product,d->selection.instance,&p->identity,e)) return false;
    return true;
}

static bool namespace_text(const application_native_q2_publication *p, uint64_t generation,
    application_unified_json *j, qa_error *e)
{
    const qa_launch_instance *d = qa_launch_instance_lease_view(p->lease);
    char digest[65], suffix[40]; qa_sha256_hex(&p->metadata->declaration_digest, digest);
    snprintf(suffix, sizeof(suffix), ":%llu", (unsigned long long)generation);
    return application_unified_json_text(j, "native-component:", e) && application_unified_json_percent_encoded(j, d->selection.instance, e) &&
        application_unified_json_text(j, ":", e) && application_unified_json_percent_encoded(j, p->metadata->key, e) &&
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
    for (uint64_t generation = 1;; ++generation) {
        application_unified_json name = {0};
        ok = namespace_text(p, generation, &name, e);
        if (ok && !qa_strings_find(strings, (qa_bytes){name.bytes.data, name.bytes.size})) {
            ok = qa_strings_intern(strings, (qa_bytes){name.bytes.data, name.bytes.size}, &p->owner, e);
            if (ok) p->activation_generation = generation;
        }
        application_unified_json_dispose(&name);
        if (!ok || p->owner) return ok;
        if (generation == UINT64_MAX) break;
    }
    return application_fail(e, QA_ERROR_FORMAT, "Native component registration generations are exhausted");
}

bool application_native_q2_publication_activate(application_native_q2_publication *p, qa_error *e)
{
    if (!p) return true;
    if (!storage(p) || !p->owner || !p->activation_generation || !p->engine->initialized ||
        !p->engine->map_ready || !p->engine->provider->state.native.host || p->engine->shutting_down ||
        (p->engine->callbacks && !application_native_q2_callbacks_current(p->engine->callbacks)))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native component activation lacks its completed GAME initialization");
    p->active = true;
    return true;
}
bool application_native_q2_publication_retire(application_native_q2_publication *p, qa_error *e)
{
    if (!p || !p->active) return true;
    if (p->generation == UINT64_MAX)
        return application_fail(e, QA_ERROR_FORMAT, "Native component presentation generation is exhausted");
    ++p->generation; p->active = false;
    return true;
}
void application_native_q2_publication_destroy(application_native_q2_publication **out)
{
    if (!out || !*out) return;
    application_native_q2_publication *p = *out;
    qa_unified_component_identity_dispose(&p->identity); qa_launch_instance_lease_release(p->lease); free(p); *out = NULL;
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
    if (!storage(p) || !p->active || !p->owner ||
        (n->callbacks && !application_native_q2_callbacks_current(n->callbacks)) ||
        v->owner != source->owner || cut.source_owner != source->owner ||
        cut.launch != v->launch || !qa_application_native_q2_presentation_current(app, &cut))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native component lost its registered physical GAME activation");
    *out = (application_native_q2_publication_view){.registration = p,
        .descriptor = qa_launch_instance_lease_view(p->lease), .metadata = p->metadata, .identity = &p->identity,
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

bool application_native_q2_publication_checkpoint_read(qa_application *app, const application_unified_source *source,
    application_native_q2_publication_view *out, bool *found, qa_error *e)
{
    if (!out || !found || !(application_unified_source_current(app, source) ||
        application_unified_source_checkpoint_current(app, source)))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native component checkpoint requires its actual imported Source");
    application_provider *v = application_world_provider(app, QA_ROLE_ENTITIES, "");
    struct application_native_q2 *n = v && v->kind == APPLICATION_PROVIDER_NATIVE ? v->state.native.q2_engine : NULL;
    application_native_q2_publication *p = n ? n->publication : NULL;
    *found = false;
    if (!p || (!p->camera && p->hud == APPLICATION_NATIVE_Q2_HUD_NONE)) return true;
    if (v->owner != source->owner || n->provider != v || !n->initialized || !n->map_ready ||
        n->calls || n->shutting_down || !storage(p) || !p->active || !p->owner || !p->activation_generation)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native checkpoint lost its imported component registration");
    *out = (application_native_q2_publication_view){.registration = p,
        .descriptor = qa_launch_instance_lease_view(p->lease), .metadata = p->metadata, .identity = &p->identity,
        .owner = p->owner, .source_owner = v->owner, .activation_generation = p->activation_generation,
        .generation = p->generation, .hud = p->hud, .camera = p->camera};
    *found = true;
    return true;
}

bool application_native_q2_publication_capture(struct application_native_q2 *n, qa_buffer *out, qa_error *e)
{
    application_native_q2_publication *p = n ? n->publication : NULL;
    if (!n || !out || (p && (!storage(p) || !p->owner || !p->activation_generation ||
        p->active != n->map_ready)))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native publication capture lost its registration");
    const char *name = p ? qa_strings_cstr(qa_session_strings(n->provider->application->session), p->owner) : "";
    if (!name || (p && !*name))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native publication lost its actual namespace string");
    size_t length = name ? strlen(name) : 0;
    if (length > 65535) return application_fail(e, QA_ERROR_FORMAT, "Native registration namespace is too long");
    qa_buffer bytes = {.data = calloc(1, 60 + length), .size = 60 + length};
    if (!bytes.data) return application_fail(e, QA_ERROR_MEMORY, "Retaining native publication continuation");
    memcpy(bytes.data, "NQ2P", 4); bytes.data[4] = p != NULL;
    bytes.data[5] = p && p->active;
    if (p) {
        qa_store_u64le(bytes.data + 8, p->activation_generation); qa_store_u64le(bytes.data + 16, p->generation);
        memcpy(bytes.data + 24, qa_launch_instance_lease_view(p->lease)->identity.bytes, 32);
    }
    qa_store_u32le(bytes.data + 56, (uint32_t)length); if (length) memcpy(bytes.data + 60, name, length);
    *out = bytes;
    return true;
}
bool application_native_q2_publication_restore_prepare(struct application_native_q2 *n, qa_bytes bytes, bool map_ready,
    application_native_q2_publication_restore **out, qa_error *e)
{
    if (!n || !out || *out || !bytes.data || bytes.size < 60 || memcmp(bytes.data, "NQ2P", 4) ||
        bytes.data[4] > 1 || bytes.data[5] > 1 ||
        bytes.data[6] || bytes.data[7] || qa_load_u32le(bytes.data + 56) != bytes.size - 60 ||
        bytes.size - 60 > 65535 || memchr(bytes.data + 60, 0, bytes.size - 60) ||
        (bytes.data[4] != 0) != (n->publication != NULL))
        return application_fail(e, QA_ERROR_FORMAT, "Native publication continuation differs from its declared owner");
    application_native_q2_publication *p = n->publication;
    if (p && (bytes.data[5] != 0) != map_ready)
        return application_fail(e, QA_ERROR_FORMAT, "Native publication activation differs from its saved GAME lifecycle");
    uint64_t activation = qa_load_u64le(bytes.data + 8), generation = qa_load_u64le(bytes.data + 16);
    qa_actor_owner namespace = 0;
    if (p) {
        application_unified_json expected = {0};
        bool ok = activation && storage(p) &&
            !memcmp(bytes.data + 24, qa_launch_instance_lease_view(p->lease)->identity.bytes, 32) &&
            namespace_text(p, activation, &expected, e) && expected.bytes.size == bytes.size - 60 &&
            !memcmp(expected.bytes.data, bytes.data + 60, expected.bytes.size);
        if (ok) namespace = qa_strings_find(qa_session_strings(n->provider->application->session),
            (qa_bytes){bytes.data + 60, bytes.size - 60});
        application_unified_json_dispose(&expected);
        if (!ok || !namespace) return application_fail(e, QA_ERROR_FORMAT, "Native publication lost its saved registration namespace");
    } else {
        for (size_t i = 5; i < bytes.size; ++i)
            if (bytes.data[i]) return application_fail(e, QA_ERROR_FORMAT, "Absent native publication contains state");
    }
    application_native_q2_publication_restore *r = calloc(1, sizeof(*r));
    if (!r) return application_fail(e, QA_ERROR_MEMORY, "Preparing native publication continuation");
    *r = (application_native_q2_publication_restore){.owner = p, .namespace = namespace,
        .activation_generation = activation, .generation = generation, .active = bytes.data[5] != 0};
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
