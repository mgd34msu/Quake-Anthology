/* Port of src/content/q3/base/shared/definitions.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/shared/definitions.h"

const char *qa_q3_base_product_name(qa_q3_base_product product) {
  return product == QA_Q3_BASE_PRODUCT_BASEQ3 ? "baseq3" : "missionpack";
}
static const qa_q3_base_stat_schema BASE_STATS = {
  QA_Q3_BASE_PRODUCT_BASEQ3,
  QA_Q3_BASE_STAT_HEALTH, QA_Q3_BASE_STAT_HOLDABLE_ITEM, QA_Q3_BASE_STAT_WEAPONS,
  QA_Q3_BASE_STAT_ARMOR, QA_Q3_BASE_STAT_DEAD_YAW, QA_Q3_BASE_STAT_CLIENTS_READY,
  QA_Q3_BASE_STAT_MAX_HEALTH, false, 0
};
static const qa_q3_base_stat_schema MISSIONPACK_STATS = {
  QA_Q3_BASE_PRODUCT_MISSIONPACK,
  QA_Q3_BASE_MP_STAT_HEALTH, QA_Q3_BASE_MP_STAT_HOLDABLE_ITEM, QA_Q3_BASE_MP_STAT_WEAPONS,
  QA_Q3_BASE_MP_STAT_ARMOR, QA_Q3_BASE_MP_STAT_DEAD_YAW, QA_Q3_BASE_MP_STAT_CLIENTS_READY,
  QA_Q3_BASE_MP_STAT_MAX_HEALTH, true, QA_Q3_BASE_MP_STAT_PERSISTANT_POWERUP
};
const qa_q3_base_stat_schema *qa_q3_base_stat_schema_for(qa_q3_base_product product) {
  return product == QA_Q3_BASE_PRODUCT_BASEQ3 ? &BASE_STATS : &MISSIONPACK_STATS;
}
double qa_q3_base_weapon_count(qa_q3_base_product product) {
  return product == QA_Q3_BASE_PRODUCT_BASEQ3 ? 11 : 14;
}
bool qa_q3_base_weapon_available(qa_q3_base_product product, qa_q3_weapon weapon) {
  return weapon > QA_Q3_WP_NONE && (double)weapon < qa_q3_base_weapon_count(product);
}
