// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Core/Profiler.h"
#include "../Graphics/AnimatedModel.h"
#include "../Graphics/Animation.h"
#include "../Graphics/AnimationController.h"
#include "../Graphics/Camera.h"
#include "../Graphics/CustomGeometry.h"
#include "../Graphics/DebugRenderer.h"
#include "../Graphics/DecalSet.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/GraphicsEvents.h"
#include "../Graphics/Material.h"
#include "../Graphics/Octree.h"
#include "../Graphics/ParticleEffect.h"
#include "../Graphics/ParticleEmitter.h"
#include "../Graphics/RibbonTrail.h"
#include "../Graphics/Skybox.h"
#include "../Graphics/StaticModelGroup.h"
#include "../Graphics/Technique.h"
#include "../Graphics/Terrain.h"
#include "../Graphics/TerrainPatch.h"
#include "../Graphics/Zone.h"
#include "../GraphicsAPI/GraphicsImpl.h"
#ifdef URHO3D_VULKAN
#include "../GraphicsAPI/Vulkan/VulkanGraphicsImpl.h"
#endif
#include "../GraphicsAPI/Shader.h"
#include "../GraphicsAPI/ShaderPrecache.h"
#include "../GraphicsAPI/Texture2D.h"
#include "../GraphicsAPI/Texture2DArray.h"
#include "../GraphicsAPI/Texture3D.h"
#include "../GraphicsAPI/TextureCube.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"

#include <SDL/SDL.h>

#include "../DebugNew.h"

namespace Urho3D
{

void Graphics::SetExternalWindow(void* window)
{
    if (!window_)
        externalWindow_ = window;
    else
        URHO3D_LOGERROR("Window already opened, can not set external window");
}

void Graphics::SetWindowTitle(const String& windowTitle)
{
    windowTitle_ = windowTitle;
    if (window_)
        SDL_SetWindowTitle(window_, windowTitle_.CString());
}

void Graphics::SetWindowIcon(Image* windowIcon)
{
    windowIcon_ = windowIcon;
    if (window_)
        CreateWindowIcon();
}

void Graphics::SetWindowPosition(const IntVector2& position)
{
    if (window_)
        SDL_SetWindowPosition(window_, position.x_, position.y_);
    else
        position_ = position; // Sets as initial position for SDL_CreateWindow()
}

void Graphics::SetWindowPosition(int x, int y)
{
    SetWindowPosition(IntVector2(x, y));
}

void Graphics::SetOrientations(const String& orientations)
{
    orientations_ = orientations.Trimmed();
    SDL_SetHint(SDL_HINT_ORIENTATIONS, orientations_.CString());
}

bool Graphics::SetScreenMode(int width, int height)
{
    return SetScreenMode(width, height, screenParams_);
}

bool Graphics::SetWindowModes(const WindowModeParams& windowMode, const WindowModeParams& secondaryWindowMode, bool maximize)
{
    primaryWindowMode_ = windowMode;
    secondaryWindowMode_ = secondaryWindowMode;
    return SetScreenMode(primaryWindowMode_.width_, primaryWindowMode_.height_, primaryWindowMode_.screenParams_, maximize);
}

bool Graphics::SetDefaultWindowModes(int width, int height, const ScreenModeParams& params)
{
    // Fill window mode to be applied now
    WindowModeParams primaryWindowMode;
    primaryWindowMode.width_ = width;
    primaryWindowMode.height_ = height;
    primaryWindowMode.screenParams_ = params;

    // Fill window mode to be applied on Graphics::ToggleFullscreen
    WindowModeParams secondaryWindowMode = primaryWindowMode;

    // Pick resolution automatically
    secondaryWindowMode.width_ = 0;
    secondaryWindowMode.height_ = 0;

    if (params.fullscreen_ || params.borderless_ || params.spanned_)
    {
        secondaryWindowMode.screenParams_.fullscreen_ = false;
        secondaryWindowMode.screenParams_.borderless_ = false;
        secondaryWindowMode.screenParams_.spanned_ = false;
    }
    else
    {
        secondaryWindowMode.screenParams_.borderless_ = true;
    }

    const bool maximize = (!width || !height) && !params.fullscreen_ && !params.borderless_ && !params.spanned_ && params.resizable_;
    return SetWindowModes(primaryWindowMode, secondaryWindowMode, maximize);
}

bool Graphics::SetMode(int width, int height, bool fullscreen, bool borderless, bool resizable,
    bool highDPI, bool vsync, bool tripleBuffer, int multiSample, int monitor, int refreshRate)
{
    ScreenModeParams params;
    params.fullscreen_ = fullscreen;
    params.borderless_ = borderless;
    params.resizable_ = resizable;
    params.highDPI_ = highDPI;
    params.vsync_ = vsync;
    params.tripleBuffer_ = tripleBuffer;
    params.multiSample_ = multiSample;
    params.monitor_ = monitor;
    params.refreshRate_ = refreshRate;

    return SetDefaultWindowModes(width, height, params);
}

bool Graphics::SetMode(int width, int height)
{
    return SetDefaultWindowModes(width, height, screenParams_);
}

bool Graphics::ToggleFullscreen()
{
    Swap(primaryWindowMode_, secondaryWindowMode_);
    return SetScreenMode(primaryWindowMode_.width_, primaryWindowMode_.height_, primaryWindowMode_.screenParams_);
}

void Graphics::SetShaderParameter(StringHash param, const Variant& value)
{
    switch (value.GetType())
    {
    case VAR_BOOL:
        SetShaderParameter(param, value.GetBool());
        break;

    case VAR_INT:
        SetShaderParameter(param, value.GetI32());
        break;

    case VAR_FLOAT:
    case VAR_DOUBLE:
        SetShaderParameter(param, value.GetFloat());
        break;

    case VAR_VECTOR2:
        SetShaderParameter(param, value.GetVector2());
        break;

    case VAR_VECTOR3:
        SetShaderParameter(param, value.GetVector3());
        break;

    case VAR_VECTOR4:
        SetShaderParameter(param, value.GetVector4());
        break;

    case VAR_COLOR:
        SetShaderParameter(param, value.GetColor());
        break;

    case VAR_MATRIX3:
        SetShaderParameter(param, value.GetMatrix3());
        break;

    case VAR_MATRIX3X4:
        SetShaderParameter(param, value.GetMatrix3x4());
        break;

    case VAR_MATRIX4:
        SetShaderParameter(param, value.GetMatrix4());
        break;

    case VAR_BUFFER:
        {
            const Vector<byte>& buffer = value.GetBuffer();
            if (buffer.Size() >= sizeof(float))
                SetShaderParameter(param, reinterpret_cast<const float*>(&buffer[0]), buffer.Size() / sizeof(float));
        }
        break;

    default:
        // Unsupported parameter type, do nothing
        break;
    }
}

IntVector2 Graphics::GetWindowPosition() const
{
    if (window_)
    {
        IntVector2 position;
        SDL_GetWindowPosition(window_, &position.x_, &position.y_);
        return position;
    }
    return position_;
}

Vector<IntVector3> Graphics::GetResolutions(int monitor) const
{
    Vector<IntVector3> ret;

    // Emscripten is not able to return a valid list
#ifndef __EMSCRIPTEN__
    i32 numModes = SDL_GetNumDisplayModes(monitor);

    for (i32 i = 0; i < numModes; ++i)
    {
        SDL_DisplayMode mode;
        SDL_GetDisplayMode(monitor, i, &mode);
        int width = mode.w;
        int height = mode.h;
        int rate = mode.refresh_rate;

        // Store mode if unique
        bool unique = true;
        for (const IntVector3& resolution : ret)
        {
            if (resolution.x_ == width && resolution.y_ == height && resolution.z_ == rate)
            {
                unique = false;
                break;
            }
        }

        if (unique)
            ret.Push(IntVector3(width, height, rate));
    }
#endif

    return ret;
}

i32 Graphics::FindBestResolutionIndex(int monitor, int width, int height, int refreshRate) const
{
    const Vector<IntVector3> resolutions = GetResolutions(monitor);
    if (resolutions.Empty())
        return NINDEX;

    i32 best = 0;
    i32 bestError = M_MAX_INT;

    for (i32 i = 0; i < resolutions.Size(); ++i)
    {
        i32 error = Abs(resolutions[i].x_ - width) + Abs(resolutions[i].y_ - height);
        if (refreshRate != 0)
            error += Abs(resolutions[i].z_ - refreshRate);
        if (error < bestError)
        {
            best = i;
            bestError = error;
        }
    }

    return best;
}

IntVector2 Graphics::GetDesktopResolution(int monitor) const
{
#if !defined(__ANDROID__) && !defined(IOS) && !defined(TVOS)
    SDL_DisplayMode mode;
    SDL_GetDesktopDisplayMode(monitor, &mode);
    return IntVector2(mode.w, mode.h);
#else
    // SDL_GetDesktopDisplayMode() may not work correctly on mobile platforms. Rather return the window size
    return IntVector2(width_, height_);
#endif
}

int Graphics::GetMonitorCount() const
{
    return SDL_GetNumVideoDisplays();
}

int Graphics::GetCurrentMonitor() const
{
    return window_ ? SDL_GetWindowDisplayIndex(window_) : 0;
}

bool Graphics::GetMaximized() const
{
    return window_? static_cast<bool>(SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) : false;
}

Vector3 Graphics::GetDisplayDPI(int monitor) const
{
    Vector3 result;
    SDL_GetDisplayDPI(monitor, &result.z_, &result.x_, &result.y_);
    return result;
}

unsigned Graphics::GetFrameIndex() const
{
#ifdef URHO3D_VULKAN
    if (GetGAPI() == GAPI_VULKAN)
    {
        VulkanGraphicsImpl* vkImpl = GetImpl_Vulkan();
        return vkImpl ? vkImpl->GetFrameIndex() : 0;
    }
#endif
    return 0;
}

unsigned Graphics::GetMaxFramesInFlight()
{
#ifdef URHO3D_VULKAN
    if (GetGAPI() == GAPI_VULKAN)
        return 4;  // Must match VulkanGraphicsImpl::MAX_FRAMES_IN_FLIGHT
#endif
    return 1;
}

void Graphics::Maximize()
{
    if (!window_)
        return;

    SDL_MaximizeWindow(window_);
}

void Graphics::Minimize()
{
    if (!window_)
        return;

    SDL_MinimizeWindow(window_);
}

void Graphics::Raise() const
{
    if (!window_)
        return;

    SDL_RaiseWindow(window_);
}

void Graphics::BeginDumpShaders(const String& fileName)
{
    shaderPrecache_ = new ShaderPrecache(context_, fileName);
}

void Graphics::EndDumpShaders()
{
    shaderPrecache_.Reset();
}

void Graphics::PrecacheShaders(Deserializer& source)
{
    URHO3D_PROFILE(PrecacheShaders);

    ShaderPrecache::LoadShaders(this, source);
}

void Graphics::SetShaderCacheDir(const String& path)
{
    String trimmedPath = path.Trimmed();
    if (trimmedPath.Length())
        shaderCacheDir_ = AddTrailingSlash(trimmedPath);
}

void Graphics::AddGPUObject(GPUObject* object)
{
    MutexLock lock(gpuObjectMutex_);

    gpuObjects_.Push(object);
}

void Graphics::RemoveGPUObject(GPUObject* object)
{
    MutexLock lock(gpuObjectMutex_);

    gpuObjects_.Remove(object);
}

void* Graphics::ReserveScratchBuffer(i32 size)
{
    assert(size >= 0);

    if (!size)
        return nullptr;

    if (size > maxScratchBufferRequest_)
        maxScratchBufferRequest_ = size;

    // First check for a free buffer that is large enough
    for (ScratchBuffer& scratchBuffer : scratchBuffers_)
    {
        if (!scratchBuffer.reserved_ && scratchBuffer.size_ >= size)
        {
            scratchBuffer.reserved_ = true;
            return scratchBuffer.data_.Get();
        }
    }

    // Then check if a free buffer can be resized
    for (ScratchBuffer& scratchBuffer : scratchBuffers_)
    {
        if (!scratchBuffer.reserved_)
        {
            scratchBuffer.data_ = new u8[size];
            scratchBuffer.size_ = size;
            scratchBuffer.reserved_ = true;

            URHO3D_LOGDEBUG("Resized scratch buffer to size " + String(size));

            return scratchBuffer.data_.Get();
        }
    }

    // Finally allocate a new buffer
    ScratchBuffer newBuffer;
    newBuffer.data_ = new u8[size];
    newBuffer.size_ = size;
    newBuffer.reserved_ = true;
    scratchBuffers_.Push(newBuffer);

    URHO3D_LOGDEBUG("Allocated scratch buffer with size " + String(size));

    return newBuffer.data_.Get();
}

void Graphics::FreeScratchBuffer(void* buffer)
{
    if (!buffer)
        return;

    for (ScratchBuffer& scratchBuffer : scratchBuffers_)
    {
        if (scratchBuffer.reserved_ && scratchBuffer.data_.Get() == buffer)
        {
            scratchBuffer.reserved_ = false;
            return;
        }
    }

    URHO3D_LOGWARNING("Reserved scratch buffer " + ToStringHex((unsigned)(size_t)buffer) + " not found");
}

void Graphics::CleanupScratchBuffers()
{
    for (ScratchBuffer& scratchBuffer : scratchBuffers_)
    {
        if (!scratchBuffer.reserved_ && scratchBuffer.size_ > maxScratchBufferRequest_ * 2 && scratchBuffer.size_ >= 1024 * 1024)
        {
            scratchBuffer.data_ = maxScratchBufferRequest_ > 0 ? (new u8[maxScratchBufferRequest_]) : nullptr;
            scratchBuffer.size_ = maxScratchBufferRequest_;

            URHO3D_LOGDEBUG("Resized scratch buffer to size " + String(maxScratchBufferRequest_));
        }
    }

    maxScratchBufferRequest_ = 0;
}

void Graphics::CreateWindowIcon()
{
    if (windowIcon_)
    {
        SDL_Surface* surface = windowIcon_->GetSDLSurface();
        if (surface)
        {
            SDL_SetWindowIcon(window_, surface);
            SDL_FreeSurface(surface);
        }
    }
}

IntRect Graphics::GetSpannedBounds()
{
    int numDisplays = SDL_GetNumVideoDisplays();
    if (numDisplays <= 0)
        return IntRect(0, 0, 1920, 1080);

    int minX = M_MAX_INT, minY = M_MAX_INT;
    int maxX = M_MIN_INT, maxY = M_MIN_INT;
    for (int i = 0; i < numDisplays; ++i)
    {
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(i, &bounds) == 0)
        {
            minX = Min(minX, bounds.x);
            minY = Min(minY, bounds.y);
            maxX = Max(maxX, bounds.x + bounds.w);
            maxY = Max(maxY, bounds.y + bounds.h);
        }
    }
    return IntRect(minX, minY, maxX, maxY);
}

void Graphics::AdjustScreenMode(int& newWidth, int& newHeight, ScreenModeParams& params, bool& maximize) const
{
    // High DPI is supported only for OpenGL backend
    if (Graphics::GetGAPI() != GAPI_OPENGL)
        params.highDPI_ = false;

#if defined(IOS) || defined(TVOS)
    // iOS and tvOS app always take the fullscreen (and with status bar hidden)
    params.fullscreen_ = true;
#endif

    // Make sure monitor index is not bigger than the currently detected monitors
    const int numMonitors = SDL_GetNumVideoDisplays();
    if (params.monitor_ >= numMonitors || params.monitor_ < 0)
        params.monitor_ = 0; // this monitor is not present, use first monitor

    // Fullscreen or Borderless can not be resizable and cannot be maximized
    if (params.fullscreen_ || params.borderless_)
    {
        params.resizable_ = false;
        maximize = false;
    }

    // Borderless cannot be fullscreen, they are mutually exclusive
    if (params.borderless_)
        params.fullscreen_ = false;

    // Spanned is mutually exclusive with fullscreen and borderless
    if (params.spanned_)
    {
        params.fullscreen_ = false;
        params.borderless_ = false;
        params.resizable_ = false;
        maximize = false;
    }

    // On iOS window needs to be resizable to handle orientation changes properly
#ifdef IOS
    if (!externalWindow_)
        params.resizable_ = true;
#endif

    // Ensure that multisample factor is in valid range
    params.multiSample_ = NextPowerOfTwo(Clamp(params.multiSample_, 1, 16));

    // If zero dimensions in windowed mode, set windowed mode to maximize and set a predefined default restored window size.
    // If zero in fullscreen, use desktop mode. If spanned, use union of all monitor bounds.
    if (!newWidth || !newHeight)
    {
        if (params.spanned_)
        {
            IntRect bounds = GetSpannedBounds();
            newWidth = bounds.right_ - bounds.left_;
            newHeight = bounds.bottom_ - bounds.top_;
        }
        else if (params.fullscreen_ || params.borderless_)
        {
            SDL_DisplayMode mode;
            SDL_GetDesktopDisplayMode(params.monitor_, &mode);
            newWidth = mode.w;
            newHeight = mode.h;
        }
        else
        {
            newWidth = 1024;
            newHeight = 768;
        }
    }

    // Check fullscreen mode validity (desktop only). Use a closest match if not found
#ifdef DESKTOP_GRAPHICS
    if (params.fullscreen_)
    {
        const Vector<IntVector3> resolutions = GetResolutions(params.monitor_);
        if (!resolutions.Empty())
        {
            const i32 bestResolution = FindBestResolutionIndex(params.monitor_,
                newWidth, newHeight, params.refreshRate_);
            newWidth = resolutions[bestResolution].x_;
            newHeight = resolutions[bestResolution].y_;
            params.refreshRate_ = resolutions[bestResolution].z_;
        }
    }
    else
    {
        // If windowed, use the same refresh rate as desktop
        SDL_DisplayMode mode;
        SDL_GetDesktopDisplayMode(params.monitor_, &mode);
        params.refreshRate_ = mode.refresh_rate;
    }
#endif
}

void Graphics::OnScreenModeChanged()
{
#ifdef URHO3D_LOGGING
    String msg;
    msg.AppendWithFormat("Set screen mode %dx%d rate %d Hz %s monitor %d", width_, height_, screenParams_.refreshRate_,
        (screenParams_.fullscreen_ ? "fullscreen" : "windowed"), screenParams_.monitor_);
    if (screenParams_.borderless_)
        msg.Append(" borderless");
    if (screenParams_.spanned_)
        msg.Append(" spanned");
    if (screenParams_.resizable_)
        msg.Append(" resizable");
    if (screenParams_.highDPI_)
        msg.Append(" highDPI");
    if (screenParams_.multiSample_ > 1)
        msg.AppendWithFormat(" multisample %d", screenParams_.multiSample_);
    URHO3D_LOGINFO(msg);
#endif

    using namespace ScreenMode;

    VariantMap& eventData = GetEventDataMap();
    eventData[P_WIDTH] = width_;
    eventData[P_HEIGHT] = height_;
    eventData[P_FULLSCREEN] = screenParams_.fullscreen_;
    eventData[P_BORDERLESS] = screenParams_.borderless_;
    eventData[P_SPANNED] = screenParams_.spanned_;
    eventData[P_RESIZABLE] = screenParams_.resizable_;
    eventData[P_HIGHDPI] = screenParams_.highDPI_;
    eventData[P_MONITOR] = screenParams_.monitor_;
    eventData[P_REFRESHRATE] = screenParams_.refreshRate_;
    SendEvent(E_SCREENMODE, eventData);
}

Graphics::Graphics(Context* context, GAPI gapi)
    : Object(context)
{
    Graphics::gapi = gapi;

    // Initialize viewport to default 1024x768 (will be updated by SetViewport after window creation)
    viewport_ = IntRect(0, 0, 1024, 768);

    // In-engine timeline profiler (Phase 1 of PLAN_PROFILER_TIMELINE).
    // Always created — backend-independent CPU recording. GPU lanes (Phase 2)
    // hang off the same instance via Vulkan timestamp queries.
    profilerTimeline_ = new ProfilerTimeline();

#ifdef URHO3D_VULKAN
    // Create Vulkan profiler instance
    vulkanProfiler_ = new VulkanProfiler();
#endif

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
    {
        Constructor_OGL();
        return;
    }
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
    {
        Constructor_D3D11();
        return;
    }
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
    {
        Constructor_Vulkan();
        return;
    }
#endif
}

Graphics::~Graphics()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
    {
        Destructor_OGL();
        return;
    }
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
    {
        Destructor_D3D11();
        return;
    }
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
    {
        Destructor_Vulkan();
        return;
    }
#endif
}

bool Graphics::SetScreenMode(int width, int height, const ScreenModeParams& params, bool maximize)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetScreenMode_OGL(width, height, params, maximize);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetScreenMode_D3D11(width, height, params, maximize);;
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetScreenMode_Vulkan(width, height, params, maximize);
#endif

    return {}; // Prevent warning
}

void Graphics::SetSRGB(bool enable)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetSRGB_OGL(enable);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetSRGB_D3D11(enable);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetSRGB_Vulkan(enable);
#endif
}

void Graphics::SetDither(bool enable)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetDither_OGL(enable);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetDither_D3D11(enable);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetDither_Vulkan(enable);
#endif
}

void Graphics::SetFlushGPU(bool enable)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetFlushGPU_OGL(enable);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetFlushGPU_D3D11(enable);;
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetFlushGPU_Vulkan(enable);
#endif
}

void Graphics::SetForceGL2(bool enable)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetForceGL2_OGL(enable);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetForceGL2_D3D11(enable);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetForceGL2_Vulkan(enable);
#endif
}

void Graphics::Close()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return Close_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return Close_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return Close_Vulkan();
#endif
}

bool Graphics::TakeScreenShot(Image& destImage)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return TakeScreenShot_OGL(destImage);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return TakeScreenShot_D3D11(destImage);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return TakeScreenShot_Vulkan(destImage);
#endif

    return {}; // Prevent warning
}

bool Graphics::BeginFrame()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return BeginFrame_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return BeginFrame_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return BeginFrame_Vulkan();
#endif

    return {}; // Prevent warning
}

void Graphics::EndFrame()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return EndFrame_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return EndFrame_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return EndFrame_Vulkan();
#endif
}

void Graphics::Clear(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return Clear_OGL(flags, color, depth, stencil);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return Clear_D3D11(flags, color, depth, stencil);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return Clear_Vulkan(flags, color, depth, stencil);
#endif
}

bool Graphics::ResolveToTexture(Texture2D* destination, const IntRect& viewport)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return ResolveToTexture_OGL(destination, viewport);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return ResolveToTexture_D3D11(destination, viewport);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return ResolveToTexture_Vulkan(destination, viewport);
#endif

    return {}; // Prevent warning
}

bool Graphics::ResolveToTexture(Texture2D* texture)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return ResolveToTexture_OGL(texture);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return ResolveToTexture_D3D11(texture);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return ResolveToTexture_Vulkan(texture);
#endif

    return {}; // Prevent warning
}

bool Graphics::ResolveToTexture(TextureCube* texture)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return ResolveToTexture_OGL(texture);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return ResolveToTexture_D3D11(texture);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return ResolveToTexture_Vulkan(texture);
#endif

    return {}; // Prevent warning
}

void Graphics::Draw(PrimitiveType type, unsigned vertexStart, unsigned vertexCount)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return Draw_OGL(type, vertexStart, vertexCount);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return Draw_Vulkan(type, vertexStart, vertexCount);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return Draw_D3D11(type, vertexStart, vertexCount);;
#endif
}

void Graphics::Draw(PrimitiveType type, unsigned indexStart, unsigned indexCount, unsigned minVertex, unsigned vertexCount)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return Draw_OGL(type, indexStart, indexCount, minVertex, vertexCount);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return Draw_Vulkan(type, indexStart, indexCount, minVertex, vertexCount);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return Draw_D3D11(type, indexStart, indexCount, minVertex, vertexCount);
#endif
}

void Graphics::Draw(PrimitiveType type, unsigned indexStart, unsigned indexCount, unsigned baseVertexIndex, unsigned minVertex, unsigned vertexCount)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return Draw_OGL(type, indexStart, indexCount, baseVertexIndex, minVertex, vertexCount);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return Draw_Vulkan(type, indexStart, indexCount, baseVertexIndex, minVertex, vertexCount);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return Draw_D3D11(type, indexStart, indexCount, baseVertexIndex, minVertex, vertexCount);
#endif
}

void Graphics::DrawInstanced(PrimitiveType type, unsigned indexStart, unsigned indexCount, unsigned minVertex, unsigned vertexCount, unsigned instanceCount)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return DrawInstanced_OGL(type, indexStart, indexCount, minVertex, vertexCount, instanceCount);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return DrawInstanced_Vulkan(type, indexStart, indexCount, minVertex, vertexCount, instanceCount);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return DrawInstanced_D3D11(type, indexStart, indexCount, minVertex, vertexCount, instanceCount);
#endif
}

void Graphics::DrawInstanced(PrimitiveType type, unsigned indexStart, unsigned indexCount, unsigned baseVertexIndex, unsigned minVertex,
    unsigned vertexCount, unsigned instanceCount)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return DrawInstanced_OGL(type, indexStart, indexCount, baseVertexIndex, minVertex, vertexCount, instanceCount);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return DrawInstanced_Vulkan(type, indexStart, indexCount, baseVertexIndex, minVertex, vertexCount, instanceCount, 0);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return DrawInstanced_D3D11(type, indexStart, indexCount, baseVertexIndex, minVertex, vertexCount, instanceCount);
#endif
}

void Graphics::DispatchCompute(unsigned groupCountX, unsigned groupCountY, unsigned groupCountZ)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return DispatchCompute_Vulkan(groupCountX, groupCountY, groupCountZ);
#endif

    // Compute shaders only supported on Vulkan currently
    URHO3D_LOGWARNING("DispatchCompute: Compute shaders only supported on Vulkan backend");
}

void Graphics::SetComputeShader(ShaderVariation* shader)
{
#ifdef URHO3D_VULKAN
    if (Graphics::GetGAPI() == GAPI_VULKAN)
    {
        SetComputeShader_Vulkan(shader);
        return;
    }
#endif
}

void Graphics::SetStorageBuffer(unsigned index, VertexBuffer* buffer)
{
#ifdef URHO3D_VULKAN
    if (Graphics::GetGAPI() == GAPI_VULKAN)
    {
        SetStorageBuffer_Vulkan(index, buffer);
        return;
    }
#endif
}

void Graphics::BeginComputeBatch()
{
#ifdef URHO3D_VULKAN
    if (Graphics::GetGAPI() == GAPI_VULKAN)
    {
        BeginComputeBatch_Vulkan();
        return;
    }
#endif
}

void Graphics::EndComputeBatch()
{
#ifdef URHO3D_VULKAN
    if (Graphics::GetGAPI() == GAPI_VULKAN)
    {
        EndComputeBatch_Vulkan();
        return;
    }
#endif
}

void Graphics::EndComputeBatchAsync()
{
#ifdef URHO3D_VULKAN
    if (Graphics::GetGAPI() == GAPI_VULKAN)
    {
        EndComputeBatchAsync_Vulkan();
        return;
    }
#endif
}

bool Graphics::PollComputeBatch()
{
#ifdef URHO3D_VULKAN
    if (Graphics::GetGAPI() == GAPI_VULKAN)
        return PollComputeBatch_Vulkan();
#endif
    return true;
}

void Graphics::SetVertexBuffer(VertexBuffer* buffer)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetVertexBuffer_Vulkan(0, buffer);
#endif

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetVertexBuffer_OGL(buffer);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetVertexBuffer_D3D11(buffer);
#endif
}

bool Graphics::SetVertexBuffers(const Vector<VertexBuffer*>& buffers, unsigned instanceOffset)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetVertexBuffers_OGL(buffers, instanceOffset);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetVertexBuffers_D3D11(buffers, instanceOffset);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetVertexBuffers_Vulkan(buffers, instanceOffset);
#endif

    return {}; // Prevent warning
}

bool Graphics::SetVertexBuffers(const Vector<SharedPtr<VertexBuffer>>& buffers, unsigned instanceOffset)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetVertexBuffers_OGL(buffers, instanceOffset);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetVertexBuffers_D3D11(buffers, instanceOffset);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetVertexBuffers_Vulkan(buffers, instanceOffset);
#endif

    return {}; // Prevent warning
}

void Graphics::SetIndexBuffer(IndexBuffer* buffer)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetIndexBuffer_OGL(buffer);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetIndexBuffer_D3D11(buffer);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetIndexBuffer_Vulkan(buffer);
#endif
}

void Graphics::SetShaders(ShaderVariation* vs, ShaderVariation* ps)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetShaders_OGL(vs, ps);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetShaders_D3D11(vs, ps);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetShaders_Vulkan(vs, ps, nullptr);
#endif
}

void Graphics::SetShaderParameter(StringHash param, const float* data, unsigned count)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetShaderParameter_OGL(param, data, count);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetShaderParameter_D3D11(param, data, count);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetShaderParameter_Vulkan(param, Variant(Vector<byte>((const byte*)data, count * sizeof(float))));
#endif
}

void Graphics::SetShaderParameter(StringHash param, float value)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetShaderParameter_OGL(param, value);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetShaderParameter_D3D11(param, value);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetShaderParameter_Vulkan(param, Variant(value));
#endif
}

void Graphics::SetShaderParameter(StringHash param, int value)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetShaderParameter_OGL(param, value);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetShaderParameter_D3D11(param, value);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetShaderParameter_Vulkan(param, Variant(value));
#endif
}

void Graphics::SetShaderParameter(StringHash param, bool value)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetShaderParameter_OGL(param, value);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetShaderParameter_D3D11(param, value);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetShaderParameter_Vulkan(param, Variant(value));
#endif
}

void Graphics::SetShaderParameter(StringHash param, const Color& color)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetShaderParameter_OGL(param, color);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetShaderParameter_D3D11(param, color);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetShaderParameter_Vulkan(param, Variant(color));
#endif
}

void Graphics::SetShaderParameter(StringHash param, const Vector2& vector)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetShaderParameter_OGL(param, vector);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetShaderParameter_D3D11(param, vector);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetShaderParameter_Vulkan(param, Variant(vector));
#endif
}

void Graphics::SetShaderParameter(StringHash param, const Matrix3& matrix)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetShaderParameter_OGL(param, matrix);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetShaderParameter_D3D11(param, matrix);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetShaderParameter_Vulkan(param, Variant(matrix));
#endif
}

void Graphics::SetShaderParameter(StringHash param, const Vector3& vector)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetShaderParameter_OGL(param, vector);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetShaderParameter_D3D11(param, vector);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetShaderParameter_Vulkan(param, Variant(vector));
#endif
}

void Graphics::SetShaderParameter(StringHash param, const Matrix4& matrix)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetShaderParameter_OGL(param, matrix);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetShaderParameter_D3D11(param, matrix);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetShaderParameter_Vulkan(param, Variant(matrix));
#endif
}

void Graphics::SetShaderParameter(StringHash param, const Vector4& vector)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetShaderParameter_OGL(param, vector);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetShaderParameter_D3D11(param, vector);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetShaderParameter_Vulkan(param, Variant(vector));
#endif
}

void Graphics::SetShaderParameter(StringHash param, const Matrix3x4& matrix)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetShaderParameter_OGL(param, matrix);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetShaderParameter_D3D11(param, matrix);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetShaderParameter_Vulkan(param, Variant(matrix));
#endif
}

bool Graphics::NeedParameterUpdate(ShaderParameterGroup group, const void* source)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return NeedParameterUpdate_OGL(group, source);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return NeedParameterUpdate_D3D11(group, source);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return true;  // Always update parameters for Vulkan (no caching yet)
#endif

    return {}; // Prevent warning
}

bool Graphics::HasShaderParameter(StringHash param)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return HasShaderParameter_OGL(param);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return HasShaderParameter_D3D11(param);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return HasShaderParameter_Vulkan(param);
#endif

    return {}; // Prevent warning
}

bool Graphics::HasTextureUnit(TextureUnit unit)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return HasTextureUnit_OGL(unit);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return HasTextureUnit_D3D11(unit);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return HasTextureUnit_Vulkan(unit);
#endif

    return {}; // Prevent warning
}

void Graphics::ClearParameterSource(ShaderParameterGroup group)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return ClearParameterSource_OGL(group);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return ClearParameterSource_D3D11(group);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return ClearParameterSource_Vulkan(group);
#endif
}

void Graphics::ClearParameterSources()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return ClearParameterSources_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return ClearParameterSources_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return ClearParameterSources_Vulkan();
#endif
}

void Graphics::ClearTransformSources()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return ClearTransformSources_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return ClearTransformSources_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return ClearTransformSources_Vulkan();
#endif
}

void Graphics::SetTexture(unsigned index, Texture* texture)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetTexture_OGL(index, texture);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetTexture_D3D11(index, texture);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetTexture_Vulkan(index, texture);
#endif
}

void Graphics::SetDefaultTextureFilterMode(TextureFilterMode mode)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetDefaultTextureFilterMode_OGL(mode);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetDefaultTextureFilterMode_D3D11(mode);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetDefaultTextureFilterMode_Vulkan(mode);
#endif
}

void Graphics::SetDefaultTextureAnisotropy(unsigned level)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetDefaultTextureAnisotropy_OGL(level);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetDefaultTextureAnisotropy_D3D11(level);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetDefaultTextureAnisotropy_Vulkan(level);
#endif
}

void Graphics::ResetRenderTargets()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return ResetRenderTargets_Vulkan();
#endif

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return ResetRenderTargets_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return ResetRenderTargets_D3D11();
#endif
}

void Graphics::ResetRenderTarget(unsigned index)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return ResetRenderTarget_OGL(index);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return ResetRenderTarget_D3D11(index);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return ResetRenderTarget_Vulkan(index);
#endif
}

void Graphics::ResetDepthStencil()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return ResetDepthStencil_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return ResetDepthStencil_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return ResetDepthStencil_Vulkan();
#endif
}

void Graphics::SetRenderTarget(unsigned index, RenderSurface* renderTarget)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetRenderTarget_OGL(index, renderTarget);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetRenderTarget_D3D11(index, renderTarget);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetRenderTarget_Vulkan(index, renderTarget);
#endif
}

void Graphics::SetRenderTarget(unsigned index, Texture2D* texture)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetRenderTarget_OGL(index, texture);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetRenderTarget_D3D11(index, texture);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetRenderTarget_Vulkan(index, texture);
#endif
}

void Graphics::SetDepthStencil(RenderSurface* depthStencil)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetDepthStencil_OGL(depthStencil);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetDepthStencil_D3D11(depthStencil);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetDepthStencil_Vulkan(depthStencil);
#endif
}

void Graphics::SetDepthStencil(Texture2D* texture)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetDepthStencil_OGL(texture);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetDepthStencil_D3D11(texture);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetDepthStencil_Vulkan(texture);
#endif
}

void Graphics::SetViewport(const IntRect& rect)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetViewport_OGL(rect);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetViewport_D3D11(rect);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetViewport_Vulkan(rect.left_, rect.top_, rect.Width(), rect.Height());
#endif
}

void Graphics::SetBlendMode(BlendMode mode, bool alphaToCoverage)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetBlendMode_OGL(mode, alphaToCoverage);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetBlendMode_D3D11(mode, alphaToCoverage);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetBlendMode_Vulkan(mode, alphaToCoverage);
#endif
}

void Graphics::SetColorWrite(bool enable)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetColorWrite_OGL(enable);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetColorWrite_D3D11(enable);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetColorWrite_Vulkan(enable);
#endif
}

void Graphics::SetCullMode(CullMode mode)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetCullMode_OGL(mode);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetCullMode_D3D11(mode);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetCullMode_Vulkan(mode);
#endif
}

void Graphics::SetDepthBias(float constantBias, float slopeScaledBias)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetDepthBias_OGL(constantBias, slopeScaledBias);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetDepthBias_D3D11(constantBias, slopeScaledBias);
#endif
#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetDepthBias_Vulkan(constantBias, slopeScaledBias);
#endif
}

void Graphics::SetDepthTest(CompareMode mode)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetDepthTest_OGL(mode);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetDepthTest_D3D11(mode);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetDepthTest_Vulkan(mode);
#endif
}

void Graphics::SetDepthWrite(bool enable)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetDepthWrite_OGL(enable);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetDepthWrite_D3D11(enable);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetDepthWrite_Vulkan(enable);
#endif
}

void Graphics::SetFillMode(FillMode mode)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetFillMode_OGL(mode);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetFillMode_D3D11(mode);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetFillMode_Vulkan(mode);
#endif
}

void Graphics::SetLineAntiAlias(bool enable)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetLineAntiAlias_OGL(enable);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetLineAntiAlias_D3D11(enable);
#endif
#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetLineAntiAlias_Vulkan(enable);
#endif
}

void Graphics::SetScissorTest(bool enable, const Rect& rect, bool borderInclusive)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetScissorTest_OGL(enable, rect, borderInclusive);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetScissorTest_D3D11(enable, rect, borderInclusive);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
    {
        scissorTest_ = enable;
        if (enable)
        {
            scissorRect_ = IntRect((int)rect.min_.x_, (int)rect.min_.y_, (int)rect.max_.x_, (int)rect.max_.y_);
            return SetScissor_Vulkan(scissorRect_.left_, scissorRect_.top_, scissorRect_.Width(), scissorRect_.Height());
        }
        else
        {
            scissorRect_ = IntRect::ZERO;
            return SetScissor_Vulkan(0, 0, width_, height_);
        }
    }
#endif
}

void Graphics::SetScissorTest(bool enable, const IntRect& rect)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetScissorTest_OGL(enable, rect);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetScissorTest_D3D11(enable, rect);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
    {
        scissorTest_ = enable;
        if (enable)
        {
            scissorRect_ = rect;
            return SetScissor_Vulkan(rect.left_, rect.top_, rect.Width(), rect.Height());
        }
        else
        {
            scissorRect_ = IntRect::ZERO;
            return SetScissor_Vulkan(0, 0, width_, height_);
        }
    }
#endif
}

void Graphics::SetClipPlane(bool enable, const Plane& clipPlane, const Matrix3x4& view, const Matrix4& projection)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetClipPlane_OGL(enable, clipPlane, view, projection);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetClipPlane_D3D11(enable, clipPlane, view, projection);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetClipPlane_Vulkan(enable, clipPlane, view, projection);
#endif
}

void Graphics::SetStencilTest(bool enable, CompareMode mode, StencilOp pass, StencilOp fail, StencilOp zFail, u32 stencilRef,
    u32 compareMask, u32 writeMask)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return SetStencilTest_OGL(enable, mode, pass, fail, zFail, stencilRef, compareMask, writeMask);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return SetStencilTest_D3D11(enable, mode, pass, fail, zFail, stencilRef, compareMask, writeMask);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return SetStencilTest_Vulkan(enable, mode, pass, fail, zFail, stencilRef, compareMask, writeMask);
#endif
}

bool Graphics::IsInitialized() const
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return IsInitialized_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return IsInitialized_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return IsInitialized_Vulkan();
#endif

    return {}; // Prevent warning
}

bool Graphics::GetDither() const
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetDither_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetDither_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetDither_Vulkan();
#endif

    return {}; // Prevent warning
}

bool Graphics::IsDeviceLost() const
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return IsDeviceLost_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return IsDeviceLost_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return IsDeviceLost_Vulkan();
#endif

    return {}; // Prevent warning
}

Vector<int> Graphics::GetMultiSampleLevels() const
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetMultiSampleLevels_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetMultiSampleLevels_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetMultiSampleLevels_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetFormat(CompressedFormat format) const
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetFormat_OGL(format);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetFormat_D3D11(format);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetFormat_Vulkan(format);
#endif

    return {}; // Prevent warning
}

ShaderVariation* Graphics::GetShader(ShaderType type, const String& name, const String& defines) const
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetShader_OGL(type, name, defines);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetShader_D3D11(type, name, defines);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetShader_Vulkan(type, name, defines);
#endif

    return {}; // Prevent warning
}

ShaderVariation* Graphics::GetShader(ShaderType type, const char* name, const char* defines) const
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetShader_OGL(type, name, defines);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetShader_D3D11(type, name, defines);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetShader_Vulkan(type, name, defines);
#endif

    return {}; // Prevent warning
}

VertexBuffer* Graphics::GetVertexBuffer(unsigned index) const
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetVertexBuffer_OGL(index);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetVertexBuffer_D3D11(index);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetVertexBuffer_Vulkan(index);
#endif

    return {}; // Prevent warning
}

TextureUnit Graphics::GetTextureUnit(const String& name)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetTextureUnit_OGL(name);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetTextureUnit_D3D11(name);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetTextureUnit_Vulkan(name);
#endif

    return {}; // Prevent warning
}

const String& Graphics::GetTextureUnitName(TextureUnit unit)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetTextureUnitName_OGL(unit);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetTextureUnitName_D3D11(unit);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetTextureUnitName_Vulkan(unit);
#endif

    return String::EMPTY; // Prevent warning
}

Texture* Graphics::GetTexture(unsigned index) const
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetTexture_OGL(index);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetTexture_D3D11(index);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetTexture_Vulkan(index);
#endif

    return {}; // Prevent warning
}

RenderSurface* Graphics::GetRenderTarget(unsigned index) const
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetRenderTarget_OGL(index);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetRenderTarget_D3D11(index);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetRenderTarget_Vulkan(index);
#endif

    return {}; // Prevent warning
}

IntVector2 Graphics::GetRenderTargetDimensions() const
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetRenderTargetDimensions_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetRenderTargetDimensions_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetRenderTargetDimensions_Vulkan();
#endif

    return {}; // Prevent warning
}

void Graphics::OnWindowResized()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return OnWindowResized_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return OnWindowResized_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return OnWindowResized_Vulkan();
#endif
}

void Graphics::OnWindowMoved()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return OnWindowMoved_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return OnWindowMoved_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return OnWindowMoved_Vulkan();
#endif
}

ConstantBuffer* Graphics::GetOrCreateConstantBuffer(ShaderType type, unsigned index, unsigned size)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetOrCreateConstantBuffer_OGL(type, index, size);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetOrCreateConstantBuffer_D3D11(type, index, size);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetOrCreateConstantBuffer_Vulkan(type, index, size);
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetMaxBones()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetMaxBones_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetMaxBones_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetMaxBones_Vulkan();
#endif

    return {}; // Prevent warning
}

bool Graphics::GetGL3Support()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetGL3Support_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetGL3Support_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetGL3Support_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetAlphaFormat()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetAlphaFormat_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetAlphaFormat_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetAlphaFormat_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetLuminanceFormat()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetLuminanceFormat_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetLuminanceFormat_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetLuminanceFormat_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetLuminanceAlphaFormat()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetLuminanceAlphaFormat_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetLuminanceAlphaFormat_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetLuminanceAlphaFormat_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetRGBFormat()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetRGBFormat_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetRGBFormat_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetRGBFormat_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetRGBAFormat()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetRGBAFormat_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetRGBAFormat_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetRGBAFormat_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetRGBA16Format()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetRGBA16Format_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetRGBA16Format_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetRGBA16Format_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetRGBAFloat16Format()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetRGBAFloat16Format_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetRGBAFloat16Format_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetRGBAFloat16Format_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetRGBAFloat32Format()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetRGBAFloat32Format_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetRGBAFloat32Format_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetRGBAFloat32Format_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetRG16Format()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetRG16Format_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetRG16Format_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetRG16Format_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetRGFloat16Format()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetRGFloat16Format_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetRGFloat16Format_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetRGFloat16Format_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetRGFloat32Format()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetRGFloat32Format_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetRGFloat32Format_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetRGFloat32Format_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetFloat16Format()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetFloat16Format_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetFloat16Format_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetFloat16Format_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetFloat32Format()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetFloat32Format_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetFloat32Format_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetFloat32Format_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetLinearDepthFormat()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetLinearDepthFormat_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetLinearDepthFormat_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetLinearDepthFormat_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetDepthStencilFormat()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetDepthStencilFormat_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetDepthStencilFormat_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetDepthStencilFormat_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetReadableDepthFormat()
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetReadableDepthFormat_OGL();
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetReadableDepthFormat_D3D11();
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetReadableDepthFormat_Vulkan();
#endif

    return {}; // Prevent warning
}

unsigned Graphics::GetFormat(const String& formatName)
{
    GAPI gapi = Graphics::GetGAPI();

#ifdef URHO3D_OPENGL
    if (gapi == GAPI_OPENGL)
        return GetFormat_OGL(formatName);
#endif

#ifdef URHO3D_D3D11
    if (gapi == GAPI_D3D11)
        return GetFormat_D3D11(formatName);
#endif

#ifdef URHO3D_VULKAN
    if (gapi == GAPI_VULKAN)
        return GetFormat_Vulkan(formatName);
#endif

    return {}; // Prevent warning
}

void RegisterGraphicsLibrary(Context* context)
{
    Animation::RegisterObject(context);
    Material::RegisterObject(context);
    Model::RegisterObject(context);
    Shader::RegisterObject(context);
    Technique::RegisterObject(context);
    Texture2D::RegisterObject(context);
    Texture2DArray::RegisterObject(context);
    Texture3D::RegisterObject(context);
    TextureCube::RegisterObject(context);
    Camera::RegisterObject(context);
    Drawable::RegisterObject(context);
    Light::RegisterObject(context);
    StaticModel::RegisterObject(context);
    StaticModelGroup::RegisterObject(context);
    Skybox::RegisterObject(context);
    AnimatedModel::RegisterObject(context);
    AnimationController::RegisterObject(context);
    BillboardSet::RegisterObject(context);
    ParticleEffect::RegisterObject(context);
    ParticleEmitter::RegisterObject(context);
    RibbonTrail::RegisterObject(context);
    CustomGeometry::RegisterObject(context);
    DecalSet::RegisterObject(context);
    Terrain::RegisterObject(context);
    TerrainPatch::RegisterObject(context);
    DebugRenderer::RegisterObject(context);
    Octree::RegisterObject(context);
    Zone::RegisterObject(context);
}

}
