/* Port of src/bots/behavior/rerelease/data/blockparse.ts. All strings owned
 * by the result; destroy frees. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_DATA_BLOCKPARSE_H
#define QA_BOTS_BEHAVIOR_RERELEASE_DATA_BLOCKPARSE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_block_field { char *key; char **values; size_t value_count; int64_t line; } qa_block_field;
typedef struct qa_block { char **header; size_t header_count; qa_block_field *fields; size_t field_count; int64_t line; } qa_block;
typedef struct qa_block_parse_error { char *message; int64_t line; } qa_block_parse_error;
typedef struct qa_block_parse_result { qa_block *blocks; size_t block_count; qa_block_parse_error *errors; size_t error_count; } qa_block_parse_result;
bool qa_parse_blocks(qa_block_parse_result *out, const char *text);
void qa_block_parse_result_destroy(qa_block_parse_result *r);
/* TS undefined -> false (string) / false with *out untouched. */
bool qa_field_string(const char *const *values, size_t count, const char **out);
bool qa_field_number(const char *const *values, size_t count, double *out);
bool qa_field_bool(const char *const *values, size_t count, bool *out);
/* Borrowed pointers into values; caller frees the array only. */
const char **qa_field_flag_list(const char *const *values, size_t count, size_t *out_count);
#endif
