/* Host regression for the same reader state machine used on the Atom. */
#include "afterword.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t clock_ms;
static char stored_note[96], stored_pace[4], stored_world[48];
static bool write_ok = true;

bool aw_store(const char *key, const char *value) {
    if (!write_ok) return false;
    if (!strcmp(key, "aw_note")) snprintf(stored_note, sizeof(stored_note), "%s", value);
    else if (!strcmp(key, "aw_world")) snprintf(stored_world, sizeof(stored_world), "%s", value);
    else snprintf(stored_pace, sizeof(stored_pace), "%s", value);
    return true;
}

static void tick(bool down, float gyro, int ms) {
    clock_ms += ms;
    aw_tick(clock_ms, down, gyro, gyro, true, true, false);
}

static aw_frame_t frame(void) { aw_frame_t f; aw_frame(&f); return f; }

/* One detent per 400 ms while held away from the catch angle. */
static void choose(int steps) {
    tick(true, 0, 20);
    if (steps) {
        tick(true, steps > 0 ? 6 : -6, 40);
        int n = steps > 0 ? steps : -steps;
        for (int i = 1; i < n; i++)
            for (int t = 0; t < 20; t++) tick(true, 0, 20);
    }
    tick(false, 0, 60);
}

int main(void) {
    aw_init(1, "", 0);
    choose(1);               /* past "step inside" to motion */
    assert(!strcmp(frame().word, "quiet"));
    tick(false, 2, 20);
    assert(!strcmp(frame().word, "turning"));
    choose(0); choose(1); choose(1);
    assert(!strcmp(stored_pace,"2"));
    choose(1); choose(0);    /* note -> edit */
    choose(5); choose(1);    /* borrow turning */
    choose(1);               /* space */
    choose(-2); choose(0);   /* u */
    choose(4);               /* cursor left */
    choose(-4); choose(1);   /* v before u */
    choose(-6);              /* save via wrap */
    assert(!strcmp(stored_note,"turning vu"));
    aw_init(2, stored_note, clock_ms);
    choose(3);
    assert(!strcmp(frame().word,"turning"));
    tick(false,0,1301);
    assert(!strcmp(frame().word,"vu"));
    choose(1); choose(1);
    assert(aw_pointer());
    tick(true,0,20); tick(true,1,2300); tick(false,0,20);
    assert(aw_pointer());
    tick(true,0,20); tick(false,0,2300);
    assert(!aw_pointer());
    aw_init(1,"",clock_ms);
    choose(2);
    write_ok = false;
    choose(1);
    assert(!strcmp(frame().hint,"save failed"));
    puts("Afterword handling, editing, persistence and pointer gates passed");
}
