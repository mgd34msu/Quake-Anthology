#include "internal.h"

#define SESSION_HEADER_BYTES 64u
#define SESSION_COMPONENT_BYTES 140u
#define SESSION_EXECUTION_BYTES 16u
#define SESSION_PROVIDER_BYTES 16u
#define SESSION_THINK_BYTES 40u

static void write_frame(qa_net_writer *w, const qa_source_frame *v)
{
    qa_net_write_u32(w, v->provider); qa_net_write_u32(w, v->kind); qa_net_write_u32(w, v->phase);
    qa_net_write_u64(w, v->number); qa_net_write_u64(w, v->start_ns);
    qa_net_write_u64(w, v->elapsed_ns); qa_net_write_u64(w, v->time_ns);
}
static void read_frame(qa_net_reader *r, qa_source_frame *v)
{
    v->provider = qa_net_read_u32(r); v->kind = (qa_clock_kind)qa_net_read_u32(r);
    v->phase = (qa_frame_phase)qa_net_read_u32(r); v->number = qa_net_read_u64(r);
    v->start_ns = qa_net_read_u64(r); v->elapsed_ns = qa_net_read_u64(r); v->time_ns = qa_net_read_u64(r);
}
static bool array_size(size_t *size, size_t count, size_t stride, qa_error *error)
{
    if (count > UINT32_MAX || count > (SIZE_MAX - *size) / stride)
        return persistence_fail(error, QA_ERROR_MEMORY, "Session checkpoint codec size overflow");
    *size += count * stride;
    return true;
}

bool qa_save_session_encode(const qa_session_checkpoint *v, qa_buffer *out, qa_error *error)
{
    if (!v || !out || (v->component_count && !v->components) ||
        (v->execution_count && !v->executions) || (v->scheduler.provider_count && !v->scheduler.providers) ||
        (v->scheduler.think_count && !v->scheduler.thinks))
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid session checkpoint encoding");
    size_t size = SESSION_HEADER_BYTES;
    if (!array_size(&size, v->component_count, SESSION_COMPONENT_BYTES, error) ||
        !array_size(&size, v->execution_count, SESSION_EXECUTION_BYTES, error) ||
        !array_size(&size, v->scheduler.provider_count, SESSION_PROVIDER_BYTES, error) ||
        !array_size(&size, v->scheduler.think_count, SESSION_THINK_BYTES, error)) return false;
    qa_buffer buffer = {malloc(size), size};
    if (!buffer.data) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating session checkpoint codec");
    qa_net_writer w;
    qa_net_writer_init(&w, buffer.data, size, error);
    qa_net_write_data(&w, "QASS", 4); qa_net_write_u32(&w, 1);
    qa_net_write_u32(&w, (uint32_t)v->component_count); qa_net_write_u32(&w, (uint32_t)v->execution_count);
    qa_net_write_u32(&w, (uint32_t)v->scheduler.provider_count); qa_net_write_u32(&w, (uint32_t)v->scheduler.think_count);
    qa_net_write_u32(&w, v->actor_capacity); qa_net_write_u32(&w, v->component_capacity);
    qa_net_write_u64(&w, v->elapsed_ns); qa_net_write_u64(&w, v->next_order); qa_net_write_u64(&w, v->scheduler.next_order);
    qa_net_write_u32(&w, (v->mixed_order ? 1u : 0) | (v->scheduler.mixed_order ? 2u : 0)); qa_net_write_u32(&w, 0);
    for (size_t i = 0; i < v->component_count; ++i) {
        const qa_session_component_checkpoint *c = v->components + i;
        qa_net_write_u32(&w, c->owner); qa_net_write_u32(&w, c->config.kind);
        qa_net_write_u64(&w, c->config.initial_time_ns); qa_net_write_u64(&w, c->config.interval_ns);
        qa_net_write_u64(&w, c->config.minimum_frame_ns); qa_net_write_u64(&w, c->config.maximum_frame_ns);
        qa_net_write_u64(&w, c->config.initial_lead_ns); qa_net_write_u32(&w, c->config.maximum_steps);
        qa_net_write_u64(&w, c->state.host_origin_ns); qa_net_write_u64(&w, c->state.elapsed_ns);
        qa_net_write_u64(&w, c->state.debt_ns); qa_net_write_u64(&w, c->state.frame_number);
        write_frame(&w, &c->state.frame); qa_net_write_u32(&w, c->state.paused ? 1 : 0);
        qa_net_write_u64(&w, c->order);
    }
    for (size_t i = 0; i < v->execution_count; ++i) {
        qa_net_write_u64(&w, v->executions[i].actor.generation); qa_net_write_u32(&w, v->executions[i].actor.slot);
        qa_net_write_u32(&w, v->executions[i].provider);
    }
    for (size_t i = 0; i < v->scheduler.provider_count; ++i) {
        const qa_scheduler_provider_checkpoint *p = v->scheduler.providers + i;
        qa_net_write_u32(&w, p->owner); qa_net_write_u32(&w, p->kind); qa_net_write_u64(&w, p->order);
    }
    for (size_t i = 0; i < v->scheduler.think_count; ++i) {
        const qa_scheduler_think_checkpoint *t = v->scheduler.thinks + i;
        qa_net_write_u64(&w, t->actor.generation); qa_net_write_u32(&w, t->actor.slot);
        qa_net_write_u32(&w, t->execution_provider); qa_net_write_u32(&w, t->callback_id);
        qa_net_write_u64(&w, t->due_ns); qa_net_write_u64(&w, t->sequence); qa_net_write_u32(&w, t->boundary);
    }
    if (w.failed || qa_net_writer_size(&w) != size) {
        qa_buffer_free(&buffer);
        return persistence_fail(error, QA_ERROR_FORMAT, "Session checkpoint size disagrees with codec");
    }
    *out = buffer;
    return true;
}

bool qa_save_session_decode(qa_bytes bytes, qa_session_checkpoint *out, qa_error *error)
{
    if (!out || !bytes.data || bytes.size < SESSION_HEADER_BYTES || memcmp(bytes.data, "QASS", 4))
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid session checkpoint signature");
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, error); r.bit = 32;
    uint32_t version = qa_net_read_u32(&r);
    qa_session_checkpoint v = {0};
    v.component_count = qa_net_read_u32(&r); v.execution_count = qa_net_read_u32(&r);
    v.scheduler.provider_count = qa_net_read_u32(&r); v.scheduler.think_count = qa_net_read_u32(&r);
    v.actor_capacity = qa_net_read_u32(&r); v.component_capacity = qa_net_read_u32(&r);
    v.elapsed_ns = qa_net_read_u64(&r); v.next_order = qa_net_read_u64(&r); v.scheduler.next_order = qa_net_read_u64(&r);
    uint32_t flags = qa_net_read_u32(&r), reserved = qa_net_read_u32(&r);
    v.mixed_order = (flags & 1) != 0; v.scheduler.mixed_order = (flags & 2) != 0;
    size_t expected = SESSION_HEADER_BYTES;
    if (version != 1 || reserved || (flags & ~3u) || !v.actor_capacity || !v.component_capacity ||
        v.component_count > v.component_capacity || v.execution_count > v.actor_capacity ||
        v.scheduler.provider_count != v.component_count || v.scheduler.think_count > v.execution_count ||
        v.scheduler.next_order != v.next_order || v.scheduler.mixed_order != v.mixed_order ||
        !array_size(&expected, v.component_count, SESSION_COMPONENT_BYTES, error) ||
        !array_size(&expected, v.execution_count, SESSION_EXECUTION_BYTES, error) ||
        !array_size(&expected, v.scheduler.provider_count, SESSION_PROVIDER_BYTES, error) ||
        !array_size(&expected, v.scheduler.think_count, SESSION_THINK_BYTES, error) || expected != bytes.size)
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid session checkpoint extent/flags");
    v.components = v.component_count ? calloc(v.component_count, sizeof(*v.components)) : NULL;
    v.executions = v.execution_count ? calloc(v.execution_count, sizeof(*v.executions)) : NULL;
    v.scheduler.providers = v.scheduler.provider_count ? calloc(v.scheduler.provider_count, sizeof(*v.scheduler.providers)) : NULL;
    v.scheduler.thinks = v.scheduler.think_count ? calloc(v.scheduler.think_count, sizeof(*v.scheduler.thinks)) : NULL;
    if ((v.component_count && !v.components) || (v.execution_count && !v.executions) ||
        (v.scheduler.provider_count && !v.scheduler.providers) || (v.scheduler.think_count && !v.scheduler.thinks)) {
        qa_session_checkpoint_free(&v);
        return persistence_fail(error, QA_ERROR_MEMORY, "Allocating decoded session checkpoint");
    }
    for (size_t i = 0; i < v.component_count && !r.failed; ++i) {
        qa_session_component_checkpoint *c = v.components + i;
        c->owner = qa_net_read_u32(&r); c->config.kind = (qa_clock_kind)qa_net_read_u32(&r);
        c->config.initial_time_ns = qa_net_read_u64(&r); c->config.interval_ns = qa_net_read_u64(&r);
        c->config.minimum_frame_ns = qa_net_read_u64(&r); c->config.maximum_frame_ns = qa_net_read_u64(&r);
        c->config.initial_lead_ns = qa_net_read_u64(&r); c->config.maximum_steps = qa_net_read_u32(&r);
        c->state.host_origin_ns = qa_net_read_u64(&r); c->state.elapsed_ns = qa_net_read_u64(&r);
        c->state.debt_ns = qa_net_read_u64(&r); c->state.frame_number = qa_net_read_u64(&r);
        read_frame(&r, &c->state.frame);
        uint32_t paused = qa_net_read_u32(&r); c->state.paused = paused != 0;
        if (paused > 1) qa_net_reader_fail(&r, "Invalid saved clock pause value");
        c->order = qa_net_read_u64(&r);
    }
    for (size_t i = 0; i < v.execution_count && !r.failed; ++i) {
        v.executions[i].actor.generation = qa_net_read_u64(&r); v.executions[i].actor.slot = qa_net_read_u32(&r);
        v.executions[i].provider = qa_net_read_u32(&r);
    }
    for (size_t i = 0; i < v.scheduler.provider_count && !r.failed; ++i) {
        qa_scheduler_provider_checkpoint *p = v.scheduler.providers + i;
        p->owner = qa_net_read_u32(&r); p->kind = (qa_clock_kind)qa_net_read_u32(&r); p->order = qa_net_read_u64(&r);
    }
    for (size_t i = 0; i < v.scheduler.think_count && !r.failed; ++i) {
        qa_scheduler_think_checkpoint *t = v.scheduler.thinks + i;
        t->actor.generation = qa_net_read_u64(&r); t->actor.slot = qa_net_read_u32(&r);
        t->execution_provider = qa_net_read_u32(&r); t->callback_id = qa_net_read_u32(&r);
        t->due_ns = qa_net_read_u64(&r); t->sequence = qa_net_read_u64(&r); t->boundary = (qa_think_boundary)qa_net_read_u32(&r);
    }
    if (!qa_net_reader_finish(&r)) { qa_session_checkpoint_free(&v); return false; }
    *out = v;
    return true;
}
