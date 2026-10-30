"""Post mouse drags to Hyperion's background window while a replay plays, so units get
selected (hit-point bars, selection circles) and the drag box shows, and rest the
pointer on the command card now and then for its help bubble. Posts window messages
only, marked for HYPERION_TEST_MOUSE=1; the user's mouse is not touched."""
import ctypes
import ctypes.wintypes as wt
import sys
import time

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 40
user32 = ctypes.windll.user32
MARK = 0x1000
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, MK_LBUTTON = 0x200, 0x201, 0x202, 1


def find():
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(h, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, buf, 256)
        if buf.value == "Hyperion" and user32.IsWindowVisible(h):
            found.append(h)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def xy(x, y):
    return (y << 16) | x


def post(h, msg, w, x, y):
    user32.PostMessageW(h, msg, w | MARK, xy(x, y))


deadline = time.time() + 60
h = None
while time.time() < deadline and h is None:
    h = find()
    time.sleep(0.2)
if h is None:
    sys.exit("no Hyperion window")
time.sleep(8)  # into the game
boxes = [(40, 40, 600, 380), (100, 60, 400, 300), (300, 100, 620, 390), (20, 200, 330, 395)]
end = time.time() + seconds
n = 0
while time.time() < end and user32.IsWindow(h):
    x0, y0, x1, y1 = boxes[n % len(boxes)]
    post(h, WM_MOUSEMOVE, 0, x0, y0)
    post(h, WM_LBUTTONDOWN, MK_LBUTTON, x0, y0)
    for k in range(1, 6):
        time.sleep(0.15)
        post(h, WM_MOUSEMOVE, MK_LBUTTON, x0 + (x1 - x0) * k // 5, y0 + (y1 - y0) * k // 5)
    time.sleep(0.2)
    post(h, WM_LBUTTONUP, 0, x1, y1)
    time.sleep(1.0)
    if n % 3 == 2:
        post(h, WM_MOUSEMOVE, 0, 527 + (n // 3 % 3) * 46, 380)
        time.sleep(2.0)
    n += 1
print("drags", n)
