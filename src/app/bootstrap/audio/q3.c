/* Port of src/app/bootstrap/audio/q3.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/audio/q3.h"
#include <string.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/presentation/character_resources.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
const char *qa_app_q3_voice_fallback(const qa_content_catalog *catalog, qa_content_id content, bool team_game) {
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, content);
  for (;;) {
    if (is(product->expectation.campaign, "missionpack"))
      return qa_q3_custom_sound_fallback(QA_Q3_BASE_PRODUCT_MISSIONPACK, team_game);
    if (product->expectation.base_product == NULL)
      return qa_q3_custom_sound_fallback(QA_Q3_BASE_PRODUCT_BASEQ3, team_game);
    product = qa_content_catalog_product(catalog, product->expectation.base_product);
  }
}
