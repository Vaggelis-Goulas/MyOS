import pty, os, subprocess, time, select, re

KEYMAP = {' ': 'spc', '.': 'dot', '\n': 'ret'}

master, slave = pty.openpty()
qemu = subprocess.Popen([
    'qemu-system-i386',
    '-drive', 'file=build/floppy.img,format=raw,if=floppy',
    '-drive', 'file=disk.img,format=raw,if=ide',
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

def type_str(s):
    for ch in s:
        os.write(master, f'sendkey {KEYMAP.get(ch, ch)}\n'.encode())
        time.sleep(0.12)
    os.write(master, b'sendkey ret\n')

def get_screen():
    os.write(master, b'xp /4000bx 0xb8000\n')
    data = drain(1.5)
    pairs = re.findall(r'0x([0-9a-fA-F]{2})', data.decode('latin-1'))
    if len(pairs) >= 4000:
        vals = [int(x, 16) for x in pairs][:4000]
        return [''.join(chr(vals[r*160+c*2]) if 32 <= vals[r*160+c*2] < 127 else '~'
                        for c in range(80)).rstrip() for r in range(25)]
    return []

def assert_screen(prefix, needles):
    screen = get_screen()
    joined = '\n'.join(screen)
    missing = [n for n in needles if n not in joined]
    ok = not missing
    print(f'[{"OK " if ok else "FAIL"}] {prefix}' + ('' if ok else f'  missing: {missing}'))
    return ok

time.sleep(3)
drain(0.5)

ok = True
ok &= assert_screen('boot', ['Welcome to MyOS', 'MEM] 32MB', 'PAG] Paging',
                    'ATA] Drive detected', 'FAT12 ready. 224 root entries'])

type_str('ls')
time.sleep(0.8)
ok &= assert_screen('ls initial', ['HELLO.TXT', 'TEST.TXT', 'README'])

type_str('touch newfile.txt')
time.sleep(0.8)
ok &= assert_screen('touch newfile', ['Created: NEWFILE.TXT'])

type_str('ls')
time.sleep(0.8)
ok &= assert_screen('ls after touch', ['NEWFILE.TXT'])

type_str('write newfile.txt Hello World from MyOS')
time.sleep(0.8)
ok &= assert_screen('write to file', ['Wrote', 'bytes to NEWFILE.TXT'])

type_str('cat newfile.txt')
time.sleep(0.8)
ok &= assert_screen('cat newfile', ['Hello World from MyOS'])

type_str('rm newfile.txt')
time.sleep(0.8)
ok &= assert_screen('rm file', ['Deleted: NEWFILE.TXT'])

type_str('ls')
time.sleep(0.8)
ok &= assert_screen('ls after rm', ['HELLO.TXT', 'TEST.TXT', 'README'])

type_str('cat newfile.txt')
time.sleep(0.8)
ok &= assert_screen('cat deleted file', ['File not found'])

os.write(master, b'quit\n')
time.sleep(1)
qemu.kill()
print('RESULT:', 'PASS' if ok else 'FAIL')