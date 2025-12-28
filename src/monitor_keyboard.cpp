// src/monitor_keyboard.cpp
// Small Windows CLI to monitor keyboard attach/detach and capture raw keyboard events.
// Usage similar to monitor_mouse: --poll=ms --capture --output=path --json

#define _WIN32_WINNT 0x0601
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>

#pragma comment(lib, "setupapi.lib")

static const GUID GUID_DEVINTERFACE_HID =
{0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0x00,0x11,0x11,0x00,0x00,0x30}};

struct KeyboardDevice {
    std::string devicePath;
    std::string friendlyName;
    std::string vidpid;
};

static std::string toLower(const std::string& s){
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

static std::string extractVidPid(const std::string &devPath){
    auto lower = toLower(devPath);
    size_t vid = lower.find("vid_");
    size_t pid = lower.find("pid_");
    if(vid!=std::string::npos && pid!=std::string::npos){
        return devPath.substr(vid, (pid+8)-vid);
    }
    return "";
}

static std::string getFriendlyNameForDevicePath(const std::string &devicePath) {
    std::string lowerPath = toLower(devicePath);

    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_HID, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return "";

    SP_DEVICE_INTERFACE_DATA ifData;
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    DWORD idx = 0;
    char bufDetail[4096];

    while (SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVINTERFACE_HID, idx++, &ifData)) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailA(hDevInfo, &ifData, NULL, 0, &required, NULL);
        if(required == 0 || required > sizeof(bufDetail)) continue;

        PSP_DEVICE_INTERFACE_DETAIL_DATA_A pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)bufDetail;
        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        SP_DEVINFO_DATA devInfo;
        devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
        if (SetupDiGetDeviceInterfaceDetailA(hDevInfo, &ifData, pDetail, required, NULL, &devInfo)) {
            std::string path = pDetail->DevicePath;
            if (toLower(path).find(lowerPath) != std::string::npos ||
                lowerPath.find(toLower(path)) != std::string::npos) {
                char propBuf[512];
                if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfo, SPDRP_FRIENDLYNAME, NULL, (PBYTE)propBuf, sizeof(propBuf), NULL)) {
                    SetupDiDestroyDeviceInfoList(hDevInfo);
                    return std::string(propBuf);
                }
                if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfo, SPDRP_DEVICEDESC, NULL, (PBYTE)propBuf, sizeof(propBuf), NULL)) {
                    SetupDiDestroyDeviceInfoList(hDevInfo);
                    return std::string(propBuf);
                }
                SetupDiDestroyDeviceInfoList(hDevInfo);
                return path;
            }
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return "";
}

static std::vector<KeyboardDevice> enumerateKeyboards() {
    std::vector<KeyboardDevice> res;
    UINT nDevices = 0;
    if (GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST)) != 0) {
        return res;
    }
    if (nDevices == 0) return res;
    std::vector<RAWINPUTDEVICELIST> devices(nDevices);
    if (GetRawInputDeviceList(devices.data(), &nDevices, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1) return res;

    for (UINT i = 0; i < nDevices; ++i) {
        if (devices[i].dwType != RIM_TYPEKEYBOARD) continue;
        UINT cb = 0;
        GetRawInputDeviceInfoA(devices[i].hDevice, RIDI_DEVICENAME, NULL, &cb);
        if (cb == 0) continue;
        std::vector<char> nameBuf(cb + 1);
        if (GetRawInputDeviceInfoA(devices[i].hDevice, RIDI_DEVICENAME, nameBuf.data(), &cb) == (UINT)-1) continue;
        std::string devicePath(nameBuf.data());
        std::string vidpid = extractVidPid(devicePath);
        std::string friendly = getFriendlyNameForDevicePath(devicePath);
        if (friendly.empty()) friendly = devicePath;
        res.push_back({devicePath, friendly, vidpid});
    }
    return res;
}

static std::map<std::string, KeyboardDevice> makeMap(const std::vector<KeyboardDevice>& list) {
    std::map<std::string, KeyboardDevice> m;
    for (auto &d : list) m[d.devicePath] = d;
    return m;
}

class Logger {
public:
    Logger() = default;
    ~Logger(){ if (ofs && ofs->is_open()) ofs->close(); }
    void openFile(const std::string &path) {
        std::lock_guard<std::mutex> lk(mu);
        ofs = std::make_unique<std::ofstream>(path, std::ios::app);
    }
    void setJson(bool v){ json = v; }
    void log(const std::string &line) {
        std::lock_guard<std::mutex> lk(mu);
        std::cout << line << std::endl;
        if (ofs && ofs->is_open()) {
            if (json) {
                std::string j = "{ \"message\": \"";
                for (char c : line) { if (c == '"') j += '\\'; j.push_back(c); }
                j += "\" }";
                (*ofs) << j << std::endl;
            } else {
                (*ofs) << line << std::endl;
            }
            ofs->flush();
        }
    }
private:
    std::unique_ptr<std::ofstream> ofs;
    std::mutex mu;
    bool json = false;
};

static std::atomic<bool> running{true};

BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        running = false;
        return TRUE;
    }
    return FALSE;
}

// Globals for capture thread
static Logger *gLogger = nullptr;
static bool gJson = false;

static LRESULT CALLBACK MonitorKeyboardWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_INPUT) {
        UINT dwSize = 0;
        GetRawInputData((HRAWINPUT)l, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize > 0) {
            std::vector<BYTE> buf(dwSize);
            if (GetRawInputData((HRAWINPUT)l, RID_INPUT, buf.data(), &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
                RAWINPUT* raw = (RAWINPUT*)buf.data();
                if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                    auto &k = raw->data.keyboard;
                    // k.VKey, k.MakeCode, k.Flags: RI_KEY_MAKE=0 (make), RI_KEY_BREAK=1 (break)
                    bool released = (k.Flags & RI_KEY_BREAK) != 0;
                    auto vkeyName = [&](UINT vkey, UINT scanCode, UINT flags)->std::string {
                        // Function keys
                        if (vkey >= VK_F1 && vkey <= VK_F24) {
                            int n = vkey - VK_F1 + 1;
                            return std::string("F") + std::to_string(n);
                        }
                        switch (vkey) {
                            case VK_NUMPAD0: return "NumPad0";
                            case VK_NUMPAD1: return "NumPad1";
                            case VK_NUMPAD2: return "NumPad2";
                            case VK_NUMPAD3: return "NumPad3";
                            case VK_NUMPAD4: return "NumPad4";
                            case VK_NUMPAD5: return "NumPad5";
                            case VK_NUMPAD6: return "NumPad6";
                            case VK_NUMPAD7: return "NumPad7";
                            case VK_NUMPAD8: return "NumPad8";
                            case VK_NUMPAD9: return "NumPad9";
                            case VK_RETURN: return (flags & RI_KEY_E0) ? "Enter (numpad)" : "Enter";
                            case VK_INSERT: return "Insert";
                            case VK_DELETE: return "Delete";
                            case VK_HOME: return "Home";
                            case VK_END: return "End";
                            case VK_PRIOR: return "PageUp";
                            case VK_NEXT: return "PageDown";
                            case VK_SCROLL: return "ScrollLock";
                            case VK_NUMLOCK: return "NumLock";
                            case VK_CAPITAL: return "CapsLock";
                            case VK_SPACE: return "Space";
                            case VK_TAB: return "Tab";
                            case VK_BACK: return "Backspace";
                            case VK_ESCAPE: return "Esc";
                            case VK_LEFT: return "Left";
                            case VK_RIGHT: return "Right";
                            case VK_UP: return "Up";
                            case VK_DOWN: return "Down";
                            case VK_MULTIPLY: return "NumPad*";
                            case VK_ADD: return "NumPad+";
                            case VK_SUBTRACT: return "NumPad-";
                            case VK_DECIMAL: return "NumPad.";
                            case VK_DIVIDE: return "NumPad/";
                        }
                        // Printable top-row digits
                        if (vkey >= '0' && vkey <= '9') {
                            return std::string(1, (char)vkey);
                        }
                        // Letters
                        if ((vkey >= 'A' && vkey <= 'Z') || (vkey >= 'a' && vkey <= 'z')) {
                            return std::string(1, (char)vkey);
                        }
                        // Default: return hex
                        char buf[32];
                        sprintf_s(buf, "VK_0x%02X", vkey);
                        return std::string(buf);
                    };

                    bool isNumpad = (k.VKey >= VK_NUMPAD0 && k.VKey <= VK_NUMPAD9) || k.VKey == VK_DECIMAL || k.VKey == VK_ADD || k.VKey == VK_SUBTRACT || k.VKey == VK_MULTIPLY || k.VKey == VK_DIVIDE;
                    std::string keyName = vkeyName(k.VKey, k.MakeCode, k.Flags);
                     // translate to character (UTF-8) when possible
                     auto translateVKey = [&](UINT vkey, UINT scanCode, UINT flags)->std::string {
                         BYTE keyState[256];
                         if (!GetKeyboardState(keyState)) memset(keyState, 0, sizeof(keyState));
                         // account for extended key flag: set high bit on scanCode for some calls (not strictly necessary)
                         UINT sc = scanCode;
                         if (flags & RI_KEY_E0) sc |= 0xE000;
                         WCHAR wbuf[8] = {};
                         int rc = ToUnicode(vkey, sc, keyState, wbuf, (int)std::size(wbuf), 0);
                         if (rc > 0) {
                             int needed = WideCharToMultiByte(CP_UTF8, 0, wbuf, rc, NULL, 0, NULL, NULL);
                             if (needed > 0) {
                                 std::string out; out.resize(needed);
                                 WideCharToMultiByte(CP_UTF8, 0, wbuf, rc, &out[0], needed, NULL, NULL);
                                 return out;
                             }
                         }
                         return std::string();
                     };
 
                     std::string ch = translateVKey(k.VKey, k.MakeCode, k.Flags);
                     std::ostringstream ss;
                     ss << "[INPUT] keyboard: key=" << keyName << " vkey=" << (int)k.VKey << " make=" << k.MakeCode << " flags=" << k.Flags << " " << (released?"RELEASE":"PRESS");
                     if (!ch.empty()) {
                         // show ASCII/UTF-8 and numeric codes
                         // if single-byte ASCII, print decimal and hex
                         if (ch.size() == 1 && (unsigned char)ch[0] < 128) {
                             unsigned char c = (unsigned char)ch[0];
                             ss << " char='" << ch << "' (" << (int)c << ",0x" << std::hex << (int)c << std::dec << ")";
                         } else {
                             // print UTF-8 bytes in hex
                             ss << " char='" << ch << "' bytes=";
                             for (unsigned char b : ch) {
                                 ss << "0x" << std::hex << (int)b << " ";
                             }
                             ss << std::dec;
                         }
                     }
                     // indicate numeric keypad vs top row for numeric keys
                     if (isNumpad) ss << " (numpad)";
                     // include current lock states
                     int numLock = GetKeyState(VK_NUMLOCK) & 1;
                     int scrollLock = GetKeyState(VK_SCROLL) & 1;
                     ss << " locks:{NumLock=" << (numLock?"ON":"OFF") << ",ScrollLock=" << (scrollLock?"ON":"OFF") << "}";
                     if (gLogger) gLogger->log(ss.str()); else std::cout << ss.str() << std::endl;
                 }
             }
         }
         return 0;
    } else if (msg == WM_DESTROY) {
        PostQuitMessage(0);
    }
    return DefWindowProcA(h, msg, w, l);
}

void printDevice(const KeyboardDevice &d, Logger &log, bool json) {
    std::ostringstream ss;
    if (!json) {
        ss << "[Keyboard] " << d.friendlyName;
        if (!d.vidpid.empty()) ss << " (" << d.vidpid << ")";
        ss << " -- " << d.devicePath;
    } else {
        ss << "{ \"type\": \"keyboard\", \"name\": \"";
        for (char c : d.friendlyName) { if (c=='"') ss << '\\'; ss << c; }
        ss << "\", \"vidpid\": \"" << d.vidpid << "\", \"path\": \"";
        for (char c : d.devicePath) { if (c=='"') ss << '\\'; ss << c; }
        ss << "\" }";
    }
    log.log(ss.str());
}

int main(int argc, char **argv) {
    int pollMs = 1000;
    bool captureEvents = false;
    std::string outPath;
    bool json = false;
    for (int i=1;i<argc;i++){
        std::string a = argv[i];
        if (a.rfind("--poll=",0)==0) pollMs = std::max(100, std::stoi(a.substr(7)));
        else if (a == "--capture") captureEvents = true;
        else if (a.rfind("--output=",0)==0) outPath = a.substr(9);
        else if (a == "--json") json = true;
        else if (a == "--help" || a=="-h") {
            std::cout << "Usage: monitor_keyboard [--poll=ms] [--capture] [--output=path] [--json]\n"
                      << "  --poll=ms   Poll interval in ms (default 1000)\n"
                      << "  --capture   Capture live keyboard events (receive WM_INPUT)\n"
                      << "  --output=path  Append events to path (also printed to stdout)\n"
                      << "  --json      Emit JSON-ish log lines when --output is used\n";
            return 0;
        }
    }

    Logger logger;
    logger.setJson(json);
    if (!outPath.empty()) logger.openFile(outPath);

    SetConsoleCtrlHandler(consoleHandler, TRUE);

    logger.log(std::string("Monitoring keyboards every ") + std::to_string(pollMs) + " ms. Capture: " + (captureEvents?"ON":"OFF"));
    auto initial = enumerateKeyboards();
    auto known = makeMap(initial);
    if (!known.empty()) {
        logger.log("Initial keyboards:");
        for (auto &p : known) printDevice(p.second, logger, json);
    } else {
        logger.log("No keyboards detected at startup.");
    }

    std::thread msgThread;

    if (captureEvents) {
        msgThread = std::thread([&logger,json](){
            gLogger = &logger;
            gJson = json;
            WNDCLASSA wc = {};
            wc.lpfnWndProc = MonitorKeyboardWndProc;
            wc.lpszClassName = "MonitorKeyboardMsgWnd";
            if (!RegisterClassA(&wc)) return;
            HWND h = CreateWindowA(wc.lpszClassName, "MonitorKeyboardHidden", 0,0,0,0,0, HWND_MESSAGE, NULL, NULL, NULL);
            if (!h) return;
            RAWINPUTDEVICE rid;
            rid.usUsagePage = 0x01; // Generic desktop
            rid.usUsage = 0x06;     // Keyboard usage
            rid.dwFlags = RIDEV_INPUTSINK;
            rid.hwndTarget = h;
            if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
                std::cerr << "RegisterRawInputDevices failed\n";
            }
            MSG msg;
            while (running && GetMessageA(&msg, NULL, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            if (h) DestroyWindow(h);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        logger.log("Capture thread started.");
    }

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
        auto nowList = enumerateKeyboards();
        auto nowMap = makeMap(nowList);
        for (auto &p : nowMap) {
            if (known.find(p.first) == known.end()) {
                logger.log(std::string("[ADDED] ") + p.second.friendlyName + (p.second.vidpid.empty()?"":" ("+p.second.vidpid+")") + " -- " + p.second.devicePath);
            }
        }
        for (auto &p : known) {
            if (nowMap.find(p.first) == nowMap.end()) {
                logger.log(std::string("[REMOVED] ") + p.second.friendlyName + (p.second.vidpid.empty()?"":" ("+p.second.vidpid+")") + " -- " + p.second.devicePath);
            }
        }
        known.swap(nowMap);
    }

    logger.log("Shutting down...");
    if (msgThread.joinable()) {
        PostThreadMessageA(GetCurrentThreadId(), WM_QUIT, 0, 0);
        running = false;
        msgThread.join();
    }
    return 0;
}
