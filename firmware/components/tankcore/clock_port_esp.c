#include "clock_port_esp.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

void clock_port_seed(int64_t not_before) {
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month[4] = {0};
    int day, year, hour, minute, second;
    sscanf(__DATE__, "%3s %d %d", month, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);
    struct tm t = {0};
    const char *m = strstr(months, month);
    t.tm_mon = m ? (int)((m - months) / 3) : 0;
    t.tm_mday = day;
    t.tm_year = year - 1900;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    time_t at = mktime(&t);
    if (not_before > (int64_t)at) at = (time_t)not_before;
    struct timeval tv = { .tv_sec = at };
    if (settimeofday(&tv, NULL) != 0)
        ESP_LOGE("clock", "could not seed system clock");
    else
        ESP_LOGI("clock", "system clock seeded from %s", at == not_before ? "saved tank" : "build time");
}
