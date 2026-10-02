#ifndef QA_QVM_INTERNAL_H
#define QA_QVM_INTERNAL_H

#include "qa/qvm.h"
#include "qa/binary.h"

struct qa_qvm_image {
    size_t references, instruction_count, memory_size;
    uint32_t code_length, data_length, literal_length, bss_length;
    qa_qvm_instruction *instructions;
    qa_buffer initialized;
    qa_sha256_digest digest;
};
struct qa_qvm_write_watch;
struct qa_qvm {
    qa_qvm_image *image;
    qa_qvm_options options;
    uint8_t *data;
    size_t data_size;
    uint32_t data_mask;
    uint64_t write_sequence, next_watch;
    struct qa_qvm_write_watch *watches;
    unsigned publication_depth, write_delivery_depth, lifecycle_depth;
    uint32_t api_version;
    bool retired;
    void *execution; /* Executor owns all invocation, operand and hook state. */
};

bool qa_qvm_error(qa_error *, qa_status, size_t, const char *);
bool qa_qvm_live(const qa_qvm *, qa_error *);
bool qa_qvm_mutable(const qa_qvm *, qa_error *);
bool qa_qvm_raw_range(const qa_qvm *, uint32_t, size_t, qa_error *);
bool qa_qvm_parse_restart(qa_bytes, qa_buffer *, size_t *, qa_error *);
bool qa_qvm_execution_create(qa_qvm *, qa_error *);
void qa_qvm_execution_destroy(qa_qvm *);
void qa_qvm_execution_reset(qa_qvm *);
void qa_qvm_execution_checkpoint(const qa_qvm *, uint64_t values[3]);
bool qa_qvm_execution_checkpoint_ready(const qa_qvm *, const uint64_t values[3], bool candidate, qa_error *);
void qa_qvm_execution_restore(qa_qvm *, const uint64_t values[3], bool candidate);
struct qa_qvm_saved_function;
struct qa_qvm_saved_resolver;
bool qa_qvm_execution_restore_bindings(qa_qvm *, uint64_t generation,
    const struct qa_qvm_saved_function *, const qa_qvm_binding *, size_t, qa_error *);
bool qa_qvm_execution_restore_callbacks(qa_qvm *, uint64_t generation,
    const struct qa_qvm_saved_function *, const qa_qvm_binding *, size_t,
    const struct qa_qvm_saved_resolver *, qa_qvm_binding, qa_error *);
bool qa_qvm_execution_active(const qa_qvm *);
bool qa_qvm_execution_reentry(const qa_qvm *, qa_error *);
bool qa_qvm_execution_token(const qa_qvm_call *, qa_error *);
bool qa_qvm_execution_source_callback(const qa_qvm_call *, const qa_qvm_image *,
    int32_t, const int32_t *, size_t, int32_t *, qa_error *);
bool qa_qvm_execution_source_scratch(const qa_qvm_call *, const qa_qvm_image *,
    size_t, qa_qvm_source_scratch_fn, void *, qa_error *);
bool qa_qvm_execution_source_frame(const qa_qvm_call *, const qa_qvm_image *,
    qa_qvm_source_frame *, qa_error *);
bool qa_qvm_execution_source_word(const qa_qvm_call *, const qa_qvm_image *,
    uint32_t, int32_t, qa_qvm_source_word_fn, void *, qa_error *);
/* Cleanup of a qualified active source scratch lease. RAM restoration commits
 * before fallible publication and still commits when delivery cannot allocate. */
bool qa_qvm_memory_restore_scratch(qa_qvm *, uint32_t, qa_bytes, qa_error *);
bool qa_qvm_memory_restore_words(qa_qvm *, const qa_qvm_source_word *, size_t, bool, qa_error *);
bool qa_qvm_execution_words_begin(qa_qvm *, const qa_qvm_image *,
    const qa_qvm_source_word *, size_t, qa_qvm_word_projection **, qa_error *);
bool qa_qvm_execution_words_capture(qa_qvm *, const qa_qvm_image *,
    const uint32_t *, size_t, qa_qvm_word_projection **, qa_error *);
bool qa_qvm_execution_words_begin_observed(qa_qvm *, const qa_qvm_image *,
    const qa_qvm_source_word *, size_t, qa_qvm_word_projection **, qa_error *);
bool qa_qvm_execution_words_capture_observed(qa_qvm *, const qa_qvm_image *,
    const uint32_t *, size_t, qa_qvm_word_projection **, qa_error *);
bool qa_qvm_execution_words_end(qa_qvm_word_projection **, bool, qa_error *);
bool qa_qvm_execution_words_is_last(const qa_qvm_word_projection *);
bool qa_qvm_execution_source_returned(const qa_qvm *);
bool qa_qvm_execution_source_bytes_write(qa_qvm *, const qa_qvm_image *,
    uint32_t, qa_bytes, qa_error *);
bool qa_qvm_execution_scratch_run(qa_qvm *, const qa_qvm_image *, size_t,
    qa_qvm_source_scratch_run_fn, void *, qa_error *);
bool qa_qvm_execution_scratch_run_reserved(qa_qvm *, const qa_qvm_image *, size_t,
    uint32_t, qa_qvm_source_scratch_run_fn, void *, qa_error *);
typedef bool (*qa_qvm_effect_fn)(void *, qa_error *);
bool qa_qvm_execution_effect(qa_qvm *, qa_qvm_effect_fn, void *, qa_error *);
void qa_qvm_memory_close(qa_qvm *);
/* Executes common intrinsic traps, otherwise delegates to options.syscall. */
bool qa_qvm_dispatch(qa_qvm *, const qa_qvm_call *, int32_t trap, int32_t *, qa_error *);

#endif
