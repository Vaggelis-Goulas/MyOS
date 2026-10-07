import pty, os, subprocess, time, select, re

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

# Just wait and drain output
for i in range(10):
    time.sleep(1)
    out = drain(0.5)
    if out:
        print(f'=== DRAIN {i} ===')
        print(out.decode('latin-1', errors='replace')[-1000:])

os.write(master, b'xp /4000bx 0xb8000\n')
data = drain(2)
pairs = re.findall(r'0x([0-9a-fA-F]{2})', data.decode('latin-1'))
if len(pairs) >= 4000:
    vals = [int(x, 16) for x in pairs][:4000]
    print('=== SCREEN ===')
    for r in range(25):
        line = ''.join(chr(vals[r*160+c*2]) if 32 <= vals[r*160+c*2] < 127 else '~' for c in range(80))
        print(repr(line.rstrip()))

os.write(master, b'quit\n')
time.sleep(1)
qemu.kill()
print('DONE')