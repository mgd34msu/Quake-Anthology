#include "internal.h"
#include "elf_bind.h"

/* Three address/addend terms fit this signed two-word representation. Keep
 * full precision until the ABI's narrow overflow check, without host signed
 * overflow or a compiler-specific integer type. */
typedef struct relocation_value { uint64_t low; int64_t high; } relocation_value;

static relocation_value unsigned_value(uint64_t value)
{ return (relocation_value){value, 0}; }

static relocation_value signed_value(uint64_t bits)
{ return (relocation_value){bits, bits >> 63 ? -1 : 0}; }

static relocation_value add(relocation_value a, relocation_value b)
{
    uint64_t low = a.low + b.low;
    return (relocation_value){low, a.high + b.high + (low < a.low)};
}

static relocation_value subtract(relocation_value a, relocation_value b)
{ return (relocation_value){a.low - b.low, a.high - b.high - (a.low < b.low)}; }

static bool context_valid(const guest_elf_binding *binding, qa_error *error)
{
    const guest_elf_view *image = binding ? guest_elf_describe(binding->image) : NULL;
    const qa_native_target *target = image ? &image->image.target : NULL;
    const qa_native_target *actual = binding && binding->guest ? &binding->guest->options.image.target : NULL;
    if (!image || !actual || !binding->runtime || !binding->provider ||
        guest_sysv_guest(binding->runtime) != binding->guest ||
        !qa_native_guest_idle(binding->guest) ||
        target->os != QA_NATIVE_OS_LINUX || target->os != actual->os ||
        target->arch != actual->arch || target->abi != actual->abi || target->pointer_bytes != actual->pointer_bytes ||
        !((target->arch == QA_NATIVE_ARCH_I386 && target->abi == QA_NATIVE_ABI_SYSTEM_V_I386 && target->pointer_bytes == 4) ||
          (target->arch == QA_NATIVE_ARCH_X86_64 && target->abi == QA_NATIVE_ABI_SYSTEM_V_X64 && target->pointer_bytes == 8)))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF binding needs its actual idle x86 System V process and provider");
    return true;
}

static bool live_address(const guest_elf_binding *binding, uint64_t address,
    uint32_t permissions, qa_error *error)
{
    return (address && guest_range(binding->guest, address, 1, permissions, error)) ||
        guest_fail(error, QA_ERROR_FORMAT, address, "ELF definition has no actual accessible guest address");
}

static bool indirect(const guest_elf_binding *binding, relocation_value resolver,
    uint64_t *out, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(binding->image);
    bool native = qa_native_guest_execution(binding->guest) == QA_NATIVE_GUEST_HOST_X86_64;
    if (resolver.high || !binding->return_trap ||
        (native ? binding->instruction_budget != 0 : !binding->instruction_budget) ||
        (image->image.target.pointer_bytes == 4 && resolver.low > UINT32_MAX) ||
        !live_address(binding, resolver.low, QA_NATIVE_GUEST_EXECUTE, error))
        return guest_fail(error, QA_ERROR_FORMAT, resolver.low, "ELF indirect relocation needs its actual resolver, return trap and execution capability");
    unsigned width = image->image.target.pointer_bytes;
    guest_abi_signature signature = {.abi = image->image.target.abi,
        .result = {.kind = QA_NATIVE_ADDRESS, .bytes = width, .alignment = width}};
    guest_abi_plan *plan = NULL;
    if (!guest_abi_plan_create(&signature, NULL, 0, &plan, error)) return false;
    qa_native_value result = {0};
    bool okay = native ? guest_abi_invoke_native(plan, binding->guest, resolver.low,
        binding->return_trap, NULL, 0, &result, error) :
        guest_abi_invoke(plan, binding->guest, resolver.low, binding->return_trap,
            NULL, 0, &result, binding->instruction_budget, error);
    guest_abi_plan_destroy(plan);
    if (!okay || !guest_ready(binding->guest, error)) return false;
    if (result.type != QA_NATIVE_ADDRESS || !live_address(binding, result.as.address,
        QA_NATIVE_GUEST_EXECUTE, error)) return false;
    *out = result.as.address; return true;
}

static bool defined(const guest_elf_binding *binding, const guest_elf_symbol *symbol,
    uint64_t *out, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(binding->image);
    if (!symbol->section || (symbol->section >= 0xff00 && symbol->section != 0xfff1) || symbol->type == 6)
        return guest_fail(error, QA_ERROR_FORMAT, symbol->value, "ELF definition is undefined, unallocated COMMON, reserved or TLS");
    relocation_value value = unsigned_value(symbol->value);
    if (symbol->section != 0xfff1) value = add(value, unsigned_value(image->bias));
    if (value.high || (image->image.target.pointer_bytes == 4 && value.low > UINT32_MAX))
        return guest_fail(error, QA_ERROR_FORMAT, value.low, "ELF definition exceeds its actual ABI address domain");
    if (symbol->type == 10) return indirect(binding, value, out, error);
    *out = value.low; return true;
}

static bool symbolic(const guest_elf_binding *binding)
{
    uint64_t value;
    return guest_elf_dynamic_value(binding->image, 16, &value) ||
        (guest_elf_dynamic_value(binding->image, 30, &value) && (value & 2));
}

static bool resolve(const guest_elf_binding *binding, size_t index, bool copy,
    guest_sysv_symbol *out, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(binding->image);
    *out = (guest_sysv_symbol){0};
    if (!index) return true;
    if (index >= image->symbol_count)
        return guest_fail(error, QA_ERROR_FORMAT, index, "ELF relocation lacks its actual symbol record");
    const guest_elf_symbol *symbol = &image->symbols[index];
    if (symbol->binding != 0 && symbol->binding != 1 && symbol->binding != 2 && symbol->binding != 10)
        return guest_fail(error, QA_ERROR_FORMAT, index, "ELF symbol has an unsupported binding");
    bool own = symbol->section != 0, unique = symbol->binding == 10;
    if (unique) {
        uint64_t existing = 0;
        if (symbol->type != 0 && symbol->type != 1)
            return guest_fail(error, QA_ERROR_FORMAT, index, "GNU unique binding requires an actual data symbol");
        if (!guest_sysv_unique(binding->runtime, symbol->name, 0, &existing, error)) return false;
        if (existing) {
            if (!live_address(binding, existing, QA_NATIVE_GUEST_READ, error)) return false;
            *out = (guest_sysv_symbol){.present = true, .address = existing}; return true;
        }
    }
    bool local = own && (symbol->binding == 0 || symbol->visibility != 0 || symbolic(binding));
    if (!copy && local) {
        uint64_t address;
        if (!defined(binding, symbol, &address, error)) return false;
        *out = (guest_sysv_symbol){.present = true, .provider = binding->provider,
            .address = address, .bytes = symbol->bytes, .size_known = true};
        if (unique && !guest_sysv_unique(binding->runtime, symbol->name, address, &out->address, error)) return false;
        return true;
    }
    const char *library = symbol->version && symbol->version->library ? symbol->version->library : "";
    const char *version = symbol->version ? symbol->version->name : NULL;
    if (!guest_sysv_resolve(binding->runtime, binding->provider, library, symbol->name, version,
        symbol->binding == 2, !copy && own, false, out, error)) return false;
    if (out->present) {
        if (!live_address(binding, out->address, symbol->type == 2 || symbol->type == 10 ?
            QA_NATIVE_GUEST_EXECUTE : QA_NATIVE_GUEST_READ, error)) return false;
    } else if (!copy && own) {
        if (!defined(binding, symbol, &out->address, error)) return false;
        out->present = true; out->provider = binding->provider;
        out->bytes = symbol->bytes; out->size_known = true;
    } else if (symbol->binding != 2)
        return guest_fail(error, QA_ERROR_NOT_FOUND, index, "ELF strong import has no actual process provider");
    if (unique && out->present &&
        !guest_sysv_unique(binding->runtime, symbol->name, out->address, &out->address, error)) return false;
    return guest_ready(binding->guest, error);
}

static bool tls_value(const guest_elf_binding *binding, const guest_elf_relocation *relocation,
    relocation_value addend, relocation_value *out, bool *signed_result, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(binding->image);
    bool wide = image->image.target.pointer_bytes == 8;
    guest_sysv_symbol target = {.present = true, .tls = true, .block = binding->tls};
    if (!relocation->symbol && (!image->tls || !binding->tls.module_id))
        return guest_fail(error, QA_ERROR_FORMAT, relocation->address, "ELF local TLS relocation lacks its actual allocated module");
    if (relocation->symbol) {
        if (relocation->symbol >= image->symbol_count || image->symbols[relocation->symbol].type != 6)
            return guest_fail(error, QA_ERROR_FORMAT, relocation->symbol, "ELF TLS relocation references a non-TLS symbol");
        const guest_elf_symbol *symbol = &image->symbols[relocation->symbol];
        bool local = symbol->section && (symbol->binding == 0 || symbol->visibility || symbolic(binding));
        if (!local) {
            const char *library = symbol->version && symbol->version->library ? symbol->version->library : "";
            if (!guest_sysv_resolve(binding->runtime, binding->provider, library, symbol->name,
                symbol->version ? symbol->version->name : NULL, symbol->binding == 2,
                symbol->section != 0, true, &target, error)) return false;
        }
        if (local || !target.present) {
            if (!image->tls || !binding->tls.module_id || !symbol->section ||
                symbol->value > binding->tls.bytes || symbol->bytes > binding->tls.bytes - symbol->value)
                return guest_fail(error, QA_ERROR_FORMAT, relocation->symbol, "ELF TLS definition exceeds its actual module block");
            target = (guest_sysv_symbol){.present = true, .tls = true, .offset = symbol->value, .block = binding->tls};
        }
    }
    if (!target.tls || !target.block.module_id || target.offset > target.block.bytes)
        return guest_fail(error, QA_ERROR_FORMAT, relocation->symbol, "ELF TLS provider has no actual module or offset");
    uint32_t type = relocation->type;
    if (type == (wide ? 16u : 35u)) *out = unsigned_value(target.block.module_id);
    else if (type == (wide ? 17u : 36u))
        *out = wide ? add(unsigned_value(target.offset), addend) : unsigned_value(target.offset);
    else {
        relocation_value displacement = add(signed_value((uint64_t)target.block.thread_pointer_offset), unsigned_value(target.offset));
        if (!wide && (type == 34 || type == 37)) displacement = subtract(unsigned_value(0), displacement);
        *out = add(displacement, addend); *signed_result = true;
    }
    return true;
}

static bool image_slot(const guest_elf_view *image, uint64_t address, uint64_t bytes, qa_error *error)
{
    for (size_t i = 0; i < image->segment_count; ++i) {
        const guest_elf_segment *segment = &image->segments[i];
        if (segment->type == 1 && address >= segment->address && address - segment->address <= segment->memory_bytes &&
            bytes <= segment->memory_bytes - (address - segment->address)) return true;
    }
    return guest_fail(error, QA_ERROR_FORMAT, address, "ELF relocation target exceeds its actual PT_LOAD memory extent");
}

static bool image_mapping(const guest_elf_view *image, uint64_t address)
{
    if (image->role == GUEST_ELF_LIBRARY)
        return address >= image->bias + image->first && address < image->bias + image->end;
    for (size_t i = 0; i < image->segment_count; ++i) {
        const guest_elf_segment *segment = &image->segments[i];
        if (segment->type != 1 || !segment->memory_bytes) continue;
        uint64_t first = segment->address & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1);
        uint64_t end = (segment->address + segment->memory_bytes + QA_NATIVE_GUEST_PAGE - 1) & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1);
        if (address >= image->bias + first && address < image->bias + end) return true;
    }
    return false;
}

static bool provider_size(const guest_elf_binding *binding, const guest_elf_symbol *symbol,
    guest_sysv_symbol *definition, qa_error *error)
{
    if (definition->size_known) return true;
    const guest_elf_view *image = guest_elf_describe(binding->image);
    relocation_value own = unsigned_value(symbol->value);
    if (symbol->section != 0xfff1) own = add(own, unsigned_value(image->bias));
    if (symbol->section && (symbol->section < 0xff00 || symbol->section == 0xfff1) && symbol->type != 10 &&
        !own.high && own.low == definition->address) {
        definition->bytes = symbol->bytes; definition->size_known = true; return true;
    }
    guest_sysv_symbol provider = {0};
    const char *library = symbol->version && symbol->version->library ? symbol->version->library : "";
    if (!guest_sysv_resolve(binding->runtime, binding->provider, library, symbol->name,
        symbol->version ? symbol->version->name : NULL, symbol->binding == 2,
        false, false, &provider, error)) return false;
    if (!provider.present || !provider.size_known)
        return guest_fail(error, QA_ERROR_FORMAT, definition->address, "ELF COPY/SIZE needs actual provider size metadata");
    definition->bytes = provider.bytes; definition->size_known = true; return true;
}

static bool apply(const guest_elf_binding *binding, const guest_elf_relocation *relocation, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(binding->image);
    uint32_t type = relocation->type;
    if (!type) return true;
    bool wide = image->image.target.pointer_bytes == 8;
    unsigned width = !wide ? 4 : type == 12 || type == 13 ? 2 : type == 14 || type == 15 ? 1 :
        type == 2 || type == 4 || type == 10 || type == 11 || type == 23 || type == 32 ? 4 : 8;
    const guest_elf_symbol *symbol = relocation->symbol < image->symbol_count ? &image->symbols[relocation->symbol] : NULL;
    uint64_t bytes = type == 5 && symbol ? symbol->bytes : width;
    if (bytes > SIZE_MAX || relocation->address > UINT64_MAX - image->bias)
        return guest_fail(error, QA_ERROR_FORMAT, relocation->address, "ELF relocation extent exceeds the actual host or guest address domain");
    if (!image_slot(image, relocation->address, bytes, error)) return false;
    uint64_t slot = image->bias + relocation->address;
    if (!guest_range(binding->guest, slot, (size_t)bytes, QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, error)) return false;
    uint8_t data[8] = {0}; relocation_value addend = unsigned_value(0);
    if (type != 5) {
        if (!qa_native_guest_read(binding->guest, slot, data, width, error)) return false;
        uint64_t bits = width == 8 ? qa_load_u64le(data) : width == 4 ? qa_load_u32le(data) :
            width == 2 ? qa_load_u16le(data) : data[0];
        if (width < 8 && (bits & (UINT64_C(1) << (width * 8 - 1)))) bits |= UINT64_MAX << (width * 8);
        if (relocation->explicit_addend) {
            bits = relocation->addend_bits;
            if (!wide && (bits & UINT64_C(0x80000000))) bits |= UINT64_C(0xffffffff00000000);
        }
        addend = signed_value(bits);
    }
    relocation_value result; bool signed_result = false;
    if (type == 8 || (wide && type == 38)) {
        if (relocation->symbol) return guest_fail(error, QA_ERROR_FORMAT, slot, "ELF relative relocation has a symbol");
        result = add(unsigned_value(image->bias), addend);
    } else if (type == (wide ? 37u : 42u)) {
        uint64_t address;
        if (relocation->symbol || !indirect(binding, add(unsigned_value(image->bias), addend), &address, error)) return false;
        result = unsigned_value(address);
    } else if (wide ? type == 16 || type == 17 || type == 18 || type == 23 :
        type == 14 || type == 17 || type == 34 || type == 35 || type == 36 || type == 37) {
        if (!tls_value(binding, relocation, addend, &result, &signed_result, error)) return false;
    } else {
        if (!symbol) return guest_fail(error, QA_ERROR_FORMAT, relocation->symbol, "ELF relocation has no actual symbol");
        guest_sysv_symbol definition;
        if (!resolve(binding, relocation->symbol, type == 5, &definition, error)) return false;
        uint64_t address = definition.address;
        if (type == 5) {
            if (!address) return symbol->binding == 2 || guest_fail(error, QA_ERROR_FORMAT, slot, "ELF COPY provider is null");
            if (image_mapping(image, address))
                return guest_fail(error, QA_ERROR_FORMAT, address, "ELF COPY must use a provider outside the requesting image");
            if (!provider_size(binding, symbol, &definition, error)) return false;
            size_t copied = definition.bytes < bytes ? (size_t)definition.bytes : (size_t)bytes;
            uint8_t *source = copied ? malloc(copied) : NULL;
            if (copied && !source) return guest_fail(error, QA_ERROR_MEMORY, slot, "owning ELF COPY source snapshot");
            bool okay = qa_native_guest_read(binding->guest, address, source, copied, error) &&
                qa_native_guest_write(binding->guest, slot, (qa_bytes){source, copied}, error);
            free(source); return okay;
        }
        result = unsigned_value(address);
        switch (type) {
        case 1: case 10: case 11: case 12: case 14:
            if (!wide && type != 1 && type != 10) goto unsupported;
            if (!wide && type == 10) {
                uint64_t got;
                if (!guest_elf_dynamic_value(binding->image, 3, &got)) goto missing_got;
                result = subtract(add(add(unsigned_value(image->bias), unsigned_value(got)), addend), unsigned_value(slot));
            } else result = add(result, addend);
            signed_result = type == 11; break;
        case 2: case 13: case 15: case 24:
            if (!wide && type != 2) goto unsupported;
            result = subtract(add(result, addend), unsigned_value(slot)); signed_result = true; break;
        case 6: case 7: break;
        case 9: {
            if (wide) goto unsupported;
            uint64_t got;
            if (!guest_elf_dynamic_value(binding->image, 3, &got)) goto missing_got;
            result = subtract(add(result, addend), add(unsigned_value(image->bias), unsigned_value(got))); break;
        }
        case 32: case 33: case 38:
            if (wide ? type == 38 : type != 38) goto unsupported;
            if (!provider_size(binding, symbol, &definition, error)) return false;
            result = add(unsigned_value(definition.bytes), addend); break;
        default: goto unsupported;
        }
    }
    if (wide && width < 8) {
        unsigned bits = width * 8; uint64_t maximum = (UINT64_C(1) << (signed_result ? bits - 1 : bits)) - 1;
        bool fits = result.high == 0 && result.low <= maximum;
        if (signed_result && result.high == -1 && result.low >= UINT64_MAX - maximum) fits = true;
        if (!fits) return guest_fail(error, QA_ERROR_FORMAT, slot, "ELF relocation overflows its actual narrow ABI field");
    }
    if (width == 8) qa_store_u64le(data, result.low);
    else if (width == 4) qa_store_u32le(data, (uint32_t)result.low);
    else if (width == 2) qa_store_u16le(data, (uint16_t)result.low);
    else data[0] = (uint8_t)result.low;
    return qa_native_guest_write(binding->guest, slot, (qa_bytes){data, width}, error);
missing_got:
    return guest_fail(error, QA_ERROR_FORMAT, slot, "ELF GOT relocation lacks its actual DT_PLTGOT");
unsupported:
    return guest_fail(error, QA_ERROR_FORMAT, type, "ELF relocation requires another actual processor or link-editor contract");
}

bool guest_elf_bind_relocations(const guest_elf_binding *binding, qa_error *error)
{
    if (!context_valid(binding, error)) return false;
    const guest_elf_view *image = guest_elf_describe(binding->image);
    bool wide = image->image.target.pointer_bytes == 8;
    for (unsigned pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < image->relocation_count; ++i) {
            const guest_elf_relocation *relocation = &image->relocations[i];
            const guest_elf_symbol *symbol = relocation->symbol < image->symbol_count ? &image->symbols[relocation->symbol] : NULL;
            bool deferred = relocation->type == (wide ? 37u : 42u) || (symbol && symbol->type == 10 && symbol->section);
            if (deferred != (pass != 0)) continue;
            if (!apply(binding, relocation, error) || !guest_ready(binding->guest, error)) {
                binding->guest->failed = true; return false;
            }
        }
    return true;
}

bool guest_elf_bind_export(const guest_elf_binding *binding, size_t index,
    guest_sysv_symbol *out, qa_error *error)
{
    if (!out) return guest_fail(error, QA_ERROR_ARGUMENT, index, "ELF export output is required");
    if (!context_valid(binding, error)) return false;
    const guest_elf_view *image = guest_elf_describe(binding->image);
    if (index >= image->symbol_count || !image->symbols[index].section)
        return guest_fail(error, QA_ERROR_ARGUMENT, index, "ELF export requires its actual defined symbol");
    const guest_elf_symbol *symbol = &image->symbols[index];
    if (symbol->type == 6) {
        if (!image->tls || !binding->tls.module_id || symbol->value > binding->tls.bytes || symbol->bytes > binding->tls.bytes - symbol->value)
            return guest_fail(error, QA_ERROR_FORMAT, index, "ELF TLS export exceeds its actual module");
        *out = (guest_sysv_symbol){.present = true, .tls = true, .provider = binding->provider,
            .bytes = symbol->bytes, .size_known = true, .offset = symbol->value, .block = binding->tls}; return true;
    }
    guest_sysv_symbol result = {.present = true, .provider = binding->provider, .bytes = symbol->bytes, .size_known = true};
    bool okay = symbol->binding == 10 ? resolve(binding, index, false, &result, error) : defined(binding, symbol, &result.address, error);
    if (okay) okay = live_address(binding, result.address, symbol->type == 2 || symbol->type == 10 ?
        QA_NATIVE_GUEST_EXECUTE : QA_NATIVE_GUEST_READ, error);
    if (!okay) { binding->guest->failed = true; return false; }
    result.provider = binding->provider; result.bytes = symbol->bytes; result.size_known = true;
    *out = result; return true;
}
