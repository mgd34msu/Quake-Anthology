/* ApplicationQ3Client (q3-client.ts) as the q3Client a WorldSeatPresentation takes (presentation.ts reads the client's
 * cvars, camera, prepare, frame, shared equipment, held weapons, bodies, body pose, supplemental weapon camera, weapon
 * HUD view and close). TS passes the client object itself; C passes this view of it, built once for every use
 * (createWorldSeatPresentation, replaceQ3Client after a video restart, createQ3SeatClient). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_SEAT_CLIENT_H
#define QA_APP_BOOTSTRAP_Q3_SEAT_CLIENT_H
#include "qa/app/bootstrap/presentation.h"
#include "qa/app/bootstrap/q3_client.h"
/* The view borrows client (ctx is the client): close closes it; the owner still destroys it. */
qa_app_seat_q3_client qa_app_application_q3_client_seat(qa_app_application_q3_client *client);
#endif
