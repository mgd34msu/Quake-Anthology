#ifndef QA_FRONTEND_SHARED_STORAGE_H
#define QA_FRONTEND_SHARED_STORAGE_H
#include "internal.h"
#include "qa/settings.h"
#include "qa/persistence_content.h"

typedef struct frontend_shared_storage frontend_shared_storage;
typedef struct frontend_shared_audio_preferences {
    qa_audio_output_format format;
    double effects,music;
    const char *device,*menu_track;
    bool present,has_shuffle,shuffle,has_menu_track;
} frontend_shared_audio_preferences;
typedef struct frontend_shared_view_preferences {
    double field_of_view;
    bool present;
} frontend_shared_view_preferences;
/* The configured user root owns images, its real console child owns device
 * preferences, and an optional genuine sticky product ConfigStore owns audio
 * and view preferences. An absent product is exactly {NULL,0}. Dedicated
 * preparation admits device preferences without reading image/audio files. */
bool frontend_shared_storage_open(qa_settings_store user,qa_settings_store devices,
    qa_settings_store input,bool graphical,
    frontend_shared_storage **,qa_error *);
bool frontend_shared_storage_current(const frontend_shared_storage *,qa_settings_store user,
    qa_settings_store devices,qa_settings_store input,bool graphical);
/* Admit the first actual sticky product without reopening global preferences.
 * Once admitted, only the same physical input root is accepted. */
bool frontend_shared_storage_adopt_input(frontend_shared_storage *,qa_settings_store,qa_error *);
/* Pure borrowed authority; exact zero means no product has been admitted. */
bool frontend_shared_storage_input(const frontend_shared_storage *,qa_settings_store *);
qa_bytes frontend_shared_storage_images(const frontend_shared_storage *);
const qa_cvar_archive *frontend_shared_storage_archive(const frontend_shared_storage *);
const frontend_shared_audio_preferences *frontend_shared_storage_audio(const frontend_shared_storage *);
/* Loaded explicit sticky-product view override; image-touched fov has priority. */
const frontend_shared_view_preferences *frontend_shared_storage_view(const frontend_shared_storage *);
/* A fresh physical input source owns this new archive. The caller applies it
 * once under its actual candidate receipt; cached initial archives are separate. */
bool frontend_shared_storage_load_devices(const frontend_shared_storage *,qa_cvar_archive *,qa_error *);
bool frontend_shared_storage_save_images(frontend_shared_storage *,const qa_cvars *,qa_error *);
bool frontend_shared_storage_save_input(frontend_shared_storage *,const qa_cvars *,qa_error *);
bool frontend_shared_storage_save_audio(frontend_shared_storage *,const frontend_shared_audio_preferences *,qa_error *);
/* The supplied preference must come from the actual published view owner.
 * No explicit override leaves the existing file untouched. */
bool frontend_shared_storage_save_view(frontend_shared_storage *,const frontend_shared_view_preferences *,qa_error *);
bool frontend_shared_storage_visit(const frontend_shared_storage *,const qa_application_content_visitor *,qa_error *);
bool frontend_shared_storage_checkpoint(const frontend_shared_storage *,const qa_application_content_graph *,qa_buffer *,qa_error *);
/* Actual mapped stores and decoded graph owners must already exist. No
 * settings file is reopened and no script or JSON preference is replayed. */
bool frontend_shared_storage_restore(qa_application_content_graph *,qa_bytes,
    qa_settings_store user,qa_settings_store devices,qa_settings_store input,bool graphical,
    frontend_shared_storage **,qa_error *);
void frontend_shared_storage_destroy(frontend_shared_storage *);
#endif
