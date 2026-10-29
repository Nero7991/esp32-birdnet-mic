# Sketch sources are compiled in place. The .ino becomes sketch_gen.cpp in the build
# directory (prototypes added by tools/ino_to_cpp.py, as the Arduino builder does).
set(SKETCH_DIR ${CMAKE_CURRENT_LIST_DIR}/../../esp32-birdnet-mic)
set(LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/../libs)
file(GLOB SKETCH_SRCS ${SKETCH_DIR}/*.cpp)
set(INO ${SKETCH_DIR}/esp32-birdnet-mic.ino)
set(INO_TOOL ${CMAKE_CURRENT_LIST_DIR}/../tools/ino_to_cpp.py)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # Generate once at configure time so the file exists for idf_component_register;
    # the custom command regenerates it whenever the .ino or the tool changes.
    execute_process(COMMAND python3 ${INO_TOOL} ${INO} ${CMAKE_CURRENT_BINARY_DIR}/sketch_gen.cpp)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sketch_gen.cpp
        COMMAND python3 ${INO_TOOL} ${INO} ${CMAKE_CURRENT_BINARY_DIR}/sketch_gen.cpp
        DEPENDS ${INO} ${INO_TOOL})
endif()

idf_component_register(
    SRCS ${CMAKE_CURRENT_BINARY_DIR}/sketch_gen.cpp ${SKETCH_SRCS}
         ${LIB_DIR}/WiFiManager/WiFiManager.cpp
         ${LIB_DIR}/PubSubClient/src/PubSubClient.cpp
    INCLUDE_DIRS variant . ${SKETCH_DIR} ${LIB_DIR}/WiFiManager ${LIB_DIR}/PubSubClient/src
)

# Same flags as esp32-birdnet-mic/build_opt.h, plus the board and fork defines.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables
    -DWM_NODEBUG
    -DBIRDNET_TS_FORK=1
    -DARDUINO_XIAO_ESP32S3
    -DBOARD_HAS_PSRAM
    -Wno-error=unused-variable -Wno-error=unused-function -Wno-error=deprecated-declarations
    -Wno-error=format
)
