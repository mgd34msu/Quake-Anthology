/*
 * Port of src/materials/source-state.ts: GLS_* state bits and GL_State
 * change generation. The TS generator becomes a visitor callback.
 * GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_SOURCE_STATE_H
#define QA_MATERIALS_SOURCE_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  QA_SOURCE_STATE_SRCBLEND_ZERO = 0x00000001,
  QA_SOURCE_STATE_SRCBLEND_ONE = 0x00000002,
  QA_SOURCE_STATE_SRCBLEND_DST_COLOR = 0x00000003,
  QA_SOURCE_STATE_SRCBLEND_ONE_MINUS_DST_COLOR = 0x00000004,
  QA_SOURCE_STATE_SRCBLEND_SRC_ALPHA = 0x00000005,
  QA_SOURCE_STATE_SRCBLEND_ONE_MINUS_SRC_ALPHA = 0x00000006,
  QA_SOURCE_STATE_SRCBLEND_DST_ALPHA = 0x00000007,
  QA_SOURCE_STATE_SRCBLEND_ONE_MINUS_DST_ALPHA = 0x00000008,
  QA_SOURCE_STATE_SRCBLEND_ALPHA_SATURATE = 0x00000009,
  QA_SOURCE_STATE_SRCBLEND_BITS = 0x0000000f,
  QA_SOURCE_STATE_DSTBLEND_ZERO = 0x00000010,
  QA_SOURCE_STATE_DSTBLEND_ONE = 0x00000020,
  QA_SOURCE_STATE_DSTBLEND_SRC_COLOR = 0x00000030,
  QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_SRC_COLOR = 0x00000040,
  QA_SOURCE_STATE_DSTBLEND_SRC_ALPHA = 0x00000050,
  QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_SRC_ALPHA = 0x00000060,
  QA_SOURCE_STATE_DSTBLEND_DST_ALPHA = 0x00000070,
  QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_DST_ALPHA = 0x00000080,
  QA_SOURCE_STATE_DSTBLEND_BITS = 0x000000f0,
  QA_SOURCE_STATE_DEPTHMASK_TRUE = 0x00000100,
  QA_SOURCE_STATE_POLYMODE_LINE = 0x00001000,
  QA_SOURCE_STATE_DEPTHTEST_DISABLE = 0x00010000,
  QA_SOURCE_STATE_DEPTHFUNC_EQUAL = 0x00020000,
  QA_SOURCE_STATE_ATEST_GT_0 = 0x10000000,
  QA_SOURCE_STATE_ATEST_LT_80 = 0x20000000,
  QA_SOURCE_STATE_ATEST_GE_80 = 0x40000000,
  QA_SOURCE_STATE_ATEST_BITS = 0x70000000,
  QA_SOURCE_STATE_DEFAULT = QA_SOURCE_STATE_DEPTHMASK_TRUE
};

/* TS SourceStateInput. has_blend false = TS blend null. */
typedef struct qa_source_state_input {
  const char *depth_test; /* "less-equal"|"equal" */
  bool depth_write;
  bool has_blend;
  const char *blend_source, *blend_destination;
  const char *alpha_test;
} qa_source_state_input;

/* TS SourceStateChange. kind/value carry exact TS strings; enabled
 * serves blend and depth-test, bool_value serves depth-write. */
typedef struct qa_source_state_change {
  const char *kind;
  const char *value;
  bool enabled;
  bool bool_value;
  const char *source, *destination;
} qa_source_state_change;

typedef void (*qa_source_state_change_fn)(qa_source_state_change change,
                                          void *ctx);

uint32_t qa_source_state_bits(qa_source_state_input input,
                              const char *polygon_mode,
                              bool depth_test_enabled);
size_t qa_source_state_changes(bool has_previous, uint32_t previous,
                               uint32_t next,
                               qa_source_state_change_fn visit, void *ctx);

#endif
