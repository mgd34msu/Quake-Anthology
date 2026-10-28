/* Port of src/guest/core/registers.ts. GPL-2.0-or-later. */
#include "qa/guest/core/registers.h"

#include <string.h>

#include "qa/foundation/support.h"

static const char *const k_register_names[16] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
static const uint32_t k_flag_masks[15] = {0x1, 0x4, 0x10, 0x40, 0x80, 0x100, 0x200, 0x400,
                                          0x800, 0x10000, 0x20000, 0x40000, 0x80000, 0x100000, 0x200000};
static const char *const k_flag_names[15] = {
    "carry", "parity", "auxiliary-carry", "zero", "sign", "trap", "interrupt", "direction",
    "overflow", "resume", "virtual-8086", "alignment-check", "virtual-interrupt",
    "virtual-interrupt-pending", "identification"};

const char *qa_guest_architecture_name(qa_guest_architecture arch) { return arch == QA_GUEST_I386 ? "i386" : "x86-64"; }
const char *qa_guest_register_name(qa_guest_register reg) { return k_register_names[(int)reg & 15]; }
const char *qa_guest_flag_name(qa_guest_flag flag) { return k_flag_names[(int)flag]; }
uint32_t qa_guest_flag_mask(qa_guest_flag flag) { return k_flag_masks[(int)flag]; }

size_t qa_register_file_size(qa_guest_architecture arch) { return arch == QA_GUEST_I386 ? 64 : 128; }

void qa_integer_register_file_init(qa_integer_register_file *regs, qa_guest_architecture arch) {
  regs->architecture = arch;
  memset(regs->bytes, 0, sizeof regs->bytes);
}

bool qa_integer_register_file_attached(const qa_integer_register_file *regs) {
  /* TS compares against its buffer length; the C buffer is fixed-size. */
  return qa_register_file_size(regs->architecture) == (regs->architecture == QA_GUEST_I386 ? 64u : 128u);
}

/* TS #offset validation, identical RangeError messages. */
static size_t reg_offset(const qa_integer_register_file *regs, qa_guest_register reg, int width, bool high_byte) {
  int index = (int)reg;
  if (regs->architecture == QA_GUEST_I386 && (index >= 8 || width == 64))
    qa_range_error("Register is not available in i386 mode");
  if (high_byte && (width != 8 || index > 3))
    qa_range_error("Only AH, CH, DH, and BH have high-byte register aliases");
  if (regs->architecture == QA_GUEST_I386 && width == 8 && index > 3)
    qa_range_error("SPL, BPL, SIL, and DIL require x86-64 mode");
  return (size_t)index * 8u;
}

static uint64_t load_le(const uint8_t *p, int n) {
  uint64_t v = 0;
  for (int i = n - 1; i >= 0; i--) v = (v << 8) | (uint64_t)p[i];
  return v;
}
static void store_le(uint8_t *p, uint64_t v, int n) {
  for (int i = 0; i < n; i++) { p[i] = (uint8_t)(v & 0xffu); v >>= 8; }
}

uint64_t qa_integer_register_file_read(const qa_integer_register_file *regs, qa_guest_register reg,
                                       int width, bool high_byte) {
  size_t off = reg_offset(regs, reg, width, high_byte);
  if (width == 64) return load_le(regs->bytes + off, 8);
  if (width == 32) return load_le(regs->bytes + off, 4);
  if (width == 16) return load_le(regs->bytes + off, 2);
  return load_le(regs->bytes + off + (high_byte ? 1u : 0u), 1);
}

void qa_integer_register_file_write(qa_integer_register_file *regs, qa_guest_register reg, int width,
                                    uint64_t value, bool high_byte) {
  size_t off = reg_offset(regs, reg, width, high_byte);
  if (width == 64) { store_le(regs->bytes + off, value, 8); return; }
  /* BigInt.asUintN(width, value) truncation (uint64 input already wraps). */
  uint64_t normalized = width == 32 ? value & 0xffffffffu : width == 16 ? value & 0xffffu : value & 0xffu;
  if (width == 32) store_le(regs->bytes + off, normalized, 8); /* clears upper half */
  else if (width == 16) store_le(regs->bytes + off, normalized, 2);
  else store_le(regs->bytes + off + (high_byte ? 1u : 0u), normalized, 1);
}

size_t qa_integer_register_file_checkpoint(const qa_integer_register_file *regs, uint8_t *dest, size_t dest_len) {
  size_t n = qa_register_file_size(regs->architecture);
  if (dest == NULL) return n;
  if (dest_len != n) qa_range_error("Guest register snapshot has the wrong architecture or length");
  memcpy(dest, regs->bytes, n);
  return n;
}

void qa_integer_register_file_restore(qa_integer_register_file *regs, const uint8_t *bytes, size_t len) {
  size_t n = qa_register_file_size(regs->architecture);
  if (len != n) qa_range_error("Guest register snapshot has the wrong architecture or length");
  if (regs->architecture == QA_GUEST_I386) {
    for (size_t off = 4; off < len; off += 8) {
      if (load_le(bytes + off, 4) != 0) qa_range_error("i386 snapshot has nonzero upper register bits");
    }
  }
  memcpy(regs->bytes, bytes, n);
}

void qa_processor_flags_init(qa_processor_flags *flags, uint64_t value) {
  flags->cached = value; /* BigInt.asUintN(64, value) */
  flags->cached_valid = true;
  flags->low = (uint32_t)(value & 0xffffffffu);
  flags->high = (uint32_t)(value >> 32);
}
uint64_t qa_processor_flags_value(qa_processor_flags *flags) {
  if (!flags->cached_valid) {
    flags->cached = ((uint64_t)flags->high << 32) | (uint64_t)flags->low;
    flags->cached_valid = true;
  }
  return flags->cached;
}
void qa_processor_flags_set_value(qa_processor_flags *flags, uint64_t value) { qa_processor_flags_init(flags, value); }
void qa_processor_flags_restore_words(qa_processor_flags *flags, uint32_t low, uint32_t high) {
  flags->low = low; flags->high = high; flags->cached_valid = false;
}
void qa_processor_flags_write_low_word(qa_processor_flags *flags, uint32_t value) {
  flags->low = value; flags->cached_valid = false;
}
bool qa_processor_flags_get(const qa_processor_flags *flags, qa_guest_flag flag) {
  return (flags->low & k_flag_masks[(int)flag]) != 0;
}
void qa_processor_flags_set(qa_processor_flags *flags, qa_guest_flag flag, bool value) {
  uint32_t mask = k_flag_masks[(int)flag];
  qa_processor_flags_write_low_word(flags, value ? flags->low | mask : flags->low & ~mask);
}

static void create_state(qa_guest_processor_state *out, const qa_guest_processor_initial_state *initial, bool managed) {
  qa_integer_register_file_init(&out->registers, initial->architecture);
  int width = initial->architecture == QA_GUEST_I386 ? 32 : 64;
  if (width == 32 && (initial->instruction_pointer > 0xffffffffu || initial->stack_pointer > 0xffffffffu))
    qa_range_error("Initial guest IP or stack pointer exceeds its architecture");
  if (initial->x87_control_word > 0xffffu)
    qa_range_error("Initial floating-point control state exceeds its register width");
  uint64_t limit_minus_1 = width == 32 ? 0xffffffffu : UINT64_MAX;
  qa_integer_register_file_write(&out->registers, QA_REG_RSP, width, initial->stack_pointer, false);
  qa_processor_flags_init(&out->flags, initial->flags);
  out->architecture = initial->architecture;
  out->instruction_pointer = initial->instruction_pointer;
  for (int i = 0; i < QA_SEG_COUNT; i++) {
    out->segments[i].selector = 0; out->segments[i].base = 0; out->segments[i].limit = limit_minus_1;
  }
  memset(&out->x87, 0, sizeof out->x87);
  out->x87.control_word = initial->x87_control_word;
  out->x87.tag_word = 0xffffu;
  memset(&out->simd, 0, sizeof out->simd);
  out->simd.xmm_len = initial->architecture == QA_GUEST_I386 ? 128u : 256u;
  out->simd.mxcsr = initial->mxcsr;
  out->simd.mxcsr_mask = initial->mxcsr_mask;
  out->managed = managed;
}

void qa_create_guest_processor_state(qa_guest_processor_state *out, const qa_guest_processor_initial_state *initial) {
  create_state(out, initial, false);
}
void qa_create_managed_guest_processor_state(qa_guest_processor_state *out,
                                             const qa_guest_processor_initial_state *initial) {
  create_state(out, initial, true);
}
bool qa_managed_guest_processor(const qa_guest_processor_state *state) { return state->managed; }

/* --- MappedGuestMemory helpers with TS-equivalent fallbacks --- */
void qa_memory_copy(const qa_mapped_guest_memory *mem, qa_guest_core_address a, uint8_t *dest, size_t len) { mem->copy(mem->ctx, a, dest, len); }
void qa_memory_write(const qa_mapped_guest_memory *mem, qa_guest_core_address a, const uint8_t *b, size_t len) { mem->write(mem->ctx, a, b, len); }
void qa_memory_copy_into(const qa_mapped_guest_memory *mem, qa_guest_core_address a, uint8_t *dest, size_t off, size_t len) {
  if (mem->copy_into != NULL) { mem->copy_into(mem->ctx, a, dest, off, len); return; }
  mem->copy(mem->ctx, a, dest + off, len);
}
uint8_t qa_memory_read_u8(const qa_mapped_guest_memory *mem, qa_guest_core_address a) {
  if (mem->read_u8 != NULL) return mem->read_u8(mem->ctx, a);
  uint8_t b = 0; mem->copy(mem->ctx, a, &b, 1); return b;
}
uint16_t qa_memory_read_u16(const qa_mapped_guest_memory *mem, qa_guest_core_address a) {
  if (mem->read_u16 != NULL) return mem->read_u16(mem->ctx, a);
  uint8_t b[2]; mem->copy(mem->ctx, a, b, 2); return (uint16_t)load_le(b, 2);
}
uint32_t qa_memory_read_u32(const qa_mapped_guest_memory *mem, qa_guest_core_address a) {
  if (mem->read_u32 != NULL) return mem->read_u32(mem->ctx, a);
  uint8_t b[4]; mem->copy(mem->ctx, a, b, 4); return (uint32_t)load_le(b, 4);
}
uint64_t qa_memory_read_u64(const qa_mapped_guest_memory *mem, qa_guest_core_address a) {
  if (mem->read_u64 != NULL) return mem->read_u64(mem->ctx, a);
  uint8_t b[8]; mem->copy(mem->ctx, a, b, 8); return load_le(b, 8);
}
void qa_memory_write_u8(const qa_mapped_guest_memory *mem, qa_guest_core_address a, uint8_t v) {
  if (mem->write_u8 != NULL) { mem->write_u8(mem->ctx, a, v); return; }
  mem->write(mem->ctx, a, &v, 1);
}
void qa_memory_write_u16(const qa_mapped_guest_memory *mem, qa_guest_core_address a, uint16_t v) {
  if (mem->write_u16 != NULL) { mem->write_u16(mem->ctx, a, v); return; }
  uint8_t b[2]; store_le(b, v, 2); mem->write(mem->ctx, a, b, 2);
}
void qa_memory_write_u32(const qa_mapped_guest_memory *mem, qa_guest_core_address a, uint32_t v) {
  if (mem->write_u32 != NULL) { mem->write_u32(mem->ctx, a, v); return; }
  uint8_t b[4]; store_le(b, v, 4); mem->write(mem->ctx, a, b, 4);
}
void qa_memory_write_u64(const qa_mapped_guest_memory *mem, qa_guest_core_address a, uint64_t v) {
  if (mem->write_u64 != NULL) { mem->write_u64(mem->ctx, a, v); return; }
  uint8_t b[8]; store_le(b, v, 8); mem->write(mem->ctx, a, b, 8);
}
bool qa_memory_pointer(const qa_mapped_guest_memory *mem, uint64_t raw, qa_guest_core_address *out) {
  if (mem->pointer != NULL) return mem->pointer(mem->ctx, raw, out);
  if (raw == 0) return false; /* TS null representation */
  out->byte_offset = raw;
  return true;
}
qa_guest_core_address qa_memory_offset(const qa_mapped_guest_memory *mem, qa_guest_core_address a, int64_t d) {
  if (mem->offset != NULL) return mem->offset(mem->ctx, a, d);
  qa_guest_core_address r = {(uint64_t)((int64_t)a.byte_offset + d)};
  return r;
}
void qa_memory_check(const qa_mapped_guest_memory *mem, qa_guest_core_address a, size_t len, qa_guest_access access) {
  if (mem->check != NULL) mem->check(mem->ctx, a, len, access);
}
void qa_memory_read_u64_words(const qa_mapped_guest_memory *mem, qa_guest_core_address a, uint32_t *low, uint32_t *high) {
  if (mem->read_u64_words != NULL) { mem->read_u64_words(mem->ctx, a, low, high); return; }
  uint64_t v = qa_memory_read_u64(mem, a);
  *low = (uint32_t)v; *high = (uint32_t)(v >> 32);
}
void qa_memory_write_u64_words(const qa_mapped_guest_memory *mem, qa_guest_core_address a, uint32_t low, uint32_t high) {
  if (mem->write_u64_words != NULL) { mem->write_u64_words(mem->ctx, a, low, high); return; }
  qa_memory_write_u64(mem, a, ((uint64_t)high << 32) | (uint64_t)low);
}
