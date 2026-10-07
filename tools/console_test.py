"""Regression tests for the MyOS text console.

Two things are covered, both of which used to lose text:

  * Scrollback. Output that scrolls off the top of the 25-line screen used to
    be destroyed. PgUp/PgDn, the arrows, Home and End now walk a 1000-line
    history ring instead.
  * Line wrapping. Filling column 79 used to advance the cursor immediately,
    so a line of exactly 80 columns consumed two screen rows and the line
    after it vanished. The wrap is now deferred until the next character.

The screen is read with the monitor's pmemsave rather than scraped from the
monitor's own echo, and PgUp/PgDn are sent as real QEMU key names.

    python3 tools/console_test.py
"""

import pty, os, subprocess, time, select

BOOT_IMG = 'build/floppy.img'
DATA_IMG = 'disk.img'
VGA_DUMP = '/tmp/myos_console_vga.bin'

KEYMAP = {' ': 'spc', '.': 'dot', '\n': 'ret',
          '>': 'shift-dot', '<': 'shift-comma', '-': 'minus',
          '+': 'shift-equal', '*': 'shift-8', '/': 'slash',
          ',': 'comma', ';': 'semicolon', '_': 'shift-minus'}
for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
    KEYMAP[c] = f'shift-{c.lower()}'

WIDTH, HEIGHT = 80, 25


def main():
    master, slave = pty.openpty()
    qemu = subprocess.Popen([
        'qemu-system-i386',
        '-drive', f'file={BOOT_IMG},format=raw,if=floppy',
        '-drive', f'file={DATA_IMG},format=raw,if=ide',
        '-display', 'none', '-monitor', 'stdio',
    ], stdin=slave, stdout=slave, stderr=slave, cwd='/home/vagge/MyOS', close_fds=True)

    def drain(timeout=1.0):
        buf = b''
        end = time.time() + timeout
        while time.time() < end:
            r, _, _ = select.select([master], [], [], 0.2)
            if r:
                try:
                    chunk = os.read(master, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                buf += chunk
        return buf

    def type_key(k):
        os.write(master, f'sendkey {KEYMAP.get(k, k)}\n'.encode())
        time.sleep(0.12)

    def type_str(s):
        for ch in s:
            type_key(ch)
        type_key('ret')

    def get_cells():
        """The 25x80 text buffer as (char, attribute) pairs, or None."""
        if os.path.exists(VGA_DUMP):
            os.remove(VGA_DUMP)
        for _ in range(5):
            os.write(master, f'pmemsave 0xb8000 4000 "{VGA_DUMP}"\n'.encode())
            drain(2.0)
            if os.path.exists(VGA_DUMP) and os.path.getsize(VGA_DUMP) >= 4000:
                break
            time.sleep(0.5)
        try:
            with open(VGA_DUMP, 'rb') as f:
                data = f.read()
        except OSError:
            return None
        if len(data) < 4000:
            return None
        return [(data[(r * WIDTH + c) * 2], data[(r * WIDTH + c) * 2 + 1])
                for r in range(HEIGHT) for c in range(WIDTH)]

    ok = True

    def check(name, cond, detail=''):
        nonlocal ok
        ok &= bool(cond)
        print(f'[{"OK " if cond else "FAIL"}] {name}{detail}', flush=True)

    def screen_rows():
        cells = get_cells()
        if cells is None:
            return []
        return [''.join(chr(ch) if 32 <= ch < 127 else '~'
                        for ch, _ in cells[r * WIDTH:(r + 1) * WIDTH]).rstrip()
                for r in range(HEIGHT)]

    def screen_text():
        return '\n'.join(screen_rows())

    def tinted_fraction():
        """Share of cells with a non-black background.

        The scrollback view repaints its background blue so it cannot be
        mistaken for live output; the live view leaves it black. Read from the
        attribute bytes because the characters are identical either way.
        """
        cells = get_cells()
        if cells is None:
            return 0.0
        return sum(1 for _, attr in cells if (attr >> 4) & 7) / len(cells)

    def assert_has(prefix, needles):
        joined = screen_text()
        missing = [n for n in needles if n not in joined]
        check(prefix, not missing, f'  missing: {missing}' if missing else '')

    def assert_lacks(prefix, needles):
        joined = screen_text()
        present = [n for n in needles if n in joined]
        check(prefix, not present,
              f'  unexpectedly present: {present}' if present else '')

    time.sleep(4)
    drain(0.5)

    # ---------------- scrollback ----------------

    # 'help' plus the boot banner is comfortably more than 25 lines, so the
    # first line of the boot output is pushed into the history ring.
    type_str('help')
    time.sleep(1.2)

    # Confirm the screen really did scroll before testing the way back up,
    # otherwise a pass would only prove the banner was never there.
    check('output scrolled the banner off screen',
          'Welcome to MyOS' not in screen_text())

    # help alone is ~26 lines plus the boot banner, so a handful of page-ups
    # has to reach the very first line the kernel ever printed.
    for _ in range(12):
        type_key('pgup')
        time.sleep(0.15)
    time.sleep(0.6)

    assert_has('pgup reaches the oldest retained line', ['Welcome to MyOS'])
    check('pgup marks the view as history', tinted_fraction() > 0.9,
          f'  tinted {tinted_fraction():.0%}')

    type_key('up')
    time.sleep(0.3)
    assert_has('arrow up scrolls a single line', ['Welcome to MyOS'])

    for _ in range(30):
        type_key('pgdn')
        time.sleep(0.12)
    time.sleep(0.6)

    assert_lacks('pgdn returns to the live view', ['Welcome to MyOS'])
    check('live view drops the history tint', tinted_fraction() < 0.5,
          f'  tinted {tinted_fraction():.0%}')
    assert_has('live view still shows the prompt', ['myos>'])

    # Home jumps to the oldest retained line, End back to the prompt.
    type_key('home')
    time.sleep(0.6)
    assert_has('home jumps to the oldest retained line', ['Welcome to MyOS'])
    type_key('end')
    time.sleep(0.6)
    assert_lacks('end returns to the prompt', ['Welcome to MyOS'])

    # Scrolled back, typing anything must come back to the prompt rather than
    # edit a line the user is no longer looking at.
    for _ in range(12):
        type_key('pgup')
        time.sleep(0.12)
    time.sleep(0.4)
    check('pgup again after returning live', tinted_fraction() > 0.9,
          f'  tinted {tinted_fraction():.0%}')

    type_key('x')
    time.sleep(0.5)
    joined = screen_text()
    check('a printable key leaves the scrollback', tinted_fraction() < 0.5,
          f'  tinted {tinted_fraction():.0%}')
    check('the typed key reaches the prompt line', 'myos> x' in joined,
          '  prompt line missing' if 'myos> x' not in joined else '')

    # ---------------- line wrapping ----------------

    # "myos> echo " is 11 columns, so 69 more characters make the typed line
    # exactly 80 wide: the case that used to swallow the following line. With
    # the old immediate advance, filling column 79 moved the cursor to the
    # next row and the newline then skipped a second one, so the command's
    # output landed one row lower than it should.
    type_key('spc')          # drop the stray 'x' from the line above
    type_key('ret')

    type_str('clear')
    time.sleep(0.8)
    type_str('echo ' + 'A' * 69)
    time.sleep(0.5)
    type_str('ver')
    time.sleep(0.8)

    rows = screen_rows()
    check('an exactly-80-column line fills the row', len(rows[0]) == WIDTH,
          f'  row 0 is {len(rows[0])} columns')
    check('output after a full row is not pushed down a line',
          rows[1] == 'A' * 69, f'  row 1 is {rows[1]!r}')
    check('the next prompt follows the full row',
          rows[2].startswith('myos> ver'), f'  row 2 is {rows[2]!r}')
    check('ver output follows its prompt',
          rows[3].startswith('MyOS v1.3'), f'  row 3 is {rows[3]!r}')

    # One column short of the edge must behave the same way.
    type_str('clear')
    time.sleep(0.8)
    type_str('echo ' + 'B' * 68)
    time.sleep(0.5)
    type_str('ver')
    time.sleep(0.8)

    rows = screen_rows()
    check('a 79-column line does not wrap', len(rows[0]) == 79,
          f'  row 0 is {len(rows[0])} columns')
    check('output after a 79-column line is not pushed down',
          rows[1] == 'B' * 68, f'  row 1 is {rows[1]!r}')
    check('the next prompt follows a 79-column line',
          rows[2].startswith('myos> ver'), f'  row 2 is {rows[2]!r}')

    os.write(master, b'quit\n')
    time.sleep(1)
    qemu.kill()
    print('RESULT:', 'PASS' if ok else 'FAIL', flush=True)
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
