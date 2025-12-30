// src/usb_debug.cpp
// USB Descriptor Debugger - Enumerates USB hubs and dumps device/configuration descriptors
// Useful for debugging invalid USB configuration descriptors

#define _WIN32_WINNT 0x0601
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#include <usbioctl.h>
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>

#pragma comment(lib, "setupapi.lib")

static const GUID GUID_DEVINTERFACE_USB_HUB =
{0xf18a0e88, 0xc30c, 0x11d0, {0x88, 0x15, 0x00, 0xa0, 0xc9, 0x06, 0xbe, 0xd8}};

struct USBHub {
    std::string devicePath;
    std::string friendlyName;
};

static std::vector<USBHub> enumerateUSBHubs() {
    std::vector<USBHub> res;
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_USB_HUB, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return res;

    SP_DEVICE_INTERFACE_DATA ifData;
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    DWORD idx = 0;
    char bufDetail[4096];

    while (SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVINTERFACE_USB_HUB, idx++, &ifData)) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailA(hDevInfo, &ifData, NULL, 0, &required, NULL);
        if(required == 0 || required > sizeof(bufDetail)) continue;

        PSP_DEVICE_INTERFACE_DETAIL_DATA_A pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)bufDetail;
        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        SP_DEVINFO_DATA devInfo;
        devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
        if (SetupDiGetDeviceInterfaceDetailA(hDevInfo, &ifData, pDetail, required, NULL, &devInfo)) {
            std::string devicePath = pDetail->DevicePath;
            char propBuf[512];
            std::string friendly;
            if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfo, SPDRP_FRIENDLYNAME, NULL, (PBYTE)propBuf, sizeof(propBuf), NULL)) {
                friendly = std::string(propBuf);
            } else {
                friendly = devicePath;
            }
            res.push_back({devicePath, friendly});
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return res;
}

void dumpDescriptor(const std::string& desc, const unsigned char* data, size_t len) {
    std::cout << desc << " (" << len << " bytes):" << std::endl;
    for (size_t i = 0; i < len; ++i) {
        if (i % 16 == 0) std::cout << std::hex << std::setfill('0') << std::setw(4) << i << ": ";
        std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)data[i] << " ";
        if ((i + 1) % 16 == 0) std::cout << std::endl;
    }
    if (len % 16 != 0) std::cout << std::endl;
    std::cout << std::dec;
}

int main() {
    std::cout << "USB Descriptor Debugger" << std::endl;
    std::cout << "Enumerating USB hubs..." << std::endl;

    auto hubs = enumerateUSBHubs();
    if (hubs.empty()) {
        std::cout << "No USB hubs found." << std::endl;
        return 1;
    }

    for (const auto& hub : hubs) {
        std::cout << "Hub: " << hub.friendlyName << std::endl;
        std::cout << "Path: " << hub.devicePath << std::endl;

        HANDLE hHub = CreateFileA(hub.devicePath.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hHub == INVALID_HANDLE_VALUE) {
            std::cout << "Failed to open hub: " << GetLastError() << std::endl;
            continue;
        }

        // Get hub info
        USB_NODE_INFORMATION nodeInfo;
        DWORD bytesReturned;
        if (DeviceIoControl(hHub, IOCTL_USB_GET_NODE_INFORMATION, &nodeInfo, sizeof(nodeInfo), &nodeInfo, sizeof(nodeInfo), &bytesReturned, NULL)) {
            std::cout << "Hub has " << (int)nodeInfo.u.HubInformation.HubDescriptor.bNumberOfPorts << " ports." << std::endl;

            for (UCHAR port = 1; port <= nodeInfo.u.HubInformation.HubDescriptor.bNumberOfPorts; ++port) {
                USB_NODE_CONNECTION_INFORMATION_EX connInfo;
                connInfo.ConnectionIndex = port;
                if (DeviceIoControl(hHub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, &connInfo, sizeof(connInfo), &connInfo, sizeof(connInfo), &bytesReturned, NULL)) {
                    if (connInfo.ConnectionStatus == DeviceConnected) {
                        std::cout << "Port " << (int)port << ": Device connected" << std::endl;
                        std::cout << "  Device is " << (connInfo.DeviceIsHub ? "a hub" : "a device") << std::endl;
                        std::cout << "  Speed: ";
                        switch (connInfo.Speed) {
                        case UsbLowSpeed: std::cout << "Low"; break;
                        case UsbFullSpeed: std::cout << "Full"; break;
                        case UsbHighSpeed: std::cout << "High"; break;
                        case UsbSuperSpeed: std::cout << "Super"; break;
                        default: std::cout << "Unknown";
                        }
                        std::cout << std::endl;

                        // Get device descriptor
                        char buffer[sizeof(USB_DESCRIPTOR_REQUEST) + 4096];
                        USB_DESCRIPTOR_REQUEST* descReq = (USB_DESCRIPTOR_REQUEST*)buffer;
                        memset(buffer, 0, sizeof(buffer));
                        descReq->ConnectionIndex = port;
                        descReq->SetupPacket.wValue = (USB_DEVICE_DESCRIPTOR_TYPE << 8);
                        descReq->SetupPacket.wIndex = 0;
                        descReq->SetupPacket.wLength = sizeof(USB_DEVICE_DESCRIPTOR);
                        descReq->SetupPacket.bmRequest = 0x80; // IN
                        descReq->SetupPacket.bRequest = USB_REQUEST_GET_DESCRIPTOR;

                        if (DeviceIoControl(hHub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, descReq, sizeof(buffer), descReq, sizeof(buffer), &bytesReturned, NULL)) {
                            USB_DEVICE_DESCRIPTOR* devDesc = (USB_DEVICE_DESCRIPTOR*)descReq->Data;
                            dumpDescriptor("Device Descriptor", (unsigned char*)devDesc, sizeof(USB_DEVICE_DESCRIPTOR));
                        } else {
                            std::cout << "Failed to get device descriptor: " << GetLastError() << std::endl;
                        }

                        // Get configuration descriptor
                        memset(buffer, 0, sizeof(buffer));
                        descReq->ConnectionIndex = port;
                        descReq->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8);
                        descReq->SetupPacket.wIndex = 0;
                        descReq->SetupPacket.wLength = sizeof(USB_CONFIGURATION_DESCRIPTOR);
                        descReq->SetupPacket.bmRequest = 0x80; // IN
                        descReq->SetupPacket.bRequest = USB_REQUEST_GET_DESCRIPTOR;

                        if (DeviceIoControl(hHub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, descReq, sizeof(buffer), descReq, sizeof(buffer), &bytesReturned, NULL)) {
                            USB_CONFIGURATION_DESCRIPTOR* configDesc = (USB_CONFIGURATION_DESCRIPTOR*)descReq->Data;
                            dumpDescriptor("Configuration Descriptor", (unsigned char*)configDesc, sizeof(USB_CONFIGURATION_DESCRIPTOR));

                            if (configDesc->bDescriptorType != USB_CONFIGURATION_DESCRIPTOR_TYPE) {
                                std::cout << "WARNING: Invalid configuration descriptor type! Got " << (int)configDesc->bDescriptorType << std::endl;
                            }
                            if (configDesc->wTotalLength < sizeof(USB_CONFIGURATION_DESCRIPTOR)) {
                                std::cout << "WARNING: Invalid total length! Got " << configDesc->wTotalLength << std::endl;
                            }
                        } else {
                            std::cout << "Failed to get configuration descriptor: " << GetLastError() << std::endl;
                        }

                    } else {
                        std::cout << "Port " << (int)port << ": No device connected" << std::endl;
                    }
                } else {
                    std::cout << "Failed to get port " << (int)port << " info: " << GetLastError() << std::endl;
                }
            }
        } else {
            std::cout << "Failed to get hub info: " << GetLastError() << std::endl;
        }

        CloseHandle(hHub);
        std::cout << std::endl;
    }

    return 0;
}