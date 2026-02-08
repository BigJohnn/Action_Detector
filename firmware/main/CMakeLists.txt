idf_component_register(
    SRCS "app_main.c" "bmi270_i2c.c" "udp_sender.c" "speaker_audio.c" "label_audio.c"
    INCLUDE_DIRS "."
    EMBED_FILES
        "audio_labels/swipe_left.pcm"
        "audio_labels/swipe_right.pcm"
        "audio_labels/idle.pcm"
    REQUIRES
        esp_driver_i2s
        esp_driver_i2c
        esp_driver_gpio
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        esp_system
        lwip
        freertos
        nvs_flash
)
