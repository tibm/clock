idf_component_register(SRCS "app_main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES cli core command clk_hal board_cfg
                       PRIV_REQUIRES nvs_flash esp_system)
