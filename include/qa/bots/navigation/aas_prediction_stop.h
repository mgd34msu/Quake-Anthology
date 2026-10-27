/* Port of src/bots/navigation/aas-prediction-stop.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_AAS_PREDICTION_STOP_H
#define QA_BOTS_NAVIGATION_AAS_PREDICTION_STOP_H
#include "qa/bots/behavior/prediction.h"
#include "qa/bots/navigation/aas.h"
#include <stdbool.h>
#include <stdint.h>
/* False = TS null. */
bool qa_aas_prediction_stop(const qa_aas_asset *asset, qa_vec3 start, qa_vec3 end, int64_t frame,
                            int64_t events, int64_t stop_area, qa_bot_movement_stop *out);
#endif
