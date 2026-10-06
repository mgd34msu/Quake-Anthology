#ifndef QA_CPU_FOG_PRIVATE_H
#define QA_CPU_FOG_PRIVATE_H

#include "internal.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* exp(-x), reduced to one binary exponent and a 1/256 interval. Cubic
 * Hermite interpolation uses the exponential's exact endpoint slopes.
 * This immutable table is shared by spans, triangles and depth fog. */
static const float cpu_fog_exp_table[257] = {
    0x1.0000000000000p+0f, 0x1.fe9d96b2a23d9p-1f, 0x1.fd3c22b8f71f1p-1f, 0x1.fbdba3692d514p-1f,
    0x1.fa7c1819e90d8p-1f, 0x1.f91d802243c89p-1f, 0x1.f7bfdad9cbe14p-1f, 0x1.f6632798844f9p-1f,
    0x1.f50765b6e4540p-1f, 0x1.f3ac948dd7274p-1f, 0x1.f252b376bba97p-1f, 0x1.f0f9c1cb6412ap-1f,
    0x1.efa1bee615a27p-1f, 0x1.ee4aaa2188510p-1f, 0x1.ecf482d8e67f1p-1f, 0x1.eb9f4867cca6ep-1f,
    0x1.ea4afa2a490dap-1f, 0x1.e8f7977cdb740p-1f, 0x1.e7a51fbc74c83p-1f, 0x1.e653924676d76p-1f,
    0x1.e502ee78b3ff6p-1f, 0x1.e3b333b16ee12p-1f, 0x1.e264614f5a129p-1f, 0x1.e11676b197d17p-1f,
    0x1.dfc97337b9b5fp-1f, 0x1.de7d5641c0658p-1f, 0x1.dd321f301b460p-1f, 0x1.dbe7cd63a8315p-1f,
    0x1.da9e603db3285p-1f, 0x1.d955d71ff6075p-1f, 0x1.d80e316c98398p-1f, 0x1.d6c76e862e6d3p-1f,
    0x1.d5818dcfba487p-1f, 0x1.d43c8eacaa1d6p-1f, 0x1.d2f87080d89f2p-1f, 0x1.d1b532b08c968p-1f,
    0x1.d072d4a07897cp-1f, 0x1.cf3155b5bab74p-1f, 0x1.cdf0b555dc3fap-1f, 0x1.ccb0f2e6d1675p-1f,
    0x1.cb720dcef9069p-1f, 0x1.ca3405751c4dbp-1f, 0x1.c8f6d9406e7b5p-1f, 0x1.c7ba88988c933p-1f,
    0x1.c67f12e57d14bp-1f, 0x1.c544778fafb22p-1f, 0x1.c40ab5fffd07ap-1f, 0x1.c2d1cd9fa652cp-1f,
    0x1.c199bdd85529cp-1f, 0x1.c06286141b33dp-1f, 0x1.bf2c25bd71e08p-1f, 0x1.bdf69c3f3a207p-1f,
    0x1.bcc1e904bc1d2p-1f, 0x1.bb8e0b79a6f1fp-1f, 0x1.ba5b030a10649p-1f, 0x1.b928cf22749e4p-1f,
    0x1.b7f76f2fb5e47p-1f, 0x1.b6c6e29f1c52ap-1f, 0x1.b59728de5593ap-1f, 0x1.b468415b749b1p-1f,
    0x1.b33a2b84f15fbp-1f, 0x1.b20ce6c9a8952p-1f, 0x1.b0e07298db666p-1f, 0x1.afb4ce622f2ffp-1f,
    0x1.ae89f995ad3adp-1f, 0x1.ad5ff3a3c2774p-1f, 0x1.ac36bbfd3f37ap-1f, 0x1.ab0e521356ebap-1f,
    0x1.a9e6b5579fdbfp-1f, 0x1.a8bfe53c12e59p-1f, 0x1.a799e1330b359p-1f, 0x1.a674a8af46052p-1f,
    0x1.a5503b23e255dp-1f, 0x1.a42c980460ad8p-1f, 0x1.a309bec4a2d33p-1f, 0x1.a1e7aed8eb8bcp-1f,
    0x1.a0c667b5de565p-1f, 0x1.9fa5e8d07f29ep-1f, 0x1.9e86319e32323p-1f, 0x1.9d674194bb8d5p-1f,
    0x1.9c49182a3f090p-1f, 0x1.9b2bb4d53fe0dp-1f, 0x1.9a0f170ca07bap-1f, 0x1.98f33e47a22a2p-1f,
    0x1.97d829fde4e50p-1f, 0x1.96bdd9a7670b3p-1f, 0x1.95a44cbc8520fp-1f, 0x1.948b82b5f98e5p-1f,
    0x1.93737b0cdc5e5p-1f, 0x1.925c353aa2fe2p-1f, 0x1.9145b0b91ffc6p-1f, 0x1.902fed0282c8ap-1f,
    0x1.8f1ae99157736p-1f, 0x1.8e06a5e0866d9p-1f, 0x1.8cf3216b5448cp-1f, 0x1.8be05bad61779p-1f,
    0x1.8ace5422aa0dcp-1f, 0x1.89bd0a4785810p-1f, 0x1.88ac7d98a6699p-1f, 0x1.879cad931a436p-1f,
    0x1.868d99b4492ecp-1f, 0x1.857f4179f5b21p-1f, 0x1.8471a4623c7adp-1f, 0x1.8364c1eb941f8p-1f,
    0x1.82589994cce13p-1f, 0x1.814d2add106d9p-1f, 0x1.80427543e1a12p-1f, 0x1.7f3878491c491p-1f,
    0x1.7e2f336cf4e62p-1f, 0x1.7d26a62ff86f0p-1f, 0x1.7c1ed0130c133p-1f, 0x1.7b17b0976cfdbp-1f,
    0x1.7a11473eb0187p-1f, 0x1.790b938ac1cf6p-1f, 0x1.780694fde5d3fp-1f, 0x1.77024b1ab6e09p-1f,
    0x1.75feb564267c9p-1f, 0x1.74fbd35d7cbfdp-1f, 0x1.73f9a48a58174p-1f, 0x1.72f8286ead08ap-1f,
    0x1.71f75e8ec5f74p-1f, 0x1.70f7466f42e87p-1f, 0x1.6ff7df9519484p-1f, 0x1.6ef9298593ae5p-1f,
    0x1.6dfb23c651a2fp-1f, 0x1.6cfdcddd47646p-1f, 0x1.6c012750bdabfp-1f, 0x1.6b052fa75173ep-1f,
    0x1.6a09e667f3bcdp-1f, 0x1.690f4b19e9538p-1f, 0x1.68155d44ca973p-1f, 0x1.671c1c70833f6p-1f,
    0x1.6623882552225p-1f, 0x1.652b9febc8fb7p-1f, 0x1.6434634ccc320p-1f, 0x1.633dd1d1929fdp-1f,
    0x1.6247eb03a5585p-1f, 0x1.6152ae6cdf6f4p-1f, 0x1.605e1b976dc09p-1f, 0x1.5f6a320dceb71p-1f,
    0x1.5e76f15ad2149p-1f, 0x1.5d84590998b93p-1f, 0x1.5c9268a5946b7p-1f, 0x1.5ba11fba87a03p-1f,
    0x1.5ab07dd485429p-1f, 0x1.59c0827ff07ccp-1f, 0x1.58d12d497c7fdp-1f, 0x1.57e27dbe2c4cfp-1f,
    0x1.56f4736b527dbp-1f, 0x1.56070dde910d2p-1f, 0x1.551a4ca5d920fp-1f, 0x1.542e2f4f6ad27p-1f,
    0x1.5342b569d4f82p-1f, 0x1.5257de83f4eefp-1f, 0x1.516daa2cf6642p-1f, 0x1.508417f4531eep-1f,
    0x1.4f9b2769d2ca7p-1f, 0x1.4eb2d81d8abffp-1f, 0x1.4dcb299fddd0dp-1f, 0x1.4ce41b817c114p-1f,
    0x1.4bfdad5362a27p-1f, 0x1.4b17dea6db7d7p-1f, 0x1.4a32af0d7d3dfp-1f, 0x1.494e1e192aed2p-1f,
    0x1.486a2b5c13cd0p-1f, 0x1.4786d668b3237p-1f, 0x1.46a41ed1d0058p-1f, 0x1.45c2042a7d232p-1f,
    0x1.44e086061892dp-1f, 0x1.43ffa3f84b9d4p-1f, 0x1.431f5d950a897p-1f, 0x1.423fb2709468ap-1f,
    0x1.4160a21f72e2ap-1f, 0x1.40822c367a024p-1f, 0x1.3fa4504ac801cp-1f, 0x1.3ec70df1c5175p-1f,
    0x1.3dea64c123422p-1f, 0x1.3d0e544ede173p-1f, 0x1.3c32dc313a8e5p-1f, 0x1.3b57fbfec6cf4p-1f,
    0x1.3a7db34e59ff7p-1f, 0x1.39a401b7140efp-1f, 0x1.38cae6d05d866p-1f, 0x1.37f26231e754ap-1f,
    0x1.371a7373aa9cbp-1f, 0x1.36431a2de883bp-1f, 0x1.356c55f929ff1p-1f, 0x1.3496266e3fa2dp-1f,
    0x1.33c08b26416ffp-1f, 0x1.32eb83ba8ea32p-1f, 0x1.32170fc4cd832p-1f, 0x1.31432edeeb2fdp-1f,
    0x1.306fe0a31b715p-1f, 0x1.2f9d24abd886bp-1f, 0x1.2ecafa93e2f56p-1f, 0x1.2df961f64158ap-1f,
    0x1.2d285a6e4030bp-1f, 0x1.2c57e39771b2fp-1f, 0x1.2b87fd0dad990p-1f, 0x1.2ab8a66d10f13p-1f,
    0x1.29e9df51fdee1p-1f, 0x1.291ba7591bb70p-1f, 0x1.284dfe1f56380p-1f, 0x1.2780e341ddf29p-1f,
    0x1.26b4565e27cdep-1f, 0x1.25e85711ece76p-1f, 0x1.251ce4fb2a63fp-1f, 0x1.2451ffb82140ap-1f,
    0x1.2387a6e756239p-1f, 0x1.22bdda27912d1p-1f, 0x1.21f49917ddc96p-1f, 0x1.212be3578a819p-1f,
    0x1.2063b88628cd6p-1f, 0x1.1f9c18438ce4dp-1f, 0x1.1ed5022fcd91dp-1f, 0x1.1e0e75eb44027p-1f,
    0x1.1d4873168b9aap-1f, 0x1.1c82f95281c6bp-1f, 0x1.1bbe084045cd3p-1f, 0x1.1af99f8138a1dp-1f,
    0x1.1a35beb6fcb76p-1f, 0x1.1972658375d30p-1f, 0x1.18af9388c8deap-1f, 0x1.17ed48695bbc0p-1f,
    0x1.172b83c7d517bp-1f, 0x1.166a45471c3c2p-1f, 0x1.15a98c8a58e51p-1f, 0x1.14e95934f312ep-1f,
    0x1.1429aaea92de0p-1f, 0x1.136a814f204abp-1f, 0x1.12abdc06c31ccp-1f, 0x1.11edbab5e2ab5p-1f,
    0x1.11301d0125b51p-1f, 0x1.1073028d7233ep-1f, 0x1.0fb66affed31bp-1f, 0x1.0efa55fdfa9c5p-1f,
    0x1.0e3ec32d3d1a2p-1f, 0x1.0d83b23395decp-1f, 0x1.0cc922b7247f7p-1f, 0x1.0c0f145e46c85p-1f,
    0x1.0b5586cf98910p-1f, 0x1.0a9c79b1f3919p-1f, 0x1.09e3ecac6f383p-1f, 0x1.092bdf66607e0p-1f,
    0x1.0874518759bc8p-1f, 0x1.07bd42b72a836p-1f, 0x1.0706b29ddf6dep-1f, 0x1.0650a0e3c1f89p-1f,
    0x1.059b0d3158574p-1f, 0x1.04e5f72f654b2p-1f, 0x1.04315e86e7f85p-1f, 0x1.037d42e11bbccp-1f,
    0x1.02c9a3e778061p-1f, 0x1.02168143b0281p-1f, 0x1.0163da9fb3336p-1f, 0x1.00b1afa5abcbfp-1f,
    0x1.0000000000000p-1f,
};

#define CPU_FOG_INVERSE_LOG_TWO 0x1.715476p+0f
#define CPU_FOG_LOG_TWO_HIGH 0x1.62e400p-1f
#define CPU_FOG_LOG_TWO_LOW 0x1.7f7d1cp-20f
#define CPU_FOG_EXP_STEP 0x1.62e430p-9f

/* Scalar and packed evaluation share the same ordered interpolation. */
#define CPU_FOG_EXP_CURVE(left, right, fraction, difference, step, two, three) \
  ((left) + (fraction) * (-(step) * (left) + (fraction) * \
      ((three) * (difference) + (step) * ((two) * (left) + (right)) + \
       (fraction) * (-(two) * (difference) - (step) * ((left) + (right))))))

static inline float cpu_fog_exp(float attenuation) {
  if (isnan(attenuation)) return attenuation;
  if (attenuation >= 104) return 0;
  if (attenuation <= -89) return INFINITY;
  const float inverse_log_two = CPU_FOG_INVERSE_LOG_TWO;
  const float log_two_high = CPU_FOG_LOG_TWO_HIGH;
  const float log_two_low = CPU_FOG_LOG_TWO_LOW;
  float scaled = attenuation * inverse_log_two;
  int exponent = (int)scaled;
  if ((float)exponent > scaled) --exponent;
  float position = ((attenuation - (float)exponent * log_two_high) -
      (float)exponent * log_two_low) * (256 * inverse_log_two);
  if (position < 0) position = 0;
  if (position > 256) position = 256;
  unsigned index = (unsigned)position;
  if (index == 256) index = 255;
  float fraction = position - (float)index;
  float left = cpu_fog_exp_table[index];
  float right = cpu_fog_exp_table[index + 1];
  float difference = right - left;
  float value = CPU_FOG_EXP_CURVE(left, right, fraction, difference,
      CPU_FOG_EXP_STEP, 2, 3);
  int power = -exponent;
  if (power > 127) { value *= 2; --power; }
  bool subnormal = power < -126;
  if (subnormal) power += 126;
  uint32_t bits = (uint32_t)(power + 127) << 23;
  float scale;
  memcpy(&scale, &bits, sizeof(scale));
  value *= scale;
  return subnormal ? value * 0x1p-126f : value;
}

static inline float cpu_fog_clamp(float value) {
  return !(value > 0) ? 0 : value < 1 ? value : 1;
}

static inline uint8_t cpu_fog_byte(float value) {
  return !(value > 0) ? 0 : value >= 255 ? 255 : (uint8_t)(value + .5f);
}

static inline void cpu_fog_color(const qa_scene_fog *fog, double distance,
                                 double color[4]) {
  if (fog->kind != QA_FOG_CONSTANT && fog->kind != QA_FOG_EXP2) return;
  qa_scene_fog_effect effect = fog->kind == QA_FOG_CONSTANT ? QA_FOG_COLOR : fog->effect;
  if (effect == QA_FOG_NO_EFFECT) return;
  float scaled = (fog->density / 64) * (float)distance;
  float amount = fog->kind == QA_FOG_CONSTANT ? fog->amount :
                  1 - cpu_fog_exp(scaled * scaled);
  float rgb[3] = {cpu_fog_clamp((float)color[0]),
      cpu_fog_clamp((float)color[1]), cpu_fog_clamp((float)color[2])};
  float alpha = (float)color[3];
  if (effect == QA_FOG_COLOR) {
    rgb[0] += (fog->color.x - rgb[0]) * amount;
    rgb[1] += (fog->color.y - rgb[1]) * amount;
    rgb[2] += (fog->color.z - rgb[2]) * amount;
  }
  if (effect == QA_FOG_RGB || effect == QA_FOG_RGBA)
    for (size_t c = 0; c < 3; ++c) rgb[c] *= 1 - amount;
  if (effect == QA_FOG_ALPHA || effect == QA_FOG_RGBA) alpha *= 1 - amount;
  if (effect == QA_FOG_OVERLAY) {
    rgb[0] = fog->color.x;
    rgb[1] = fog->color.y;
    rgb[2] = fog->color.z;
    alpha *= amount;
  }
  for (size_t c = 0; c < 3; ++c) color[c] = rgb[c];
  color[3] = alpha;
}

#endif
