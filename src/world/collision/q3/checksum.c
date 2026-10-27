/* Port of src/world/collision/q3/checksum.ts. GPL-2.0-or-later. */
#include "qa/world/collision/q3/checksum.h"
#include "qa/core/md4.h"
#include "qa/foundation/support.h"
#include <math.h>
static int safe_int(double v){return isfinite(v)&&v==floor(v)&&v>=-9007199254740991.0&&v<=9007199254740991.0;}
double qa_collision_lump_checksum(const uint8_t*bytes,size_t len,qa_collision_checksum_lump lump){
  if(!safe_int(lump.offset)||!safe_int(lump.length)||lump.offset<0||lump.length<0||lump.offset>(double)len-lump.length)
    qa_range_error("CM_LumpChecksum: lump outside source allocation");
  return (double)(int32_t)qa_block_checksum(bytes+(size_t)lump.offset,(size_t)lump.length);
}
double qa_collision_checksum(const uint8_t*bytes,size_t len,const qa_collision_checksum_lump*lumps,size_t count){
  uint8_t buf[44]={0};
  static const int indexes[11]={1,4,6,5,2,9,8,7,3,13,10};
  for(int i=0;i<11;i++){
    int li=indexes[i];
    if((size_t)li>=count)qa_range_error("CM_Checksum: missing header lump %d",li);
    uint32_t v=(uint32_t)(int32_t)qa_collision_lump_checksum(bytes,len,lumps[li]);
    buf[i*4]=(uint8_t)v;buf[i*4+1]=(uint8_t)(v>>8);buf[i*4+2]=(uint8_t)(v>>16);buf[i*4+3]=(uint8_t)(v>>24);
  }
  return (double)(int32_t)qa_block_checksum(buf,sizeof buf);
}
