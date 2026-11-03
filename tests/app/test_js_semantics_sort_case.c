/*
 * Regression checks for JS sort/case/truthiness semantics bugs fixed across modules
 * (tools/js_semantics_audit.py categories sort, case, truthy). Every expected value
 * was printed by the TypeScript function itself under bun (build-local script
 * importing parseApiModels, ENTSCALE_ENCODE, clientDownloadCategory and
 * orderGameArchives from ../quake-anthology-ts/src).
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/network/client_download_policy.h"
#include "qa/content/catalog/index.h"
#include "qa/llm/auth.h"
#include "qa/llm/models.h"
#include "qa/network/q1/constants.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

/* parseApiModels sorts by a.name.localeCompare(b.name); strcmp put "+x" before
 * "-x" and "Beta"/"Zeta" before every lowercase name.
 * bun: ["-x","+x","a10","a9","alpha","Beta","beta_1","beta-2","Zeta"] */
static void model_order(void) {
  const char *json =
      "{\"data\":[{\"id\":\"Zeta\"},{\"id\":\"alpha\"},{\"id\":\"beta-2\"},{\"id\":\"beta_1\"},"
      "{\"id\":\"Beta\"},{\"id\":\"+x\"},{\"id\":\"-x\"},{\"id\":\"a10\"},{\"id\":\"a9\"}]}";
  const char *want[] = {"-x", "+x", "a10", "a9", "alpha", "Beta", "beta_1", "beta-2", "Zeta"};
  qa_llm_json *value = NULL;
  CHECK(qa_llm_json_parse(json, strlen(json), &value));
  qa_llm_model_list list;
  qa_llm_error error;
  memset(&error, 0, sizeof error);
  CHECK(qa_llm_parse_api_models(value, false, &list, &error));
  CHECK(list.count == 9);
  for (size_t i = 0; i < list.count && i < 9; i++) CHECK(strcmp(list.items[i].name, want[i]) == 0);
  qa_llm_model_list_free(&list);
  qa_llm_json_free(value);
}

/* ENTSCALE_ENCODE(a) is `a ? a * 16 : 16`; NaN is falsy. bun: 16 16 32. */
static void entscale(void) {
  CHECK(qa_q1_entscale_encode(NAN) == 16);
  CHECK(qa_q1_entscale_encode(0) == 16);
  CHECK(qa_q1_entscale_encode(2) == 32);
}

/* clientDownloadCategory lowers with String.toLowerCase: U+212A KELVIN SIGN becomes
 * 'k', so "maps/x.paK" is a package. bun: package package map. */
static void download_category(void) {
  CHECK(qa_app_client_download_category_for("maps/x.pa\xe2\x84\xaa") == QA_APP_DL_PACKAGE);
  CHECK(qa_app_client_download_category_for("MAPS/X.PAK") == QA_APP_DL_PACKAGE);
  CHECK(qa_app_client_download_category_for("maps/x.bsp") == QA_APP_DL_MAP);
}

/* orderGameArchives sorts with a stable Array.prototype.sort and then reverses;
 * equal lowercase basenames keep their input order. qsort reordered them. */
static void archive_order(void) {
  static const char *const names[] = {"PAK0", "pak0", "Pak0", "zz", "ZZ"};
  qa_catalog_archive storage[24];
  qa_catalog_archive *archives[24];
  char paths[24][32];
  for (int i = 0; i < 24; i++) {
    snprintf(paths[i], sizeof paths[i], "dir%d/%s.pk3", i, names[i % 5]);
    memset(&storage[i], 0, sizeof storage[i]);
    storage[i].path = paths[i];
    storage[i].format = QA_ARCHIVE_FORMAT_PK3;
    archives[i] = &storage[i];
  }
  qa_catalog_product_expectation q3;
  memset(&q3, 0, sizeof q3);
  q3.family = QA_GAME_FAMILY_Q3;
  size_t count = 0;
  qa_catalog_archive **ordered = qa_order_game_archives(&q3, archives, 24, &count);
  /* bun orderGameArchives({ family: "q3" }, ...) */
  static const char *const want[] = {
      "dir23/zz.pk3", "dir18/zz.pk3", "dir13/zz.pk3", "dir8/zz.pk3", "dir3/zz.pk3",
      "dir19/ZZ.pk3", "dir14/ZZ.pk3", "dir9/ZZ.pk3", "dir4/ZZ.pk3", "dir21/pak0.pk3",
      "dir16/pak0.pk3", "dir11/pak0.pk3", "dir6/pak0.pk3", "dir1/pak0.pk3", "dir22/Pak0.pk3",
      "dir17/Pak0.pk3", "dir12/Pak0.pk3", "dir7/Pak0.pk3", "dir2/Pak0.pk3", "dir20/PAK0.pk3",
      "dir15/PAK0.pk3", "dir10/PAK0.pk3", "dir5/PAK0.pk3", "dir0/PAK0.pk3"};
  CHECK(ordered != NULL && count == 24);
  for (size_t i = 0; ordered != NULL && i < count && i < 24; i++) {
    checks++;
    if (strcmp(ordered[i]->path, want[i]) != 0) {
      failures++;
      fprintf(stderr, "FAIL q3 archive %zu: %s, want %s\n", i, ordered[i]->path, want[i]);
    }
  }
  free(ordered);

  static const char *const q2_paths[] = {"x/pak1.pak", "y/PAK1.pak", "z/pak1.PAK", "w/pak0.pak",
                                         "v/other.pkz", "u/Other.pkz", "t/pak1.pak", "s/PAK1.PAK"};
  qa_catalog_archive q2_storage[8];
  qa_catalog_archive *q2_archives[8];
  for (int i = 0; i < 8; i++) {
    memset(&q2_storage[i], 0, sizeof q2_storage[i]);
    q2_storage[i].path = (char *)q2_paths[i];
    q2_storage[i].format = strstr(q2_paths[i], "pkz") ? QA_ARCHIVE_FORMAT_ZIP : QA_ARCHIVE_FORMAT_PAK;
    q2_archives[i] = &q2_storage[i];
  }
  qa_catalog_product_expectation q2;
  memset(&q2, 0, sizeof q2);
  q2.family = QA_GAME_FAMILY_Q2;
  ordered = qa_order_game_archives(&q2, q2_archives, 8, &count);
  /* bun orderGameArchives({ family: "q2" }, ...) */
  static const char *const q2_want[] = {"u/Other.pkz", "v/other.pkz", "s/PAK1.PAK", "t/pak1.pak",
                                        "z/pak1.PAK", "y/PAK1.pak", "x/pak1.pak", "w/pak0.pak"};
  CHECK(ordered != NULL && count == 8);
  for (size_t i = 0; ordered != NULL && i < count && i < 8; i++) {
    checks++;
    if (strcmp(ordered[i]->path, q2_want[i]) != 0) {
      failures++;
      fprintf(stderr, "FAIL q2 archive %zu: %s, want %s\n", i, ordered[i]->path, q2_want[i]);
    }
  }
  free(ordered);
}

int main(void) {
  model_order();
  entscale();
  download_category();
  archive_order();
  printf("test_js_semantics_sort_case: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
