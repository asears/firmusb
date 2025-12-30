// src/monitor_usb.cpp
// Small Windows CLI to monitor USB device attach/detach.
// Features:
//  - detect USB device add/remove via SetupAPI enumeration
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

static const GUID GUID_DEVINTERFACE_USB_DEVICE =
{0xA5DCBF10, 0x6530, 0x11D2, {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED}};

struct USBDevice {
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

    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return "";

    SP_DEVICE_INTERFACE_DATA ifData;
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    DWORD idx = 0;
    char bufDetail[4096];

    while (SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVINTERFACE_USB_DEVICE, idx++, &ifData)) {
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

static std::vector<USBDevice> enumerateUSBDevices() {
    std::vector<USBDevice> res;
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return res;

    SP_DEVICE_INTERFACE_DATA ifData;
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    DWORD idx = 0;
    char bufDetail[4096];

    while (SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVINTERFACE_USB_DEVICE, idx++, &ifData)) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailA(hDevInfo, &ifData, NULL, 0, &required, NULL);
        if(required == 0 || required > sizeof(bufDetail)) continue;

        PSP_DEVICE_INTERFACE_DETAIL_DATA_A pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)bufDetail;
        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        SP_DEVINFO_DATA devInfo;
        devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
        if (SetupDiGetDeviceInterfaceDetailA(hDevInfo, &ifData, pDetail, required, NULL, &devInfo)) {
            std::string devicePath = pDetail->DevicePath;
            std::string vidpid = extractVidPid(devicePath);
            std::string friendly = getFriendlyNameForDevicePath(devicePath);
            if (friendly.empty()) {
                friendly = devicePath;
            }
            res.push_back({devicePath, friendly, vidpid});
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return res;
}

static std::map<std::string, USBDevice> makeMap(const std::vector<USBDevice>& list) {
    std::map<std::string, USBDevice> m;
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

void printDevice(const USBDevice &d, Logger &log, bool json) {
    std::ostringstream ss;
    if (!json) {
        ss << "[USB] " << d.friendlyName;
        if (!d.vidpid.empty()) ss << " (" << d.vidpid << ")";
        ss << " -- " << d.devicePath;
    } else {
        ss << "{ \"type\": \"usb\", \"name\": \"";
        for (char c : d.friendlyName) { if (c=='"') ss << '\\'; ss << c; }
        ss << "\", \"vidpid\": \"" << d.vidpid << "\", \"path\": \"";
        for (char c : d.devicePath) { if (c=='"') ss << '\\'; ss << c; }
        ss << "\" }";
    }
    log.log(ss.str());
}

int main(int argc, char **argv) {
    int pollMs = 1000;
    std::string outPath;
    bool json = false;
    for (int i=1;i<argc;i++){
        std::string a = argv[i];
        if (a.rfind("--poll=",0)==0) {
            pollMs = std::max(100, std::stoi(a.substr(7)));
        } else if (a.rfind("--output=",0)==0) {
            outPath = a.substr(9);
        } else if (a == "--json") {
            json = true;
        } else if (a == "--help" || a=="-h") {
            std::cout << "Usage: monitor_usb [--poll=ms] [--output=path] [--json]\n"
                      << "  --poll=ms   Poll interval in ms (default 1000)\n"
                      << "  --output=path  Append events to path (also printed to stdout)\n"
                      << "  --json      Emit JSON-ish log lines when --output is used\n";
            return 0;
        }
    }

    Logger logger;
    logger.setJson(json);
    if (!outPath.empty()) logger.openFile(outPath);

    SetConsoleCtrlHandler(consoleHandler, TRUE);

    logger.log(std::string("Monitoring USB devices every ") + std::to_string(pollMs) + " ms.");
    auto initial = enumerateUSBDevices();
    auto known = makeMap(initial);
    if (!known.empty()) {
        logger.log("Initial USB devices:");
        for (auto &p : known) printDevice(p.second, logger, json);
    } else {
        logger.log("No USB devices detected at startup.");
    }

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
        auto nowList = enumerateUSBDevices();
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
    return 0;
}