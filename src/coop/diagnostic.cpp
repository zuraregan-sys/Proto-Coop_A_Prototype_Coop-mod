#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
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
static bool g_transportReady = false;
static void* g_controllerUpdate = nullptr;
static DWORD g_lastTick = 0;
static unsigned int g_recvCount = 0;
static void FrameTick();
static bool Plausible(const void* pointer);
static unsigned int ReadU32(const void* base, unsigned int offset);
static unsigned int GetAttributeValue(void* object, unsigned int vtable);
static sockaddr_in g_peer;
static unsigned short g_localPort = 27016;
static unsigned short g_peerPort = 27017;
static bool g_isHost = true;
static int g_testMove = 0;
static bool g_testMoved = false;
static float g_testOffsetY = 0.0f;
static int g_puppetLocal = 0;
static int g_testWalk = 0;
static int g_testWalkLeft = 0;
static int g_puppetLog = 0;
static bool g_followLog = false;
static bool g_debugIntention = false;
static bool g_logSpawns = false;
static float g_intentTest = 0.0f;
static bool g_fakeSession = false;
static bool g_puppetStatic = false;
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
static void* g_findTemplate = nullptr;
static void* g_createObject = nullptr;
static void* g_setPosition = nullptr;
static void* g_getHandle = nullptr;
static void* g_nameCtor = nullptr;
static void* g_findAttribute = nullptr;
static void* g_getText = nullptr;
static bool g_spawnPending = false;
static bool g_puppetSpawned = false;
static bool g_attrDumped = false;
static void* g_puppetObject = nullptr;
static unsigned int g_puppetHandle = 0xFFFF;
static unsigned int g_effectSendCount = 0;
static bool g_remoteValid = false;
static float g_puppetX = 0.0f, g_puppetY = 0.0f, g_puppetZ = 0.0f;
static bool g_puppetPlaced = false;
static float g_remoteX = 0.0f, g_remoteY = 0.0f, g_remoteZ = 0.0f;

static void __attribute__((force_align_arg_pointer)) __thiscall HookSend(void* self, int target, int type, const void* data, unsigned int length, int flag);

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
    g_testMove = GetPrivateProfileIntA("coop", "test_move", 0, path);
    g_testOffsetY = (float)GetPrivateProfileIntA("coop", "test_offset_y", 0, path);
    g_puppetLocal = GetPrivateProfileIntA("coop", "puppet_local", 0, path);
    g_testWalk = GetPrivateProfileIntA("coop", "test_walk", 0, path);
    g_followLog = GetPrivateProfileIntA("coop", "follow_log", 0, path) != 0;
    g_debugIntention = GetPrivateProfileIntA("coop", "debug_intention", 0, path) != 0;
    g_logSpawns = GetPrivateProfileIntA("coop", "log_spawns", 0, path) != 0;
    g_intentTest = (float)GetPrivateProfileIntA("coop", "intent_test", 0, path);
    g_fakeSession = GetPrivateProfileIntA("coop", "fake_session", 0, path) != 0;
    g_puppetStatic = GetPrivateProfileIntA("coop", "puppet_static", 0, path) != 0;
    Log("config role=%s local=%u peer=%u mask=%d offset_y=%.0f", g_isHost ? "host" : "client", g_localPort,
        g_peerPort, g_peerMask, g_testOffsetY);
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
    int sent = sendto(g_socket, (const char*)packet, sizeof(packet), 0, (sockaddr*)&g_peer, sizeof(g_peer));
    if (sent == sizeof(packet))
        Log("heartbeat %u", g_heartbeat);
    else
        Log("heartbeat send failed: %d", WSAGetLastError());
}

static void* ReadObjectMatrix(void* object)
{
    if (object == nullptr) return nullptr;
    void* transformAttribute = *(void**)(reinterpret_cast<unsigned char*>(object) + 0x1c);
    if (transformAttribute == nullptr) return nullptr;
    void** vtable = *(void***)transformAttribute;
    if (vtable == nullptr || vtable[1] == nullptr) return nullptr;
    using GetMatrixFn = void* (__thiscall*)(void*);
    return reinterpret_cast<GetMatrixFn>(vtable[1])(transformAttribute);
}

// The matrix lives inline after the attribute's vtable; reading it here keeps
// the update path free of one virtual call per frame.
static const float* ReadInlineMatrix(void* object)
{
    if (object == nullptr) return nullptr;
    void* transformAttribute = *(void**)(reinterpret_cast<unsigned char*>(object) + 0x1c);
    if (transformAttribute == nullptr) return nullptr;
    return reinterpret_cast<const float*>(reinterpret_cast<unsigned char*>(transformAttribute) + 4);
}

static bool ReadObjectPosition(void* player, float* x, float* y, float* z)
{
    const float* matrix = ReadInlineMatrix(player);
    if (matrix == nullptr) return false;
    *x = matrix[12];
    *y = matrix[13];
    *z = matrix[14];
    return true;
}

static bool ReadPlayerPosition(float* x, float* y, float* z)
{
    if (g_getPlayerObject == nullptr || g_engine == nullptr) return false;
    return ReadObjectPosition(g_getPlayerObject(0), x, y, z);
}

static void SetObjectPosition(void* object, float x, float y, float z)
{
    if (object == nullptr || g_setPosition == nullptr) return;
    float position[3] = { x, y, z };
    reinterpret_cast<void (__thiscall*)(void*, const float*)>(g_setPosition)(object, position);
}

static unsigned char g_poseBuffer[128 * 32];
static unsigned char g_poseDisplay[128 * 32];
static unsigned short g_poseJoints = 0;
static bool g_poseValid = false;
static bool g_poseDisplayReady = false;
static unsigned int g_poseApplied = 0;
static bool g_poseSendLogged = false;
static bool g_poseEnterLogged = false;

// Streams the local player's joint transforms (quaternion + translation per
// joint) so the remote avatar can be drawn with its owner's animation.
static float g_remoteBasis[12];
static bool g_remoteBasisValid = false;

// Sends the player's 3x3 basis (with padding) so the remote avatar turns the
// same way the player does; the pose alone does not carry the object rotation.
static void SendRotation()
{
    void* player = g_getPlayerObject ? g_getPlayerObject(0) : nullptr;
    if (player == nullptr || g_socket == INVALID_SOCKET) return;
    const float* matrix = ReadInlineMatrix(player);
    if (matrix == nullptr) return;
    unsigned char packet[4 + 12 * 4];
    packet[0] = 'R';
    packet[1] = '1';
    packet[2] = 0;
    packet[3] = 0;
    memcpy(packet + 4, matrix, 12 * 4);
    sendto(g_socket, (const char*)packet, sizeof(packet), 0, (sockaddr*)&g_peer, sizeof(g_peer));
}

static void StoreRotation(const unsigned char* data, int length)
{
    if (length < 4 + 12 * 4) return;
    memcpy(g_remoteBasis, data + 4, sizeof(g_remoteBasis));
    if (!g_remoteBasisValid) Log("rotation stored");
    g_remoteBasisValid = true;
}

static void SendPose()
{
    void* player = g_getPlayerObject ? g_getPlayerObject(0) : nullptr;
    if (!g_poseEnterLogged) { g_poseEnterLogged = true; Log("pose enter: player=%p socket=%d", player, (int)g_socket); }
    if (player == nullptr || g_socket == INVALID_SOCKET) return;
    unsigned int pose = GetAttributeValue(player, 0x10E80688);
    if (!Plausible(reinterpret_cast<void*>(pose))) return;
    unsigned int joints = ReadU32(reinterpret_cast<void*>(pose), 0x14);
    unsigned int rotations = ReadU32(reinterpret_cast<void*>(pose), 0x18);
    unsigned int translations = ReadU32(reinterpret_cast<void*>(pose), 0x1C);
    if (!g_poseSendLogged)
    {
        g_poseSendLogged = true;
        Log("pose send: pose=%08X joints=%u rot=%08X trans=%08X", pose, joints, rotations, translations);
    }
    if (joints == 0 || joints > 128 || !Plausible(reinterpret_cast<void*>(rotations)) ||
        !Plausible(reinterpret_cast<void*>(translations)))
        return;
    static unsigned char packet[8 + 128 * 32];
    packet[0] = 'A';
    packet[1] = '1';
    memcpy(packet + 2, &joints, 2);
    for (unsigned int i = 0; i < joints; ++i)
    {
        memcpy(packet + 4 + i * 32, reinterpret_cast<void*>(rotations + i * 16), 16);
        memcpy(packet + 4 + i * 32 + 16, reinterpret_cast<void*>(translations + i * 16), 16);
    }
    sendto(g_socket, (const char*)packet, 4 + (int)joints * 32, 0, (sockaddr*)&g_peer, sizeof(g_peer));
}

static void StorePose(const unsigned char* data, int length)
{
    if (length < 4) return;
    unsigned short joints = 0;
    memcpy(&joints, data + 2, 2);
    if (joints == 0 || joints > 128 || length < 4 + (int)joints * 32) return;
    memcpy(g_poseBuffer, data + 4, (size_t)joints * 32);
    g_poseJoints = joints;
    if (!g_poseValid)
    {
        memcpy(g_poseDisplay, g_poseBuffer, (size_t)joints * 32);
        g_poseDisplayReady = true;
        Log("pose stored: %u joints", joints);
    }
    g_poseValid = true;
}

static void SendPlayerTransform()
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!ReadPlayerPosition(&x, &y, &z)) return;
    unsigned char packet[8] = { 'P', '1', 0, 0, 0, 0, 0, 0 };
    if (g_testOffsetY != 0.0f && (g_updateCalls % 600) == 0)
        Log("send: offset_y=%.1f before=%.2f", g_testOffsetY, y);
    y += g_testOffsetY;
    // 1/8 unit steps: +-4096 units, enough for the whole city, 12 cm accuracy.
    int qx = (int)(x * 8.0f);
    int qy = (int)(y * 8.0f);
    int qz = (int)(z * 8.0f);
    if (qx > 32767) qx = 32767; if (qx < -32768) qx = -32768;
    if (qy > 32767) qy = 32767; if (qy < -32768) qy = -32768;
    if (qz > 32767) qz = 32767; if (qz < -32768) qz = -32768;
    short sx = (short)qx, sy = (short)qy, sz = (short)qz;
    memcpy(packet + 2, &sx, 2);
    memcpy(packet + 4, &sy, 2);
    memcpy(packet + 6, &sz, 2);
    Log("transform send(%s) %.2f %.2f %.2f -> %d %d %d", g_isHost ? "host" : "client", x, y, z, sx, sy, sz);
    sendto(g_socket, (const char*)packet, sizeof(packet), 0, (sockaddr*)&g_peer, sizeof(g_peer));
}

static const char* NameText(const void* name)
{
    using GetTextFn = const char* (__thiscall*)(const void*);
    if (name == nullptr || g_getText == nullptr) return "(?)";
    const char* text = reinterpret_cast<GetTextFn>(g_getText)(name);
    if (text == nullptr) return "(null)";
    if (strcmp(text, "?Name?") == 0) return "(hash-only)";
    return text;
}

// Walks the attribute lists of a game object: the static one at +8..+C and the
// dynamic one behind +0x18. Entries are 16 bytes: name key then attribute pointer.
static void DumpAttributes(void* object)
{
    if (object == nullptr) return;
    unsigned char* bytes = reinterpret_cast<unsigned char*>(object);
    unsigned int begin = *(unsigned int*)(bytes + 8);
    unsigned int end = *(unsigned int*)(bytes + 0xC);
    unsigned int count = (end - begin) / 16;
    Log("attrs: static begin=%08X end=%08X count=%u", begin, end, count);
    for (unsigned int i = 0; i < count && i < 256; ++i)
    {
        unsigned char* entry = reinterpret_cast<unsigned char*>(begin) + i * 16;
        void* attribute = *(void**)(entry + 8);
        Log("attrs: [%u] key='%s' hash=%08X:%08X attr=%p vtable=%08X", i, NameText(entry),
            *(unsigned int*)(entry + 4), *(unsigned int*)entry, attribute,
            attribute != nullptr ? *(unsigned int*)attribute : 0);
        if (attribute != nullptr)
            Log("attrs:     value0='%s' value1='%s'", NameText(reinterpret_cast<unsigned char*>(attribute) + 4),
                NameText(reinterpret_cast<unsigned char*>(attribute) + 0xC));
    }
    unsigned int dynamicList = *(unsigned int*)(bytes + 0x18);
    void* viaVtable = ReadObjectMatrix(object);
    const float* inlineMatrix = ReadInlineMatrix(object);
    Log("matrix: vtable=%p inline=%p same=%d", viaVtable, reinterpret_cast<const void*>(inlineMatrix),
        viaVtable == reinterpret_cast<const void*>(inlineMatrix));
    if (dynamicList == 0)
    {
        Log("attrs: no dynamic list");
        return;
    }
    unsigned int dynBegin = *(unsigned int*)dynamicList;
    unsigned int dynEnd = *(unsigned int*)(dynamicList + 4);
    Log("attrs: dynamic list=%08X begin=%08X end=%08X count=%u", dynamicList, dynBegin, dynEnd,
        (dynEnd - dynBegin) / 16);
    for (unsigned int i = 0; i < (dynEnd - dynBegin) / 16 && i < 256; ++i)
    {
        unsigned char* entry = reinterpret_cast<unsigned char*>(dynBegin) + i * 16;
        void* attribute = *(void**)(entry + 8);
        Log("attrs: d[%u] key='%s' vtable=%08X", i, NameText(entry),
            attribute != nullptr ? *(unsigned int*)attribute : 0);
        if (attribute != nullptr)
            Log("attrs:     value0='%s'", NameText(reinterpret_cast<unsigned char*>(attribute) + 4));
    }
}

static bool Plausible(const void* pointer)
{
    unsigned int value = reinterpret_cast<unsigned int>(pointer);
    return value >= 0x10000 && value < 0x7FFF0000;
}

static unsigned int ReadU32(const void* base, unsigned int offset)
{
    const unsigned char* address = reinterpret_cast<const unsigned char*>(base) + offset;
    if (!Plausible(base) || !Plausible(address)) return 0;
    return *(const unsigned int*)address;
}

static void* ReadPtr(const void* base, unsigned int offset)
{
    return reinterpret_cast<void*>(ReadU32(base, offset));
}

static void* g_playerTemplate = nullptr;

// Every game object the game spawns passes through the static
// GameObjectSpawner::Create; remembering which template made which object
// tells us the player's own template without ever resolving its name.
struct SpawnRecord
{
    void* object;
    void* tmpl;
};

static SpawnRecord g_spawns[4096];
static volatile long g_spawnCount = 0;

static void LookupPlayerTemplate(void* player)
{
    long count = g_spawnCount;
    if (count > 4096) count = 4096;
    for (long i = 0; i < count; ++i)
    {
        if (g_spawns[i].object == player)
        {
            g_playerTemplate = g_spawns[i].tmpl;
            Log("tmpl: player %p is spawn %ld template=%p name=%08X%08X", player, i, g_playerTemplate,
                ReadU32(g_playerTemplate, 0x14), ReadU32(g_playerTemplate, 0x10));
            return;
        }
    }
    Log("tmpl: player %p not in %ld spawns", player, count);
}

// Looks among the objects the mission already spawned for one whose template
// carries that name hash, so the puppet can reuse a template known to render.
static void* FindSpawnedTemplateByHash(unsigned int hash)
{
    long count = g_spawnCount;
    if (count > 4096) count = 4096;
    for (long i = 0; i < count; ++i)
    {
        void* tmpl = g_spawns[i].tmpl;
        if (tmpl == nullptr) continue;
        if (ReadU32(tmpl, 0x10) == hash)
            return tmpl;
    }
    return nullptr;
}

// Same, but over every inventory the root holds: templates the mission has not
// spawned yet only live there.
static void* FindInventoryTemplateByHash(unsigned int hash)
{
    void* root = ReadPtr(g_engine, 0x1283818);
    if (!Plausible(root)) return nullptr;
    unsigned int inventoryVtable = ReadU32(root, 0);
    void* inventories[160];
    int inventoryCount = 0;
    inventories[inventoryCount++] = root;
    const unsigned int casterVtable = 0x10D95DB8;
    using CastFn = void* (__thiscall*)(void*, void*);
    struct Caster { void* vtable; };
    CastFn cast = *reinterpret_cast<CastFn*>(casterVtable);
    Caster caster;
    caster.vtable = reinterpret_cast<void*>(casterVtable);
    for (int index = 0; index < inventoryCount && index < 160; ++index)
    {
        void* inventory = inventories[index];
        void* table = ReadPtr(inventory, 0x0C);
        if (!Plausible(table)) continue;
        unsigned int count = ReadU32(table, 0);
        void** objects = reinterpret_cast<void**>(ReadPtr(table, 0x1C));
        if (!Plausible(objects) || count > 20000) continue;
        for (unsigned int i = 0; i < count; ++i)
        {
            void* object = objects[i];
            if (!Plausible(object)) continue;
            unsigned int vtable = ReadU32(object, 0);
            if (vtable == inventoryVtable)
            {
                bool seen = false;
                for (int k = 0; k < inventoryCount; ++k)
                    if (inventories[k] == object) { seen = true; break; }
                if (!seen && inventoryCount < 160) inventories[inventoryCount++] = object;
                continue;
            }
            if (vtable < 0x10D00000 || vtable > 0x11200000) continue;
            if (ReadU32(object, 0x10) != hash) continue;
            void* tmpl = cast(&caster, object);
            if (tmpl != nullptr)
                return tmpl;
        }
    }
    return nullptr;
}

// Reads the MSVC RTTI name behind an object's vtable (".?AVClass@Namespace@@"),
// which is how the puppet's behaviours are identified.
static const char* RttiName(void* object)
{
    if (object == nullptr) return nullptr;
    unsigned int vtable = *(unsigned int*)object;
    if (vtable < 0x10D00000 || vtable > 0x11200000) return nullptr;
    unsigned int locator = *(unsigned int*)(reinterpret_cast<unsigned char*>(vtable) - 4);
    if (!Plausible(reinterpret_cast<void*>(locator))) return nullptr;
    unsigned int descriptor = ReadU32(reinterpret_cast<void*>(locator), 0x0C);
    if (!Plausible(reinterpret_cast<void*>(descriptor))) return nullptr;
    // The MSVC TypeDescriptor holds the decorated name inline, right after its
    // vftable and spare word.
    return reinterpret_cast<const char*>(descriptor + 8);
}

// Walks the object's behaviour controller (GameObject+0x20) and reports the
// behaviour classes it holds: the render/animation ones are kept, the fight ones
// are what let a puppet hurt players.
static void DumpBehaviours(void* object)
{
    if (object == nullptr) return;
    unsigned char* controller = reinterpret_cast<unsigned char*>(object) + 0x20;
    for (unsigned int offset = 0; offset <= 0x40; offset += 4)
    {
        unsigned int begin = ReadU32(controller, offset);
        unsigned int end = ReadU32(controller, offset + 4);
        if (!Plausible(reinterpret_cast<void*>(begin)) || end < begin) continue;
        unsigned int count = (end - begin) / 4;
        if (count == 0 || count > 64) continue;
        bool ownerMatches = true;
        for (unsigned int i = 0; i < count; ++i)
        {
            void* behaviour = reinterpret_cast<void*>(ReadU32(reinterpret_cast<void*>(begin), i * 4));
            if (!Plausible(behaviour) ||
                (ReadU32(behaviour, 0) != reinterpret_cast<unsigned int>(object) &&
                 ReadU32(behaviour, 4) != reinterpret_cast<unsigned int>(object)))
            {
                ownerMatches = false;
                break;
            }
        }
        if (!ownerMatches) continue;
        Log("beh: controller offset +0x%X count=%u", offset, count);
        for (unsigned int i = 0; i < count; ++i)
        {
            void* behaviour = reinterpret_cast<void*>(ReadU32(reinterpret_cast<void*>(begin), i * 4));
            const char* name = RttiName(behaviour);
            Log("beh: [%u] %p %s", i, behaviour, name != nullptr ? name : "(no rtti)");
        }
        return;
    }
    Log("beh: no behaviour list found");
}

// The behaviours that let a puppet hurt, shove or react to the player. They are
// switched off so the avatar is only visual, while the animation and render
// behaviours stay on.
static const char* const g_harmfulBehaviours[] =
{
    "PhysicsBehaviour",
    "TouchBehaviour",
    "CollisionAction",
    "HitReaction",
    "FightVariables",
    "ChargeBehaviour",
    "GrabSelection",
    "Grabbable",
    "Targetable",
    "LockOnTargeting",
    "ThreatReceiver",
    "SensesInfo",
    "PoisonReaction",
    "PowerWheel",
    "Tendril",
    "Transformation",
    "Disguise",
};

static void DisableCombatBehaviours(void* object)
{
    if (object == nullptr) return;
    unsigned char* controller = reinterpret_cast<unsigned char*>(object) + 0x20;
    for (unsigned int offset = 0; offset <= 0x40; offset += 4)
    {
        unsigned int begin = ReadU32(controller, offset);
        unsigned int end = ReadU32(controller, offset + 4);
        if (!Plausible(reinterpret_cast<void*>(begin)) || end < begin) continue;
        unsigned int count = (end - begin) / 4;
        if (count == 0 || count > 64) continue;
        bool ownerMatches = true;
        for (unsigned int i = 0; i < count && ownerMatches; ++i)
        {
            void* behaviour = reinterpret_cast<void*>(ReadU32(reinterpret_cast<void*>(begin), i * 4));
            if (!Plausible(behaviour) ||
                (ReadU32(behaviour, 0) != reinterpret_cast<unsigned int>(object) &&
                 ReadU32(behaviour, 4) != reinterpret_cast<unsigned int>(object)))
                ownerMatches = false;
        }
        if (!ownerMatches) continue;
        using SetEnabledFn = void (__thiscall*)(void*, bool);
        unsigned int disabled = 0;
        for (unsigned int i = 0; i < count; ++i)
        {
            void* behaviour = reinterpret_cast<void*>(ReadU32(reinterpret_cast<void*>(begin), i * 4));
            const char* name = RttiName(behaviour);
            if (name == nullptr) continue;
            for (unsigned int k = 0; k < sizeof(g_harmfulBehaviours) / sizeof(g_harmfulBehaviours[0]); ++k)
            {
                if (strstr(name, g_harmfulBehaviours[k]) != nullptr)
                {
                    reinterpret_cast<SetEnabledFn>(At<void*>(0x007EEB50))(behaviour, false);
                    ++disabled;
                    break;
                }
            }
        }
        Log("beh: disabled %u of %u behaviours", disabled, count);
        return;
    }
    Log("beh: no behaviour list found to disable");
}

static void* FindBehaviourByName(void* object, const char* wanted)
{
    if (object == nullptr) return nullptr;
    unsigned char* controller = reinterpret_cast<unsigned char*>(object) + 0x20;
    for (unsigned int offset = 0; offset <= 0x40; offset += 4)
    {
        unsigned int begin = ReadU32(controller, offset);
        unsigned int end = ReadU32(controller, offset + 4);
        if (!Plausible(reinterpret_cast<void*>(begin)) || end < begin) continue;
        unsigned int count = (end - begin) / 4;
        if (count == 0 || count > 64) continue;
        bool ownerMatches = true;
        for (unsigned int i = 0; i < count && ownerMatches; ++i)
        {
            void* behaviour = reinterpret_cast<void*>(ReadU32(reinterpret_cast<void*>(begin), i * 4));
            if (!Plausible(behaviour) ||
                (ReadU32(behaviour, 0) != reinterpret_cast<unsigned int>(object) &&
                 ReadU32(behaviour, 4) != reinterpret_cast<unsigned int>(object)))
                ownerMatches = false;
        }
        if (!ownerMatches) continue;
        for (unsigned int i = 0; i < count; ++i)
        {
            void* behaviour = reinterpret_cast<void*>(ReadU32(reinterpret_cast<void*>(begin), i * 4));
            const char* name = RttiName(behaviour);
            if (name != nullptr && strstr(name, wanted) != nullptr)
                return behaviour;
        }
        return nullptr;
    }
    return nullptr;
}

// Dumps the character's intention state (behaviour+0x10, 0x58 bytes) so the
// movement axes can be told apart from the rest of the fields.
static void DumpIntention(void* object, const char* tag)
{
    void* behaviour = FindBehaviourByName(object, "CharacterIntentionBehaviour");
    if (behaviour == nullptr)
    {
        Log("intent(%s): no behaviour", tag);
        return;
    }
    const unsigned int* words = reinterpret_cast<const unsigned int*>(reinterpret_cast<unsigned char*>(behaviour) + 0x10);
    Log("intent(%s): behaviour=%p words %08X %08X %08X %08X %08X %08X %08X %08X", tag, behaviour,
        words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7]);
    unsigned int buttons = words[4];
    unsigned int axes = words[7];
    if (Plausible(reinterpret_cast<void*>(axes)))
    {
        Log("intent(%s): axes=%p [0]=%.2f %.2f [1]=%.2f %.2f [2]=%.2f %.2f [3]=%.2f %.2f", tag,
            reinterpret_cast<void*>(axes),
            *(const float*)(axes + 0), *(const float*)(axes + 4),
            *(const float*)(axes + 8), *(const float*)(axes + 12),
            *(const float*)(axes + 16), *(const float*)(axes + 20),
            *(const float*)(axes + 24), *(const float*)(axes + 28));
    }
    if (Plausible(reinterpret_cast<void*>(buttons)))
    {
        Log("intent(%s): buttons=%p [0]=%d %.2f [1]=%d %.2f [2]=%d %.2f [3]=%d %.2f", tag,
            reinterpret_cast<void*>(buttons),
            *(const int*)(buttons + 0), *(const float*)(buttons + 4),
            *(const int*)(buttons + 12), *(const float*)(buttons + 16),
            *(const int*)(buttons + 24), *(const float*)(buttons + 28),
            *(const int*)(buttons + 36), *(const float*)(buttons + 40));
    }
}

// Pokes the character's intention axes: used to find which axis drives the
// locomotion animations.
static void PokeIntention(void* object, float value)
{
    void* behaviour = FindBehaviourByName(object, "CharacterIntentionBehaviour");
    if (behaviour == nullptr) return;
    unsigned int axes = ReadU32(reinterpret_cast<unsigned char*>(behaviour) + 0x10, 0x1C);
    if (!Plausible(reinterpret_cast<void*>(axes))) return;
    *(float*)(axes + 0) = value;
    *(float*)(axes + 8) = value;
    *(float*)(axes + 16) = value;
    *(float*)(axes + 24) = value;
}

// The template copy gives the puppet the player's own animation objects. Sharing
// the choreo puppet drives the player's pose a second time, so the puppet gets
// it cleared: it still draws, but it no longer animates the player.
static unsigned int GetAttributeValue(void* object, unsigned int vtable)
{
    if (object == nullptr) return 0;
    unsigned int begin = ReadU32(object, 8);
    unsigned int end = ReadU32(object, 0xC);
    if (!Plausible(reinterpret_cast<void*>(begin)) || end < begin) return 0;
    unsigned int count = (end - begin) / 16;
    for (unsigned int i = 0; i < count && i < 64; ++i)
    {
        unsigned char* entry = reinterpret_cast<unsigned char*>(begin) + i * 16;
        unsigned int attribute = *(unsigned int*)(entry + 8);
        if (attribute == 0 || !Plausible(reinterpret_cast<void*>(attribute + 4))) continue;
        if (*(unsigned int*)attribute == vtable)
            return *(unsigned int*)(attribute + 4);
    }
    return 0;
}

// The template copy gives the puppet the player's own animation drivers, and a
static void ClonePuppetDrivers(void* puppet, void* source)
{
    if (puppet == nullptr || source == nullptr) return;
    unsigned int count = ReadU32(source, 0x190);
    if (count == 0 || count > 32)
    {
        Log("anim: no drivers to clone (count=%u)", count);
        return;
    }
    using GetDriverFn = void* (__thiscall*)(const void*, unsigned int);
    using CopyDriverFromFn = void* (__thiscall*)(void*, void*, void*);
    // Drop the entry list without releasing the drivers: they are the player's
    // and releasing them here frees objects the player still uses.
    *(unsigned int*)(reinterpret_cast<unsigned char*>(puppet) + 0x190) = 0;
    for (unsigned int i = 0; i < count; ++i)
    {
        void* driver = reinterpret_cast<GetDriverFn>(At<void*>(0x009475C0))(source, i);
        if (driver != nullptr)
            reinterpret_cast<CopyDriverFromFn>(At<void*>(0x00947600))(puppet, driver, source);
    }
    Log("anim: cloned %u drivers", count);
}

static void ClearAttributeByVtable(void* object, unsigned int vtable)
{
    if (object == nullptr) return;
    unsigned int begin = ReadU32(object, 8);
    unsigned int end = ReadU32(object, 0xC);
    if (!Plausible(reinterpret_cast<void*>(begin)) || end < begin) return;
    unsigned int count = (end - begin) / 16;
    for (unsigned int i = 0; i < count && i < 64; ++i)
    {
        unsigned char* entry = reinterpret_cast<unsigned char*>(begin) + i * 16;
        unsigned int attribute = *(unsigned int*)(entry + 8);
        if (attribute == 0 || !Plausible(reinterpret_cast<void*>(attribute + 4))) continue;
        if (*(unsigned int*)attribute == vtable)
        {
            *(unsigned int*)(attribute + 4) = 0;
            Log("attrs: cleared %08X on %p", vtable, object);
        }
    }
}

static void SpawnPuppet()
{
    Log("spawn: begin pending=%d spawned=%d", g_spawnPending ? 1 : 0, g_puppetSpawned ? 1 : 0);
    if (g_puppetSpawned || g_engine == nullptr) return;
    void* inventory = *(void**)(reinterpret_cast<unsigned char*>(g_engine) + 0x1283818);
    if (inventory == nullptr)
    {
        Log("spawn: gRootInventory is null");
        return;
    }
    using SetPositionFn = void (__thiscall*)(void*, const float*);

    // Prefer the player's own template: with an Alex model the puppet is
    // visible, and it stands where the remote player stands.
    void* tmpl = g_playerTemplate;
    if (tmpl != nullptr)
        Log("spawn: using player template %p", tmpl);
    if (tmpl == nullptr)
    {
        tmpl = FindSpawnedTemplateByHash(0x2C48D34E); // "Soldier"
        if (tmpl == nullptr)
            tmpl = FindInventoryTemplateByHash(0x2C48D34E);
        if (tmpl != nullptr)
            Log("spawn: using Soldier template %p", tmpl);
    }
    if (tmpl == nullptr)
    {
        tmpl = FindSpawnedTemplateByHash(0xA5B3FA51); // "Brawler"
        if (tmpl == nullptr)
            tmpl = FindInventoryTemplateByHash(0xA5B3FA51);
        if (tmpl != nullptr)
            Log("spawn: using Brawler template %p", tmpl);
    }
    if (tmpl == nullptr)
        tmpl = g_playerTemplate;
    Log("spawn: player template = %p", tmpl);
    if (tmpl == nullptr)
    {
        g_puppetSpawned = true;
        return;
    }
    using SpawnFn = void* (__cdecl*)(void*, const void*, void*, void*, bool);
    using NameCtorFn = void (__thiscall*)(void*, const char*);
    unsigned long long objectName = 0;
    reinterpret_cast<NameCtorFn>(g_nameCtor)(&objectName, "coop_puppet");
    Log("spawn: object name=%08X%08X", (unsigned int)(objectName >> 32), (unsigned int)objectName);
    void* object = reinterpret_cast<SpawnFn>(g_createObject)(tmpl, &objectName, inventory, nullptr, true);
    Log("spawn: object = %p vtable=%08X", object, object != nullptr ? *(unsigned int*)object : 0);
    if (object == nullptr)
    {
        g_puppetSpawned = true;
        return;
    }
    // Start on the ground next to the local player: spawning at the origin makes
    // the puppet fall and its landing blast throws and hurts the player.
    void* owner = g_getPlayerObject ? g_getPlayerObject(0) : nullptr;
    float position[3] = { g_remoteX, g_remoteY, g_remoteZ };
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    if (owner != nullptr && ReadObjectPosition(owner, &px, &py, &pz))
    {
        position[0] = px - 1.5f;
        position[1] = py;
        position[2] = pz - 1.5f;
    }
    reinterpret_cast<SetPositionFn>(g_setPosition)(object, position);
    // GameObject::GetHandle returns the handle through a hidden pointer, so build
    // it from the object's slot index and the generation in the master table.
    unsigned short objectIndex = *(unsigned short*)(reinterpret_cast<unsigned char*>(object) + 0x58);
    unsigned int tableAddress = ReadU32(g_engine, 0x128348c);
    unsigned short generation = 0;
    if (objectIndex != 0xFFFF && Plausible(reinterpret_cast<void*>(tableAddress)))
        generation = *(unsigned short*)(tableAddress + objectIndex * 8 + 4);
    unsigned int handle = objectIndex | (generation << 16);
    g_puppetHandle = handle;
    *(unsigned int*)(reinterpret_cast<unsigned char*>(g_engine) + 0x126E410) = handle;
    g_puppetObject = object;
    void* playerObject = g_getPlayerObject ? g_getPlayerObject(0) : nullptr;
    Log("anim: pose player=%08X puppet=%08X choreo player=%08X puppet=%08X",
        GetAttributeValue(playerObject, 0x10E80688), GetAttributeValue(object, 0x10E80688),
        GetAttributeValue(playerObject, 0x10D9653C), GetAttributeValue(object, 0x10D9653C));
    if (g_puppetStatic)
    {
        ClearAttributeByVtable(object, 0x10D9653C); // choreo::Puppet*
        reinterpret_cast<void (__thiscall*)(void*, bool)>(At<void*>(0x007E0960))(object, false);
        Log("spawn: puppet is static (behaviours off)");
    }
    else
    {
        DisableCombatBehaviours(object);
        ClonePuppetDrivers(reinterpret_cast<void*>(GetAttributeValue(object, 0x10D9653C)),
            reinterpret_cast<void*>(GetAttributeValue(playerObject, 0x10D9653C)));
    }
    void* player1 = g_getPlayerObject ? g_getPlayerObject(1) : nullptr;
    Log("spawn: index=%u gen=%u handle=%08X p1=%p at %.1f %.1f %.1f", objectIndex, generation, handle,
        player1, position[0], position[1], position[2]);
    g_puppetSpawned = true;
}


static void* g_renderUpdate = nullptr;

static void __attribute__((force_align_arg_pointer)) __thiscall HookRenderUpdate(void* self, float dt, unsigned short mask)
{
    reinterpret_cast<void (__thiscall*)(void*, float, unsigned short)>(g_renderUpdate)(self, dt, mask);
    if (g_puppetObject == nullptr || !g_poseValid) return;
    void* owner = *(void**)(reinterpret_cast<unsigned char*>(self) + 4);
    if (owner == nullptr) owner = *(void**)(reinterpret_cast<unsigned char*>(self) + 0);
    if (owner != g_puppetObject) return;
    unsigned int pose = GetAttributeValue(g_puppetObject, 0x10E80688);
    if (!Plausible(reinterpret_cast<void*>(pose))) return;
    if (ReadU32(reinterpret_cast<void*>(pose), 0x14) != g_poseJoints) return;
    unsigned int rotations = ReadU32(reinterpret_cast<void*>(pose), 0x18);
    unsigned int translations = ReadU32(reinterpret_cast<void*>(pose), 0x1C);
    if (!Plausible(reinterpret_cast<void*>(rotations)) || !Plausible(reinterpret_cast<void*>(translations)))
        return;
    // Smooth the 30 Hz stream up to the frame rate: nlerp the quaternions and
    // lerp the translations of a display copy, then push that to the pose.
    for (unsigned int i = 0; i < g_poseJoints; ++i)
    {
        float* shown = reinterpret_cast<float*>(g_poseDisplay + i * 32);
        const float* target = reinterpret_cast<const float*>(g_poseBuffer + i * 32);
        float dot = shown[0] * target[0] + shown[1] * target[1] + shown[2] * target[2] + shown[3] * target[3];
        float sign = dot < 0.0f ? -1.0f : 1.0f;
        for (int k = 0; k < 4; ++k)
            shown[k] += (target[k] * sign - shown[k]) * 0.45f;
        float length = sqrtf(shown[0] * shown[0] + shown[1] * shown[1] + shown[2] * shown[2] + shown[3] * shown[3]);
        if (length > 0.0001f)
        {
            shown[0] /= length;
            shown[1] /= length;
            shown[2] /= length;
            shown[3] /= length;
        }
        for (int k = 4; k < 7; ++k)
            shown[k] += (target[k] - shown[k]) * 0.45f;
        shown[7] = target[7];
    }
    for (unsigned int i = 0; i < g_poseJoints; ++i)
    {
        memcpy(reinterpret_cast<void*>(rotations + i * 16), g_poseDisplay + i * 32, 16);
        memcpy(reinterpret_cast<void*>(translations + i * 16), g_poseDisplay + i * 32 + 16, 16);
    }
    *(unsigned char*)(pose + 0x40) |= 3;
    reinterpret_cast<void (__thiscall*)(void*)>(At<void*>(0x006DD6E0))(reinterpret_cast<void*>(pose));
    using CopyPoseFn = void (__thiscall*)(void*, void*);
    reinterpret_cast<CopyPoseFn>(At<void*>(0x007E8740))(
        reinterpret_cast<unsigned char*>(self) + 0x14, reinterpret_cast<void*>(pose));
    if ((g_poseApplied++ % 3000) == 0)
        Log("pose applied: %u joints", g_poseJoints);
}

static void* g_startEffect = nullptr;
static unsigned int g_effectRecvCount = 0;

static void SpawnRemoteEffect(const unsigned char* data, int length)
{
    if (length < 10 + 0x40 || g_puppetObject == nullptr || g_puppetHandle == 0xFFFF) return;
    void* manager = reinterpret_cast<void* (*)()>(At<void*>(0x008193A0))();
    if (!Plausible(manager)) return;
    unsigned char setup[0xC0];
    reinterpret_cast<void (__thiscall*)(void*)>(At<void*>(0x00820080))(setup);
    memcpy(setup, data + 10, 0xA0);
    memcpy(setup + 0x48, &g_puppetHandle, 4);
    unsigned char out[4] = { 0, 0, 0, 0 };
    unsigned int nameCopy[2];
    memcpy(nameCopy, data + 2, 8);
    nameCopy[1] = 0; // the registry index is per process; the hash is the same
    reinterpret_cast<void (__thiscall*)(void*, void*, const void*, void*)>(At<void*>(0x0081E6B0))(
        manager, out, nameCopy, setup);
    if ((g_effectRecvCount++ % 50) == 0)
        Log("effect recv #%u", g_effectRecvCount);
}

static void* __attribute__((force_align_arg_pointer)) __thiscall HookStartEffect(void* self, void* out, const void* name, void* setup)
{
    void* result = reinterpret_cast<void* (__thiscall*)(void*, void*, const void*, void*)>(g_startEffect)(
        self, out, name, setup);
    if (!g_transportReady || setup == nullptr || name == nullptr || g_engine == nullptr) return result;
    unsigned int target = ReadU32(setup, 0x48);
    unsigned int playerHandle = ReadU32(g_engine, 0x126E40C);
    if (target == 0 || target == 0xFFFF || target != playerHandle) return result;
    static unsigned int windowStart = 0;
    static unsigned int windowCount = 0;
    unsigned int now = GetTickCount();
    if (now - windowStart > 1000)
    {
        windowStart = now;
        windowCount = 0;
    }
    if (++windowCount > 60) return result;
    unsigned char packet[2 + 8 + 0xA0];
    packet[0] = 'E';
    packet[1] = '1';
    memcpy(packet + 2, name, 8);
    memcpy(packet + 10, setup, 0xA0);
    sendto(g_socket, (const char*)packet, sizeof(packet), 0, (sockaddr*)&g_peer, sizeof(g_peer));
    if ((++g_effectSendCount % 100) == 0)
        Log("effect send #%u", g_effectSendCount);
    return result;
}

static void* __attribute__((force_align_arg_pointer)) __thiscall HookOpen(void* self, unsigned char kind, unsigned int a, unsigned int b)
{
    if (self == &g_fakeChannelManager)
    {
        Log("fake ChannelManager::Open kind=%u a=%u b=%u", kind, a, b);
        return &g_fakeChannel;
    }
    Log("ChannelManager::Open self=%p kind=%u a=%u b=%u", self, kind, a, b);
    return g_open(self, kind, a, b);
}

static void __attribute__((force_align_arg_pointer)) __thiscall HookClose(void* self, void* channel)
{
    if (self == &g_fakeChannelManager || channel == &g_fakeChannel)
    {
        Log("fake ChannelManager::Close");
        return;
    }
    Log("ChannelManager::Close self=%p channel=%p", self, channel);
    g_close(self, channel);
}

static void __attribute__((force_align_arg_pointer)) __thiscall HookSend(void* self, int target, int type, const void* data, unsigned int length, int flag)
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

static const char* __attribute__((force_align_arg_pointer)) __thiscall HookReceive(void* self, char* buffer, int capacity, unsigned int* length, unsigned char* flag, int** source)
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

static void __attribute__((force_align_arg_pointer)) __cdecl HookStartSync(void* owner, int a, int b)
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

static void HandlePacket(const unsigned char* data, int length)
{
    if (length >= 10 && data[0] == 'E' && data[1] == '1')
    {
        SpawnRemoteEffect(data, length);
        return;
    }
    if (length >= 4 && data[0] == 'R' && data[1] == '1')
    {
        StoreRotation(data, length);
        return;
    }
    if (length >= 4 && data[0] == 'A' && data[1] == '1')
    {
        StorePose(data, length);
        return;
    }
    if (length >= 8 && memcmp(data, "P1", 2) == 0)
    {
        short sx = 0, sy = 0, sz = 0;
        memcpy(&sx, data + 2, 2);
        memcpy(&sy, data + 4, 2);
        memcpy(&sz, data + 6, 2);
        g_remoteX = sx / 8.0f;
        g_remoteY = sy / 8.0f;
        g_remoteZ = sz / 8.0f;
        g_remoteValid = true;
        if ((g_recvCount++ % 10) == 0)
            Log("transform recv(%s) %.2f %.2f %.2f", g_isHost ? "host" : "client", g_remoteX, g_remoteY, g_remoteZ);
    }
    else if (length >= 4 && memcmp(data, "PCOP", 4) == 0)
    {
        Log("heartbeat received");
    }
}

static void PollSocket()
{
    if (g_socket == INVALID_SOCKET) return;
    for (int i = 0; i < 16; ++i)
    {
        unsigned char buffer[8192];
        sockaddr_in from = {};
        int fromSize = sizeof(from);
        int received = recvfrom(g_socket, (char*)buffer, sizeof(buffer), 0, (sockaddr*)&from, &fromSize);
        if (received <= 0) break;
        HandlePacket(buffer, received);
    }
}

static void __attribute__((force_align_arg_pointer)) __cdecl HookUpdateSync()
{
    FrameTick();
    g_updateSync();
}

static void __attribute__((force_align_arg_pointer)) __thiscall HookControllerUpdate(void* self)
{
    FrameTick();
    reinterpret_cast<void (__thiscall*)(void*)>(g_controllerUpdate)(self);
}

static void FrameTick()
{
    // Both tick sources (engine sync and the controller) may fire in one frame.
    DWORD now = GetTickCount();
    if (now == g_lastTick) return;
    g_lastTick = now;

    PollSocket();

    if (!g_started && !g_transportReady && g_updateCalls > 300)
    {
        if (g_fakeSession)
            StartFakeSession();
        else
        {
            g_transportReady = InitSocket();
            Log("transport ready=%d", g_transportReady ? 1 : 0);
        }
    }
    if (g_started || g_transportReady)
    {
        if ((g_updateCalls % 60) == 0)
            SendHeartbeat();
        // ~20 Hz keeps the remote avatar close to its real position.
        if ((g_updateCalls % 3) == 0)
            SendPlayerTransform();
        // 30 Hz pose stream; the receiver smooths it to the frame rate.
        if ((g_updateCalls % 2) == 0)
            SendPose();
        if ((g_updateCalls % 3) == 2)
            SendRotation();
    }
    if (g_getPlayerObject != nullptr && (g_updateCalls % 600) == 0)
    {
        void* player0 = g_getPlayerObject(0);
        void* player1 = g_getPlayerObject(1);
        float px = 0.0f, py = 0.0f, pz = 0.0f;
        if (!ReadObjectPosition(player0, &px, &py, &pz))
            px = py = pz = -1.0f;
        Log("players p0=%p p1=%p handles=%08X/%08X pos=%.2f %.2f %.2f", player0, player1,
            *(unsigned int*)(reinterpret_cast<unsigned char*>(g_engine) + 0x126E40C),
            *(unsigned int*)(reinterpret_cast<unsigned char*>(g_engine) + 0x126E410), px, py, pz);
        if (player0 != nullptr && !g_puppetSpawned)
        {
            if (!g_attrDumped)
            {
                g_attrDumped = true;
                DumpAttributes(player0);
            }
            LookupPlayerTemplate(player0);
            if (g_playerTemplate != nullptr)
                g_spawnPending = true;
        }
        if (g_puppetObject != nullptr)
        {
            float px = 0.0f, py = 0.0f, pz = 0.0f;
            float qx = 0.0f, qy = 0.0f, qz = 0.0f;
            bool playerOk = ReadObjectPosition(player0, &px, &py, &pz);
            bool puppetOk = ReadObjectPosition(g_puppetObject, &qx, &qy, &qz);
            Log("puppet: player=%d %.2f %.2f %.2f puppet=%d %.2f %.2f %.2f", playerOk ? 1 : 0, px, py, pz,
                puppetOk ? 1 : 0, qx, qy, qz);
        }
    }
    if (g_debugIntention && (g_updateCalls % 15) == 0)
    {
        void* localPlayer = g_getPlayerObject ? g_getPlayerObject(0) : nullptr;
        if (localPlayer != nullptr)
            DumpIntention(localPlayer, "local");
    }
    g_updateCalls++;
    if (g_started)
        g_updateSync();
    if (g_isHost && g_testMove != 0 && !g_testMoved && g_updateCalls > 6000)
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (ReadPlayerPosition(&x, &y, &z))
        {
            float dx = 0.0f, dy = 0.0f, dz = 0.0f;
            if (g_testMove == 1) dx = 3.0f;
            else if (g_testMove == 2) dx = -3.0f;
            else if (g_testMove == 3) dz = 3.0f;
            else if (g_testMove == 4) dz = -3.0f;
            SetObjectPosition(g_getPlayerObject(0), x + dx, y + dy, z + dz);
            Log("test: moved host player to %.2f %.2f %.2f (mode %d)", x + dx, y + dy, z + dz, g_testMove);
            g_testMoved = true;
        }
    }
    if (g_spawnPending && !g_puppetSpawned)
        SpawnPuppet();
    if (g_isHost && g_testWalk > 0 && g_testWalkLeft == 0 && g_updateCalls > 6000)
    {
        g_testWalkLeft = g_testWalk;
        Log("walk: starting %d frames", g_testWalk);
    }
    if (g_testWalkLeft > 0)
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        void* player = g_getPlayerObject ? g_getPlayerObject(0) : nullptr;
        if (player != nullptr && ReadObjectPosition(player, &x, &y, &z))
        {
            SetObjectPosition(player, x + 0.2f, y, z);
            if (--g_testWalkLeft == 0)
                Log("walk: finished at %.2f %.2f %.2f", x + 0.2f, y, z);
        }
        else
        {
            g_testWalkLeft = 0;
        }
    }
    if (g_puppetObject != nullptr)
    {
        float px = 0.0f, py = 0.0f, pz = 0.0f;
        bool playerOk = ReadPlayerPosition(&px, &py, &pz);
        float x = g_remoteX, y = g_remoteY, z = g_remoteZ;
        if (g_puppetLocal != 0 || !g_remoteValid)
        {
            if (playerOk)
            {
                x = px;
                y = py;
                z = pz;
                if (g_puppetLocal == 1)
                    y += 1.0f;
                else if (g_puppetLocal == 3)
                    x += 1.5f;
            }
        }
        // When the puppet would stand inside the local player (both clients load
        // the same save), move it a step to the side so it stands on the roof
        // instead of over the drop.
        if (playerOk)
        {
            float dx = x - px;
            float dz = z - pz;
            if (dx * dx + dz * dz < 1.0f)
            {
                x = px - 1.5f;
                z = pz - 1.5f;
                y = py;
            }
        }
        // Smooth the steps between packets; snap when the gap is too large.
        if (!g_puppetPlaced)
        {
            g_puppetX = x;
            g_puppetY = y;
            g_puppetZ = z;
            g_puppetPlaced = true;
        }
        else
        {
            float dx = x - g_puppetX;
            float dy = y - g_puppetY;
            float dz = z - g_puppetZ;
            if (dx * dx + dy * dy + dz * dz > 64.0f)
            {
                g_puppetX = x;
                g_puppetY = y;
                g_puppetZ = z;
            }
            else
            {
                g_puppetX += dx * 0.35f;
                g_puppetY += dy * 0.35f;
                g_puppetZ += dz * 0.35f;
            }
        }
        if (g_followLog && (g_updateCalls % 15) == 0)
            Log("follow: puppet %.2f %.2f %.2f target %.2f %.2f %.2f", g_puppetX, g_puppetY, g_puppetZ, x, y, z);
        if (g_intentTest != 0.0f)
            PokeIntention(g_puppetObject, g_intentTest);
        if (g_remoteBasisValid)
        {
            float* matrix = const_cast<float*>(ReadInlineMatrix(g_puppetObject));
            if (matrix != nullptr)
            {
                memcpy(matrix, g_remoteBasis, sizeof(g_remoteBasis));
                matrix[12] = g_puppetX;
                matrix[13] = g_puppetY;
                matrix[14] = g_puppetZ;
            }
            else
            {
                SetObjectPosition(g_puppetObject, g_puppetX, g_puppetY, g_puppetZ);
            }
        }
        else
        {
            SetObjectPosition(g_puppetObject, g_puppetX, g_puppetY, g_puppetZ);
        }
    }
}

static void* __attribute__((force_align_arg_pointer)) __cdecl HookApplySync(const unsigned int* machines, int type, const void* message)
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
        Log("transform recv(%s) %.2f %.2f %.2f", g_isHost ? "host" : "client", sx / 8.0f, sy / 8.0f, sz / 8.0f);
        g_remoteX = sx / 8.0f;
        g_remoteY = sy / 8.0f;
        g_remoteZ = sz / 8.0f;
        g_remoteValid = true;
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

// Every game object the game spawns passes through the static
// GameObjectSpawner::Create; remembering which template made which object
// tells us the player's own template without ever resolving its name.
static void* g_sampledTemplate = nullptr;
using CreateSpawnedFn = void* (__cdecl*)(void*, const void*, int, void*, void*, bool);
static CreateSpawnedFn g_createSpawned = nullptr;

static void* __attribute__((force_align_arg_pointer)) __cdecl HookCreateSpawned(void* tmpl, const void* name, int heap, void* inventory, void* target, bool flag)
{
    void* object = g_createSpawned(tmpl, name, heap, inventory, target, flag);
    long index = InterlockedIncrement(&g_spawnCount) - 1;
    if (index >= 0 && index < 4096)
    {
        g_spawns[index].object = object;
        g_spawns[index].tmpl = tmpl;
    }
    if (index < 60)
        Log("spawned[%ld] tmpl=%p name=%08X%08X object=%p vtable=%08X", index, tmpl,
            name != nullptr ? *(const unsigned int*)((const unsigned char*)name + 4) : 0,
            name != nullptr ? *(const unsigned int*)name : 0,
            object, object != nullptr ? *(unsigned int*)object : 0);
    if (g_logSpawns && object != nullptr)
    {
        const char* className = RttiName(object);
        float px = 0.0f, py = 0.0f, pz = 0.0f;
        float ox = 0.0f, oy = 0.0f, oz = 0.0f;
        void* player = g_getPlayerObject ? g_getPlayerObject(0) : nullptr;
        bool havePlayer = player != nullptr && ReadObjectPosition(player, &px, &py, &pz);
        bool haveObject = ReadObjectPosition(object, &ox, &oy, &oz);
        float dx = havePlayer && haveObject ? ox - px : 0.0f;
        float dy = havePlayer && haveObject ? oy - py : 0.0f;
        float dz = havePlayer && haveObject ? oz - pz : 0.0f;
        float distance = sqrtf(dx * dx + dy * dy + dz * dz);
        if (!havePlayer || !haveObject || distance < 12.0f)
            Log("spawn+: u=%u d=%.1f hash=%08X class=%s", g_updateCalls, distance, ReadU32(tmpl, 0x10),
                className != nullptr ? className : "(none)");
    }
    return object;
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
    Install("GameObjectSpawner::Create", At<void*>(0x007F25C0), (void*)&HookCreateSpawned, (void**)&g_createSpawned);
    Install("ControllerSystem::Update", At<void*>(0x00643A20), (void*)&HookControllerUpdate, (void**)&g_controllerUpdate);
    Install("RenderBehaviour::Update", At<void*>(0x008627B0), (void*)&HookRenderUpdate, (void**)&g_renderUpdate);
    Install("EffectManager::StartEffect", At<void*>(0x0081E6B0), (void*)&HookStartEffect, (void**)&g_startEffect);
    g_sendSyncMessage = At<SendSyncMessageFn>(0x00833790);
    g_getPlayerObject = At<GetPlayerObjectFn>(0x00099560);
    g_findTemplate = At<void*>(0x00374650);
    g_createObject = At<void*>(0x007F2740);
    g_setPosition = At<void*>(0x007E08E0);
    g_getHandle = At<void*>(0x007EEF60);
    g_findAttribute = At<void*>(0x007E0C00);
    g_getText = At<void*>(0x0027F1A0);
    g_createObject = At<void*>(0x007F2710);
    g_nameCtor = At<void*>(0x0005D5E0);

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
