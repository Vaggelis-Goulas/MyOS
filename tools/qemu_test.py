import pty, os, subprocess, time, select

BOOT_IMG = 'build/floppy.img'
DATA_IMG = 'disk.img'
VGA_DUMP = '/tmp/myos_vga.bin'

KEYMAP = {' ': 'spc', '.': 'dot', '\n': 'ret',
          '>': 'shift-dot', '<': 'shift-comma', '-': 'minus',
          '+': 'shift-equal', '*': 'shift-8', '/': 'slash',
          ',': 'comma', ';': 'semicolon', '_': 'shift-minus'}
# Add uppercase letters with shift (QEMU format: shift-a)
for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
    KEYMAP[c] = f'shift-{c.lower()}'


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

    def clear():
        """Blank the screen and wait for it.

        The console has no erase-to-end-of-line, so output from earlier
        commands stays on screen. Every check that looks at command output
        clears first, otherwise a stale line (for instance the echoed
        command itself) could satisfy an assertion by accident.
        """
        type_str('clear')
        time.sleep(0.6)

    def get_screen():
        """Read the 80x25 VGA text buffer via the monitor.

        pmemsave dumps the buffer to a file, which is much more reliable
        than scraping the monitor's "xp" output: the terminal echo of the
        xp command contains "0xb8000", and its "0xb8" would otherwise be
        parsed as the first screen byte and shift every following cell.
        """
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
            return []
        if len(data) < 4000:
            return []
        return [''.join(chr(data[(r * 80 + c) * 2])
                        if 32 <= data[(r * 80 + c) * 2] < 127 else '~'
                        for c in range(80)).rstrip()
                for r in range(25)]

    def assert_screen(prefix, needles, absent=()):
        screen = get_screen()
        joined = '\n'.join(screen)
        missing = [n for n in needles if n not in joined]
        present = [n for n in absent if n in joined]
        ok = not missing and not present
        detail = ''
        if missing:
            detail += f'  missing: {missing}'
        if present:
            detail += f'  unexpectedly present: {present}'
        print(f'[{"OK " if ok else "FAIL"}] {prefix}{detail}', flush=True)
        return ok

    time.sleep(4)
    drain(0.5)

    ok = True
    ok &= assert_screen('boot + mem + ATA + FAT12', ['Welcome to MyOS',
                        'MEM] 32MB physical', 'PAG] Paging enabled',
                        'ATA] Drive detected', 'FAT12 ready. 224 root entries'])

    clear()
    type_str('ls')
    time.sleep(0.8)
    ok &= assert_screen('ls lists files', ['HELLO.TXT', 'TEST.TXT', 'README',
                                           '56 bytes', '51 bytes'])

    clear()
    type_str('cat hello.txt')
    time.sleep(1.0)
    ok &= assert_screen('cat hello.txt reads content', ['Hello from MyOS!',
                                                        'read from the FAT12 disk.'])

    clear()
    type_str('cat nosuchfile')
    time.sleep(0.8)
    ok &= assert_screen('cat missing file errors', ['File not found'])

    clear()
    type_str('touch newfile.txt')
    time.sleep(0.8)
    ok &= assert_screen('touch creates file', ['Created: newfile.txt'])

    clear()
    type_str('ls')
    time.sleep(0.8)
    # 'NEWFILE' rather than 'NEWFILE.TXT': the listing is wider than 80
    # columns, so the name itself can be split across the wrap point.
    ok &= assert_screen('ls shows newfile', ['NEWFILE'])

    clear()
    type_str('write newfile.txt Hello World')
    time.sleep(0.8)
    ok &= assert_screen('write writes file', ['Wrote', 'bytes to newfile.txt'])

    clear()
    type_str('cat newfile.txt')
    time.sleep(0.8)
    ok &= assert_screen('cat reads newfile', ['Hello World'])

    clear()
    type_str('echo test output > redirect.txt')
    time.sleep(0.8)

    clear()
    type_str('cat redirect.txt')
    time.sleep(0.8)
    ok &= assert_screen('cat redirect after >', ['test output'])

    clear()
    type_str('echo appended >> redirect.txt')
    time.sleep(0.8)

    clear()
    type_str('cat redirect.txt')
    time.sleep(0.8)
    ok &= assert_screen('cat redirect after >>', ['test output', 'appended'])

    clear()
    type_str('rm newfile.txt')
    time.sleep(0.8)
    ok &= assert_screen('rm deletes file', ['Deleted: newfile.txt'])

    # newfile.txt must be gone from the directory listing now, and
    # redirect.txt must still be there. Needles stay short because the
    # listing wraps at column 80.
    clear()
    type_str('ls')
    time.sleep(0.8)
    ok &= assert_screen('ls after rm', ['HELLO.TXT', 'FAT12 Root Directory'],
                        absent=['NEWFILE'])

    os.write(master, b'quit\n')
    time.sleep(1)
    qemu.kill()
    print('RESULT:', 'PASS' if ok else 'FAIL', flush=True)
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())