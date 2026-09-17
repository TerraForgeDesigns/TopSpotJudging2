#include "judging_model.h"

#include <stdio.h>
#include <string.h>

static void copy_text(char *dest, size_t dest_size, const char *value)
{
    if (dest_size == 0) {
        return;
    }
    snprintf(dest, dest_size, "%s", value != NULL ? value : "");
}

void top_spot_load_default_development_show(top_spot_show_config_t *show)
{
    memset(show, 0, sizeof(*show));
    show->loaded = true;
    show->show_id = 0;
    copy_text(show->show_name, sizeof(show->show_name), "No Show Loaded");
    show->config_revision = 0;
    show->data_revision = 0;
    show->score_min = 1;
    show->score_max = TOP_SPOT_DEFAULT_SCORE_MAX;
    show->category_count = 0;
    show->award_count = 0;
}

void top_spot_apply_show_to_current(top_spot_record_t *record, const top_spot_show_config_t *show)
{
    record->show_id = show->show_id;
    copy_text(record->show_name, sizeof(record->show_name), show->show_name);
    record->show_config_revision = show->config_revision;
    record->score_min = show->score_min > 0 ? show->score_min : 1;
    record->score_range_max = show->score_max > 0 ? show->score_max : TOP_SPOT_DEFAULT_SCORE_MAX;
    if (record->score_range_max > TOP_SPOT_MAX_SCORE_RANGE) {
        record->score_range_max = TOP_SPOT_MAX_SCORE_RANGE;
    }
    record->score_count = show->category_count;
    record->max_score = record->score_count * record->score_range_max;
    for (int i = 0; i < show->category_count && i < TOP_SPOT_MAX_CATEGORIES; i++) {
        record->score_category_ids[i] = show->categories[i].id;
        copy_text(record->score_category_names[i], sizeof(record->score_category_names[i]),
                  show->categories[i].name);
    }
    record->award_count = show->award_count;
    for (int i = 0; i < show->award_count && i < TOP_SPOT_MAX_AWARDS; i++) {
        record->award_ids[i] = show->awards[i].id;
        copy_text(record->award_names[i], sizeof(record->award_names[i]), show->awards[i].name);
    }
}

void top_spot_start_new_car(top_spot_app_state_t *state)
{
    memset(&state->current, 0, sizeof(state->current));
    top_spot_apply_show_to_current(&state->current, &state->show);
    state->category_index = 0;
}

void top_spot_state_init(top_spot_app_state_t *state)
{
    memset(state, 0, sizeof(*state));
    top_spot_load_default_development_show(&state->show);
    top_spot_start_new_car(state);
}

bool top_spot_save_current(top_spot_app_state_t *state)
{
    if (state->saved_count >= TOP_SPOT_MAX_SAVED_RECORDS) {
        return false;
    }

    state->saved[state->saved_count] = state->current;
    state->saved_count++;
    return true;
}

int top_spot_total_score(const top_spot_record_t *record)
{
    int total = 0;
    for (int i = 0; i < record->score_count && i < TOP_SPOT_MAX_CATEGORIES; i++) {
        total += record->scores[i];
    }
    return total;
}

int top_spot_record_max_score(const top_spot_record_t *record)
{
    if (record->max_score > 0) {
        return record->max_score;
    }
    int score_range_max = record->score_range_max > 0 ? record->score_range_max : TOP_SPOT_DEFAULT_SCORE_MAX;
    return record->score_count * score_range_max;
}

int top_spot_selected_nomination_count(const top_spot_record_t *record)
{
    int total = 0;
    for (int i = 0; i < record->award_count && i < TOP_SPOT_MAX_AWARDS; i++) {
        if (record->nominations[i]) {
            total++;
        }
    }
    return total;
}
