idf_component_register(
    SRCS
        "zectrix_board.cc"
        "charge_status.cc"
        "i2c_bus_lock.cc"
        "i2c_device.cc"
        "rtc_pcf8563.cc"
        "zectrix_nfc.cc"
        "audio_codec.cc"
        "es8311_audio_codec.cc"
    INCLUDE_DIRS "." "include"
    REQUIRES
        esp_adc
        esp_codec_dev
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_timer
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
