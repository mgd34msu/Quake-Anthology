#include "qa/network_q3_fields_save.h"
#include "save_fields.h"

bool qa_q3_save_entity_fields(qa_net_writer *writer, const qa_q3_entity *value)
{ return q3_save_entity(writer, value); }
bool qa_q3_restore_entity_fields(qa_net_reader *reader, qa_q3_entity *value)
{ return q3_restore_entity(reader, value); }
bool qa_q3_save_player_fields(qa_net_writer *writer, const qa_q3_player *value)
{ return q3_save_player(writer, value); }
bool qa_q3_restore_player_fields(qa_net_reader *reader, qa_q3_player *value, qa_q3_product product)
{ return q3_restore_player(reader, value, product); }
bool qa_q3_save_usercmd_fields(qa_net_writer *writer, const qa_q3_usercmd *value)
{ return q3_save_usercmd(writer, value); }
bool qa_q3_restore_usercmd_fields(qa_net_reader *reader, qa_q3_usercmd *value)
{ return q3_restore_usercmd(reader, value); }
bool qa_q3_save_gamestate_fields(qa_net_writer *writer, const qa_q3_gamestate *value)
{ return q3_save_gamestate(writer, value); }
bool qa_q3_restore_gamestate_fields(qa_net_reader *reader, qa_q3_gamestate *value)
{ return q3_restore_gamestate(reader, value); }
