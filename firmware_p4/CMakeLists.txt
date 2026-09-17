cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{ESP_IDF_VERSION} OR "$ENV{ESP_IDF_VERSION}" STREQUAL "")
    set(ENV{ESP_IDF_VERSION} "5.5")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-format)
add_compile_options(-Wno-error=attributes)

project(top_spot_waveshare_p4_bringup)

set(TOP_SPOT_EXPECTED_HOSTED_CONFIG
    "SDIO transport, 4-bit, 40000 KHz, CLK18/CMD19/D0-14/D1-15/D2-16/D3-17/RESET54")

if(NOT CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE OR CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE)
    message(FATAL_ERROR
        "Top Spot P4 requires ESP-Hosted ${TOP_SPOT_EXPECTED_HOSTED_CONFIG}; "
        "refusing to build a non-SDIO transport configuration.")
endif()

if(NOT CONFIG_ESP_HOSTED_SDIO_4_BIT_BUS
        OR NOT "${CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ}" STREQUAL "40000"
        OR NOT "${CONFIG_ESP_HOSTED_SDIO_PIN_CLK}" STREQUAL "18"
        OR NOT "${CONFIG_ESP_HOSTED_SDIO_PIN_CMD}" STREQUAL "19"
        OR NOT "${CONFIG_ESP_HOSTED_SDIO_PIN_D0}" STREQUAL "14"
        OR NOT "${CONFIG_ESP_HOSTED_SDIO_PIN_D1}" STREQUAL "15"
        OR NOT "${CONFIG_ESP_HOSTED_SDIO_PIN_D2}" STREQUAL "16"
        OR NOT "${CONFIG_ESP_HOSTED_SDIO_PIN_D3}" STREQUAL "17"
        OR NOT "${CONFIG_ESP_HOSTED_GPIO_SLAVE_RESET_SLAVE}" STREQUAL "54")
    message(FATAL_ERROR
        "Top Spot P4 ESP-Hosted config must remain ${TOP_SPOT_EXPECTED_HOSTED_CONFIG}.")
endif()
