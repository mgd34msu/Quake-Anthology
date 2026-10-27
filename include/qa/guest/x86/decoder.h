/*
 * Port of src/guest/x86/decoder.ts: i386 prefix/ModRM decode and
 * operand access. TS bigint offsets/values become uint64_t (32-bit
 * wrapped at the TS BigInt.asUintN points); X86ProcessorFault and
 * UnsupportedX86Instruction ride the contracts.h throw channel
 * (installed on the sparse memory by the CPU run). The decoder works
 * on the concrete sparse memory (fetchByte/scalars live there).
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_X86_DECODER_H
#define QA_GUEST_X86_DECODER_H

#include "qa/guest/core/contracts.h"
#include "qa/guest/core/memory.h"

typedef enum qa_x86_repeat { QA_X86_REPEAT_NONE = 0, QA_X86_REPEAT_F2, QA_X86_REPEAT_F3 } qa_x86_repeat;
const char *qa_x86_repeat_name(qa_x86_repeat repeat);
const char *qa_guest_segment_name(qa_guest_segment_index segment);

typedef struct qa_x86_operand {
  bool is_memory;
  /* register */
  qa_guest_register reg;
  bool high_byte;
  int index;
  /* memory */
  uint64_t offset;
  qa_guest_segment_index segment;
  bool stack_pointer_base;
} qa_x86_operand;

typedef struct qa_x86_modrm {
  int byte;
  int group;
  qa_x86_operand register_operand; /* TS `register` */
  qa_x86_operand operand;
} qa_x86_modrm;

qa_guest_address qa_x86_guest_address(qa_sparse_guest_memory *memory, uint64_t raw,
                                      qa_guest_access access, size_t length);
qa_x86_operand qa_x86_register_operand(int index, int width);

typedef struct qa_x86_decoder {
  qa_guest_processor_state *state;
  qa_sparse_guest_memory *memory;
  uint64_t start;
  uint8_t bytes[15];
  size_t byte_count;
  uint64_t cursor;
  int operand_bits; /* 16 | 32 */
  int address_bits; /* 16 | 32 */
  bool has_segment;
  qa_guest_segment_index segment;
  qa_x86_repeat repeat;
  bool lock;
  int opcode;
} qa_x86_decoder;

/* Constructor: consumes prefixes and the opcode (may throw). */
void qa_x86_decoder_init(qa_x86_decoder *decoder, qa_guest_processor_state *state, qa_sparse_guest_memory *memory);
int qa_x86_decoder_byte(qa_x86_decoder *decoder);
uint64_t qa_x86_decoder_immediate(qa_x86_decoder *decoder, int width); /* width: 8|16|32 */
int64_t qa_x86_decoder_signed(qa_x86_decoder *decoder, int width);
uint64_t qa_x86_decoder_register_value(qa_x86_decoder *decoder, int index, int width); /* 16|32 */
qa_x86_modrm qa_x86_decoder_modrm(qa_x86_decoder *decoder, int width);
qa_guest_address qa_x86_decoder_address(qa_x86_decoder *decoder, const qa_x86_operand *operand,
                                        size_t width_bytes, qa_guest_access access);
uint64_t qa_x86_decoder_read(qa_x86_decoder *decoder, const qa_x86_operand *operand, int width);
void qa_x86_decoder_check_write(qa_x86_decoder *decoder, const qa_x86_operand *operand, int width);
void qa_x86_decoder_write(qa_x86_decoder *decoder, const qa_x86_operand *operand, int width, uint64_t value);

#endif
