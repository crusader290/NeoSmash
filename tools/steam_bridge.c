/* NeoSmash's Steam bridge: netplay between friends through Steam, with no port forwarding.
 *
 * The game already plays Local Wireless over a relay (runtime/net.h): a private room's relay runs
 * inside the host player's game, and the others' games send it UDP datagrams. This program
 * carries exactly those datagrams over Steam's networking (ISteamNetworkingMessages: Steam finds
 * a direct path between the PCs, or relays through its own servers), and starts the game itself:
 *
 *   host  - creates a Steam lobby (this game build, a room code), starts the game as the host of
 *           a private room bound to 127.0.0.1, and gives each Steam player in the lobby a local UDP
 *           socket of its own into that relay, so the relay sees one address per player;
 *   join  - finds the host's lobby by its room code (or among friends playing), joins it, and
 *           starts the game joined to 127.0.0.1:<port>, which this program forwards to the host.
 *
 *   neosmash_steam host [--code CODE] [--room NAME] [--port 39420] --build ID -- <game command>
 *   neosmash_steam join [--code CODE] [--port 39430] --build ID -- <game command>
 *   neosmash_steam list --build ID
 *   neosmash_steam lobby [--region au] [--port 39430] --build ID -- <game command>
 *                                            (everyone in one shared lobby; see run_lobby)
 *   neosmash_steam rooms --build ID          (every open room of this build: code, host, players)
 *
 * Steam runs it as Spacewar (app 480, Valve's test app): the Steam client must be running and
 * signed in; steam_api64.dll (Valve's, copied from an installed game by tools/steam_setup.py) and
 * steam_appid.txt sit beside this program. Players appear in Steam as playing Spacewar.
 *
 * --udp-test LOCAL:PEER replaces Steam with UDP on 127.0.0.1 (the host's PEER is ignored, a
 * joiner's PEER is the host's LOCAL), so two games on one PC test the bridging (tools/netmatch.py
 * --steam-test). Steam's API is loaded at run time through its flat C interface, no SDK needed. */
#define WIN32_LEAN_AND_MEAN
#define FD_SETSIZE 512   /* (select over a shared lobby's sockets: one a player, up to 250) */
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t u64;
typedef uint32_t u32;

/* What the player needs to see goes to the window (stdout) as well as the log (stderr: the
 * launchers send it, with the game's, to build\play.log). */
#define SAY(...) do { fprintf(stderr, __VA_ARGS__); printf(__VA_ARGS__); fflush(stdout); } while (0)

/* ---- Steam's flat API (steam_api64.dll), the few calls used -------------------------------- */
#pragma pack(push, 1)
typedef struct { int type; int size; union { u64 steam_id; uint32_t reserved[32]; } u; } NetIdentity;   /* SteamNetworkingIdentity */
#pragma pack(pop)
typedef struct {   /* SteamNetworkingMessage_t, as far as read here */
    void *data; int size; u32 conn; NetIdentity peer;
} NetMessage;
typedef struct { int user; int callback; uint8_t *param; int param_size; } CallbackMsg;   /* CallbackMsg_t */
typedef struct { u64 call; int callback; u32 param_size; } CallCompleted;                /* SteamAPICallCompleted_t */
typedef struct { u64 game_id; u32 ip; uint16_t port, query_port; u64 lobby; } FriendGame; /* FriendGameInfo_t */

enum {
    CB_LOBBY_ENTER = 504, CB_LOBBY_CHAT_UPDATE = 506, CB_LOBBY_MATCH_LIST = 510, CB_LOBBY_CREATED = 513,
    CB_LOBBY_JOIN_REQUESTED = 333, CB_CALL_COMPLETED = 703, CB_SESSION_REQUEST = 1251, CB_SESSION_FAILED = 1252,
};
#define SEND_FLAGS (1 | 32)   /* unreliable, no Nagle delay, restart a broken session */
#define SPACEWAR 480

static struct {
    int (*InitFlat)(char *err);
    void (*Shutdown)(void);
    void (*DispatchInit)(void);
    int (*GetPipe)(void);
    void (*RunFrame)(int);
    int (*NextCallback)(int, CallbackMsg *);
    void (*FreeCallback)(int);
    int (*CallResult)(int, u64, void *, int, int, int *);
    void *(*Matchmaking)(void);
    void *(*Messages)(void);
    void *(*Friends)(void);
    void *(*User)(void);
    void *(*Utils)(void);
    u64 (*CreateLobby)(void *, int, int);
    int (*SetLobbyData)(void *, u64, const char *, const char *);
    const char *(*GetLobbyData)(void *, u64, const char *);
    u64 (*RequestLobbyList)(void *);
    void (*AddStringFilter)(void *, const char *, const char *, int);
    void (*AddDistanceFilter)(void *, int);
    u64 (*LobbyByIndex)(void *, int);
    u64 (*JoinLobby)(void *, u64);
    void (*LeaveLobby)(void *, u64);
    u64 (*LobbyOwner)(void *, u64);
    int (*LobbyMembers)(void *, u64);
    u64 (*LobbyMember)(void *, u64, int);
    int (*SendToUser)(void *, const NetIdentity *, const void *, u32, int, int);
    int (*Receive)(void *, int, NetMessage **, int);
    int (*AcceptSession)(void *, const NetIdentity *);
    int (*CloseSession)(void *, const NetIdentity *);
    void (*Release)(NetMessage *);
    void (*SetSteamID)(NetIdentity *, u64);
    void (*InitRelay)(void *);
    u64 (*MySteamID)(void *);
    const char *(*PersonaName)(void *);
    int (*FriendCount)(void *, int);
    u64 (*FriendByIndex)(void *, int, int);
    int (*FriendGamePlayed)(void *, u64, FriendGame *);
    const char *(*FriendName)(void *, u64);
    int (*SetRichPresence)(void *, const char *, const char *);
} S;
static void *mm, *msgs, *friends;
static int pipe_handle;

static int steam_load(void) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    *strrchr(dir, '\\') = 0;
    snprintf(path, sizeof path, "%s\\steam_api64.dll", dir);
    HMODULE m = LoadLibraryA(path);
    if (!m) { SAY( "steam: cannot load %s (run tools\\steam_setup.py)\n", path); return 0; }
    struct { void **slot; const char *name; } table[] = {
        {(void **)&S.InitFlat, "SteamAPI_InitFlat"}, {(void **)&S.Shutdown, "SteamAPI_Shutdown"},
        {(void **)&S.DispatchInit, "SteamAPI_ManualDispatch_Init"}, {(void **)&S.GetPipe, "SteamAPI_GetHSteamPipe"},
        {(void **)&S.RunFrame, "SteamAPI_ManualDispatch_RunFrame"}, {(void **)&S.NextCallback, "SteamAPI_ManualDispatch_GetNextCallback"},
        {(void **)&S.FreeCallback, "SteamAPI_ManualDispatch_FreeLastCallback"}, {(void **)&S.CallResult, "SteamAPI_ManualDispatch_GetAPICallResult"},
        {(void **)&S.Matchmaking, "SteamAPI_SteamMatchmaking_v009"}, {(void **)&S.Messages, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002"},
        {(void **)&S.Friends, "SteamAPI_SteamFriends_v018"}, {(void **)&S.User, "SteamAPI_SteamUser_v023"},
        {(void **)&S.Utils, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004"},
        {(void **)&S.CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby"}, {(void **)&S.SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData"},
        {(void **)&S.GetLobbyData, "SteamAPI_ISteamMatchmaking_GetLobbyData"}, {(void **)&S.RequestLobbyList, "SteamAPI_ISteamMatchmaking_RequestLobbyList"},
        {(void **)&S.AddStringFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter"},
        {(void **)&S.AddDistanceFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter"},
        {(void **)&S.LobbyByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyByIndex"}, {(void **)&S.JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby"},
        {(void **)&S.LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby"}, {(void **)&S.LobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner"},
        {(void **)&S.LobbyMembers, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers"}, {(void **)&S.LobbyMember, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex"},
        {(void **)&S.SendToUser, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser"},
        {(void **)&S.Receive, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel"},
        {(void **)&S.AcceptSession, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser"},
        {(void **)&S.CloseSession, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser"},
        {(void **)&S.Release, "SteamAPI_SteamNetworkingMessage_t_Release"}, {(void **)&S.SetSteamID, "SteamAPI_SteamNetworkingIdentity_SetSteamID64"},
        {(void **)&S.InitRelay, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess"}, {(void **)&S.MySteamID, "SteamAPI_ISteamUser_GetSteamID"},
        {(void **)&S.PersonaName, "SteamAPI_ISteamFriends_GetPersonaName"}, {(void **)&S.FriendCount, "SteamAPI_ISteamFriends_GetFriendCount"},
        {(void **)&S.FriendByIndex, "SteamAPI_ISteamFriends_GetFriendByIndex"}, {(void **)&S.FriendGamePlayed, "SteamAPI_ISteamFriends_GetFriendGamePlayed"},
        {(void **)&S.FriendName, "SteamAPI_ISteamFriends_GetFriendPersonaName"}, {(void **)&S.SetRichPresence, "SteamAPI_ISteamFriends_SetRichPresence"},
    };
    for (size_t i = 0; i < sizeof table / sizeof *table; i++)
        if (!(*table[i].slot = (void *)GetProcAddress(m, table[i].name))) {
            SAY( "steam: steam_api64.dll has no %s (too old: run tools\\steam_setup.py)\n", table[i].name);
            return 0;
        }
    char appid[16];
    snprintf(appid, sizeof appid, "%d", SPACEWAR);
    SetEnvironmentVariableA("SteamAppId", appid);
    SetEnvironmentVariableA("SteamGameId", appid);
    char err[1024] = {0};
    int r = S.InitFlat(err);
    if (r) { SAY( "steam: cannot start (%d: %s) - is Steam running and signed in?\n", r, err); return 0; }
    S.DispatchInit();   /* (after the init, as Valve requires: callbacks then come through steam_pump) */
    pipe_handle = S.GetPipe();
    mm = S.Matchmaking(); msgs = S.Messages(); friends = S.Friends();
    S.InitRelay(S.Utils());
    return 1;
}

/* ---- the transport: Steam, or UDP on loopback for tests -------------------------------------- */
static int udp_test;              /* --udp-test */
static SOCKET test_sock;
static u64 test_peer;             /* a joiner: the host's port */

static NetIdentity identity(u64 id) {
    NetIdentity n;
    memset(&n, 0, sizeof n);
    S.SetSteamID(&n, id);
    return n;
}

static void transport_send(u64 to, const void *data, int len) {
    if (udp_test) {
        struct sockaddr_in a = {AF_INET, htons((u_short)to)};
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sendto(test_sock, data, len, 0, (struct sockaddr *)&a, sizeof a);
        return;
    }
    NetIdentity n = identity(to);
    S.SendToUser(msgs, &n, data, (u32)len, SEND_FLAGS, 0);
}

/* (each message: from whom, its bytes) */
typedef void Arrived(u64 from, const char *data, int len);
static void transport_poll(Arrived *arrived) {
    if (udp_test) {
        char buf[65536];
        struct sockaddr_in a;
        int al = sizeof a, n;
        while ((n = recvfrom(test_sock, buf, sizeof buf, 0, (struct sockaddr *)&a, &al)) > 0) {
            arrived(ntohs(a.sin_port), buf, n);
            al = sizeof a;
        }
        return;
    }
    NetMessage *got[64];
    int n;
    while ((n = S.Receive(msgs, 0, got, 64)) > 0)
        for (int i = 0; i < n; i++) {
            arrived(got[i]->peer.u.steam_id, (const char *)got[i]->data, got[i]->size);
            S.Release(got[i]);
        }
}

/* ---- Steam callbacks ------------------------------------------------------------------------- */
static u64 lobby, lobby_owner, pending_call;
static int pending_kind, match_count, joined_lobby = -1;
static int member_allowed(u64 id);

static void on_callback(int kind, const uint8_t *p) {
    switch (kind) {
    case CB_LOBBY_CREATED:
        if (*(const int *)p == 1) lobby = *(const u64 *)(p + 8);
        else SAY( "steam: the lobby was not created (result %d)\n", *(const int *)p);
        break;
    case CB_LOBBY_MATCH_LIST: match_count = (int)*(const u32 *)p; break;
    case CB_LOBBY_ENTER:
        joined_lobby = *(const u32 *)(p + 16) == 1;
        if (joined_lobby) lobby = *(const u64 *)p;
        break;
    case CB_SESSION_REQUEST: {
        u64 who = *(const u64 *)(p + 8);
        NetIdentity n = identity(who);
        if (member_allowed(who)) S.AcceptSession(msgs, &n);
        else SAY( "steam: ignored a connection from %llu, not in the lobby\n", (unsigned long long)who);
        break;
    }
    case CB_SESSION_FAILED: fprintf(stderr, "steam: a connection failed; Steam retries it\n"); break;
    case CB_LOBBY_JOIN_REQUESTED:
        SAY( "steam: invited to lobby %llu (start the join launcher with that friend's room code)\n",
                (unsigned long long)*(const u64 *)p);
        break;
    }
}

static void steam_pump(void) {
    if (udp_test) return;
    S.RunFrame(pipe_handle);
    CallbackMsg m;
    while (S.NextCallback(pipe_handle, &m)) {
        if (m.callback == CB_CALL_COMPLETED) {
            const CallCompleted *c = (const CallCompleted *)m.param;
            uint8_t buf[512];
            int failed = 0;
            if (c->param_size <= sizeof buf && S.CallResult(pipe_handle, c->call, buf, (int)c->param_size, c->callback, &failed) && !failed)
                on_callback(c->callback, buf);
            if (c->call == pending_call) pending_call = 0;
        } else on_callback(m.callback, m.param);
        S.FreeCallback(pipe_handle);
    }
}

static int wait_for(volatile u64 *call, DWORD ms) {   /* until an API call's result came, or ms passed */
    for (DWORD t0 = GetTickCount(); *call && GetTickCount() - t0 < ms;) { steam_pump(); Sleep(10); }
    return !*call;
}

/* ---- the bridge ------------------------------------------------------------------------------ */
#define PEERS 16
static struct { u64 id; SOCKET s; u64 in, out; DWORD seen; } peers[PEERS];   /* host: one local socket per player */
static SOCKET game_sock;                       /* joiner: the game's relay address */
static struct sockaddr_in game_addr;
static int have_game_addr, hosting;
static u_short relay_port;
static u64 host_id;
static u64 sent, received;

static int member_allowed(u64 id) {
    if (udp_test || !lobby) return 1;
    int n = S.LobbyMembers(mm, lobby);
    for (int i = 0; i < n; i++) if (S.LobbyMember(mm, lobby, i) == id) return 1;
    return 0;
}

static SOCKET udp_socket(u_short port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = {AF_INET, htons(port)};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s == INVALID_SOCKET || bind(s, (struct sockaddr *)&a, sizeof a)) { closesocket(s); return INVALID_SOCKET; }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    int big = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&big, sizeof big);
    return s;
}

static void arrived(u64 from, const char *data, int len) {
    received++;
    if (!hosting) {   /* from the host, for the game */
        if (from != host_id || !have_game_addr) return;
        sendto(game_sock, data, len, 0, (struct sockaddr *)&game_addr, sizeof game_addr);
        return;
    }
    if (!member_allowed(from)) return;
    int k = -1;
    for (int i = 0; i < PEERS; i++) if (peers[i].s && peers[i].id == from) { k = i; break; }
    if (k < 0) {
        for (int i = 0; i < PEERS && k < 0; i++) if (!peers[i].s) k = i;
        if (k < 0) return;
        SOCKET s = udp_socket(0);
        if (s == INVALID_SOCKET) return;
        struct sockaddr_in relay = {AF_INET, htons(relay_port)};
        relay.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        connect(s, (struct sockaddr *)&relay, sizeof relay);
        peers[k].id = from; peers[k].s = s; peers[k].in = peers[k].out = 0;
        SAY( "steam: player %llu connected\n", (unsigned long long)from);
    }
    peers[k].seen = GetTickCount();
    peers[k].in++;
    send(peers[k].s, data, len, 0);
}

static void quote_append(char *out, size_t n, const char *arg) {
    size_t l = strlen(out);
    snprintf(out + l, n - l, "%s\"%s\"", l ? " " : "", arg);
}

/* The environment as it was before Steam started in this process. SteamAPI_Init sets variables for
 * "its game" (this bridge): Steam's Vulkan layers (ENABLE_VK_LAYER_VALVE_steam_fossilize_1 and
 * _overlay_1: the pipeline recorder kept DXVK's shader workers busy, so the game stayed on
 * "preparing match shaders", and the overlay hooks the game), SDL's list of controllers to ignore
 * (Sony's among them: the game lost the PS4 controller), the app id. The game gets this copy, the
 * bridge's own netplay settings, and both layers turned off. */
static char *env_before;

static void snapshot_environment(void) {
    char *block = GetEnvironmentStringsA(), *end = block;
    while (*end) end += strlen(end) + 1;
    env_before = malloc(end - block + 1);
    memcpy(env_before, block, end - block + 1);
    FreeEnvironmentStringsA(block);
}

static int by_name(const void *a, const void *b) { return _stricmp(*(char *const *)a, *(char *const *)b); }

static char *game_environment(void) {
    static const char *const ours[] = {"SSBU_NET_NAME", "SSBU_NET_DIRECT", "SSBU_NET_UPNP", "SSBU_NET_HOST", "SSBU_NET_BIND",
                                       "SSBU_NET_PORT", "SSBU_NET_ROOM", "SSBU_NET_RELAY"};
    static char extra[16][512];
    char *entries[4096];
    int n = 0, k = 0;
    for (char *e = env_before; *e && n < 4000; e += strlen(e) + 1) {
        int skip = 0;
        for (size_t i = 0; i < sizeof ours / sizeof *ours && !skip; i++) {
            size_t l = strlen(ours[i]);
            skip = !_strnicmp(e, ours[i], l) && e[l] == '=';
        }
        if (!skip && _strnicmp(e, "DISABLE_VK_LAYER_VALVE_steam_", 29)) entries[n++] = e;
    }
    for (size_t i = 0; i < sizeof ours / sizeof *ours; i++) {
        char value[480];
        if (GetEnvironmentVariableA(ours[i], value, sizeof value)) {
            snprintf(extra[k], sizeof extra[k], "%s=%s", ours[i], value);
            entries[n++] = extra[k++];
        }
    }
    entries[n++] = "DISABLE_VK_LAYER_VALVE_steam_fossilize_1=1";
    entries[n++] = "DISABLE_VK_LAYER_VALVE_steam_overlay_1=1";
    qsort(entries, n, sizeof *entries, by_name);   /* (CreateProcess wants the block sorted) */
    size_t size = 1;
    for (int i = 0; i < n; i++) size += strlen(entries[i]) + 1;
    char *block = malloc(size), *at = block;
    for (int i = 0; i < n; i++) { size_t l = strlen(entries[i]) + 1; memcpy(at, entries[i], l); at += l; }
    *at = 0;
    return block;
}

static HANDLE start_game(int argc, char **argv) {
    char cmd[8192] = "";
    for (int i = 0; i < argc; i++) quote_append(cmd, sizeof cmd, argv[i]);
    STARTUPINFOA si = {sizeof si};
    PROCESS_INFORMATION pi;
    char *env = env_before ? game_environment() : NULL;
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, env, NULL, &si, &pi);
    free(env);
    if (!ok) {
        SAY( "steam: cannot start the game: %s\n", cmd);
        return NULL;
    }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

static void random_code(char *out) {
    static const char letters[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    srand(GetTickCount() ^ GetCurrentProcessId());
    for (int i = 0; i < 6; i++) out[i] = letters[rand() % (sizeof letters - 1)];
    out[6] = 0;
}

static u64 find_lobby(const char *build, const char *code) {
    if (code && *code) {   /* by room code */
        S.AddStringFilter(mm, "neosmash", build, 0);
        S.AddStringFilter(mm, "code", code, 0);
        S.AddDistanceFilter(mm, 3);   /* worldwide */
        match_count = -1;
        pending_call = S.RequestLobbyList(mm);
        if (!wait_for(&pending_call, 15000) || match_count < 1) return 0;
        return S.LobbyByIndex(mm, 0);
    }
    int n = S.FriendCount(friends, 4);   /* friends playing (Spacewar) in a lobby */
    u64 found[64];
    int k = 0;
    for (int i = 0; i < n && k < 64; i++) {
        u64 f = S.FriendByIndex(friends, i, 4);
        FriendGame g;
        memset(&g, 0, sizeof g);
        if (S.FriendGamePlayed(friends, f, &g) && (g.game_id & 0xFFFFFF) == SPACEWAR && g.lobby) {
            printf("  %d. %s\n", k + 1, S.FriendName(friends, f));
            found[k++] = g.lobby;
        }
    }
    if (!k) return 0;
    int pick = 1;
    if (k > 1) {
        printf("Which friend's room [1]: ");
        fflush(stdout);
        char line[16];
        if (fgets(line, sizeof line, stdin) && atoi(line) >= 1 && atoi(line) <= k) pick = atoi(line);
    }
    return found[pick - 1];
}

/* ---- the shared lobby (Play SSBU Steam.bat) ---------------------------------------------------
 * Everyone joins one public Steam lobby for this game build and region ("au": NeoSmash Australia),
 * made by whoever comes first. Its owner's bridge runs the online lobby server (tools/ssbu_relay.py,
 * on 127.0.0.1, SSBU_STEAM_RELAY_CMD says how) and gives every member - itself too - a local socket
 * of its own into it, so the server sees one address per player. Every game talks to its own
 * bridge on 127.0.0.1; the bridge passes that on to the owner's server, over Steam unless it is the
 * owner. Each player's Local Wireless rooms are listed to all, as in the online lobby. When the owner
 * leaves, Steam names another, whose bridge starts the server; the games notice their lobby went
 * quiet and join again (runtime/net_relay.c), so rooms in progress end but the lobby goes on. */
#define LOBBY_MAX 250
static struct { u64 id; SOCKET s; DWORD seen; } lpeers[LOBBY_MAX];   /* owner: a socket into the server per member */
static u64 me_id, server_owner;
static HANDLE server_proc, server_job;
static u_short server_port;

static void stop_server(void) {
    if (server_proc) { TerminateProcess(server_proc, 0); CloseHandle(server_proc); server_proc = NULL; }
    for (int i = 0; i < LOBBY_MAX; i++)
        if (lpeers[i].s) { closesocket(lpeers[i].s); memset(&lpeers[i], 0, sizeof lpeers[i]); }
}

static int start_server(void) {
    const char *base = getenv("SSBU_STEAM_RELAY_CMD");
    if (!base || !*base) { SAY("steam: cannot run the lobby here (SSBU_STEAM_RELAY_CMD is not set)\n"); return 0; }
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "%s --bind 127.0.0.1 --port %u --per-ip %d --stats 60", base, server_port, LOBBY_MAX);
    if (!server_job) {   /* (the server ends with this bridge, however it ends) */
        server_job = CreateJobObjectA(NULL, NULL);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
        memset(&li, 0, sizeof li);
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(server_job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    STARTUPINFOA si = {sizeof si};
    PROCESS_INFORMATION pi;
    char *env = game_environment();
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_SUSPENDED, env, NULL, &si, &pi);
    free(env);
    if (!ok) { SAY("steam: cannot start the lobby server: %s\n", cmd); return 0; }
    AssignProcessToJobObject(server_job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    server_proc = pi.hProcess;
    Sleep(1500);   /* (its socket bound before the first packet) */
    SAY("steam: this PC now runs the lobby for everyone in it\n");
    return 1;
}

/* (owner) a member's datagram into the server, through that member's own socket */
static void to_server(u64 who, const char *data, int len) {
    int k = -1, free_slot = -1;
    for (int i = 0; i < LOBBY_MAX; i++) {
        if (lpeers[i].s && lpeers[i].id == who) { k = i; break; }
        if (!lpeers[i].s && free_slot < 0) free_slot = i;
    }
    if (k < 0) {
        if (free_slot < 0) return;
        SOCKET s = udp_socket(0);
        if (s == INVALID_SOCKET) return;
        struct sockaddr_in a = {AF_INET, htons(server_port)};
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        connect(s, (struct sockaddr *)&a, sizeof a);
        k = free_slot;
        lpeers[k].id = who; lpeers[k].s = s;
    }
    lpeers[k].seen = GetTickCount();
    send(lpeers[k].s, data, len, 0);
}

static void lobby_arrived(u64 from, const char *data, int len) {
    received++;
    if (server_owner == me_id) { if (member_allowed(from)) to_server(from, data, len); }
    else if (from == server_owner && have_game_addr) sendto(game_sock, data, len, 0, (struct sockaddr *)&game_addr, sizeof game_addr);
}

/* the shared lobby of this build and region with the lowest id (everyone picks the same one), or 0 */
static u64 find_shared(const char *build, const char *region) {
    S.AddStringFilter(mm, "neosmash_lobby", build, 0);
    S.AddStringFilter(mm, "region", region, 0);
    S.AddDistanceFilter(mm, 3);
    match_count = -1;
    pending_call = S.RequestLobbyList(mm);
    if (!wait_for(&pending_call, 15000) || match_count < 1) return 0;
    u64 best = 0;
    for (int i = 0; i < match_count; i++) {
        u64 l = S.LobbyByIndex(mm, i);
        if (S.LobbyMembers(mm, l) > 0 && (!best || l < best)) best = l;
    }
    return best;
}

static int enter_lobby(u64 l) {
    joined_lobby = -1;
    pending_call = S.JoinLobby(mm, l);
    for (DWORD t0 = GetTickCount(); joined_lobby < 0 && GetTickCount() - t0 < 15000;) { steam_pump(); Sleep(10); }
    return joined_lobby == 1;
}

static int run_lobby(const char *build, const char *region, int port, int argc, char **argv) {
    char title[64];
    snprintf(title, sizeof title, "NeoSmash %s", !_stricmp(region, "au") ? "Australia" : region);
    me_id = S.MySteamID(S.User());
    server_port = 39421;
    printf("Looking for the %s lobby...\n", title);
    fflush(stdout);
    u64 l = find_shared(build, region);
    if (l && !enter_lobby(l)) { SAY("steam: could not enter the %s lobby (full?); making another\n", title); l = 0; }
    if (!l) {
        lobby = 0;
        pending_call = S.CreateLobby(mm, 2, LOBBY_MAX);   /* public: everyone of this build finds it */
        if (!wait_for(&pending_call, 15000) || !lobby) { SAY("steam: the lobby could not be made\n"); return 1; }
        S.SetLobbyData(mm, lobby, "neosmash_lobby", build);
        S.SetLobbyData(mm, lobby, "region", region);
        S.SetLobbyData(mm, lobby, "name", title);
        Sleep(2000);   /* someone else may have made one at the same moment: everyone keeps the oldest */
        u64 other = find_shared(build, region);
        if (other && other < lobby) {
            S.LeaveLobby(mm, lobby);
            lobby = 0;
            if (!enter_lobby(other)) { SAY("steam: could not enter the %s lobby\n", title); return 1; }
        }
    }
    printf("In the %s lobby: %d player%s. In the game: Local Wireless - create a room, or pick one from the Rooms List.\n",
           title, S.LobbyMembers(mm, lobby), S.LobbyMembers(mm, lobby) == 1 ? "" : "s");
    fflush(stdout);
    S.SetRichPresence(friends, "status", title);

    u_short gport = (u_short)(port ? port : 39430);
    game_sock = udp_socket(gport);
    if (game_sock == INVALID_SOCKET) { SAY("steam: local port %d in use\n", gport); return 1; }
    char value[64];
    snprintf(value, sizeof value, "127.0.0.1:%d", gport);
    SetEnvironmentVariableA("SSBU_NET_RELAY", value);
    snprintf(value, sizeof value, "steam-%s", region);
    SetEnvironmentVariableA("SSBU_NET_ROOM", value);
    SetEnvironmentVariableA("SSBU_NET_HOST", NULL);
    SetEnvironmentVariableA("SSBU_NET_BIND", NULL);
    HANDLE game = argc ? start_game(argc, argv) : NULL;
    if (argc && !game) return 1;

    DWORD last_owner_check = 0, last_report = GetTickCount();
    char buf[65536];
    for (;;) {
        steam_pump();
        if (GetTickCount() - last_owner_check > 250) {   /* who runs the server: the lobby's owner */
            last_owner_check = GetTickCount();
            u64 owner = S.LobbyOwner(mm, lobby);
            if (owner && owner != server_owner) {
                if (server_owner == me_id) stop_server();
                server_owner = owner;
                if (owner == me_id) { if (!start_server()) break; }
                else fprintf(stderr, "steam: the lobby runs on %s's PC\n", S.FriendName(friends, owner));
            }
        }
        transport_poll(lobby_arrived);
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(game_sock, &rd);
        if (server_owner == me_id)
            for (int i = 0; i < LOBBY_MAX; i++) if (lpeers[i].s && rd.fd_count < FD_SETSIZE) FD_SET(lpeers[i].s, &rd);
        struct timeval tv = {0, 1000};
        select(0, &rd, NULL, NULL, &tv);
        struct sockaddr_in from;
        int fl = sizeof from, n;
        while ((n = recvfrom(game_sock, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl)) > 0) {   /* this PC's game */
            game_addr = from; have_game_addr = 1;
            if (server_owner == me_id) to_server(me_id, buf, n);
            else if (server_owner) { transport_send(server_owner, buf, n); sent++; }
            fl = sizeof from;
        }
        if (server_owner == me_id)
            for (int i = 0; i < LOBBY_MAX; i++) {   /* the server's answers, to each member */
                if (!lpeers[i].s) continue;
                while ((n = recv(lpeers[i].s, buf, sizeof buf, 0)) > 0) {
                    if (lpeers[i].id == me_id) { if (have_game_addr) sendto(game_sock, buf, n, 0, (struct sockaddr *)&game_addr, sizeof game_addr); }
                    else { transport_send(lpeers[i].id, buf, n); sent++; }
                }
                if (lpeers[i].id != me_id && GetTickCount() - lpeers[i].seen > 120000) {   /* gone two minutes */
                    NetIdentity id = identity(lpeers[i].id);
                    S.CloseSession(msgs, &id);
                    closesocket(lpeers[i].s);
                    memset(&lpeers[i], 0, sizeof lpeers[i]);
                }
            }
        if (server_proc && WaitForSingleObject(server_proc, 0) == WAIT_OBJECT_0) {
            SAY("steam: the lobby server stopped; starting it again\n");
            CloseHandle(server_proc); server_proc = NULL;
            if (!start_server()) break;
        }
        if (GetTickCount() - last_report > 30000) {
            fprintf(stderr, "steam: %s lobby, %d players, %s; %llu sent, %llu received\n", title, S.LobbyMembers(mm, lobby),
                    server_owner == me_id ? "running it here" : "through the owner's PC", (unsigned long long)sent, (unsigned long long)received);
            last_report = GetTickCount();
        }
        if (game && WaitForSingleObject(game, 0) == WAIT_OBJECT_0) break;
    }
    DWORD code_out = 0;
    if (game) GetExitCodeProcess(game, &code_out);
    stop_server();
    if (lobby) S.LeaveLobby(mm, lobby);
    S.Shutdown();
    return (int)code_out;
}

static int usage(void) {
    fprintf(stderr, "usage: neosmash_steam host|join|lobby|list|rooms [--code CODE] [--region au] [--room NAME] [--port N] --build ID "
                    "[--udp-test LOCAL:PEER] -- <game command>\n");
    return 2;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    const char *mode = argv[1], *code = "", *room = "steam", *build = "", *region = "au";
    int port = 0, test_local = 0, test_remote = 0, dash = argc;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--")) { dash = i + 1; break; }
        if (i + 1 >= argc) return usage();
        if (!strcmp(argv[i], "--code")) code = argv[++i];
        else if (!strcmp(argv[i], "--room")) room = argv[++i];
        else if (!strcmp(argv[i], "--region")) region = argv[++i];
        else if (!strcmp(argv[i], "--port")) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--build")) build = argv[++i];
        else if (!strcmp(argv[i], "--udp-test")) { udp_test = 1; sscanf(argv[++i], "%d:%d", &test_local, &test_remote); }
        else return usage();
    }
    hosting = !strcmp(mode, "host");
    if (!hosting && strcmp(mode, "join") && strcmp(mode, "list") && strcmp(mode, "rooms") && strcmp(mode, "lobby")) return usage();
    if (!*build) { fprintf(stderr, "steam: --build (the game's build id, version.txt) is needed\n"); return 2; }
    snapshot_environment();   /* (before Steam adds its own: see game_environment) */
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    timeBeginPeriod(1);
    if (udp_test) {
        test_sock = udp_socket((u_short)test_local);
        test_peer = (u64)test_remote;
        if (test_sock == INVALID_SOCKET) { SAY( "steam: --udp-test port %d in use\n", test_local); return 1; }
    } else if (!steam_load()) return 1;

    if (!strcmp(mode, "list")) {
        u64 l = find_lobby(build, code);
        printf(l ? "found a room\n" : "no room found\n");
        S.Shutdown();
        return l ? 0 : 1;
    }
    if (!strcmp(mode, "rooms")) {   /* every open room of this game build: code, host, players */
        S.AddStringFilter(mm, "neosmash", build, 0);
        S.AddDistanceFilter(mm, 3);
        match_count = -1;
        pending_call = S.RequestLobbyList(mm);
        if (!wait_for(&pending_call, 15000)) { SAY("steam: no answer from Steam's lobby list\n"); return 1; }
        printf("%d room%s for this game build:\n", match_count, match_count == 1 ? "" : "s");
        for (int i = 0; i < match_count; i++) {
            u64 l = S.LobbyByIndex(mm, i);
            printf("  code %-8s host %-24s %d player%s\n", S.GetLobbyData(mm, l, "code"), S.GetLobbyData(mm, l, "host"),
                   S.LobbyMembers(mm, l), S.LobbyMembers(mm, l) == 1 ? "" : "s");
        }
        S.Shutdown();
        return 0;
    }

    char name[64];
    snprintf(name, sizeof name, "%s", getenv("SSBU_NET_NAME") ? getenv("SSBU_NET_NAME") : udp_test ? "Player" : S.PersonaName(friends));
    SetEnvironmentVariableA("SSBU_NET_NAME", name);
    SetEnvironmentVariableA("SSBU_NET_DIRECT", "0");   /* (Steam already finds the direct path) */
    SetEnvironmentVariableA("SSBU_NET_UPNP", "0");
    if (!strcmp(mode, "lobby")) {
        if (udp_test) { SAY("steam: the shared lobby needs Steam (no --udp-test)\n"); return 2; }
        return run_lobby(build, region, port, dash < argc ? argc - dash : 0, argv + dash);
    }
    char value[64];
    if (hosting) {
        relay_port = (u_short)(port ? port : 39420);
        char generated[8];
        if (!*code) { random_code(generated); code = generated; }
        if (!udp_test) {
            pending_call = S.CreateLobby(mm, 2, 8);   /* public, so the code finds it; members only get in */
            if (!wait_for(&pending_call, 15000) || !lobby) { SAY( "steam: no lobby\n"); return 1; }
            S.SetLobbyData(mm, lobby, "neosmash", build);
            S.SetLobbyData(mm, lobby, "code", code);
            S.SetLobbyData(mm, lobby, "room", room);
            S.SetLobbyData(mm, lobby, "host", name);
            S.SetRichPresence(friends, "status", "NeoSmash: hosting a room");
        }
        printf("\nRoom code: %s   (friends join with Join SSBU Steam.bat and this code)\n\n", code);
        fflush(stdout);
        SetEnvironmentVariableA("SSBU_NET_HOST", "1");
        SetEnvironmentVariableA("SSBU_NET_BIND", "127.0.0.1");
        snprintf(value, sizeof value, "%d", relay_port);
        SetEnvironmentVariableA("SSBU_NET_PORT", value);
        SetEnvironmentVariableA("SSBU_NET_ROOM", room);
        SetEnvironmentVariableA("SSBU_NET_RELAY", NULL);
    } else {
        if (udp_test) host_id = test_peer;
        else {
            printf("Looking for the room%s%s...\n", *code ? " " : " among friends", code);
            u64 l = find_lobby(build, code);
            if (!l) { SAY( "steam: no room found%s%s\n", *code ? " with code " : "", code); return 1; }
            joined_lobby = -1;
            pending_call = S.JoinLobby(mm, l);
            for (DWORD t0 = GetTickCount(); joined_lobby < 0 && GetTickCount() - t0 < 15000;) { steam_pump(); Sleep(10); }
            if (joined_lobby != 1) { SAY( "steam: could not join the room's lobby\n"); return 1; }
            const char *theirs = S.GetLobbyData(mm, lobby, "neosmash");
            if (strcmp(theirs, build)) { SAY( "steam: that room is another game version (%s)\n", theirs); return 1; }
            room = _strdup(S.GetLobbyData(mm, lobby, "room"));
            host_id = S.LobbyOwner(mm, lobby);
            if (host_id == S.MySteamID(S.User())) {
                /* Steam does not carry messages from an account to itself: a second game here would
                 * wait for a host it can never reach */
                SAY("steam: that room is hosted by this same Steam account (%s). Steam cannot connect an account\n"
                    "to itself: join from another PC signed in to another Steam account.\n", S.PersonaName(friends));
                S.LeaveLobby(mm, lobby);
                S.Shutdown();
                return 1;
            }
            printf("Joined %s's room.\n", S.GetLobbyData(mm, lobby, "host"));
            S.SetRichPresence(friends, "status", "NeoSmash: in a friend's room");
        }
        game_sock = udp_socket((u_short)(port ? port : 39430));
        if (game_sock == INVALID_SOCKET) { SAY( "steam: local port %d in use\n", port ? port : 39430); return 1; }
        snprintf(value, sizeof value, "127.0.0.1:%d", port ? port : 39430);
        SetEnvironmentVariableA("SSBU_NET_RELAY", value);
        SetEnvironmentVariableA("SSBU_NET_ROOM", room);
        SetEnvironmentVariableA("SSBU_NET_HOST", NULL);
    }
    (void)lobby_owner; (void)pending_kind;

    HANDLE game = dash < argc ? start_game(argc - dash, argv + dash) : NULL;
    if (dash < argc && !game) return 1;
    DWORD last_report = GetTickCount();
    for (;;) {
        steam_pump();
        transport_poll(arrived);
        fd_set rd;
        FD_ZERO(&rd);
        if (hosting) { for (int i = 0; i < PEERS; i++) if (peers[i].s) FD_SET(peers[i].s, &rd); }
        else FD_SET(game_sock, &rd);
        if (udp_test) FD_SET(test_sock, &rd);
        struct timeval tv = {0, 1000};
        if (rd.fd_count) select(0, &rd, NULL, NULL, &tv); else Sleep(1);
        char buf[65536];
        if (hosting) {
            for (int i = 0; i < PEERS; i++) {
                if (!peers[i].s) continue;
                int n;
                while ((n = recv(peers[i].s, buf, sizeof buf, 0)) > 0) { transport_send(peers[i].id, buf, n); peers[i].out++; sent++; }
                if (GetTickCount() - peers[i].seen > 120000) {   /* gone two minutes: its relay session has ended */
                    SAY( "steam: player %llu left\n", (unsigned long long)peers[i].id);
                    if (!udp_test) { NetIdentity id = identity(peers[i].id); S.CloseSession(msgs, &id); }
                    closesocket(peers[i].s);
                    memset(&peers[i], 0, sizeof peers[i]);
                }
            }
        } else {
            struct sockaddr_in from;
            int fl = sizeof from, n;
            while ((n = recvfrom(game_sock, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl)) > 0) {
                game_addr = from; have_game_addr = 1;
                transport_send(host_id, buf, n);
                sent++;
                fl = sizeof from;
            }
        }
        if (GetTickCount() - last_report > 10000) {
            int np = 0;
            for (int i = 0; i < PEERS; i++) np += peers[i].s != 0;
            fprintf(stderr, "steam: %s; %llu sent, %llu received%s\n", hosting ? "hosting" : "joined",
                    (unsigned long long)sent, (unsigned long long)received, hosting ? (np == 1 ? ", 1 player" : np ? ", players" : ", nobody yet") : "");
            last_report = GetTickCount();
        }
        if (game && WaitForSingleObject(game, 0) == WAIT_OBJECT_0) break;
    }
    DWORD code_out = 0;
    GetExitCodeProcess(game, &code_out);
    if (!udp_test) {
        if (lobby) S.LeaveLobby(mm, lobby);
        S.Shutdown();
    }
    return (int)code_out;
}
