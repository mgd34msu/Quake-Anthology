#include "internal.h"

bool qa_qvm_source_scratch_qualify(const qa_qvm_image *image, size_t length,
    uint32_t *offset, qa_error *error)
{
    if (!image || !offset || !length)
        return qa_qvm_error(error, QA_ERROR_ARGUMENT, 0, "QVM source scratch needs its immutable image and positive extent");
    uint64_t end = (uint64_t)image->data_length + image->literal_length + image->bss_length;
    uint64_t start = (end + 15) & ~UINT64_C(15);
    if (image->memory_size < UINT32_C(65536) || start > UINT32_MAX ||
        start > image->memory_size - UINT32_C(65536) ||
        length > image->memory_size - UINT32_C(65536) - start)
        return qa_qvm_error(error, QA_ERROR_ARGUMENT, 0, "QVM source scratch overlaps immutable data or reserved source stack");
    *offset = (uint32_t)start;
    return true;
}

bool qa_qvm_invoke_source_callback(const qa_qvm_call *call, const qa_qvm_image *image,
    int32_t pointer, const int32_t *words, size_t count, int32_t *out, qa_error *error)
{
    return qa_qvm_execution_source_callback(call, image, pointer, words, count, out, error);
}

bool qa_qvm_source_scratch(const qa_qvm_call *call, const qa_qvm_image *image, size_t length,
    qa_qvm_source_scratch_fn perform, void *context, qa_error *error)
{
    return qa_qvm_execution_source_scratch(call, image, length, perform, context, error);
}

bool qa_qvm_call_source_frame(const qa_qvm_call *call, const qa_qvm_image *image,
    qa_qvm_source_frame *out, qa_error *error)
{
    return qa_qvm_execution_source_frame(call, image, out, error);
}

bool qa_qvm_source_global_word(const qa_qvm_call *call, const qa_qvm_image *image,
    uint32_t offset, int32_t value, qa_qvm_source_word_fn perform, void *context, qa_error *error)
{
    return qa_qvm_execution_source_word(call, image, offset, value, perform, context, error);
}
