#ifndef QA_NETWORK_Q2_WIRE_SAVE_H
#define QA_NETWORK_Q2_WIRE_SAVE_H
#include "qa/network_q2_messages.h"
#include "qa/source_save.h"

/* Explicit native continuation fields. These are not live wire encoders:
 * exact source floats, inactive fields and physical history slots survive. */
bool qa_q2_save_protocol(qa_source_save_io *, qa_net_protocol_id *);
bool qa_q2_save_channel_options(qa_source_save_io *, qa_q2_channel_options *);
bool qa_q2_save_codec(qa_source_save_io *, qa_q2_codec *);
bool qa_q2_save_usercmd(qa_source_save_io *, qa_q2_usercmd *);
bool qa_q2_save_entity(qa_source_save_io *, qa_q2_entity *);
bool qa_q2_save_player(qa_source_save_io *, qa_q2_player *);
bool qa_q2_save_serverdata(qa_source_save_io *, qa_q2_serverdata *);
bool qa_q2_save_frame(qa_source_save_io *, qa_q2_wire_frame *empty_on_read);
bool qa_q2_save_history(qa_source_save_io *, qa_q2_frame_history **empty_on_read);
bool qa_q2_save_channel(qa_source_save_io *, qa_q2_channel **empty_on_read);
bool qa_q2_save_messages(qa_source_save_io *, const qa_q2_message_options *candidate_options,
    qa_q2_messages **empty_on_read);
bool qa_q2_save_record(qa_source_save_io *, qa_q2_server_record *empty_on_read);
void qa_q2_saved_record_free(qa_q2_server_record *);
#endif
