cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET "esp32s31" CACHE STRING "Chime board target")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(audio_dsp_firmware)
