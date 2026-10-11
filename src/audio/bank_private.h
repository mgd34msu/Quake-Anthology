#ifndef QA_AUDIO_BANK_PRIVATE_H
#define QA_AUDIO_BANK_PRIVATE_H
#include "qa/audio.h"

struct qa_audio_asset {
    atomic_uint references;
    qa_audio_sample *sample;
    qa_audio_sample *resampled[2];
    uint32_t resampled_rate[2];
    qa_resource *resource;
    qa_vfs *files;
    uint64_t resource_id;
    qa_mount_id mount;
    qa_game_family family;
    qa_audio_wav_policy policy;
    char name[];
};

typedef struct bank_entry {
    qa_audio_asset *asset;
    uint64_t touched;
    qa_string_id aliases[3];
    qa_game_family missing_family;
} bank_entry;

typedef struct bank_name {
    qa_string_id id;
    size_t row;
} bank_name;

struct qa_audio_bank {
    qa_strings *strings;
    qa_vfs *view;
    bank_entry *entries;
    size_t count, capacity;
    bank_name *names;
    size_t name_capacity;
    uint64_t registration;
    uint64_t lookup_generation;
};
void qa_audio_bank_sync(qa_audio_bank *);

#endif
