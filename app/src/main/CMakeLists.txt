cmake_minimum_required(VERSION 3.22.1)
project("cubik")

# Declaramos nuestra librería compartida con main.cpp y android_native_app_glue.c
add_library(
    cubik
    SHARED
    main.cpp
    android_native_app_glue.c
)

# Indicamos dónde encontrar los archivos de cabecera (.h)
target_include_directories(cubik PRIVATE
    .
)

# Encontramos las librerías nativas del sistema de Android
find_library(log-lib log)
find_library(vulkan-lib vulkan)
find_library(android-lib android)

# Vinculamos las librerías del sistema con nuestra biblioteca cubik
target_link_libraries(
    cubik
    ${log-lib}
    ${vulkan-lib}
    ${android-lib}
)

