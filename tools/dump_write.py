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
        time.sleep(0.25 if ch == ' ' else 0.15)
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

def show(prefix):
    print(f'=== {prefix} ===')
    for l in get_screen():
        print(l)
    print()

time.sleep(3)
drain(0.5)

show('BOOT')
type_str('touch testfile.txt')
time.sleep(0.8)
show('AFTER touch')
type_str('write testfile.txt hello world')
time.sleep(0.8)
show('AFTER write')
type_str('cat testfile.txt')
time.sleep(0.8)
show('AFTER cat')
type_str('rm testfile.txt')
time.sleep(0.8)
show('AFTER rm')

os.write(master, b'quit\n')
time.sleep(1)
qemu.kill()