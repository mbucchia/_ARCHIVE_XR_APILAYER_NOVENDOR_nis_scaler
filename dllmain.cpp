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

#define STRINGIFY(s) XSTRINGIFY(s)
#define XSTRINGIFY(s) #s

namespace {
    using Microsoft::WRL::ComPtr;

    const std::string LayerName = "XR_APILAYER_NOVENDOR_nis_scaler";

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

    // State of the layer
    bool useD3D11 = false;
    bool useD3D12 = false;
    uint32_t actualDisplayWidth;
    uint32_t actualDisplayHeight;
    ComPtr<ID3D11Device> d3d11Device = nullptr;
    DeviceResources deviceResources;
    DXGI_FORMAT indirectFormat = DXGI_FORMAT_UNKNOWN;
    std::unordered_map<XrSwapchain, std::shared_ptr<BilinearUpscale>> bilinearScaler;
    std::unordered_map<XrSwapchain, std::shared_ptr<NVScaler>> NISScaler;
    std::unordered_map<XrSwapchain, XrSwapchainCreateInfo> swapchainInfo;
    struct ScalerResources
    {
        // Common resources.
        ID3D11Texture2D* runtimeTexture;
        ComPtr<ID3D11UnorderedAccessView> runtimeTextureUav[2];
        ComPtr<ID3D11Texture2D> appTexture;
        ComPtr<ID3D11ShaderResourceView> appTextureSrv[2];

        // Resources for indirect color conversion mode.
        ComPtr<ID3D11RenderTargetView> runtimeTextureRtv[2];
        ComPtr<ID3D11Texture2D> intermediateTexture;
        ComPtr<ID3D11ShaderResourceView> intermediateTextureSrv[2];
    };
    std::unordered_map<XrSwapchain, std::vector<ScalerResources>> d3d11TextureMap;
    std::unordered_map<XrSwapchain, uint32_t> swapchainIndices;
    ComPtr<ID3D11VertexShader> colorConversionVertexShader;
    ComPtr<ID3D11PixelShader> colorConversionPixelShader;
    ComPtr<ID3D11SamplerState> colorConversionSampler;
    ComPtr<ID3D11RasterizerState> colorConversionRasterizer;

    bool useBilinearScaler = false;
    float newSharpness;

    void Log(const char* fmt, ...);

    struct {
        bool loaded;
        float scaleFactor;
        float sharpness;
        bool onlyAdvertiseCapableFormats;
        bool prioritizeCapableFormats;

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
                if (isDebugBuild)
                {
                    Log("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                    Log("!!! USING DEBUG SETTINGS - PERFORMANCE WILL BE DECREASED             !!!\n");
                    Log("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                }
                if (onlyAdvertiseCapableFormats)
                {
                    Log("Only advertise texture formats supported by this layer\n");
                }
                else
                {
                    Log("Advertise all texture formats\n");
                    if (prioritizeCapableFormats)
                    {
                        Log("Prioritize capable formats\n");
                    }
                }
                Log("Use scaling factor: %.3f\n", scaleFactor);
                Log("Sharpness set to %.3f\n", sharpness);
            }
        }

        void Reset()
        {
            loaded = false;
            scaleFactor = 0.7f;
            sharpness = 1.0f;
            onlyAdvertiseCapableFormats = false;
            prioritizeCapableFormats = true;
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
                        else if (name == "only_advertise_capable_formats")
                        {
                            config.onlyAdvertiseCapableFormats = value == "1" || value == "true";
                        }
                        else if (name == "prioritize_capable_formats")
                        {
                            config.prioritizeCapableFormats = value == "1" || value == "true";
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

    bool IsSupportedColorFormat(
        const DXGI_FORMAT format)
    {
        // Want to use ID3D11Device::CheckFormatSupport() but there might not be a device. Use a list instead.
        return format == DXGI_FORMAT_R8G8B8A8_UNORM
            || format == DXGI_FORMAT_R8G8B8A8_UINT
            || format == DXGI_FORMAT_R8G8B8A8_SNORM
            || format == DXGI_FORMAT_R8G8B8A8_SINT;
    }

    bool IsIndirectlySupportedColorFormat(
        const DXGI_FORMAT format)
    {
        return format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }

    bool IsSupportedDepthFormat(
        const DXGI_FORMAT format)
    {
        // TODO: Support depth.
        return false;
    }

    bool IsSwapchainHandled(
        const XrSwapchain swapchain)
    {
        return swapchainInfo.find(swapchain) != swapchainInfo.cend();
    }

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
            // Override the recommended image size to account for scaling.
            for (uint32_t i = 0; i < *viewCountOutput; i++)
            {
                actualDisplayWidth = views[i].recommendedImageRectWidth;
                actualDisplayHeight = views[i].recommendedImageRectHeight;

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

        DebugLog("<-- NISScaler_xrEnumerateViewConfigurationViews %d\n", result);

        return result;
    }

    XrResult NISScaler_xrEnumerateSwapchainFormats(
        const XrSession session,
        uint32_t formatCapacityInput,
        uint32_t* const formatCountOutput,
        int64_t* const formats)
    {
        DebugLog("--> NISScaler_xrEnumerateSwapchainFormats\n");

        XrResult result;
        int64_t* fullArrayOfFormats = nullptr;

        // We might need to return a smaller array. Always allocate the full-size array.
        result = next_xrEnumerateSwapchainFormats(session, 0, formatCountOutput, nullptr);
        if (result == XR_SUCCESS)
        {
            // TODO: Compliance: someone could've passed a smaller array...
            formatCapacityInput = *formatCountOutput;
            fullArrayOfFormats = new int64_t[formatCapacityInput];
        }

        // Call the chain to perform the actual operation. We always pass a full-size array.
        result = next_xrEnumerateSwapchainFormats(session, formatCapacityInput, formatCountOutput, fullArrayOfFormats);
        if (result == XR_SUCCESS && formatCapacityInput > 0)
        {
            if (config.onlyAdvertiseCapableFormats || config.prioritizeCapableFormats)
            {
                // Prioritize the formats that we can handle for scaling.
                for (uint32_t i = 0; i < *formatCountOutput; i++)
                {
                    if (!IsSupportedColorFormat((DXGI_FORMAT)fullArrayOfFormats[i]) && !IsSupportedDepthFormat((DXGI_FORMAT)fullArrayOfFormats[i]))
                    {
                        for (uint32_t j = i + 1; j < *formatCountOutput; j++)
                        {
                            if (IsSupportedColorFormat((DXGI_FORMAT)fullArrayOfFormats[j]) || IsSupportedDepthFormat((DXGI_FORMAT)fullArrayOfFormats[j]))
                            {
                                std::swap(fullArrayOfFormats[i], fullArrayOfFormats[j]);
                                break;
                            }
                        }
                    }
                }
            }

            // If needed, do not advertise formats that cannot be handled by this layer.
            if (config.onlyAdvertiseCapableFormats)
            {
                for (uint32_t i = 0; i < *formatCountOutput; i++)
                {
                    if (!IsSupportedColorFormat((DXGI_FORMAT)fullArrayOfFormats[i]) && !IsSupportedDepthFormat((DXGI_FORMAT)fullArrayOfFormats[i]))
                    {
                        *formatCountOutput = i + 1;
                        break;
                    }
                }
            }

            // Select the format for indirect mode here.
            for (uint32_t i = 0; i < *formatCountOutput; i++)
            {
                if (IsSupportedColorFormat((DXGI_FORMAT)fullArrayOfFormats[i]))
                {
                    indirectFormat = (DXGI_FORMAT)fullArrayOfFormats[i];
                    break;
                }
            }

            if (formats)
            {
                CopyMemory(formats, fullArrayOfFormats, *formatCountOutput * sizeof(uint64_t));
            }
        }

        if (fullArrayOfFormats)
        {
            delete[] fullArrayOfFormats;
        }

        DebugLog("<-- NISScaler_xrEnumerateSwapchainFormats %d\n", result);

        return result;
    }

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
            useD3D11 = useD3D12 = false;

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

                        // Initialize resources for color conversion (in case we actually need it).
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

                        useD3D11 = true;
                    }
                    else if (entry->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR)
                    {
                        // TODO: Support D3D12.
                        Log("D3D12 is not supported.\n");

                        useD3D12 = true;
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

            useBilinearScaler = false;
            newSharpness = config.sharpness;
        }

        DebugLog("<-- NISScaler_xrCreateSession %d\n", result);

        return result;
    }

    XrResult NISScaler_xrDestroySession(
        const XrSession session)
    {
        DebugLog("--> NISScaler_xrDestroySession\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrDestroySession(session);
        if (result == XR_SUCCESS)
        {
            // Cleanup all the scaler's resources.
            NISScaler.clear();
            bilinearScaler.clear();
            colorConversionRasterizer = nullptr;
            colorConversionSampler = nullptr;
            colorConversionPixelShader = nullptr;
            colorConversionVertexShader = nullptr;
            deviceResources.create(nullptr);
            d3d11Device = nullptr;
        }

        DebugLog("<-- NISScaler_xrDestroySession %d\n", result);

        return result;
    }

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
        const bool isHandled = createInfo->arraySize <= 2 && createInfo->faceCount == 1 && (isSupportedColorFormat || isSupportedDepthFormat);

        if (isHandled)
        {
            // Request the full device resolution. The app will not see this texture, only the runtime.
            chainCreateInfo.width = actualDisplayWidth;
            chainCreateInfo.height = actualDisplayHeight;

            // Make sure this format is supported for a UAV.
            if (isIndirectlySupportedColorFormat)
            {
                if (indirectFormat != DXGI_FORMAT_UNKNOWN)
                {
                    Log("Using indirect texture format mapping with format %d\n", indirectFormat);

                    // Fallback to a format supported by the scaler, and rely on implicit color space conversion.
                    chainCreateInfo.format = (uint64_t)indirectFormat;

                    // Add the flag to allow the textures to be the output of the scaler.
                    chainCreateInfo.usageFlags |= XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT;
                }
                else
                {
                    Log("Using indirect texture format conversion\n");

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
                    // Create the scalers.
                    // TODO: Add an option to disable the bilinear scaler and save memory.
                    {
                        auto scaler = std::make_shared<BilinearUpscale>(deviceResources);
                        scaler->update(createInfo->width, createInfo->height, actualDisplayWidth, actualDisplayHeight);
                        bilinearScaler.insert_or_assign(*swapchain, scaler);
                    }
                    {
                        auto scaler = std::make_shared<NVScaler>(deviceResources, dllHome);
                        scaler->update(config.sharpness, createInfo->width, createInfo->height, actualDisplayWidth, actualDisplayHeight);
                        NISScaler.insert_or_assign(*swapchain, scaler);
                    }

                    // We keep track of the (real) swapchain info for when we intercept the textures in xrEnumerateSwapchainImages().
                    swapchainInfo.insert_or_assign(*swapchain, *createInfo);

                    // We will keep track of the textures we distribute to the app.
                    d3d11TextureMap.insert_or_assign(*swapchain, std::vector<ScalerResources>());
                }
                catch (std::runtime_error exc)
                {
                    Log("Error: %s\n", exc.what());
                }
            }
            else
            {
                Log("Swapchain with format %d and array size %u is not supported.\n", createInfo->format, createInfo->arraySize);
            }
        }
        else
        {
            Log("xrCreateSwapchain failed with %d\n", result);
        }

        DebugLog("<-- NISScaler_xrCreateSwapchain %d\n", result);

        return result;
    }

    XrResult NISScaler_xrDestroySwapchain(
        const XrSwapchain swapchain)
    {
        DebugLog("--> NISScaler_xrDestroySwapchain\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrDestroySwapchain(swapchain);
        if (result == XR_SUCCESS && IsSwapchainHandled(swapchain))
        {
            // Cleanup the resources.
            bilinearScaler.erase(swapchain);
            NISScaler.erase(swapchain);
            d3d11TextureMap.erase(swapchain);

            // Forget about this swapchain.
            swapchainIndices.erase(swapchain);
            swapchainInfo.erase(swapchain);
        }

        DebugLog("<-- NISScaler_xrDestroySwapchain %d\n", result);

        return result;
    }

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
                if (useD3D11)
                {
                    XrSwapchainImageD3D11KHR* d3dImages = reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
                    const XrSwapchainCreateInfo& imageInfo = swapchainInfo.find(swapchain)->second;
                    const bool indirectMode = IsIndirectlySupportedColorFormat((DXGI_FORMAT)imageInfo.format);
                    const bool needColorConversion = indirectMode && indirectFormat == DXGI_FORMAT_UNKNOWN;
                    for (uint32_t i = 0; i < *imageCountOutput; i++)
                    {
                        ScalerResources resources;
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
                        // TODO: Investigate doing this through a TYPELESS instead... Would be better performance.
                        if (needColorConversion)
                        {
                            textureDesc.Width = actualDisplayWidth;
                            textureDesc.Height = actualDisplayHeight;
                            textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                            textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                            DX::ThrowIfFailed(deviceResources.device()->CreateTexture2D(&textureDesc, nullptr, resources.intermediateTexture.GetAddressOf()));
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
                            srvDesc.Texture2DArray.ArraySize = imageInfo.arraySize;
                            srvDesc.Texture2DArray.FirstArraySlice = D3D11CalcSubresource(0, j, imageInfo.mipCount);
                            DX::ThrowIfFailed(deviceResources.device()->CreateShaderResourceView(resources.appTexture.Get(), &srvDesc, resources.appTextureSrv[j].GetAddressOf()));

                            if (needColorConversion)
                            {
                                srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                                DX::ThrowIfFailed(deviceResources.device()->CreateShaderResourceView(resources.intermediateTexture.Get(), &srvDesc, resources.intermediateTextureSrv[j].GetAddressOf()));
                            }

                            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
                            ZeroMemory(&uavDesc, sizeof(D3D11_UNORDERED_ACCESS_VIEW_DESC));
                            if (!indirectMode)
                            {
                                uavDesc.Format = (DXGI_FORMAT)imageInfo.format;
                            }
                            else if (!needColorConversion)
                            {
                                uavDesc.Format = indirectFormat;
                            }
                            else
                            {
                                uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                            }
                            uavDesc.ViewDimension = imageInfo.arraySize == 1 ? D3D11_UAV_DIMENSION_TEXTURE2D : D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
                            uavDesc.Texture2DArray.MipSlice = 0;
                            uavDesc.Texture2DArray.ArraySize = imageInfo.arraySize;
                            uavDesc.Texture2DArray.FirstArraySlice = D3D11CalcSubresource(0, j, imageInfo.mipCount);
                            ID3D11Resource* const targetTexture = needColorConversion ? resources.intermediateTexture.Get() : resources.runtimeTexture;
                            DX::ThrowIfFailed(deviceResources.device()->CreateUnorderedAccessView(targetTexture, &uavDesc, resources.runtimeTextureUav[j].GetAddressOf()));

                            if (needColorConversion)
                            {
                                D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
                                ZeroMemory(&rtvDesc, sizeof(D3D11_RENDER_TARGET_VIEW_DESC));
                                rtvDesc.Format = (DXGI_FORMAT)imageInfo.format;
                                rtvDesc.ViewDimension = imageInfo.arraySize == 1 ? D3D11_RTV_DIMENSION_TEXTURE2D : D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                                rtvDesc.Texture2DArray.MipSlice = 0;
                                rtvDesc.Texture2DArray.ArraySize = imageInfo.arraySize;
                                rtvDesc.Texture2DArray.FirstArraySlice = D3D11CalcSubresource(0, j, imageInfo.mipCount);
                                DX::ThrowIfFailed(deviceResources.device()->CreateRenderTargetView(resources.runtimeTexture, &rtvDesc, resources.runtimeTextureRtv[j].GetAddressOf()));
                            }
                        }

                        // Let the app use our downscaled texture and keep track of the resources to use during xrEndFrame().
                        d3d11TextureMap[swapchain].push_back(resources);
                        d3dImages[i].texture = resources.appTexture.Get();
                    }
                }
                else if (useD3D12)
                {
                    // TODO: Support D3D12.
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

    XrResult NISScaler_xrAcquireSwapchainImage(
        const XrSwapchain swapchain,
        const XrSwapchainImageAcquireInfo* const acquireInfo,
        uint32_t* const index)
    {
        DebugLog("--> NISScaler_xrAcquireSwapchainImage\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrAcquireSwapchainImage(swapchain, acquireInfo, index);
        if (result == XR_SUCCESS && IsSwapchainHandled(swapchain))
        {
            // Keep track of the current texture index.
            swapchainIndices.insert_or_assign(swapchain, *index);
        }

        DebugLog("<-- NISScaler_xrAcquireSwapchainImage %d\n", result);

        return result;
    }

    XrResult NISScaler_xrEndFrame(
        const XrSession session,
        const XrFrameEndInfo* const frameEndInfo)
    {
        DebugLog("--> NISScaler_xrEndFrame\n");

        // Check keyboard input.
        {
            static bool wasF1Pressed = false;
            const bool isF1Pressed = GetAsyncKeyState(VK_CONTROL) && (GetAsyncKeyState(VK_LEFT) || GetAsyncKeyState(VK_F1));
            if (!wasF1Pressed && isF1Pressed)
            {
                useBilinearScaler = !useBilinearScaler;
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

        // Go through each projection layer.
        std::vector<const XrCompositionLayerBaseHeader*> layers(frameEndInfo->layerCount);
        for (uint32_t i = 0; i < frameEndInfo->layerCount; i++)
        {
            if (frameEndInfo->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION)
            {
                const XrCompositionLayerProjection* proj = reinterpret_cast<const XrCompositionLayerProjection*>(frameEndInfo->layers[i]);

                for (uint32_t j = 0; j < proj->viewCount; j++)
                {
                    const auto& view = proj->views[j];

                    if (!IsSwapchainHandled(view.subImage.swapchain))
                    {
                        continue;
                    }

                    // Perform upscaling.
                    if (useD3D11)
                    {
                        const XrSwapchainCreateInfo& imageInfo = swapchainInfo.find(view.subImage.swapchain)->second;
                        const bool indirectMode = IsIndirectlySupportedColorFormat((DXGI_FORMAT)imageInfo.format);
                        const bool needColorConversion = indirectMode && indirectFormat == DXGI_FORMAT_UNKNOWN;

                        // Adjust the scaler if needed.
                        if (abs(config.sharpness - newSharpness) > FLT_EPSILON)
                        {
                            bilinearScaler[view.subImage.swapchain]->update(imageInfo.width, imageInfo.height, actualDisplayWidth, actualDisplayHeight);
                            NISScaler[view.subImage.swapchain]->update(newSharpness, imageInfo.width, imageInfo.height, actualDisplayWidth, actualDisplayHeight);
                            config.sharpness = newSharpness;
                        }

                        // Invoke the scaler.
                        const ScalerResources& colorResources = d3d11TextureMap[view.subImage.swapchain][swapchainIndices[view.subImage.swapchain]];
                        ID3D11ShaderResourceView* const srv = colorResources.appTextureSrv[view.subImage.imageArrayIndex].Get();
                        ID3D11UnorderedAccessView* const uav = colorResources.runtimeTextureUav[view.subImage.imageArrayIndex].Get();
                        if (!useBilinearScaler)
                        {
                            NISScaler[view.subImage.swapchain]->dispatch(&srv, &uav);
                        }
                        else
                        {
                            bilinearScaler[view.subImage.swapchain]->dispatch(&srv, &uav);
                        }
                        deviceResources.context()->OMSetRenderTargets(0, nullptr, nullptr);

                        // Perform color conversion if needed.
                        if (needColorConversion)
                        {
                            // Use a deferred context so we can use the context saving feature.
                            ComPtr<ID3D11DeviceContext> deferredContext;
                            DX::ThrowIfFailed(deviceResources.device()->CreateDeferredContext(0, deferredContext.GetAddressOf()));

                            deferredContext->ClearState();

                            ID3D11RenderTargetView* const rtvs[] = { colorResources.runtimeTextureRtv[view.subImage.imageArrayIndex].Get() };
                            deferredContext->OMSetRenderTargets(1, rtvs, nullptr);
                            deferredContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
                            deferredContext->OMSetDepthStencilState(nullptr, 0);
                            deferredContext->VSSetShader(colorConversionVertexShader.Get(), nullptr, 0);
                            deferredContext->PSSetShader(colorConversionPixelShader.Get(), nullptr, 0);
                            ID3D11ShaderResourceView* const srvs[] = { colorResources.intermediateTextureSrv[view.subImage.imageArrayIndex].Get() };
                            deferredContext->PSSetShaderResources(0, 1, srvs);
                            ID3D11SamplerState* const ss[] = { colorConversionSampler.Get() };
                            deferredContext->PSSetSamplers(0, 1, ss);
                            deferredContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
                            deferredContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
                            deferredContext->IASetInputLayout(nullptr);
                            deferredContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

                            // TODO: Need to handle imageRect properly.
                            CD3D11_VIEWPORT viewport(
                                0.0f, 0.0f,
                                (float)actualDisplayWidth, (float)actualDisplayHeight);
                            deferredContext->RSSetViewports(1, &viewport);
                            deferredContext->RSSetState(colorConversionRasterizer.Get());

                            deferredContext->Draw(4, 0);

                            // Execute the commands now.
                            ComPtr<ID3D11CommandList> commandList;
                            DX::ThrowIfFailed(deferredContext->FinishCommandList(FALSE, commandList.GetAddressOf()));
                            deviceResources.context()->ExecuteCommandList(commandList.Get(), TRUE);
                        }

                        // Forward the real texture size to OpenXR.
                        // TODO: This is non-compliant AND dangerous.
                        ((XrCompositionLayerProjectionView*)&view)->subImage.imageRect.extent.width = actualDisplayWidth;
                        ((XrCompositionLayerProjectionView*)&view)->subImage.imageRect.extent.height = actualDisplayHeight;
                    }
                    else if (useD3D12)
                    {
                        // TODO: Support D3D12.
                    }

                    // Perform upscaling for the depth layer if needed.
                    const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(view.next);
                    while (entry)
                    {
                        if (entry->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR)
                        {
                            const XrCompositionLayerDepthInfoKHR* depth = reinterpret_cast<const XrCompositionLayerDepthInfoKHR*>(entry);

                            if (!IsSwapchainHandled(depth->subImage.swapchain))
                            {
                                break;
                            }

                            // TODO: Support depth.

                            break;
                        }

                        entry = entry->next;
                    }
                }
            }
        }

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
            INTERCEPT_CALL(xrEnumerateSwapchainFormats);
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

        Log("%s layer is active\n", LayerName.c_str());

        return XR_SUCCESS;
    }

}
