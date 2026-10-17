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

/* deko3d renderer for Switch*/
/* needs neko3d.dnro in sdmc:/lib/ */

#include "../SDL_sysrender.h"

#include <switch.h>

#include "SDL_render_deko3d_dispatch.h"

#include "sdl_color_vsh_dksh.h"
#include "sdl_color_fsh_dksh.h"
#include "sdl_tex_vsh_dksh.h"
#include "sdl_tex_fsh_dksh.h"

#define SWITCH_DEKO3D_FB_NUM    2
#define SWITCH_DEKO3D_CODE_SIZE (64 * 1024)
#define SWITCH_DEKO3D_CMD_SIZE  (256 * 1024)
#define SWITCH_DEKO3D_DESC_SIZE 0x4000
#define SWITCH_DEKO3D_MAX_TEX  256
#define SWITCH_DEKO3D_VTX_RING (4 * 1024 * 1024)

typedef struct
{
    float x, y;
    SDL_FColor color;
} SWITCH_DEKO3D_ColorVtx;

typedef struct
{
    float x, y;
    float u, v;
    SDL_FColor color;
} SWITCH_DEKO3D_TexVtx;

typedef struct
{
    DkMemBlock mem;
    DkImage img;
    int slot;
    int w, h;
    uint32_t pitch;
    bool rgbadirect;
    void *shadow;
    SDL_Rect lockrect;
} SWITCH_DEKO3D_TextureData;

typedef struct
{
    SDL_SharedObject *neko3d;
    SDL_Deko3dDispatch dk;
    DkDevice device;
    DkQueue queue;
    DkSwapchain swapchain;
    DkCmdBuf cmdbuf;
    DkMemBlock cmdMem;
    DkMemBlock fbMem;
    DkImage fbs[SWITCH_DEKO3D_FB_NUM];
    DkMemBlock codeMem;
    uint32_t codeOff;
    DkShader colorVsh, colorFsh, texVsh, texFsh;
    DkMemBlock uboMem;
    DkGpuAddr uboGpu;
    DkMemBlock descMem;
    uint8_t *descCpu;
    DkGpuAddr descGpu;
    bool texSlotUsed[SWITCH_DEKO3D_MAX_TEX];
    DkMemBlock vtxMem;
    uint8_t *vtxCpu;
    DkGpuAddr vtxGpu;
    uint32_t vtxCursor;
    NWindow *window;   // adopted, host-owned
    ViLayer layer;     // adopted copy, host-owned
    ViDisplay display; // adopted copy, host-owned
    bool has_layer;
    bool has_display;
    int fbW, fbH;
    int slot;
    bool drawing;
    bool framePending; // submitted work still on the GPU: next CPU touch waits
    bool vsync;
    SDL_Rect viewport;
    bool viewport_dirty;
    SDL_Rect cliprect;
    bool clip_enabled;
    bool clip_dirty;
} SWITCH_DEKO3D_RenderData;

static bool SWITCH_DEKO3D_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window, SDL_PropertiesID create_props);

static void LoadCode(SWITCH_DEKO3D_RenderData *data, const void *blob, uint32_t size, DkShader *sh)
{
    uint32_t o = data->codeOff;
    data->codeOff += (size + DK_SHADER_CODE_ALIGNMENT - 1) & ~(DK_SHADER_CODE_ALIGNMENT - 1);
    SDL_memcpy(data->dk.dkMemBlockGetCpuAddr(data->codeMem) + o, blob, size);
    {
        DkShaderMaker mk;
        dkShaderMakerDefaults(&mk, data->codeMem, o);
        data->dk.dkShaderInitialize(sh, &mk);
    }
}

static int TexSlotAlloc(SWITCH_DEKO3D_RenderData *data)
{
    for (int i = 0; i < SWITCH_DEKO3D_MAX_TEX; i++) {
        if (!data->texSlotUsed[i]) {
            data->texSlotUsed[i] = true;
            return i;
        }
    }
    return -1;
}

static void TexSlotFree(SWITCH_DEKO3D_RenderData *data, int slot)
{
    if (slot >= 0 && slot < SWITCH_DEKO3D_MAX_TEX) {
        data->texSlotUsed[slot] = false;
    }
}

static void WriteImageDesc(SWITCH_DEKO3D_RenderData *data, int slot, const DkImageView *view)
{
    if (slot < 0 || slot >= SWITCH_DEKO3D_MAX_TEX) {
        return;
    }
    data->dk.dkImageDescriptorInitialize((DkImageDescriptor *)(data->descCpu + slot * 0x20), view, false, false);
}

static void WriteSamplerDesc(SWITCH_DEKO3D_RenderData *data)
{
    DkSampler sam;
    dkSamplerDefaults(&sam);
    sam.minFilter = DkFilter_Linear;
    sam.magFilter = DkFilter_Linear;
    sam.wrapMode[0] = DkWrapMode_ClampToEdge;
    sam.wrapMode[1] = DkWrapMode_ClampToEdge;
    sam.wrapMode[2] = DkWrapMode_ClampToEdge;
    data->dk.dkSamplerDescriptorInitialize(
        (DkSamplerDescriptor *)(data->descCpu + SWITCH_DEKO3D_MAX_TEX * 0x20), &sam);
}

static void *VtxAlloc(SWITCH_DEKO3D_RenderData *data, uint32_t size, DkGpuAddr *out_gpu)
{
    uint32_t aligned = (data->vtxCursor + 15) & ~15u;
    uint8_t *cpu;
    if (size > SWITCH_DEKO3D_VTX_RING || aligned > SWITCH_DEKO3D_VTX_RING - size) {
        return NULL;
    }
    data->vtxCursor = aligned + size;
    cpu = data->vtxCpu + aligned;
    *out_gpu = data->vtxGpu + aligned;
    return cpu;
}

static DkGpuAddr VtxCpuToGpu(SWITCH_DEKO3D_RenderData *data, const void *cpu)
{
    return data->vtxGpu + (uint32_t)((const uint8_t *)cpu - data->vtxCpu);
}

static void BindUbo(SWITCH_DEKO3D_RenderData *data)
{
    DkBufExtents ext = { data->uboGpu, 16 };
    data->dk.dkCmdBufBindUniformBuffers(data->cmdbuf, DkStage_Vertex, 0, &ext, 1);
}

static void BindTexSets(SWITCH_DEKO3D_RenderData *data)
{
    data->dk.dkCmdBufBindImageDescriptorSet(data->cmdbuf, data->descGpu, SWITCH_DEKO3D_MAX_TEX);
    data->dk.dkCmdBufBindSamplerDescriptorSet(data->cmdbuf, data->descGpu + SWITCH_DEKO3D_MAX_TEX * 0x20, 1);
    data->dk.dkCmdBufBarrier(data->cmdbuf, DkBarrier_None,
                             DkInvalidateFlags_Descriptors |
                                 DkInvalidateFlags_Image |
                                 DkInvalidateFlags_L2Cache);
}

static void ApplyViewportClip(SWITCH_DEKO3D_RenderData *data)
{
    if (data->viewport_dirty) {
        DkViewport vp = {
            (float)data->viewport.x, (float)data->viewport.y,
            (float)data->viewport.w, (float)data->viewport.h,
            0.0f, 1.0f
        };
        data->dk.dkCmdBufSetViewports(data->cmdbuf, 0, &vp, 1);
        data->viewport_dirty = false;
        data->clip_dirty = true;
    }
    if (data->clip_dirty) {
        DkScissor sc;
        if (data->clip_enabled) {
            sc.x = (uint32_t)data->cliprect.x;
            sc.y = (uint32_t)data->cliprect.y;
            sc.width = (uint32_t)data->cliprect.w;
            sc.height = (uint32_t)data->cliprect.h;
        } else {
            sc.x = 0;
            sc.y = 0;
            sc.width = (uint32_t)data->fbW;
            sc.height = (uint32_t)data->fbH;
        }
        data->dk.dkCmdBufSetScissors(data->cmdbuf, 0, &sc, 1);
        data->clip_dirty = false;
    }
}

static void BindBlend(SWITCH_DEKO3D_RenderData *data, SDL_BlendMode blend, DkFace cullMode)
{
    DkRasterizerState rast;
    DkColorWriteState colW;
    DkColorState col;
    DkBlendState bl;
    dkRasterizerStateDefaults(&rast);
    rast.cullMode = cullMode;
    dkColorWriteStateDefaults(&colW);
    dkColorStateDefaults(&col);
    if (blend != SDL_BLENDMODE_NONE) {
        DkBlendFactor srcRgb = DkBlendFactor_SrcAlpha, dstRgb = DkBlendFactor_InvSrcAlpha;
        DkBlendFactor srcA = DkBlendFactor_SrcAlpha, dstA = DkBlendFactor_InvSrcAlpha;
        switch (blend) {
        case SDL_BLENDMODE_ADD:
            srcRgb = DkBlendFactor_SrcAlpha;
            dstRgb = DkBlendFactor_One;
            srcA = DkBlendFactor_SrcAlpha;
            dstA = DkBlendFactor_One;
            break;
        case SDL_BLENDMODE_MOD:
            srcRgb = DkBlendFactor_Zero;
            dstRgb = DkBlendFactor_SrcColor;
            srcA = DkBlendFactor_Zero;
            dstA = DkBlendFactor_SrcColor;
            break;
        case SDL_BLENDMODE_MUL:
            srcRgb = DkBlendFactor_DstColor;
            dstRgb = DkBlendFactor_Zero;
            srcA = DkBlendFactor_DstColor;
            dstA = DkBlendFactor_Zero;
            break;
        default:
            break;
        }
        dkColorStateSetBlendEnable(&col, 0, true);
        dkBlendStateDefaults(&bl);
        dkBlendStateSetOps(&bl, DkBlendOp_Add, DkBlendOp_Add);
        dkBlendStateSetFactors(&bl, srcRgb, dstRgb, srcA, dstA);
        data->dk.dkCmdBufBindRasterizerState(data->cmdbuf, &rast);
        data->dk.dkCmdBufBindColorState(data->cmdbuf, &col);
        data->dk.dkCmdBufBindBlendStates(data->cmdbuf, 0, &bl, 1);
        data->dk.dkCmdBufBindColorWriteState(data->cmdbuf, &colW);
    } else {
        data->dk.dkCmdBufBindRasterizerState(data->cmdbuf, &rast);
        data->dk.dkCmdBufBindColorState(data->cmdbuf, &col);
        data->dk.dkCmdBufBindColorWriteState(data->cmdbuf, &colW);
    }
}

static void EnsureIdle(SWITCH_DEKO3D_RenderData *data)
{
    if (data->framePending) {
        data->dk.dkQueueWaitIdle(data->queue);
        data->framePending = false;
    }
}

static bool StartDrawing(SWITCH_DEKO3D_RenderData *data)
{
    DkImageView view;
    if (data->drawing) {
        return true;
    }
    EnsureIdle(data);
    data->slot = data->dk.dkQueueAcquireImage(data->queue, data->swapchain);
    if (data->slot < 0) {
        return SDL_SetError("dkQueueAcquireImage failed");
    }
    data->dk.dkCmdBufClear(data->cmdbuf);
    dkImageViewDefaults(&view, &data->fbs[data->slot]);
    {
        const DkImageView *targets[1] = { &view };
        data->dk.dkCmdBufBindRenderTargets(data->cmdbuf, targets, 1, NULL);
    }
    data->drawing = true;
    data->viewport_dirty = true;
    data->clip_dirty = true;
    return true;
}

static bool DrawVerts(SWITCH_DEKO3D_RenderData *data, SDL_BlendMode blend,
                      SWITCH_DEKO3D_TextureData *tex, const void *verts,
                      size_t count, DkPrimitive prim)
{
    const DkShader *shaders[2];
    DkBufExtents ext;
    if (!StartDrawing(data)) {
        return false;
    }
    ApplyViewportClip(data);
    if (tex) {
        shaders[0] = &data->texVsh;
        shaders[1] = &data->texFsh;
    } else {
        shaders[0] = &data->colorVsh;
        shaders[1] = &data->colorFsh;
    }
    data->dk.dkCmdBufBindShaders(data->cmdbuf, DkStageFlag_GraphicsMask, shaders, 2);
    BindBlend(data, blend, DkFace_None);
    BindUbo(data);
    if (tex) {
        DkVtxAttribState attribs[3];
        DkVtxBufferState state[1];
        BindTexSets(data);
        {
            DkResHandle handle = dkMakeTextureHandle((uint32_t)tex->slot, 0);
            data->dk.dkCmdBufBindTextures(data->cmdbuf, DkStage_Fragment, 0, &handle, 1);
        }
        SDL_memset(attribs, 0, sizeof(attribs));
        attribs[0].bufferId = 0;
        attribs[0].offset = 0;
        attribs[0].size = DkVtxAttribSize_2x32;
        attribs[0].type = DkVtxAttribType_Float;
        attribs[1].bufferId = 0;
        attribs[1].offset = 8;
        attribs[1].size = DkVtxAttribSize_2x32;
        attribs[1].type = DkVtxAttribType_Float;
        attribs[2].bufferId = 0;
        attribs[2].offset = 16;
        attribs[2].size = DkVtxAttribSize_4x32;
        attribs[2].type = DkVtxAttribType_Float;
        state[0].stride = sizeof(SWITCH_DEKO3D_TexVtx);
        state[0].divisor = 0;
        data->dk.dkCmdBufBindVtxAttribState(data->cmdbuf, attribs, 3);
        data->dk.dkCmdBufBindVtxBufferState(data->cmdbuf, state, 1);
        ext.addr = VtxCpuToGpu(data, verts);
        ext.size = (uint32_t)(count * sizeof(SWITCH_DEKO3D_TexVtx));
    } else {
        DkVtxAttribState attribs[2];
        DkVtxBufferState state[1];
        SDL_memset(attribs, 0, sizeof(attribs));
        attribs[0].bufferId = 0;
        attribs[0].offset = 0;
        attribs[0].size = DkVtxAttribSize_2x32;
        attribs[0].type = DkVtxAttribType_Float;
        attribs[1].bufferId = 0;
        attribs[1].offset = 8;
        attribs[1].size = DkVtxAttribSize_4x32;
        attribs[1].type = DkVtxAttribType_Float;
        state[0].stride = sizeof(SWITCH_DEKO3D_ColorVtx);
        state[0].divisor = 0;
        data->dk.dkCmdBufBindVtxAttribState(data->cmdbuf, attribs, 2);
        data->dk.dkCmdBufBindVtxBufferState(data->cmdbuf, state, 1);
        ext.addr = VtxCpuToGpu(data, verts);
        ext.size = (uint32_t)(count * sizeof(SWITCH_DEKO3D_ColorVtx));
    }
    data->dk.dkCmdBufBindVtxBuffers(data->cmdbuf, 0, &ext, 1);
    data->dk.dkCmdBufDraw(data->cmdbuf, prim, (uint32_t)count, 1, 0, 0);
    return true;
}

static bool QueueInterleaved(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                             const float *xy, int xy_stride, const SDL_FColor *color, int color_stride,
                             const float *uv, int uv_stride, int num_vertices, const void *indices,
                             int num_indices, int size_indices, float scale_x, float scale_y)
{
    SWITCH_DEKO3D_RenderData *data = (SWITCH_DEKO3D_RenderData *)renderer->internal;
    const float color_scale = cmd->data.draw.color_scale;
    int count = indices ? num_indices : num_vertices;
    DkGpuAddr gpu;
    size_indices = indices ? size_indices : 0;
    cmd->data.draw.count = (size_t)count;

    EnsureIdle(data);
    if (texture) {
        SWITCH_DEKO3D_TexVtx *verts = (SWITCH_DEKO3D_TexVtx *)VtxAlloc(data, (uint32_t)(count * sizeof(*verts)), &gpu);
        if (!verts) {
            return SDL_OutOfMemory();
        }
        for (int i = 0; i < count; i++) {
            int j = i;
            const float *xy_;
            const float *uv_;
            SDL_FColor col_;
            if (size_indices == 4) {
                j = (int)((const Uint32 *)indices)[i];
            } else if (size_indices == 2) {
                j = (int)((const Uint16 *)indices)[i];
            } else if (size_indices == 1) {
                j = (int)((const Uint8 *)indices)[i];
            }
            xy_ = (const float *)((const char *)xy + j * xy_stride);
            col_ = *(const SDL_FColor *)((const char *)color + j * color_stride);
            uv_ = (const float *)((const char *)uv + j * uv_stride);
            verts[i].x = xy_[0] * scale_x;
            verts[i].y = xy_[1] * scale_y;
            verts[i].u = uv_[0];
            verts[i].v = uv_[1];
            verts[i].color.r = col_.r * color_scale;
            verts[i].color.g = col_.g * color_scale;
            verts[i].color.b = col_.b * color_scale;
            verts[i].color.a = col_.a;
        }
        cmd->data.draw.first = (size_t)verts;
    } else {
        SWITCH_DEKO3D_ColorVtx *verts = (SWITCH_DEKO3D_ColorVtx *)VtxAlloc(data, (uint32_t)(count * sizeof(*verts)), &gpu);
        if (!verts) {
            return SDL_OutOfMemory();
        }
        for (int i = 0; i < count; i++) {
            int j = i;
            const float *xy_;
            SDL_FColor col_;
            if (size_indices == 4) {
                j = (int)((const Uint32 *)indices)[i];
            } else if (size_indices == 2) {
                j = (int)((const Uint16 *)indices)[i];
            } else if (size_indices == 1) {
                j = (int)((const Uint8 *)indices)[i];
            }
            xy_ = (const float *)((const char *)xy + j * xy_stride);
            col_ = *(const SDL_FColor *)((const char *)color + j * color_stride);
            verts[i].x = xy_[0] * scale_x;
            verts[i].y = xy_[1] * scale_y;
            verts[i].color.r = col_.r * color_scale;
            verts[i].color.g = col_.g * color_scale;
            verts[i].color.b = col_.b * color_scale;
            verts[i].color.a = col_.a;
        }
        cmd->data.draw.first = (size_t)verts;
    }
    return true;
}

static void SWITCH_DEKO3D_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event)
{
    (void)renderer;
    (void)event;
    // TODO: react to size changes
}

static bool SWITCH_DEKO3D_SupportsBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    (void)renderer;
    return blendMode == SDL_BLENDMODE_NONE || blendMode == SDL_BLENDMODE_BLEND ||
           blendMode == SDL_BLENDMODE_ADD || blendMode == SDL_BLENDMODE_MOD || blendMode == SDL_BLENDMODE_MUL;
}

static bool FormatConvertible(SDL_PixelFormat format, bool *out_direct)
{
    switch (format) {
    case SDL_PIXELFORMAT_RGBA8888:
        *out_direct = true;
        return true;
    case SDL_PIXELFORMAT_ARGB8888:
    case SDL_PIXELFORMAT_ABGR8888:
    case SDL_PIXELFORMAT_BGRA8888:
        *out_direct = false;
        return true;
    default:
        return false;
    }
}

static void ConvertRowRGBA(uint8_t *dst, const uint8_t *src, int w, SDL_PixelFormat format)
{
    if (format == SDL_PIXELFORMAT_RGBA8888) {
        SDL_memcpy(dst, src, (size_t)w * 4);
        return;
    }
    for (int i = 0; i < w; i++) {
        uint8_t a, r, g, b;
        if (format == SDL_PIXELFORMAT_ARGB8888) {
            a = src[0];
            r = src[1];
            g = src[2];
            b = src[3];
        } else if (format == SDL_PIXELFORMAT_ABGR8888) {
            a = src[0];
            b = src[1];
            g = src[2];
            r = src[3];
        } else { // SDL_PIXELFORMAT_BGRA8888
            b = src[0];
            g = src[1];
            r = src[2];
            a = src[3];
        }
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = a;
        src += 4;
        dst += 4;
    }
}

static void UploadRows(SWITCH_DEKO3D_RenderData *data, SWITCH_DEKO3D_TextureData *tex,
                       const void *pixels, int pitch, const SDL_Rect *rect, SDL_PixelFormat format)
{
    const uint8_t *src = (const uint8_t *)pixels;
    uint8_t *dst = (uint8_t *)data->dk.dkMemBlockGetCpuAddr(tex->mem);
    dst += (size_t)rect->y * tex->pitch + (size_t)rect->x * 4;
    for (int y = 0; y < rect->h; y++) {
        ConvertRowRGBA(dst, src, rect->w, format);
        src += pitch;
        dst += tex->pitch;
    }
}

static bool SWITCH_DEKO3D_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture, SDL_PropertiesID create_props)
{
    SWITCH_DEKO3D_RenderData *data = (SWITCH_DEKO3D_RenderData *)renderer->internal;
    SWITCH_DEKO3D_TextureData *tex;
    DkImageLayoutMaker lm;
    DkImageLayout lay;
    DkMemBlockMaker mm;
    uint32_t sz, al;
    DkImageView view;
    bool direct;
    (void)create_props;

    if (texture->access == SDL_TEXTUREACCESS_TARGET) {
        return SDL_Unsupported(); // no render targets yet
    }
    if (!FormatConvertible(texture->format, &direct)) {
        return SDL_Unsupported();
    }

    tex = (SWITCH_DEKO3D_TextureData *)SDL_calloc(1, sizeof(*tex));
    if (!tex) {
        return false;
    }
    tex->slot = -1;

    dkImageLayoutMakerDefaults(&lm, data->device);
    lm.flags = DkImageFlags_PitchLinear;
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.dimensions[0] = (uint32_t)texture->w;
    lm.dimensions[1] = (uint32_t)texture->h;
    data->dk.dkImageLayoutInitialize(&lay, &lm);
    sz = data->dk.dkImageLayoutGetSize(&lay);
    al = data->dk.dkImageLayoutGetAlignment(&lay);
    sz = (sz + al - 1) & ~(al - 1);
    sz = (sz + 0xFFFu) & ~0xFFFu; // deko3d needs 4K-multiple blocks
    dkMemBlockMakerDefaults(&mm, data->device, sz);
    mm.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    tex->mem = data->dk.dkMemBlockCreate(&mm);
    if (!tex->mem) {
        SDL_free(tex);
        return SDL_OutOfMemory();
    }
    data->dk.dkImageInitialize(&tex->img, &lay, tex->mem, 0);
    tex->w = texture->w;
    tex->h = texture->h;
    tex->pitch = texture->h > 0 ? data->dk.dkImageLayoutGetSize(&lay) / (uint32_t)texture->h : 0;
    tex->rgbadirect = direct;
    if (!direct) {
        tex->shadow = SDL_malloc((size_t)texture->w * (size_t)texture->h * 4);
        if (!tex->shadow) {
            data->dk.dkMemBlockDestroy(tex->mem);
            SDL_free(tex);
            return SDL_OutOfMemory();
        }
    }

    tex->slot = TexSlotAlloc(data);
    if (tex->slot < 0) {
        SDL_free(tex->shadow);
        data->dk.dkMemBlockDestroy(tex->mem);
        SDL_free(tex);
        return SDL_OutOfMemory();
    }
    dkImageViewDefaults(&view, &tex->img);
    WriteImageDesc(data, tex->slot, &view);
    texture->internal = tex;
    return true;
}

static bool SWITCH_DEKO3D_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                                        const SDL_Rect *rect, const void *pixels, int pitch)
{
    SWITCH_DEKO3D_RenderData *data = (SWITCH_DEKO3D_RenderData *)renderer->internal;
    SWITCH_DEKO3D_TextureData *tex = (SWITCH_DEKO3D_TextureData *)texture->internal;
    if (!tex) {
        return SDL_SetError("Texture not initialized");
    }
    EnsureIdle(data);
    UploadRows(data, tex, pixels, pitch, rect, texture->format);
    return true;
}

static bool SWITCH_DEKO3D_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                                      const SDL_Rect *rect, void **pixels, int *pitch)
{
    SWITCH_DEKO3D_RenderData *data = (SWITCH_DEKO3D_RenderData *)renderer->internal;
    SWITCH_DEKO3D_TextureData *tex = (SWITCH_DEKO3D_TextureData *)texture->internal;
    (void)renderer;
    if (!tex) {
        return SDL_SetError("Texture not initialized");
    }
    EnsureIdle(data);
    tex->lockrect = *rect;
    if (tex->rgbadirect) {
        uint8_t *base = (uint8_t *)data->dk.dkMemBlockGetCpuAddr(tex->mem);
        *pixels = base + (size_t)rect->y * tex->pitch + (size_t)rect->x * 4;
        *pitch = (int)tex->pitch;
    } else {
        *pixels = (uint8_t *)tex->shadow + ((size_t)rect->y * (size_t)tex->w + (size_t)rect->x) * 4;
        *pitch = tex->w * 4;
    }
    return true;
}

static void SWITCH_DEKO3D_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    SWITCH_DEKO3D_RenderData *data = (SWITCH_DEKO3D_RenderData *)renderer->internal;
    SWITCH_DEKO3D_TextureData *tex = (SWITCH_DEKO3D_TextureData *)texture->internal;
    SDL_Rect rect;
    if (!tex || tex->rgbadirect) {
        return; // direct mappings need no writeback
    }
    rect.x = 0;
    rect.y = 0;
    rect.w = tex->w;
    rect.h = tex->h;
    if (tex->lockrect.w > 0 && tex->lockrect.h > 0) {
        rect = tex->lockrect;
    }
    {
        uint8_t *tmp = (uint8_t *)SDL_malloc((size_t)rect.w * (size_t)rect.h * 4);
        if (!tmp) {
            return;
        }
        for (int y = 0; y < rect.h; y++) {
            SDL_memcpy(tmp + (size_t)y * rect.w * 4,
                       (uint8_t *)tex->shadow + ((size_t)(rect.y + y) * tex->w + rect.x) * 4,
                       (size_t)rect.w * 4);
        }
        UploadRows(data, tex, tmp, rect.w * 4, &rect, texture->format);
        SDL_free(tmp);
    }
    SDL_memset(&tex->lockrect, 0, sizeof(tex->lockrect));
}

static bool SWITCH_DEKO3D_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    (void)renderer;
    if (texture) {
        return SDL_Unsupported(); // no render targets yet
    }
    return true;
}

static bool SWITCH_DEKO3D_QueueNoOp(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    (void)renderer;
    (void)cmd;
    return true;
}

static bool SWITCH_DEKO3D_QueueSetDrawColor(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    (void)renderer;
    (void)cmd;
    return true;
}

static bool SWITCH_DEKO3D_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    return QueueInterleaved(renderer, cmd, NULL,
                            &points[0].x, sizeof(*points), &cmd->data.draw.color, 0,
                            NULL, 0, count, NULL, 0, 0, 1.0f, 1.0f);
}

static bool SWITCH_DEKO3D_QueueDrawLines(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    return QueueInterleaved(renderer, cmd, NULL,
                            &points[0].x, sizeof(*points), &cmd->data.draw.color, 0,
                            NULL, 0, count, NULL, 0, 0, 1.0f, 1.0f);
}

static bool SWITCH_DEKO3D_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                                        const float *xy, int xy_stride, const SDL_FColor *color, int color_stride, const float *uv, int uv_stride,
                                        int num_vertices, const void *indices, int num_indices, int size_indices,
                                        float scale_x, float scale_y)
{
    return QueueInterleaved(renderer, cmd, texture, xy, xy_stride, color, color_stride,
                            uv, uv_stride, num_vertices, indices, num_indices,
                            size_indices, scale_x, scale_y);
}

static void SWITCH_DEKO3D_InvalidateCachedState(SDL_Renderer *renderer)
{
    SWITCH_DEKO3D_RenderData *data = (SWITCH_DEKO3D_RenderData *)renderer->internal;
    data->viewport_dirty = true;
    data->clip_dirty = true;
}

static bool SWITCH_DEKO3D_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    SWITCH_DEKO3D_RenderData *data = (SWITCH_DEKO3D_RenderData *)renderer->internal;
    (void)vertices;
    (void)vertsize;

    while (cmd) {
        switch (cmd->command) {
        case SDL_RENDERCMD_SETVIEWPORT:
            SDL_copyp(&data->viewport, &cmd->data.viewport.rect);
            data->viewport_dirty = true;
            break;
        case SDL_RENDERCMD_SETCLIPRECT:
            data->clip_enabled = cmd->data.cliprect.enabled;
            SDL_copyp(&data->cliprect, &cmd->data.cliprect.rect);
            data->clip_dirty = true;
            break;
        case SDL_RENDERCMD_SETDRAWCOLOR:
            break;
        case SDL_RENDERCMD_CLEAR: {
            SWITCH_DEKO3D_ColorVtx *v;
            DkGpuAddr gpu;
            SDL_FColor c = cmd->data.color.color;
            float scale = cmd->data.color.color_scale;
            float W = (float)data->fbW, H = (float)data->fbH;
            DkScissor full;
            // draw fullscreen triangle to clear
            if (!StartDrawing(data)) {
                return false;
            }
            v = (SWITCH_DEKO3D_ColorVtx *)VtxAlloc(data, 3 * (uint32_t)sizeof(*v), &gpu);
            if (!v) {
                return SDL_OutOfMemory();
            }
            v[0].x = 0.0f;
            v[0].y = 0.0f;
            v[1].x = 2.0f * W;
            v[1].y = 0.0f;
            v[2].x = 0.0f;
            v[2].y = 2.0f * H;
            v[0].color.r = v[1].color.r = v[2].color.r = c.r * scale;
            v[0].color.g = v[1].color.g = v[2].color.g = c.g * scale;
            v[0].color.b = v[1].color.b = v[2].color.b = c.b * scale;
            v[0].color.a = v[1].color.a = v[2].color.a = c.a;
            full.x = 0;
            full.y = 0;
            full.width = (uint32_t)data->fbW;
            full.height = (uint32_t)data->fbH;
            data->dk.dkCmdBufSetViewports(data->cmdbuf, 0,
                                          &(DkViewport){ 0.0f, 0.0f, W, H, 0.0f, 1.0f }, 1);
            data->viewport_dirty = true; // re-apply the viewport on the next draw
            data->dk.dkCmdBufSetScissors(data->cmdbuf, 0, &full, 1);
            data->clip_dirty = true; // re-apply the cliprect on the next draw
            {
                const DkShader *shaders[2] = { &data->colorVsh, &data->colorFsh };
                DkVtxAttribState attribs[2];
                DkVtxBufferState state[1];
                DkBufExtents ext;
                data->dk.dkCmdBufBindShaders(data->cmdbuf, DkStageFlag_GraphicsMask, shaders, 2);
                BindBlend(data, SDL_BLENDMODE_NONE, DkFace_None);
                BindUbo(data);
                SDL_memset(attribs, 0, sizeof(attribs));
                attribs[0].bufferId = 0;
                attribs[0].offset = 0;
                attribs[0].size = DkVtxAttribSize_2x32;
                attribs[0].type = DkVtxAttribType_Float;
                attribs[1].bufferId = 0;
                attribs[1].offset = 8;
                attribs[1].size = DkVtxAttribSize_4x32;
                attribs[1].type = DkVtxAttribType_Float;
                state[0].stride = sizeof(SWITCH_DEKO3D_ColorVtx);
                state[0].divisor = 0;
                data->dk.dkCmdBufBindVtxAttribState(data->cmdbuf, attribs, 2);
                data->dk.dkCmdBufBindVtxBufferState(data->cmdbuf, state, 1);
                ext.addr = gpu;
                ext.size = 3 * (uint32_t)sizeof(*v);
                data->dk.dkCmdBufBindVtxBuffers(data->cmdbuf, 0, &ext, 1);
                data->dk.dkCmdBufDraw(data->cmdbuf, DkPrimitive_Triangles, 3, 1, 0, 0);
            }
            break;
        }
        case SDL_RENDERCMD_DRAW_POINTS:
            if (!DrawVerts(data, cmd->data.draw.blend, NULL,
                           (const void *)cmd->data.draw.first, cmd->data.draw.count,
                           DkPrimitive_Points)) {
                return false;
            }
            break;
        case SDL_RENDERCMD_DRAW_LINES:
            if (!DrawVerts(data, cmd->data.draw.blend, NULL,
                           (const void *)cmd->data.draw.first, cmd->data.draw.count,
                           DkPrimitive_Lines)) {
                return false;
            }
            break;
        case SDL_RENDERCMD_GEOMETRY: {
            SWITCH_DEKO3D_TextureData *tex = NULL;
            if (cmd->data.draw.texture) {
                tex = (SWITCH_DEKO3D_TextureData *)cmd->data.draw.texture->internal;
                if (!tex) {
                    return SDL_SetError("Texture not initialized");
                }
            }
            if (!DrawVerts(data, cmd->data.draw.blend, tex,
                           (const void *)cmd->data.draw.first, cmd->data.draw.count,
                           DkPrimitive_Triangles)) {
                return false;
            }
            break;
        }
        default:
            break; // NO_OP
        }
        cmd = cmd->next;
    }
    return true;
}

static SDL_Surface *SWITCH_DEKO3D_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    (void)renderer;
    (void)rect;
    SDL_Unsupported();
    return NULL;
}

static bool SWITCH_DEKO3D_RenderPresent(SDL_Renderer *renderer)
{
    SWITCH_DEKO3D_RenderData *data = (SWITCH_DEKO3D_RenderData *)renderer->internal;
    DkCmdList list;
    if (!data->drawing) {
        return true;
    }
    list = data->dk.dkCmdBufFinishList(data->cmdbuf);
    data->dk.dkQueueSubmitCommands(data->queue, list);
    data->dk.dkQueuePresentImage(data->queue, data->swapchain, data->slot);
    data->drawing = false;
    data->framePending = true;
    data->vtxCursor = 0;
    return true;
}

static void SWITCH_DEKO3D_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    SWITCH_DEKO3D_RenderData *data = (SWITCH_DEKO3D_RenderData *)renderer->internal;
    SWITCH_DEKO3D_TextureData *tex = (SWITCH_DEKO3D_TextureData *)texture->internal;
    if (!tex) {
        return;
    }
    data->dk.dkQueueWaitIdle(data->queue);
    data->framePending = false;
    TexSlotFree(data, tex->slot);
    SDL_free(tex->shadow);
    data->dk.dkMemBlockDestroy(tex->mem);
    SDL_free(tex);
    texture->internal = NULL;
}

static void SWITCH_DEKO3D_Teardown(SWITCH_DEKO3D_RenderData *data)
{
    if (!data) {
        return;
    }
    if (data->queue) {
        data->dk.dkQueueWaitIdle(data->queue);
        data->dk.dkQueueDestroy(data->queue);
        data->queue = NULL;
    }
    if (data->cmdbuf) {
        data->dk.dkCmdBufDestroy(data->cmdbuf);
        data->cmdbuf = NULL;
    }
    if (data->cmdMem) {
        data->dk.dkMemBlockDestroy(data->cmdMem);
        data->cmdMem = NULL;
    }
    if (data->vtxMem) {
        data->dk.dkMemBlockDestroy(data->vtxMem);
        data->vtxMem = NULL;
        data->vtxCpu = NULL;
    }
    if (data->descMem) {
        data->dk.dkMemBlockDestroy(data->descMem);
        data->descMem = NULL;
        data->descCpu = NULL;
    }
    if (data->uboMem) {
        data->dk.dkMemBlockDestroy(data->uboMem);
        data->uboMem = NULL;
    }
    if (data->codeMem) {
        data->dk.dkMemBlockDestroy(data->codeMem);
        data->codeMem = NULL;
    }
    if (data->swapchain) {
        data->dk.dkSwapchainDestroy(data->swapchain);
        data->swapchain = NULL;
    }
    if (data->fbMem) {
        data->dk.dkMemBlockDestroy(data->fbMem);
        data->fbMem = NULL;
    }
    if (data->device) {
        data->dk.dkDeviceDestroy(data->device);
        data->device = NULL;
    }
}

static void SWITCH_DEKO3D_DestroyRenderer(SDL_Renderer *renderer)
{
    SWITCH_DEKO3D_RenderData *data = (SWITCH_DEKO3D_RenderData *)renderer->internal;
    if (data) {
        if (data->device) {
            SWITCH_DEKO3D_Teardown(data);
        }
        SDL_Deko3dDispatch_Unload(&data->dk, data->neko3d);
        SDL_free(data);
        renderer->internal = NULL;
    }
}

static bool SWITCH_DEKO3D_SetVSync(SDL_Renderer *renderer, int vsync)
{
    SWITCH_DEKO3D_RenderData *data = (SWITCH_DEKO3D_RenderData *)renderer->internal;
    data->vsync = (vsync != 0);
    return true;
}

SDL_RenderDriver SWITCH_DEKO3D_RenderDriver = {
    SWITCH_DEKO3D_CreateRenderer, "deko3d"
};

static bool SWITCH_DEKO3D_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window, SDL_PropertiesID create_props)
{
    SWITCH_DEKO3D_RenderData *data;
    SDL_PropertiesID window_props;
    NWindow *nwindow;
    ViLayer *vilayer;
    ViDisplay *vidisplay;
    DkDeviceMaker devmaker;
    DkMemBlockMaker memMk;
    DkImageLayoutMaker fbMk;
    DkImageLayout lay;
    DkSwapchainMaker scMk;
    DkImage const *imgs[SWITCH_DEKO3D_FB_NUM];
    DkCmdBufMaker cbMk;
    DkQueueMaker qMk;
    uint32_t fbSize, fbAlign;
    int i;

    SDL_SetupRendererColorspace(renderer, create_props);

    if (renderer->output_colorspace != SDL_COLORSPACE_SRGB) {
        return SDL_SetError("Unsupported output colorspace");
    }

    data = (SWITCH_DEKO3D_RenderData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return false;
    }

    window_props = SDL_GetWindowProperties(window);
    nwindow = (NWindow *)SDL_GetPointerProperty(window_props, SDL_PROP_WINDOW_CREATE_SWITCH_NWINDOW_POINTER, NULL);
    if (!nwindow) {
        SDL_free(data);
        return SDL_SetError("deko3d renderer requires an adopted NWindow (SDL_PROP_WINDOW_CREATE_SWITCH_NWINDOW_POINTER)");
    }
    data->window = nwindow;

    vilayer = (ViLayer *)SDL_GetPointerProperty(window_props, SDL_PROP_WINDOW_CREATE_SWITCH_VILAYER_POINTER, NULL);
    if (vilayer) {
        data->layer = *vilayer;
        data->has_layer = true;
    }
    vidisplay = (ViDisplay *)SDL_GetPointerProperty(window_props, SDL_PROP_WINDOW_CREATE_SWITCH_VIDISPLAY_POINTER, NULL);
    if (vidisplay) {
        data->display = *vidisplay;
        data->has_display = true;
    }

    if (!SDL_Deko3dDispatch_Load(&data->dk, &data->neko3d)) {
        SDL_free(data);
        return false;
    }

    SDL_GetWindowSizeInPixels(window, &data->fbW, &data->fbH);
    if (data->fbW <= 0 || data->fbH <= 0) {
        data->fbW = 1280;
        data->fbH = 720;
    }
    data->viewport.x = 0;
    data->viewport.y = 0;
    data->viewport.w = data->fbW;
    data->viewport.h = data->fbH;
    data->viewport_dirty = true;
    data->clip_enabled = false;
    data->clip_dirty = true;

    dkDeviceMakerDefaults(&devmaker);
    data->device = data->dk.dkDeviceCreate(&devmaker);
    if (!data->device) {
        goto fail;
    }

    for (i = 0; i < 2; i++) {
        dkImageLayoutMakerDefaults(&fbMk, data->device);
        fbMk.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
        fbMk.format = DkImageFormat_RGBA8_Unorm;
        fbMk.dimensions[0] = (uint32_t)data->fbW;
        fbMk.dimensions[1] = (uint32_t)data->fbH;
        data->dk.dkImageLayoutInitialize(&lay, &fbMk);
        fbSize = (uint32_t)data->dk.dkImageLayoutGetSize(&lay);
        fbAlign = data->dk.dkImageLayoutGetAlignment(&lay);
        fbSize = (fbSize + fbAlign - 1) & ~(fbAlign - 1);
        dkMemBlockMakerDefaults(&memMk, data->device, fbSize * SWITCH_DEKO3D_FB_NUM);
        memMk.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
        data->fbMem = data->dk.dkMemBlockCreate(&memMk);
        if (data->fbMem) {
            break;
        }
        data->fbW = 1280;
        data->fbH = 720;
    }
    if (!data->fbMem) {
        SDL_SetError("Could not allocate framebuffers");
        goto fail;
    }
    for (i = 0; i < SWITCH_DEKO3D_FB_NUM; i++) {
        imgs[i] = &data->fbs[i];
        data->dk.dkImageInitialize(&data->fbs[i], &lay, data->fbMem, i * fbSize);
    }
    dkSwapchainMakerDefaults(&scMk, data->device, data->window, imgs, SWITCH_DEKO3D_FB_NUM);
    data->swapchain = data->dk.dkSwapchainCreate(&scMk);
    if (!data->swapchain) {
        SDL_SetError("dkSwapchainCreate failed");
        goto fail;
    }

    dkMemBlockMakerDefaults(&memMk, data->device, SWITCH_DEKO3D_CODE_SIZE);
    memMk.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;
    data->codeMem = data->dk.dkMemBlockCreate(&memMk);
    if (!data->codeMem) {
        SDL_SetError("Could not allocate shader code");
        goto fail;
    }
    data->codeOff = 0;
    LoadCode(data, sdl_color_vsh_dksh, sdl_color_vsh_dksh_len, &data->colorVsh);
    LoadCode(data, sdl_color_fsh_dksh, sdl_color_fsh_dksh_len, &data->colorFsh);
    LoadCode(data, sdl_tex_vsh_dksh, sdl_tex_vsh_dksh_len, &data->texVsh);
    LoadCode(data, sdl_tex_fsh_dksh, sdl_tex_fsh_dksh_len, &data->texFsh);
    if (data->codeOff > SWITCH_DEKO3D_CODE_SIZE) {
        SDL_SetError("Shader code overflow");
        goto fail;
    }

    dkMemBlockMakerDefaults(&memMk, data->device, SWITCH_DEKO3D_CMD_SIZE);
    memMk.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    data->cmdMem = data->dk.dkMemBlockCreate(&memMk);
    if (!data->cmdMem) {
        SDL_SetError("Could not allocate command memory");
        goto fail;
    }
    dkCmdBufMakerDefaults(&cbMk, data->device);
    data->cmdbuf = data->dk.dkCmdBufCreate(&cbMk);
    if (!data->cmdbuf) {
        SDL_SetError("dkCmdBufCreate failed");
        goto fail;
    }
    data->dk.dkCmdBufAddMemory(data->cmdbuf, data->cmdMem, 0, SWITCH_DEKO3D_CMD_SIZE);

    dkQueueMakerDefaults(&qMk, data->device);
    qMk.flags = DkQueueFlags_Graphics;
    data->queue = data->dk.dkQueueCreate(&qMk);
    if (!data->queue) {
        SDL_SetError("dkQueueCreate failed");
        goto fail;
    }

    dkMemBlockMakerDefaults(&memMk, data->device, 0x1000); // deko3d needs 4K-multiple blocks
    memMk.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    data->uboMem = data->dk.dkMemBlockCreate(&memMk);
    if (!data->uboMem) {
        SDL_SetError("Could not allocate UBO");
        goto fail;
    }
    data->uboGpu = data->dk.dkMemBlockGetGpuAddr(data->uboMem);
    {
        float *u = (float *)data->dk.dkMemBlockGetCpuAddr(data->uboMem);
        u[0] = (float)data->fbW;
        u[1] = (float)data->fbH;
    }

    dkMemBlockMakerDefaults(&memMk, data->device, SWITCH_DEKO3D_DESC_SIZE);
    memMk.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    data->descMem = data->dk.dkMemBlockCreate(&memMk);
    if (!data->descMem) {
        SDL_SetError("Could not allocate descriptors");
        goto fail;
    }
    data->descCpu = (uint8_t *)data->dk.dkMemBlockGetCpuAddr(data->descMem);
    data->descGpu = data->dk.dkMemBlockGetGpuAddr(data->descMem);
    SDL_memset(data->descCpu, 0, SWITCH_DEKO3D_DESC_SIZE);
    WriteSamplerDesc(data);

    dkMemBlockMakerDefaults(&memMk, data->device, SWITCH_DEKO3D_VTX_RING);
    memMk.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    data->vtxMem = data->dk.dkMemBlockCreate(&memMk);
    if (!data->vtxMem) {
        SDL_SetError("Could not allocate vertex ring");
        goto fail;
    }
    data->vtxCpu = (uint8_t *)data->dk.dkMemBlockGetCpuAddr(data->vtxMem);
    data->vtxGpu = data->dk.dkMemBlockGetGpuAddr(data->vtxMem);
    data->vtxCursor = 0;

    renderer->WindowEvent = SWITCH_DEKO3D_WindowEvent;
    renderer->SupportsBlendMode = SWITCH_DEKO3D_SupportsBlendMode;
    renderer->CreateTexture = SWITCH_DEKO3D_CreateTexture;
    renderer->UpdateTexture = SWITCH_DEKO3D_UpdateTexture;
    renderer->LockTexture = SWITCH_DEKO3D_LockTexture;
    renderer->UnlockTexture = SWITCH_DEKO3D_UnlockTexture;
    renderer->SetRenderTarget = SWITCH_DEKO3D_SetRenderTarget;
    renderer->QueueSetViewport = SWITCH_DEKO3D_QueueNoOp;
    renderer->QueueSetDrawColor = SWITCH_DEKO3D_QueueSetDrawColor;
    renderer->QueueDrawPoints = SWITCH_DEKO3D_QueueDrawPoints;
    renderer->QueueDrawLines = SWITCH_DEKO3D_QueueDrawLines;
    renderer->QueueGeometry = SWITCH_DEKO3D_QueueGeometry;
    renderer->InvalidateCachedState = SWITCH_DEKO3D_InvalidateCachedState;
    renderer->RunCommandQueue = SWITCH_DEKO3D_RunCommandQueue;
    renderer->RenderReadPixels = SWITCH_DEKO3D_RenderReadPixels;
    renderer->RenderPresent = SWITCH_DEKO3D_RenderPresent;
    renderer->DestroyTexture = SWITCH_DEKO3D_DestroyTexture;
    renderer->DestroyRenderer = SWITCH_DEKO3D_DestroyRenderer;
    renderer->SetVSync = SWITCH_DEKO3D_SetVSync;

    renderer->internal = data;
    renderer->window = window;

    renderer->name = SWITCH_DEKO3D_RenderDriver.name;
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_RGBA8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_ARGB8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_ABGR8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_BGRA8888);

    SDL_SetPointerProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_DEKO3D_NWINDOW_POINTER, nwindow);
    SDL_SetPointerProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_DEKO3D_DEVICE_POINTER, (void *)data->device);
    SDL_SetPointerProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_DEKO3D_QUEUE_POINTER, (void *)data->queue);

    return true;

fail:
    SWITCH_DEKO3D_Teardown(data);
    SDL_Deko3dDispatch_Unload(&data->dk, data->neko3d);
    SDL_free(data);
    return false;
}

#endif // SDL_VIDEO_RENDER_SWITCH_DEKO3D
