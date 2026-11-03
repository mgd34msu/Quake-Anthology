/* Port of src/app/bootstrap/original-save.ts: prepareApplicationSave over the
 * real save readers (readSavedGame, readKexQ2SaveArchive), the content catalog
 * (selectQ1SaveProduct), loadApplicationContent and the SharedSimulation
 * (createSimulation, admitPlayer, restoreOriginalSave, checkpoint). TS async is
 * synchronous; TS throws are qa_raise (Error, SaveFormatError, ...).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_ORIGINAL_SAVE_H
#define QA_APP_BOOTSTRAP_ORIGINAL_SAVE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/options.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/session.h"
/* TS { image, options }. */
typedef struct qa_app_prepared_application_save {
  qa_save_image image;
  qa_app_options options;
} qa_app_prepared_application_save;
/* TS prepareApplicationSave(options, catalog, path, sourceProduct?). source_product NULL is TS undefined. The catalog
 * is borrowed (the content loader reads its digest cache). On return out->options is owned and out->image lives
 * until qa_app_prepared_application_save_free(out, *out_backing): the image of an imported original save borrows
 * its recipe from the closed simulation and content the backing keeps, a shared image its decoded save. On a raise
 * nothing is left to free (out stays zeroed, *out_backing NULL). */
void qa_app_prepare_application_save_native(const qa_app_options *options, const qa_content_catalog *catalog, const char *path,
    const char *source_product, qa_app_prepared_application_save *out, void **out_backing);
/* Releases the options and the image's backing; backing NULL frees only the options. */
void qa_app_prepared_application_save_free(qa_app_prepared_application_save *save, void *backing);
#endif
