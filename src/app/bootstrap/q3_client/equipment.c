/* Port of src/app/bootstrap/q3-client/equipment.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_client/equipment.h"
#include "qa/foundation/js.h"
qa_q3_wire_user_command qa_app_q3_equipment_command(qa_q3_wire_user_command command,
    const qa_app_q3_equipment_presentation *equipment) {
  if (equipment == NULL) return command;
  command.weapon = qa_js_to_int32(equipment->primary_weapon);
  command.buttons &= ~1;
  return command;
}
