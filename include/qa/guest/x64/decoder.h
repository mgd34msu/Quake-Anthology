/*
 * Port of src/guest/x64/decoder.ts: canonical addressing, operand
 * access and the X64DecodeCursor. Operand shapes are the plan.h
 * qa_x64_operand (plan.ts/plan.c already mirror this module's types).
 * X64ProcessorFault/X64Unsupported ride the contracts.h throw
 * channel; TS bigint values are uint64_t bit patterns (displacements
 * and immediates sign-extend through int64_t where the TS does).
 * The cursor works on the concrete sparse memory (fetchSequence and
 * retainExecutableBytes live there). GPL-2.0-or-later.
 */
#ifndef QA_GUEST_X64_DECODER_H
#define QA_GUEST_X64_DECODER_H

#include "qa/guest/core/contracts.h"
#include "qa/guest/core/memory.h"
#include "qa/guest/floating_point/binary.h" /* qa_numeric_prefix */
#include "qa/guest/x64/plan.h"

typedef enum qa_x64_repeat { QA_X64_REPEAT_NONE = 0, QA_X64_REPEAT_F2, QA_X64_REPEAT_F3 } qa_x64_repeat;
const char *qa_x64_repeat_name(qa_x64_repeat repeat);

qa_guest_register qa_x64_register_name_at(qa_guest_throw *throw_ctx, int index);

typedef struct qa_x64_modrm {
  int byte;
  int extension;
  int register_index;
  int rm_index;
  qa_x64_operand reg;
  qa_x64_operand rm;
} qa_x64_modrm;

typedef struct qa_x64_decoded_operand_record {
  bool valid;
  int width;
  int end;
  qa_x64_modrm value;
} qa_x64_decoded_operand_record;
typedef struct qa_x64_decoded_immediate_record {
  bool valid;
  int length;
  uint64_t value;
} qa_x64_decoded_immediate_record;

typedef struct qa_x64_decoded_instruction {
  uint8_t bytes[15];
  size_t byte_count;
  int prefix_length;
  int opcode;
  bool has_rex;
  int rex;
  bool operand_override;
  bool address_override;
  bool lock;
  qa_x64_repeat repeat;
  qa_x64_segment_override segment;
  qa_x64_decoded_operand_record operands[15];
  qa_x64_decoded_immediate_record immediates[15];
  qa_guest_executable_guard guard; /* TS `unchanged` closure */
} qa_x64_decoded_instruction;
bool qa_x64_decoded_unchanged(qa_x64_decoded_instruction *decoded);

uint64_t qa_x64_canonical_address(qa_guest_throw *throw_ctx, uint64_t value);
qa_guest_core_address qa_x64_guest_address(qa_sparse_guest_memory *memory, uint64_t raw, qa_guest_access access);
uint64_t qa_x64_read_memory(qa_sparse_guest_memory *memory, qa_guest_core_address address, int width);
void qa_x64_write_memory(qa_sparse_guest_memory *memory, qa_guest_core_address address, int width, uint64_t value);
uint64_t qa_x64_effective_operand_offset(const qa_guest_processor_state *state, const qa_x64_operand *operand, uint64_t next_ip);
qa_guest_core_address qa_x64_operand_address(qa_sparse_guest_memory *memory, const qa_guest_processor_state *state,
                                        const qa_x64_operand *operand, uint64_t next_ip, qa_guest_access access);
uint64_t qa_x64_read_operand(qa_sparse_guest_memory *memory, const qa_guest_processor_state *state,
                             const qa_x64_operand *operand, uint64_t next_ip);
void qa_x64_write_operand(qa_sparse_guest_memory *memory, qa_guest_processor_state *state,
                          const qa_x64_operand *operand, uint64_t next_ip, uint64_t value);
void qa_x64_writable_operand(qa_sparse_guest_memory *memory, const qa_guest_processor_state *state,
                             const qa_x64_operand *operand, uint64_t next_ip);

typedef struct qa_x64_decode_cursor {
  qa_sparse_guest_memory *memory;
  qa_guest_processor_state *state;
  /* public TS fields */
  qa_x64_semantic_plan plan;
  bool has_plan;
  bool has_rex;
  int rex;
  bool operand_override;
  bool address_override;
  bool lock;
  qa_x64_repeat repeat;
  qa_x64_segment_override segment;
  /* private */
  uint64_t start;
  qa_x64_decoded_instruction *decoded; /* borrowed */
  qa_guest_fetch_sequence fetch;
  bool has_fetch;
  uint8_t record_bytes[15];
  size_t record_byte_count;
  qa_x64_decoded_operand_record record_operands[15];
  qa_x64_decoded_immediate_record record_immediates[15];
  int prefix_length;
  int position;
  int opcode_value;
} qa_x64_decode_cursor;

void qa_x64_decode_cursor_init(qa_x64_decode_cursor *cursor, qa_sparse_guest_memory *memory,
                               qa_guest_processor_state *state, qa_x64_decoded_instruction *decoded);
void qa_x64_decode_cursor_reset(qa_x64_decode_cursor *cursor, qa_x64_decoded_instruction *decoded);
uint64_t qa_x64_cursor_start(const qa_x64_decode_cursor *cursor);
int qa_x64_cursor_opcode(const qa_x64_decode_cursor *cursor);
size_t qa_x64_cursor_bytes(const qa_x64_decode_cursor *cursor, uint8_t out[15]);
bool qa_x64_cursor_cache(qa_x64_decode_cursor *cursor, qa_x64_decoded_instruction *out);
uint64_t qa_x64_cursor_next_ip(const qa_x64_decode_cursor *cursor);
int qa_x64_cursor_width(const qa_x64_decode_cursor *cursor);       /* 16|32|64 */
int qa_x64_cursor_stack_width(const qa_x64_decode_cursor *cursor); /* 16|64 */
int qa_x64_cursor_address_bits(const qa_x64_decode_cursor *cursor);
int qa_x64_cursor_rex_b(const qa_x64_decode_cursor *cursor);
int qa_x64_cursor_rex_x(const qa_x64_decode_cursor *cursor);
int qa_x64_cursor_rex_r(const qa_x64_decode_cursor *cursor);
qa_numeric_prefix qa_x64_cursor_numeric_prefix(const qa_x64_decode_cursor *cursor);
int qa_x64_cursor_read_byte(qa_x64_decode_cursor *cursor);
uint64_t qa_x64_cursor_read_unsigned(qa_x64_decode_cursor *cursor, int byte_length);
int64_t qa_x64_cursor_read_signed(qa_x64_decode_cursor *cursor, int byte_length);
uint64_t qa_x64_cursor_immediate(qa_x64_decode_cursor *cursor, int width);
qa_x64_operand qa_x64_cursor_register(qa_x64_decode_cursor *cursor, int index, int width);
qa_x64_modrm qa_x64_cursor_decode_modrm(qa_x64_decode_cursor *cursor, int width);
uint64_t qa_x64_cursor_effective_offset(qa_x64_decode_cursor *cursor, const qa_x64_operand *operand,
                                        bool has_next_ip, uint64_t next_ip);
qa_guest_core_address qa_x64_cursor_address(qa_x64_decode_cursor *cursor, const qa_x64_operand *operand, qa_guest_access access);
uint64_t qa_x64_cursor_read(qa_x64_decode_cursor *cursor, const qa_x64_operand *operand);
void qa_x64_cursor_write(qa_x64_decode_cursor *cursor, const qa_x64_operand *operand, uint64_t value);
void qa_x64_cursor_writable(qa_x64_decode_cursor *cursor, const qa_x64_operand *operand);

#endif
