#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// XInput 1.3 proxy: forwards everything to the system DLL and injects a
// scripted gamepad on user 0 so the menus can be driven without a physical pad.

struct XINPUT_GAMEPAD
{
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};

struct XINPUT_STATE
{
    DWORD dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
};

struct XINPUT_VIBRATION
{
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
};

struct XINPUT_CAPABILITIES
{
    BYTE Type;
    BYTE SubType;
    WORD Flags;
    XINPUT_GAMEPAD Gamepad;
    XINPUT_VIBRATION Vibration;
};

struct XINPUT_BATTERY_INFORMATION
{
    BYTE BatteryType;
    BYTE BatteryLevel;
};

struct XINPUT_KEYSTROKE
{
    WORD VirtualKey;
    WCHAR Unicode;
    WORD Flags;
    BYTE UserIndex;
    BYTE HidCode;
};

namespace
{
    HMODULE g_real = nullptr;
    FILE* g_log = nullptr;
    DWORD g_start = 0;
    DWORD g_packet = 0;
    bool g_movesForward = true;
    bool g_isClient = false;

    using GetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
    using SetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_VIBRATION*);
    using GetCapsFn = DWORD (WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    using EnableFn = void (WINAPI*)(BOOL);
    using GetBatteryFn = DWORD (WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
    using GetKeystrokeFn = DWORD (WINAPI*)(DWORD, XINPUT_KEYSTROKE*);
    using GetDSoundFn = DWORD (WINAPI*)(DWORD, GUID*, GUID*);

    GetStateFn realGetState = nullptr;
    SetStateFn realSetState = nullptr;
    GetCapsFn realGetCaps = nullptr;
    EnableFn realEnable = nullptr;
    GetBatteryFn realGetBattery = nullptr;
    GetKeystrokeFn realGetKeystroke = nullptr;
    GetDSoundFn realGetDSound = nullptr;

    void Log(const char* format, ...)
    {
        if (g_log == nullptr) return;
        va_list args;
        va_start(args, format);
        vfprintf(g_log, format, args);
        va_end(args);
        fputc('\n', g_log);
        fflush(g_log);
    }

    void InitLog(HMODULE self)
    {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return;
        char* slash = strrchr(path, '\\');
        if (slash == nullptr) return;
        strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "xinput_proxy.log");
        if (fopen_s(&g_log, path, "a") != 0) g_log = nullptr;
    }

    void LoadReal()
    {
        char path[MAX_PATH];
        UINT n = GetSystemWow64DirectoryA(path, MAX_PATH);
        if (n == 0)
            n = GetSystemDirectoryA(path, MAX_PATH);
        if (n == 0) return;
        strcat_s(path, MAX_PATH, "\\xinput1_3.dll");
        g_real = LoadLibraryA(path);
        if (g_real == nullptr)
        {
            Log("failed to load %s: %lu", path, GetLastError());
            return;
        }
        realGetState = (GetStateFn)GetProcAddress(g_real, "XInputGetState");
        realSetState = (SetStateFn)GetProcAddress(g_real, "XInputSetState");
        realGetCaps = (GetCapsFn)GetProcAddress(g_real, "XInputGetCapabilities");
        realEnable = (EnableFn)GetProcAddress(g_real, "XInputEnable");
        realGetBattery = (GetBatteryFn)GetProcAddress(g_real, "XInputGetBatteryInformation");
        realGetKeystroke = (GetKeystrokeFn)GetProcAddress(g_real, "XInputGetKeystroke");
        realGetDSound = (GetDSoundFn)GetProcAddress(g_real, "XInputGetDSoundAudioDeviceGuids");
        Log("loaded real xinput from %s", path);
    }

    HWND g_window = nullptr;
    DWORD g_lastFocus = 0;
    char g_statePath[MAX_PATH] = { 0 };
    DWORD g_stateChecked = 0;
    bool g_gameRunning = false;
    char g_startPath[MAX_PATH] = { 0 };
    DWORD g_startChecked = 0;
    bool g_startRequested = false;
    DWORD g_scriptStart = 0;
    bool g_scriptRunning = false;

    // The plugin writes "game" once a player object exists; until then the
    // scripted menu repeats, so a slow front end still gets driven.
    void InitStatePath(HMODULE self)
    {
        DWORD n = GetModuleFileNameA(self, g_statePath, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) { g_statePath[0] = 0; return; }
        char* slash = strrchr(g_statePath, '\\');
        if (slash == nullptr) { g_statePath[0] = 0; return; }
        strcpy_s(slash + 1, MAX_PATH - (slash + 1 - g_statePath), "coop_menu.txt");
        strcpy_s(g_startPath, MAX_PATH, g_statePath);
        char* startSlash = strrchr(g_startPath, '\\');
        if (startSlash != nullptr)
            strcpy_s(startSlash + 1, MAX_PATH - (startSlash + 1 - g_startPath), "coop_start.txt");
    }

    // The lobby's Start button writes coop_start.txt; until then the real device
    // is forwarded so the player can use the menu normally.
    bool StartRequested()
    {
        DWORD now = GetTickCount();
        if (now - g_startChecked < 500) return g_startRequested;
        g_startChecked = now;
        if (g_startPath[0] == 0) return false;
        FILE* file = nullptr;
        g_startRequested = (fopen_s(&file, g_startPath, "r") == 0 && file != nullptr);
        if (file != nullptr) fclose(file);
        return g_startRequested;
    }

    bool GameRunning()
    {
        DWORD now = GetTickCount();
        if (now - g_stateChecked < 1000) return g_gameRunning;
        g_stateChecked = now;
        if (g_statePath[0] == 0) return g_gameRunning;
        FILE* file = nullptr;
        if (fopen_s(&file, g_statePath, "r") == 0 && file != nullptr)
        {
            char text[16] = { 0 };
            fgets(text, sizeof(text), file);
            fclose(file);
            g_gameRunning = (text[0] == 'g' || text[0] == 'G');
        }
        return g_gameRunning;
    }

    BOOL CALLBACK FindMainWindow(HWND hwnd, LPARAM)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
        if (!IsWindowVisible(hwnd)) return TRUE;
        if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
        char cls[64] = { 0 };
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (strcmp(cls, "ProtoCoopLobby") == 0) return TRUE;
        g_window = hwnd;
        return FALSE;
    }

    // The front end only takes the scripted pad while its window is active.
    void FocusGame()
    {
        DWORD now = GetTickCount();
        if (now - g_lastFocus < 900) return;
        g_lastFocus = now;
        if (g_window == nullptr || !IsWindow(g_window)) EnumWindows(FindMainWindow, 0);
        if (g_window == nullptr) return;
        if (GetForegroundWindow() == g_window) return;
        DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        DWORD ownThread = GetCurrentThreadId();
        if (foregroundThread != 0 && foregroundThread != ownThread)
            AttachThreadInput(ownThread, foregroundThread, TRUE);
        SetForegroundWindow(g_window);
        SetWindowPos(g_window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        if (foregroundThread != 0 && foregroundThread != ownThread)
            AttachThreadInput(ownThread, foregroundThread, FALSE);
        Log("focused window %p", g_window);
    }

    WORD ButtonsAt(DWORD elapsedMs)
    {
        // Fast boot: args.txt skips the movies, legal screen and the loading
        // prompt, so the front end reaches the save list in a few seconds.
        if (elapsedMs >= 6000 && elapsedMs < 6120) return 0x0010;  // START
        if (elapsedMs >= 8000 && elapsedMs < 8120) return 0x1000;  // A (notice)
        if (elapsedMs >= 10000 && elapsedMs < 10120) return 0x1000; // A (notice)
        if (elapsedMs >= 12000 && elapsedMs < 12120) return 0x0002; // DPAD DOWN (CONTINUE)
        if (elapsedMs >= 14000 && elapsedMs < 14120) return 0x1000; // A (save list)
        if (elapsedMs >= 16000 && elapsedMs < 16120) return 0x1000; // A (load)
        if (elapsedMs >= 18000 && elapsedMs < 18120) return 0x1000; // A (confirm)
        if (elapsedMs >= 21000 && elapsedMs < 21120) return 0x1000; // A (info panel)
        return 0;
    }
}

extern "C" DWORD WINAPI XInputGetState(DWORD user, XINPUT_STATE* state)
{
    if (user == 0 && state != nullptr)
    {
        // Nothing is driven until the lobby starts; the player is in the menu.
        if (!StartRequested())
            return realGetState ? realGetState(user, state) : ERROR_DEVICE_NOT_CONNECTED;
        if (!g_scriptRunning)
        {
            g_scriptRunning = true;
            g_scriptStart = GetTickCount();
        }
        DWORD elapsed = GetTickCount() - g_scriptStart;
        if (GameRunning()) return realGetState ? realGetState(user, state) : ERROR_DEVICE_NOT_CONNECTED;
        // Repeat the whole sequence every 22 s while the game has not reached
        // gameplay, so a slow front end is still driven into the save list.
        DWORD scripted = elapsed % 22000;
        WORD buttons = ButtonsAt(scripted);
        if (buttons != 0 || scripted < 7000) FocusGame();
        memset(state, 0, sizeof(*state));
        state->dwPacketNumber = ++g_packet;
        state->Gamepad.wButtons = buttons;
        state->Gamepad.sThumbLX = 0;
        state->Gamepad.sThumbLY = 0;
        state->Gamepad.sThumbRX = 0;
        state->Gamepad.sThumbRY = 0;
        if (buttons != 0)
            Log("scripted buttons=%04X at %lu ms", buttons, scripted);
        return ERROR_SUCCESS;
    }
    return realGetState ? realGetState(user, state) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI XInputSetState(DWORD user, XINPUT_VIBRATION* vibration)
{
    if (user == 0) return ERROR_SUCCESS;
    return realSetState ? realSetState(user, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI XInputGetCapabilities(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps)
{
    if (user == 0 && caps != nullptr)
    {
        memset(caps, 0, sizeof(*caps));
        caps->Type = 1; // XINPUT_DEVTYPE_GAMEPAD
        caps->SubType = 1; // XINPUT_DEVSUBTYPE_GAMEPAD
        caps->Flags = 4; // XINPUT_CAPS_FFB_SUPPORTED
        caps->Gamepad.wButtons = 0xFFFF;
        caps->Gamepad.bLeftTrigger = 0xFF;
        caps->Gamepad.bRightTrigger = 0xFF;
        caps->Gamepad.sThumbLX = 0x7FFF;
        caps->Gamepad.sThumbLY = 0x7FFF;
        caps->Gamepad.sThumbRX = 0x7FFF;
        caps->Gamepad.sThumbRY = 0x7FFF;
        caps->Vibration.wLeftMotorSpeed = 0xFFFF;
        caps->Vibration.wRightMotorSpeed = 0xFFFF;
        return ERROR_SUCCESS;
    }
    return realGetCaps ? realGetCaps(user, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" void WINAPI XInputEnable(BOOL enable)
{
    if (realEnable) realEnable(enable);
}

extern "C" DWORD WINAPI XInputGetBatteryInformation(DWORD user, BYTE type, XINPUT_BATTERY_INFORMATION* battery)
{
    if (user == 0 && battery != nullptr)
    {
        battery->BatteryType = 1; // BATTERY_TYPE_WIRED
        battery->BatteryLevel = 4; // BATTERY_LEVEL_FULL
        return ERROR_SUCCESS;
    }
    return realGetBattery ? realGetBattery(user, type, battery) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI XInputGetKeystroke(DWORD user, XINPUT_KEYSTROKE* keystroke)
{
    if (user == 0) return ERROR_EMPTY;
    return realGetKeystroke ? realGetKeystroke(user, keystroke) : ERROR_EMPTY;
}

extern "C" DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD user, GUID* render, GUID* capture)
{
    if (user == 0)
    {
        if (render) memset(render, 0, sizeof(GUID));
        if (capture) memset(capture, 0, sizeof(GUID));
        return ERROR_SUCCESS;
    }
    return realGetDSound ? realGetDSound(user, render, capture) : ERROR_DEVICE_NOT_CONNECTED;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        g_start = GetTickCount();
        char selfPath[MAX_PATH];
        if (GetModuleFileNameA(module, selfPath, MAX_PATH) != 0)
        {
            g_movesForward = strstr(selfPath, "TestInstances\\1\\") == nullptr;
        }
        g_isClient = !g_movesForward;
        InitLog(module);
        InitStatePath(module);
        Log("xinput proxy attached");
        LoadReal();
    }
    return TRUE;
}
