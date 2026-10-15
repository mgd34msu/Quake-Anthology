#include "abi.h"
#include "qa/binary.h"
#include <string.h>

void guest_abi_scalar_arguments(const guest_abi_scalar_call *call,
    const uint64_t registers[16], uint64_t values[3])
{
    for (size_t i = 0; i < call->count; ++i) values[i] = registers[call->arguments[i]];
}

void guest_abi_integer_register(uint64_t registers[16], unsigned reg, size_t bytes, uint64_t value)
{ registers[reg] = bytes == 8 ? value : value & ((UINT64_C(1) << (bytes * 8)) - 1); }

void guest_abi_scalar_result(const guest_abi_scalar_call *call, uint64_t registers[16], uint64_t value)
{ guest_abi_integer_register(registers, call->result, call->result_bytes, value); }

bool guest_abi_return_address(unsigned word, size_t callee_pop, uint64_t sp,
    guest_abi_read_fn read, guest_abi_access_fn access, void *context,
    uint64_t *next_sp, uint64_t *instruction, qa_error *error)
{
    size_t pop = word + callee_pop;
    if (pop < word || sp > UINT64_MAX - pop || (word == 4 && sp + pop > UINT32_MAX)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, (size_t)sp, "guest ABI callback return stack overflows");
        return false;
    }
    uint8_t address[8] = {0};
    if (!read(context, sp, address, word, error)) return false;
    uint64_t target = word == 8 ? qa_load_u64le(address) : qa_load_u32le(address);
    if (!access(context, target, 1, QA_NATIVE_GUEST_EXECUTE, error)) return false;
    *next_sp = sp + pop; *instruction = target; return true;
}

bool guest_readonly_apply(guest_readonly_kind kind, qa_bytes left, qa_bytes right,
    uint64_t count, uint64_t *out)
{
    if (kind == GUEST_READONLY_COMPARE) {
        if (count > 0x10000000 || count > left.size || count > right.size) return false;
        *out = 0;
        for (size_t i = 0; i < (size_t)count; ++i) if (left.data[i] != right.data[i]) {
            *out = (uint64_t)((int)left.data[i] - (int)right.data[i]); break;
        }
        return true;
    }
    if (kind == GUEST_READONLY_LENGTH) {
        size_t bytes = left.size < 1048576 ? left.size : 1048576;
        const uint8_t *end = bytes ? memchr(left.data, 0, bytes) : NULL;
        if (!end) return false;
        *out = (uint64_t)(end - left.data); return true;
    }
    return false;
}
