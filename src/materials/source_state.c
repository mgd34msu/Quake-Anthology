/* Port of src/materials/source-state.ts. GPL-2.0-or-later. */
#include <string.h>

#include "qa/core/common_error.h"
#include "qa/foundation/support.h"
#include "qa/materials/source_state.h"

static bool eq(const char *a, const char *b) {
  return a != NULL && strcmp(a, b) == 0;
}

static uint32_t source_bits(const char *factor) {
  if (eq(factor, "zero")) return QA_SOURCE_STATE_SRCBLEND_ZERO;
  if (eq(factor, "one")) return QA_SOURCE_STATE_SRCBLEND_ONE;
  if (eq(factor, "dst-color")) return QA_SOURCE_STATE_SRCBLEND_DST_COLOR;
  if (eq(factor, "one-minus-dst-color")) return QA_SOURCE_STATE_SRCBLEND_ONE_MINUS_DST_COLOR;
  if (eq(factor, "src-alpha")) return QA_SOURCE_STATE_SRCBLEND_SRC_ALPHA;
  if (eq(factor, "one-minus-src-alpha")) return QA_SOURCE_STATE_SRCBLEND_ONE_MINUS_SRC_ALPHA;
  if (eq(factor, "dst-alpha")) return QA_SOURCE_STATE_SRCBLEND_DST_ALPHA;
  if (eq(factor, "one-minus-dst-alpha")) return QA_SOURCE_STATE_SRCBLEND_ONE_MINUS_DST_ALPHA;
  if (eq(factor, "src-alpha-saturate")) return QA_SOURCE_STATE_SRCBLEND_ALPHA_SATURATE;
  qa_range_error("GL_State cannot encode blend factor '%s'", factor ? factor : "(null)");
}
static uint32_t destination_bits(const char *factor) {
  if (eq(factor, "zero")) return QA_SOURCE_STATE_DSTBLEND_ZERO;
  if (eq(factor, "one")) return QA_SOURCE_STATE_DSTBLEND_ONE;
  if (eq(factor, "src-color")) return QA_SOURCE_STATE_DSTBLEND_SRC_COLOR;
  if (eq(factor, "one-minus-src-color")) return QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_SRC_COLOR;
  if (eq(factor, "src-alpha")) return QA_SOURCE_STATE_DSTBLEND_SRC_ALPHA;
  if (eq(factor, "one-minus-src-alpha")) return QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_SRC_ALPHA;
  if (eq(factor, "dst-alpha")) return QA_SOURCE_STATE_DSTBLEND_DST_ALPHA;
  if (eq(factor, "one-minus-dst-alpha")) return QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_DST_ALPHA;
  qa_range_error("GL_State cannot encode blend factor '%s'", factor ? factor : "(null)");
}
static uint32_t alpha_bits(const char *test) {
  if (eq(test, "none")) return 0;
  if (eq(test, "gt0")) return QA_SOURCE_STATE_ATEST_GT_0;
  if (eq(test, "lt128")) return QA_SOURCE_STATE_ATEST_LT_80;
  if (eq(test, "ge128")) return QA_SOURCE_STATE_ATEST_GE_80;
  qa_range_error("GL_State cannot encode alpha test '%s'", test ? test : "(null)");
}

uint32_t qa_source_state_bits(qa_source_state_input input,
                              const char *polygon_mode,
                              bool depth_test_enabled) {
  uint32_t bits;
  if (eq(input.depth_test, "less-equal")) bits = 0;
  else if (eq(input.depth_test, "equal")) bits = QA_SOURCE_STATE_DEPTHFUNC_EQUAL;
  else qa_range_error("GL_State cannot encode depth function '%s'",
                      input.depth_test ? input.depth_test : "(null)");
  if (input.has_blend)
    bits |= source_bits(input.blend_source) | destination_bits(input.blend_destination);
  if (input.depth_write) bits |= QA_SOURCE_STATE_DEPTHMASK_TRUE;
  if (eq(polygon_mode, "line")) bits |= QA_SOURCE_STATE_POLYMODE_LINE;
  if (!depth_test_enabled) bits |= QA_SOURCE_STATE_DEPTHTEST_DISABLE;
  return bits | alpha_bits(input.alpha_test);
}

static const char *source_factor(uint32_t bits) {
  switch (bits) {
    case QA_SOURCE_STATE_SRCBLEND_ZERO: return "zero";
    case QA_SOURCE_STATE_SRCBLEND_ONE: return "one";
    case QA_SOURCE_STATE_SRCBLEND_DST_COLOR: return "dst-color";
    case QA_SOURCE_STATE_SRCBLEND_ONE_MINUS_DST_COLOR: return "one-minus-dst-color";
    case QA_SOURCE_STATE_SRCBLEND_SRC_ALPHA: return "src-alpha";
    case QA_SOURCE_STATE_SRCBLEND_ONE_MINUS_SRC_ALPHA: return "one-minus-src-alpha";
    case QA_SOURCE_STATE_SRCBLEND_DST_ALPHA: return "dst-alpha";
    case QA_SOURCE_STATE_SRCBLEND_ONE_MINUS_DST_ALPHA: return "one-minus-dst-alpha";
    case QA_SOURCE_STATE_SRCBLEND_ALPHA_SATURATE: return "src-alpha-saturate";
    default: qa_common_error(QA_COMMON_ERROR_DROP, "GL_State: invalid src blend state bits\n");
  }
}
static const char *destination_factor(uint32_t bits) {
  switch (bits) {
    case QA_SOURCE_STATE_DSTBLEND_ZERO: return "zero";
    case QA_SOURCE_STATE_DSTBLEND_ONE: return "one";
    case QA_SOURCE_STATE_DSTBLEND_SRC_COLOR: return "src-color";
    case QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_SRC_COLOR: return "one-minus-src-color";
    case QA_SOURCE_STATE_DSTBLEND_SRC_ALPHA: return "src-alpha";
    case QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_SRC_ALPHA: return "one-minus-src-alpha";
    case QA_SOURCE_STATE_DSTBLEND_DST_ALPHA: return "dst-alpha";
    case QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_DST_ALPHA: return "one-minus-dst-alpha";
    default: qa_common_error(QA_COMMON_ERROR_DROP, "GL_State: invalid dst blend state bits\n");
  }
}

size_t qa_source_state_changes(bool has_previous, uint32_t previous,
                               uint32_t next,
                               qa_source_state_change_fn visit, void *ctx) {
  uint32_t diff = has_previous ? (previous ^ next) : 0xffffffffu;
  size_t count = 0;
  if (diff == 0) return 0;
  if (diff & QA_SOURCE_STATE_DEPTHFUNC_EQUAL) {
    qa_source_state_change c = {.kind = "depth-function",
        .value = (next & QA_SOURCE_STATE_DEPTHFUNC_EQUAL) ? "equal" : "less-equal"};
    visit(c, ctx); count++;
  }
  if (diff & (QA_SOURCE_STATE_SRCBLEND_BITS | QA_SOURCE_STATE_DSTBLEND_BITS)) {
    qa_source_state_change c = {.kind = "blend"};
    if (next & (QA_SOURCE_STATE_SRCBLEND_BITS | QA_SOURCE_STATE_DSTBLEND_BITS)) {
      c.enabled = true;
      c.source = source_factor(next & QA_SOURCE_STATE_SRCBLEND_BITS);
      c.destination = destination_factor(next & QA_SOURCE_STATE_DSTBLEND_BITS);
    }
    visit(c, ctx); count++;
  }
  if (diff & QA_SOURCE_STATE_DEPTHMASK_TRUE) {
    qa_source_state_change c = {.kind = "depth-write",
        .bool_value = (next & QA_SOURCE_STATE_DEPTHMASK_TRUE) != 0};
    visit(c, ctx); count++;
  }
  if (diff & QA_SOURCE_STATE_POLYMODE_LINE) {
    qa_source_state_change c = {.kind = "polygon-mode",
        .value = (next & QA_SOURCE_STATE_POLYMODE_LINE) ? "line" : "fill"};
    visit(c, ctx); count++;
  }
  if (diff & QA_SOURCE_STATE_DEPTHTEST_DISABLE) {
    qa_source_state_change c = {.kind = "depth-test",
        .enabled = (next & QA_SOURCE_STATE_DEPTHTEST_DISABLE) == 0};
    visit(c, ctx); count++;
  }
  if (diff & QA_SOURCE_STATE_ATEST_BITS) {
    const char *value = NULL;
    switch (next & QA_SOURCE_STATE_ATEST_BITS) {
      case 0: value = "none"; break;
      case QA_SOURCE_STATE_ATEST_GT_0: value = "gt0"; break;
      case QA_SOURCE_STATE_ATEST_LT_80: value = "lt128"; break;
      case QA_SOURCE_STATE_ATEST_GE_80: value = "ge128"; break;
      default: break; /* source assert(0) is a release no-op */
    }
    if (value != NULL) {
      qa_source_state_change c = {.kind = "alpha-test", .value = value};
      visit(c, ctx); count++;
    }
  }
  return count;
}
