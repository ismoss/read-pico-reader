set(kiiko_selected_sources "read_pico_pmu.c" "pico_soc_filter.c")
idf_component_register(
    SRCS ${kiiko_selected_sources}
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES freertos esp_rom esp_timer
)
