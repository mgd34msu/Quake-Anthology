/*
 * Port of src/guest/core/registers.ts (IntegerRegisterFile, ProcessorFlags,
 * createGuestProcessorState). The shared processor/memory types come from
 * src/guest/core/contracts.ts and src/contracts/execution.ts, which are not
 * yet ported as their own modules; the minimal shapes used by the guest
 * leaves live here (the registers module owns processor-state creation).
 * TS bigint register/IP values become uint64_t (guest addresses are < 2^64).
 * TS Object.freeze/managed WeakMap bookkeeping has no observable C effect
 * beyond the `managed` flag recorded here. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_CORE_REGISTERS_H
#define QA_GUEST_CORE_REGISTERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* GuestArchitecture / GuestRegister / GuestFlag (core/contracts.ts). */
typedef enum qa_guest_architecture { QA_GUEST_I386 = 0, QA_GUEST_X86_64 = 1 } qa_guest_architecture;
const char *qa_guest_architecture_name(qa_guest_architecture arch);

typedef enum qa_guest_register {
  QA_REG_RAX = 0, QA_REG_RCX, QA_REG_RDX, QA_REG_RBX, QA_REG_RSP, QA_REG_RBP, QA_REG_RSI, QA_REG_RDI,
  QA_REG_R8, QA_REG_R9, QA_REG_R10, QA_REG_R11, QA_REG_R12, QA_REG_R13, QA_REG_R14, QA_REG_R15
} qa_guest_register;
const char *qa_guest_register_name(qa_guest_register reg);

typedef enum qa_guest_flag {
  QA_FLAG_CARRY = 0, QA_FLAG_PARITY, QA_FLAG_AUXILIARY_CARRY, QA_FLAG_ZERO, QA_FLAG_SIGN,
  QA_FLAG_TRAP, QA_FLAG_INTERRUPT, QA_FLAG_DIRECTION, QA_FLAG_OVERFLOW, QA_FLAG_RESUME,
  QA_FLAG_VIRTUAL_8086, QA_FLAG_ALIGNMENT_CHECK, QA_FLAG_VIRTUAL_INTERRUPT,
  QA_FLAG_VIRTUAL_INTERRUPT_PENDING, QA_FLAG_IDENTIFICATION
} qa_guest_flag;
const char *qa_guest_flag_name(qa_guest_flag flag);
uint32_t qa_guest_flag_mask(qa_guest_flag flag);

/* IntegerRegisterFile: 64 bytes (i386) or 128 bytes (x86-64) of LE slots. */
typedef struct qa_integer_register_file {
  qa_guest_architecture architecture;
  uint8_t bytes[128];
} qa_integer_register_file;

size_t qa_register_file_size(qa_guest_architecture arch);
void qa_integer_register_file_init(qa_integer_register_file *regs, qa_guest_architecture arch);
bool qa_integer_register_file_attached(const qa_integer_register_file *regs);
uint64_t qa_integer_register_file_read(const qa_integer_register_file *regs, qa_guest_register reg,
                                       int width, bool high_byte);
void qa_integer_register_file_write(qa_integer_register_file *regs, qa_guest_register reg, int width,
                                    uint64_t value, bool high_byte);
/* checkpoint: if dest==NULL returns byte count only; else dest_len must match
 * the file size or it aborts like the TS RangeError. Returns byte count. */
size_t qa_integer_register_file_checkpoint(const qa_integer_register_file *regs, uint8_t *dest, size_t dest_len);
void qa_integer_register_file_restore(qa_integer_register_file *regs, const uint8_t *bytes, size_t len);

/* ProcessorFlags: low/high 32-bit words with a lazily rebuilt 64-bit cache,
 * mirroring the TS #value/#low/#high representation. */
typedef struct qa_processor_flags {
  uint32_t low;
  uint32_t high;
  uint64_t cached;
  bool cached_valid;
} qa_processor_flags;

void qa_processor_flags_init(qa_processor_flags *flags, uint64_t value);
uint64_t qa_processor_flags_value(qa_processor_flags *flags);
void qa_processor_flags_set_value(qa_processor_flags *flags, uint64_t value);
void qa_processor_flags_restore_words(qa_processor_flags *flags, uint32_t low, uint32_t high);
void qa_processor_flags_write_low_word(qa_processor_flags *flags, uint32_t value);
bool qa_processor_flags_get(const qa_processor_flags *flags, qa_guest_flag flag);
void qa_processor_flags_set(qa_processor_flags *flags, qa_guest_flag flag, bool value);

/* GuestSegment / GuestX87State / GuestSimdState (core/contracts.ts). */
typedef struct qa_guest_segment { uint32_t selector; uint64_t base; uint64_t limit; } qa_guest_segment;
typedef enum qa_guest_segment_index {
  QA_SEG_CS = 0, QA_SEG_DS, QA_SEG_ES, QA_SEG_SS, QA_SEG_FS, QA_SEG_GS, QA_SEG_COUNT
} qa_guest_segment_index;

typedef struct qa_guest_x87_state {
  uint8_t registers[80]; /* eight 10-byte slots */
  uint32_t control_word;
  uint32_t status_word;
  uint32_t tag_word;
  uint32_t last_opcode;
  uint64_t instruction_pointer;
  uint64_t data_pointer;
  uint32_t instruction_selector;
  uint32_t data_selector;
} qa_guest_x87_state;

typedef struct qa_guest_simd_state {
  uint8_t xmm[256];
  size_t xmm_len; /* 128 (i386) or 256 (x86-64) significant bytes */
  uint32_t mxcsr;
  uint32_t mxcsr_mask;
  uint8_t sse_scratch[16]; /* retained raw-SSE memory operand (TS WeakMap) */
  bool sse_scratch_valid;
} qa_guest_simd_state;

typedef struct qa_guest_processor_state {
  qa_guest_architecture architecture;
  qa_integer_register_file registers;
  uint64_t instruction_pointer;
  qa_processor_flags flags;
  qa_guest_segment segments[QA_SEG_COUNT];
  qa_guest_x87_state x87;
  qa_guest_simd_state simd;
  bool managed;
} qa_guest_processor_state;

/* GuestProcessorInitialState (registers.ts). */
typedef struct qa_guest_processor_initial_state {
  qa_guest_architecture architecture;
  uint64_t instruction_pointer;
  uint64_t stack_pointer;
  uint64_t flags;
  uint32_t x87_control_word;
  uint32_t mxcsr;
  uint32_t mxcsr_mask;
} qa_guest_processor_initial_state;

void qa_create_guest_processor_state(qa_guest_processor_state *out, const qa_guest_processor_initial_state *initial);
void qa_create_managed_guest_processor_state(qa_guest_processor_state *out,
                                             const qa_guest_processor_initial_state *initial);
/* TS managedGuestProcessor(): true when created via the managed factory. */
bool qa_managed_guest_processor(const qa_guest_processor_state *state);

/*
 * GuestAddress + MappedGuestMemory (contracts/execution.ts GuestMemory and
 * core/contracts.ts MappedGuestMemory). Interface becomes fn pointers + ctx.
 * Only the operations the guest leaves use are modeled. `copy` writes into a
 * caller buffer (C accommodation for TS copy() returning a fresh Uint8Array).
 * read/write_u64_words and `managed_sparse` mirror SparseGuestMemory extras
 * used by floating-point/raw-sse.ts. Optional ops may be NULL; the
 * qa_memory_* helpers fall back to copy/write/read_u64/write_u64.
 */
typedef struct qa_guest_address { uint64_t byte_offset; } qa_guest_address;

typedef enum qa_guest_access { QA_ACCESS_READ = 0, QA_ACCESS_WRITE = 1, QA_ACCESS_EXECUTE = 2 } qa_guest_access;

typedef struct qa_mapped_guest_memory {
  void *ctx;
  int pointer_bytes; /* 4 | 8 */
  bool managed_sparse;
  void (*copy)(void *ctx, qa_guest_address address, uint8_t *dest, size_t len);
  void (*write)(void *ctx, qa_guest_address address, const uint8_t *bytes, size_t len);
  void (*copy_into)(void *ctx, qa_guest_address address, uint8_t *dest, size_t dest_offset, size_t len);
  uint8_t (*read_u8)(void *ctx, qa_guest_address address);
  uint16_t (*read_u16)(void *ctx, qa_guest_address address);
  uint32_t (*read_u32)(void *ctx, qa_guest_address address);
  uint64_t (*read_u64)(void *ctx, qa_guest_address address);
  void (*write_u8)(void *ctx, qa_guest_address address, uint8_t value);
  void (*write_u16)(void *ctx, qa_guest_address address, uint16_t value);
  void (*write_u32)(void *ctx, qa_guest_address address, uint32_t value);
  void (*write_u64)(void *ctx, qa_guest_address address, uint64_t value);
  bool (*pointer)(void *ctx, uint64_t raw, qa_guest_address *out); /* false = TS null */
  qa_guest_address (*offset)(void *ctx, qa_guest_address address, int64_t displacement);
  void (*check)(void *ctx, qa_guest_address address, size_t len, qa_guest_access access);
  void (*read_u64_words)(void *ctx, qa_guest_address address, uint32_t *low, uint32_t *high);
  void (*write_u64_words)(void *ctx, qa_guest_address address, uint32_t low, uint32_t high);
} qa_mapped_guest_memory;

void qa_memory_copy(const qa_mapped_guest_memory *mem, qa_guest_address address, uint8_t *dest, size_t len);
void qa_memory_write(const qa_mapped_guest_memory *mem, qa_guest_address address, const uint8_t *bytes, size_t len);
void qa_memory_copy_into(const qa_mapped_guest_memory *mem, qa_guest_address address, uint8_t *dest,
                         size_t dest_offset, size_t len);
uint8_t qa_memory_read_u8(const qa_mapped_guest_memory *mem, qa_guest_address address);
uint16_t qa_memory_read_u16(const qa_mapped_guest_memory *mem, qa_guest_address address);
uint32_t qa_memory_read_u32(const qa_mapped_guest_memory *mem, qa_guest_address address);
uint64_t qa_memory_read_u64(const qa_mapped_guest_memory *mem, qa_guest_address address);
void qa_memory_write_u8(const qa_mapped_guest_memory *mem, qa_guest_address address, uint8_t value);
void qa_memory_write_u16(const qa_mapped_guest_memory *mem, qa_guest_address address, uint16_t value);
void qa_memory_write_u32(const qa_mapped_guest_memory *mem, qa_guest_address address, uint32_t value);
void qa_memory_write_u64(const qa_mapped_guest_memory *mem, qa_guest_address address, uint64_t value);
bool qa_memory_pointer(const qa_mapped_guest_memory *mem, uint64_t raw, qa_guest_address *out);
qa_guest_address qa_memory_offset(const qa_mapped_guest_memory *mem, qa_guest_address address, int64_t displacement);
void qa_memory_check(const qa_mapped_guest_memory *mem, qa_guest_address address, size_t len, qa_guest_access access);
void qa_memory_read_u64_words(const qa_mapped_guest_memory *mem, qa_guest_address address, uint32_t *low, uint32_t *high);
void qa_memory_write_u64_words(const qa_mapped_guest_memory *mem, qa_guest_address address, uint32_t low, uint32_t high);

#endif
