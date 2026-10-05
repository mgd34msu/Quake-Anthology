#include "internal.h"
#include "qa/text.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

static atomic_bool initialized = false;
static atomic_flag initialization_lock = ATOMIC_FLAG_INIT;
static float sine_table[1024], noise_values[256], fog_table[256];
static uint8_t noise_permutation[256];

typedef struct material_random { uint32_t state[31]; unsigned front, rear; } material_random;
static uint32_t random_next(material_random *random)
{
    uint32_t value = random->state[random->front] + random->state[random->rear];
    random->state[random->front] = value;
    random->front = (random->front + 1u) % 31u;
    random->rear = (random->rear + 1u) % 31u;
    return value >> 1;
}
static void initialize_tables(void)
{
    if (atomic_load_explicit(&initialized, memory_order_acquire)) return;
    while (atomic_flag_test_and_set_explicit(&initialization_lock, memory_order_acquire)) { }
    if (!atomic_load_explicit(&initialized, memory_order_relaxed)) {
        for (unsigned i = 0; i < 1024; ++i) {
            float degrees = (float)((double)i * 360.0 / 1023.0);
            sine_table[i] = (float)sin((double)degrees * 3.14159265358979323846 / 180.0);
        }
        material_random random = {{1001}, 3, 0};
        for (unsigned i = 1; i < 31; ++i)
            random.state[i] = (uint32_t)((UINT64_C(16807) * random.state[i - 1]) % UINT64_C(2147483647));
        for (unsigned i = 0; i < 310; ++i) (void)random_next(&random);
        for (unsigned i = 0; i < 256; ++i) {
            float value = (float)random_next(&random) / (float)INT32_MAX;
            noise_values[i] = value * 2.0f - 1.0f;
            value = (float)random_next(&random) / (float)INT32_MAX;
            noise_permutation[i] = (uint8_t)(value * 255.0f);
            fog_table[i] = sqrtf((float)i / 255.0f);
        }
        atomic_store_explicit(&initialized, true, memory_order_release);
    }
    atomic_flag_clear_explicit(&initialization_lock, memory_order_release);
}
unsigned qa_material_table_index(float value)
{
    if (!isfinite(value)) return 0;
    return (unsigned)(uint32_t)qa_source_float_to_i32(value) & 1023u;
}
float qa_material_sine(unsigned index)
{
    initialize_tables();
    return sine_table[index & 1023u];
}
float qa_material_inverse_sqrt(float square)
{
    uint32_t bits;
    memcpy(&bits, &square, sizeof(bits));
    bits = UINT32_C(0x5f3759df) - (bits >> 1);
    float inverse;
    memcpy(&inverse, &bits, sizeof(inverse));
    float half = square * 0.5f;
    float product = half * inverse;
    product *= inverse;
    return inverse * (1.5f - product);
}
qa_vec3 qa_material_fast_normalize(qa_vec3 value)
{
    return qa_vec_scale(value, qa_material_inverse_sqrt(qa_vec_dot(value, value)));
}
static unsigned noise_cell(float value)
{
    return (unsigned)(uint32_t)qa_source_float_to_i32(floorf(value)) & 255u;
}
static float noise_lattice(unsigned x, unsigned y, unsigned z, unsigned t)
{
    unsigned index = noise_permutation[t & 255u];
    index = noise_permutation[(z + index) & 255u];
    index = noise_permutation[(y + index) & 255u];
    return noise_values[noise_permutation[(x + index) & 255u]];
}
static float noise_lerp(float a, float b, float amount)
{
    return a * (1.0f - amount) + b * amount;
}
float qa_material_noise(float x, float y, float z, float time)
{
    if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(time)) return NAN;
    initialize_tables();
    unsigned ix = noise_cell(x), iy = noise_cell(y), iz = noise_cell(z), it = noise_cell(time);
    float fx = x - floorf(x), fy = y - floorf(y), fz = z - floorf(z), ft = time - floorf(time);
    float planes[2][2];
    for (unsigned dt = 0; dt < 2; ++dt) for (unsigned dz = 0; dz < 2; ++dz) {
        float a = noise_lerp(noise_lattice(ix, iy, iz + dz, it + dt),
                             noise_lattice(ix + 1u, iy, iz + dz, it + dt), fx);
        float b = noise_lerp(noise_lattice(ix, iy + 1u, iz + dz, it + dt),
                             noise_lattice(ix + 1u, iy + 1u, iz + dz, it + dt), fx);
        planes[dt][dz] = noise_lerp(a, b, fy);
    }
    return noise_lerp(noise_lerp(planes[0][0], planes[0][1], fz),
                      noise_lerp(planes[1][0], planes[1][1], fz), ft);
}
float qa_material_wave_evaluate(const qa_material_wave *wave, double seconds)
{
    if (wave == NULL || !isfinite(seconds)) return NAN;
    float time = (float)seconds;
    float phase = wave->phase + time * wave->frequency;
    float raw_index = phase * 1024.0f;
    if (!isfinite(raw_index) || (double)raw_index < -2147483648.0 ||
        (double)raw_index >= 2147483648.0) return NAN;
    unsigned index = qa_material_table_index(raw_index);
    float value;
    switch (wave->kind) {
    case QA_WAVE_SIN: value = qa_material_sine(index); break;
    case QA_WAVE_SQUARE: value = index < 512 ? 1.0f : -1.0f; break;
    case QA_WAVE_TRIANGLE:
        value = index < 256 ? (float)index / 256.0f : index < 768 ?
            2.0f - (float)index / 256.0f : (float)index / 256.0f - 4.0f;
        break;
    case QA_WAVE_SAWTOOTH: value = (float)index / 1024.0f; break;
    case QA_WAVE_INVERSE_SAWTOOTH: value = 1.0f - (float)index / 1024.0f; break;
    case QA_WAVE_NOISE:
    case QA_WAVE_NONE: return NAN;
    default: return NAN;
    }
    return wave->base + value * wave->amplitude;
}
float qa_material_fog_factor(float s, float t)
{
    float distance = s - 1.0f / 512.0f;
    if (distance < 0.0f || t < 1.0f / 32.0f) return 0.0f;
    if (t < 31.0f / 32.0f) distance *= (t - 1.0f / 32.0f) / (30.0f / 32.0f);
    distance *= 8.0f;
    if (!isfinite(distance)) return distance > 0.0f ? 1.0f : 0.0f;
    distance = fmaxf(0.0f, fminf(1.0f, distance));
    initialize_tables();
    return fog_table[(unsigned)(distance * 255.0f)];
}
