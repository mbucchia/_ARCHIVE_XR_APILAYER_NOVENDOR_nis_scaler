// Implement the necessary DeviceResources methods to interop with the NIS sample code.
//
// This is mostly copy/pasted from the NIS SDK, hence reproducing the copyright notice below:
//
// The MIT License(MIT)
//
// Copyright(c) 2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files(the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "pch.h"

#include "DeviceResources.h"
#include <DXUtilities.h>

// HACK: We use this method to initialize the object.
// This is very dirty, but allows us to use the NIS sample code from the Git submodule without any changes.
void DeviceResources::create(HWND hWnd, uint32_t adapterIdx)
{
    m_d3dDevice = reinterpret_cast<ID3D11Device*>(hWnd);
    if (!m_d3dDevice)
    {
        m_d3dContext = nullptr;
        m_initialized = false;
        return;
    }

    m_d3dDevice->GetImmediateContext(m_d3dContext.ReleaseAndGetAddressOf());
    m_initialized = true;
}

// These are copy/pasted as-is from DeviceResources.cpp.

void DeviceResources::createSRV(ID3D11Resource* pResource, DXGI_FORMAT format, ID3D11ShaderResourceView** ppSRView)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    DX::ThrowIfFailed(m_d3dDevice->CreateShaderResourceView(pResource, &srvDesc, ppSRView));
}

void DeviceResources::createLinearClampSampler(ID3D11SamplerState** ppSampleState)
{
    D3D11_SAMPLER_DESC samplerDesc;
    ZeroMemory(&samplerDesc, sizeof(D3D11_SAMPLER_DESC));
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    DX::ThrowIfFailed(m_d3dDevice->CreateSamplerState(&samplerDesc, ppSampleState));
}

void DeviceResources::createTexture2D(int w, int h, DXGI_FORMAT format, D3D11_USAGE heapType, const void* data, uint32_t rowPitch, uint32_t imageSize, ID3D11Texture2D** ppTexture2D)
{
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    desc.MiscFlags = 0;
    desc.Usage = heapType;
    if (heapType == D3D11_USAGE_STAGING)
    {
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags = 0;
    }
    else
    {
        desc.CPUAccessFlags = 0;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }

    D3D11_SUBRESOURCE_DATA* pInitialData = nullptr;
    D3D11_SUBRESOURCE_DATA initData;
    if (data)
    {
        initData.pSysMem = data;
        initData.SysMemPitch = static_cast<uint32_t>(rowPitch);
        initData.SysMemSlicePitch = static_cast<uint32_t>(imageSize);
        pInitialData = &initData;
    }

    DX::ThrowIfFailed(m_d3dDevice->CreateTexture2D(&desc, pInitialData, ppTexture2D));
}

void DeviceResources::updateConstBuffer(void* data, uint32_t size, ID3D11Buffer* ppBuffer)
{
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    m_d3dContext->Map(ppBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    uint8_t* mappData = (uint8_t*)mappedResource.pData;
    memcpy(mappData, data, size);
    m_d3dContext->Unmap(ppBuffer, 0);
}

void DeviceResources::createConstBuffer(void* initialData, uint32_t size, ID3D11Buffer** ppBuffer)
{
    D3D11_BUFFER_DESC bDesc;
    ZeroMemory(&bDesc, sizeof(D3D11_BUFFER_DESC));
    bDesc.ByteWidth = size;
    bDesc.Usage = D3D11_USAGE_DYNAMIC;
    bDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bDesc.MiscFlags = 0;
    bDesc.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA srData;
    srData.pSysMem = initialData;
    DX::ThrowIfFailed(m_d3dDevice->CreateBuffer(&bDesc, &srData, ppBuffer));
}

// The methods below are not used in our use case.

void DeviceResources::createUAV(ID3D11Resource* pResource, DXGI_FORMAT format, ID3D11UnorderedAccessView** ppUAView)
{
    abort();
}

void DeviceResources::initRenderTarget()
{
    abort();
}

void DeviceResources::resizeRenderTarget(uint32_t Width, uint32_t Height, DXGI_FORMAT format)
{
    abort();
}

void DeviceResources::clearRenderTargetView(const float color[4])
{
    abort();
}

void DeviceResources::getTextureData(ID3D11Texture2D* texture, uint8_t* data)
{
    abort();
}
