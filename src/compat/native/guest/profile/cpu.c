#include "cpu.h"
#include "qa/binary.h"
#include <string.h>

static bool fail(qa_error *error, uint64_t where, const char *detail)
{ qa_error_set(error, QA_ERROR_UNSUPPORTED, (size_t)where, "%s", detail); return false; }

static bool layouts(const guest_host_x86_64_state *state,
    const guest_host_x86_64_capabilities *capability,
    guest_host_x86_64_component rows[64], qa_error *error)
{
    if (!guest_host_x86_64_state_valid(state, capability, error)) return false;
    /* These are the actual named user components supported by QAHC.
     * Unknown enabled components require a real ownership/codec extension. */
    const uint64_t known = UINT64_C(3) | (UINT64_C(1) << 2) |
        (UINT64_C(1) << 3) | (UINT64_C(1) << 4) | (UINT64_C(1) << 5) |
        (UINT64_C(1) << 6) | (UINT64_C(1) << 7) | (UINT64_C(1) << 9) |
        (UINT64_C(1) << 17) | (UINT64_C(1) << 18) | (UINT64_C(1) << 19);
    if ((capability->xfeatures & 3) != 3 || (capability->xfeatures & ~known))
        return fail(error, capability->xfeatures, "physical user component has no source profile owner");
    memset(rows, 0, 64 * sizeof(*rows));
    for (unsigned bit = 2; bit < 64; ++bit) if ((capability->xfeatures >> bit) & 1) {
        if (!guest_host_x86_64_component_read(bit, rows + bit, error)) return false;
        const guest_host_x86_64_component *row = rows + bit;
        size_t expected = 0;
        switch (bit) {
        case 2: expected = 256; break;
        case 3: expected = 64; break;
        case 4: expected = 64; break;
        case 5: expected = 64; break;
        case 6: expected = 512; break;
        case 7: expected = 1024; break;
        case 9: expected = 8; break;
        case 17: expected = 64; break;
        case 18: expected = row->bytes;
            if (!expected || expected % 1024 || expected > 8192)
                return fail(error, bit, "actual tile register layout has no source domain");
            break;
        case 19: expected = 128; break;
        }
        if (row->supervisor || row->offset < 576 || row->offset > capability->xsave_bytes ||
            row->bytes != expected || row->bytes > capability->xsave_bytes - row->offset)
            return fail(error, bit, "source component differs from its actual user transfer layout");
    }
    if ((capability->xfeatures & (UINT64_C(1) << 9)) && rows[9].bytes < 4)
        return fail(error, 9, "actual PKRU component is truncated");
    return true;
}

static bool zero(const uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; ++i) if (bytes[i]) return false;
    return true;
}

static bool component_init(unsigned bit, const uint8_t *data, size_t bytes)
{
    /* Match the named QAHC register fields, never architectural reserved
     * padding. Active init-valued SIMD rows are legitimate: DR's signal
     * delivery materializes their BV bits while copying actual registers. */
    if (bit == 4) return zero(data, 16); /* BNDCFGU/BNDSTATUS */
    if (bit == 17) return zero(data, 2) && zero(data + 16, 16) && zero(data + 48, 8);
    return zero(data, bytes);
}

bool guest_profile_cpu_domain_read(const guest_host_x86_64_state *baseline,
    const guest_host_x86_64_capabilities *capability,
    guest_profile_cpu_domain *out, qa_error *error)
{
    guest_host_x86_64_component rows[64];
    if (!out || !layouts(baseline, capability, rows, error)) return false;
    if (baseline->xfeatures != capability->xfeatures)
        return fail(error, 0, "constructor baseline lacks its physical enabled-component inventory");
    bool has_pkru = (baseline->xfeatures & (UINT64_C(1) << 9)) != 0;
    uint64_t active = qa_load_u64le(baseline->xsave.data + 512);
    uint32_t pkru = has_pkru && (active & (UINT64_C(1) << 9)) ?
        qa_load_u32le(baseline->xsave.data + rows[9].offset) : 0;
    /* The real anonymous/file mappings use pkey zero. It must admit their
     * reads/writes; other protection-key values remain the host's actual ones. */
    if (pkru & 3) return fail(error, 9, "physical controller denies its actual pkey-zero guest mappings");
    *out = (guest_profile_cpu_domain){baseline->xfeatures, pkru, has_pkru};
    return true;
}

bool guest_profile_cpu_current(const guest_profile_cpu_domain *domain,
    const guest_host_x86_64_capabilities *capability,
    const guest_host_x86_64_state *state, qa_error *error)
{
    guest_host_x86_64_component rows[64];
    if (!domain || !layouts(state, capability, rows, error)) return false;
    if (domain->xfeatures != capability->xfeatures ||
        domain->has_pkru != ((capability->xfeatures & (UINT64_C(1) << 9)) != 0) ||
        (!domain->has_pkru && domain->pkru) || (domain->pkru & 3))
        return fail(error, 0, "source CPU domain differs from its physical constructor baseline");
    uint64_t active = qa_load_u64le(state->xsave.data + 512);
    /* The actual kernel transfer may omit unused enabled components, such as
     * tile data. Their init values follow the once-fresh source construction
     * and excluded writes, with genuine kernel/engine capture required to
     * preserve that domain. state_valid alone only proves transfer/BV bounds.
     * Retain the physical domain independently; never enlarge the frame. */
    for (unsigned bit = 2; bit < 64; ++bit) if (bit != 9 && ((active >> bit) & 1) &&
        !component_init(bit, state->xsave.data + rows[bit].offset, rows[bit].bytes))
        return fail(error, bit, "non-init extended registers have no source x87/SSE domain");
    uint32_t pkru = domain->has_pkru && (active & (UINT64_C(1) << 9)) ?
        qa_load_u32le(state->xsave.data + rows[9].offset) : 0;
    if (pkru != domain->pkru) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,9,
            "source PKRU %u differs from the actual controller baseline %u at instruction %llu",
            pkru,domain->pkru,(unsigned long long)state->instruction);
        return false;
    }
    return true;
}

bool guest_profile_cpu_initialize(const guest_profile_cpu_domain *domain,
    const guest_host_x86_64_capabilities *capability,
    guest_host_x86_64_state *fresh, qa_error *error)
{
    guest_host_x86_64_component rows[64];
    if (!domain || !layouts(fresh, capability, rows, error)) return false;
    if (fresh->xfeatures != capability->xfeatures ||
        fresh->xsave.size < capability->xsave_bytes ||
        domain->xfeatures != fresh->xfeatures ||
        domain->has_pkru != ((fresh->xfeatures & (UINT64_C(1) << 9)) != 0) ||
        (!domain->has_pkru && domain->pkru) || (domain->pkru & 3))
        return fail(error, 0, "fresh source state differs from its actual physical domain");
    /* XSTATE_BV-clear is the architecture's init state. No ignored transfer
     * bytes become register values; legacy x87/SSE source initialization and
     * actual GPR/segment/stack/return state remain owned by their constructors. */
    for (unsigned bit = 2; bit < 64; ++bit) if ((fresh->xfeatures >> bit) & 1)
        memset(fresh->xsave.data + rows[bit].offset, 0, rows[bit].bytes);
    uint64_t active = qa_load_u64le(fresh->xsave.data + 512) & 3;
    if (domain->has_pkru && domain->pkru) {
        qa_store_u32le(fresh->xsave.data + rows[9].offset, domain->pkru);
        active |= UINT64_C(1) << 9;
    }
    qa_store_u64le(fresh->xsave.data + 512, active);
    return guest_profile_cpu_current(domain, capability, fresh, error);
}
