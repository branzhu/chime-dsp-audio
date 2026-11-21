idf_component_register(
    SRCS "system_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES board_support dsp_control amp_control audio_pipeline input_control freertos log
)
