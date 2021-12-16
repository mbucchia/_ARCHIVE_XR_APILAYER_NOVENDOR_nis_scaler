// Copyright (c) 2021, Matthieu Bucchianeri
//
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "pch.h"

#include <DeviceResources.h>
#include <BilinearUpscale.h>
#include <NVScaler.h>
#include <NVSharpen.h>

#define STRINGIFY(s) XSTRINGIFY(s)
#define XSTRINGIFY(s) #s

namespace {
    using Microsoft::WRL::ComPtr;

    const std::string LayerName = "XR_APILAYER_NOVENDOR_nis_scaler";
    const std::string VersionString = "Alpha4";

    // TODO: Optimize the VS to only draw 1 triangle and use clipping.
    const std::string colorConversionShadersSource = R"_(
Texture2D srcTex;
SamplerState srcSampler;

void vsMain(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD0)
{
    texcoord.x = (id == 2) ?  2.0 :  0.0;
    texcoord.y = (id == 1) ?  2.0 :  0.0;

    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 1.0, 1.0);
}

float4 psMain(in float4 position : SV_POSITION, in float2 texcoord : TEXCOORD0) : SV_TARGET {
	return srcTex.Sample(srcSampler, texcoord);
}
    )_";


    // The path where the DLL loads config files and stores logs.
    std::string dllHome;

    // The path to find the NIS shader source.
    std::string nisShaderHome;

    // The file logger.
    std::ofstream logStream;

    // Function pointers to chain calls with the next layers and/or the OpenXR runtime.
    PFN_xrGetInstanceProcAddr next_xrGetInstanceProcAddr = nullptr;
    PFN_xrEnumerateViewConfigurationViews next_xrEnumerateViewConfigurationViews = nullptr;
    PFN_xrEnumerateSwapchainFormats next_xrEnumerateSwapchainFormats = nullptr;
    PFN_xrCreateSession next_xrCreateSession = nullptr;
    PFN_xrDestroySession next_xrDestroySession = nullptr;
    PFN_xrCreateSwapchain next_xrCreateSwapchain = nullptr;
    PFN_xrDestroySwapchain next_xrDestroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages next_xrEnumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage next_xrAcquireSwapchainImage = nullptr;
    PFN_xrEndFrame next_xrEndFrame = nullptr;

    // Device state.
    uint32_t actualDisplayWidth;
    uint32_t actualDisplayHeight;
    ComPtr<ID3D11Device> d3d11Device = nullptr;
    DeviceResources deviceResources;

    // Scalers state and resources.
    bool isIntermediateFormatCompatible = false;
    bool needBindUnorderedAccessWorkaround = false;
    struct SwapchainImageResources
    {
        // Resources needed by the scaler.
        ComPtr<ID3D11UnorderedAccessView> upscaledTextureUav[2];
        ComPtr<ID3D11Texture2D> appTexture;
        ComPtr<ID3D11ShaderResourceView> appTextureSrv[2];

        // Resources needed for flat upscaling and color conversion.
        ComPtr<ID3D11RenderTargetView> runtimeTextureRtv[2];

        // The original texture returned by OpenXR.
        ID3D11Texture2D* runtimeTexture;
    };
    struct GpuTimer
    {
        ComPtr<ID3D11Query> timeStampDis;
        ComPtr<ID3D11Query> timeStampStart;
        ComPtr<ID3D11Query> timeStampEnd;
        bool valid;
    };
    struct ScalerResources
    {
        // The swapchain info as requested by the application.
        XrSwapchainCreateInfo swapchainInfo;

        // Scaler processors. Either NISScaler or NISSharpen will be used based on the requested scaling (not both).
        std::shared_ptr<BilinearUpscale> bilinearScaler;
        std::shared_ptr<NVScaler> NISScaler;
        std::shared_ptr<NVSharpen> NISSharpen;

        // Common resources for color conversion mode.
        ComPtr<ID3D11Texture2D> intermediateTexture;
        ComPtr<ID3D11ShaderResourceView> intermediateTextureSrv[2];

        // The resources for each swapchain image.
        std::vector<SwapchainImageResources> imageResources;

        // GPU timers.
        mutable GpuTimer scalerTimer;
        mutable GpuTimer colorConversionTimer;
    };
    std::map<XrSwapchain, ScalerResources> scalerResources;
    std::map<XrSwapchain, uint32_t> swapchainIndices;

    // Common resources for indirect color conversion mode.
    ComPtr<ID3D11VertexShader> colorConversionVertexShader;
    ComPtr<ID3D11PixelShader> colorConversionPixelShader;
    ComPtr<ID3D11SamplerState> colorConversionSampler;
    ComPtr<ID3D11RasterizerState> colorConversionRasterizer;
    ComPtr<ID3D11RasterizerState> colorConversionRasterizerMSAA;

    // Statistics.
    const uint64_t StatsPeriodMs = 60000;
    struct Statistics
    {
        uint64_t windowBeginning;
        uint64_t nextWindow;

        uint64_t totalScalerTime;
        uint64_t totalColorConversionTime;

        uint32_t numFrames;

        void Reset()
        {
            totalScalerTime = totalColorConversionTime = 0;
            numFrames = 0;
        }
    };
    Statistics stats;

    // Interactive state (for use with hotkeys).
    enum ScalingMode
    {
        Flat = 0,
        Bilinear,
        NIS,
        EnumMax
    } scalingMode;
    float newSharpness;

    void Log(const char* fmt, ...);

    struct {
        bool loaded;
        float scaleFactor;
        float sharpness;
        bool disableBilinearScaler;
        DXGI_FORMAT intermediateFormat;
        bool fastContextSwitch;
        bool enableStats;

        void Dump()
        {
            if (loaded)
            {
                const bool isDebugBuild =
#ifdef _DEBUG
                    true;
#else
                    false;
#endif
                if (isDebugBuild || config.enableStats)
                {
                    Log("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                    Log("!!! USING DEBUG SETTINGS - PERFORMANCE WILL BE DECREASED             !!!\n");
                    Log("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                }

                Log("Using intermediate format: %d\n", intermediateFormat);
                if (config.fastContextSwitch)
                {
                    Log("Using fast context switch\n");
                }
                if (scaleFactor < 1.f)
                {
                    Log("Use scaling factor: %.3f\n", scaleFactor);
                }
                else
                {
                    Log("No scaling, sharpening only\n");
                }
                Log("Sharpness set to %.3f\n", sharpness);
            }
        }

        void Reset()
        {
            loaded = false;
            scaleFactor = 0.7f;
            sharpness = 0.5f;
            disableBilinearScaler = true;
            intermediateFormat = DXGI_FORMAT_R16G16B16A16_UNORM;
            fastContextSwitch = true;
            enableStats = false;
        }
    } config;

    // Utility logging function.
    void InternalLog(
        const char* fmt,
        va_list va)
    {
        char buf[1024];
        _vsnprintf_s(buf, sizeof(buf), fmt, va);
        OutputDebugStringA(buf);
        if (logStream.is_open())
        {
            logStream << buf;
            logStream.flush();
        }
    }

    // General logging function.
    void Log(
        const char* fmt,
        ...)
    {
        va_list va;
        va_start(va, fmt);
        InternalLog(fmt, va);
        va_end(va);
    }

    // Debug logging function. Can make things very slow (only enabled on Debug builds).
    void DebugLog(
        const char* fmt,
        ...)
    {
#ifdef _DEBUG
        va_list va;
        va_start(va, fmt);
        InternalLog(fmt, va);
        va_end(va);
#endif
    }

    // Load configuration for our layer.
    bool LoadConfiguration(
        const std::string configName)
    {
        if (configName.empty())
        {
            return false;
        }

        std::ifstream configFile(std::filesystem::path(dllHome) / std::filesystem::path(configName + ".cfg"));
        if (configFile.is_open())
        {
            Log("Loading config for \"%s\"\n", configName.c_str());

            unsigned int lineNumber = 0;
            std::string line;
            while (std::getline(configFile, line))
            {
                lineNumber++;
                try
                {
                    // TODO: Usability: handle comments, white spaces, blank lines...
                    const auto offset = line.find('=');
                    if (offset != std::string::npos)
                    {
                        const std::string name = line.substr(0, offset);
                        const std::string value = line.substr(offset + 1);

                        if (name == "scaling")
                        {
                            config.scaleFactor = std::clamp(std::stof(value), 0.f, 1.f);
                        }
                        else if (name == "sharpness")
                        {
                            config.sharpness = std::clamp(std::stof(value), 0.f, 1.f);
                        }
                        else if (name == "disable_bilinear_scaler")
                        {
                            config.disableBilinearScaler = value == "1" || value == "true";
                        }
                        else if (name == "intermediate_format")
                        {
                            config.intermediateFormat = (DXGI_FORMAT)std::stoi(value);
                        }
                        else if (name == "fast_context_switch")
                        {
                            config.fastContextSwitch = value == "1" || value == "true";
                        }
                        else if (name == "enable_stats")
                        {
                            config.enableStats = value == "1" || value == "true";
                        }
                    }
                }
                catch (...)
                {
                    Log("Error parsing L%u\n", lineNumber);
                }
            }
            configFile.close();

            config.loaded = true;

            return true;
        }

        Log("Could not load config for \"%s\"\n", configName.c_str());

        return false;
    }

    // Implement keyboard shortcut detection and handling.
    void HandleHotkeys()
    {
        static bool wasF1Pressed = false;
        const bool isF1Pressed = GetAsyncKeyState(VK_CONTROL) && (GetAsyncKeyState(VK_LEFT) || GetAsyncKeyState(VK_F1));
        if (!wasF1Pressed && isF1Pressed)
        {
            do
            {
                scalingMode = (ScalingMode)((scalingMode + 1) % ScalingMode::EnumMax);
            } while (config.disableBilinearScaler && scalingMode == ScalingMode::Bilinear);
        }
        wasF1Pressed = isF1Pressed;

        static bool wasF2Pressed = false;
        const bool isF2Pressed = GetAsyncKeyState(VK_CONTROL) && (GetAsyncKeyState(VK_DOWN) || GetAsyncKeyState(VK_F2));
        if (!wasF2Pressed && isF2Pressed)
        {
            newSharpness = max(0.f, newSharpness - 0.05f);
            Log("sharpness=%.3f\n", newSharpness);
        }
        wasF2Pressed = isF2Pressed;

        static bool wasF3Pressed = false;
        const bool isF3Pressed = GetAsyncKeyState(VK_CONTROL) && (GetAsyncKeyState(VK_UP) || GetAsyncKeyState(VK_F3));
        if (!wasF3Pressed && isF3Pressed)
        {
            newSharpness = min(1.f, newSharpness + 0.05f);
            Log("sharpness=%.3f\n", newSharpness);
        }
        wasF3Pressed = isF3Pressed;
    }

    // Returns whether a texture format is directly supported by the NIS shader.
    bool IsSupportedColorFormat(
        const DXGI_FORMAT format)
    {
        // Want to use ID3D11Device::CheckFormatSupport() but there might not be a device. Use a list instead.
        return format == DXGI_FORMAT_R8G8B8A8_UNORM
            || format == DXGI_FORMAT_R8G8B8A8_UINT
            || format == DXGI_FORMAT_R8G8B8A8_SNORM
            || format == DXGI_FORMAT_R8G8B8A8_SINT;
    }

    // Returns whether a texture format is indirectly supported by the NIS shader.
    // Indirectly means that we implement a conversion path from this format to a supported format.
    bool IsIndirectlySupportedColorFormat(
        const DXGI_FORMAT format)
    {
        return format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }

    // Returns whether a depth format is supported by our layer.
    bool IsSupportedDepthFormat(
        const DXGI_FORMAT format)
    {
        // TODO: Support depth.
        return false;
    }

    // Returns whether this swapchain is currently set up for scaling.
    bool IsSwapchainHandled(
        const XrSwapchain swapchain)
    {
        return scalerResources.find(swapchain) != scalerResources.cend();
    }

    void InitTimer(GpuTimer& timer)
    {
        D3D11_QUERY_DESC queryDesc;
        ZeroMemory(&queryDesc, sizeof(D3D11_QUERY_DESC));
        queryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        DX::ThrowIfFailed(deviceResources.device()->CreateQuery(&queryDesc, timer.timeStampDis.GetAddressOf()));
        queryDesc.Query = D3D11_QUERY_TIMESTAMP;
        DX::ThrowIfFailed(deviceResources.device()->CreateQuery(&queryDesc, timer.timeStampStart.GetAddressOf()));
        DX::ThrowIfFailed(deviceResources.device()->CreateQuery(&queryDesc, timer.timeStampEnd.GetAddressOf()));
        timer.valid = false;
    }

    void StartTimer(GpuTimer& timer)
    {
        if (timer.timeStampDis)
        {
            deviceResources.context()->Begin(timer.timeStampDis.Get());
            deviceResources.context()->End(timer.timeStampStart.Get());
        }
    }

    void StopTimer(GpuTimer& timer)
    {
        if (timer.timeStampDis)
        {
            deviceResources.context()->End(timer.timeStampEnd.Get());
            deviceResources.context()->End(timer.timeStampDis.Get());
            timer.valid = true;
        }
    }

    uint64_t QueryTimer(GpuTimer& timer)
    {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disData;
        UINT64 startime;
        UINT64 endtime;

        if (timer.valid && timer.timeStampDis &&
            deviceResources.context()->GetData(timer.timeStampDis.Get(), &disData, sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT), 0) == S_OK &&
            deviceResources.context()->GetData(timer.timeStampStart.Get(), &startime, sizeof(UINT64), 0) == S_OK &&
            deviceResources.context()->GetData(timer.timeStampEnd.Get(), &endtime, sizeof(UINT64), 0) == S_OK &&
            !disData.Disjoint)
        {
            timer.valid = false;
            return (uint64_t)((endtime - startime) / double(disData.Frequency) * 1e6);
        }
        return 0;
    }

    // We override this OpenXR API in order to return the desired rendering resolution to the application.
    // This resolution is pre-upscaling.
    XrResult NISScaler_xrEnumerateViewConfigurationViews(
        const XrInstance instance,
        const XrSystemId systemId,
        const XrViewConfigurationType viewConfigurationType,
        const uint32_t viewCapacityInput,
        uint32_t* const viewCountOutput,
        XrViewConfigurationView* const views)
    {
        DebugLog("--> NISScaler_xrEnumerateViewConfigurationViews\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrEnumerateViewConfigurationViews(instance, systemId, viewConfigurationType, viewCapacityInput, viewCountOutput, views);
        if (result == XR_SUCCESS && viewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO && viewCapacityInput > 0)
        {
            actualDisplayWidth = views[0].recommendedImageRectWidth;
            actualDisplayHeight = views[0].recommendedImageRectHeight;

            if (config.scaleFactor < 1.f)
            {
                // Store the actual image size and override the recommended image size to account for scaling.
                for (uint32_t i = 0; i < *viewCountOutput; i++)
                {
                    views[i].recommendedImageRectWidth = (uint32_t)(views[i].recommendedImageRectWidth * config.scaleFactor);
                    views[i].recommendedImageRectHeight = (uint32_t)(views[i].recommendedImageRectHeight * config.scaleFactor);

                    if (i == 0)
                    {
                        Log("Scaled resolution is: %ux%u (%u%% of %ux%u)\n",
                            views[i].recommendedImageRectWidth, views[i].recommendedImageRectHeight,
                            (unsigned int)((config.scaleFactor + 0.001f) * 100), actualDisplayWidth, actualDisplayHeight);
                    }
                }
            }
            else
            {
                Log("Using OpenXR resolution (no scaling): %ux%u\n", actualDisplayWidth, actualDisplayHeight);
            }
        }

        DebugLog("<-- NISScaler_xrEnumerateViewConfigurationViews %d\n", result);

        return result;
    }

    // We override this OpenXR API in order to intercept the D3D device used by the application.
    // We also initialize some common resources.
    XrResult NISScaler_xrCreateSession(
        const XrInstance instance,
        const XrSessionCreateInfo* const createInfo,
        XrSession* const session)
    {
        DebugLog("--> NISScaler_xrCreateSession\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrCreateSession(instance, createInfo, session);
        if (result == XR_SUCCESS)
        {
            try
            {
                const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(createInfo->next);
                while (entry)
                {
                    if (entry->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)
                    {
                        // Keep track of the D3D device.
                        const XrGraphicsBindingD3D11KHR* d3dBindings = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(entry);
                        d3d11Device = d3dBindings->device;

                        // HACK: See our DeviceResources implementation. We use the existing interface using an HWND pointer as an opaque pointer.
                        deviceResources.create(reinterpret_cast<HWND>(d3d11Device.Get()));

                        // Check whether we need color conversion.
                        uint32_t formatsCount = 0;
                        next_xrEnumerateSwapchainFormats(*session, 0, &formatsCount, nullptr);
                        std::vector<int64_t> formats(formatsCount, {});
                        if (next_xrEnumerateSwapchainFormats(*session, formatsCount, &formatsCount, formats.data()) == XR_SUCCESS)
                        {
                            for (auto format : formats)
                            {
                                if (format == config.intermediateFormat)
                                {
                                    isIntermediateFormatCompatible = true;
                                    break;
                                }
                            }
                        }

                        if (needBindUnorderedAccessWorkaround)
                        {
                            if (isIntermediateFormatCompatible)
                            {
                                Log("Using BindUnorderedAccess workaround.\n");
                            }
                            isIntermediateFormatCompatible = false;
                        }

                        // Initialize resources for color conversion (in case we actually need it). We also use this for the unfiltered (flat) scaling mode.
                        ComPtr<ID3DBlob> errors;

                        ComPtr<ID3DBlob> vsBytes;
                        HRESULT hr = D3DCompile(colorConversionShadersSource.c_str(), colorConversionShadersSource.length(), nullptr, nullptr, nullptr, "vsMain", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, vsBytes.GetAddressOf(), errors.GetAddressOf());
                        if (FAILED(hr)) {
                            Log("VS compile failed: %*s\n", errors->GetBufferSize(), errors->GetBufferPointer());
                            DX::ThrowIfFailed(hr);
                        }
                        DX::ThrowIfFailed(d3d11Device->CreateVertexShader(vsBytes->GetBufferPointer(), vsBytes->GetBufferSize(), nullptr, colorConversionVertexShader.GetAddressOf()));

                        ComPtr<ID3DBlob> psBytes;
                        hr = D3DCompile(colorConversionShadersSource.c_str(), colorConversionShadersSource.length(), nullptr, nullptr, nullptr, "psMain", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, psBytes.GetAddressOf(), errors.ReleaseAndGetAddressOf());
                        if (FAILED(hr)) {
                            Log("PS compile failed: %*s\n", errors->GetBufferSize(), errors->GetBufferPointer());
                            DX::ThrowIfFailed(hr);
                        }
                        DX::ThrowIfFailed(d3d11Device->CreatePixelShader(psBytes->GetBufferPointer(), psBytes->GetBufferSize(), nullptr, colorConversionPixelShader.GetAddressOf()));

                        D3D11_SAMPLER_DESC sampDesc;
                        ZeroMemory(&sampDesc, sizeof(D3D11_SAMPLER_DESC));
                        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
                        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
                        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
                        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                        sampDesc.MaxAnisotropy = 1;
                        sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
                        DX::ThrowIfFailed(d3d11Device->CreateSamplerState(&sampDesc, colorConversionSampler.GetAddressOf()));

                        D3D11_RASTERIZER_DESC rsDesc;
                        ZeroMemory(&rsDesc, sizeof(D3D11_RASTERIZER_DESC));
                        rsDesc.FillMode = D3D11_FILL_SOLID;
                        rsDesc.CullMode = D3D11_CULL_NONE;
                        rsDesc.FrontCounterClockwise = TRUE;
                        DX::ThrowIfFailed(d3d11Device->CreateRasterizerState(&rsDesc, colorConversionRasterizer.GetAddressOf()));
                        rsDesc.MultisampleEnable = TRUE;
                        DX::ThrowIfFailed(d3d11Device->CreateRasterizerState(&rsDesc, colorConversionRasterizerMSAA.GetAddressOf()));
                    }
                    else if (entry->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR)
                    {
                        // TODO: Support D3D12.
                        Log("D3D12 is not supported.\n");
                    }

                    entry = entry->next;
                }

                if (!d3d11Device)
                {
                    Log("Application does not use D3D11.\n");
                }
            }
            catch (std::runtime_error exc)
            {
                Log("Error: %s\n", exc.what());
            }

            scalingMode = ScalingMode::NIS;
            newSharpness = config.sharpness;

            // Make the first update quicker.
            stats.windowBeginning = GetTickCount64();
            stats.nextWindow = stats.windowBeginning + StatsPeriodMs / 10;
            stats.Reset();
        }

        DebugLog("<-- NISScaler_xrCreateSession %d\n", result);

        return result;
    }

    // We override this OpenXR API in order to do cleanup.
    XrResult NISScaler_xrDestroySession(
        const XrSession session)
    {
        DebugLog("--> NISScaler_xrDestroySession\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrDestroySession(session);
        if (result == XR_SUCCESS)
        {
            // Cleanup all the scaler's resources.
            scalerResources.clear();
            swapchainIndices.clear();
            colorConversionRasterizer = nullptr;
            colorConversionRasterizerMSAA = nullptr;
            colorConversionSampler = nullptr;
            colorConversionPixelShader = nullptr;
            colorConversionVertexShader = nullptr;
            deviceResources.create(nullptr);
            d3d11Device = nullptr;
        }

        DebugLog("<-- NISScaler_xrDestroySession %d\n", result);

        return result;
    }

    // We override this OpenXR API in order to setup our NIS scaler for the appropriate resolutions.
    // We also request that the textures provided by the OpenXR runtime can be used with the NIS scaler.
    XrResult NISScaler_xrCreateSwapchain(
        const XrSession session,
        const XrSwapchainCreateInfo* const createInfo,
        XrSwapchain* const swapchain)
    {
        DebugLog("--> NISScaler_xrCreateSwapchain\n");

        // This function is the most likely to fail due to OpenXR runtime variations, GPU variations etc...
        // Add extra logging in here.

        XrSwapchainCreateInfo chainCreateInfo = *createInfo;

        const bool isIndirectlySupportedColorFormat = IsIndirectlySupportedColorFormat((DXGI_FORMAT)createInfo->format);
        const bool isSupportedColorFormat = IsSupportedColorFormat((DXGI_FORMAT)createInfo->format) || isIndirectlySupportedColorFormat;
        const bool isSupportedDepthFormat = IsSupportedDepthFormat((DXGI_FORMAT)createInfo->format);
        const bool isHandled = d3d11Device && createInfo->arraySize <= 2 && createInfo->faceCount == 1 && (isSupportedColorFormat || isSupportedDepthFormat);

        if (isHandled)
        {
            // Request the full device resolution. The app will not see this texture, only the runtime.
            chainCreateInfo.width = actualDisplayWidth;
            chainCreateInfo.height = actualDisplayHeight;

            // Make sure this format is supported for a UAV.
            if (isIndirectlySupportedColorFormat)
            {
                if (isIntermediateFormatCompatible)
                {
                    Log("Using indirect texture format mapping\n");

                    // Fallback to a format supported by the both the scaler and the runtime, and avoid the explicit color conversion pass.
                    chainCreateInfo.format = (uint64_t)config.intermediateFormat;

                    // Add the flag to allow the textures to be the output of the scaler.
                    chainCreateInfo.usageFlags |= XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT;
                }
                else
                {
                    Log("Using indirect texture format with color conversion\n");

                    // Otherwise we keep the requested format, and we will have to do an extra pass for color mapping.
                }
            }
            else
            {
                // Add the flag to allow the textures to be the output of the scaler.
                chainCreateInfo.usageFlags |= XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT;
            }
        }

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrCreateSwapchain(session, &chainCreateInfo, swapchain);
        if (result == XR_SUCCESS)
        {
            if (isHandled)
            {
                try
                {
                    ScalerResources resources;

                    // Create the scalers.
                    if (!config.disableBilinearScaler)
                    {
                        resources.bilinearScaler = std::make_shared<BilinearUpscale>(deviceResources);
                        resources.bilinearScaler->update(createInfo->width, createInfo->height, actualDisplayWidth, actualDisplayHeight);
                    }
                    if (config.scaleFactor < 1.f)
                    {
                        resources.NISScaler = std::make_shared<NVScaler>(deviceResources, nisShaderHome);
                        resources.NISScaler->update(config.sharpness, createInfo->width, createInfo->height, actualDisplayWidth, actualDisplayHeight);
                    }
                    else
                    {
                        resources.NISSharpen = std::make_shared<NVSharpen>(deviceResources, nisShaderHome);
                        resources.NISSharpen->update(config.sharpness, createInfo->width, createInfo->height);
                    }

                    // We keep track of the (real) swapchain info for when we intercept the textures in xrEnumerateSwapchainImages().
                    resources.swapchainInfo = *createInfo;

                    // We will keep track of the textures we distribute to the app.
                    scalerResources.insert_or_assign(*swapchain, std::move(resources));
                }
                catch (std::runtime_error exc)
                {
                    Log("Error: %s\n", exc.what());
                }
            }
            else
            {
                Log("Swapchain with format %d, array size %u and face count %u is not supported.\n", createInfo->format, createInfo->arraySize, createInfo->faceCount);
            }
        }
        else
        {
            Log("xrCreateSwapchain failed with %d\n", result);
        }

        DebugLog("<-- NISScaler_xrCreateSwapchain %d\n", result);

        return result;
    }

    // We override this OpenXR API in order to do cleanup.
    XrResult NISScaler_xrDestroySwapchain(
        const XrSwapchain swapchain)
    {
        DebugLog("--> NISScaler_xrDestroySwapchain\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrDestroySwapchain(swapchain);
        if (result == XR_SUCCESS && IsSwapchainHandled(swapchain))
        {
            // Cleanup the resources.
            scalerResources.erase(swapchain);
            swapchainIndices.erase(swapchain);
        }

        DebugLog("<-- NISScaler_xrDestroySwapchain %d\n", result);

        return result;
    }

    // We override this OpenXR API in order to intercept the textures that the app will render to.
    // We setup our resources in order to insert the NIS scaler between the application and the runtime.
    XrResult NISScaler_xrEnumerateSwapchainImages(
        const XrSwapchain swapchain,
        const uint32_t imageCapacityInput,
        uint32_t* const imageCountOutput,
        XrSwapchainImageBaseHeader* const images)
    {
        DebugLog("--> NISScaler_xrEnumerateSwapchainImages\n");

        // This function is the most likely to fail due to OpenXR runtime variations, GPU variations etc...
        // Add extra logging in here.

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrEnumerateSwapchainImages(swapchain, imageCapacityInput, imageCountOutput, images);
        if (result == XR_SUCCESS && IsSwapchainHandled(swapchain) && imageCapacityInput > 0)
        {
            try
            {
                XrSwapchainImageD3D11KHR* d3dImages = reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
                ScalerResources& commonResources = scalerResources[swapchain];

                // Detect some properties for our resources.
                const XrSwapchainCreateInfo& imageInfo = commonResources.swapchainInfo;
                const bool indirectMode = IsIndirectlySupportedColorFormat((DXGI_FORMAT)imageInfo.format);
                const bool needColorConversion = !isIntermediateFormatCompatible;
                for (uint32_t i = 0; i < *imageCountOutput; i++)
                {
                    SwapchainImageResources resources;

                    // We don't strictly need to keep the runtime texture, but we do it for possible future uses.
                    resources.runtimeTexture = d3dImages[i].texture;

                    // Create the texture that the app will render to.
                    D3D11_TEXTURE2D_DESC textureDesc;
                    ZeroMemory(&textureDesc, sizeof(D3D11_TEXTURE2D_DESC));
                    textureDesc.Width = imageInfo.width;
                    textureDesc.Height = imageInfo.height;
                    textureDesc.MipLevels = imageInfo.mipCount;
                    textureDesc.ArraySize = imageInfo.arraySize;
                    textureDesc.Format = (DXGI_FORMAT)imageInfo.format;
                    textureDesc.SampleDesc.Count = imageInfo.sampleCount;
                    textureDesc.Usage = D3D11_USAGE_DEFAULT;
                    if (imageInfo.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT)
                    {
                        textureDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
                    }
                    if (imageInfo.usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
                    {
                        textureDesc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
                    }
                    if (imageInfo.usageFlags & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT)
                    {
                        textureDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
                    }
                    // This flag is needed for the scaler.
                    textureDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
                    DX::ThrowIfFailed(deviceResources.device()->CreateTexture2D(&textureDesc, nullptr, resources.appTexture.GetAddressOf()));

                    // Create an intermediate texture for color conversion. This texture is compatible with the scaler's output.
                    if (needColorConversion && i == 0)
                    {
                        textureDesc.Width = actualDisplayWidth;
                        textureDesc.Height = actualDisplayHeight;
                        textureDesc.Format = config.intermediateFormat;
                        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                        DX::ThrowIfFailed(deviceResources.device()->CreateTexture2D(&textureDesc, nullptr, commonResources.intermediateTexture.GetAddressOf()));
                    }

                    // Create the views needed by the scalers and color conversion.
                    for (uint32_t j = 0; j < imageInfo.arraySize; j++)
                    {
                        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
                        ZeroMemory(&srvDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
                        srvDesc.Format = (DXGI_FORMAT)imageInfo.format;
                        srvDesc.ViewDimension = imageInfo.arraySize == 1 ? D3D11_SRV_DIMENSION_TEXTURE2D : D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                        srvDesc.Texture2DArray.MostDetailedMip = 0;
                        srvDesc.Texture2DArray.MipLevels = imageInfo.mipCount;
                        srvDesc.Texture2DArray.ArraySize = 1;
                        srvDesc.Texture2DArray.FirstArraySlice = D3D11CalcSubresource(0, j, imageInfo.mipCount);
                        DX::ThrowIfFailed(deviceResources.device()->CreateShaderResourceView(resources.appTexture.Get(), &srvDesc, resources.appTextureSrv[j].GetAddressOf()));

                        if (needColorConversion && i == 0)
                        {
                            srvDesc.Format = config.intermediateFormat;
                            DX::ThrowIfFailed(deviceResources.device()->CreateShaderResourceView(commonResources.intermediateTexture.Get(), &srvDesc, commonResources.intermediateTextureSrv[j].GetAddressOf()));
                        }

                        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
                        ZeroMemory(&uavDesc, sizeof(D3D11_UNORDERED_ACCESS_VIEW_DESC));
                        uavDesc.Format = !indirectMode ? (DXGI_FORMAT)imageInfo.format : config.intermediateFormat;
                        uavDesc.ViewDimension = imageInfo.arraySize == 1 ? D3D11_UAV_DIMENSION_TEXTURE2D : D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
                        uavDesc.Texture2DArray.MipSlice = 0;
                        uavDesc.Texture2DArray.ArraySize = 1;
                        uavDesc.Texture2DArray.FirstArraySlice = D3D11CalcSubresource(0, j, imageInfo.mipCount);
                        ID3D11Resource* const targetTexture = needColorConversion ? commonResources.intermediateTexture.Get() : resources.runtimeTexture;
                        DX::ThrowIfFailed(deviceResources.device()->CreateUnorderedAccessView(targetTexture, &uavDesc, resources.upscaledTextureUav[j].GetAddressOf()));

                        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
                        ZeroMemory(&rtvDesc, sizeof(D3D11_RENDER_TARGET_VIEW_DESC));
                        rtvDesc.Format = !indirectMode || !isIntermediateFormatCompatible ? (DXGI_FORMAT)imageInfo.format : config.intermediateFormat;
                        rtvDesc.ViewDimension = imageInfo.arraySize == 1 ? D3D11_RTV_DIMENSION_TEXTURE2D : D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                        rtvDesc.Texture2DArray.MipSlice = 0;
                        rtvDesc.Texture2DArray.ArraySize = imageInfo.arraySize;
                        rtvDesc.Texture2DArray.FirstArraySlice = D3D11CalcSubresource(0, j, imageInfo.mipCount);
                        DX::ThrowIfFailed(deviceResources.device()->CreateRenderTargetView(resources.runtimeTexture, &rtvDesc, resources.runtimeTextureRtv[j].GetAddressOf()));

                        commonResources.imageResources.push_back(resources);
                    }

                    // Let the app use our downscaled texture and keep track of the resources to use during xrEndFrame().
                    d3dImages[i].texture = resources.appTexture.Get();
                }

                // Create the GPU timers.
                if (config.enableStats)
                {
                    InitTimer(commonResources.scalerTimer);
                    InitTimer(commonResources.colorConversionTimer);
                }
            }
            catch (std::runtime_error exc)
            {
                Log("Error: %s\n", exc.what());
            }
        }

        DebugLog("<-- NISScaler_xrEnumerateSwapchainImages %d\n", result);
        
        return result;
    }

    // We override this OpenXR API in order to record which texture within a swapchain is being submitted to xrEndFrame().
    XrResult NISScaler_xrAcquireSwapchainImage(
        const XrSwapchain swapchain,
        const XrSwapchainImageAcquireInfo* const acquireInfo,
        uint32_t* const index)
    {
        DebugLog("--> NISScaler_xrAcquireSwapchainImage\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrAcquireSwapchainImage(swapchain, acquireInfo, index);
        if (result == XR_SUCCESS)
        {
            auto swapchainIndex = swapchainIndices.find(swapchain);
            if (swapchainIndex != swapchainIndices.end())
            {
                // Keep track of the current texture index.
                swapchainIndex->second = *index;
            }
        }

        DebugLog("<-- NISScaler_xrAcquireSwapchainImage %d\n", result);

        return result;
    }

    // We override this OpenXR API in order to apply the NIS scaling and submit its output to the OpenXR runtime.
    XrResult NISScaler_xrEndFrame(
        const XrSession session,
        const XrFrameEndInfo* const frameEndInfo)
    {
        static ScalingMode lastFrameScalingMode = scalingMode;

        DebugLog("--> NISScaler_xrEndFrame\n");

        stats.numFrames++;

        // Check keyboard input.
        HandleHotkeys();

        // Unbind any RTV to avoid D3D debug layer warning.
        {
            ID3D11RenderTargetView* const rtvs[] = { nullptr };
            deviceResources.context()->OMSetRenderTargets(1, rtvs, nullptr);
        }

        // Go through each projection layer.
        std::vector<const XrCompositionLayerBaseHeader*> layers(frameEndInfo->layerCount);
        for (uint32_t i = 0; i < frameEndInfo->layerCount; i++)
        {
            if (frameEndInfo->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION)
            {
                const XrCompositionLayerProjection* proj = reinterpret_cast<const XrCompositionLayerProjection*>(frameEndInfo->layers[i]);
                for (uint32_t j = 0; j < proj->viewCount; j++)
                {
                    // Check whether this layer can be upscaled.
                    const XrCompositionLayerProjectionView& view = proj->views[j];
                    auto scalerResourceIt = scalerResources.find(view.subImage.swapchain);
                    if (scalerResourceIt == scalerResources.end())
                    {
                        continue;
                    }

                    // Collect the resources and properties of the swapchain.
                    const ScalerResources& commonResources = scalerResourceIt->second;
                    const XrSwapchainCreateInfo& imageInfo = commonResources.swapchainInfo;
                    const SwapchainImageResources& swapchainResources = commonResources.imageResources[swapchainIndices[view.subImage.swapchain]];
                    const bool indirectMode = IsIndirectlySupportedColorFormat((DXGI_FORMAT)imageInfo.format);
                    const bool needColorConversion = !isIntermediateFormatCompatible;

                    // Update the statistics.
                    if (config.enableStats)
                    {
                        stats.totalScalerTime += QueryTimer(commonResources.scalerTimer);
                        stats.totalColorConversionTime += QueryTimer(commonResources.colorConversionTimer);

                        const uint64_t now = GetTickCount64();
                        if (now >= stats.nextWindow || (scalingMode != lastFrameScalingMode && stats.numFrames))
                        {
                            Log("numFrames=%u (%u fps), scalerTime=%lu, colorConversionTime=%lu\n",
                                stats.numFrames, (1000 * stats.numFrames) / (now - stats.windowBeginning),
                                stats.totalScalerTime / stats.numFrames,
                                stats.totalColorConversionTime / stats.numFrames);

                            stats.Reset();

                            stats.windowBeginning = now;
                            stats.nextWindow = stats.windowBeginning + StatsPeriodMs;
                        }
                    }

                    // Adjust the scaler's settings if needed.
                    if (abs(config.sharpness - newSharpness) > FLT_EPSILON)
                    {
                        if (commonResources.bilinearScaler)
                        {
                            commonResources.bilinearScaler->update(imageInfo.width, imageInfo.height, actualDisplayWidth, actualDisplayHeight);
                        }
                        if (commonResources.NISScaler)
                        {
                            commonResources.NISScaler->update(newSharpness, imageInfo.width, imageInfo.height, actualDisplayWidth, actualDisplayHeight);
                        }
                        else
                        {
                            commonResources.NISSharpen->update(newSharpness, imageInfo.width, imageInfo.height);
                        }
                        config.sharpness = newSharpness;
                    }

                    // Invoke the scaler.
                    // TODO: Need to handle imageRect properly.
                    ID3D11ShaderResourceView* const srv = swapchainResources.appTextureSrv[view.subImage.imageArrayIndex].Get();
                    ID3D11UnorderedAccessView* const uav = swapchainResources.upscaledTextureUav[view.subImage.imageArrayIndex].Get();
                    if (scalingMode == ScalingMode::NIS)
                    {
                        StartTimer(commonResources.scalerTimer);
                        if (commonResources.NISScaler)
                        {
                            commonResources.NISScaler->dispatch(&srv, &uav);
                        }
                        else
                        {
                            commonResources.NISSharpen->dispatch(&srv, &uav);
                        }
                        StopTimer(commonResources.scalerTimer);

                        // Unbind the UAV to avoid D3D debug layer warning.
                        ID3D11UnorderedAccessView* const uavs = { nullptr };
                        deviceResources.context()->CSSetUnorderedAccessViews(0, 1, &uavs, nullptr);
                    }
                    else if (scalingMode == ScalingMode::Bilinear)
                    {
                        StartTimer(commonResources.scalerTimer);
                        commonResources.bilinearScaler->dispatch(&srv, &uav);
                        StopTimer(commonResources.scalerTimer);

                        // Unbind the UAV to avoid D3D debug layer warning.
                        ID3D11UnorderedAccessView* const uavs = { nullptr };
                        deviceResources.context()->CSSetUnorderedAccessViews(1, 1, &uavs, nullptr);
                    }

                    // Perform color conversion if needed. We also reuse this (basic) shader to perform unfiltered upscale for comparison.
                    if (needColorConversion || scalingMode == ScalingMode::Flat)
                    {
                        StartTimer(scalingMode == ScalingMode::Flat ? commonResources.scalerTimer : commonResources.colorConversionTimer);

                        ComPtr<ID3D11DeviceContext> executionContext;
                        if (!config.fastContextSwitch)
                        {
                            // Use a deferred context so we can use the context saving feature.
                            DX::ThrowIfFailed(deviceResources.device()->CreateDeferredContext(0, executionContext.GetAddressOf()));
                            executionContext->ClearState();
                        }
                        else
                        {
                            executionContext.Attach(deviceResources.context());
                        }

                        // Draw a quad to invoke our shader.
                        ID3D11RenderTargetView* const rtvs[] = { swapchainResources.runtimeTextureRtv[view.subImage.imageArrayIndex].Get() };
                        executionContext->OMSetRenderTargets(1, rtvs, nullptr);
                        executionContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
                        executionContext->OMSetDepthStencilState(nullptr, 0);
                        executionContext->VSSetShader(colorConversionVertexShader.Get(), nullptr, 0);
                        executionContext->PSSetShader(colorConversionPixelShader.Get(), nullptr, 0);
                        ID3D11ShaderResourceView* const srvs[] = {
                            scalingMode == ScalingMode::Flat ? swapchainResources.appTextureSrv[view.subImage.imageArrayIndex].Get() : commonResources.intermediateTextureSrv[view.subImage.imageArrayIndex].Get()
                        };
                        executionContext->PSSetShaderResources(0, 1, srvs);
                        ID3D11SamplerState* const ss[] = { colorConversionSampler.Get() };
                        executionContext->PSSetSamplers(0, 1, ss);
                        executionContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
                        executionContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
                        executionContext->IASetInputLayout(nullptr);
                        executionContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

                        // TODO: Need to handle imageRect properly.
                        CD3D11_VIEWPORT viewport(0.f, 0.f, (float)actualDisplayWidth, (float)actualDisplayHeight);
                        executionContext->RSSetViewports(1, &viewport);
                        executionContext->RSSetState(imageInfo.sampleCount > 1 ? colorConversionRasterizerMSAA.Get() : colorConversionRasterizer.Get());

                        executionContext->Draw(4, 0);

                        if (!config.fastContextSwitch)
                        {
                            // Execute the commands now and make sure we restore the context.
                            ComPtr<ID3D11CommandList> commandList;
                            DX::ThrowIfFailed(executionContext->FinishCommandList(FALSE, commandList.GetAddressOf()));
                            deviceResources.context()->ExecuteCommandList(commandList.Get(), TRUE);
                        }
                        else
                        {
                            executionContext.Detach();
                        }

                        StopTimer(scalingMode == ScalingMode::Flat ? commonResources.scalerTimer : commonResources.colorConversionTimer);
                    }

                    // Forward the real texture size to OpenXR.
                    // TODO: This is non-compliant AND dangerous. We cannot bypass the constness here and should make a copy instead.
                    ((XrCompositionLayerProjectionView*)&view)->subImage.imageRect.extent.width = actualDisplayWidth;
                    ((XrCompositionLayerProjectionView*)&view)->subImage.imageRect.extent.height = actualDisplayHeight;

                    // Perform upscaling for the depth layer if needed.
                    const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(view.next);
                    while (entry)
                    {
                        if (entry->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR)
                        {
                            const XrCompositionLayerDepthInfoKHR* depth = reinterpret_cast<const XrCompositionLayerDepthInfoKHR*>(entry);

                            // TODO: Support depth.
                            break;
                        }

                        entry = entry->next;
                    }
                }
            }
        }

        lastFrameScalingMode = scalingMode;

        // Call the chain to perform the actual submission.
        const XrResult result = next_xrEndFrame(session, frameEndInfo);

        DebugLog("<-- NISScaler_xrEndFrame %d\n", result);

        return result;
    }

    // Entry point for OpenXR calls.
    XrResult NISScaler_xrGetInstanceProcAddr(
        const XrInstance instance,
        const char* const name,
        PFN_xrVoidFunction* const function)
    {
        DebugLog("--> NISScaler_xrGetInstanceProcAddr \"%s\"\n", name);

        // Call the chain to resolve the next function pointer.
        const XrResult result = next_xrGetInstanceProcAddr(instance, name, function);
        if (config.loaded && result == XR_SUCCESS)
        {
            const std::string apiName(name);

            // Intercept the calls handled by our layer.
#define INTERCEPT_CALL(xrCall)                                                          \
            if (apiName == STRINGIFY(xrCall)) {                                         \
                next_##xrCall = reinterpret_cast<PFN_##xrCall>(*function);              \
                *function = reinterpret_cast<PFN_xrVoidFunction>(NISScaler_##xrCall);   \
            }

            INTERCEPT_CALL(xrEnumerateViewConfigurationViews);
            INTERCEPT_CALL(xrCreateSwapchain);
            INTERCEPT_CALL(xrDestroySwapchain);
            INTERCEPT_CALL(xrEnumerateSwapchainImages);
            INTERCEPT_CALL(xrCreateSession);
            INTERCEPT_CALL(xrDestroySession);
            INTERCEPT_CALL(xrAcquireSwapchainImage);
            INTERCEPT_CALL(xrEndFrame);

#undef INTERCEPT_CALL

            // Leave all unhandled calls to the next layer.
        }

        DebugLog("<-- NISScaler_xrGetInstanceProcAddr %d\n", result);

        return result;
    }

    // Entry point for creating the layer.
    XrResult NISScaler_xrCreateApiLayerInstance(
        const XrInstanceCreateInfo* const instanceCreateInfo,
        const struct XrApiLayerCreateInfo* const apiLayerInfo,
        XrInstance* const instance)
    {
        DebugLog("--> NISScaler_xrCreateApiLayerInstance\n");

        if (!apiLayerInfo ||
            apiLayerInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO ||
            apiLayerInfo->structVersion != XR_API_LAYER_CREATE_INFO_STRUCT_VERSION ||
            apiLayerInfo->structSize != sizeof(XrApiLayerCreateInfo) ||
            !apiLayerInfo->nextInfo ||
            apiLayerInfo->nextInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO ||
            apiLayerInfo->nextInfo->structVersion != XR_API_LAYER_NEXT_INFO_STRUCT_VERSION ||
            apiLayerInfo->nextInfo->structSize != sizeof(XrApiLayerNextInfo) ||
            apiLayerInfo->nextInfo->layerName != LayerName ||
            !apiLayerInfo->nextInfo->nextGetInstanceProcAddr ||
            !apiLayerInfo->nextInfo->nextCreateApiLayerInstance)
        {
            Log("xrCreateApiLayerInstance validation failed\n");
            return XR_ERROR_INITIALIZATION_FAILED;
        }

        // Store the next xrGetInstanceProcAddr to resolve the functions not handled by our layer.
        next_xrGetInstanceProcAddr = apiLayerInfo->nextInfo->nextGetInstanceProcAddr;

        // Call the chain to create the instance.
        XrApiLayerCreateInfo chainApiLayerInfo = *apiLayerInfo;
        chainApiLayerInfo.nextInfo = apiLayerInfo->nextInfo->next;
        const XrResult result = apiLayerInfo->nextInfo->nextCreateApiLayerInstance(instanceCreateInfo, &chainApiLayerInfo, instance);
        if (result == XR_SUCCESS)
        {
            PFN_xrGetInstanceProperties xrGetInstanceProperties;
            XrInstanceProperties instanceProperties = { XR_TYPE_INSTANCE_PROPERTIES };
            if (next_xrGetInstanceProcAddr(*instance, "xrGetInstanceProperties", reinterpret_cast<PFN_xrVoidFunction*>(&xrGetInstanceProperties)) == XR_SUCCESS &&
                xrGetInstanceProperties(*instance, &instanceProperties) == XR_SUCCESS)
            {
                const std::string runtimeName(instanceProperties.runtimeName);
                Log("Using OpenXR runtime %s, version %u.%u.%u\n", runtimeName.c_str(),
                    XR_VERSION_MAJOR(instanceProperties.runtimeVersion), XR_VERSION_MINOR(instanceProperties.runtimeVersion), XR_VERSION_PATCH(instanceProperties.runtimeVersion));

                needBindUnorderedAccessWorkaround = runtimeName.find("SteamVR") != std::string::npos;
            }

            next_xrGetInstanceProcAddr(*instance, "xrEnumerateSwapchainFormats", reinterpret_cast<PFN_xrVoidFunction*>(&next_xrEnumerateSwapchainFormats));

            config.Reset();

            // Identify the application and load our configuration. Try by application first, then fallback to engines otherwise.
            if (!LoadConfiguration(instanceCreateInfo->applicationInfo.applicationName)) {
                LoadConfiguration(instanceCreateInfo->applicationInfo.engineName);
            }
            config.Dump();
        }

        DebugLog("<-- NISScaler_xrCreateApiLayerInstance %d\n", result);

        return result;
    }

}

extern "C" {

    // Entry point for the loader.
    XrResult __declspec(dllexport) XRAPI_CALL NISScaler_xrNegotiateLoaderApiLayerInterface(
        const XrNegotiateLoaderInfo* const loaderInfo,
        const char* const apiLayerName,
        XrNegotiateApiLayerRequest* const apiLayerRequest)
    {
        DebugLog("--> (early) NISScaler_xrNegotiateLoaderApiLayerInterface\n");

        // Retrieve the path of the DLL.
        if (dllHome.empty())
        {
            HMODULE module;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&dllHome, &module))
            {
                char path[_MAX_PATH];
                GetModuleFileNameA(module, path, sizeof(path));
                dllHome = std::filesystem::path(path).parent_path().string();
            }
            else
            {
                // Falling back to loading config/writing logs to the current working directory.
                DebugLog("Failed to locate DLL\n");
            }

            nisShaderHome = (std::filesystem::path(dllHome) / std::filesystem::path("NVIDIAImageScaling") / std::filesystem::path("NIS")).string();
        }

        // Start logging to file.
        if (!logStream.is_open())
        {
            std::string logFile = (std::filesystem::path(getenv("LOCALAPPDATA")) / std::filesystem::path(LayerName + ".log")).string();
            logStream.open(logFile, std::ios_base::ate);
            Log("dllHome is \"%s\"\n", dllHome.c_str());
        }

        DebugLog("--> NISScaler_xrNegotiateLoaderApiLayerInterface\n");

        if (apiLayerName && apiLayerName != LayerName)
        {
            Log("Invalid apiLayerName \"%s\"\n", apiLayerName);
            return XR_ERROR_INITIALIZATION_FAILED;
        }

        if (!loaderInfo ||
            !apiLayerRequest ||
            loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
            loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
            loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo) ||
            apiLayerRequest->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
            apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
            apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest) ||
            loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
            loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
            loaderInfo->maxInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
            loaderInfo->maxApiVersion < XR_CURRENT_API_VERSION ||
            loaderInfo->minApiVersion > XR_CURRENT_API_VERSION)
        {
            Log("xrNegotiateLoaderApiLayerInterface validation failed\n");
            return XR_ERROR_INITIALIZATION_FAILED;
        }

        // Setup our layer to intercept OpenXR calls.
        apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
        apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
        apiLayerRequest->getInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(NISScaler_xrGetInstanceProcAddr);
        apiLayerRequest->createApiLayerInstance = reinterpret_cast<PFN_xrCreateApiLayerInstance>(NISScaler_xrCreateApiLayerInstance);

        DebugLog("<-- NISScaler_xrNegotiateLoaderApiLayerInterface\n");

        Log("%s layer (%s) is active\n", LayerName.c_str(), VersionString.c_str());

        return XR_SUCCESS;
    }

}
