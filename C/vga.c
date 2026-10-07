#include "vga.h"
#include "io.h"
#include "task.h"
#include "serial.h"

#define VIDEO 0xB8000
#define WIDTH 80
#define HEIGHT 25

/* Lines kept above the visible screen. Text that scrolls off the top used to
 * be destroyed, so long output (ls, cat, a crash dump) was unrecoverable.
 * 1000 rows is ~80 KiB of .bss, well inside the kernel's address space, and
 * keeps roughly forty screens of history. */
#define SCROLLBACK_LINES 1000

/* The live screen, kept separate from the VGA window. The hardware buffer is
 * only ever a rendering of this array: while the user reads scrollback the
 * window shows older lines instead, and a redraw puts the live screen back.
 * Keeping our own copy is what lets the view freeze without losing the text
 * the kernel is still writing to. */
static unsigned short screen[HEIGHT * WIDTH];

/* Ring of rows that have scrolled off the top, oldest first. Each entry is a
 * whole 80-cell row (char + attribute per cell) so a row can be pushed and
 * read back without unpacking it. */
static unsigned short scrollback[SCROLLBACK_LINES * WIDTH];
static int sb_count;                  /* rows held above the screen */
static int sb_head;                   /* ring slot holding the oldest row */
static int sb_view;                   /* rows scrolled back from the live view */

static int cursor_x;
static int cursor_y;
static int wrap_pending;              /* column 79 filled, wrap on next char */
static int text_color = 0x07;

#define CELL(ch) ((unsigned short)((ch) | (text_color << 8)))

void vga_set_color(int color) {
    preempt_disable();
    text_color = color;
    preempt_enable();
}

void vga_disable_cursor() {
    preempt_disable();
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
    preempt_enable();
}

/* Draw the current view into the hardware buffer: sb_view rows back from the
 * live screen, clamped so a full page is always shown. Row r of the view is
 * history row (sb_count - sb_view + r), where rows below sb_count come from
 * the live screen. Callers must already hold the preemption lock. */
static void vga_render(void) {
    volatile unsigned short *v = (volatile unsigned short *)VIDEO;

    for (int r = 0; r < HEIGHT; r++) {
        int line = sb_count - sb_view + r;
        for (int c = 0; c < WIDTH; c++) {
            unsigned short cell = (line < sb_count)
                ? scrollback[((sb_head + line) % SCROLLBACK_LINES) * WIDTH + c]
                : screen[(line - sb_count) * WIDTH + c];

            /* Reading history: repaint the background blue so the frozen view
             * cannot be mistaken for live output. A banner row was tried first
             * and rejected -- it covered the top line, which is precisely one
             * of the lines the user paged back to read. */
            if (sb_view > 0) {
                unsigned int attr = (cell >> 8) & 0xFF;
                attr = (attr & 0x8F) | 0x10;         /* keep fg, force bg blue */
                cell = (unsigned short)((cell & 0x00FF) | (attr << 8));
            }
            v[r * WIDTH + c] = cell;
        }
    }
}

void vga_clear() {
    preempt_disable();
    for (int i = 0; i < WIDTH * HEIGHT; i++) screen[i] = CELL(' ');
    cursor_x = 0;
    cursor_y = 0;
    wrap_pending = 0;
    /* Scrollback deliberately survives a clear: the point of the history is
     * that nothing already printed can be lost. It is a ring, so it only
     * gives up its oldest rows once it is full. */
    vga_render();
    preempt_enable();
}

/* Scroll the text up one line, retiring the top row into the history ring.
 * Callers must already hold the preemption lock. */
static void vga_scroll(void) {
    unsigned short *slot = &scrollback[((sb_head + sb_count) % SCROLLBACK_LINES) * WIDTH];
    for (int i = 0; i < WIDTH; i++) slot[i] = screen[i];

    if (sb_count < SCROLLBACK_LINES) {
        sb_count++;
    } else {
        /* Full: drop the oldest row to make room. */
        sb_head = (sb_head + 1) % SCROLLBACK_LINES;
    }

    for (int i = 0; i < (HEIGHT - 1) * WIDTH; i++) screen[i] = screen[i + WIDTH];
    for (int c = 0; c < WIDTH; c++) screen[(HEIGHT - 1) * WIDTH + c] = CELL(' ');

    /* The shift only touched the backing store, so the hardware buffer still
     * shows the pre-scroll page. Repaint it here; a scroll costs one 4 KiB
     * copy per line, which is nothing next to the text it just saved. */
    if (sb_view == 0) vga_render();
}

/* Advance to the next line, scrolling once the bottom row is used up.
 * Without this the cursor used to wrap back to (0,0) and overwrite the
 * top of the screen as soon as the shell produced more than 25 lines. */
static void vga_newline(void) {
    cursor_x = 0;
    wrap_pending = 0;
    if (cursor_y >= HEIGHT - 1) {
        vga_scroll();
        cursor_y = HEIGHT - 1;
    } else {
        cursor_y++;
    }
}

void vga_putchar(char c) {
    /* Mirror to COM1 so the session can be captured without a window.
     * serial_putc() is a no-op unless serial_init() found a UART. */
    serial_putc(c);

    preempt_disable();
    /* Both the backing store and the hardware window are addressed as VGA
     * cells (char in the low byte, attribute in the high byte), not as raw
     * bytes. While the view is parked in scrollback the hardware keeps showing
     * that frozen page, so new output updates `screen` only and repaints on
     * the next scroll instead of yanking the view away. */
    volatile unsigned short *v = (volatile unsigned short *)VIDEO;
    if (c == '\n') {
        vga_newline();
    } else if (c == '\r') {
        cursor_x = 0;
        wrap_pending = 0;
    } else if (c == '\b') {
        /* Parked at the last column after filling the line, so step back
         * before erasing rather than clamping. */
        if (wrap_pending) {
            wrap_pending = 0;
            cursor_x--;
        } else if (cursor_x > 0) {
            cursor_x--;
        }
        screen[cursor_y * WIDTH + cursor_x] = CELL(' ');
        if (sb_view == 0) v[cursor_y * WIDTH + cursor_x] = CELL(' ');
    } else {
        /* Deferred wrap: filling column 79 leaves the cursor there and only
         * the next character starts a new line. Advancing immediately made
         * every exactly-80-column line swallow the line after it. */
        if (wrap_pending) vga_newline();

        screen[cursor_y * WIDTH + cursor_x] = CELL(c);
        if (sb_view == 0) v[cursor_y * WIDTH + cursor_x] = CELL(c);

        cursor_x++;
        if (cursor_x >= WIDTH) {
            cursor_x = WIDTH - 1;
            wrap_pending = 1;
        }
    }
    preempt_enable();
}

void vga_print(const char *s) {
    while (*s) vga_putchar(*s++);
}

void vga_print_color(const char *s, int color) {
    preempt_disable();
    int old = text_color;
    text_color = color;
    vga_print(s);
    text_color = old;
    preempt_enable();
}

/* ---------- SCROLLBACK ---------- */

int vga_scrollback_view(void) {
    return sb_view;
}

static void vga_scroll_by(int rows) {
    int target = sb_view + rows;
    if (target > sb_count) target = sb_count;
    if (target < 0) target = 0;
    if (target == sb_view) return;
    sb_view = target;
    vga_render();
}

void vga_scroll_up(int rows) {          /* older lines */
    preempt_disable();
    vga_scroll_by(rows);
    preempt_enable();
}

void vga_scroll_down(int rows) {        /* newer lines, back to the prompt */
    preempt_disable();
    vga_scroll_by(-rows);
    preempt_enable();
}

void vga_scroll_top(void) {
    preempt_disable();
    vga_scroll_by(SCROLLBACK_LINES);
    preempt_enable();
}

void vga_scroll_bottom(void) {
    preempt_disable();
    vga_scroll_by(-SCROLLBACK_LINES);
    preempt_enable();
}

void print_hex(unsigned int n) {
    char hex[] = "0123456789ABCDEF";
    vga_print("0x");
    for (int i = 28; i >= 0; i -= 4) vga_putchar(hex[(n >> i) & 0xF]);
}

void print_dec(unsigned int n) {
    if (n == 0) { vga_putchar('0'); return; }
    char buf[12];
    int i = 0;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) vga_putchar(buf[--i]);
}
