/* Port of src/ui/common/localization.ts. GPL-2.0-or-later. */
#include "qa/ui/common/localization.h"
#include <stdlib.h>
#include <string.h>

const char *qa_ui_menu_localize(const qa_ui_menu_localizer *localizer, const char *text) {
  char *language = localizer->language(localizer->ctx);
  const char *result = text;
  if (language != NULL && strcmp(language, "english") != 0) {
    qa_ui_menu_catalogs catalogs = localizer->catalogs(localizer->ctx);
    for (size_t i = 0; i < catalogs.count; i++) {
      const qa_ui_menu_catalog *catalog = &catalogs.catalogs[i];
      if (strcmp(catalog->language, language) != 0) continue;
      for (size_t e = 0; e < catalog->entry_count; e++)
        if (strcmp(catalog->entries[e].text, text) == 0) { result = catalog->entries[e].translation; break; }
      break;
    }
  }
  free(language);
  return result;
}
