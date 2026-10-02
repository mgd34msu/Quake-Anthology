#include "internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define QVM_MAGIC UINT32_C(0x12721444)
#define QVM_MAX_MEMORY UINT32_C(0x40000000)
#define QVM_MAX_CODE UINT32_C(0x1fffffff)

bool qa_qvm_error(qa_error *error, qa_status code, size_t offset, const char *message)
{
    qa_error_set(error,code,offset,"%s",message);
    return false;
}

static bool section(qa_bytes bytes, int32_t offset, uint64_t length, size_t field, qa_error *error)
{
    return (offset >= 32 && (uint32_t)offset <= bytes.size && length <= bytes.size - (uint32_t)offset)
        || qa_qvm_error(error,QA_ERROR_FORMAT,field,"invalid QVM section");
}

static uint8_t operand_width(uint8_t opcode)
{
    if (opcode == QA_QVM_ARG) return 1;
    if ((opcode >= QA_QVM_EQ && opcode <= QA_QVM_GEF)
        || opcode == QA_QVM_ENTER || opcode == QA_QVM_LEAVE || opcode == QA_QVM_CONST
        || opcode == QA_QVM_LOCAL || opcode == QA_QVM_BLOCK_COPY) return 4;
    return 0;
}

static bool source_header(qa_bytes bytes, int32_t *data_offset, size_t *initialized,
                          size_t *memory, qa_error *error)
{
    if (bytes.data == NULL || bytes.size < 32)
        return qa_qvm_error(error,QA_ERROR_FORMAT,0,"truncated QVM header");
    int32_t code_length = qa_load_i32le(bytes.data + 12);
    int32_t data_length = qa_load_i32le(bytes.data + 20);
    int32_t literal_length = qa_load_i32le(bytes.data + 24);
    int32_t bss_length = qa_load_i32le(bytes.data + 28);
    if (qa_load_u32le(bytes.data) != QVM_MAGIC || code_length <= 0 || data_length < 0 || literal_length < 0 || bss_length < 0)
        return qa_qvm_error(error,QA_ERROR_FORMAT,0,"invalid QVM source header");
    uint64_t total = (uint64_t)(uint32_t)data_length + (uint32_t)literal_length + (uint32_t)bss_length;
    if (total > QVM_MAX_MEMORY)
        return qa_qvm_error(error,QA_ERROR_FORMAT,28,"QVM memory exceeds positive signed allocation range");
    *data_offset = qa_load_i32le(bytes.data + 16);
    *initialized = (size_t)data_length + (size_t)literal_length;
    *memory = 1;
    while (*memory < total) *memory *= 2;
    return true;
}

bool qa_qvm_image_load(qa_bytes bytes, qa_qvm_image **out, qa_error *error)
{
    if (out == NULL) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"missing QVM image output");
    int32_t data_offset;
    size_t initialized, memory;
    if (!source_header(bytes,&data_offset,&initialized,&memory,error)) return false;
    int32_t instruction_count = qa_load_i32le(bytes.data + 4), code_offset = qa_load_i32le(bytes.data + 8);
    int32_t code_length = qa_load_i32le(bytes.data + 12), data_length = qa_load_i32le(bytes.data + 20);
    if (code_length > (int32_t)QVM_MAX_CODE || instruction_count <= 0 || instruction_count > code_length)
        return qa_qvm_error(error,QA_ERROR_FORMAT,4,"invalid QVM code or instruction count");
    if (data_length % 4 != 0) return qa_qvm_error(error,QA_ERROR_FORMAT,20,"QVM initialized words are incomplete");
    if (!section(bytes,code_offset,(uint32_t)code_length,8,error) || !section(bytes,data_offset,initialized,16,error)) return false;
    if (initialized > 0 && (uint64_t)(uint32_t)code_offset < (uint64_t)(uint32_t)data_offset + initialized
        && (uint64_t)(uint32_t)data_offset < (uint64_t)(uint32_t)code_offset + (uint32_t)code_length)
        return qa_qvm_error(error,QA_ERROR_FORMAT,16,"QVM code and initialized memory overlap");
    if ((size_t)instruction_count > SIZE_MAX / sizeof(qa_qvm_instruction))
        return qa_qvm_error(error,QA_ERROR_MEMORY,4,"QVM instruction table is too large");
    qa_qvm_image *image = calloc(1,sizeof(*image));
    if (image == NULL) return qa_qvm_error(error,QA_ERROR_MEMORY,0,"allocating QVM image");
    image->references = 1;
    image->instructions = calloc((size_t)instruction_count,sizeof(*image->instructions));
    if (initialized > 0) image->initialized.data = malloc(initialized);
    if (image->instructions == NULL || (initialized > 0 && image->initialized.data == NULL)) {
        qa_qvm_image_release(image);
        return qa_qvm_error(error,QA_ERROR_MEMORY,0,"allocating QVM image records");
    }
    size_t position = (size_t)code_offset, end = position + (size_t)code_length;
    for (size_t i = 0; i < (size_t)instruction_count; ++i) {
        if (position >= end) { qa_qvm_error(error,QA_ERROR_FORMAT,position,"QVM instructions exceed code section"); goto failed; }
        qa_qvm_instruction instruction = {.byte_offset = (uint32_t)(position - (size_t)code_offset), .opcode = bytes.data[position++]};
        if (instruction.opcode > QA_QVM_CVFI) { qa_qvm_error(error,QA_ERROR_FORMAT,position - 1,"unknown QVM opcode"); goto failed; }
        instruction.operand_width = operand_width(instruction.opcode);
        if (instruction.operand_width > end - position) { qa_qvm_error(error,QA_ERROR_FORMAT,position,"truncated QVM operand"); goto failed; }
        if (instruction.operand_width == 4) instruction.operand = qa_load_i32le(bytes.data + position);
        else if (instruction.operand_width == 1) instruction.operand = bytes.data[position];
        position += instruction.operand_width;
        if (instruction.opcode >= QA_QVM_EQ && instruction.opcode <= QA_QVM_GEF
            && (instruction.operand < 0 || instruction.operand >= instruction_count)) {
            qa_qvm_error(error,QA_ERROR_FORMAT,position - 4,"QVM branch target outside instruction table"); goto failed;
        }
        image->instructions[i] = instruction;
    }
    image->instruction_count = (size_t)instruction_count;
    image->code_length = (uint32_t)code_length;
    image->data_length = (uint32_t)data_length;
    image->literal_length = qa_load_u32le(bytes.data + 24);
    image->bss_length = qa_load_u32le(bytes.data + 28);
    image->memory_size = memory;
    image->initialized.size = initialized;
    if (initialized > 0) memcpy(image->initialized.data,bytes.data + data_offset,initialized);
    qa_sha256(bytes,&image->digest);
    *out = image;
    return true;
failed:
    qa_qvm_image_release(image);
    return false;
}

bool qa_qvm_parse_restart(qa_bytes bytes, qa_buffer *out, size_t *memory_size, qa_error *error)
{
    if (out == NULL || memory_size == NULL) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"missing restart output");
    int32_t offset;
    size_t initialized, memory;
    if (!source_header(bytes,&offset,&initialized,&memory,error)) return false;
    /* Restart keeps prepared code and does not inspect replacement opcodes. */
    if (offset < 0 || (uint32_t)offset > bytes.size || initialized > bytes.size - (uint32_t)offset)
        return qa_qvm_error(error,QA_ERROR_FORMAT,16,"invalid QVM restart data");
    qa_buffer buffer = {NULL,initialized};
    if (initialized > 0) {
        buffer.data = malloc(initialized);
        if (buffer.data == NULL) return qa_qvm_error(error,QA_ERROR_MEMORY,0,"allocating restart data");
        memcpy(buffer.data,bytes.data + offset,initialized);
    }
    *out = buffer; *memory_size = memory;
    return true;
}

void qa_qvm_image_retain(qa_qvm_image *image)
{
    if (image != NULL) ++image->references;
}

void qa_qvm_image_release(qa_qvm_image *image)
{
    if (image == NULL || --image->references != 0) return;
    free(image->instructions); qa_buffer_free(&image->initialized); free(image);
}

const qa_sha256_digest *qa_qvm_image_digest(const qa_qvm_image *image) { return image == NULL ? NULL : &image->digest; }
const qa_qvm_instruction *qa_qvm_image_instructions(const qa_qvm_image *image, size_t *count)
{
    if (count != NULL) *count = image == NULL ? 0 : image->instruction_count;
    return image == NULL ? NULL : image->instructions;
}
size_t qa_qvm_image_memory_size(const qa_qvm_image *image) { return image == NULL ? 0 : image->memory_size; }
qa_bytes qa_qvm_image_initialized_data(const qa_qvm_image *image)
{
    return image ? (qa_bytes){image->initialized.data, image->initialized.size} : (qa_bytes){0};
}

bool qa_qvm_qualify_global_word(const qa_qvm_image *image, uint32_t offset, qa_error *error)
{
    uint64_t end = image ? (uint64_t)image->data_length + image->literal_length + image->bss_length : 0;
    return (image && !(offset & 3) && (uint64_t)offset + 4 <= end) ||
        qa_qvm_error(error, QA_ERROR_ARGUMENT, offset, "QVM source global word leaves immutable data/literal/BSS extent");
}

bool qa_qvm_qualify_source_span(const qa_qvm_image *image, uint32_t offset,
    size_t length, qa_error *error)
{
    uint64_t end = image ? (uint64_t)image->data_length + image->literal_length + image->bss_length : 0;
    return (image && offset <= end && length <= end - offset) ||
        qa_qvm_error(error, QA_ERROR_ARGUMENT, offset, "QVM source byte span leaves immutable data/literal/BSS extent");
}

bool qa_qvm_qualify_evaluation_stack(const qa_qvm_image *image,
    const qa_qvm_evaluation_stack *stack, qa_error *error)
{
    uint64_t initialized = image ? (uint64_t)image->data_length + image->literal_length : 0;
    return (image && stack && !((stack->floor | stack->top) & 3) &&
        stack->floor >= initialized && stack->floor < stack->top && stack->top <= image->memory_size) ||
        qa_qvm_error(error, QA_ERROR_ARGUMENT, 0, "QVM declared evaluation stack leaves aligned original allocation");
}
