#pragma once

#include "amp_control.h"
#include "audio_pipeline.h"
#include "board_support.h"
#include "dsp_control.h"
#include "esp_err.h"
#include "input_control.h"

typedef enum {
    SYSTEM_STATE_BOARD_READY = 0,
    SYSTEM_STATE_DSP_BOOTING,
    SYSTEM_STATE_DSP_READY,
    SYSTEM_STATE_AMP_READY,
    SYSTEM_STATE_RUNNING,
    SYSTEM_STATE_MUTED,
    SYSTEM_STATE_STANDBY,
    SYSTEM_STATE_FAULT,
    SYSTEM_STATE_SAFE,
} system_state_t;

typedef struct {
    board_context_t *board;
    dsp_controller_t *dsp;
    amp_controller_t *amplifiers;
    audio_pipeline_t *audio;
    input_controller_t *input;
    system_state_t state;
} system_manager_t;

esp_err_t system_manager_init(
    system_manager_t *system,
    board_context_t *board,
    dsp_controller_t *dsp,
    amp_controller_t *amplifiers,
    audio_pipeline_t *audio,
    input_controller_t *input);
const char *system_manager_state_name(system_state_t state);
void system_manager_run(system_manager_t *system);
