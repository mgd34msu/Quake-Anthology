/* Port of src/content/q3/equipment/lrctf-grapple-profile.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_EQUIPMENT_LRCTF_GRAPPLE_PROFILE_H
#define QA_CONTENT_Q3_EQUIPMENT_LRCTF_GRAPPLE_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/execution.h"
#include "qa/contracts/qvm_grapple.h"

/* Mirror of QvmInstruction (TS owner: src/compat/qvm/image.ts). */
typedef struct qa_q3_equipment_qvm_instruction {
  double byte_offset, opcode, operand_width, operand;
} qa_q3_equipment_qvm_instruction;
/* Mirror of QvmImage fields read by this slice (TS owner: src/compat/qvm/image.ts). */
typedef struct qa_q3_equipment_qvm_image {
  const uint8_t *initialized_data; size_t initialized_data_length;
  double allocated_data_length;
  const qa_q3_equipment_qvm_instruction *instructions; size_t instruction_count;
  double data_length, literal_length, bss_length;
} qa_q3_equipment_qvm_image;
/* Mirror of KnownQvmArtifact product field (TS owner: src/compat/qvm/artifacts.ts). */
typedef struct qa_q3_equipment_known_artifact { const char *product; } qa_q3_equipment_known_artifact;
/* Mirror of QvmModuleOptions["artifact"] (TS owner: src/compat/qvm/module.ts,
 * bytecode ResolvedQvmArtifact from src/compat/qvm/artifacts.ts).
 * abi_profile NULL is TS undefined; known NULL is TS null. */
typedef struct qa_q3_equipment_qvm_artifact {
  qa_module_identity module;
  const char *role; /* "qagame"|"cgame"|"ui" */
  const char *abi_profile;
  const qa_q3_equipment_known_artifact *known;
  qa_q3_equipment_qvm_image image;
} qa_q3_equipment_qvm_artifact;
/* Mirror of QvmOpcode values used here (TS owner: src/compat/qvm/image.ts). */
enum { QA_Q3_EQUIPMENT_OP_ENTER = 3, QA_Q3_EQUIPMENT_OP_CONST = 8,
       QA_Q3_EQUIPMENT_OP_LEI = 14, QA_Q3_EQUIPMENT_OP_STORE4 = 32 };

extern const char qa_lrctf_grapple_digest[];
/* Mirror of readQvmGrappleProfile validation (TS owner:
 * src/compat/qvm/grapple-profile.ts) over an already-typed definition;
 * the SaveReader field decoding has no C counterpart for literal
 * declarations. Invalid definitions abort with the TS messages. */
struct qa_qvm_grapple_definition qa_q3_equipment_read_grapple_profile(
    struct qa_qvm_grapple_definition definition, const qa_q3_equipment_qvm_artifact *artifact);
/* False is TS null. */
bool qa_lrctf_grapple_profile(const qa_q3_equipment_qvm_artifact *artifact,
    struct qa_qvm_grapple_definition *out);
#endif
