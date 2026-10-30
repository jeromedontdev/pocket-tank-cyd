#undef NDEBUG
#include "progression.h"
#include "aquarium_store.h"
#include "setup.h"
#include "companion.h"
#include "weather.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool weather_location_south(bool *south) { (void)south; return false; }

const setup_place_snapshot_t *setup_place_original(void) { return NULL; }

int main(void) {
    const char *path = getenv("POCKET_TANK_SAVE");
    assert(path && *path);
    tank_t saved, loaded;
    tank_init(&saved, 9123);
    progression_boot(&saved);
    if (!progression_mode_chosen(&saved)) assert(!progression_mode_select(&saved, GAME_MODE_NORMAL));
    while (saved.n_fish < 4) {
        assert(tank_add_fish(&saved, 0, 1) >= 0);
        companion_polish_init(&saved, false);
    }
    tank_set_name(&saved, 0, "bolt");
    tank_set_name(&saved, 1, "pip");
    tank_set_name(&saved, 2, "kelp");
    tank_set_name(&saved, 3, "odie");
    saved.sd_balance = 123456;
    saved.journal.xp = journal_level_xp(10);
    assert(progression_setup_done(&saved));
    saved.journal.theme = 4;
    assert(progression_save(&saved));

    unsigned char current[AQUARIUM_SAVE_BYTES];
    size_t current_size = 0;
    assert(persist_port_load(current, sizeof current, &current_size));
    assert(current_size == AQUARIUM_SAVE_BYTES && AQUARIUM_SAVE_BYTES == 6476);

    char directory[1024];
    int n = snprintf(directory, sizeof directory, "%s.profiles", path);
    assert(n > 0 && (size_t)n < sizeof directory);
    const uint32_t profile_id[2] = {0x50524631u, 0x41435456u};
    char snapshot[1200];
    n = snprintf(snapshot, sizeof snapshot, "%s/%08lx%08lx-0.a", directory,
                 (unsigned long)profile_id[0], (unsigned long)profile_id[1]);
    assert(n > 0 && (size_t)n < sizeof snapshot);
    assert(remove(snapshot) == 0);
    n = snprintf(snapshot, sizeof snapshot, "%s/%08lx%08lx-0.b", directory,
                 (unsigned long)profile_id[0], (unsigned long)profile_id[1]);
    assert(n > 0 && (size_t)n < sizeof snapshot);
    assert(remove(snapshot) == 0);
    assert(aquarium_store_write(directory, profile_id, 0, current, 6464));

    tank_init(&loaded, 1);
    (void)progression_wake(&loaded, 0);
    assert(progression_storage_ready());
    assert(loaded.n_fish == 4 && loaded.sd_balance == 123456);
    assert(!strcmp(loaded.fish[0].name, "bolt"));
    assert(!strcmp(loaded.fish[1].name, "pip"));
    assert(!strcmp(loaded.fish[2].name, "kelp"));
    assert(!strcmp(loaded.fish[3].name, "odie"));
    assert(loaded.progression.version == 1);
    assert(loaded.foliage.version == 1 && loaded.foliage.owned == 1 && loaded.foliage.planted == 1);

    assert(progression_save(&loaded));
    memset(current, 0, sizeof current);
    assert(persist_port_load(current, sizeof current, &current_size));
    assert(current_size == AQUARIUM_SAVE_BYTES);
    tank_init(&saved, 2);
    (void)progression_wake(&saved, 0);
    assert(progression_storage_ready() && saved.n_fish == 4 && saved.sd_balance == 123456);
    assert(!strcmp(saved.fish[0].name, "bolt") && !strcmp(saved.fish[3].name, "odie"));
    assert(aquarium_port_ready());
    n = snprintf(directory, sizeof directory, "%s.tanks", path);
    assert(n > 0 && (size_t)n < sizeof directory);
    const uint32_t tank_id[2] = {0x11223344, 0x55667788};
    assert(aquarium_store_write(directory, tank_id, 0, current, 6472));
    unsigned char legacy[AQUARIUM_SAVE_BYTES];
    memset(legacy, 0xa5, sizeof legacy);
    size_t stored_size = 0;
    assert(aquarium_port_read_sized(tank_id, 0, legacy, sizeof legacy, &stored_size));
    assert(stored_size == 6472 && !memcmp(legacy, current, stored_size));
    for (size_t i = stored_size; i < sizeof legacy; i++) assert(!legacy[i]);
    memset(legacy, 0xa5, sizeof legacy);
    assert(aquarium_port_read(tank_id, 0, legacy, sizeof legacy));
    assert(!memcmp(legacy, current, 6472));
    for (size_t i = 6472; i < sizeof legacy; i++) assert(!legacy[i]);
    stored_size = 123;
    assert(!aquarium_port_read_sized(tank_id, 1, legacy, sizeof legacy, &stored_size) && !stored_size);
    puts("PASS: real archive-port v21/6464-byte profile migration preserves four named fish and wallet, then upgrades on save");
    puts("PASS: real simulator aquarium ports preserve 6472-byte legacy length, zero-fill tails, retain unsized reads and clear failed length");
    return 0;
}
