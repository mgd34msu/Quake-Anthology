#include "qa/persistence_gameplay.h"
#include "../campaign/targets_internal.h"
#include "lease_serials.h"
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, const char *text)
{ qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", text); return false; }

bool qa_persistence_targets_binding(const qa_targets *targets, qa_actor_id actor, qa_target_binding *out)
{
    if (!targets || !out || actor.slot >= targets->capacity ||
        !qa_actors_get(qa_session_actors(targets->options.session), actor) ||
        !qa_actor_id_equal(targets->bindings[actor.slot].actor, actor)) return false;
    *out = targets->bindings[actor.slot]; return true;
}

static uint32_t callbacks(const qa_target_binding *binding)
{
    return (binding->read ? 1u : 0u) | (binding->use ? 2u : 0u) | (binding->field ? 4u : 0u) |
        (binding->set_targetname ? 8u : 0u) | (binding->set_target ? 16u : 0u) |
        (binding->set_delay ? 32u : 0u) | (binding->remap_shader ? 64u : 0u);
}

static bool fields(qa_source_save_io *io, qa_authored_target *target)
{
    return qa_source_save_string(io, &target->classname) && qa_source_save_string(io, &target->targetname) &&
        qa_source_save_string(io, &target->target) && qa_source_save_string(io, &target->killtarget) &&
        qa_source_save_string(io, &target->message) && qa_source_save_string(io, &target->shader_old) &&
        qa_source_save_string(io, &target->shader_new) && qa_source_save_f32(io, &target->delay_seconds) &&
        isfinite(target->delay_seconds) && qa_source_save_f32(io, &target->wait_seconds) && isfinite(target->wait_seconds);
}

static bool same_fields(const qa_authored_target *a, const qa_authored_target *b)
{
    return a->classname == b->classname && a->targetname == b->targetname && a->target == b->target &&
        a->killtarget == b->killtarget && a->message == b->message && a->shader_old == b->shader_old &&
        a->shader_new == b->shader_new && a->delay_seconds == b->delay_seconds && a->wait_seconds == b->wait_seconds;
}

static bool signature(qa_source_save_io *io)
{
    unsigned char actual[8] = {'Q','A','T','A','R','G','E','T'};
    static const unsigned char expected[8] = {'Q','A','T','A','R','G','E','T'};
    uint32_t version = 2;
    return qa_source_save_bytes(io, actual, sizeof(actual)) && !memcmp(actual, expected, sizeof(actual)) &&
        qa_source_save_u32(io, &version) && version == 2;
}

static bool record(qa_source_save_io *io, qa_target_binding *binding, uint64_t *serial,
                   const qa_persistence_gameplay_resolvers *resolve)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t source = binding->source, mask = callbacks(binding);
    qa_authored_target value = {0};
    if (!reading && (!binding->read || !binding->read(binding->context, binding->actor, &value)))
        return fail(io->error, "Target owner cannot read its authored fields");
    if (!qa_source_save_actor(io, &binding->actor) ||
        !qa_actors_get(qa_session_actors(io->session), binding->actor) ||
        !qa_source_save_u64(io, serial) || !*serial || !qa_source_save_u32(io, &source) || source > QA_CLOCK_Q3 ||
        !qa_source_save_u32(io, &mask) || !(mask & 1u) || (mask & ~127u) || !fields(io, &value)) return false;
    binding->source = (qa_clock_kind)source;
    if (reading) {
        qa_target_binding restored = {0}; qa_authored_target observed = {0};
        if (!resolve || !resolve->target ||
            !resolve->target(resolve->context, binding->actor, binding->source, &restored, io->error) ||
            !qa_actor_id_equal(restored.actor, binding->actor) || restored.source != binding->source ||
            callbacks(&restored) != mask || !restored.read(restored.context, restored.actor, &observed) ||
            !same_fields(&observed, &value))
            return fail(io->error, "Restored target source differs from saved authority");
        *binding = restored;
    }
    return true;
}

bool qa_persistence_targets_capture(qa_targets *targets, qa_buffer *out, qa_error *error)
{
    if (!targets || !out || targets->depth || !qa_session_safe(targets->options.session))
        return fail(error, "Target capture requires idle source callbacks");
    qa_source_save_io io = {0};
    if (!qa_source_save_writer(&io, targets->options.session, error)) return false;
    size_t count = 0;
    for (size_t i = 0; i < targets->capacity; ++i) if (targets->bindings[i].actor.registry) ++count;
    uint64_t next = targets->next_binding_serial;
    bool ok = signature(&io) && qa_source_save_u64(&io, &next) && qa_source_save_count(&io, &count, targets->capacity);
    for (size_t i = 0; ok && i < targets->capacity; ++i) if (targets->bindings[i].actor.registry) {
        qa_target_binding copy = targets->bindings[i]; uint64_t serial = targets->binding_serial[i];
        ok = record(&io, &copy, &serial, NULL);
    }
    if (ok) ok = !targets->depth && next == targets->next_binding_serial && qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) fail(error, "Invalid authored target continuation");
    qa_source_save_dispose(&io); return ok;
}

bool qa_persistence_targets_restore(qa_targets *targets, const qa_persistence_gameplay_resolvers *resolve,
                                     qa_bytes bytes, qa_error *error)
{
    if (!targets || targets->depth || !qa_session_safe(targets->options.session))
        return fail(error, "Target restore requires an idle candidate");
    qa_targets *scratch = qa_targets_create(&targets->options, error);
    if (!scratch) return false;
    qa_source_save_io io = {0}; size_t count = 0;
    uint64_t *serials = NULL; size_t serial_count = 0, serial_capacity = 0;
    bool ok = qa_source_save_reader(&io, targets->options.session, bytes, error) && signature(&io) &&
        qa_source_save_u64(&io, &scratch->next_binding_serial) && qa_source_save_count(&io, &count, targets->capacity);
    uint32_t previous = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        qa_target_binding binding = {0}; uint64_t serial = 0;
        ok = record(&io, &binding, &serial, resolve);
        if (ok && ((i && binding.actor.slot <= previous) || serial > scratch->next_binding_serial))
            ok = fail(error, "Saved target ordering or serial namespace is invalid");
        if (ok) ok = persistence_serial_append(&serials, &serial_count, &serial_capacity, serial, scratch->next_binding_serial, error);
        if (ok) {
            previous = binding.actor.slot; scratch->bindings[binding.actor.slot] = binding;
            scratch->binding_serial[binding.actor.slot] = serial;
        }
    }
    if (ok) ok = persistence_serial_unique(serials, serial_count, error) && qa_source_save_finish(&io, NULL) && !targets->depth;
    if (ok) {
        qa_target_binding *old = targets->bindings; uint64_t *old_serial = targets->binding_serial;
        targets->bindings = scratch->bindings; targets->binding_serial = scratch->binding_serial;
        scratch->bindings = old; scratch->binding_serial = old_serial;
        targets->next_binding_serial = scratch->next_binding_serial; qa_targets_changed(targets);
    }
    if (!ok && (!error || error->code == QA_OK)) fail(error, "Invalid saved target continuation");
    free(serials); qa_source_save_dispose(&io); qa_targets_destroy(scratch); return ok;
}
