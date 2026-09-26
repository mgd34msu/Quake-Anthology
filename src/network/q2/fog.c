#include "qa/network_q2_messages.h"
#include <math.h>

bool qa_q2_fog_read(qa_net_reader *r, qa_q2_fog *out) {
    if (!out) return qa_net_reader_fail(r, "Missing Q2 fog output");
    qa_q2_fog f = {0};
    f.bits = qa_net_read_u8(r);
    if (f.bits & 128u) f.bits |= (uint16_t)((uint16_t)qa_net_read_u8(r) << 8);
    if (f.bits & 1u) { f.density = qa_net_read_f32(r); f.sky_factor = qa_net_read_u8(r); }
    for (unsigned i = 0; i < 3; ++i)
        if (f.bits & (2u << i)) f.color[i] = qa_net_read_u8(r);
    if (f.bits & 16u) f.time = qa_net_read_u16(r);
    if (f.bits & 32u) f.height_falloff = qa_net_read_f32(r);
    if (f.bits & 64u) f.height_density = qa_net_read_f32(r);
    for (unsigned i = 0; i < 3; ++i)
        if (f.bits & (256u << i)) f.height_start_color[i] = qa_net_read_u8(r);
    if (f.bits & 2048u) f.height_start_distance = qa_net_read_i32(r);
    for (unsigned i = 0; i < 3; ++i)
        if (f.bits & (4096u << i)) f.height_end_color[i] = qa_net_read_u8(r);
    if (f.bits & 32768u) f.height_end_distance = qa_net_read_i32(r);
    if (r->failed) return false;
    if (!isfinite(f.density) || !isfinite(f.height_falloff) || !isfinite(f.height_density))
        return qa_net_reader_fail(r, "Nonfinite Q2 fog value");
    *out = f;
    return true;
}

bool qa_q2_fog_write(qa_net_writer *w, const qa_q2_fog *f) {
    if (!f || ((f->bits & 1u) && !isfinite(f->density)) ||
        ((f->bits & 32u) && !isfinite(f->height_falloff)) ||
        ((f->bits & 64u) && !isfinite(f->height_density)))
        return qa_net_writer_fail(w, "Invalid Q2 fog value");
    uint16_t bits = f->bits;
    if (bits & 0xff00u) bits |= 128u;
    qa_net_write_u8(w, (uint8_t)bits);
    if (bits & 128u) qa_net_write_u8(w, (uint8_t)(bits >> 8));
    if (bits & 1u) { qa_net_write_f32(w, f->density); qa_net_write_u8(w, f->sky_factor); }
    for (unsigned i = 0; i < 3; ++i)
        if (bits & (2u << i)) qa_net_write_u8(w, f->color[i]);
    if (bits & 16u) qa_net_write_u16(w, f->time);
    if (bits & 32u) qa_net_write_f32(w, f->height_falloff);
    if (bits & 64u) qa_net_write_f32(w, f->height_density);
    for (unsigned i = 0; i < 3; ++i)
        if (bits & (256u << i)) qa_net_write_u8(w, f->height_start_color[i]);
    if (bits & 2048u) qa_net_write_i32(w, f->height_start_distance);
    for (unsigned i = 0; i < 3; ++i)
        if (bits & (4096u << i)) qa_net_write_u8(w, f->height_end_color[i]);
    if (bits & 32768u) qa_net_write_i32(w, f->height_end_distance);
    return !w->failed;
}
