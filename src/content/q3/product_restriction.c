/* Port of src/content/q3/product-restriction.ts. GPL-2.0-or-later. */
#include "qa/content/q3/product_restriction.h"
#include "qa/foundation/support.h"
static const uint8_t SCRAMBLED[] = {
  220,129,255,108,244,163,171,55,133,65,199,36,140,222,53,99,
  65,171,175,232,236,193,210,250,169,104,231,231,21,201,170,208,
  135,175,130,136,85,215,71,23,96,32,96,83,44,240,219,138,
  184,215,73,27,196,247,55,139,148,68,78,203,213,238,139,23,
  45,205,118,186,236,230,231,107,212,1,10,98,30,20,116,180,
  216,248,166,35,45,22,215,229,35,116,250,167,117,3,57,55,
  201,229,218,222,128,12,141,149,32,110,168,215,184,53,31,147,
  62,12,138,67,132,54,125,6,221,148,140,4,21,44,198,3,
  126,12,100,236,61,42,44,251,15,135,14,134,89,92,177,246,
  152,106,124,78,118,80,28,42,
};
qa_q3_mount_restriction qa_q3_resolve_mount_restriction(qa_q3_product_policy policy, bool fs_restrict,
    qa_q3_read_product_id_fn read_product_id, void *read_product_id_ctx){
  qa_q3_mount_restriction forced=qa_q3_mount_restriction_make(policy,fs_restrict);
  if(forced.kind==QA_Q3_MOUNT_DEMO) return forced;
  const uint8_t *product_id=NULL; size_t length=0;
  if(!read_product_id(read_product_id_ctx,&product_id,&length)) return qa_q3_mount_restriction_make(policy,true);
  int32_t seed=5000;
  for(size_t i=0;i<sizeof SCRAMBLED;i++){
    uint8_t expected=(uint8_t)(SCRAMBLED[i]^(seed&255));
    uint8_t actual=i<length?product_id[i]:0;
    if(expected!=actual) qa_range_error("Invalid product identification");
    seed=(int32_t)((uint32_t)69069*(uint32_t)seed+1u);
  }
  return forced;
}
