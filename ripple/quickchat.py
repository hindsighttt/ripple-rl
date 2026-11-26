import ctypes, ctypes.wintypes as wt, time, threading
import os
from PySide6.QtCore import QAbstractNativeEventFilter
from . import features

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
ULONG_PTR = ctypes.c_size_t


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ULONG_PTR)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD),
                ("dwFlags", wt.DWORD), ("time", wt.DWORD), ("dwExtraInfo", ULONG_PTR)]


class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [("uMsg", wt.DWORD), ("wParamL", wt.WORD), ("wParamH", wt.WORD)]


class INPUTUNION(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT), ("hi", HARDWAREINPUT)]


class INPUT(ctypes.Structure):
    _anonymous_ = ("value",)
    _fields_ = [("type", wt.DWORD), ("value", INPUTUNION)]


user32.SendInput.argtypes = [wt.UINT, ctypes.POINTER(INPUT), ctypes.c_int]
user32.SendInput.restype = wt.UINT
user32.GetForegroundWindow.restype = wt.HWND
user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
kernel32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
kernel32.OpenProcess.restype = wt.HANDLE
kernel32.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)]
kernel32.CloseHandle.argtypes = [wt.HANDLE]
QC_KB = 1048576
_send_lock = threading.Lock()


def key_label(code):
    code = int(code)
    vk = code & 0xFFFF if code & QC_KB else code
    if not 1 <= vk <= 255:
        return ""
    scan = user32.MapVirtualKeyW(vk, 0)
    extended = vk in (0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E, 0x6F)
    value = scan << 16 | int(extended) << 24
    label = ctypes.create_unicode_buffer(128)
    user32.GetKeyNameTextW(value, label, len(label))
    return label.value


WM_HOTKEY = 0x0312
MODS = {"alt": 0x1, "ctrl": 0x2, "shift": 0x4, "win": 0x8}
VK = {"tab": 0x09, "space": 0x20, "enter": 0x0D, "f1": 0x70}
for i in range(1, 25):
    VK["f%d" % i] = 0x6F + i
for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789":
    VK[c.lower()] = ord(c)


def parse(combo):
    if "@" in (combo or ""):
        try:
            key, mods = (int(x) for x in combo.rsplit("@", 1)[1].split(":"))
            return (mods, key) if 0 <= mods <= 15 and 1 <= key <= 255 else (0, None)
        except ValueError:
            return 0, None
    mods = 0
    key = None
    for part in (combo or "").split("+"):
        p = part.strip().lower()
        if p in MODS:
            mods |= MODS[p]
        elif p:
            key = VK.get(p)
    return mods, key


def _game_window():
    window = user32.GetForegroundWindow()
    process_id = wt.DWORD()
    user32.GetWindowThreadProcessId(window, ctypes.byref(process_id))
    process = kernel32.OpenProcess(0x1000, False, process_id.value)
    if not process:
        return None
    try:
        path = ctypes.create_unicode_buffer(32768)
        size = wt.DWORD(len(path))
        if kernel32.QueryFullProcessImageNameW(process, 0, path, ctypes.byref(size)):
            if os.path.basename(path.value).lower() == "rocketleague.exe":
                return window
    finally:
        kernel32.CloseHandle(process)
    return None


def _key(vk=0, scan=0, flags=0):
    inputs = (INPUT * 2)()
    inputs[0].type = inputs[1].type = 1
    inputs[0].ki = KEYBDINPUT(vk, scan, flags, 0, 0)
    inputs[1].ki = KEYBDINPUT(vk, scan, flags | 0x2, 0, 0)
    if user32.SendInput(2, inputs, ctypes.sizeof(INPUT)) != 2:
        raise ctypes.WinError(ctypes.get_last_error())


def _type_unicode(text, focused=lambda: True):
    encoded = text.encode("utf-16le")
    for offset in range(0, len(encoded), 2):
        if not focused():
            return False
        _key(scan=int.from_bytes(encoded[offset:offset + 2], "little"), flags=0x4)
        time.sleep(0.004)
    return True


def _send_chat(text, target=0, trigger_keys=(), cancel_event=None):
    settings = features.get("qc")
    window = _game_window()
    if not settings.get("enabled", True) or window is None or not _send_lock.acquire(False):
        return
    try:
        # Release shortcut modifiers before opening the game's chat field.
        deadline = time.monotonic() + 0.5
        release_keys = (0x10, 0x11, 0x12) + tuple(trigger_keys)
        while any(user32.GetAsyncKeyState(vk) & 0x8000 for vk in release_keys):
            if time.monotonic() >= deadline or (cancel_event and cancel_event.is_set()):
                return
            time.sleep(0.01)
        target = target if target in (0, 1, 2) else 0
        keys = settings.get("keys") or []
        configured = keys[target].get("vk", 0) if target < len(keys) else 0
        vk = int(configured or (ord("T"), ord("Y"), ord("U"))[target])
        focused = lambda: user32.GetForegroundWindow() == window and not (cancel_event and cancel_event.is_set())
        if not focused():
            return
        _key(vk=vk)
        time.sleep(0.08)
        if _type_unicode(text, focused) and focused():
            _key(vk=0x0D)
    finally:
        _send_lock.release()


def _start_chat(text, target=0, trigger_keys=(), cancel_event=None):
    threading.Thread(target=_send_chat, args=(text, target, trigger_keys, cancel_event), daemon=True).start()


class Hotkeys(QAbstractNativeEventFilter):
    def __init__(self):
        super().__init__()
        self._ids = {}
        self._next = 1
        self._stop_event = threading.Event()
        self._poll_thread = None

    def bind(self, combo, callback):
        mods, key = parse(combo)
        if not key:
            return False
        hid = self._next
        self._next += 1
        if user32.RegisterHotKey(None, hid, mods, key):
            self._ids[hid] = callback
            return True
        return False

    def nativeEventFilter(self, evtype, message):
        try:
            msg = ctypes.cast(int(message), ctypes.POINTER(wt.MSG)).contents
            if msg.message == WM_HOTKEY and msg.wParam in self._ids:
                self._ids[msg.wParam]()
        except Exception:
            pass
        return False, 0

    def unbind_all(self):
        self._stop_event.set()
        if self._poll_thread:
            self._poll_thread.join(0.5)
        for hid in list(self._ids):
            user32.UnregisterHotKey(None, hid)
        self._ids.clear()

    def watch_sequences(self, entries):
        sequences = []
        for entry in entries:
            codes = entry.get("seq") or []
            if codes and all(isinstance(code, int) and QC_KB < code < QC_KB + 256 for code in codes):
                sequences.append((tuple(code & 0xFFFF for code in codes), entry))
        if sequences:
            self._poll_thread = threading.Thread(target=self._watch_sequences, args=(sequences,), daemon=True)
            self._poll_thread.start()

    def _watch_sequences(self, sequences):
        keys = {vk for sequence, _ in sequences for vk in sequence}
        down = set()
        history = []
        last_press = 0.0
        longest = max(len(sequence) for sequence, _ in sequences)
        while not self._stop_event.wait(0.01):
            pressed = {vk for vk in keys if user32.GetAsyncKeyState(vk) & 0x8000}
            edges = pressed - down
            down = pressed
            if _send_lock.locked() or _game_window() is None:
                history.clear()
                continue
            if not edges:
                continue
            now = time.monotonic()
            if now - last_press > 1.0:
                history.clear()
            last_press = now
            for vk in sorted(edges):
                history.append(vk)
                history = history[-longest:]
                for sequence, entry in sequences:
                    if tuple(history[-len(sequence):]) == sequence:
                        _start_chat(entry["text"], entry.get("target", 0), sequence, self._stop_event)
                        history.clear()
                        break


def quickchat(entries):
    """Bind legacy shortcuts and the SPA's keyboard sequences to chat entries."""
    hk = Hotkeys()
    for e in entries:
        text = (e.get("text") or "").strip()
        if not text:
            continue
        combo = e.get("key", "")
        _, trigger = parse(combo)
        hk.bind(combo, lambda t=text, target=e.get("target", 0), key=trigger: _start_chat(t, target, (key,), hk._stop_event))
    hk.watch_sequences([e for e in entries if (e.get("text") or "").strip()])
    return hk

