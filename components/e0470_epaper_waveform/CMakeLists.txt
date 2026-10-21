set(kiiko_selected_sources "e0470_epaper_waveform.c" "e0470_waveform_trim.c")
idf_component_register(
    SRCS ${kiiko_selected_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "waveforms"
    REQUIRES epdiy
)
