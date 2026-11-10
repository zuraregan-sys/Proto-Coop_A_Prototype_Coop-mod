#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "MinHook.h"

#pragma comment(lib, "ws2_32.lib")

// First prototype: install the transport/sync hooks, create a fake SyncOwner,
// start the engine's input sync on a UDP loopback channel, and exchange an
// 8-byte heartbeat through the engine's UpdateSync -> Channel::Receive path.

static FILE* g_log = nullptr;
static HMODULE g_engine = nullptr;
static unsigned int g_updateCalls = 0;
static unsigned int g_sendCalls = 0;
static unsigned int g_receiveCalls = 0;
static unsigned int g_applyCalls = 0;
static bool g_started = false;
static bool g_hooksReady = false;

struct FakeChannelManager { unsigned char pad[4]; };
struct FakeChannel { unsigned int pad; };
struct FakeNetSystem
{
    unsigned char pad[0x14];
    void* localAddress;
    unsigned char pad2[8];
    int target;
};
struct FakeSyncOwner
{
    unsigned char pad[0x20];
    unsigned int machines;
    unsigned int unknown24;
    void* session;
};

static FakeChannelManager g_fakeChannelManager;
static FakeChannel g_fakeChannel;
static FakeNetSystem g_fakeNetSystem;
static unsigned int g_fakeAddress;
static FakeSyncOwner g_fakeOwner;
static int g_peerMask = 2;

static SOCKET g_socket = INVALID_SOCKET;
static sockaddr_in g_peer;
static unsigned short g_localPort = 27016;
static unsigned short g_peerPort = 27017;
static bool g_isHost = true;
static unsigned int g_heartbeat = 0;

static void Log(const char* format, ...)
{
    if (g_log == nullptr) return;
    va_list args;
    va_start(args, format);
    vfprintf(g_log, format, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}

static void InitLog(HMODULE self)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    char* slash = strrchr(path, '\\');
    if (slash == nullptr) return;
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "coop.log");
    if (fopen_s(&g_log, path, "a") != 0) g_log = nullptr;
}

template <typename T>
static T At(unsigned int rva)
{
    return reinterpret_cast<T>(reinterpret_cast<unsigned char*>(g_engine) + rva);
}

using OpenFn = void* (__thiscall*)(void*, unsigned char, unsigned int, unsigned int);
using CloseFn = void (__thiscall*)(void*, void*);
using SendFn = void (__thiscall*)(void*, int, int, const void*, unsigned int, int);
using ReceiveFn = const char* (__thiscall*)(void*, char*, int, unsigned int*, unsigned char*, int**);
using StartSyncFn = void (__cdecl*)(void*, int, int);
using UpdateSyncFn = void (__cdecl*)();
using ApplySyncFn = void* (__cdecl*)(const unsigned int*, int, const void*);
using SendSyncMessageFn = void (__cdecl*)(int, const void*);
using GetPlayerObjectFn = void* (__cdecl*)(int);


static OpenFn g_open = nullptr;
static CloseFn g_close = nullptr;
static SendFn g_send = nullptr;
static ReceiveFn g_receive = nullptr;
static StartSyncFn g_startSync = nullptr;
static UpdateSyncFn g_updateSync = nullptr;
static ApplySyncFn g_applySync = nullptr;
static SendSyncMessageFn g_sendSyncMessage = nullptr;
static GetPlayerObjectFn g_getPlayerObject = nullptr;

static void __thiscall HookSend(void* self, int target, int type, const void* data, unsigned int length, int flag);

static void ReadConfig()
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(GetModuleHandleA("prototype_coop.asi"), path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    char* slash = strrchr(path, '\\');
    if (slash == nullptr) return;
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "coop.ini");

    char role[32] = "host";
    GetPrivateProfileStringA("coop", "role", "host", role, sizeof(role), path);
    g_isHost = (role[0] == 'h' || role[0] == 'H');
    g_localPort = (unsigned short)GetPrivateProfileIntA("coop", "port", g_isHost ? 27016 : 27017, path);
    g_peerPort = (unsigned short)GetPrivateProfileIntA("coop", "peer_port", g_isHost ? 27017 : 27016, path);
    g_peerMask = g_isHost ? 1 : 2;
    Log("config role=%s local=%u peer=%u mask=%d", g_isHost ? "host" : "client", g_localPort, g_peerPort, g_peerMask);
}

static bool InitSocket()
{
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        Log("WSAStartup failed");
        return false;
    }
    g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_socket == INVALID_SOCKET)
    {
        Log("socket failed: %d", WSAGetLastError());
        return false;
    }
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(g_localPort);
    if (bind(g_socket, (sockaddr*)&local, sizeof(local)) != 0)
    {
        Log("bind %u failed: %d", g_localPort, WSAGetLastError());
        return false;
    }
    u_long nonblocking = 1;
    ioctlsocket(g_socket, FIONBIO, &nonblocking);
    g_peer.sin_family = AF_INET;
    g_peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    g_peer.sin_port = htons(g_peerPort);
    Log("UDP ready local=%u peer=%u", g_localPort, g_peerPort);
    return true;
}

static void SendHeartbeat()
{
    unsigned char packet[8] = { 'P', 'C', 'O', 'P', 0, 0, 0, 0 };
    ++g_heartbeat;
    memcpy(packet + 4, &g_heartbeat, sizeof(g_heartbeat));
    void* netSystem = nullptr;
    if (g_engine != nullptr)
        netSystem = *(void**)(reinterpret_cast<unsigned char*>(g_engine) + 0x13187D4);
    if (g_sendSyncMessage != nullptr && netSystem != nullptr)
    {
        g_sendSyncMessage(0, packet);
        Log("heartbeat via SendSyncMessage %u", g_heartbeat);
        return;
    }
    int sent = sendto(g_socket, (const char*)packet, sizeof(packet), 0, (sockaddr*)&g_peer, sizeof(g_peer));
    if (sent == sizeof(packet))
        Log("heartbeat direct %u", g_heartbeat);
    else
        Log("heartbeat send failed: %d", WSAGetLastError());
}

static bool ReadPlayerPosition(float* x, float* y, float* z)
{
    if (g_getPlayerObject == nullptr || g_engine == nullptr) return false;
    void* player = g_getPlayerObject(0);
    if (player == nullptr) return false;
    void* transformAttribute = *(void**)(reinterpret_cast<unsigned char*>(player) + 0x1c);
    if (transformAttribute == nullptr) return false;
    void** vtable = *(void***)transformAttribute;
    if (vtable == nullptr || vtable[1] == nullptr) return false;
    using GetMatrixFn = void* (__thiscall*)(void*);
    void* matrix = reinterpret_cast<GetMatrixFn>(vtable[1])(transformAttribute);
    if (matrix == nullptr) return false;
    *x = *(float*)(reinterpret_cast<unsigned char*>(matrix) + 0x30);
    *y = *(float*)(reinterpret_cast<unsigned char*>(matrix) + 0x34);
    *z = *(float*)(reinterpret_cast<unsigned char*>(matrix) + 0x38);
    return true;
}

static void SendPlayerTransform()
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!ReadPlayerPosition(&x, &y, &z)) return;
    // UpdateSync receives at most 8 bytes; send a quantized position in one
    // SyncMessage-sized datagram: "P1" + three int16 coordinates (1/32 unit).
    unsigned char packet[8] = { 'P', '1', 0, 0, 0, 0, 0, 0 };
    int qx = (int)(x * 32.0f);
    int qy = (int)(y * 32.0f);
    int qz = (int)(z * 32.0f);
    if (qx > 32767) qx = 32767; if (qx < -32768) qx = -32768;
    if (qy > 32767) qy = 32767; if (qy < -32768) qy = -32768;
    if (qz > 32767) qz = 32767; if (qz < -32768) qz = -32768;
    short sx = (short)qx, sy = (short)qy, sz = (short)qz;
    memcpy(packet + 2, &sx, 2);
    memcpy(packet + 4, &sy, 2);
    memcpy(packet + 6, &sz, 2);
    Log("transform host %.2f %.2f %.2f -> %d %d %d", x, y, z, sx, sy, sz);
    HookSend(&g_fakeChannel, 0, 1, packet, sizeof(packet), 1);
}

static void* __thiscall HookOpen(void* self, unsigned char kind, unsigned int a, unsigned int b)
{
    if (self == &g_fakeChannelManager)
    {
        Log("fake ChannelManager::Open kind=%u a=%u b=%u", kind, a, b);
        return &g_fakeChannel;
    }
    Log("ChannelManager::Open self=%p kind=%u a=%u b=%u", self, kind, a, b);
    return g_open(self, kind, a, b);
}

static void __thiscall HookClose(void* self, void* channel)
{
    if (self == &g_fakeChannelManager || channel == &g_fakeChannel)
    {
        Log("fake ChannelManager::Close");
        return;
    }
    Log("ChannelManager::Close self=%p channel=%p", self, channel);
    g_close(self, channel);
}

static void __thiscall HookSend(void* self, int target, int type, const void* data, unsigned int length, int flag)
{
    if (self == &g_fakeChannel)
    {
        if (g_sendCalls++ < 40)
            Log("fake Channel::Send target=%d type=%d len=%u flag=%d", target, type, length, flag);
        if (g_socket != INVALID_SOCKET && data != nullptr)
            sendto(g_socket, (const char*)data, length, 0, (sockaddr*)&g_peer, sizeof(g_peer));
        return;
    }
    if (g_sendCalls < 40)
    {
        Log("Channel::Send self=%p target=%d type=%d data=%p len=%u flag=%d", self, target, type, data, length, flag);
        g_sendCalls++;
    }
    g_send(self, target, type, data, length, flag);
}

static const char* __thiscall HookReceive(void* self, char* buffer, int capacity, unsigned int* length, unsigned char* flag, int** source)
{
    if (self == &g_fakeChannel)
    {
        if (g_socket == INVALID_SOCKET) return nullptr;
        sockaddr_in from = {};
        int fromLen = sizeof(from);
        int received = recvfrom(g_socket, buffer, capacity, 0, (sockaddr*)&from, &fromLen);
        if (received <= 0) return nullptr;
        if (length) *length = (unsigned int)received;
        if (flag) *flag = 0;
        if (source) *source = &g_peerMask;
        if (g_receiveCalls++ < 40)
            Log("fake Channel::Receive len=%d from=%u", received, ntohs(from.sin_port));
        return buffer;
    }
    const char* result = g_receive(self, buffer, capacity, length, flag, source);
    if (result != nullptr && g_receiveCalls < 40)
    {
        Log("Channel::Receive self=%p len=%u flag=%u source=%p", self, length ? *length : 0, flag ? *flag : 0, source ? *source : nullptr);
        g_receiveCalls++;
    }
    return result;
}

static void __cdecl HookStartSync(void* owner, int a, int b)
{
    Log("StartSync owner=%p a=%d b=%d", owner, a, b);
    g_startSync(owner, a, b);
}

static void StartFakeSession()
{
    if (g_started || g_startSync == nullptr) return;
    if (!InitSocket()) return;

    memset(&g_fakeOwner, 0, sizeof(g_fakeOwner));
    g_fakeOwner.machines = 3;
    g_fakeOwner.session = &g_fakeChannelManager;
    memset(&g_fakeNetSystem, 0, sizeof(g_fakeNetSystem));
    g_fakeNetSystem.localAddress = &g_fakeAddress;
    g_fakeNetSystem.target = 0;
    *(void**)(reinterpret_cast<unsigned char*>(g_engine) + 0x13187D4) = &g_fakeNetSystem;
    g_started = true;
    Log("starting fake session");
    g_startSync(&g_fakeOwner, 0x2000, 100);
}

static void __cdecl HookUpdateSync()
{
    if (!g_started && g_updateCalls > 300)
        StartFakeSession();
    if (g_started && g_isHost && (g_updateCalls % 60) == 0)
    {
        SendHeartbeat();
        SendPlayerTransform();
    }
    if (g_getPlayerObject != nullptr && (g_updateCalls % 600) == 0)
    {
        void* player0 = g_getPlayerObject(0);
        void* player1 = g_getPlayerObject(1);
        Log("players p0=%p p1=%p handles=%08X/%08X", player0, player1,
            *(unsigned int*)(reinterpret_cast<unsigned char*>(g_engine) + 0x126E40C),
            *(unsigned int*)(reinterpret_cast<unsigned char*>(g_engine) + 0x126E410));
    }
    g_updateCalls++;
    g_updateSync();
}

static void* __cdecl HookApplySync(const unsigned int* machines, int type, const void* message)
{
    if (message != nullptr && memcmp(message, "PCOP", 4) == 0)
    {
        Log("heartbeat received from mask=%08X", machines ? *machines : 0);
        return nullptr;
    }
    if (message != nullptr && memcmp(message, "P1", 2) == 0)
    {
        const unsigned char* bytes = (const unsigned char*)message;
        short sx, sy, sz;
        memcpy(&sx, bytes + 2, 2);
        memcpy(&sy, bytes + 4, 2);
        memcpy(&sz, bytes + 6, 2);
        Log("transform client %.2f %.2f %.2f", sx / 32.0f, sy / 32.0f, sz / 32.0f);
        return nullptr;
    }
    if (g_applyCalls < 40)
    {
        unsigned int mask = machines ? *machines : 0;
        Log("ApplySyncMessage machines=%08X type=%d message=%p", mask, type, message);
        g_applyCalls++;
    }
    return g_applySync(machines, type, message);
}

static bool Install(const char* name, void* target, void* detour, void** original)
{
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK)
    {
        Log("MH_CreateHook %s failed: %d", name, (int)status);
        return false;
    }
    Log("hook %s target=%p", name, target);
    return true;
}

static void InstallHooks()
{
    if (g_hooksReady) return;
    g_engine = GetModuleHandleA("prototypeenginef.dll");
    if (g_engine == nullptr)
    {
        Log("prototypeenginef.dll not loaded yet");
        return;
    }
    Log("engine base=%p", g_engine);
    if (MH_Initialize() != MH_OK)
    {
        Log("MH_Initialize failed");
        return;
    }

    Install("ChannelManager::Open", At<void*>(0x008C31D0), (void*)&HookOpen, (void**)&g_open);
    Install("ChannelManager::Close", At<void*>(0x008C3670), (void*)&HookClose, (void**)&g_close);
    Install("Channel::Send", At<void*>(0x008C3F90), (void*)&HookSend, (void**)&g_send);
    Install("Channel::Receive", At<void*>(0x008C4030), (void*)&HookReceive, (void**)&g_receive);
    Install("StartSync", At<void*>(0x00833380), (void*)&HookStartSync, (void**)&g_startSync);
    Install("UpdateSync", At<void*>(0x008336F0), (void*)&HookUpdateSync, (void**)&g_updateSync);
    Install("ApplySyncMessage", At<void*>(0x00833450), (void*)&HookApplySync, (void**)&g_applySync);
    g_sendSyncMessage = At<SendSyncMessageFn>(0x00833790);
    g_getPlayerObject = At<GetPlayerObjectFn>(0x00099560);

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
        Log("MH_EnableHook failed");
    else
        Log("hooks enabled");
    g_hooksReady = true;
}

static DWORD WINAPI WaitForEngine(LPVOID)
{
    for (int i = 0; i < 300; ++i)
    {
        if (GetModuleHandleA("prototypeenginef.dll") != nullptr)
        {
            InstallHooks();
            return 0;
        }
        Sleep(100);
    }
    Log("timed out waiting for prototypeenginef.dll");
    return 0;
}

extern "C" __declspec(dllexport) void InitializeASI()
{
    InitLog(GetModuleHandleA("prototype_coop.asi"));
    Log("prototype_coop starting");
    ReadConfig();
    if (GetModuleHandleA("prototypeenginef.dll") != nullptr)
        InstallHooks();
    else
        CreateThread(nullptr, 0, WaitForEngine, nullptr, 0, nullptr);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}
