/**
 * @file spifs.cpp — the WeAct spifs example on the Pi-Zero: load the flat filesystem and its
 * driver, then init, format, create, list, read and delete by name. Writing stops XIP, so
 * that happens before core 1 starts; both cores then list and read.
 */
#include "spifs-lib.hpp"

const char* const kAppName = "spifs-" LOADER_NAME;

static Spifs g_fs;

bool app_load() { return g_fs.load(0) && libs_ready(); }

bool app_start() {
    if (g_fs.init() != 0) { say("  flash NOT found\n"); return false; }
    say("  flash detected\n");
    if (g_fs.format() != 0) { say("  format FAILED\n"); return false; }
    say("  flatfs formatted\n");
    g_fs.create("hello.txt", "Hello Pi-Zero!\n", 15);
    g_fs.create("nums.txt", "1 2 3 4 5\n", 10);
    g_fs.create("scl.txt", "loaded by name\n", 15);
    sayf("  created 3 files; count=%u\n", g_fs.count());
    expect(g_fs.count() == 3u, "3 files created");
    say("  -- ls --\n");
    g_fs.ls();
    say("  -- cat --\n");
    g_fs.cat("hello.txt", "Hello Pi-Zero!\n");
    g_fs.del("hello.txt");
    sayf("  deleted hello.txt; count=%u\n", g_fs.count());
    expect(g_fs.count() == 2u, "the file is gone");
    return true;
}

void app_on_core(unsigned) {
    say("  -- ls --\n");
    g_fs.ls();
    say("  -- cat --\n");
    g_fs.cat("nums.txt", "1 2 3 4 5\n");
    g_fs.cat("scl.txt", "loaded by name\n");
    expect(g_fs.count() == 2u, "both files listed");
}
