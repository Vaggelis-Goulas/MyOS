#ifndef KEYBOARD_H
#define KEYBOARD_H

/* Keys with no printable character are reported by keyboard_getchar() as
 * negative values, so they can share the input ring with ASCII text.
 * Consumers must compare against these before treating a byte as a char. */
#define KEY_UP    (-1)
#define KEY_DOWN  (-2)
#define KEY_HOME  (-3)
#define KEY_END   (-4)
#define KEY_PGUP  (-5)
#define KEY_PGDN  (-6)

void keyboard_init();
char keyboard_getchar();

#endif
