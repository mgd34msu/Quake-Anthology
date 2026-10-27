/* Port of src/render/q3-hardware.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_Q3_HARDWARE_H
#define QA_RENDER_Q3_HARDWARE_H
/* TS Q3Hardware exact strings: "generic"|"3dfx"|"riva128"|"ragepro"|"permedia2". */
typedef const char *qa_q3_hardware;
typedef qa_q3_hardware (*qa_q3_hardware_read_fn)(void *ctx);
/* TS q3EffectHardware result: getters re-read on every access. */
typedef struct qa_q3_effect_hardware { qa_q3_hardware_read_fn read; void *ctx; } qa_q3_effect_hardware;
qa_q3_effect_hardware qa_q3_effect_hardware_make(qa_q3_hardware_read_fn read, void *ctx);
const char *qa_q3_effect_hardware_hardware(const qa_q3_effect_hardware *p); /* "generic"|"ragepro" */
const char *qa_q3_effect_hardware_type(const qa_q3_effect_hardware *p); /* "generic"|"rage-pro" */
qa_q3_hardware qa_q3_hardware_classify(const char *renderer);
double qa_q3_hardware_number(qa_q3_hardware hardware);
#endif
