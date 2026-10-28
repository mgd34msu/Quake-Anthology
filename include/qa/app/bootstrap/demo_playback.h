/* Port of src/app/bootstrap/demo-playback.ts. Async read becomes a
 * synchronous fn+ctx. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_DEMO_PLAYBACK_H
#define QA_APP_BOOTSTRAP_DEMO_PLAYBACK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* TS DemoFamily. */
typedef enum qa_app_demo_family { QA_APP_DEMO_Q1, QA_APP_DEMO_QW, QA_APP_DEMO_Q2, QA_APP_DEMO_Q3 } qa_app_demo_family;
typedef struct qa_app_demo_request { qa_app_demo_family family; const char *name; bool timedemo; } qa_app_demo_request;
/* Owned path+bytes; free with qa_app_demo_resource_destroy. protocol 0 unless q3 (66|67|68). */
typedef struct qa_app_demo_resource {
  qa_app_demo_family kind;
  char *path;
  uint8_t *bytes; size_t byte_count;
  double protocol;
} qa_app_demo_resource;
/* read returns malloc'd bytes (caller frees on success path via the
 * resource) or false for TS undefined. */
typedef struct qa_app_demo_reader {
  void *ctx;
  bool (*read)(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_count);
  void (*print)(void *ctx, const char *text);
} qa_app_demo_reader;
qa_app_demo_family qa_app_demo_family_of(const char *name, qa_app_demo_family fallback);
/* Aborts with exact TS Errors when the demo cannot be opened. */
qa_app_demo_resource qa_app_open_demo_resource(qa_app_demo_request request, qa_app_demo_reader reader);
void qa_app_demo_resource_destroy(qa_app_demo_resource *resource);
#endif
