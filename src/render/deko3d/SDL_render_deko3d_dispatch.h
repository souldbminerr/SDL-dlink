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

#ifndef SDL_RENDER_DEKO3D_DISPATCH_H
#define SDL_RENDER_DEKO3D_DISPATCH_H

#include "SDL_internal.h"

#ifdef SDL_VIDEO_RENDER_SWITCH_DEKO3D

#include <deko3d.h>

#define SDL_DEKO3D_FUNCTIONS \
    SDL_DEKO3D_FN(void, dkCmdBufAddMemory, (DkCmdBuf obj, DkMemBlock mem, uint32_t offset, uint32_t size)) \
    SDL_DEKO3D_FN(void, dkCmdBufBarrier, (DkCmdBuf obj, DkBarrier mode, uint32_t invalidateFlags)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindBlendStates, (DkCmdBuf obj, uint32_t firstId, DkBlendState const states[], uint32_t numStates)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindColorState, (DkCmdBuf obj, DkColorState const* state)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindColorWriteState, (DkCmdBuf obj, DkColorWriteState const* state)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindImageDescriptorSet, (DkCmdBuf obj, DkGpuAddr setAddr, uint32_t numDescriptors)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindRasterizerState, (DkCmdBuf obj, DkRasterizerState const* state)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindRenderTargets, (DkCmdBuf obj, DkImageView const* const colorTargets[], uint32_t numColorTargets, DkImageView const* depthTarget)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindSamplerDescriptorSet, (DkCmdBuf obj, DkGpuAddr setAddr, uint32_t numDescriptors)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindShaders, (DkCmdBuf obj, uint32_t stageMask, DkShader const* const shaders[], uint32_t numShaders)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindTextures, (DkCmdBuf obj, DkStage stage, uint32_t firstId, DkResHandle const handles[], uint32_t numHandles)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindUniformBuffers, (DkCmdBuf obj, DkStage stage, uint32_t firstId, DkBufExtents const buffers[], uint32_t numBuffers)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindVtxAttribState, (DkCmdBuf obj, DkVtxAttribState const attribs[], uint32_t numAttribs)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindVtxBuffers, (DkCmdBuf obj, uint32_t firstId, DkBufExtents const buffers[], uint32_t numBuffers)) \
    SDL_DEKO3D_FN(void, dkCmdBufBindVtxBufferState, (DkCmdBuf obj, DkVtxBufferState const buffers[], uint32_t numBuffers)) \
    SDL_DEKO3D_FN(void, dkCmdBufClear, (DkCmdBuf obj)) \
    SDL_DEKO3D_FN(DkCmdBuf, dkCmdBufCreate, (DkCmdBufMaker const* maker)) \
    SDL_DEKO3D_FN(void, dkCmdBufDestroy, (DkCmdBuf obj)) \
    SDL_DEKO3D_FN(void, dkCmdBufDraw, (DkCmdBuf obj, DkPrimitive prim, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)) \
    SDL_DEKO3D_FN(DkCmdList, dkCmdBufFinishList, (DkCmdBuf obj)) \
    SDL_DEKO3D_FN(void, dkCmdBufSetScissors, (DkCmdBuf obj, uint32_t firstId, DkScissor const scissors[], uint32_t numScissors)) \
    SDL_DEKO3D_FN(void, dkCmdBufSetViewports, (DkCmdBuf obj, uint32_t firstId, DkViewport const viewports[], uint32_t numViewports)) \
    SDL_DEKO3D_FN(DkDevice, dkDeviceCreate, (DkDeviceMaker const* maker)) \
    SDL_DEKO3D_FN(void, dkDeviceDestroy, (DkDevice obj)) \
    SDL_DEKO3D_FN(void, dkImageDescriptorInitialize, (DkImageDescriptor* obj, DkImageView const* view, bool usesLoadOrStore, bool decayMS)) \
    SDL_DEKO3D_FN(void, dkImageInitialize, (DkImage* obj, DkImageLayout const* layout, DkMemBlock memBlock, uint32_t offset)) \
    SDL_DEKO3D_FN(uint32_t, dkImageLayoutGetAlignment, (DkImageLayout const* obj)) \
    SDL_DEKO3D_FN(uint64_t, dkImageLayoutGetSize, (DkImageLayout const* obj)) \
    SDL_DEKO3D_FN(void, dkImageLayoutInitialize, (DkImageLayout* obj, DkImageLayoutMaker const* maker)) \
    SDL_DEKO3D_FN(DkMemBlock, dkMemBlockCreate, (DkMemBlockMaker const* maker)) \
    SDL_DEKO3D_FN(void, dkMemBlockDestroy, (DkMemBlock obj)) \
    SDL_DEKO3D_FN(void*, dkMemBlockGetCpuAddr, (DkMemBlock obj)) \
    SDL_DEKO3D_FN(DkGpuAddr, dkMemBlockGetGpuAddr, (DkMemBlock obj)) \
    SDL_DEKO3D_FN(int, dkQueueAcquireImage, (DkQueue obj, DkSwapchain swapchain)) \
    SDL_DEKO3D_FN(DkQueue, dkQueueCreate, (DkQueueMaker const* maker)) \
    SDL_DEKO3D_FN(void, dkQueueDestroy, (DkQueue obj)) \
    SDL_DEKO3D_FN(void, dkQueuePresentImage, (DkQueue obj, DkSwapchain swapchain, int imageSlot)) \
    SDL_DEKO3D_FN(void, dkQueueSubmitCommands, (DkQueue obj, DkCmdList cmds)) \
    SDL_DEKO3D_FN(void, dkQueueWaitIdle, (DkQueue obj)) \
    SDL_DEKO3D_FN(void, dkSamplerDescriptorInitialize, (DkSamplerDescriptor* obj, DkSampler const* sampler)) \
    SDL_DEKO3D_FN(void, dkShaderInitialize, (DkShader* obj, DkShaderMaker const* maker)) \
    SDL_DEKO3D_FN(DkSwapchain, dkSwapchainCreate, (DkSwapchainMaker const* maker)) \
    SDL_DEKO3D_FN(void, dkSwapchainDestroy, (DkSwapchain obj))

typedef struct SDL_Deko3dDispatch
{
#define SDL_DEKO3D_FN(ret, name, args) ret (*name) args;
    SDL_DEKO3D_FUNCTIONS
#undef SDL_DEKO3D_FN
} SDL_Deko3dDispatch;

extern bool SDL_Deko3dDispatch_Load(SDL_Deko3dDispatch *dispatch, SDL_SharedObject **phandle);

extern void SDL_Deko3dDispatch_Unload(SDL_Deko3dDispatch *dispatch, SDL_SharedObject *handle);

#endif // SDL_VIDEO_RENDER_SWITCH_DEKO3D

#endif // SDL_RENDER_DEKO3D_DISPATCH_H
