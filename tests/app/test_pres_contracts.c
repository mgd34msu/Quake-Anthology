/* C checks for the presentation type contracts: SimulationPresentationEvent
 * (simulation/types.ts) stored, copied and read back for every kind; the
 * model renderer and light sampler on WorldScene and the assets provider;
 * the QVM presentation context, client syscall interfaces and the Q3 client
 * assets host built from ApplicationAssets; Q3SelectedSource and
 * Q3SourcePresentationState. Expected values come from the TS modules run
 * under bun on the same inputs. Each part lives in pres_contracts/<part>.inc;
 * define PRES_CONTRACTS_SKIP_<PART> to build without one part.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
#define CHECK_STR(a, b) CHECK((a) != NULL && (b) != NULL && strcmp((a), (b)) == 0)
/* Every callback in these tests receives this context and checks it. */
static int pres_sentinel_storage;
#define PRES_SENTINEL ((void *)&pres_sentinel_storage)
#define CHECK_SENTINEL(ctx) CHECK((ctx) == PRES_SENTINEL)

#ifndef PRES_CONTRACTS_SKIP_EVENTS
#include "pres_contracts/events.inc"
#endif
#ifndef PRES_CONTRACTS_SKIP_RENDER
#include "pres_contracts/render.inc"
#endif
#ifndef PRES_CONTRACTS_SKIP_QVM
#include "pres_contracts/qvm.inc"
#endif
#ifndef PRES_CONTRACTS_SKIP_Q3SOURCE
#include "pres_contracts/q3source.inc"
#endif

int main(void) {
#ifndef PRES_CONTRACTS_SKIP_EVENTS
  pres_events_tests();
#endif
#ifndef PRES_CONTRACTS_SKIP_RENDER
  pres_render_tests();
#endif
#ifndef PRES_CONTRACTS_SKIP_QVM
  pres_qvm_tests();
#endif
#ifndef PRES_CONTRACTS_SKIP_Q3SOURCE
  pres_q3source_tests();
#endif
  printf("test_pres_contracts: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
