#ifndef VGA_H
#define VGA_H

void vga_clear();
void vga_putchar(char c);
void vga_print(const char *s);
void vga_print_color(const char *s, int color);
void vga_disable_cursor();
void vga_set_color(int color);
void print_hex(unsigned int n);
void print_dec(unsigned int n);

/* Scrollback: rows that have scrolled off the top stay readable. The view is
 * clamped to what is actually stored, so scrolling past either end is a
 * no-op rather than an error. */
int  vga_scrollback_view(void);
void vga_scroll_up(int rows);
void vga_scroll_down(int rows);
void vga_scroll_top(void);
void vga_scroll_bottom(void);

#endif
