#include <windows.h>
#include <commctrl.h>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define ID_TIMER_EDIT      101
#define ID_START_BUTTON    102
#define ID_CANCEL_BUTTON   103
#define ID_PRESET_30       104
#define ID_PRESET_60       105
#define ID_PRESET_120      106

HWND hTimerEdit, hStartButton, hCancelButton;
int remainingSeconds = 0;

bool ShutdownComputerWithPrivilege()
{
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp = { 0 };

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);

    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);

    BOOL result = ExitWindowsEx(EWX_POWEROFF | EWX_FORCE,
        SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_OTHER);

    tkp.Privileges[0].Attributes = 0;
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);
    CloseHandle(hToken);

    return result != FALSE;
}

void SetTimerAndShowMessage(HWND hwnd, int minutes)
{
    if (minutes > 0)
    {
        remainingSeconds = minutes * 60;

        std::wstring msg = L"Компьютер будет выключен через " + std::to_wstring(minutes) + L" минут.";
        MessageBoxW(hwnd, msg.c_str(), L"Таймер установлен", MB_OK | MB_ICONINFORMATION);

        SetTimer(hwnd, 1, 1000, NULL);  // 1 секунда
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        CreateWindowW(L"STATIC", L"Время до выключения (минуты):",
            WS_VISIBLE | WS_CHILD, 10, 10, 220, 20, hwnd, NULL, NULL, NULL);

        hTimerEdit = CreateWindowW(L"EDIT", L"30",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER, 10, 35, 220, 25,
            hwnd, (HMENU)ID_TIMER_EDIT, NULL, NULL);

        CreateWindowW(L"BUTTON", L"30 мин",
            WS_VISIBLE | WS_CHILD, 10, 70, 70, 25,
            hwnd, (HMENU)ID_PRESET_30, NULL, NULL);

        CreateWindowW(L"BUTTON", L"60 мин",
            WS_VISIBLE | WS_CHILD, 90, 70, 70, 25,
            hwnd, (HMENU)ID_PRESET_60, NULL, NULL);

        CreateWindowW(L"BUTTON", L"120 мин",
            WS_VISIBLE | WS_CHILD, 170, 70, 70, 25,
            hwnd, (HMENU)ID_PRESET_120, NULL, NULL);

        hStartButton = CreateWindowW(L"BUTTON", L"Запустить",
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 10, 105, 110, 30,
            hwnd, (HMENU)ID_START_BUTTON, NULL, NULL);

        hCancelButton = CreateWindowW(L"BUTTON", L"Отменить",
            WS_VISIBLE | WS_CHILD, 130, 105, 110, 30,
            hwnd, (HMENU)ID_CANCEL_BUTTON, NULL, NULL);

        SendMessage(hTimerEdit, EM_SETLIMITTEXT, 5, 0);
        break;
    }
    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case ID_START_BUTTON:
        {
            wchar_t buffer[10];
            GetWindowText(hTimerEdit, buffer, 10);
            int minutes = _wtoi(buffer);
            KillTimer(hwnd, 1); // Сброс старого таймера
            SetTimerAndShowMessage(hwnd, minutes);
            break;
        }
        case ID_PRESET_30:
            KillTimer(hwnd, 1);
            SetTimerAndShowMessage(hwnd, 30);
            break;
        case ID_PRESET_60:
            KillTimer(hwnd, 1);
            SetTimerAndShowMessage(hwnd, 60);
            break;
        case ID_PRESET_120:
            KillTimer(hwnd, 1);
            SetTimerAndShowMessage(hwnd, 120);
            break;
        case ID_CANCEL_BUTTON:
            KillTimer(hwnd, 1);
            remainingSeconds = 0;
            SetWindowText(hTimerEdit, L"0");
            MessageBoxW(hwnd, L"Таймер отменен.", L"Информация", MB_OK | MB_ICONINFORMATION);
            break;
        }
        break;
    }
    case WM_TIMER:
    {
        if (remainingSeconds > 0)
        {
            remainingSeconds--;

            int minutes = remainingSeconds / 60;
            int seconds = remainingSeconds % 60;

            std::wstring timeStr = std::to_wstring(minutes) + L":" + (seconds < 10 ? L"0" : L"") + std::to_wstring(seconds);
            SetWindowText(hTimerEdit, timeStr.c_str());
        }
        else
        {
            KillTimer(hwnd, 1);
            if (!ShutdownComputerWithPrivilege())
            {
                MessageBoxW(hwnd, L"Не удалось выключить компьютер.", L"Ошибка", MB_OK | MB_ICONERROR);
            }
            DestroyWindow(hwnd);
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ShutdownTimerClass";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc))
        return -1;

    HWND hwnd = CreateWindowExW(0, L"ShutdownTimerClass", L"Таймер выключения ПК",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 260, 180, NULL, NULL, hInstance, NULL);

    if (!hwnd)
        return -1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
