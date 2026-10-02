#ifndef QA_NATIVE_WINDOWS_LOCALE_H
#define QA_NATIVE_WINDOWS_LOCALE_H
#include "qa/common.h"
#include <string.h>

enum { QA_NATIVE_WINDOWS_LOCALE_UNITS = 16 };
/* source=0 has no acquired default; 1 maps an acquired POSIX numeric locale;
 * 2 holds actual Windows NLS user/system records. These are immutable snapshots,
 * including user overrides and their separately acquired default values. */
typedef struct qa_native_windows_locale {
    uint32_t lcid, language_id, ansi_code_page, oem_code_page;
    uint16_t decimal[QA_NATIVE_WINDOWS_LOCALE_UNITS];
    uint16_t thousands[QA_NATIVE_WINDOWS_LOCALE_UNITS];
    uint16_t grouping[QA_NATIVE_WINDOWS_LOCALE_UNITS];
    uint16_t default_decimal[QA_NATIVE_WINDOWS_LOCALE_UNITS];
    uint16_t default_thousands[QA_NATIVE_WINDOWS_LOCALE_UNITS];
    uint16_t default_grouping[QA_NATIVE_WINDOWS_LOCALE_UNITS];
} qa_native_windows_locale;
typedef struct qa_native_windows_locale_profile {
    uint32_t source, ansi_code_page, oem_code_page;
    qa_native_windows_locale user, system;
} qa_native_windows_locale_profile;
static inline bool qa_native_windows_locale_equal(const qa_native_windows_locale *a,
    const qa_native_windows_locale *b)
{
    return a->lcid == b->lcid && a->language_id == b->language_id &&
        a->ansi_code_page == b->ansi_code_page && a->oem_code_page == b->oem_code_page &&
        !memcmp(a->decimal,b->decimal,sizeof(a->decimal)) && !memcmp(a->thousands,b->thousands,sizeof(a->thousands)) &&
        !memcmp(a->grouping,b->grouping,sizeof(a->grouping)) && !memcmp(a->default_decimal,b->default_decimal,sizeof(a->default_decimal)) &&
        !memcmp(a->default_thousands,b->default_thousands,sizeof(a->default_thousands)) &&
        !memcmp(a->default_grouping,b->default_grouping,sizeof(a->default_grouping));
}
static inline bool qa_native_windows_locale_profile_equal(const qa_native_windows_locale_profile *a,
    const qa_native_windows_locale_profile *b)
{
    return a->source == b->source && a->ansi_code_page == b->ansi_code_page && a->oem_code_page == b->oem_code_page &&
        qa_native_windows_locale_equal(&a->user,&b->user) && qa_native_windows_locale_equal(&a->system,&b->system);
}
static inline bool qa_native_windows_locale_text_valid(const uint16_t *text)
{
    bool terminated = false;
    for (size_t i = 0; i < QA_NATIVE_WINDOWS_LOCALE_UNITS; ++i) {
        if (!text[i]) terminated = true;
        else if (terminated) return false;
    }
    return terminated;
}
static inline bool qa_native_windows_locale_valid(const qa_native_windows_locale *locale)
{
    const qa_native_windows_locale empty = {0};
    if (!locale->lcid) return qa_native_windows_locale_equal(locale,&empty);
    return !(locale->lcid & UINT32_C(0xfff00000)) && locale->language_id && locale->language_id <= 65535 &&
        locale->ansi_code_page <= 65535 && locale->oem_code_page <= 65535 &&
        locale->decimal[0] && locale->grouping[0] && locale->default_decimal[0] && locale->default_grouping[0] &&
        qa_native_windows_locale_text_valid(locale->decimal) && qa_native_windows_locale_text_valid(locale->thousands) &&
        qa_native_windows_locale_text_valid(locale->grouping) && qa_native_windows_locale_text_valid(locale->default_decimal) &&
        qa_native_windows_locale_text_valid(locale->default_thousands) && qa_native_windows_locale_text_valid(locale->default_grouping);
}
static inline bool qa_native_windows_locale_profile_valid(const qa_native_windows_locale_profile *profile)
{
    const qa_native_windows_locale_profile empty = {0};
    if (!profile->source) return qa_native_windows_locale_profile_equal(profile,&empty);
    if (profile->source > 2 || !profile->ansi_code_page || profile->ansi_code_page > 65535 ||
        !profile->oem_code_page || profile->oem_code_page > 65535 ||
        !qa_native_windows_locale_valid(&profile->user) || !qa_native_windows_locale_valid(&profile->system)) return false;
    if (profile->source == 1) return (profile->user.lcid == 0 || profile->user.lcid == 0x007f || profile->user.lcid == 0x0409) &&
        qa_native_windows_locale_equal(&profile->user,&profile->system);
    return profile->user.lcid && profile->system.lcid;
}
#endif
