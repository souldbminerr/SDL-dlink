/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL_internal.h"

#ifdef SDL_VIDEO_RENDER_SWITCH_DEKO3D

#include "SDL_render_deko3d_dispatch.h"

bool SDL_Deko3dDispatch_Load(SDL_Deko3dDispatch *dispatch, SDL_SharedObject **phandle)
{
    SDL_SharedObject *handle;

    if (!dispatch || !phandle) {
        return SDL_SetError("Invalid deko3d dispatch parameters");
    }

    handle = SDL_LoadObject("neko3d");
    if (!handle) {
        return false;
    }

#define SDL_DEKO3D_FN(ret, name, args)                             \
    dispatch->name = (ret (*)args)SDL_LoadFunction(handle, #name); \
    if (!dispatch->name) {                                         \
        SDL_UnloadObject(handle);                                  \
        return false;                                              \
    }
    SDL_DEKO3D_FUNCTIONS
#undef SDL_DEKO3D_FN

    *phandle = handle;
    return true;
}

void SDL_Deko3dDispatch_Unload(SDL_Deko3dDispatch *dispatch, SDL_SharedObject *handle)
{
    if (dispatch) {
        SDL_memset(dispatch, 0, sizeof(*dispatch));
    }
    SDL_UnloadObject(handle);
}

#endif // SDL_VIDEO_RENDER_SWITCH_DEKO3D
