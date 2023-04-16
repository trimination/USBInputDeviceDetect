/**
 * Author:      Trimination/Tim T.
 * Date:        2023-16-04
 * Description: Detect when a particular USB device is connected/removed, or present at launch of program,
 *              based on product and vendor ID combo. This standalone program creates an unseen winapi window
 *              to perform its functionality; the purpose of the code is to be implemented within other projects
 *              that already have window creation and/or message event handling.
 *
 *              May require "user32" linking.
 */

#define ANSI
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT   0x0501
// GUID_DEVINTERFACE_HID
#define HID_CLASSGUID {0x4d1e55b2, 0xf16f, 0x11cf,{ 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}}
#define CLS_NAME "DTCT_CLASS"
#define HWND_MESSAGE ((HWND)-3)

#include <windows.h>
#include <winuser.h>
#include <initguid.h>
#include <usbiodef.h>
#include <Dbt.h>
#include <string>
#include <iostream>
#include <stdexcept>
#include <sstream>

bool InPidVidArrays(int pid, int vid);
int ParseDevName(std::string name, std::string prefix);
void HandleDeviceDetection();
void HandleDeviceRemoval();

/**
 * ID 0 and 1 = DS4 DualShock4 controller (tested)
 * ID 2 = Cronus Zen (based on usb device online data)
 * ID 3 = CronusMax (missing, not listed, do not own device).
 */
int vids[] = {1356, 1356, 8200};
int pids[] = {1356, 2508, 16};

// called at program launch to check if device(s) is/are connected
void initialCheck() {
    UINT nDevices = 0;
    GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST));

    // no devices
    if (nDevices < 1) {
        return;
    }

    // allocate memory for list
    PRAWINPUTDEVICELIST pRawInputDeviceList;
    pRawInputDeviceList = new RAWINPUTDEVICELIST[sizeof(RAWINPUTDEVICELIST) * nDevices];

    //  no memory for list, could not allocate
    if (pRawInputDeviceList == NULL) {
        return;
    }

    // populate device list buffer
    int nResult;
    nResult = GetRawInputDeviceList(pRawInputDeviceList, &nDevices, sizeof(RAWINPUTDEVICELIST));

    // couldn't get device list
    if (nResult < 0) {
        delete[] pRawInputDeviceList;
        return;
    }

    for (UINT i = 0; i < nDevices; i++) {
        UINT nBufferSize = 0;

        // get device name size
        nResult = GetRawInputDeviceInfo(pRawInputDeviceList[i].hDevice,
                                        RIDI_DEVICENAME,
                                        NULL,
                                        &nBufferSize);
        if (nResult < 0) {
            continue;
        }


        WCHAR *wcDeviceName = new WCHAR[nBufferSize + 1];

        // no device name size, go to next device in list
        if (wcDeviceName == NULL) {
            continue;
        }

        // get device name, seems to return empty on my system. idk why.
        nResult = GetRawInputDeviceInfo(pRawInputDeviceList[i].hDevice,
                                        RIDI_DEVICENAME,
                                        wcDeviceName,
                                        &nBufferSize);

        // no device name, go to next device in list
        if (nResult < 0) {
            delete[] wcDeviceName;
            continue;
        }

        RID_DEVICE_INFO rdiDeviceInfo;
        rdiDeviceInfo.cbSize = sizeof(RID_DEVICE_INFO);
        nBufferSize = rdiDeviceInfo.cbSize;

        nResult = GetRawInputDeviceInfo(pRawInputDeviceList[i].hDevice,
                                        RIDI_DEVICEINFO,
                                        &rdiDeviceInfo,
                                        &nBufferSize);

        if (nResult < 0) {
            continue;
        }

        if (rdiDeviceInfo.dwType == RIM_TYPEHID) {
            if (InPidVidArrays(rdiDeviceInfo.hid.dwProductId, rdiDeviceInfo.hid.dwVendorId)) {
                // handle connection of listed device at launch
                HandleDeviceDetection();
            }

        }
        delete[] wcDeviceName;
    }
    delete[] pRawInputDeviceList;
}

bool InPidVidArrays(const int pid, const int vid) {
    int pidsLen = sizeof(pids) / sizeof(*pids);
    int vidsLen = sizeof(vids) / sizeof(*vids);

    // redundancy check
    if (pidsLen != vidsLen) {
        throw std::runtime_error("PIDS and VIDS must have matching number of elements");
        exit(-1);
    }
    // compare given pid/vid to pid/vid arrays
    for (int i = 0; i < pidsLen; i++) {
        if (pids[i] == pid && vids[i] == vid)
            return true;
    }
    return false;
}

LRESULT message_handler(HWND__ *hwnd, UINT uint, WPARAM wparam, LPARAM lparam) {
    switch (uint) {
        case WM_NCCREATE:
            return true;
            break;

        case WM_CREATE: {
            DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
            ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
            NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
            NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
            memcpy(&(NotificationFilter.dbcc_classguid), &(GUID_DEVINTERFACE_USB_DEVICE), sizeof(struct _GUID));
            HDEVNOTIFY dev_notify = RegisterDeviceNotification(hwnd, &NotificationFilter,
                                                               DEVICE_NOTIFY_WINDOW_HANDLE);
            if (dev_notify == NULL) {
                throw std::runtime_error("Could not register for devicenotifications!");
            }
            break;
        }

        case WM_DEVICECHANGE: {
            PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR) lparam;
            PDEV_BROADCAST_DEVICEINTERFACE lpdbv = (PDEV_BROADCAST_DEVICEINTERFACE) lpdb;
            std::string path;
            if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                path = std::string(lpdbv->dbcc_name);

                switch (wparam) {
                    case DBT_DEVICEARRIVAL: {
                        int pid = ParseDevName(path, "PID_");
                        int vid = ParseDevName(path, "VID_");

                        // handle re-connection of listed device
                        if (InPidVidArrays(pid, vid)) {
                            HandleDeviceDetection();
                        }

                        break;
                    }
                    case DBT_DEVICEREMOVECOMPLETE:
                        int pid = ParseDevName(path, "PID_");
                        int vid = ParseDevName(path, "VID_");

                        // handle re-connection of listed device
                        if (InPidVidArrays(pid, vid)) {
                            HandleDeviceRemoval();
                        }
                        break;
                }
            }
            break;
        }

    }
    return 0L;
}

int main(int argc, char *argv[]) {
    initialCheck();

    HWND hWnd = NULL;
    WNDCLASSEX wx;
    ZeroMemory(&wx, sizeof(wx));

    wx.cbSize = sizeof(WNDCLASSEX);
    wx.lpfnWndProc = reinterpret_cast<WNDPROC>(message_handler);
    wx.hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandle(0));
    wx.style = CS_HREDRAW | CS_VREDRAW;
    wx.hInstance = GetModuleHandle(0);
    wx.hbrBackground = (HBRUSH) (COLOR_WINDOW);
    wx.lpszClassName = CLS_NAME;

    GUID guid = HID_CLASSGUID;

    if (RegisterClassEx(&wx)) {
        hWnd = CreateWindow(CLS_NAME, "DevNotifWnd", WS_ICONIC,
                            0, 0, CW_USEDEFAULT, 0, HWND_MESSAGE,
                            NULL, GetModuleHandle(0), (void *) &guid);
    }

    if (hWnd == NULL) {
        throw std::runtime_error("Could not create message window!");
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

int ParseDevName(std::string name, std::string prefix) {
    int id = -1;
    size_t idStart = name.find(prefix);
    if (idStart == std::string::npos) {
        return -1;
    }
    std::string idStr = std::string() + name[idStart + 4] + name[idStart + 5] + name[idStart + 6] + name[idStart + 7];
    if (!std::all_of(idStr.begin(), idStr.end(), ::isxdigit)) {
        // idStr is not valid hex (VID/PID)
        return -1;
    }

    std::stringstream ss;
    ss << std::hex << idStr;
    ss >> id;
    return id;
}

void HandleDeviceDetection() {
    std::cout << "Listed device detected" << std::endl;
}

void HandleDeviceRemoval() {
    std::cout << "Listed device removed" << std::endl;
}
