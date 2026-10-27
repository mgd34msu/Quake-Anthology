/* Port of src/compat/q2/native-input.ts (readClassicUserCommand;
 * NativeInputBinding needs classic/rerelease hosts, see unresolved). */
#include "qa/compat/q2/native_input.h"
static int16_t get16(const uint8_t *v,size_t o){ return (int16_t)(uint16_t)(v[o]|(v[o+1]<<8)); }
qa_q2_user_command qa_compat_q2_read_classic_user_command(const uint8_t *view) {
  qa_q2_user_command c; c.milliseconds=view[0]; c.buttons=view[1];
  c.angle_shorts.values[0]=get16(view,2); c.angle_shorts.values[1]=get16(view,4); c.angle_shorts.values[2]=get16(view,6);
  c.forward_move=get16(view,8); c.side_move=get16(view,10); c.up_move=get16(view,12); c.impulse=view[14]; c.light_level=view[15]; return c;
}
void qa_compat_q2_native_input_binding_close(qa_compat_q2_native_input_binding *s) { s->closed=true; }
