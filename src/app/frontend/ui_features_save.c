#include "ui_features_private.h"
#include "save_private.h"
#include "qa/media_captions_save.h"
#include "cinematic_captions.h"
#include "save_menu.h"

static bool view_key(void *context, const qa_vfs *view, uint64_t *out, qa_error *error)
{
    uint64_t key = qa_application_content_view_id(context, view);
    if (!view || !out || !key) return frontend_fail(error, QA_ERROR_FORMAT, "Caption view leaves its actual content graph");
    *out = key; return true;
}
static bool claim_view(void *context, uint64_t key, qa_vfs **out, qa_error *error)
{ return qa_application_content_claim_view(context, key, out, error); }
static bool blob(qa_source_save_io *io, qa_buffer *owned, qa_bytes *input)
{
    size_t size = io->direction == QA_SOURCE_SAVE_WRITE ? owned->size : 0;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, owned->data, size);
    if (size > io->input.size - io->offset) return false;
    *input = (qa_bytes){io->input.data + io->offset, size}; io->offset += size; return true;
}
static bool fixed_text(qa_source_save_io *io, char *text, size_t capacity)
{ return qa_source_save_bytes(io, text, capacity) && memchr(text, 0, capacity) != NULL; }
static bool short_text(qa_source_save_io *io,char *text,size_t capacity)
{
    size_t length=io->direction==QA_SOURCE_SAVE_WRITE?strlen(text):0;
    if(!qa_source_save_count(io,&length,capacity-1) || !qa_source_save_bytes(io,text,length) || memchr(text,0,length))return false;
    text[length]=0;return true;
}
static bool error_fields(qa_source_save_io *io, qa_error *error)
{
    uint32_t code = error->code;
    if (!qa_source_save_u32(io, &code) || code > QA_ERROR_NOT_FOUND ||
        !qa_source_save_count(io, &error->offset, SIZE_MAX) || !fixed_text(io, error->message, sizeof(error->message))) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) error->code = (qa_status)code;
    return true;
}
static bool save_fields(qa_source_save_io *io, frontend_ui_seat_features *seat)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = seat->saves.count;
    if (!qa_source_save_count(io, &count, reading ? (io->input.size - io->offset) / 550 : SIZE_MAX / sizeof(qa_save_slot_entry))) return false;
    if (reading && count) {
        if (count > SIZE_MAX / sizeof(qa_ui_row) || count > SIZE_MAX / 256 || count>SIZE_MAX/sizeof(qa_error)) return false;
        seat->saves.entries = calloc(count, sizeof(*seat->saves.entries));
        seat->save_rows = calloc(count, sizeof(*seat->save_rows)); seat->save_details = calloc(count, 256);
        seat->save_qualification=calloc(count,sizeof(*seat->save_qualification));
        if (!seat->saves.entries || !seat->save_rows || !seat->save_details || !seat->save_qualification)
            return frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring actual save-menu reservations");
        seat->saves.count = count;
    }
    if (count && (!seat->saves.entries || !seat->save_qualification)) return false;
    for (size_t i = 0; i < count; ++i) {
        qa_save_slot_entry *entry = seat->saves.entries + i; uint32_t format=entry->format;
        if (!qa_source_save_owned_text(io, &entry->name) || !entry->name || strncmp(entry->name, "saves/", 6) ||
            !qa_save_slot_name(entry->name, io->error) || (i && strcmp(seat->saves.entries[i - 1].name, entry->name) >= 0) ||
            !qa_source_save_u32(io,&format) || format>QA_SAVE_SLOT_Q1_V6) return false;
        if (reading) entry->format=(qa_save_slot_format)format;
        if (format==QA_SAVE_SLOT_SHARED) {
            uint32_t purpose=entry->metadata.purpose;
            if (!qa_source_save_u32(io,&purpose) || purpose>QA_SAVE_DEMO_KEYFRAME ||
                !qa_source_save_u64(io,&entry->metadata.elapsed_ns) ||
                !qa_source_save_u64(io,&entry->metadata.configuration_generation) ||
                !qa_source_save_u64(io,&entry->metadata.world_generation) ||
                !short_text(io,entry->metadata.map,sizeof(entry->metadata.map)) ||
                !short_text(io,entry->metadata.game,sizeof(entry->metadata.game))) return false;
            if (reading) entry->metadata.purpose=(qa_save_purpose)purpose;
        } else {
            qa_q1_save_slot_metadata *source=&entry->source;
            if (!qa_source_save_owned_text(io,&source->comment) || !qa_source_save_owned_text(io,&source->map) ||
                !qa_source_save_owned_text(io,&source->game_directories) || !qa_source_save_owned_text(io,&source->world_message) ||
                !qa_source_save_owned_text(io,&source->killed_monsters) || !qa_source_save_owned_text(io,&source->total_monsters) ||
                !qa_source_save_owned_text(io,&source->found_secrets) || !qa_source_save_owned_text(io,&source->total_secrets) ||
                !qa_source_save_f64(io,&source->time) || !isfinite(source->time) ||
                !qa_source_save_i32(io,&source->skill) || !qa_source_save_count(io,&source->entity_count,SIZE_MAX) ||
                !qa_source_save_bool(io,&source->player_record_present) ||
                !short_text(io,seat->save_details+count*128+i*128,128)) return false;
        }
        if (!error_fields(io,&entry->error) || !error_fields(io,seat->save_qualification+i)) return false;
        if (entry->error.code==QA_OK && format!=QA_SAVE_SLOT_SHARED &&
            (!entry->source.comment || !entry->source.map || !*entry->source.map || entry->source.entity_count<2)) return false;
    }
    if (!qa_source_save_count(io,&seat->selected_save,SIZE_MAX) ||
        (!count?seat->selected_save!=0:seat->selected_save>=count) || !qa_source_save_u64(io,&seat->save_revision) ||
        !qa_source_save_owned_text(io,&seat->save_name) || !fixed_text(io,seat->save_error,sizeof(seat->save_error)) ||
        !qa_source_save_bool(io,&seat->overwrite)) return false;
    size_t choices=seat->save_product_count;
    if (!qa_source_save_count(io,&choices,reading?(io->input.size-io->offset)/10:SIZE_MAX/sizeof(char *)) ||
        choices==1 || (choices && (!count || seat->saves.entries[seat->selected_save].format==QA_SAVE_SLOT_SHARED ||
            seat->saves.entries[seat->selected_save].error.code!=QA_OK))) return false;
    if (reading && choices) {
        seat->save_product_keys=calloc(choices,sizeof(*seat->save_product_keys));
        seat->save_product_labels=calloc(choices,sizeof(*seat->save_product_labels));
        seat->save_product_count=choices;
        if (!seat->save_product_keys || !seat->save_product_labels)
            return frontend_fail(io->error,QA_ERROR_MEMORY,"Restoring actual source save product choices");
    }
    if (choices && (!seat->save_product_keys || !seat->save_product_labels)) return false;
    for (size_t i=0;i<choices;++i) {
        if (!qa_source_save_owned_text(io,seat->save_product_keys+i) || !qa_source_save_owned_text(io,seat->save_product_labels+i) ||
            !seat->save_product_labels[i] || !*seat->save_product_labels[i] ||
            (!i?seat->save_product_keys[i]!=NULL:(!seat->save_product_keys[i] || !*seat->save_product_keys[i]))) return false;
        for (size_t j=1;j<i;++j) if (!strcmp(seat->save_product_keys[j],seat->save_product_keys[i])) return false;
    }
    return qa_source_save_count(io,&seat->selected_product,SIZE_MAX) &&
        (!choices?!seat->selected_product:seat->selected_product<choices);
}
static bool campaign_fields(qa_source_save_io *io,qa_frontend *f,frontend_ui_seat_features *seat)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    (void)f;
    if (!qa_source_save_owned_text(io,&seat->campaign_instance) || !qa_source_save_owned_text(io,&seat->shown_instance) ||
        !qa_source_save_u64(io,&seat->campaign_generation) || !qa_source_save_u64(io,&seat->shown_generation) ||
        !qa_source_save_u64(io,&seat->shown_map_revision) || !qa_source_save_u64(io,&seat->shown_command_generation) ||
        !qa_source_save_i32(io,&seat->shown_intermission) || !qa_source_save_i32(io,&seat->campaign_level) ||
        !qa_source_save_i32(io,&seat->campaign_tier) || !qa_source_save_bool(io,&seat->campaign_selected) ||
        !qa_source_save_bool(io,&seat->shown_result) ||
        !qa_source_save_count(io,&seat->campaign_capacity,SIZE_MAX/512) ||
        seat->campaign_capacity>SIZE_MAX/sizeof(qa_ui_row) ||
        (seat->campaign_selected && (!seat->campaign_instance || !*seat->campaign_instance)) ||
        (seat->shown_result && (!seat->shown_instance || !*seat->shown_instance))) return false;
    for (size_t i=0;i<seat->campaign_capacity;++i) { uint8_t zero=0; if (!qa_source_save_u8(io,&zero) || zero) return false; }
    if (reading && seat->campaign_capacity) {
        seat->campaign_rows=calloc(seat->campaign_capacity,sizeof(*seat->campaign_rows));
        seat->campaign_labels=calloc(seat->campaign_capacity,512);
        if (!seat->campaign_rows || !seat->campaign_labels) return frontend_fail(io->error,QA_ERROR_MEMORY,"Restoring actual arena menu reservations");
    }
    return true;
}
static bool fields(qa_source_save_io *io, qa_frontend *f, const qa_audio_asset_inventory *assets)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, dedicated = f->options.dedicated;
    uint8_t magic[4] = {'Q','F','U','F'}; uint32_t seats = f->options.seats;
    frontend_ui_features *owner = f->ui_features;
    uint32_t profile=owner->ui_profile;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QFUF", 4) || !qa_source_save_u32(io, &seats) || seats != f->options.seats || !qa_source_save_bool(io, &dedicated) || dedicated != f->options.dedicated ||
        !error_fields(io, &owner->audio_error) || !qa_source_save_u32(io,&profile) || profile>QA_LOCALIZATION_Q2_RERELEASE) return false;
    if (reading) owner->ui_profile=(qa_localization_profile)profile;
    qa_buffer compiled = {0}; qa_bytes input = {0};
    bool ok = reading || qa_caption_library_checkpoint(owner->tracks, &compiled, io->error);
    ok = ok && blob(io, &compiled, &input);
    if (ok && reading) ok = qa_caption_library_restore(owner->tracks, input, io->error);
    qa_buffer_free(&compiled);
    if (!ok) return false;
    ok = reading || qa_localization_pool_checkpoint(owner->catalogs, &compiled, io->error);
    ok = ok && blob(io, &compiled, &input);
    if (ok && reading) ok = qa_localization_pool_restore(owner->catalogs, input, io->error);
    qa_buffer_free(&compiled);
    if (!ok) return false;
    qa_sound_caption_save_refs refs = {.assets = assets, .context = qa_application_content_graph_read(f->application),
        .view_key = view_key, .claim_view = claim_view};
    if (!refs.context) return false;
    for (unsigned i = 0; !dedicated && i < seats; ++i) {
        frontend_ui_seat_features *seat = owner->seats + i;
        if (!seat->captions ||
            !save_fields(io, seat) || !campaign_fields(io,f,seat)) return false;
        ok=reading || frontend_save_menu_checkpoint(f->seats+i,&compiled,io->error);
        ok=ok && blob(io,&compiled,&input);
        if(ok && reading)ok=frontend_save_menu_restore(f->seats+i,input,io->error);
        qa_buffer_free(&compiled);
        if(!ok)return false;
        uint64_t catalog=0;
        if (!reading && !qa_localization_pool_catalog_key(owner->catalogs,seat->localization,&catalog)) return false;
        if (!qa_source_save_owned_text(io,&seat->language) || !qa_source_save_u64(io,&catalog) ||
            ((seat->language!=NULL)!=(catalog!=0))) return false;
        if (reading) {
            seat->localization=qa_localization_pool_catalog(owner->catalogs,catalog);
            if (catalog && !seat->localization) return false;
            qa_localization_retain(seat->localization);
        }
        ok = reading || qa_sound_captions_checkpoint(seat->captions, &refs, &compiled, io->error);
        ok = ok && blob(io, &compiled, &input);
        if (ok && reading) ok = qa_sound_captions_restore(seat->captions, &refs, input, io->error);
        qa_buffer_free(&compiled);
        if (!ok) return false;
    }
    return frontend_ui_cinematic_fields(io,f);
}
static bool qualified(qa_frontend *f, const qa_audio_asset_inventory *assets, qa_error *error)
{
    if (!f || !f->application || !f->ui_features || !frontend_ui_features_idle(f) ||
        f->stepping || !assets || !f->options.seats || f->options.seats > QA_INPUT_LOCAL_SEATS ||
        (f->audio && !qa_audio_engine_observer_is(f->audio, frontend_ui_audio_event, f)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "UI feature continuation requires its idle actual factory and inventory");
    return true;
}
bool frontend_ui_features_checkpoint(qa_frontend *f, const qa_audio_asset_inventory *assets, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !qualified(f, assets, error)) return false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, f, assets) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "UI features leave their actual continuation owners");
    return ok;
}
bool frontend_ui_features_restore(qa_frontend *f, const qa_audio_asset_inventory *assets, qa_bytes bytes, qa_error *error)
{
    if (!qualified(f, assets, error) || !f->source_restoring) return false;
    for (unsigned i = 0; i < QA_INPUT_LOCAL_SEATS; ++i) if (f->ui_features->seats[i].saves.entries ||
        f->ui_features->seats[i].save_rows || f->ui_features->seats[i].save_details ||
        f->ui_features->seats[i].save_qualification || f->ui_features->seats[i].save_product_keys ||
        f->ui_features->seats[i].save_product_labels || f->ui_features->seats[i].save_product_count ||
        f->ui_features->seats[i].selected_product ||
        f->ui_features->seats[i].save_name ||
        f->ui_features->seats[i].localization || f->ui_features->seats[i].language ||
        f->ui_features->seats[i].campaign_rows || f->ui_features->seats[i].campaign_labels ||
        f->ui_features->seats[i].campaign_instance || f->ui_features->seats[i].shown_instance) return false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, f, assets) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Saved UI features leave their isolated actual owners");
    return ok;
}
