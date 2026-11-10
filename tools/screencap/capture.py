# Record one platform's README sequence from a running emulator.
# Run with pebble-tool's python: capture.py <platform> <outdir>
# A sampler thread screendumps through the QEMU monitor as fast as it can and keeps only frames that
# changed, each with the time it appeared; the main thread drives the buttons through pypkjs.
#
# The sequence is the one the earlier README animations showed, with a peek and a split added: dial
# a minute and start it, count down coarsely to the live seconds, pause into editing, reset, then a
# stopwatch with a split, and reset. Settings: 10-second updates above 40 s, minute updates above
# 1 min, a green timer and a red stopwatch.
import json, os, socket, sys, threading, time
from PIL import Image
from libpebble2.communication import PebbleConnection
from libpebble2.communication.transports.websocket import WebsocketTransport, MessageTargetPhone
from libpebble2.communication.transports.websocket.protocol import WebSocketRelayQemu
from libpebble2.communication.transports.qemu.protocol import QemuButton, QemuPacket

PLATFORM, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
info = json.load(open('/tmp/pb-emulator.json'))[PLATFORM]['4.44']

transport = WebsocketTransport('ws://127.0.0.1:%d/' % info['pypkjs']['port'])
pebble = PebbleConnection(transport)
pebble.connect()
pebble.run_async()

BUTTONS = {'back': QemuButton.Button.Back, 'up': QemuButton.Button.Up,
           'select': QemuButton.Button.Select, 'down': QemuButton.Button.Down}

def qemu(data):
    packet = QemuPacket(data=data)
    packet.serialise()
    transport.send_packet(WebSocketRelayQemu(protocol=packet.protocol, data=data.serialise()),
                          target=MessageTargetPhone())

def press(name, hold_ms=120):
    qemu(QemuButton(state=BUTTONS[name]))
    time.sleep(hold_ms / 1000.0)
    qemu(QemuButton(state=0))

frames = []      # (t, image)
marks = {}       # name -> index of the frame showing at that moment
stop = False
t0 = time.time()

def sampler():
    sock = socket.create_connection(('127.0.0.1', info['qemu']['monitor']), timeout=2)
    time.sleep(0.2)
    sock.recv(65536)
    last = None
    n = 0
    while not stop:
        path = os.path.join(OUT, 'dump%d.ppm' % (n % 2))
        if os.path.exists(path):
            os.remove(path)
        sock.sendall(('screendump %s\n' % path).encode())
        deadline = time.time() + 1.0
        while time.time() < deadline and not (os.path.exists(path) and os.path.getsize(path) > 0):
            time.sleep(0.002)
        try:
            sock.recv(65536)
        except socket.timeout:
            pass
        try:
            img = Image.open(path).convert('RGB')
            img.load()
        except Exception:
            continue
        now = time.time() - t0
        if last is None or img.tobytes() != last:
            frames.append((now, img))
            last = img.tobytes()
        n += 1

def mark(name):
    time.sleep(0.15)
    marks[name] = len(frames) - 1

def wait_until(seconds):
    time.sleep(max(0.0, started + seconds - (time.time() - t0)))

thread = threading.Thread(target=sampler, daemon=True)

press('select', 1000)          # a clean start: hold select resets to zero
time.sleep(1.0)
thread.start()
time.sleep(1.0)
mark('start')
press('up')                    # a minute on the minutes field
time.sleep(1.0)
press('select')                # to the seconds
time.sleep(1.0)
mark('setting')
press('select')                # start: 1:__ under minute updates, then 0:5_ and 0:4_
started = time.time() - t0
wait_until(14.0)               # 0:4_
mark('counting')
press('up')                    # a peek at the exact time
time.sleep(0.4)
mark('peek')
wait_until(24.3)               # the live seconds, 0:39 down to 0:36
press('select')                # pause, into editing
time.sleep(1.5)
mark('paused')
press('select', 1000)          # hold: reset to zero
time.sleep(1.5)
press('select')                # to the seconds
time.sleep(1.0)
press('select')                # the stopwatch starts, in its own accent
time.sleep(2.5)
mark('chrono')
press('up')                    # a split, its fraction in LECO
time.sleep(1.5)
mark('split')
press('up')                    # released
time.sleep(1.5)
press('select', 1000)          # reset
time.sleep(1.5)
stop = True
thread.join(timeout=3)

for i, (t, img) in enumerate(frames):
    img.save(os.path.join(OUT, 'f%04d.png' % i))
json.dump({'times': [t for t, _ in frames], 'marks': marks}, open(os.path.join(OUT, 'frames.json'), 'w'))
print(PLATFORM, len(frames), 'frames over', round(frames[-1][0] - frames[0][0], 1), 's; marks', marks)
os._exit(0)
