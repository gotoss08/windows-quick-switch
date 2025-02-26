#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>

#include <iostream>
#include <shellscalingapi.h>

#define WM_TRAY_ICON (WM_USER + 1)

HHOOK keyboardHook = NULL;
HWND rememberedWindows[10] = {0};
std::vector<HWND> overlayWindows;
std::mutex overlayMutex;

const LPCWSTR notificationFont = L"Segoe UI";
const DWORD notificationFontSize = 42;
std::wstring notificationText;

std::wstring formatWString(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);

    // Determine the required buffer size
    int bufferSize = _vscwprintf(format, args) + 1; // +1 for null terminator

    if (bufferSize <= 0) {
        va_end(args);
        return L""; // Handle error, or throw exception
    }

    // Allocate buffer
    std::wstring result(bufferSize, L'\0');

    // Format the string
    _vsnwprintf_s(&result[0], bufferSize, bufferSize - 1, format, args);

    va_end(args);

    // Remove the null terminator
    return result.substr(0, bufferSize - 1);
}

std::wstring GetWindowTitle(HWND hwnd) {
    int length = GetWindowTextLengthW(hwnd); // Use GetWindowTextLengthW for Unicode
    if (length == 0) {
        return L""; // Empty title
    }

    std::wstring buffer(length + 1, L'\0'); // +1 for null terminator
    int result = GetWindowTextW(hwnd, &buffer[0], length + 1); // Use GetWindowTextW for Unicode

    if (result == 0) {
        return L""; // Error getting title
    }

    return buffer.substr(0, length); // Remove null terminator
}

BOOL SwitchToWindow(HWND hWnd) {
    if (!IsWindow(hWnd)) {
        std::cerr << "Invalid window handle." << std::endl;
        return FALSE;
    }

    // If the window is minimized, restore it.
    if (IsIconic(hWnd)) {
        ShowWindow(hWnd, SW_RESTORE);
    }

    // Get the current foreground window.
    HWND hForeground = GetForegroundWindow();
    DWORD dwForegroundThread = GetWindowThreadProcessId(hForeground, NULL);
    DWORD dwCurrentThread = GetCurrentThreadId();

    // Attach the input processing of the current thread to the foreground window's thread.
    BOOL bAttached = FALSE;
    if (dwCurrentThread != dwForegroundThread) {
        bAttached = AttachThreadInput(dwCurrentThread, dwForegroundThread, TRUE);
    }

    // Bring the window to the top and show it.
    SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BOOL bResult = SetForegroundWindow(hWnd);

    // Detach the input thread if we attached earlier.
    if (bAttached) {
        AttachThreadInput(dwCurrentThread, dwForegroundThread, FALSE);
    }

    return bResult;
}

void ShowOverlayNotificaiton(std::wstring text) {
    HDC hdc = GetDC(NULL);
    HFONT hFont = CreateFont(notificationFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, notificationFont);
    SelectObject(hdc, hFont);

    notificationText = text;
    SIZE textSize;
    GetTextExtentPoint32(hdc, notificationText.c_str(), (int)notificationText.length(), &textSize);

    ReleaseDC(NULL, hdc);
    DeleteObject(hFont);

    int padding = 10;
    int boxWidth = textSize.cx + 2 * padding;
    int boxHeight = textSize.cy + 2 * padding;

    // Top-left corner position
    int x = padding; // Add padding to the top-left corner
    int y = padding;

    HWND overlayWindow = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"WindowsQuickSwitchOverlayClass",
        L"Windows Quick Switch Overlay",
        WS_POPUP,
        x, y,
        boxWidth, boxHeight,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );
    if (overlayWindow != NULL) {
        SetLayeredWindowAttributes(overlayWindow, RGB(30, 30, 30), 240, LWA_ALPHA);
        ShowWindow(overlayWindow, SW_SHOW);
        InvalidateRect(overlayWindow, NULL, TRUE); //Force a paint.

        {
            std::lock_guard<std::mutex> lock(overlayMutex);
            std::for_each(overlayWindows.begin(), overlayWindows.end(), [](auto hwnd) {
                DestroyWindow(hwnd);
            });
            overlayWindows.clear();
            overlayWindows.push_back(overlayWindow);
        }

        std::thread([overlayWindow = overlayWindow]() {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            PostMessage(overlayWindow, WM_CLOSE, 0, 0);
        }).detach();
    }
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ToolbarWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void RegisterOverlayWindowClass(HINSTANCE hInstance) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = OverlayWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"WindowsQuickSwitchOverlayClass";
    RegisterClass(&wc);
}

void RegisterToolbarWindowClass(HINSTANCE hInstance) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = ToolbarWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"WindowsQuickSwitchToolbarClass";
    RegisterClass(&wc);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {

    // SetConsoleOutputCP(1251);
    // std::setlocale(LC_ALL, "");
    // std::wcout.imbue(std::locale(""));

    // SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    SetProcessDPIAware(); // mingw compiler compatibility

    RegisterOverlayWindowClass(hInstance);
    RegisterToolbarWindowClass(hInstance);

    keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);

    HWND toolbarWindow = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        L"WindowsQuickSwitchToolbarClass", // Class name
        L"Windows Quick Switch", // Window title (not visible)
        WS_POPUP, // Popup window style
        0, 0, // Position
        GetSystemMetrics(SM_CXSCREEN), // Width
        GetSystemMetrics(SM_CYSCREEN), // Height
        NULL, // Parent window
        NULL, // Menu
        GetModuleHandle(NULL), // Instance handle
        NULL // Additional application data
    );

    if (toolbarWindow == NULL) {
        MessageBox(
          NULL,
          L"Could not create tooltip window",
          L"Error",
          MB_OK | MB_ICONERROR
        );
        return 1;
    }

    NOTIFYICONDATA nid = { sizeof(nid) };
    nid.hWnd = toolbarWindow;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_ICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Windows Quick Switch");
    Shell_NotifyIcon(NIM_ADD, &nid);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    nid.uFlags = 0;
    Shell_NotifyIcon(NIM_DELETE, &nid);
    DestroyIcon(nid.hIcon);

    UnhookWindowsHookEx(keyboardHook);

    return 0;
}

LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        HBRUSH hBrush = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(hdc, &ps.rcPaint, hBrush);
        DeleteObject(hBrush);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));

        HFONT hFont = CreateFont(notificationFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, notificationFont);
        SelectObject(hdc, hFont);

        RECT rect;
        GetClientRect(hwnd, &rect);
        DrawText(hdc, notificationText.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        DeleteObject(hFont);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_CLOSE: {
        std::lock_guard<std::mutex> lock(overlayMutex);
        auto it = std::find(overlayWindows.begin(), overlayWindows.end(), hwnd);
        if (it != overlayWindows.end()) {
            overlayWindows.erase(it);
            DestroyWindow(hwnd);
        }
        break;
    }
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ToolbarWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_TRAY_ICON:
        if (lParam == WM_RBUTTONDOWN) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1001, L"Exit");
            SetForegroundWindow(hwnd);
            UINT clicked = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            if(clicked == 1001){
                PostQuitMessage(0);
            }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (GetAsyncKeyState(VK_LWIN) & 0x8000 || GetAsyncKeyState(VK_RWIN) & 0x8000) {
                if (pKeyboard->vkCode >= '1' && pKeyboard->vkCode <= '9') {
                    char windowIndex = pKeyboard->vkCode - 49;
                    bool shiftPressed = GetAsyncKeyState(VK_SHIFT) & 0x8000;

                    if (shiftPressed) {
                        HWND currentWindow = GetForegroundWindow();
                        rememberedWindows[windowIndex] = currentWindow;
                        std::cout << "remembered window: " << currentWindow << std::endl;
                        std::wstring windowTitle = GetWindowTitle(currentWindow);
                        ShowOverlayNotificaiton(formatWString(L"Window '%s' bound to WIN+%d", windowTitle.c_str(), windowIndex + 1));
                    } else {
                        HWND activeWindow = rememberedWindows[windowIndex];
                        std::cout << "switching to window: " << activeWindow << std::endl;
                        BOOL switched = SwitchToWindow(activeWindow);
                        if (switched) {
                            std::cout << "switched" << std::endl;
                        } else {
                            std::cout << "failed to switch window" << std::endl;
                        }
                    }

                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
}
