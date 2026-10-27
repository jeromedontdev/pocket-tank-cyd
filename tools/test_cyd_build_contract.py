"""Guard the CYD's update safety and selective flash-size configuration."""
import csv
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parent.parent
FIRMWARE = ROOT / "firmware"


class CydBuildContractTests(unittest.TestCase):
    def test_client_tls_keeps_trust_and_rollback(self):
        for name in ("sdkconfig.defaults", "sdkconfig.cyd"):
            with self.subTest(config=name):
                text = (FIRMWARE / name).read_text()
                for setting in ("MBEDTLS_TLS_CLIENT_ONLY", "MBEDTLS_CERTIFICATE_BUNDLE",
                                "MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN",
                                "BOOTLOADER_APP_ROLLBACK_ENABLE"):
                    self.assertIn("CONFIG_" + setting + "=y", text)
                self.assertNotIn("CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT=y", text)
                self.assertIn('CONFIG_POCKET_TANK_OTA_URL="https://github.com/jeromedontdev/'
                              'pocket-tank-cyd/releases/latest/download/ota.json"', text)
        for name in ("net_port_esp.c", "weather_port_esp.c"):
            self.assertIn(".crt_bundle_attach = esp_crt_bundle_attach",
                          (FIRMWARE / "main" / name).read_text())

    def test_two_application_slots_and_save_storage(self):
        lines = (FIRMWARE / "partitions.cyd.csv").read_text().splitlines()
        partitions = {}
        for row in csv.reader(line for line in lines if line.strip() and not line.startswith("#")):
            name, kind, subtype, offset, size, *_ = [field.strip() for field in row]
            partitions[name] = (kind, subtype, int(offset, 0), int(size, 0))
        self.assertEqual(partitions["ota_0"], ("app", "ota_0", 0x20000, 0x1e0000))
        self.assertEqual(partitions["ota_1"], ("app", "ota_1", 0x200000, 0x1e0000))
        self.assertEqual(partitions["storage"], ("data", "spiffs", 0x3e0000, 0x20000))
        self.assertEqual(partitions["nvs"][0:2], ("data", "nvs"))
        ordered = sorted((v[2], v[2] + v[3]) for v in partitions.values())
        self.assertLessEqual(ordered[-1][1], 0x400000)
        for previous, current in zip(ordered, ordered[1:]):
            self.assertLessEqual(previous[1], current[0])

    def test_weather_response_memory_follows_verified_tls_handshake(self):
        source = (FIRMWARE / "main" / "weather_port_esp.c").read_text()
        fetch = source[source.index("static bool fetch("):source.index("static bool query_url(")]
        self.assertLess(fetch.index("esp_http_client_open(client, 0)"),
                        fetch.index("heap_caps_malloc(WEATHER_BODY_MAX + 2"))
        self.assertLess(fetch.index("esp_http_client_fetch_headers(client)"),
                        fetch.index("heap_caps_malloc(WEATHER_BODY_MAX + 2"))
        self.assertIn(".crt_bundle_attach = esp_crt_bundle_attach", fetch)
        self.assertIn("declared <= WEATHER_BODY_MAX", fetch)
        self.assertIn("esp_http_client_cleanup(client)", fetch)
        self.assertIn(".buffer_size = 1024, .buffer_size_tx = 512", fetch)

    def test_update_mode_defers_tank_only_memory(self):
        main = (FIRMWARE / "main" / "main.c").read_text()
        app = main[main.index("void app_main(void)"):]
        self.assertIn("if (!network_mode && !reserve_tank_scratch()) return;", app)
        self.assertLess(app.index("update_mode_pending()"), app.index("reserve_tank_scratch()"))
        self.assertLess(app.index("update_mode_run(fb[0])"),
                        app.index("if (network_mode && !reserve_tank_scratch()) return;"))
        net = (FIRMWARE / "main" / "net_port_esp.c").read_text()
        check = net[net.index("static void do_check("):net.index("static bool verify_download(")]
        self.assertLess(check.index("esp_http_client_open(c, 0)"),
                        check.index("heap_caps_malloc(4096"))

    def test_size_flags_exclude_frame_loop_sources(self):
        core = (FIRMWARE / "components" / "tankcore" / "CMakeLists.txt").read_text()
        size_flags = re.search(r"set_property\(SOURCE(.*?)APPEND PROPERTY COMPILE_OPTIONS \"-Os\"\)",
                               core, re.S)
        self.assertIsNotNone(size_flags)
        for source in ("tank.c", "render.c", "progression.c", "companion.c"):
            self.assertNotIn(f'/{source}"', size_flags[1])
        self.assertIn('/games.c"', size_flags[1])
        self.assertIn("PRIVATE -O2 -ffast-math", core)
        renderer = (ROOT / "common" / "render.c").read_text()
        menus = re.findall(r"#pragma GCC push_options(.*?)#pragma GCC pop_options", renderer, re.S)
        self.assertEqual(len(menus), 3)
        for section in menus:
            self.assertIn('#pragma GCC optimize ("Os")', section)
            for hot in ("render_tank_scene(", "draw_fish_core(", "clock_overlay_live("):
                self.assertNotIn(hot, section)
        for menu in ("render_stats_card(", "render_milestones(", "render_shop(", "render_settings(", "render_life("):
            self.assertTrue(any(menu in section for section in menus), menu)
        main = (FIRMWARE / "main" / "CMakeLists.txt").read_text()
        size_flags = re.search(r"set_property\(SOURCE(.*?)APPEND PROPERTY COMPILE_OPTIONS \"-Os\"\)",
                               main, re.S)
        self.assertIsNotNone(size_flags)
        for source in ("main.c", "display_port_cyd.c", "touch_port_xpt2046.c",
                       "brightness.c", "ambient.c"):
            self.assertNotIn(source, size_flags[1].split())

    def test_backup_completion_and_network_memory_exclusion(self):
        main = (FIRMWARE / "main" / "main.c").read_text()
        loop = main[main.index("static void tank_task(void *arg) {"):main.index("static void request_update(void) {")]
        self.assertIn("sd_port_auto_backup_service(&tank, dt, !weather_network_needed_after(dt));", loop)
        self.assertNotIn("sd_port_auto_backup_tick(", loop)
        self.assertIn("render_work_caches(!weather_lease && !backup_busy);", loop)
        self.assertIn("if (!backup_busy) weather_tick(dt);", loop)
        self.assertIn("if (s_benchmark_requested && !sd_port_auto_backup_busy())", loop)
        for signature in ("static void request_update(void)", "void device_wifi_setup(void)",
                          "void device_provision_request(void)"):
            start = main.index(signature + " {")
            self.assertTrue(main[start:].splitlines()[1].strip().startswith(
                "if (backup_network_blocked()) return;"), signature)

    def test_pipeline_preserves_single_framebuffer_and_update_startup(self):
        memory = (FIRMWARE / "main" / "memory_plan.h").read_text()
        self.assertIn("#define PLAN_FB_BYTES (240 * 180 * 2)", memory)
        self.assertIn("#define PLAN_FB_COUNT 1", memory)
        self.assertIn("#define PLAN_PIPELINE_ROWS 40", memory)
        self.assertIn("#define PLAN_PIPELINE_BUFFERS 2", memory)
        main = (FIRMWARE / "main" / "main.c").read_text()
        self.assertIn("display_port_pipeline_init()", main[main.index("static void tank_task(void *arg) {"):])
        self.assertNotIn("display_port_pipeline_init()", main[main.index("void app_main(void)"):])
        display = (FIRMWARE / "main" / "display_port_cyd.c").read_text()
        init = display[display.index("bool display_port_init(void)"):display.index("bool display_port_pipeline_init(void)")]
        self.assertNotIn("xTaskCreate", init)
        self.assertNotIn("heap_caps_malloc", display[display.index("bool display_port_pipeline_init(void)"):])


if __name__ == "__main__":
    unittest.main()
