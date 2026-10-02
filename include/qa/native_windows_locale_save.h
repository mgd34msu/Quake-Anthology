#ifndef QA_NATIVE_WINDOWS_LOCALE_SAVE_H
#define QA_NATIVE_WINDOWS_LOCALE_SAVE_H
#include "qa/native_windows_locale.h"
#include "qa/source_save.h"

static inline bool qa_native_windows_locale_save(qa_source_save_io *io, qa_native_windows_locale *locale)
{
    if (!qa_source_save_u32(io,&locale->lcid) || !qa_source_save_u32(io,&locale->language_id) ||
        !qa_source_save_u32(io,&locale->ansi_code_page) ||
        !qa_source_save_u32(io,&locale->oem_code_page)) return false;
    uint16_t *fields[] = {locale->decimal,locale->thousands,locale->grouping,
        locale->default_decimal,locale->default_thousands,locale->default_grouping};
    for (size_t i = 0; i < sizeof(fields)/sizeof(*fields); ++i)
        for (size_t j = 0; j < QA_NATIVE_WINDOWS_LOCALE_UNITS; ++j)
            if (!qa_source_save_u16(io,fields[i] + j)) return false;
    return true;
}
static inline bool qa_native_windows_locale_profile_save(qa_source_save_io *io,
    qa_native_windows_locale_profile *profile)
{
    if (!qa_source_save_u32(io,&profile->source) || !qa_source_save_u32(io,&profile->ansi_code_page) ||
        !qa_source_save_u32(io,&profile->oem_code_page) || !qa_native_windows_locale_save(io,&profile->user) ||
        !qa_native_windows_locale_save(io,&profile->system)) return false;
    if (qa_native_windows_locale_profile_valid(profile)) return true;
    qa_error_set(io->error,QA_ERROR_FORMAT,io->offset,"Windows locale snapshot is invalid"); return false;
}
#endif
