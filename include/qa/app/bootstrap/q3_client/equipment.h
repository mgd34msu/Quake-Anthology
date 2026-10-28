/* Port of src/app/bootstrap/q3-client/equipment.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_EQUIPMENT_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_EQUIPMENT_H
#include <stdbool.h>
#include "qa/contracts/ui.h"
#include "qa/network/q3/message.h"
/* TS Q3EquipmentPresentation. */
typedef struct qa_app_q3_equipment_presentation {
  double primary_weapon;
  qa_arsenal_ammo_warning warning;
} qa_app_q3_equipment_presentation;
/* TS q3EquipmentCommand (equipment NULL = TS null). */
qa_q3_wire_user_command qa_app_q3_equipment_command(qa_q3_wire_user_command command,
    const qa_app_q3_equipment_presentation *equipment);
#endif
