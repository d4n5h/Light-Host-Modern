"""Read physical window/DPI/monitor metrics; no window input or mutation."""
import argparse
import ctypes
import json
from ctypes import wintypes as w

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--hwnd", type=int, required=True)
parser.add_argument("--pid", type=int, required=True)
args = parser.parse_args()
user = ctypes.WinDLL("user32", use_last_error=True)
user.SetThreadDpiAwarenessContext.argtypes = [w.HANDLE]
user.SetThreadDpiAwarenessContext.restype = w.HANDLE
user.SetThreadDpiAwarenessContext(w.HANDLE(-4))  # Only this inspecting thread.
user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
pid = w.DWORD()
user.GetWindowThreadProcessId(w.HWND(args.hwnd), ctypes.byref(pid))
if pid.value != args.pid:
    raise RuntimeError("The observed window no longer belongs to the expected test process.")
user.GetDpiForWindow.argtypes = [w.HWND]
user.GetDpiForWindow.restype = w.UINT
user.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
rect = w.RECT()
if not user.GetWindowRect(w.HWND(args.hwnd), ctypes.byref(rect)):
    raise ctypes.WinError(ctypes.get_last_error())


class MonitorInfo(ctypes.Structure):
    _fields_ = [("size", w.DWORD), ("monitor", w.RECT), ("work", w.RECT), ("flags", w.DWORD)]


user.MonitorFromWindow.argtypes = [w.HWND, w.DWORD]
user.MonitorFromWindow.restype = w.HANDLE
user.GetMonitorInfoW.argtypes = [w.HANDLE, ctypes.POINTER(MonitorInfo)]
info = MonitorInfo()
info.size = ctypes.sizeof(info)
if not user.GetMonitorInfoW(user.MonitorFromWindow(w.HWND(args.hwnd), 2), ctypes.byref(info)):
    raise ctypes.WinError(ctypes.get_last_error())


def bounds(value):
    return {"x": value.left, "y": value.top, "width": value.right - value.left, "height": value.bottom - value.top}


print(json.dumps({"pid": pid.value, "dpi": user.GetDpiForWindow(w.HWND(args.hwnd)), "window": bounds(rect),
                  "workArea": bounds(info.work), "monitor": bounds(info.monitor),
                  "withinWorkArea": rect.left >= info.work.left and rect.top >= info.work.top and rect.right <= info.work.right and rect.bottom <= info.work.bottom}))
