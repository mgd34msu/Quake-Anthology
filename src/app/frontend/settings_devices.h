#ifndef QA_FRONTEND_SETTINGS_DEVICES_H
#define QA_FRONTEND_SETTINGS_DEVICES_H

#include "qa/frontend.h"
#include "qa/input_platform.h"

bool frontend_settings_keyboard_read(const qa_frontend *, int *slot);
bool frontend_settings_keyboard_initial(const qa_frontend *, int *slot, qa_error *);
bool frontend_settings_keyboard_select(qa_frontend *, int slot, qa_error *);
bool frontend_settings_controller_select(qa_frontend *, unsigned slot,
    const qa_controller_selection *, qa_error *);
bool frontend_settings_controller_read(const qa_frontend *, unsigned slot, qa_controller_info *);
bool frontend_settings_audio_select(qa_frontend *, const char *name, qa_error *);
bool frontend_settings_devices_save(qa_frontend *, qa_error *);
bool frontend_settings_gyro_prepare(qa_frontend *, qa_error *);
void frontend_settings_gyro_assignment_changed(void *, unsigned slot, int32_t previous, int32_t instance);
void frontend_settings_gyro_configuration_published(qa_frontend *);
const char *frontend_settings_gyro_message(const qa_frontend *, unsigned slot);

bool frontend_settings_devices_pending(const qa_frontend *);
bool frontend_settings_devices_drain(qa_frontend *, bool *complete, qa_error *);
bool frontend_settings_devices_destroy(qa_frontend *, qa_error *);

#endif
