// goldfork's offline stand-in for libsteam_api.so (Steamworks ~1.2x, as used by
// Half-Life steam_legacy). The headless game never talks to Steam: init and
// login succeed, the user and server have fixed made-up Steam IDs, and nothing
// can install a crash reporter. Written from the public interface layouts in
// open-steamworks; no Valve code. Every method that goldfork doesn't model returns
// 0; FORK_STEAMSTUB_LOG=1 reports the first call of each.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))

typedef struct { uint64_t id; } steamid_t;   // CSteamID, returned in memory
static const uint64_t USER_ID = 0x0110000100000001ull;     // individual, public universe
static const uint64_t SERVER_ID = 0x0140000000000001ull;   // anonymous game server

enum { USER, FRIENDS, APPS, HTTP, SERVER, UTILS, OTHER, NIFACES };
static const char *iface_name[NIFACES] = { "SteamUser016", "SteamFriends013", "SteamApps005", "SteamHTTP002", "SteamGameServer011", "SteamUtils", "other" };

static void note(int i, int n) {
	static unsigned char seen[NIFACES][128];
	static int log = -1;
	if (log < 0) log = getenv("FORK_STEAMSTUB_LOG") != NULL;
	if (log && !seen[i][n]) { seen[i][n] = 1; fprintf(stderr, "[steamstub] %s slot %d\n", iface_name[i], n); }
}

// Fallback slots: cdecl, so the caller pops any arguments; 0 in edx:eax.
#define FALLBACK(i, n) static uint64_t slot_##i##_##n(void) { note(i, n); return 0; }
#include "steamstub_slots.h"
SLOTS(FALLBACK, 0) SLOTS(FALLBACK, 1) SLOTS(FALLBACK, 2) SLOTS(FALLBACK, 3) SLOTS(FALLBACK, 4) SLOTS(FALLBACK, 5) SLOTS(FALLBACK, 6)
#define ENTRY(i, n) (void *)slot_##i##_##n,
static void *vtbl[NIFACES][128] = {
	{ SLOTS(ENTRY, 0) }, { SLOTS(ENTRY, 1) }, { SLOTS(ENTRY, 2) }, { SLOTS(ENTRY, 3) },
	{ SLOTS(ENTRY, 4) }, { SLOTS(ENTRY, 5) }, { SLOTS(ENTRY, 6) },
};
static struct { void **vtbl; } object[NIFACES];

// Modelled methods (`self` is the C++ this pointer, passed first on i386).
static int ret1(void *self) { (void)self; return 1; }
static const char *english(void *self) { (void)self; return "english"; }
static const char *persona(void *self) { (void)self; return "goldfork"; }
static steamid_t user_id(void *self) { (void)self; return (steamid_t){ USER_ID }; }
static steamid_t server_id(void *self) { (void)self; return (steamid_t){ SERVER_ID }; }
static steamid_t nil_id2(void *self, int a, int b) { (void)self; (void)a; (void)b; return (steamid_t){ 0 }; }

static int write_ticket(void *blob, int max) {
	if (!blob || max < 8) return 0;
	memcpy(blob, &USER_ID, 8);
	return 8;
}
// int InitiateGameConnection(void *blob, int max, CSteamID server, uint32 ip, uint16 port, bool secure)
static int initiate_game_connection(void *self, void *blob, int max, uint64_t server, uint32_t ip, uint32_t port, int secure) {
	(void)self; (void)server; (void)ip; (void)port; (void)secure;
	return write_ticket(blob, max);
}
// HAuthTicket GetAuthSessionTicket(void *ticket, int max, uint32 *len)
static uint32_t auth_ticket(void *self, void *ticket, int max, uint32_t *len) {
	(void)self;
	int n = write_ticket(ticket, max);
	if (len) *len = (uint32_t)n;
	return 1;
}
// bool SendUserConnectAndAuthenticate(uint32 ip, const void *blob, uint32 len, CSteamID *user)
static int connect_and_authenticate(void *self, uint32_t ip, const void *blob, uint32_t len, steamid_t *user) {
	(void)self; (void)ip; (void)blob; (void)len;
	if (user) user->id = USER_ID;
	return 1;
}

__attribute__((constructor)) static void init(void) {
	for (int i = 0; i < NIFACES; i++) object[i].vtbl = vtbl[i];
	void **u = vtbl[USER], **s = vtbl[SERVER], **a = vtbl[APPS], **f = vtbl[FRIENDS];
	u[0] = ret1; u[1] = ret1; u[2] = user_id; u[3] = initiate_game_connection; u[13] = auth_ticket;
	s[0] = ret1; s[8] = ret1; s[10] = server_id; s[24] = connect_and_authenticate; s[27] = ret1;
	a[0] = ret1; a[4] = english; a[5] = english; a[6] = ret1; a[19] = ret1;
	f[0] = persona; f[4] = nil_id2;
	// Everything else, including BeginAuthSession and UserHasLicenseForApp, returns 0 = OK.
}

// ---------------------------------------------------------------- flat API
EXPORT int SteamAPI_Init(void) { return 1; }
EXPORT int SteamAPI_InitSafe(void) { return 1; }
EXPORT int SteamAPI_IsSteamRunning(void) { return 1; }
EXPORT int SteamAPI_RestartAppIfNecessary(uint32_t app) { (void)app; return 0; }
EXPORT void SteamAPI_Shutdown(void) {}
EXPORT void SteamAPI_RunCallbacks(void) {}
EXPORT void SteamAPI_RegisterCallback(void *cb, int id) { (void)cb; (void)id; }
EXPORT void SteamAPI_UnregisterCallback(void *cb) { (void)cb; }
EXPORT void SteamAPI_RegisterCallResult(void *cb, uint64_t call) { (void)cb; (void)call; }
EXPORT void SteamAPI_UnregisterCallResult(void *cb, uint64_t call) { (void)cb; (void)call; }
// Crash reporting: deliberately nothing.
EXPORT void SteamAPI_SetBreakpadAppID(uint32_t app) { (void)app; }
EXPORT void SteamAPI_UseBreakpadCrashHandler(const char *v, const char *d, const char *t, int full, void *ctx, void *pre) { (void)v; (void)d; (void)t; (void)full; (void)ctx; (void)pre; }
EXPORT void SteamAPI_WriteMiniDump(uint32_t code, void *info, uint32_t build) { (void)code; (void)info; (void)build; }
EXPORT void SteamAPI_SetMiniDumpComment(const char *msg) { (void)msg; }
EXPORT void SteamAPI_SetTryCatchCallbacks(int on) { (void)on; }
EXPORT int SteamAPI_GetHSteamPipe(void) { return 1; }
EXPORT int SteamAPI_GetHSteamUser(void) { return 1; }
EXPORT int GetHSteamPipe(void) { return 1; }
EXPORT int GetHSteamUser(void) { return 1; }
EXPORT int Steam_GetHSteamUserCurrent(void) { return 1; }
EXPORT const char *SteamAPI_GetSteamInstallPath(void) { return ""; }
EXPORT void Steam_RunCallbacks(int pipe, int server) { (void)pipe; (void)server; }
EXPORT void Steam_RegisterInterfaceFuncs(void *m) { (void)m; }
EXPORT const char *SteamRealPath(const char *p) { return p; }

EXPORT int SteamGameServer_Init(uint32_t ip, uint16_t sp, uint16_t gp, uint16_t qp, int mode, const char *v) { (void)ip; (void)sp; (void)gp; (void)qp; (void)mode; (void)v; return 1; }
EXPORT int SteamGameServer_InitSafe(uint32_t ip, uint16_t sp, uint16_t gp, uint16_t qp, int mode, const char *v) { return SteamGameServer_Init(ip, sp, gp, qp, mode, v); }
EXPORT void SteamGameServer_RunCallbacks(void) {}
EXPORT void SteamGameServer_Shutdown(void) {}
EXPORT int SteamGameServer_BSecure(void) { return 0; }
EXPORT uint64_t SteamGameServer_GetSteamID(void) { return SERVER_ID; }
EXPORT int SteamGameServer_GetHSteamPipe(void) { return 1; }
EXPORT int SteamGameServer_GetHSteamUser(void) { return 1; }
EXPORT uint32_t SteamGameServer_GetIPCCallCount(void) { return 0; }
EXPORT int SteamContentServer_Init(uint32_t a, uint32_t b) { (void)a; (void)b; return 0; }
EXPORT void SteamContentServer_RunCallbacks(void) {}
EXPORT void SteamContentServer_Shutdown(void) {}

// ---------------------------------------------------------------- interfaces
#define IFACE(name, i) EXPORT void *name(void) { return &object[i]; }
IFACE(SteamUser, USER) IFACE(SteamFriends, FRIENDS) IFACE(SteamApps, APPS) IFACE(SteamHTTP, HTTP)
IFACE(SteamGameServer, SERVER) IFACE(SteamUtils, UTILS) IFACE(SteamGameServerUtils, UTILS)
IFACE(SteamClient, OTHER) IFACE(SteamContentServer, OTHER) IFACE(SteamContentServerUtils, OTHER)
IFACE(SteamGameServerApps, APPS) IFACE(SteamGameServerHTTP, HTTP) IFACE(SteamGameServerNetworking, OTHER)
IFACE(SteamGameServerStats, OTHER) IFACE(SteamMatchmaking, OTHER) IFACE(SteamMatchmakingServers, OTHER)
IFACE(SteamNetworking, OTHER) IFACE(SteamRemoteStorage, OTHER) IFACE(SteamScreenshots, OTHER)
IFACE(SteamUnifiedMessages, OTHER) IFACE(SteamUserStats, OTHER)
