/*
Copyright(c) 2016 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ===========================
#include "D3D11API.h"
#include "../../Logging/Log.h"
#include "../../Core/Helper.h"
#include "../../FileSystem/FileSystem.h"
#include "../Graphics.h"
#include "../../Core/Settings.h"
//======================================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

//= ENUMERATIONS ============================
static const D3D11_CULL_MODE d3dCullMode[] =
{
	D3D11_CULL_NONE,
	D3D11_CULL_BACK,
	D3D11_CULL_FRONT
};
//===========================================

Graphics::Graphics(Context* context) : Subsystem(context)
{
	m_inputLayout = PositionTextureNormalTangent;
	m_cullMode = CullBack;
	m_primitiveTopology = TriangleList;
	m_zBufferEnabled = true;
	m_alphaBlendingEnabled = false;

	m_api = new GraphicsAPI();
	m_api->m_device = nullptr;
	m_api->m_deviceContext = nullptr;
	m_api->m_swapChain = nullptr;
	m_api->m_renderTargetView = nullptr;
	m_api->m_driverType = D3D_DRIVER_TYPE_HARDWARE;
	m_api->m_featureLevel = D3D_FEATURE_LEVEL_11_0;

	m_api->m_displayModeList = nullptr;
	m_api->m_videoCardMemory = 0;
	m_api->m_videoCardDescription = DATA_NOT_ASSIGNED;

	m_api->m_depthStencilBuffer = nullptr;
	m_api->m_depthStencilStateEnabled = nullptr;
	m_api->m_depthStencilStateDisabled = nullptr;
	m_api->m_depthStencilView = nullptr;

	m_api->m_rasterStateCullFront = nullptr;
	m_api->m_rasterStateCullBack = nullptr;
	m_api->m_rasterStateCullNone = nullptr;

	m_api->m_blendStateAlphaEnabled = nullptr;
	m_api->m_blendStateAlphaDisabled = nullptr;
}

Graphics::~Graphics()
{
	// Before shutting down set to windowed mode or 
	// upon releasing the swap chain it will throw an exception.
	if (m_api->m_swapChain)
		m_api->m_swapChain->SetFullscreenState(false, nullptr);

	m_api->m_blendStateAlphaEnabled->Release();
	m_api->m_blendStateAlphaDisabled->Release();
	m_api->m_rasterStateCullFront->Release();
	m_api->m_rasterStateCullBack->Release();
	m_api->m_rasterStateCullNone->Release();
	m_api->m_depthStencilView->Release();
	m_api->m_depthStencilStateEnabled->Release();
	m_api->m_depthStencilStateDisabled->Release();
	m_api->m_depthStencilBuffer->Release();
	m_api->m_renderTargetView->Release();
	m_api->m_deviceContext->Release();
	m_api->m_device->Release();
	m_api->m_swapChain->Release();

	delete[] m_api->m_displayModeList;
	m_api->m_displayModeList = nullptr;

	delete m_api;
	m_api = nullptr;
}

void Graphics::Initialize(HWND windowHandle)
{
	//= GRAPHICS INTERFACE FACTORY =================================================
	IDXGIFactory* factory;
	HRESULT hResult = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&factory));
	if (FAILED(hResult))
		LOG_ERROR("Failed to create a DirectX graphics interface factory.");
	//==============================================================================

	//= ADAPTER ====================================================================
	IDXGIAdapter* adapter;
	hResult = factory->EnumAdapters(0, &adapter);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create a primary graphics interface adapter.");

	factory->Release();
	//==============================================================================

	//= ADAPTER OUTPUT / DISPLAY MODE ==============================================
	IDXGIOutput* adapterOutput;
	unsigned int numModes;

	// Enumerate the primary adapter output (monitor).
	hResult = adapter->EnumOutputs(0, &adapterOutput);
	if (FAILED(hResult))
		LOG_ERROR("Failed to enumerate the primary adapter output.");

	// Get the number of modes that fit the DXGI_FORMAT_R8G8B8A8_UNORM display format for the adapter output (monitor).
	hResult = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, nullptr);
	if (FAILED(hResult))
		LOG_ERROR("Failed to get adapter's display modes.");

	// Create display mode list
	DXGI_MODE_DESC* m_displayModeList = new DXGI_MODE_DESC[numModes];
	if (!m_displayModeList)
		LOG_ERROR("Failed to create a display mode list.");

	// Now fill the display mode list structures.
	hResult = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, m_displayModeList);
	if (FAILED(hResult))
		LOG_ERROR("Failed to fill the display mode list structures.");

	// Release the adapter output.
	adapterOutput->Release();

	// Go through all the display modes and find the one that matches the screen width and height.
	unsigned int numerator = 0, denominator = 1;
	for (auto i = 0; i < numModes; i++)
		if (m_displayModeList[i].Width == (unsigned int)RESOLUTION_WIDTH && m_displayModeList[i].Height == (unsigned int)RESOLUTION_HEIGHT)
		{
			numerator = m_displayModeList[i].RefreshRate.Numerator;
			denominator = m_displayModeList[i].RefreshRate.Denominator;
			break;
		}
	//==============================================================================

	//= ADAPTER DESCRIPTION ========================================================
	DXGI_ADAPTER_DESC adapterDesc;
	// Get the adapter (video card) description.
	hResult = adapter->GetDesc(&adapterDesc);
	if (FAILED(hResult))
		LOG_ERROR("Failed to get the adapter's description.");

	// Release the adapter.
	adapter->Release();

	// Store the dedicated video card memory in megabytes.
	m_api->m_videoCardMemory = (int)(adapterDesc.DedicatedVideoMemory / 1024 / 1024);
	//==============================================================================

	//= SWAP CHAIN =================================================================
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));

	swapChainDesc.BufferCount = 1;
	swapChainDesc.BufferDesc.Width = RESOLUTION_WIDTH;
	swapChainDesc.BufferDesc.Height = RESOLUTION_HEIGHT;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = windowHandle;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;

	// Set to full screen or windowed mode.
	swapChainDesc.Windowed = (BOOL)!FULLSCREEN_ENABLED;

	// Set the scan line ordering and scaling to unspecified.
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; // alt + enter fullscreen

	// Create the swap chain, Direct3D device, and Direct3D device context.
	hResult = D3D11CreateDeviceAndSwapChain(nullptr, m_api->m_driverType, nullptr, 0,
		&m_api->m_featureLevel, 1, D3D11_SDK_VERSION, &swapChainDesc,
		&m_api->m_swapChain, &m_api->m_device, nullptr, &m_api->m_deviceContext);

	if (FAILED(hResult))
		LOG_ERROR("Failed to create the swap chain, Direct3D device, and Direct3D device context.");
	//==============================================================================

	//= RENDER TARGET VIEW =========================================================
	ID3D11Texture2D* backBufferPtr;
	// Get the pointer to the back buffer.
	hResult = m_api->m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)(&backBufferPtr));
	if (FAILED(hResult))
		LOG_ERROR("Failed to get the pointer to the back buffer.");

	// Create the render target view with the back buffer pointer.
	hResult = m_api->m_device->CreateRenderTargetView(backBufferPtr, nullptr, &m_api->m_renderTargetView);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the render target view.");

	// Release pointer to the back buffer
	backBufferPtr->Release();
	backBufferPtr = nullptr;
	//==============================================================================

	// Depth Stencil Buffer
	CreateDepthStencilBuffer();

	// Depth-Stencil
	CreateDepthStencil();

	// DEPTH-STENCIL VIEW ==========================================================
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));

	// Set up the depth stencil view description.
	depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	// Create the depth stencil view.
	hResult = m_api->m_device->CreateDepthStencilView(m_api->m_depthStencilBuffer, &depthStencilViewDesc, &m_api->m_depthStencilView);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the depth stencil view.");

	// Bind the render target view and depth stencil buffer to the output render pipeline.
	m_api->m_deviceContext->OMSetRenderTargets(1, &m_api->m_renderTargetView, m_api->m_depthStencilView);
	//==============================================================================

	//= RASTERIZER =================================================================
	D3D11_RASTERIZER_DESC rasterizerDesc;
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	rasterizerDesc.FrontCounterClockwise = false;
	rasterizerDesc.DepthBias = 0;
	rasterizerDesc.SlopeScaledDepthBias = 0.0f;
	rasterizerDesc.DepthBiasClamp = 0.0f;
	rasterizerDesc.DepthClipEnable = true;
	rasterizerDesc.ScissorEnable = false;
	rasterizerDesc.MultisampleEnable = false;
	rasterizerDesc.AntialiasedLineEnable = false;

	// Create a rasterizer state with back face CullMode
	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	hResult = m_api->m_device->CreateRasterizerState(&rasterizerDesc, &m_api->m_rasterStateCullBack);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the rasterizer cull back state.");

	// Create a rasterizer state with front face CullMode
	rasterizerDesc.CullMode = D3D11_CULL_FRONT;
	hResult = m_api->m_device->CreateRasterizerState(&rasterizerDesc, &m_api->m_rasterStateCullFront);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the rasterizer cull front state.");

	// Create a rasterizer state with no face CullMode
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	hResult = m_api->m_device->CreateRasterizerState(&rasterizerDesc, &m_api->m_rasterStateCullNone);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create rasterizer cull none state.");

	// set the default rasterizer state
	m_api->m_deviceContext->RSSetState(m_api->m_rasterStateCullBack);
	//==============================================================================

	//= BLEND STATE ================================================================
	D3D11_BLEND_DESC blendStateDesc;
	ZeroMemory(&blendStateDesc, sizeof(blendStateDesc));
	blendStateDesc.RenderTarget[0].BlendEnable = (BOOL)true;
	blendStateDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendStateDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendStateDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendStateDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendStateDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendStateDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendStateDesc.RenderTarget[0].RenderTargetWriteMask = 0x0f;

	// Create a blending state with alpha blending enabled
	blendStateDesc.RenderTarget[0].BlendEnable = (BOOL)true;
	HRESULT result = m_api->m_device->CreateBlendState(&blendStateDesc, &m_api->m_blendStateAlphaEnabled);
	if (FAILED(result))
		LOG_ERROR("Failed to create blend state.");

	// Create a blending state with alpha blending disabled
	blendStateDesc.RenderTarget[0].BlendEnable = (BOOL)false;
	result = m_api->m_device->CreateBlendState(&blendStateDesc, &m_api->m_blendStateAlphaDisabled);
	if (FAILED(result))
		LOG_ERROR("Failed to create blend state.");
	//==============================================================================

	SetViewport(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);
}

//= DEPTH ======================================================================================================
void Graphics::EnableZBuffer(bool enable)
{
	if (m_zBufferEnabled == enable)
		return;

	// Set depth stencil state
	m_api->m_deviceContext->OMSetDepthStencilState(enable ? m_api->m_depthStencilStateEnabled : m_api->m_depthStencilStateDisabled, 1);

	m_zBufferEnabled = enable;
}

bool Graphics::CreateDepthStencilBuffer()
{
	D3D11_TEXTURE2D_DESC depthBufferDesc;
	ZeroMemory(&depthBufferDesc, sizeof(depthBufferDesc));
	depthBufferDesc.Width = RESOLUTION_WIDTH;
	depthBufferDesc.Height = RESOLUTION_HEIGHT;
	depthBufferDesc.MipLevels = 1;
	depthBufferDesc.ArraySize = 1;
	depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthBufferDesc.SampleDesc.Count = 1;
	depthBufferDesc.SampleDesc.Quality = 0;
	depthBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	depthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depthBufferDesc.CPUAccessFlags = 0;
	depthBufferDesc.MiscFlags = 0;

	// Create the texture for the depth buffer using the filled out description.
	HRESULT hResult = m_api->m_device->CreateTexture2D(&depthBufferDesc, nullptr, &m_api->m_depthStencilBuffer);
	if (FAILED(hResult))
	{
		LOG_ERROR("Failed to create the texture for the depth buffer.");
		return false;
	}

	return true;
}

bool Graphics::CreateDepthStencil()
{
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
	ZeroMemory(&depthStencilDesc, sizeof(depthStencilDesc));

	// Depth test parameters
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;

	// Stencil test parameters
	depthStencilDesc.StencilEnable = true;
	depthStencilDesc.StencilReadMask = 0xFF;
	depthStencilDesc.StencilWriteMask = 0xFF;

	// Stencil operations if pixel is front-facing
	depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Stencil operations if pixel is back-facing
	depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Create a depth stencil state with depth enabled
	depthStencilDesc.DepthEnable = true;
	HRESULT hResult = m_api->m_device->CreateDepthStencilState(&depthStencilDesc, &m_api->m_depthStencilStateEnabled);
	if (FAILED(hResult))
	{
		LOG_ERROR("Failed to create depth stencil enabled state.");
		return false;
	}

	// Create a depth stencil state with depth disabled
	depthStencilDesc.DepthEnable = false;
	HRESULT result = m_api->m_device->CreateDepthStencilState(&depthStencilDesc, &m_api->m_depthStencilStateDisabled);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to create depth stencil disabled state.");
		return false;
	}

	// Set the default depth stencil state
	m_api->m_deviceContext->OMSetDepthStencilState(m_api->m_depthStencilStateEnabled, 1);

	return true;
}
//========================================================================================================================

void Graphics::Clear(const Vector4& color)
{
	float clearColor[4] = { color.x, color.y, color.z, color.w };

	// Clear the back buffer.
	m_api->m_deviceContext->ClearRenderTargetView(m_api->m_renderTargetView, clearColor);

	// Clear the depth buffer.
	m_api->m_deviceContext->ClearDepthStencilView(m_api->m_depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void Graphics::Present()
{
	m_api->m_swapChain->Present(VSYNC, 0);
}

void Graphics::EnableAlphaBlending(bool enable)
{
	if (m_alphaBlendingEnabled == enable)
		return;

	// Blend factor.
	float blendFactor[4];
	blendFactor[0] = 0.0f;
	blendFactor[1] = 0.0f;
	blendFactor[2] = 0.0f;
	blendFactor[3] = 0.0f;

	if (enable)
		m_api->m_deviceContext->OMSetBlendState(m_api->m_blendStateAlphaEnabled, blendFactor, 0xffffffff);
	else
		m_api->m_deviceContext->OMSetBlendState(m_api->m_blendStateAlphaDisabled, blendFactor, 0xffffffff);

	m_alphaBlendingEnabled = enable;
}

void Graphics::SetResolution(int width, int height)
{
	//Release old views and the old depth/stencil buffer.
	SafeRelease(m_api->m_renderTargetView);
	SafeRelease(m_api->m_depthStencilView);
	SafeRelease(m_api->m_depthStencilBuffer);

	//Resize the swap chain and recreate the render target views. 
	HRESULT result = m_api->m_swapChain->ResizeBuffers(1, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	if (FAILED(result))
		LOG_ERROR("Failed to resize swap chain buffers.");

	ID3D11Texture2D* backBuffer;
	result = m_api->m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)(&backBuffer));
	if (FAILED(result))
		LOG_ERROR("Failed to get pointer to the swap chain's back buffer.");

	result = m_api->m_device->CreateRenderTargetView(backBuffer, nullptr, &m_api->m_renderTargetView);
	if (FAILED(result))
		LOG_ERROR("Failed to create render target view.");

	SafeRelease(backBuffer);

	CreateDepthStencilBuffer();
	CreateDepthStencil();

	SetViewport(width, height);
}

void Graphics::SetViewport(int width, int height)
{
	m_api->m_viewport.Width = float(width);
	m_api->m_viewport.Height = float(height);
	m_api->m_viewport.MinDepth = 0.0f;
	m_api->m_viewport.MaxDepth = 1.0f;
	m_api->m_viewport.TopLeftX = 0.0f;
	m_api->m_viewport.TopLeftY = 0.0f;

	m_api->m_deviceContext->RSSetViewports(1, &m_api->m_viewport);
}

void Graphics::ResetViewport()
{
	m_api->m_deviceContext->RSSetViewports(1, &m_api->m_viewport);
}

void Graphics::SetCullMode(CullMode cullMode)
{
	// Set face CullMode only if not already set
	if (m_cullMode == cullMode)
		return;

	auto mode = d3dCullMode[cullMode];

	if (mode == D3D11_CULL_FRONT)
		m_api->m_deviceContext->RSSetState(m_api->m_rasterStateCullFront);
	else if (mode == D3D11_CULL_BACK)
		m_api->m_deviceContext->RSSetState(m_api->m_rasterStateCullBack);
	else if (mode == D3D11_CULL_NONE)
		m_api->m_deviceContext->RSSetState(m_api->m_rasterStateCullNone);

	// Save the current CullMode mode
	m_cullMode = cullMode;
}

void Graphics::SetBackBufferAsRenderTarget()
{
	m_api->m_deviceContext->OMSetRenderTargets(1, &m_api->m_renderTargetView, m_api->m_depthStencilView);
}

void Graphics::SetPrimitiveTopology(PrimitiveTopology primitiveTopology)
{
	// Set PrimitiveTopology only if not already set
	if (m_primitiveTopology == primitiveTopology)
		return;

	// Set PrimitiveTopology
	if (primitiveTopology == TriangleList)
		m_api->m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	else if (primitiveTopology == LineList)
		m_api->m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	// Save the current PrimitiveTopology mode
	m_primitiveTopology = primitiveTopology;
}
void Graphics::SetInputLayout(InputLayout inputLayout)
{
	if (m_inputLayout == inputLayout)
		return;

	m_inputLayout = inputLayout;
}