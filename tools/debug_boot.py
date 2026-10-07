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

time.sleep(6)
print('=== FULL DRAIN ===')
print(drain(2).decode('latin-1', errors='replace')[-3000:])

os.write(master, b'quit\n')
time.sleep(1)
qemu.kill()