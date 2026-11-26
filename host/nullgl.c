// goldfork null renderer: no-op OpenGL plus the SDL window/GL calls GoldSrc uses.
//
// Built twice from this file:
//   libGL.so.1     (-DNULLGL_LIBGL)  satisfies hw.so's NEEDED libGL.so.1
//   libgoldfork.so (LD_PRELOAD)      overrides SDL window/GL functions
//
// Nothing here may touch game state: the engine only writes to GL, and the few
// values it reads back (strings, sizes, ids) are constants or counters.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef unsigned char GLboolean;

// ---------------------------------------------------------------- GL

static int gl_noop(void) { return 0; }  // cdecl: caller pops args

static GLuint next_name = 1;

static void fill_ints(GLenum pname, GLint *p) {
	switch (pname) {
	case 0x0BA2: /* GL_VIEWPORT */
	case 0x0C10: /* GL_SCISSOR_BOX */
		p[0] = 0; p[1] = 0; p[2] = 640; p[3] = 480; return;
	case 0x0D33: /* GL_MAX_TEXTURE_SIZE */ p[0] = 4096; return;
	case 0x84E2: /* GL_MAX_TEXTURE_UNITS */ p[0] = 4; return;
	default: p[0] = 0; return;
	}
}

static void ident16f(GLfloat *m) { memset(m, 0, 16 * sizeof *m); m[0] = m[5] = m[10] = m[15] = 1; }
static void ident16d(GLdouble *m) { memset(m, 0, 16 * sizeof *m); m[0] = m[5] = m[10] = m[15] = 1; }
static int is_matrix(GLenum p) { return p == 0x0BA6 || p == 0x0BA7 || p == 0x0BA8; }

static const unsigned char *gl_GetString(GLenum name) {
	switch (name) {
	case 0x1F00: return (const unsigned char *)"goldfork";         // GL_VENDOR
	case 0x1F01: return (const unsigned char *)"goldfork null renderer"; // GL_RENDERER
	case 0x1F02: return (const unsigned char *)"1.2";              // GL_VERSION
	default:     return (const unsigned char *)"";                 // GL_EXTENSIONS
	}
}
static void gl_GetIntegerv(GLenum p, GLint *v) { fill_ints(p, v); }
static void gl_GetFloatv(GLenum p, GLfloat *v) {
	if (is_matrix(p)) { ident16f(v); return; }
	GLint i[4] = {0}; fill_ints(p, i);
	int n = (p == 0x0BA2 || p == 0x0C10) ? 4 : 1;
	for (int k = 0; k < n; k++) v[k] = (GLfloat)i[k];
}
static void gl_GetDoublev(GLenum p, GLdouble *v) {
	if (is_matrix(p)) { ident16d(v); return; }
	GLint i[4] = {0}; fill_ints(p, i);
	int n = (p == 0x0BA2 || p == 0x0C10) ? 4 : 1;
	for (int k = 0; k < n; k++) v[k] = (GLdouble)i[k];
}
static void gl_GetBooleanv(GLenum p, GLboolean *v) { (void)p; v[0] = 0; }
static void gl_GetIntOut(GLenum a, GLenum b, GLint *v) { (void)a; (void)b; v[0] = 0; }
static void gl_GetIntOut3(GLenum a, GLint b, GLenum c, GLint *v) { (void)a; (void)b; (void)c; v[0] = 0; }
static void gl_GenNames(GLsizei n, GLuint *ids) { for (GLsizei i = 0; i < n; i++) ids[i] = next_name++; }
static GLuint gl_GenLists(GLsizei n) { GLuint b = next_name; next_name += n; return b; }
static GLenum gl_CheckFramebufferStatus(GLenum t) { (void)t; return 0x8CD5; } // COMPLETE

struct gl_fn { const char *name; void *fn; };
static const struct gl_fn gl_special[] = {
	{"glGetString", (void *)gl_GetString},
	{"glGetIntegerv", (void *)gl_GetIntegerv},
	{"glGetFloatv", (void *)gl_GetFloatv},
	{"glGetDoublev", (void *)gl_GetDoublev},
	{"glGetBooleanv", (void *)gl_GetBooleanv},
	{"glGetTexParameteriv", (void *)gl_GetIntOut},
	{"glGetTexParameterfv", (void *)gl_GetIntOut},
	{"glGetTexEnviv", (void *)gl_GetIntOut},
	{"glGetTexEnvfv", (void *)gl_GetIntOut},
	{"glGetRenderbufferParameterivEXT", (void *)gl_GetIntOut},
	{"glGetTexLevelParameteriv", (void *)gl_GetIntOut3},
	{"glGetTexLevelParameterfv", (void *)gl_GetIntOut3},
	{"glGenTextures", (void *)gl_GenNames},
	{"glGenFramebuffersEXT", (void *)gl_GenNames},
	{"glGenRenderbuffersEXT", (void *)gl_GenNames},
	{"glGenLists", (void *)gl_GenLists},
	{"glCheckFramebufferStatusEXT", (void *)gl_CheckFramebufferStatus},
	{0, 0},
};

EXPORT void *fork_gl_lookup(const char *name) {
	for (const struct gl_fn *f = gl_special; f->name; f++)
		if (!strcmp(f->name, name)) return f->fn;
	return (void *)gl_noop;
}

#ifdef NULLGL_LIBGL
// Every GL entry point hw.so or BXT may link.
#include "gl_exports.h"
EXPORT void *glXGetProcAddressARB(const char *n) { return fork_gl_lookup(n); }
EXPORT void *glXGetProcAddress(const char *n) { return fork_gl_lookup(n); }
#else
// ---------------------------------------------------------------- SDL

#define SDL_WINDOW_FULLSCREEN 0x1
#define SDL_WINDOW_OPENGL 0x2

EXPORT void *SDL_CreateWindow(const char *title, int x, int y, int w, int h, uint32_t flags) {
	// hw.so is dlopen'ed with local scope, so SDL is not in the global search
	// order and RTLD_NEXT misses it: look it up through SDL's own handle.
	static void *(*real)(const char *, int, int, int, int, uint32_t);
	if (!real) {
		void *sdl = dlopen("libSDL2-2.0.so.0", RTLD_LAZY | RTLD_NOLOAD);
		if (sdl) real = dlsym(sdl, "SDL_CreateWindow");
		if (!real) { fprintf(stderr, "[nullgl] SDL_CreateWindow not found\n"); abort(); }
	}
	return real(title, x, y, w, h, flags & ~(SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN));
}

EXPORT void *SDL_GL_CreateContext(void *window) { (void)window; return (void *)0x1; }
EXPORT int SDL_GL_SetSwapInterval(int i) { (void)i; return 0; }
EXPORT void SDL_GL_SwapWindow(void *w) { (void)w; }
EXPORT int SDL_GL_SetAttribute(int attr, int v) { (void)attr; (void)v; return 0; }

EXPORT int SDL_GL_GetAttribute(int attr, int *v) {
	switch (attr) {
	case 0: case 1: case 2: *v = 8; break;  // R, G, B
	case 5: *v = 1; break;                  // DOUBLEBUFFER
	case 6: *v = 24; break;                 // DEPTH
	default: *v = 0;
	}
	return 0;
}

EXPORT void *SDL_GL_GetProcAddress(const char *name) {
	return fork_gl_lookup(name);
}
#endif
