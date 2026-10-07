#include "keyboard.h"
#include "io.h"
#include "isr.h"

#define KEYBOARD_PORT 0x60
#define BUFFER_SIZE 256

static const char scancode[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};
static const char shift_table[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
};

static signed char key_buffer[BUFFER_SIZE];
static volatile int buf_head = 0;
static volatile int buf_tail = 0;
static int lshift, rshift;
static int e0_pending;

/* Cursor and paging keys carry no ASCII code, so the tables above map them to
 * 0 and the handler used to drop them on the floor. Only the extended,
 * E0-prefixed codes are mapped here: the bare codes are ambiguous (0x47 is
 * also keypad 7), and the E0 prefix is what identifies them. */
static int special_key(unsigned char sc) {
    switch (sc) {
    case 0x47: return KEY_HOME;
    case 0x48: return KEY_UP;
    case 0x49: return KEY_PGUP;
    case 0x4F: return KEY_END;
    case 0x50: return KEY_DOWN;
    case 0x51: return KEY_PGDN;
    default: return 0;
    }
}

static void keyboard_push(int key) {
    int next = (buf_tail + 1) % BUFFER_SIZE;
    if (next != buf_head) {
        key_buffer[buf_tail] = key;
        buf_tail = next;
    }
}

void keyboard_handler(registers_t *r) {
    unsigned char sc = inb(KEYBOARD_PORT);

    /* Arrows and paging keys arrive as E0 <make>, then E0 <release> with bit
     * 7 set on the repeated code. Both halves carry bit 7 in the release and
     * the prefix itself is 0xE0, so without tracking the prefix every one of
     * these keys was discarded before it could reach the shell. */
    if (sc == 0xE0) { e0_pending = 1; return; }
    if (e0_pending) {
        e0_pending = 0;
        if (sc & 0x80) return;                 /* release */
        int key = special_key(sc);
        if (key) keyboard_push(key);
        return;
    }

    if (sc == 0x2A) { lshift = 1; return; }
    if (sc == 0xAA) { lshift = 0; return; }
    if (sc == 0x36) { rshift = 1; return; }
    if (sc == 0xB6) { rshift = 0; return; }
    if (sc & 0x80) return;
    if (sc > 127) return;

    char c = (lshift || rshift) ? shift_table[sc] : scancode[sc];
    if (!c) return;

    keyboard_push(c);
}

void keyboard_init() {
    // Register IRQ1 handler (keyboard is IRQ1 -> interrupt 33)
    register_interrupt_handler(33, keyboard_handler);
}

char keyboard_getchar() {
    while (buf_head == buf_tail);
    char c = key_buffer[buf_head];
    buf_head = (buf_head + 1) % BUFFER_SIZE;
    return c;
}
