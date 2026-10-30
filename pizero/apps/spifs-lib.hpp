/** @file spifs-lib.hpp — the spifs library's API, through whichever loader loaded it. */
#pragma once
#include "pizero.hpp"
#include <cstdint>

struct Spifs {
    Lib lib;
    void* init_ = nullptr; void* format_ = nullptr; void* create_ = nullptr; void* del_ = nullptr;
    void* count_ = nullptr; void* list_ = nullptr; void* read_ = nullptr;

    bool load(unsigned slot) {
        if (!lib_load(lib, slot, "spifs")) return false;
        init_ = lib_sym(lib, "spifs_init"); format_ = lib_sym(lib, "spifs_format");
        create_ = lib_sym(lib, "spifs_create"); del_ = lib_sym(lib, "spifs_delete");
        count_ = lib_sym(lib, "spifs_count"); list_ = lib_sym(lib, "spifs_list");
        read_ = lib_sym(lib, "spifs_read");
        return init_ && format_ && create_ && del_ && count_ && list_ && read_;
    }
    int init() { return lib_call(lib, init_); }
    int format() { return lib_call(lib, format_); }
    int create(const char* n, const void* d, unsigned l) {
        return lib_call(lib, create_, reinterpret_cast<std::uintptr_t>(n), reinterpret_cast<std::uintptr_t>(d), l);
    }
    int del(const char* n) { return lib_call(lib, del_, reinterpret_cast<std::uintptr_t>(n)); }
    unsigned count() { return static_cast<unsigned>(lib_call(lib, count_)); }
    int list(unsigned i, char* name, unsigned* size) {
        return lib_call(lib, list_, i, reinterpret_cast<std::uintptr_t>(name), reinterpret_cast<std::uintptr_t>(size));
    }
    int read(const char* n, void* b, unsigned cap, unsigned* got) {
        return lib_call(lib, read_, reinterpret_cast<std::uintptr_t>(n), reinterpret_cast<std::uintptr_t>(b), cap,
                        reinterpret_cast<std::uintptr_t>(got));
    }
    void ls() {
        for (unsigned i = 0;; ++i) {
            char name[17];
            unsigned size = 0;
            if (list(i, name, &size) != 0) break;
            say("    "); say(name); sayf(" (%uB)\n", size);
        }
    }
    /// Print a file, and check it holds what is expected.
    void cat(const char* n, const char* want) {
        char buf[128];
        unsigned got = 0;
        const bool ok = read(n, buf, sizeof buf - 1, &got) == 0;
        expect(ok, "file reads back");
        if (!ok) return;
        buf[got < sizeof buf ? got : sizeof buf - 1] = 0;
        say("    "); say(n); say(": "); say(buf);
        expect(__builtin_strcmp(buf, want) == 0, "file holds what was written");
    }
};
