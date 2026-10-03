#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "native_cpu_codec.h"
#include "qa/binary.h"
#include <stdlib.h>
#include <string.h>

typedef struct hardware_codec {
    qa_bytes input;
    qa_buffer output;
    size_t cursor, capacity;
    bool reading;
    qa_error *error;
} hardware_codec;

static bool fail(hardware_codec *io, qa_status code, const char *message)
{ qa_error_set(io->error, code, io->cursor, "%s", message); return false; }

static bool span(hardware_codec *io, void *value, size_t bytes)
{
    if (io->reading) {
        if (bytes > io->input.size - io->cursor)
            return fail(io, QA_ERROR_FORMAT, "hardware architectural checkpoint is truncated");
        if (bytes) memcpy(value, io->input.data + io->cursor, bytes);
        io->cursor += bytes; return true;
    }
    if (bytes > SIZE_MAX - io->output.size)
        return fail(io, QA_ERROR_MEMORY, "hardware architectural checkpoint overflows");
    size_t needed = io->output.size + bytes;
    if (needed > io->capacity) {
        size_t capacity = io->capacity ? io->capacity : 1024;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
            capacity *= 2;
        }
        void *grown = realloc(io->output.data, capacity);
        if (!grown) return fail(io, QA_ERROR_MEMORY, "owning hardware architectural checkpoint");
        io->output.data = grown; io->capacity = capacity;
    }
    if (bytes) memcpy(io->output.data + io->output.size, value, bytes);
    io->output.size = needed; io->cursor = needed; return true;
}

static bool scalar(hardware_codec *io, uint8_t *field, size_t bytes)
{
    uint8_t wire[8];
    if (!io->reading) {
        if (bytes == 8) qa_store_u64le(wire, qa_load_u64le(field));
        else if (bytes == 4) qa_store_u32le(wire, qa_load_u32le(field));
        else if (bytes == 2) qa_store_u16le(wire, qa_load_u16le(field));
        else wire[0] = field[0];
    }
    if (!span(io, wire, bytes)) return false;
    if (io->reading) memcpy(field, wire, bytes);
    return true;
}

static bool integer(hardware_codec *io, uint64_t *value)
{
    uint8_t field[8];
    if (!io->reading) qa_store_u64le(field, *value);
    if (!scalar(io, field, 8)) return false;
    if (io->reading) *value = qa_load_u64le(field);
    return true;
}

static bool words(hardware_codec *io, uint8_t *fields, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (!scalar(io, fields + i * 8, 8)) return false;
    return true;
}

/* Linux v6.18 arch/x86/include/asm/fpu/types.h and the architectural standard
 * XSAVE layouts: extended rows name registers, excluding reserved padding. */
static bool component(hardware_codec *io, unsigned bit,
    guest_host_x86_64_component *layout, size_t *wire_bytes)
{
    if (!guest_host_x86_64_component_read(bit, layout, io->error)) return false;
    size_t required = 0, payload = 0;
    switch (bit) {
    case 2: required = payload = 16 * 16; break; /* YMM0..15 upper128 */
    case 3: required = payload = 4 * 16; break; /* BND0..3 lower/upper */
    case 4: required = 64; payload = 16; break; /* BNDCFGU/BNDSTATUS */
    case 5: required = payload = 8 * 8; break; /* k0..7 */
    case 6: required = payload = 16 * 32; break; /* ZMM0..15 upper256 */
    case 7: required = payload = 16 * 64; break; /* ZMM16..31 */
    case 9: required = 8; payload = 4; break; /* PKRU */
    case 17: required = 64; payload = 2 + 8 * 2 + 8; break; /* TILECFG */
    case 18: /* Each architectural tile is sixteen 64-byte rows. */
        required = payload = layout->bytes;
        if (!required || required % 1024 || required > 8 * 1024)
            return fail(io, QA_ERROR_UNSUPPORTED, "hardware tile register layout is unsupported");
        break;
    case 19: required = payload = 16 * 8; break; /* R16..31 */
    default: return fail(io, QA_ERROR_UNSUPPORTED, "enabled hardware component has no architectural cold codec");
    }
    if (layout->supervisor || layout->offset < 576 || layout->bytes != required)
        return fail(io, QA_ERROR_UNSUPPORTED, "hardware component differs from its actual user architectural layout");
    *wire_bytes = payload; return true;
}

static bool component_fields(hardware_codec *io, unsigned bit, uint8_t *data, size_t bytes)
{
    if (bit == 9) return scalar(io, data, 4);
    if (bit == 17) {
        if (!scalar(io, data, 1) || !scalar(io, data + 1, 1)) return false;
        for (size_t i = 0; i < 8; ++i)
            if (!scalar(io, data + 16 + i * 2, 2)) return false;
        for (size_t i = 0; i < 8; ++i)
            if (!scalar(io, data + 48 + i, 1)) return false;
        return true;
    }
    return words(io, data, bytes / 8);
}

static bool legacy(hardware_codec *io, uint8_t *data)
{
    if (!scalar(io, data, 2) || !scalar(io, data + 2, 2) ||
        !scalar(io, data + 4, 1) || !scalar(io, data + 6, 2) ||
        !scalar(io, data + 8, 8) || !scalar(io, data + 16, 8) ||
        !scalar(io, data + 24, 4)) return false;
    /* ST0..7 register80 images retain TOP and the physical abridged tag word.
     * Each register's six unused transfer bytes never enter the cold schema. */
    for (size_t i = 0; i < 8; ++i)
        if (!scalar(io, data + 32 + i * 16, 8) ||
            !scalar(io, data + 40 + i * 16, 2)) return false;
    return words(io, data + 160, 32); /* XMM0..15 */
}

static bool encode(hardware_codec *io, guest_host_x86_64_state *state,
    const guest_host_x86_64_capabilities *capability)
{
    uint8_t magic[4] = {'Q','A','H','C'};
    uint64_t active = io->reading ? 0 : qa_load_u64le(state->xsave.data + 512);
    if (!span(io, magic, sizeof(magic)) || memcmp(magic, "QAHC", sizeof(magic)) ||
        !integer(io, &state->xfeatures) || !integer(io, &active))
        return fail(io, QA_ERROR_FORMAT, "hardware architectural checkpoint header is invalid");
    if ((state->xfeatures & 3) != 3 || (active & ~state->xfeatures) ||
        (state->xfeatures & ~capability->xfeatures))
        return fail(io, QA_ERROR_UNSUPPORTED, "saved hardware features differ from the actual destination CPU");
    /* Qualify every enabled feature, including architectural init-state rows.
     * Their inactive bytes are not registers and are not checkpoint payload. */
    guest_host_x86_64_component layouts[64] = {{0}};
    size_t payloads[64] = {0};
    for (unsigned bit = 2; bit < 64; ++bit) if ((state->xfeatures >> bit) & 1) {
        if (!component(io, bit, layouts + bit, payloads + bit) ||
            layouts[bit].offset > capability->xsave_bytes ||
            layouts[bit].bytes > capability->xsave_bytes - layouts[bit].offset)
            return fail(io, QA_ERROR_UNSUPPORTED, "saved component has no complete actual destination transfer");
    }
    for (size_t i = 0; i < 16; ++i) if (!integer(io, state->registers + i)) return false;
    if (!integer(io, &state->instruction) || !integer(io, &state->flags) ||
        !integer(io, &state->fs_base) || !integer(io, &state->gs_base)) return false;
    for (size_t i = 0; i < 6; ++i) {
        uint8_t selector[2]; if (!io->reading) qa_store_u16le(selector, state->selectors[i]);
        if (!scalar(io, selector, 2)) return false;
        if (io->reading) state->selectors[i] = qa_load_u16le(selector);
    }
    if (!legacy(io, state->xsave.data)) return false;
    for (unsigned bit = 2; bit < 64; ++bit) if ((active >> bit) & 1) {
        uint64_t saved_bit = bit, saved_bytes = payloads[bit];
        if (!integer(io, &saved_bit) || !integer(io, &saved_bytes) ||
            saved_bit != bit || saved_bytes != payloads[bit])
            return fail(io, QA_ERROR_FORMAT, "hardware architectural component row differs from its named schema");
        if (layouts[bit].offset > state->xsave.size ||
            layouts[bit].bytes > state->xsave.size - layouts[bit].offset)
            return fail(io, QA_ERROR_FORMAT, "active hardware component has no retained register storage");
        if (!component_fields(io, bit, state->xsave.data + layouts[bit].offset, payloads[bit])) return false;
    }
    if (io->reading) qa_store_u64le(state->xsave.data + 512, active);
    return true;
}

bool guest_native_cpu_checkpoint(const guest_host_x86_64_state *state,
    const guest_host_x86_64_capabilities *capability, qa_buffer *out, qa_error *error)
{
    hardware_codec io = {.error = error};
    if (!out || out->data || out->size || !capability)
        return fail(&io, QA_ERROR_ARGUMENT, "hardware capture requires empty output and actual capabilities");
    if (!guest_host_x86_64_state_valid(state, capability, error)) return false;
    guest_host_x86_64_state owned = {0};
    if (!guest_host_x86_64_state_copy(state, &owned, error)) return false;
    bool okay = encode(&io, &owned, capability);
    guest_host_x86_64_state_free(&owned);
    if (okay) *out = io.output;
    else qa_buffer_free(&io.output);
    return okay;
}

bool guest_native_cpu_restore(qa_bytes bytes,
    const guest_host_x86_64_capabilities *capability, guest_host_x86_64_state *out, qa_error *error)
{
    hardware_codec io = {.input = bytes, .reading = true, .error = error};
#if defined(__linux__) && defined(__x86_64__)
    if (!out || out->xsave.data || out->xsave.size || !bytes.data || !capability ||
        capability->xsave_bytes < 576)
        return fail(&io, QA_ERROR_ARGUMENT, "hardware cold restore requires empty state and genuine destination capabilities");
    guest_host_x86_64_state owned = {0}; void *transfer = NULL;
    if (posix_memalign(&transfer, 64, capability->xsave_bytes) != 0)
        return fail(&io, QA_ERROR_MEMORY, "owning architectural hardware transfer");
    memset(transfer, 0, capability->xsave_bytes);
    owned.xsave = (qa_buffer){transfer, capability->xsave_bytes};
    qa_store_u32le(owned.xsave.data + 28, capability->mxcsr_mask);
    bool okay = encode(&io, &owned, capability) && io.cursor == bytes.size &&
        guest_host_x86_64_state_valid(&owned, capability, error);
    if (okay) *out = owned;
    else { guest_host_x86_64_state_free(&owned); if (error && error->code == QA_OK)
        fail(&io, QA_ERROR_FORMAT, "hardware architectural checkpoint has trailing or invalid state"); }
    return okay;
#else
    (void)capability; (void)out;
    return fail(&io, QA_ERROR_UNSUPPORTED, "hardware cold restore requires its actual Linux x86-64 transfer profile");
#endif
}
