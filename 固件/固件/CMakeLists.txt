# DormMate ESP32 固件 CMakeLists.txt
# 乐鑫 IDF 项目构建配置

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 项目名称
set(PROJECT_NAME "dormmate_firmware")

# 源文件列表
set(MAIN_SRCS
    "main/main.c"
    "main/app_control.c"
    "main/app_http.c"
    "main/app_nvs.c"
    "main/app_oled.c"
    "main/app_predict.c"
    "main/app_sntp.c"
    "main/app_voice.c"
)

# 主程序组件（main component）
idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "main"
    REQUIRES
        nvs_flash
        esp_wifi
        esp_http_client
        esp_sntp
        driver
        i2c_management
        DHT11
        LIGHT
        OLED
        PIR
        YUYIN
        zhixinger
        zhixingyi
)

# 外部组件路径
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/components")

project(${PROJECT_NAME})
