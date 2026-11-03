/* Port of src/compat/qvm/grapple-profile.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_GRAPPLE_PROFILE_H
#define QA_COMPAT_QVM_GRAPPLE_PROFILE_H

#include "qa/compat/qvm/artifacts.h"
#include "qa/contracts/qvm_grapple.h"
#include "qa/persistence/value.h"

typedef qa_qvm_grapple_definition qa_qvm_grapple_profile;

/*
 * The returned profile borrows strings from the value tree and owns
 * its list arrays; free with qa_qvm_grapple_profile_free. NULL only
 * on OOM. `artifact` is the resolved module artifact (TS
 * QvmModuleOptions["artifact"]).
 */
qa_qvm_grapple_profile *qa_qvm_read_grapple_profile(const qa_save_value *value,
                                                    const qa_resolved_qvm_artifact *artifact);
void qa_qvm_grapple_profile_free(qa_qvm_grapple_profile *profile);
/* Owned declaration tree: the whole profile plus {version:1, artifactDigest,
 * artifactPath}, accepted by qa_qvm_read_grapple_profile; NULL only on OOM. */
qa_save_value *qa_qvm_grapple_profile_declaration(const qa_qvm_grapple_profile *profile);

#endif
