idf_component_register(
    SRCS "dsp_control.c" "dsp_transfer.c"
    INCLUDE_DIRS "include"
    REQUIRES board_support freertos log
)
