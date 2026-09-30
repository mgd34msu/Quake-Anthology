#ifndef QA_NETWORK_Q3_FIELDS_SAVE_H
#define QA_NETWORK_Q3_FIELDS_SAVE_H

#include "qa/network_q3.h"

/* Explicit native field streams shared by channel and guest owners. Cursors
 * retain their ordinary sticky error; the enclosing owner versions the stream,
 * qualifies history identities, and checks its complete byte extent. */
bool qa_q3_save_entity_fields(qa_net_writer *, const qa_q3_entity *);
bool qa_q3_restore_entity_fields(qa_net_reader *, qa_q3_entity *);
bool qa_q3_save_player_fields(qa_net_writer *, const qa_q3_player *);
bool qa_q3_restore_player_fields(qa_net_reader *, qa_q3_player *, qa_q3_product);
bool qa_q3_save_usercmd_fields(qa_net_writer *, const qa_q3_usercmd *);
bool qa_q3_restore_usercmd_fields(qa_net_reader *, qa_q3_usercmd *);
bool qa_q3_save_gamestate_fields(qa_net_writer *, const qa_q3_gamestate *);
bool qa_q3_restore_gamestate_fields(qa_net_reader *, qa_q3_gamestate *);

#endif
