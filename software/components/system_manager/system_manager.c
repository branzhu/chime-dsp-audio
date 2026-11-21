#include "system_manager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "system";

const char *system_manager_state_name(system_state_t state)
{
    switch (state) {
    case SYSTEM_STATE_BOARD_READY:
        return "BOARD_READY";
    case SYSTEM_STATE_DSP_BOOTING:
        return "DSP_BOOTING";
    case SYSTEM_STATE_DSP_READY:
        return "DSP_READY";
    case SYSTEM_STATE_AMP_READY:
        return "AMP_READY";
    case SYSTEM_STATE_RUNNING:
        return "RUNNING";
    case SYSTEM_STATE_MUTED:
        return "MUTED";
    case SYSTEM_STATE_STANDBY:
        return "STANDBY";
    case SYSTEM_STATE_FAULT:
        return "FAULT";
    case SYSTEM_STATE_SAFE:
        return "SAFE";
    default:
        return "UNKNOWN";
    }
}

static void set_state(system_manager_t *system, system_state_t next_state)
{
    if (system->state != next_state) {
        ESP_LOGI(TAG, "state %s -> %s",
                 system_manager_state_name(system->state),
                 system_manager_state_name(next_state));
    }
    system->state = next_state;
}

static void enter_safe_state(system_manager_t *system, system_state_t state, esp_err_t cause)
{
    set_state(system, state);
    audio_pipeline_stop(system->audio);
    amp_controller_set_enabled(system->amplifiers, false, false);
    dsp_controller_hold_reset(system->dsp);
    board_support_set_rgb(false, false, true);
    if (cause != ESP_OK) {
        ESP_LOGW(TAG, "safe state entered: state=%s cause=%s",
                 system_manager_state_name(state), esp_err_to_name(cause));
    }
}

esp_err_t system_manager_init(
    system_manager_t *system,
    board_context_t *board,
    dsp_controller_t *dsp,
    amp_controller_t *amplifiers,
    audio_pipeline_t *audio,
    input_controller_t *input)
{
    if (system == NULL || board == NULL || dsp == NULL || amplifiers == NULL || audio == NULL || input == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    system->board = board;
    system->dsp = dsp;
    system->amplifiers = amplifiers;
    system->audio = audio;
    system->input = input;
    system->state = SYSTEM_STATE_BOARD_READY;
    ESP_LOGI(TAG, "state initialized: %s", system_manager_state_name(system->state));
    return ESP_OK;
}

void system_manager_run(system_manager_t *system)
{
    if (system == NULL) {
        return;
    }

    board_support_safe_shutdown(system->board);
    set_state(system, SYSTEM_STATE_DSP_BOOTING);

    esp_err_t err = dsp_controller_begin_boot(system->dsp);
    if (err == ESP_OK) {
        err = dsp_controller_wait_ready(system->dsp);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DSP is not ready; keeping both amplifiers disabled");
        enter_safe_state(system, SYSTEM_STATE_SAFE, err);
    } else {
        set_state(system, SYSTEM_STATE_DSP_READY);
        err = amp_controller_configure(system->amplifiers);
        if (err == ESP_OK) {
            err = audio_pipeline_start(system->audio);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "audio hardware is not configured; keeping safe state");
            enter_safe_state(system, SYSTEM_STATE_SAFE, err);
        } else {
            set_state(system, SYSTEM_STATE_AMP_READY);
            amp_controller_set_enabled(system->amplifiers, true, true);
            set_state(system, SYSTEM_STATE_RUNNING);
            board_support_set_rgb(false, true, false);
        }
    }

    input_snapshot_t input = {0};
    while (true) {
        input_controller_read(system->input, &input);
        if (system->state == SYSTEM_STATE_RUNNING && amp_controller_has_fault(system->amplifiers)) {
            ESP_LOGE(TAG, "amplifier fault detected; entering safe state");
            enter_safe_state(system, SYSTEM_STATE_FAULT, ESP_ERR_INVALID_STATE);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
