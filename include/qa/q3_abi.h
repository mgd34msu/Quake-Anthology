#ifndef QA_Q3_ABI_H
#define QA_Q3_ABI_H

#include "qa/qvm.h"

/* Source records contain fixed-width words, including in native modules.
 * The reader borrows an admitted byte view. Writes retain source field order;
 * a QVM writer can therefore publish its ordinary memory observations. Offsets
 * are relative to bytes, never host or module pointers. */
typedef struct qa_q3_abi_record {
    qa_qvm_abi abi;
    qa_bytes bytes;
    void *context;
    bool (*write)(void *, size_t offset, qa_bytes, qa_error *);
} qa_q3_abi_record;

bool qa_q3_abi_read_entity(qa_q3_abi_record *, size_t, bool source_tags,
                            qa_q3_entity *, qa_error *);
bool qa_q3_abi_write_entity(qa_q3_abi_record *, size_t, bool source_tags,
                             const qa_q3_entity *, qa_error *);
bool qa_q3_abi_read_player(qa_q3_abi_record *, size_t, bool source_tags,
                            qa_q3_player *, qa_error *);
bool qa_q3_abi_write_player(qa_q3_abi_record *, size_t, bool source_tags,
                             bool preserve_private, const qa_q3_player *, qa_error *);
bool qa_q3_abi_read_usercmd(qa_q3_abi_record *, size_t, qa_q3_usercmd *, qa_error *);
bool qa_q3_abi_write_usercmd(qa_q3_abi_record *, size_t, bool preserve_private,
                              const qa_q3_usercmd *, qa_error *);
bool qa_q3_abi_write_gamestate(qa_q3_abi_record *, size_t, bool source_tags,
                                const qa_q3_gamestate *, qa_error *);
bool qa_q3_abi_write_snapshot(qa_q3_abi_record *, size_t, bool source_tags,
                               const qa_q3_snapshot *, int32_t ping, qa_error *);
bool qa_q3_abi_write_trace(qa_q3_abi_record *, size_t, const qa_trace_result *,
                            int32_t entity_number, qa_error *);
bool qa_q3_abi_read_shared_entity(qa_q3_abi_record *, size_t,
                                   qa_qvm_entity_shared *, qa_error *);
bool qa_q3_abi_write_shared_entity(qa_q3_abi_record *, size_t,
                                    const qa_qvm_entity_shared *, qa_error *);

/* Pointer normalization belongs to the executor. All remaining operations use
 * admitted addresses, preserving aliased reads and each source write boundary. */
typedef struct qa_q3_abi_memory {
    void *context;
    bool (*span)(void *, uint64_t raw, size_t, uint64_t *address, qa_error *);
    bool (*read)(void *, uint64_t, void *, size_t, qa_error *);
    bool (*write)(void *, uint64_t, qa_bytes, qa_error *);
    bool (*copy)(void *, uint64_t destination, uint64_t source, size_t, qa_error *);
    bool (*fill)(void *, uint64_t, size_t, uint8_t, qa_error *);
    bool (*string_length)(void *, uint64_t, size_t limit, size_t *, bool *terminated, qa_error *);
} qa_q3_abi_memory;
bool qa_q3_abi_intrinsic_signature(qa_qvm_role, qa_qvm_abi, int32_t trap,
                                    size_t *argument_count, uint32_t *pointer_mask,
                                    bool *address_result);
bool qa_q3_abi_intrinsic(qa_qvm_role, qa_qvm_abi, int32_t trap,
                          const uint64_t *arguments, size_t argument_count,
                          const qa_q3_abi_memory *, uint64_t *result, qa_error *);

#endif
