// Engine and libc functions goldfork replaces: faster drop-ins for hot engine
// helpers, plus the clock, Steam callbacks and save paths of branches.
//
// hw.so binds every call to these through load-time relocations, so a
// preloaded definition replaces them without patching any Valve code.
// tests/bitexact.py and tests/equivalence.py check that results stay identical.

#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <link.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EXPORT __attribute__((visibility("default")))

// ---------------------------------------------------------------- zone memory
//
// The engine's zone allocator walks a block list on every call; console
// commands (which BXT issues every frame) allocate and free through it
// constantly. Z_Malloc returns zeroed memory; calloc does the same.
// No other module calls these, and hw.so never calls Z_TagMalloc from outside.

EXPORT void *Z_Malloc(int size) {
	void *p = calloc(1, size > 0 ? (size_t)size : 1);
	if (!p) { fputs("[goldfork] Z_Malloc: out of memory\n", stderr); abort(); }
	return p;
}

EXPORT void Z_Free(void *p) { free(p); }

// ---------------------------------------------------------------- strings
//
// glibc's strncasecmp folds through the locale's tolower table. The game runs
// in en_US.UTF-8 (or C), where only ASCII letters fold, so this is identical,
// including the returned difference.

static inline int fold(unsigned char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

EXPORT int strncasecmp(const char *a, const char *b, size_t n) {
	const unsigned char *s = (const unsigned char *)a, *t = (const unsigned char *)b;
	for (; n; n--, s++, t++) {
		int d = fold(*s) - fold(*t);
		if (d || !*s) return d;
	}
	return 0;
}

EXPORT int strcasecmp(const char *a, const char *b) {
	const unsigned char *s = (const unsigned char *)a, *t = (const unsigned char *)b;
	for (;; s++, t++) {
		int d = fold(*s) - fold(*t);
		if (d || !*s) return d;
	}
}

// ---------------------------------------------------------------- fork children
//
// Children skip Steam callbacks and the server heartbeat: they don't touch
// game state, and the parent's Steam threads don't exist in a child.

static int fork_child;

// ---------------------------------------------------------------- clock
//
// The parent sits inside bxt_fork_serve while batches run, so the wall clock
// keeps moving while its game is frozen. A child forked minutes later would
// start with a huge real-time jump, and the engine's network timeouts drop
// the local client ("Server shutting down"). Children therefore see the clock
// continue from the session's first fork, as if forked at that moment.

// The next definition of a libc function, by exact version. Never plain
// dlsym: BXT's SPTLib hooks dlsym and its hook calls fopen, so a dlsym from
// inside an interposed fopen would recurse until the stack overflows.
static void *next_sym(const char *name, const char *version) {
	return dlvsym(RTLD_NEXT, name, version);
}

static int (*real_clock_gettime)(clockid_t, struct timespec *);
static long long first_fork_ns = -1, fork_ns, offset_ns;

static long long raw_ns(void) {
	struct timespec t;
	real_clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000ll + t.tv_nsec;
}

EXPORT int clock_gettime(clockid_t id, struct timespec *ts) {
	int r = real_clock_gettime(id, ts);
	if (r == 0 && offset_ns) {
		long long t = ts->tv_sec * 1000000000ll + ts->tv_nsec - offset_ns;
		ts->tv_sec = t / 1000000000ll;
		ts->tv_nsec = t % 1000000000ll;
	}
	return r;
}

static void before_fork(void) {
	fork_ns = raw_ns();
	if (first_fork_ns < 0) first_fork_ns = fork_ns;
}

static void in_child(void) {
	fork_child = 1;
	offset_ns = fork_ns - first_fork_ns;
}

__attribute__((constructor)) static void hooks_init(void) {
	real_clock_gettime = (int (*)(clockid_t, struct timespec *))next_sym("clock_gettime", "GLIBC_2.17");
	pthread_atfork(before_fork, NULL, in_child);
}

static void *steam_api(const char *name) {
	void *h = dlopen("libsteam_api.so", RTLD_LAZY | RTLD_NOLOAD);
	return h ? dlsym(h, name) : NULL;
}

static int find_hw(struct dl_phdr_info *info, size_t size, void *data) {
	(void)size;
	if (strstr(info->dlpi_name, "/hw.so")) { *(void **)data = dlopen(info->dlpi_name, RTLD_LAZY | RTLD_NOLOAD); return 1; }
	return 0;
}

static void *hw_sym(const char *name) {
	void *h = NULL;
	dl_iterate_phdr(find_hw, &h);
	return h ? dlsym(h, name) : NULL;
}

EXPORT void Steam_RunFrame(void) {
	static void (*real)(void);
	if (fork_child) return;
	if (!real) real = (void (*)(void))hw_sym("Steam_RunFrame");
	if (real) real();
}

EXPORT void SteamAPI_RunCallbacks(void) {
	static void (*real)(void);
	if (fork_child) return;
	if (!real) real = (void (*)(void))steam_api("SteamAPI_RunCallbacks");
	if (real) real();
}

EXPORT void SteamGameServer_RunCallbacks(void) {
	static void (*real)(void);
	if (fork_child) return;
	if (!real) real = (void (*)(void))steam_api("SteamGameServer_RunCallbacks");
	if (real) real();
}

// ---------------------------------------------------------------- private saves
//
// Children may save and load (ground-entity tricks need it), but they must not
// share the game's SAVE directory. In a child, every path with a SAVE/
// component is redirected to SAVE/.fork/<pid>/, which starts as a copy of the
// parent's saves, made on the child's first access. The parent's SAVE is never
// written by a child. Session removes SAVE/.fork after each batch.

static char save_private[4096];     // ".../SAVE/.fork/<pid>" once set up
static char save_shared[4096];      // ".../SAVE"

static FILE *(*real_fopen)(const char *, const char *);
static int (*real_open)(const char *, int, ...);
static int (*real_mkdir)(const char *, mode_t);
static DIR *(*real_opendir)(const char *);

static void resolve(void) {
	if (real_open) return;
	real_fopen = (FILE *(*)(const char *, const char *))next_sym("fopen", "GLIBC_2.1");
	real_open = (int (*)(const char *, int, ...))next_sym("open", "GLIBC_2.0");
	real_mkdir = (int (*)(const char *, mode_t))next_sym("mkdir", "GLIBC_2.0");
	real_opendir = (DIR *(*)(const char *))next_sym("opendir", "GLIBC_2.0");
}

// A truncated path would point at the wrong file: stop instead.
#define FIT(n, size) do { if ((n) < 0 || (size_t)(n) >= (size)) { fputs("[goldfork] path too long\n", stderr); abort(); } } while (0)

// Where SAVE ends in p (index just past "SAVE"), or -1 if p has no SAVE component.
static int save_component(const char *p) {
	for (const char *q = strstr(p, "SAVE"); q; q = strstr(q + 1, "SAVE")) {
		if ((q == p || q[-1] == '/') && (q[4] == '/' || q[4] == '\0'))
			return (int)(q - p) + 4;
	}
	return -1;
}

static void copy_saves(void) {
	DIR *d = real_opendir(save_shared);
	if (!d) return;
	struct dirent *e;
	char from[4096 + 256], to[4096 + 256], buf[65536];
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		FIT(snprintf(from, sizeof from, "%s/%s", save_shared, e->d_name), sizeof from);
		FIT(snprintf(to, sizeof to, "%s/%s", save_private, e->d_name), sizeof to);
		int in = real_open(from, O_RDONLY);
		if (in < 0) continue;
		int out = real_open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		ssize_t n;
		while (out >= 0 && (n = read(in, buf, sizeof buf)) > 0)
			if (write(out, buf, (size_t)n) != n) break;
		close(in);
		if (out >= 0) close(out);
	}
	closedir(d);
}

// Returns p, or the child's private equivalent in buf.
static const char *map_path(const char *p, char *buf, size_t size) {
	if (!fork_child || !p) return p;
	int end = save_component(p);
	if (end < 0 || strstr(p, "/.fork/")) return p;
	resolve();
	if (!save_private[0]) {
		FIT(snprintf(save_shared, sizeof save_shared, "%.*s", end, p), sizeof save_shared);
		FIT(snprintf(save_private, sizeof save_private, "%s/.fork", save_shared), sizeof save_private);
		real_mkdir(save_private, 0755);
		FIT(snprintf(save_private, sizeof save_private, "%s/.fork/%d", save_shared, (int)getpid()), sizeof save_private);
		real_mkdir(save_private, 0755);
		copy_saves();
	}
	FIT(snprintf(buf, size, "%s%s", save_private, p + end), size);
	return buf;
}

#define MAP(p) char mapped_##p[4096]; p = map_path(p, mapped_##p, sizeof mapped_##p)

EXPORT FILE *fopen(const char *path, const char *mode) {
	resolve(); MAP(path);
	return real_fopen(path, mode);
}

EXPORT FILE *fopen64(const char *path, const char *mode) {
	static FILE *(*real)(const char *, const char *);
	if (!real) real = (FILE *(*)(const char *, const char *))next_sym("fopen64", "GLIBC_2.1");
	MAP(path);
	return real(path, mode);
}

EXPORT int open(const char *path, int flags, ...) {
	resolve();
	mode_t mode = 0;
	if (flags & (O_CREAT | O_TMPFILE)) { va_list a; va_start(a, flags); mode = va_arg(a, mode_t); va_end(a); }
	MAP(path);
	return real_open(path, flags, mode);
}

EXPORT int open64(const char *path, int flags, ...) {
	static int (*real)(const char *, int, ...);
	if (!real) real = (int (*)(const char *, int, ...))next_sym("open64", "GLIBC_2.1");
	mode_t mode = 0;
	if (flags & (O_CREAT | O_TMPFILE)) { va_list a; va_start(a, flags); mode = va_arg(a, mode_t); va_end(a); }
	MAP(path);
	return real(path, flags, mode);
}

// The stat family: hw.so and filesystem_stdio.so bind the old __xstat entry
// points, compat symbols on a new glibc.
EXPORT int __xstat(int ver, const char *path, struct stat *st) {
	static int (*real)(int, const char *, struct stat *);
	if (!real) real = (int (*)(int, const char *, struct stat *))next_sym("__xstat", "GLIBC_2.0");
	MAP(path);
	return real(ver, path, st);
}

EXPORT int __xstat64(int ver, const char *path, struct stat64 *st) {
	static int (*real)(int, const char *, struct stat64 *);
	if (!real) real = (int (*)(int, const char *, struct stat64 *))next_sym("__xstat64", "GLIBC_2.2");
	MAP(path);
	return real(ver, path, st);
}

EXPORT int __lxstat(int ver, const char *path, struct stat *st) {
	static int (*real)(int, const char *, struct stat *);
	if (!real) real = (int (*)(int, const char *, struct stat *))next_sym("__lxstat", "GLIBC_2.0");
	MAP(path);
	return real(ver, path, st);
}

EXPORT int access(const char *path, int mode) {
	static int (*real)(const char *, int);
	if (!real) real = (int (*)(const char *, int))next_sym("access", "GLIBC_2.0");
	MAP(path);
	return real(path, mode);
}

EXPORT int mkdir(const char *path, mode_t mode) {
	resolve(); MAP(path);
	return real_mkdir(path, mode);
}

EXPORT DIR *opendir(const char *path) {
	resolve(); MAP(path);
	return real_opendir(path);
}

EXPORT int scandir(const char *path, struct dirent ***list, int (*sel)(const struct dirent *),
                   int (*cmp)(const struct dirent **, const struct dirent **)) {
	static int (*real)(const char *, struct dirent ***, int (*)(const struct dirent *),
	                   int (*)(const struct dirent **, const struct dirent **));
	if (!real) real = (int (*)(const char *, struct dirent ***, int (*)(const struct dirent *),
	                           int (*)(const struct dirent **, const struct dirent **)))next_sym("scandir", "GLIBC_2.0");
	MAP(path);
	return real(path, list, sel, cmp);
}

EXPORT int rename(const char *from, const char *to) {
	static int (*real)(const char *, const char *);
	if (!real) real = (int (*)(const char *, const char *))next_sym("rename", "GLIBC_2.0");
	MAP(from); MAP(to);
	return real(from, to);
}

EXPORT int unlink(const char *path) {
	static int (*real)(const char *);
	if (!real) real = (int (*)(const char *))next_sym("unlink", "GLIBC_2.0");
	MAP(path);
	return real(path);
}

EXPORT char *realpath(const char *path, char *resolved) {
	static char *(*real)(const char *, char *);
	if (!real) real = (char *(*)(const char *, char *))next_sym("realpath", "GLIBC_2.3");
	MAP(path);
	return real(path, resolved);
}
