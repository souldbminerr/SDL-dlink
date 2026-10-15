/* SDL-dlink smoke test (plain homebrew NRO).
 *
 * Validates dynamically linked SDL3: the host registers its own symbols,
 * loads sdl3.dnro from sdmc:/lib/ (plus neko3d.dnro, like the qlaunch
 * loader does), binds the SDL API via dlsym, then drives the deko3d
 * renderer on the adopted default NWindow. Screen cycles red/green/blue;
 * every step is logged to sdmc:/sdl3smoke.log for headless diagnosis.
 */

#include <switch.h>

#include <dlink/dlink.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_properties.h>

#include <stdio.h>
#include <stdarg.h>

static FILE *g_log;

static void logline(const char *fmt, ...)
{
    if (!g_log)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

typedef bool (*fnInit)(SDL_InitFlags);
typedef const char *(*fnGetError)(void);
typedef SDL_PropertiesID (*fnCreateProperties)(void);
typedef bool (*fnSetPointerProperty)(SDL_PropertiesID, const char *, void *);
typedef bool (*fnSetNumberProperty)(SDL_PropertiesID, const char *, Sint64);
typedef void (*fnDestroyProperties)(SDL_PropertiesID);
typedef SDL_Window *(*fnCreateWindowWithProperties)(SDL_PropertiesID);
typedef void (*fnDestroyWindow)(SDL_Window *);
typedef SDL_Renderer *(*fnCreateRenderer)(SDL_Window *, const char *);
typedef void (*fnDestroyRenderer)(SDL_Renderer *);
typedef bool (*fnSetRenderDrawColor)(SDL_Renderer *, Uint8, Uint8, Uint8, Uint8);
typedef bool (*fnRenderClear)(SDL_Renderer *);
typedef bool (*fnRenderPresent)(SDL_Renderer *);
typedef void (*fnDelay)(Uint32);
typedef void (*fnQuit)(void);

static struct
{
    fnInit Init;
    fnGetError GetError;
    fnCreateProperties CreateProperties;
    fnSetPointerProperty SetPointerProperty;
    fnSetNumberProperty SetNumberProperty;
    fnDestroyProperties DestroyProperties;
    fnCreateWindowWithProperties CreateWindowWithProperties;
    fnDestroyWindow DestroyWindow;
    fnCreateRenderer CreateRenderer;
    fnDestroyRenderer DestroyRenderer;
    fnSetRenderDrawColor SetRenderDrawColor;
    fnRenderClear RenderClear;
    fnRenderPresent RenderPresent;
    fnDelay Delay;
    fnQuit Quit;
} S;

#define BIND(h, field, type, sym)                                  \
    do {                                                           \
        S.field = (type)dlsym((h), (sym));                         \
        if (!S.field) {                                            \
            logline("BIND FAIL %s: %s", (sym), dlerror());         \
            ok = false;                                            \
        }                                                          \
    } while (0)

int main(int argc, char **argv)
{
    void *hneko = NULL;
    void *hsdl = NULL;
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    SDL_PropertiesID props = 0;
    bool ok = true;
    Result rc;

    (void)argc;
    (void)argv;

    g_log = fopen("sdmc:/sdl3smoke.log", "w");
    logline("sdl3 smoke: start");

    /* Expose this NRO's libc/libnx to dlink modules. */
    rc = dlink_register_host();
    logline("dlink_register_host: 0x%X", rc);
    if (R_FAILED(rc)) {
        logline("FATAL: host registration failed");
        ok = false;
    }

    /* Bare names resolve under sdmc:/lib/ by default. */
    dlink_set_library_dir(NULL);

    /* Mirror the qlaunch loader: neko3d first (GLOBAL), then SDL3. */
    if (ok) {
        hneko = dlopen("neko3d", RTLD_GLOBAL);
        logline("dlopen neko3d: %p (%s)", hneko, hneko ? "ok" : dlerror());
        ok = (hneko != NULL);
    }
    if (ok) {
        hsdl = dlopen("sdl3", RTLD_LOCAL);
        logline("dlopen sdl3: %p (%s)", hsdl, hsdl ? "ok" : dlerror());
        ok = (hsdl != NULL);
    }

    if (ok) {
        BIND(hsdl, Init, fnInit, "SDL_Init");
        BIND(hsdl, GetError, fnGetError, "SDL_GetError");
        BIND(hsdl, CreateProperties, fnCreateProperties, "SDL_CreateProperties");
        BIND(hsdl, SetPointerProperty, fnSetPointerProperty, "SDL_SetPointerProperty");
        BIND(hsdl, SetNumberProperty, fnSetNumberProperty, "SDL_SetNumberProperty");
        BIND(hsdl, DestroyProperties, fnDestroyProperties, "SDL_DestroyProperties");
        BIND(hsdl, CreateWindowWithProperties, fnCreateWindowWithProperties,
             "SDL_CreateWindowWithProperties");
        BIND(hsdl, DestroyWindow, fnDestroyWindow, "SDL_DestroyWindow");
        BIND(hsdl, CreateRenderer, fnCreateRenderer, "SDL_CreateRenderer");
        BIND(hsdl, DestroyRenderer, fnDestroyRenderer, "SDL_DestroyRenderer");
        BIND(hsdl, SetRenderDrawColor, fnSetRenderDrawColor, "SDL_SetRenderDrawColor");
        BIND(hsdl, RenderClear, fnRenderClear, "SDL_RenderClear");
        BIND(hsdl, RenderPresent, fnRenderPresent, "SDL_RenderPresent");
        BIND(hsdl, Delay, fnDelay, "SDL_Delay");
        BIND(hsdl, Quit, fnQuit, "SDL_Quit");
        logline("bind: %s", ok ? "ok" : "FAILED");
    }

    if (ok && !S.Init(SDL_INIT_VIDEO)) {
        logline("SDL_Init failed: %s", S.GetError());
        ok = false;
    } else if (ok) {
        logline("SDL_Init: ok");
    }

    if (ok) {
        /* Adopt hbmenu's default window for the deko3d swapchain. */
        props = S.CreateProperties();
        S.SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_SWITCH_NWINDOW_POINTER,
                             nwindowGetDefault());
        S.SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 1280);
        S.SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 720);
        win = S.CreateWindowWithProperties(props);
        S.DestroyProperties(props);
        logline("window: %p (%s)", win, win ? "ok" : S.GetError());
        ok = (win != NULL);
    }

    if (ok) {
        ren = S.CreateRenderer(win, "deko3d");
        logline("renderer: %p (%s)", ren, ren ? "ok" : S.GetError());
        ok = (ren != NULL);
    }

    if (ok) {
        const Uint8 cols[3][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } };
        for (int f = 0; f < 180 && appletMainLoop(); f++) {
            const Uint8 *c = cols[(f / 60) % 3];
            if (!S.SetRenderDrawColor(ren, c[0], c[1], c[2], 255) ||
                !S.RenderClear(ren) || !S.RenderPresent(ren)) {
                logline("frame %d failed: %s", f, S.GetError());
                ok = false;
                break;
            }
            S.Delay(16);
        }
        logline("frames: %s", ok ? "ok" : "FAILED");
    }

    if (ren)
        S.DestroyRenderer(ren);
    if (win)
        S.DestroyWindow(win);
    if (ok)
        S.Quit();
    if (hsdl)
        dlclose(hsdl);
    if (hneko)
        dlclose(hneko);

    logline("sdl3 smoke: %s", ok ? "PASS" : "FAIL");
    if (g_log)
        fclose(g_log);
    return ok ? 0 : 1;
}
