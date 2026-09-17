#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TOP_SPOT_MAX_CATEGORIES 12
#define TOP_SPOT_MAX_AWARDS 16
#define TOP_SPOT_MAX_SAVED_RECORDS 32
#define TOP_SPOT_MAX_CACHED_CARS 360
#define TOP_SPOT_DEFAULT_SCORE_MAX 5
#define TOP_SPOT_MAX_SCORE_RANGE 25

typedef struct {
    int id;
    char key[24];
    char name[48];
    int sort_order;
} top_spot_category_t;

typedef struct {
    int id;
    char key[24];
    char name[48];
} top_spot_award_t;

typedef struct {
    bool loaded;
    int show_id;
    char show_name[80];
    char show_date[16];
    int config_revision;
    int data_revision;
    int score_min;
    int score_max;
    int category_count;
    int award_count;
    top_spot_category_t categories[TOP_SPOT_MAX_CATEGORIES];
    top_spot_award_t awards[TOP_SPOT_MAX_AWARDS];
} top_spot_show_config_t;

typedef struct {
    char local_record_id[16];
    char handheld_id[24];
    int show_id;
    char show_name[80];
    int show_config_revision;
    int closed_at_uptime_ms;
    int score_min;
    int score_range_max;
    int max_score;
    char entry_number[8];
    char participant[80];
    char year[8];
    char make[48];
    char model[48];
    int score_count;
    int score_category_ids[TOP_SPOT_MAX_CATEGORIES];
    char score_category_names[TOP_SPOT_MAX_CATEGORIES][48];
    int scores[TOP_SPOT_MAX_CATEGORIES];
    int award_count;
    int award_ids[TOP_SPOT_MAX_AWARDS];
    char award_names[TOP_SPOT_MAX_AWARDS][48];
    bool nominations[TOP_SPOT_MAX_AWARDS];
    bool vehicle_photo_captured;
    bool judge_sheet_photo_captured;
    char vehicle_photo_path[128];
    char judge_sheet_photo_path[128];
    bool vehicle_photo_missing;
    bool judge_sheet_photo_missing;
    bool submitted;
    char sync_state[16];
    int retry_count;
    char last_error[96];
    char file_path[160];
} top_spot_record_t;

typedef struct {
    int id;
    int data_revision;
    char entry_number[8];
    char participant[80];
    char year[8];
    char make[48];
    char model[48];
    char vehicle_type[24];
    char status[20];
    char judged_source[24];
    char judged_at[32];
} top_spot_car_status_t;

typedef struct {
    bool loaded;
    int show_id;
    int data_revision;
    int car_count;
    top_spot_car_status_t cars[TOP_SPOT_MAX_CACHED_CARS];
} top_spot_car_cache_t;

typedef struct {
    top_spot_show_config_t show;
    top_spot_car_cache_t car_cache;
    top_spot_record_t current;
    top_spot_record_t saved[TOP_SPOT_MAX_SAVED_RECORDS];
    int saved_count;
    int category_index;
} top_spot_app_state_t;

void top_spot_state_init(top_spot_app_state_t *state);
void top_spot_start_new_car(top_spot_app_state_t *state);
bool top_spot_save_current(top_spot_app_state_t *state);
int top_spot_total_score(const top_spot_record_t *record);
int top_spot_record_max_score(const top_spot_record_t *record);
int top_spot_selected_nomination_count(const top_spot_record_t *record);
void top_spot_load_default_development_show(top_spot_show_config_t *show);
void top_spot_apply_show_to_current(top_spot_record_t *record, const top_spot_show_config_t *show);
