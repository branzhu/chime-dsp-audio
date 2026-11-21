# Requirements stay discoverable during IDF early expansion.
set(sources)
if(CONFIG_CHIME_BT_LHDC_V5)
    set(sources "chime_lhdc.c")
endif()
idf_component_register(SRCS ${sources} INCLUDE_DIRS "." PRIV_REQUIRES chime_codec_reference)
