/* Knight of Knights port: entry point. Boot mirrors the original WinMain (FUN_00402b50). */
#include "game/gen/vmc.h"
#include <stdio.h>

extern int game_boot(void);
extern int game_frame(void);

int main(int argc, char **argv)
{
    if (!plat_init(argc, argv)) return 1;
    if (!vm_load_image(plat_data_path("kok.exe"))) {
        plat_log("cannot read kok.exe from the data directory (run tools/make_data.sh)");
        return 1;
    }
    rm_init();
    dx_init();
    if (!game_boot()) return 1;
    while (plat_pump() && game_frame()) { }
    plat_shutdown();
    return 0;
}
