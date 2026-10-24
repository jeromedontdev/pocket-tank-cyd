set(COMMON ${CMAKE_CURRENT_LIST_DIR}/../../../common)
set(CYD_ASSETS "${CMAKE_BINARY_DIR}/cyd_assets")
idf_component_register(
    SRCS "${COMMON}/tank.c" "${COMMON}/advisor.c" "${COMMON}/render.c" "${COMMON}/games.c"
         "${COMMON}/icons.c" "${COMMON}/progression.c"
         "${COMMON}/setup.c" "${COMMON}/notice.c" "${COMMON}/update.c" "${COMMON}/aquarium_store.c"
         "${COMMON}/companion.c" "${COMMON}/companion_ui.c" "${COMMON}/weather.c"
         "persist_port_esp.c" "clock_port_esp.c"
    INCLUDE_DIRS "${COMMON}" "."
    PRIV_REQUIRES nvs_flash cjson spiffs spi_flash
)
add_custom_command(
    OUTPUT "${CYD_ASSETS}/icons_sd.c" "${CYD_ASSETS}/icons_sd_bank.h" "${CYD_ASSETS}/icons.bin"
    COMMAND ${PYTHON} "${COMMON}/../tools/pack_cyd_icons.py" "${COMMON}/icons.c" "${CYD_ASSETS}"
    DEPENDS "${COMMON}/icons.c" "${COMMON}/../tools/pack_cyd_icons.py"
    VERBATIM)
add_custom_target(cyd_icon_assets DEPENDS
    "${CYD_ASSETS}/icons_sd.c" "${CYD_ASSETS}/icons_sd_bank.h" "${CYD_ASSETS}/icons.bin")
add_dependencies(${COMPONENT_LIB} cyd_icon_assets)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffast-math)
target_compile_definitions(${COMPONENT_LIB} PUBLIC POP_CAP=5 CYD_FLAT_EDGES=1)
set_source_files_properties("${COMMON}/progression.c" "${COMMON}/weather.c" "${COMMON}/companion.c"
    PROPERTIES COMPILE_OPTIONS "-fno-fast-math")
