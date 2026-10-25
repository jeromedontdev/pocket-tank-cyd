/* Independent Pocket Tank CYD release version. Saves record PT_RELEASE_NUM;
 * version_port_string adds the build identity for diagnostics. */
#ifndef VERSION_H
#define VERSION_H

#define PT_RELEASE_MAJOR 2
#define PT_RELEASE_MINOR 3
#define PT_RELEASE_PATCH 6
#define PT_RELEASE       "2.3.6"
#define PT_RELEASE_STAGE "CYD"
#define PT_RELEASE_NUM   ((PT_RELEASE_MAJOR << 16) | (PT_RELEASE_MINOR << 8) | PT_RELEASE_PATCH)

#if PT_BOARD_UNIFIED
#define PT_BOARD         "cydesp32profiles"
#define PT_BOARD_NAME    "ESP32 CYD hardware profiles"
#elif PT_BOARD_ELEGOO
#define PT_BOARD         "elegoo28"
#define PT_BOARD_NAME    "ELEGOO ESP32-32E 2.8-inch"
#else
#define PT_BOARD         "cyd2432s028"
#define PT_BOARD_NAME    "ESP32-2432S028 CYD"
#endif
#define PT_BOARD_MAGIC   "PTBOARD"             /* the image's board marker begins with these 8 bytes (with the NUL) */
/* ... at this offset in the app image: the image header (24) + the
                                                  first segment's header (8) + esp_app_desc_t (256) */
#define PT_BOARD_MARKER_OFFSET 0x120

#endif
