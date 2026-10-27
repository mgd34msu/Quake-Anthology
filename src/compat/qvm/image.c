/* Port of src/compat/qvm/image.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"

#define QVM_MAGIC 0x12721444
#define HEADER_LENGTH 32
#define MAX_DATA_LENGTH 0x40000000LL
#define MAX_CODE_LENGTH 0x1fffffffLL

noreturn void qa_qvm_header_error(const char *source) {
  fprintf(stderr, "QvmHeaderError: %s has bad header\n", source != NULL ? source : "<qvm>");
  abort();
}

static char *dup_str(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p != NULL) memcpy(p, s, n);
  return p;
}

bool qa_qvm_read_source_header(const uint8_t *bytes, size_t length, const char *source,
                               qa_qvm_source_header *out) {
  const char *src = source != NULL ? source : "<qvm>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, src)) return false;
  int32_t magic = qa_binary_reader_i32(&reader);
  qa_binary_reader_skip(&reader, 8);
  int32_t code_length = qa_binary_reader_i32(&reader);
  int32_t data_offset = qa_binary_reader_i32(&reader);
  int32_t data_length = qa_binary_reader_i32(&reader);
  int32_t literal_length = qa_binary_reader_i32(&reader);
  int32_t bss_length = qa_binary_reader_i32(&reader);
  qa_binary_reader_destroy(&reader);
  if (magic != QVM_MAGIC || bss_length < 0 || data_length < 0 || literal_length < 0 ||
      code_length <= 0)
    qa_qvm_header_error(src);
  out->data_offset = data_offset;
  out->data_length = data_length;
  out->literal_length = literal_length;
  out->bss_length = bss_length;
  return true;
}

bool qa_qvm_parse_restart(const uint8_t *bytes, size_t length, const char *source,
                          qa_qvm_data_image *out) {
  const char *src = source != NULL ? source : "<qvm>";
  qa_qvm_source_header header;
  if (!qa_qvm_read_source_header(bytes, length, src, &header)) return false;
  int64_t initialized_length = header.data_length + header.literal_length;
  int64_t total = initialized_length + header.bss_length;
  if (total > MAX_DATA_LENGTH)
    qa_binary_error(src, 28, "QVM data exceeds positive signed power-of-two allocation range");
  int64_t allocated = 1;
  while (allocated < total) allocated *= 2;
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, src)) return false;
  qa_binary_reader_seek(&reader, header.data_offset);
  uint8_t *data = qa_binary_reader_bytes(&reader, initialized_length);
  qa_binary_reader_destroy(&reader);
  if (data == NULL && initialized_length > 0) return false;
  out->initialized_data = data;
  out->initialized_length = (size_t)initialized_length;
  out->allocated_data_length = allocated;
  return true;
}

static int32_t read_word(qa_binary_reader *reader, int64_t code_end) {
  if ((int64_t)qa_binary_reader_offset(reader) > code_end - 4)
    qa_binary_error(reader->source, (int64_t)qa_binary_reader_offset(reader),
                    "QVM word operand exceeds code section");
  return qa_binary_reader_i32(reader);
}

static qa_qvm_instruction decode_instruction(qa_binary_reader *reader, int64_t code_offset,
                                             int64_t code_end, int64_t instruction_count) {
  int64_t byte_offset = (int64_t)qa_binary_reader_offset(reader) - code_offset;
  if ((int64_t)qa_binary_reader_offset(reader) >= code_end)
    qa_binary_error(reader->source, (int64_t)qa_binary_reader_offset(reader),
                    "QVM instruction exceeds code section");
  uint8_t opcode = qa_binary_reader_u8(reader);
  qa_qvm_instruction result = {(qa_qvm_opcode)opcode, byte_offset, 0, 0};
  switch ((qa_qvm_opcode)opcode) {
    case QA_QVM_OP_EQ: case QA_QVM_OP_NE: case QA_QVM_OP_LTI: case QA_QVM_OP_LEI:
    case QA_QVM_OP_GTI: case QA_QVM_OP_GEI: case QA_QVM_OP_LTU: case QA_QVM_OP_LEU:
    case QA_QVM_OP_GTU: case QA_QVM_OP_GEU: case QA_QVM_OP_EQF: case QA_QVM_OP_NEF:
    case QA_QVM_OP_LTF: case QA_QVM_OP_LEF: case QA_QVM_OP_GTF: case QA_QVM_OP_GEF: {
      int32_t operand = read_word(reader, code_end);
      if (operand < 0 || operand >= instruction_count)
        qa_binary_error(reader->source, (int64_t)qa_binary_reader_offset(reader) - 4,
                        "QVM branch target %d outside instruction table", (int)operand);
      result.operand_width = 4;
      result.operand = operand;
      return result;
    }
    case QA_QVM_OP_ENTER: case QA_QVM_OP_LEAVE: case QA_QVM_OP_CONST:
    case QA_QVM_OP_LOCAL: case QA_QVM_OP_BLOCK_COPY:
      result.operand_width = 4;
      result.operand = read_word(reader, code_end);
      return result;
    case QA_QVM_OP_ARG:
      if ((int64_t)qa_binary_reader_offset(reader) >= code_end)
        qa_binary_error(reader->source, (int64_t)qa_binary_reader_offset(reader),
                        "QVM byte operand exceeds code section");
      result.operand_width = 1;
      result.operand = qa_binary_reader_u8(reader);
      return result;
    case QA_QVM_OP_UNDEF: case QA_QVM_OP_IGNORE: case QA_QVM_OP_BREAK:
    case QA_QVM_OP_CALL: case QA_QVM_OP_PUSH: case QA_QVM_OP_POP: case QA_QVM_OP_JUMP:
    case QA_QVM_OP_LOAD1: case QA_QVM_OP_LOAD2: case QA_QVM_OP_LOAD4:
    case QA_QVM_OP_STORE1: case QA_QVM_OP_STORE2: case QA_QVM_OP_STORE4:
    case QA_QVM_OP_SEX8: case QA_QVM_OP_SEX16: case QA_QVM_OP_NEGI:
    case QA_QVM_OP_ADD: case QA_QVM_OP_SUB: case QA_QVM_OP_DIVI: case QA_QVM_OP_DIVU:
    case QA_QVM_OP_MODI: case QA_QVM_OP_MODU: case QA_QVM_OP_MULI: case QA_QVM_OP_MULU:
    case QA_QVM_OP_BAND: case QA_QVM_OP_BOR: case QA_QVM_OP_BXOR: case QA_QVM_OP_BCOM:
    case QA_QVM_OP_LSH: case QA_QVM_OP_RSHI: case QA_QVM_OP_RSHU:
    case QA_QVM_OP_NEGF: case QA_QVM_OP_ADDF: case QA_QVM_OP_SUBF:
    case QA_QVM_OP_DIVF: case QA_QVM_OP_MULF: case QA_QVM_OP_CVIF: case QA_QVM_OP_CVFI:
      return result;
    default:
      qa_binary_error(reader->source, (int64_t)qa_binary_reader_offset(reader) - 1,
                      "unknown QVM opcode %u", (unsigned)opcode);
  }
}

static void check_section(qa_binary_reader *reader, int64_t offset, int64_t length,
                          int64_t field_offset) {
  if (offset < HEADER_LENGTH || length < 0 ||
      offset > (int64_t)qa_binary_reader_length(reader) - length)
    qa_binary_error(reader->source, field_offset, "invalid QVM section at %lld with length %lld",
                    (long long)offset, (long long)length);
}

bool qa_qvm_parse(const uint8_t *bytes, size_t length, const char *source, qa_qvm_image *out) {
  const char *src = source != NULL ? source : "<qvm>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, src)) return false;
  if (qa_binary_reader_i32(&reader) != QVM_MAGIC) qa_binary_error(src, 0, "invalid QVM magic");
  int32_t instruction_count = qa_binary_reader_i32(&reader);
  int32_t code_offset = qa_binary_reader_i32(&reader);
  int32_t code_length = qa_binary_reader_i32(&reader);
  int32_t data_offset = qa_binary_reader_i32(&reader);
  int32_t data_length = qa_binary_reader_i32(&reader);
  int32_t literal_length = qa_binary_reader_i32(&reader);
  int32_t bss_length = qa_binary_reader_i32(&reader);
  if (code_length <= 0 || code_length > MAX_CODE_LENGTH)
    qa_binary_error(src, 12, "QVM code length outside source allocation range");
  if (instruction_count <= 0 || instruction_count > code_length)
    qa_binary_error(src, 4, "QVM instruction count outside code bounds");
  if (data_length < 0 || data_length % 4 != 0)
    qa_binary_error(src, 20, "QVM data length must contain complete words");
  if (literal_length < 0) qa_binary_error(src, 24, "negative QVM literal length");
  if (bss_length < 0) qa_binary_error(src, 28, "negative QVM BSS length");
  int64_t initialized_length = (int64_t)data_length + literal_length;
  int64_t total = initialized_length + bss_length;
  if (total > MAX_DATA_LENGTH)
    qa_binary_error(src, 28, "QVM data exceeds positive signed power-of-two allocation range");
  check_section(&reader, code_offset, code_length, 8);
  check_section(&reader, data_offset, initialized_length, 16);
  if (initialized_length > 0 && code_offset < (int64_t)data_offset + initialized_length &&
      data_offset < (int64_t)code_offset + code_length)
    qa_binary_error(src, 16, "QVM code and initialized data sections overlap");
  int64_t allocated = 1;
  while (allocated < total) allocated *= 2;
  qa_qvm_instruction *instructions =
      malloc(sizeof(*instructions) * (size_t)instruction_count);
  if (instructions == NULL) { qa_binary_reader_destroy(&reader); return false; }
  qa_binary_reader_seek(&reader, code_offset);
  for (int32_t i = 0; i < instruction_count; i++)
    instructions[i] = decode_instruction(&reader, code_offset,
                                         (int64_t)code_offset + code_length, instruction_count);
  qa_binary_reader_seek(&reader, data_offset);
  uint8_t *data = qa_binary_reader_bytes(&reader, initialized_length);
  qa_binary_reader_destroy(&reader);
  if (data == NULL && initialized_length > 0) { free(instructions); return false; }
  char *source_copy = dup_str(src);
  if (source_copy == NULL) { free(instructions); free(data); return false; }
  out->initialized_data = data;
  out->initialized_length = (size_t)initialized_length;
  out->allocated_data_length = allocated;
  out->source = source_copy;
  out->instructions = instructions;
  out->instruction_count = (size_t)instruction_count;
  out->code_offset = code_offset;
  out->code_length = code_length;
  out->data_length = data_length;
  out->literal_length = literal_length;
  out->bss_length = bss_length;
  out->data_mask = allocated - 1;
  return true;
}

void qa_qvm_data_image_destroy(qa_qvm_data_image *image) {
  if (image == NULL) return;
  free(image->initialized_data);
  image->initialized_data = NULL;
  image->initialized_length = 0;
}

void qa_qvm_image_destroy(qa_qvm_image *image) {
  if (image == NULL) return;
  free(image->initialized_data);
  free(image->source);
  free(image->instructions);
  image->initialized_data = NULL;
  image->source = NULL;
  image->instructions = NULL;
  image->instruction_count = 0;
  image->initialized_length = 0;
}
