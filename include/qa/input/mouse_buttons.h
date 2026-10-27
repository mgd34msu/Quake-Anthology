/*
 * Port of src/input/mouse-buttons.ts: physical inputs retain SDL
 * button IDs in events and saved seat settings; Quake numbers swap
 * the middle and right buttons. GPL-2.0-or-later.
 */
#ifndef QA_INPUT_MOUSE_BUTTONS_H
#define QA_INPUT_MOUSE_BUTTONS_H

double qa_quake_mouse_button(double physical_button);
double qa_physical_mouse_button(double quake_button);

#endif
