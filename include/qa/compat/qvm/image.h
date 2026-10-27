/* Port of src/compat/qvm/image.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_IMAGE_H
#define QA_COMPAT_QVM_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

#define QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS 62

typedef enum qa_qvm_opcode {
  QA_QVM_OP_UNDEF, QA_QVM_OP_IGNORE, QA_QVM_OP_BREAK,
  QA_QVM_OP_ENTER, QA_QVM_OP_LEAVE, QA_QVM_OP_CALL, QA_QVM_OP_PUSH, QA_QVM_OP_POP,
  QA_QVM_OP_CONST, QA_QVM_OP_LOCAL, QA_QVM_OP_JUMP,
  QA_QVM_OP_EQ, QA_QVM_OP_NE, QA_QVM_OP_LTI, QA_QVM_OP_LEI, QA_QVM_OP_GTI, QA_QVM_OP_GEI,
  QA_QVM_OP_LTU, QA_QVM_OP_LEU, QA_QVM_OP_GTU, QA_QVM_OP_GEU,
  QA_QVM_OP_EQF, QA_QVM_OP_NEF, QA_QVM_OP_LTF, QA_QVM_OP_LEF, QA_QVM_OP_GTF, QA_QVM_OP_GEF,
  QA_QVM_OP_LOAD1, QA_QVM_OP_LOAD2, QA_QVM_OP_LOAD4,
  QA_QVM_OP_STORE1, QA_QVM_OP_STORE2, QA_QVM_OP_STORE4,
  QA_QVM_OP_ARG, QA_QVM_OP_BLOCK_COPY, QA_QVM_OP_SEX8, QA_QVM_OP_SEX16,
  QA_QVM_OP_NEGI, QA_QVM_OP_ADD, QA_QVM_OP_SUB, QA_QVM_OP_DIVI, QA_QVM_OP_DIVU,
  QA_QVM_OP_MODI, QA_QVM_OP_MODU, QA_QVM_OP_MULI, QA_QVM_OP_MULU,
  QA_QVM_OP_BAND, QA_QVM_OP_BOR, QA_QVM_OP_BXOR, QA_QVM_OP_BCOM,
  QA_QVM_OP_LSH, QA_QVM_OP_RSHI, QA_QVM_OP_RSHU, QA_QVM_OP_NEGF,
  QA_QVM_OP_ADDF, QA_QVM_OP_SUBF, QA_QVM_OP_DIVF, QA_QVM_OP_MULF,
  QA_QVM_OP_CVIF, QA_QVM_OP_CVFI
} qa_qvm_opcode;

/* TS QvmInstruction: operand present iff operand_width != 0 (4 or 1). */
typedef struct qa_qvm_instruction {
  qa_qvm_opcode opcode;
  int64_t byte_offset;
  int operand_width; /* 0 | 4 | 1 */
  int32_t operand;
} qa_qvm_instruction;

typedef struct qa_qvm_data_image {
  uint8_t *initialized_data; /* owned; NULL when length is 0 */
  size_t initialized_length;
  int64_t allocated_data_length;
} qa_qvm_data_image;

typedef struct qa_qvm_image {
  uint8_t *initialized_data; /* owned */
  size_t initialized_length;
  int64_t allocated_data_length;
  char *source; /* owned */
  qa_qvm_instruction *instructions; /* owned */
  size_t instruction_count;
  int64_t code_offset, code_length, data_length, literal_length, bss_length, data_mask;
} qa_qvm_image;

/* TS QvmSourceHeader (return of readQvmSourceHeader). */
typedef struct qa_qvm_source_header {
  int64_t data_offset, data_length, literal_length, bss_length;
} qa_qvm_source_header;

/* TS `throw new QvmHeaderError(source)`: prints the Error string form and aborts. */
noreturn void qa_qvm_header_error(const char *source);

/* `source` NULL means "<qvm>" where the TS default applies. False only on OOM. */
bool qa_qvm_read_source_header(const uint8_t *bytes, size_t length, const char *source,
                               qa_qvm_source_header *out);
bool qa_qvm_parse_restart(const uint8_t *bytes, size_t length, const char *source,
                          qa_qvm_data_image *out);
bool qa_qvm_parse(const uint8_t *bytes, size_t length, const char *source, qa_qvm_image *out);
void qa_qvm_data_image_destroy(qa_qvm_data_image *image);
void qa_qvm_image_destroy(qa_qvm_image *image);

#endif
