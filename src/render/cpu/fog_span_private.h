#ifndef QA_CPU_FOG_SPAN_PRIVATE_H
#define QA_CPU_FOG_SPAN_PRIVATE_H
#include "fog_private.h"

typedef struct cpu_fog_span {
  float amount, first, second, third;
  uint32_t count;
} cpu_fog_span;

typedef struct cpu_fog_span_style {
  qa_scene_fog_effect effect;
  float density, amount, color[3];
  bool exponential, enabled;
} cpu_fog_span_style;

static inline cpu_fog_span_style cpu_fog_span_style_prepare(const qa_scene_fog *fog) {
  bool active = fog->kind == QA_FOG_CONSTANT ||
      (fog->kind == QA_FOG_EXP2 && fog->effect != QA_FOG_NO_EFFECT);
  return (cpu_fog_span_style){.effect = fog->kind == QA_FOG_CONSTANT ? QA_FOG_COLOR : fog->effect,
      .density = fog->density / 64, .amount = fog->amount,
      .color = {fog->color.x * 255, fog->color.y * 255, fog->color.z * 255},
      .exponential = fog->kind == QA_FOG_EXP2, .enabled = active};
}

static inline double cpu_fog_span_amount(double density, double q) {
  double scaled = density / q;
  return 1 - cpu_fog_exp((float)(scaled * scaled));
}

static inline cpu_fog_span cpu_fog_span_prepare(const cpu_fog_span_style *style,
    float q, float q_step, uint32_t count) {
  if (!style->exponential)
    return (cpu_fog_span){.amount = style->amount, .count = count};
  double first_q = q, delta_q = q_step, density = style->density;
  double start = cpu_fog_span_amount(density, first_q);
  double c1 = 0, c2 = 0, c3 = 0;
  while (count > 1) {
    double steps = count - 1, last_q = first_q + steps * delta_q;
    double end = cpu_fog_span_amount(density, last_q);
    double start_scaled = density / first_q, end_scaled = density / last_q;
    double start_slope = -2 * start_scaled * start_scaled * delta_q / first_q * (1 - start);
    double end_slope = -2 * end_scaled * end_scaled * delta_q / last_q * (1 - end);
    c1 = steps * start_slope;
    c2 = 3 * (end - start) - 2 * c1 - steps * end_slope;
    c3 = 2 * (start - end) + c1 + steps * end_slope;
    bool accurate = true;
    for (unsigned i = 1; i < 4; ++i) {
      double t = i * .25;
      double exact = cpu_fog_span_amount(density, first_q + steps * delta_q * t);
      double estimate = start + t * (c1 + t * (c2 + t * c3));
      if (!isfinite(estimate) || fabs(estimate - exact) > .125 / 255) { accurate = false; break; }
    }
    if (accurate) break;
    count = (count + 1) / 2;
  }
  if (count == 1) return (cpu_fog_span){.amount = (float)start, .count = 1};
  double inverse = 1.0 / (count - 1), square = inverse * inverse, cube = square * inverse;
  return (cpu_fog_span){(float)start,
      (float)(c1 * inverse + c2 * square + c3 * cube),
      (float)(2 * c2 * square + 6 * c3 * cube), (float)(6 * c3 * cube), count};
}

static inline void cpu_fog_span_apply(uint8_t *pixel,
    const cpu_fog_span_style *style, float amount, bool alpha) {
  switch (style->effect) {
  case QA_FOG_COLOR:
    for (size_t c = 0; c < 3; ++c)
      pixel[c] = cpu_fog_byte(pixel[c] + (style->color[c] - pixel[c]) * amount);
    break;
  case QA_FOG_RGB: case QA_FOG_RGBA:
    for (size_t c = 0; c < 3; ++c) pixel[c] = cpu_fog_byte(pixel[c] * (1 - amount));
    break;
  case QA_FOG_OVERLAY:
    for (size_t c = 0; c < 3; ++c) pixel[c] = cpu_fog_byte(style->color[c]);
    break;
  case QA_FOG_ALPHA: case QA_FOG_NO_EFFECT: break;
  }
  if (alpha && (style->effect == QA_FOG_ALPHA || style->effect == QA_FOG_RGBA || style->effect == QA_FOG_OVERLAY))
    pixel[3] = cpu_fog_byte(pixel[3] * (style->effect == QA_FOG_OVERLAY ? amount : 1 - amount));
  if (!alpha) pixel[3] = 255;
}

static inline void cpu_fog_span_step(cpu_fog_span *span) {
  span->amount += span->first;
  span->first += span->second;
  span->second += span->third;
}
#endif
