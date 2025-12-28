// src/monitor_mouse.cpp
// Small Windows CLI to monitor mouse attach/detach and capture raw mouse events.
// Features:
//  - detect mouse add/remove via Raw Input enumeration
//  - optional capture mode to receive WM_INPUT events
//  - optional JSON output and file logging (--output=path, --json)
//  - graceful shutdown on Ctrl+C

#define _WIN32_WINNT 0x0601
// Prevent Windows headers from defining min/max macros that break std::max/std::min
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

struct MouseDevice {
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

static std::vector<MouseDevice> enumerateMice() {
    std::vector<MouseDevice> res;
    UINT nDevices = 0;
    if (GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST)) != 0) {
        return res;
    }
    if (nDevices == 0) return res;
    std::vector<RAWINPUTDEVICELIST> devices(nDevices);
    if (GetRawInputDeviceList(devices.data(), &nDevices, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1) return res;

    for (UINT i = 0; i < nDevices; ++i) {
        if (devices[i].dwType != RIM_TYPEMOUSE) continue;
        UINT cb = 0;
        GetRawInputDeviceInfoA(devices[i].hDevice, RIDI_DEVICENAME, NULL, &cb);
        if (cb == 0) continue;
        std::vector<char> nameBuf(cb + 1);
        if (GetRawInputDeviceInfoA(devices[i].hDevice, RIDI_DEVICENAME, nameBuf.data(), &cb) == (UINT)-1) continue;
        std::string devicePath(nameBuf.data());
        std::string vidpid = extractVidPid(devicePath);
        std::string friendly = getFriendlyNameForDevicePath(devicePath);
        if (friendly.empty()) {
            friendly = devicePath;
        }
        res.push_back({devicePath, friendly, vidpid});
    }
    return res;
}

static std::map<std::string, MouseDevice> makeMap(const std::vector<MouseDevice>& list) {
    std::map<std::string, MouseDevice> m;
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
                for (char c : line) {
                    if (c == '"') j += '\\';
                    j.push_back(c);
                }
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

void printDevice(const MouseDevice &d, Logger &log, bool json) {
    std::ostringstream ss;
    if (!json) {
        ss << "[Mouse] " << d.friendlyName;
        if (!d.vidpid.empty()) ss << " (" << d.vidpid << ")";
        ss << " -- " << d.devicePath;
    } else {
        ss << "{ \"type\": \"mouse\", \"name\": \"";
        for (char c : d.friendlyName) { if (c=='"') ss << '\\'; ss << c; }
        ss << "\", \"vidpid\": \"" << d.vidpid << "\", \"path\": \"";
        for (char c : d.devicePath) { if (c=='"') ss << '\\'; ss << c; }
        ss << "\" }";
    }
    log.log(ss.str());
}

// Globals used by the capture message thread
static Logger *gLogger = nullptr;
static bool gJson = false;

// Window procedure used by the message thread (must use CALLBACK calling convention)
static LRESULT CALLBACK MonitorWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_INPUT) {
        UINT dwSize = 0;
        GetRawInputData((HRAWINPUT)l, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize > 0) {
            std::vector<BYTE> buf(dwSize);
            if (GetRawInputData((HRAWINPUT)l, RID_INPUT, buf.data(), &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
                RAWINPUT* raw = (RAWINPUT*)buf.data();
                if (raw->header.dwType == RIM_TYPEMOUSE) {
                    auto &m = raw->data.mouse;
                    std::ostringstream ss;
                    ss << "[INPUT] mouse: buttons=" << m.usButtonFlags << " dx=" << m.lLastX << " dy=" << m.lLastY;
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

int main(int argc, char **argv) {
    int pollMs = 1000;
    bool captureEvents = false;
    std::string outPath;
    bool json = false;
    for (int i=1;i<argc;i++){
        std::string a = argv[i];
        if (a.rfind("--poll=",0)==0) {
            pollMs = std::max(100, std::stoi(a.substr(7)));
        } else if (a == "--capture") {
            captureEvents = true;
        } else if (a.rfind("--output=",0)==0) {
            outPath = a.substr(9);
        } else if (a == "--json") {
            json = true;
        } else if (a == "--help" || a=="-h") {
            std::cout << "Usage: monitor_mouse [--poll=ms] [--capture] [--output=path] [--json]\n"
                      << "  --poll=ms   Poll interval in ms (default 1000)\n"
                      << "  --capture   Capture live mouse events (receive WM_INPUT)\n"
                      << "  --output=path  Append events to path (also printed to stdout)\n"
                      << "  --json      Emit JSON-ish log lines when --output is used\n";
            return 0;
        }
    }

    Logger logger;
    logger.setJson(json);
    if (!outPath.empty()) logger.openFile(outPath);

    SetConsoleCtrlHandler(consoleHandler, TRUE);

    logger.log(std::string("Monitoring mice every ") + std::to_string(pollMs) + " ms. Capture: " + (captureEvents?"ON":"OFF"));
    auto initial = enumerateMice();
    auto known = makeMap(initial);
    if (!known.empty()) {
        logger.log("Initial mice:");
        for (auto &p : known) printDevice(p.second, logger, json);
    } else {
        logger.log("No mice detected at startup.");
    }

    HWND hwnd = NULL;
    std::thread msgThread;

    if (captureEvents) {
        msgThread = std::thread([&logger,json](){
            // expose logger and json flag to the window procedure
            gLogger = &logger;
            gJson = json;
            WNDCLASSA wc = {};
            wc.lpfnWndProc = MonitorWndProc;
            wc.lpszClassName = "MonitorMouseMsgWnd";
            if (!RegisterClassA(&wc)) return;
            HWND h = CreateWindowA(wc.lpszClassName, "MonitorMouseHidden", 0, 0,0,0,0, HWND_MESSAGE, NULL, NULL, NULL);
            if (!h) return;
            RAWINPUTDEVICE rid;
            rid.usUsagePage = 0x01; // Generic desktop
            rid.usUsage = 0x02;     // Mouse
            rid.dwFlags = RIDEV_INPUTSINK; // receive even if not focused
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
        auto nowList = enumerateMice();
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
