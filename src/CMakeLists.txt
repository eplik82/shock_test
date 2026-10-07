FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c ${CMAKE_SOURCE_DIR}/src/*.cpp)
idf_component_register(SRCS ${app_sources}
    INCLUDE_DIRS "."
    EMBED_FILES "../fonts/DejaVuSans.ttf" "../fonts/DejaVuSans-Bold.ttf" "web/index.html.gz" "logo.rle")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers -Wno-unused-variable)
# paanikakäsitleja mähis (crashinfo.cpp)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_panic_handler" APPEND)
