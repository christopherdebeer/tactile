#include "afterword.h"
#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum page { HOME, WORLD, MOTION, PACE, NOTE, EDIT, LETTER, BORROW, POINTER, DEVICE };
enum phrase { READ_WORLD, READ_MOTION, READ_PACE, READ_NOTE, READ_POINTER, READ_DEVICE, PHRASES };
static const char *const groups[] = {"abcde", "fghij", "klmno", "pqrst", "uvwxyz"};
static const char *const words[] = {"quiet","turning","motion","reading","pace","pointer"};
static const char *const paces[] = {"slow","steady","quick"};
static const int periods[] = {3200,2200,1300};
static const char *const edit_actions[] = {
    "abcde","fghij","klmno","pqrst","uvwxyz","borrow word","space",
    "erase","cursor left","cursor right","save","discard"
};
#define COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))
static enum page page;
static int phrase, choice, pace, group;
static char saved[96], draft[96];
static size_t cursor, read_at;
static bool held, live, moving, online, connected;
static float angle;
static uint32_t last, next, repeat_at, pressed, still_since;
static const char *notice;
static float gravity[3];

void aw_gravity(const float a[3]) { memcpy(gravity, a, sizeof(gravity)); }
void aw_world_load(const char *state) { world_load(state); }

static int wrap(int i, int n) { return (i % n + n) % n; }

void aw_init(int initial, const char *note, uint32_t now) {
    page = HOME;
    phrase = choice = 0;
    held = false;
    notice = "";
    pace = initial >= 0 && initial < 3 ? initial : 1;
    snprintf(saved, sizeof(saved), "%s", note);
    last = now;
    next = now + periods[pace];
}

static const char *home(int i) {
    switch (i) {
    case READ_WORLD: return "step inside";
    case READ_MOTION: return !live ? "motion unavailable" : moving ? "motion is turning" : "motion is quiet";
    case READ_PACE: return "my reading pace";
    case READ_NOTE: return saved[0] ? "you left a note" : "leave a note";
    case READ_POINTER: return "use the pointer";
    default: return "about this pebble";
    }
}

static void enter(enum page p, int position) {
    page = p;
    choice = position;
    notice = "";
    next = last + periods[pace];
}

static void insert(const char *s) {
    size_t n = strlen(s), len = strlen(draft);
    if (len + n >= sizeof(draft)) { notice = "note full"; return; }
    memmove(draft + cursor + n, draft + cursor, len - cursor + 1);
    memcpy(draft + cursor, s, n);
    cursor += n;
}

static int choices(void) {
    switch (page) {
    case HOME: return PHRASES;
    case PACE: return 4;
    case NOTE: return 2;
    case EDIT: return COUNT(edit_actions);
    case LETTER: return (int)strlen(groups[group]) + 1;
    case BORROW: return COUNT(words) + 1;
    default: return 1;
    }
}

static void select_choice(void) {
    switch (page) {
    case HOME: {
        static const enum page pages[] = {WORLD,MOTION,PACE,NOTE,POINTER,DEVICE};
        read_at = 0;
        enter(pages[phrase], phrase == READ_PACE ? pace : 0);
        if (page == WORLD) world_enter(last, saved, periods[pace] / 5);
        break;
    }
    case PACE:
        if (choice < 3) {
            char value[] = {(char)('0' + choice), 0};
            if (!aw_store("aw_pace", value)) { notice = "save failed"; break; }
            pace = choice;
        }
        enter(HOME, 0);
        break;
    case NOTE:
        if (choice == 0) {
            strcpy(draft, saved);
            cursor = strlen(draft);
            enter(EDIT, 0);
        } else enter(HOME, 0);
        break;
    case EDIT:
        if (choice < COUNT(groups)) { group = choice; enter(LETTER, 0); }
        else if (choice == 5) enter(BORROW, 0);
        else if (choice == 6) insert(" ");
        else if (choice == 7 && cursor) {
            memmove(draft + cursor - 1, draft + cursor, strlen(draft + cursor) + 1);
            --cursor;
        } else if (choice == 8 && cursor) --cursor;
        else if (choice == 9 && draft[cursor]) ++cursor;
        else if (choice == 10 || choice == 11) {
            if (choice == 10) {
                if (!aw_store("aw_note", draft)) { notice = "save failed"; break; }
                strcpy(saved, draft);
            }
            read_at = 0;
            enter(NOTE, 0);
        }
        break;
    case LETTER:
    case BORROW: {
        int index = choice;
        bool borrow = page == BORROW;
        enter(EDIT, borrow ? 5 : group);
        if (borrow && index < COUNT(words)) {
            char token[20];
            snprintf(token,sizeof(token),"%s%s",cursor && draft[cursor-1]!=' ' ? " " : "", words[index]);
            insert(token);
        } else if (!borrow && index < (int)strlen(groups[group])) {
            char letter[] = {groups[group][index], 0};
            insert(letter);
        }
        break;
    }
    case POINTER: break;
    default: enter(HOME, 0); break;
    }
}

bool aw_pointer(void) { return page == POINTER; }

static size_t note_chunk(void) {
    size_t n = strcspn(saved + read_at, " ");
    return n > 18 ? 18 : n;
}

void aw_tick(uint32_t now, bool down, float twist, float speed, bool valid, bool wifi, bool paired) {
    unsigned elapsed = now - last;
    last = now;
    live = valid; moving = valid && speed > 0.35f;
    online = wifi; connected = paired;
    if (page == WORLD) {
        if (world_tick(now, down, twist, speed, gravity, valid)) enter(HOME, 0);
        held = down;
        return;
    }
    if (down && !held) {
        angle = 0;
        pressed = repeat_at = still_since = now;
        notice = "";
    }
    if (down && held && valid && elapsed < 100) {
        angle += twist * elapsed * 0.001f;
        if (fabsf(angle) >= 0.20f && (int32_t)(now - repeat_at) >= 0) {
            int delta = angle > 0 ? 1 : -1;
            if (page == HOME) phrase = wrap(phrase + delta, PHRASES);
            else choice = wrap(choice + delta, choices());
            repeat_at = now + 400;
        }
    }
    if (down && moving) still_since = now;
    if (!down && held) {
        if (page == POINTER && now - still_since >= 2200) enter(HOME, 0);
        else if (now - pressed >= 40) select_choice();
        next = now + periods[pace];
    } else if (!down && (int32_t)(now - next) >= 0) {
        next = now + periods[pace];
        if (page == HOME) phrase = wrap(phrase + 1, PHRASES);
        else if (page == NOTE && saved[0]) {
            read_at += note_chunk();
            while (saved[read_at] == ' ') ++read_at;
            if (!saved[read_at]) read_at = 0;
        }
    }
    held = down;
}

#define PUT(field, ...) snprintf(f->field, sizeof(f->field), __VA_ARGS__)
void aw_frame(aw_frame_t *f) {
    memset(f, 0, sizeof(*f));
    PUT(section,"AFTERWORD");
    PUT(hint,"%s",held ? "release to choose" : "tap / hold + twist");
    switch (page) {
    case WORLD: world_frame(f); return;
    case HOME:
        PUT(before,"%s",home(wrap(phrase-1,PHRASES)));
        PUT(word,"%s",home(phrase));
        PUT(after,"%s",home(wrap(phrase+1,PHRASES))); break;
    case MOTION:
        PUT(before,"motion feels"); PUT(word,"%s",!live ? "unavailable" : moving ? "turning" : "quiet");
        PUT(hint,"tap to return"); break;
    case PACE:
        PUT(section,"READING PACE"); PUT(word,"%s",choice < 3 ? paces[choice] : "return");
        if (choice < 3) { PUT(after,"%s",(last % periods[choice]) < (unsigned)periods[choice]/2 ? "breathe in" : "breathe out"); }
        break;
    case NOTE:
        PUT(section,"YOUR NOTE"); PUT(before,"%.*s",(int)note_chunk(),saved+read_at);
        PUT(word,"%s",held ? (choice ? "return" : "edit note") : saved[0] ? f->before : "empty page");
        if (!held) { f->before[0]=0; PUT(after,"edit / return"); }
        break;
    case EDIT: case LETTER: case BORROW: {
        PUT(section,"WRITE");
        size_t start = cursor > 16 ? cursor-16 : 0;
        PUT(before,"%.*s|",(int)(cursor-start),draft+start);
        PUT(after,"%.18s",draft+cursor);
        if (page == EDIT) { PUT(word,"%s",edit_actions[choice]); }
        else if (page == BORROW) { PUT(word,"%s",choice < COUNT(words) ? words[choice] : "back"); }
        else { char c = groups[group][choice]; if (c) { PUT(word,"%c",c); } else { PUT(word,"back"); } }
        break;
    }
    case DEVICE:
        PUT(before,"Wi-Fi %s",online ? "online" : "offline");
        PUT(word,"v%s",TACTILE_VERSION);
        PUT(after,"BLE %s",connected ? "paired" : "unlinked"); PUT(hint,"tap to return"); break;
    case POINTER:
        PUT(word,"pointer"); PUT(before,"%s",connected ? "connected" : "pair via Bluetooth");
        PUT(after,"tap click / hold aim"); PUT(hint,"hold still 2.2s: exit"); break;
    }
    if (*notice) { PUT(hint,"%s",notice); }
}
