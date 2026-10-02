#include "instruction.h"

static bool x87(uint8_t op, uint8_t modrm)
{
    unsigned group = (modrm >> 3) & 7;
    if (modrm < 0xc0) {
        if (op == 0xd8 || op == 0xdc || op == 0xda || op == 0xde) return true;
        if ((op == 0xd9 || op == 0xdd) && (group == 4 || group == 6 || group == 7)) return true;
        if (op == 0xd9 && group == 5) return true;
        return group == 0 || group == 2 || group == 3 || group == 7 ||
            (group == 5 && (op == 0xdb || op == 0xdf)) ||
            (group == 1 && (op == 0xdb || op == 0xdd || op == 0xdf));
    }
    if (op == 0xd8) return true;
    if (op == 0xdc || op == 0xde) return (op == 0xde && modrm == 0xd9) || (group != 2 && group != 3);
    if (op == 0xd9) return group < 2 || modrm == 0xd0 || modrm == 0xe0 || modrm == 0xe1 ||
        modrm == 0xe4 || modrm == 0xe5 || modrm == 0xe8 || modrm == 0xeb || modrm == 0xee ||
        modrm == 0xf3 || modrm == 0xf6 || modrm == 0xf7 || modrm == 0xfa ||
        modrm == 0xfc || modrm == 0xfe || modrm == 0xff;
    if (op == 0xdd) return group == 0 || (group >= 2 && group <= 5);
    return (op == 0xda && modrm == 0xe9) || (op == 0xdb && (modrm == 0xe2 || modrm == 0xe3)) ||
        (op == 0xdf && modrm == 0xe0) || ((op == 0xdb || op == 0xdf) && (group == 5 || group == 6)) ||
        ((op == 0xda || op == 0xdb) && group <= 3);
}

static bool sse(uint8_t op, uint8_t prefix, uint8_t modrm)
{
    bool ordinary = prefix == 0 || prefix == 0x66;
    bool scalar = prefix == 0xf2 || prefix == 0xf3;
    bool memory = modrm < 0xc0;
    unsigned group = (modrm >> 3) & 7;
    if (op == 0x10 || op == 0x11) return true;
    if (op == 0x28 || op == 0x29 || op == 0x14 || op == 0x15 ||
        op == 0x2e || op == 0x2f || (op >= 0x54 && op <= 0x57) || op == 0xc6) return ordinary;
    if (op == 0x50) return ordinary && !memory;
    if (op >= 0x12 && op <= 0x17) return ordinary &&
        ((op == 0x13 || op == 0x17 || prefix == 0x66) ? memory : true);
    if (op == 0x6f || op == 0x7f) return prefix == 0x66 || prefix == 0xf3;
    if (op == 0x2a || op == 0x2c || op == 0x2d) return scalar;
    if (op == 0x5a) return true;
    if (op == 0x5b) return prefix != 0xf2;
    if (op == 0xe6 || op == 0x70) return prefix != 0;
    if (op == 0xae) return memory && (group == 2 || group == 3) && prefix == 0;
    if (op == 0x6e || op == 0x7e) return prefix == 0x66 || (op == 0x7e && prefix == 0xf3);
    if (op == 0x51 || op == 0x58 || op == 0x59 || (op >= 0x5c && op <= 0x5f) || op == 0xc2) return true;
    if (op == 0xc4 || op == 0xc5) return prefix == 0x66 && (op == 0xc4 || !memory);
    if (op >= 0x71 && op <= 0x73) return prefix == 0x66 && !memory &&
        (group == 2 || group == 6 || (group == 4 && op != 0x73) || (op == 0x73 && (group == 3 || group == 7)));
    if (prefix != 0x66) return false;
    if (op == 0xd7) return !memory;
    return (op >= 0x60 && op <= 0x62) || (op >= 0x64 && op <= 0x66) ||
        (op >= 0x68 && op <= 0x6a) || op == 0x6c || op == 0x6d ||
        (op >= 0x74 && op <= 0x76) || (op >= 0xd1 && op <= 0xd6) ||
        op == 0xdb || op == 0xdf || op == 0xe1 || op == 0xe2 || op == 0xe4 || op == 0xe5 ||
        op == 0xeb || op == 0xef || (op >= 0xf1 && op <= 0xf6) || (op >= 0xf8 && op <= 0xfe);
}

guest_profile_instruction guest_profile_x64_instruction(qa_bytes bytes)
{
    guest_profile_instruction unsupported = {GUEST_PROFILE_UNSUPPORTED, 0, false, "instruction has no source x64 feature admission"};
    guest_profile_instruction hardware = {GUEST_PROFILE_HARDWARE, 0, false, NULL};
    if (!bytes.data || !bytes.size || bytes.size > 15) return unsupported;
    size_t at = 0; uint8_t repeat = 0, prefix = 0; bool lock = false;
    while (at < bytes.size) {
        uint8_t p = bytes.data[at];
        if (p >= 0x40 && p <= 0x4f) { ++at; continue; }
        if (p == 0x66) { if (!repeat) prefix = 0x66; }
        else if (p == 0xf2 || p == 0xf3) { repeat = p; prefix = p; }
        else if (p == 0xf0) lock = true;
        else if (p != 0x67 && p != 0x64 && p != 0x65 && p != 0x2e && p != 0x36 && p != 0x3e && p != 0x26) break;
        ++at;
    }
    if (at == bytes.size) return unsupported;
    uint8_t op = bytes.data[at++];
    uint8_t modrm = at < bytes.size ? bytes.data[at] : 0;
    bool allowed = false, atomic = false;
    if (op < 0x40 && (op & 7) <= 5) { allowed = true; atomic = (op & 7) <= 1 && (op >> 3) != 7 && modrm < 0xc0; }
    else if ((op >= 0x50 && op <= 0x5f) || (op >= 0x70 && op <= 0x7f) ||
        (op >= 0x90 && op <= 0x97) || (op >= 0xb0 && op <= 0xbf)) allowed = true;
    else if (op >= 0xd8 && op <= 0xdf) allowed = at < bytes.size && x87(op, modrm);
    else if (op == 0x0f) {
        if (at == bytes.size) return unsupported;
        op = bytes.data[at++]; modrm = at < bytes.size ? bytes.data[at] : 0;
        if (op == 0x05 && bytes.size == 2 && bytes.data[0] == 0x0f)
            return (guest_profile_instruction){GUEST_PROFILE_SYSCALL, 0, false, NULL};
        if (op == 0xa2 && !lock) return (guest_profile_instruction){GUEST_PROFILE_CPUID, 0, false, NULL};
        if (op == 0x0b && !lock) return (guest_profile_instruction){GUEST_PROFILE_PROCESSOR_FAULT, 6, false, "UD2"};
        if ((op >= 0x40 && op <= 0x4f) || (op >= 0x80 && op <= 0x9f) || (op >= 0xc8 && op <= 0xcf)) allowed = true;
        else if (op == 0x1e) allowed = repeat == 0xf3 && at < bytes.size && modrm == 0xfa;
        else if (op == 0x1f || op == 0xaf || op == 0xb6 || op == 0xb7 || op == 0xbe || op == 0xbf ||
            op == 0xa4 || op == 0xa5 || op == 0xac || op == 0xad) allowed = true;
        else if (op == 0xbc || op == 0xbd) allowed = repeat != 0xf3;
        else if (op == 0xb0 || op == 0xb1 || op == 0xc0 || op == 0xc1) { allowed = true; atomic = modrm < 0xc0; }
        else if (op == 0xa3 || op == 0xab || op == 0xb3 || op == 0xbb || op == 0xba) {
            unsigned group = (modrm >> 3) & 7;
            allowed = op != 0xba || group >= 4;
            atomic = modrm < 0xc0 && op != 0xa3 && (op != 0xba || group != 4);
        } else allowed = at < bytes.size && sse(op, prefix, modrm);
    } else {
        unsigned group = (modrm >> 3) & 7;
        switch (op) {
        case 0x63: case 0x68: case 0x6a: case 0x69: case 0x6b:
        case 0x84: case 0x85: case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x8d:
        case 0x98: case 0x99: case 0x9b: case 0x9c: case 0x9d: case 0x9e: case 0x9f:
        case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa8: case 0xa9:
        case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc9: case 0xcc:
        case 0xd0: case 0xd1: case 0xd2: case 0xd3:
        case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe8: case 0xe9: case 0xeb:
        case 0xf5: case 0xf8: case 0xf9: case 0xfc: case 0xfd: allowed = true; break;
        case 0x80: case 0x81: case 0x83: allowed = true; atomic = group != 7 && modrm < 0xc0; break;
        case 0x86: case 0x87: allowed = true; atomic = modrm < 0xc0; break;
        case 0x8f: case 0xc6: case 0xc7: allowed = group == 0; break;
        case 0xf6: case 0xf7: allowed = group != 1; atomic = (group == 2 || group == 3) && modrm < 0xc0; break;
        case 0xfe: case 0xff: allowed = group < 2 || (op == 0xff && (group == 2 || group == 4 || group == 6)); atomic = group < 2 && modrm < 0xc0; break;
        case 0xf4: if (!lock) return (guest_profile_instruction){GUEST_PROFILE_PROCESSOR_FAULT, 13, false, "user-mode HLT"}; break;
        default:
            if ((op >= 0xa4 && op <= 0xa7) || (op >= 0xaa && op <= 0xaf)) {
                allowed = true; hardware.repeated_string = repeat != 0;
            }
            break;
        }
    }
    if (lock && !atomic) return (guest_profile_instruction){GUEST_PROFILE_PROCESSOR_FAULT, 6, false, "invalid LOCK prefix"};
    return allowed ? hardware : unsupported;
}

void guest_profile_x64_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t out[4])
{
    (void)subleaf;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (leaf == 0) { out[0] = 7; out[1] = 0x6b617551; out[2] = 0x74736575; out[3] = 0x67535465; }
    else if (leaf == 1) { out[0] = 0x600; out[1] = 0x10000; out[3] = 0x06008101; }
    else if (leaf == 0x80000000) out[0] = 0x80000001;
    else if (leaf == 0x80000001) { out[2] = 1; out[3] = 0x20000000; }
}
