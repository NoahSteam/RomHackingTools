#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "Utils\Utils.h"

#include "Rendering\API\D3D11\D3D11Interface.h"

using std::string;
using std::vector;

extern HWND GWindowHandle;
GraphicsInterface* GGraphicsInterface = nullptr;
static D3D11Interface* GD3DInterface = nullptr;

////////////////////////////
//     D3D11Interface     //
////////////////////////////

//Static function
void D3D11Interface::CreateGraphicsInterface()
{
	assert(GGraphicsInterface == nullptr);
	if(GGraphicsInterface)
	{
		return;
	}

	GD3DInterface = new D3D11Interface();
	GGraphicsInterface = GD3DInterface;
	GGraphicsInterface->InitializeGraphics();
}

//Static function
ID3D11Device1* D3D11Interface::GetDevice()
{
	return GD3DInterface->mpD3DDevice;
}

//Static function
ID3D11DeviceContext1* D3D11Interface::GetDeviceContext()
{
	return GD3DInterface->mpD3DDeviceContext;
}

D3D11Interface::~D3D11Interface()
{

}

bool D3D11Interface::InitializeGraphics()
{
	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
	ID3D11Device* baseDevice;
	ID3D11DeviceContext* baseDeviceContext;
	D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_DEBUG, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &baseDevice, nullptr, &baseDeviceContext);

	baseDevice->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&mpD3DDevice));
	
	baseDeviceContext->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&mpD3DDeviceContext));
	
	IDXGIDevice1* dxgiDevice;
	mpD3DDevice->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice));

	IDXGIAdapter* dxgiAdapter;
	dxgiDevice->GetAdapter(&dxgiAdapter);

	IDXGIFactory2* dxgiFactory;
	dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory));

	//Swap Chain
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
	swapChainDesc.Width              = 0; // use window width
	swapChainDesc.Height             = 0; // use window height
	swapChainDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	swapChainDesc.Stereo             = FALSE;
	swapChainDesc.SampleDesc.Count   = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount        = 2;
	swapChainDesc.Scaling            = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD; // prefer DXGI_SWAP_EFFECT_FLIP_DISCARD, see Minimal D3D11 pt2 
	swapChainDesc.AlphaMode          = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapChainDesc.Flags              = 0;
	dxgiFactory->CreateSwapChainForHwnd(mpD3DDevice, GWindowHandle, &swapChainDesc, nullptr, nullptr, &mpSwapChange);

	//Main frame buffer
	ID3D11Texture2D* frameBuffer;
	mpSwapChange->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&frameBuffer));
	mpD3DDevice->CreateRenderTargetView(frameBuffer, nullptr, &mpMainRenderTarget);

	//Create depth buffer
	D3D11_TEXTURE2D_DESC depthBufferDesc;
	frameBuffer->GetDesc(&depthBufferDesc);
	depthBufferDesc.Format    = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	mDepthBufferWidth = (float)depthBufferDesc.Width;
	mDepthBufferHeight= (float)depthBufferDesc.Height;

	//Depth Buffer
	mpD3DDevice->CreateTexture2D(&depthBufferDesc, nullptr, &mpDepthBuffer);
	mpD3DDevice->CreateDepthStencilView(mpDepthBuffer, nullptr, &mpDepthBufferView);

	//Raster state
	D3D11_RASTERIZER_DESC1 rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	mpD3DDevice->CreateRasterizerState1(&rasterizerDesc, &mpRasterizerState);

	// Create a depth stencil state object
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
	ZeroMemory(&depthStencilDesc, sizeof(depthStencilDesc));
	depthStencilDesc.DepthEnable               = true;
	depthStencilDesc.DepthWriteMask            = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc                 = D3D11_COMPARISON_LESS_EQUAL;
	mpD3DDevice->CreateDepthStencilState(&depthStencilDesc, &mpDepthStencilState);

	// Create a depth stencil state object that disables depth testing and depth writes
	depthStencilDesc;
	ZeroMemory(&depthStencilDesc, sizeof(depthStencilDesc));
	depthStencilDesc.DepthEnable = false;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.StencilEnable = false;
	mpD3DDevice->CreateDepthStencilState(&depthStencilDesc, &mpDepthStencilStateDisable);

	InitializeImGUI();

	return true;
}

void D3D11Interface::InitializeImGUI()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(GWindowHandle);
	ImGui_ImplDX11_Init(mpD3DDevice, mpD3DDeviceContext);
}

void D3D11Interface::OnRecreateResources(unsigned int InResizeWidth, unsigned int InResizeHeight)
{
	CleanupRenderTarget();
	mpSwapChange->ResizeBuffers(0, InResizeWidth, InResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
	CreateRenderTarget();
}

void D3D11Interface::CleanupRenderTarget()
{
	if (mpMainRenderTarget)
	{
		mpMainRenderTarget->Release();
		mpMainRenderTarget = nullptr;
	}
}

void D3D11Interface::CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (mpSwapChange)
	{ 
		mpSwapChange->Release(); 
		mpSwapChange = nullptr; 
	}

	if(mpDepthStencilState)
	{
		mpDepthStencilState->Release();
		mpDepthStencilState = nullptr;
	}

	if(mpDepthStencilStateDisable)
	{
		mpDepthStencilStateDisable->Release();
		mpDepthStencilStateDisable = nullptr;
	}

	if (mpD3DDeviceContext)
	{
		mpD3DDeviceContext->Release(); 
		mpD3DDeviceContext = nullptr; 
	}

	if (mpD3DDevice)
	{
		mpD3DDevice->Release(); 
		mpD3DDevice = nullptr;
	}
}

void D3D11Interface::CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer;
	mpSwapChange->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	mpD3DDevice->CreateRenderTargetView(pBackBuffer, nullptr, &mpMainRenderTarget);
	pBackBuffer->Release();
}

void D3D11Interface::BeginScene()
{
	const D3D11_VIEWPORT viewport = { 0.0f, 0.0f, mDepthBufferWidth, mDepthBufferHeight, 0.0f, 1.0f };
	const float backgroundColor[4] = { 0.025f, 0.025f, 0.025f, 1.0f };

	mpD3DDeviceContext->ClearRenderTargetView(mpMainRenderTarget, backgroundColor);
	mpD3DDeviceContext->ClearDepthStencilView(mpDepthBufferView, D3D11_CLEAR_DEPTH, 1.0f, 0);
	mpD3DDeviceContext->RSSetViewports(1, &viewport);
	mpD3DDeviceContext->OMSetRenderTargets(1, &mpMainRenderTarget, mpDepthBufferView);
	mpD3DDeviceContext->RSSetState(mpRasterizerState);
}

void D3D11Interface::EndScene()
{
	mpSwapChange->Present(1, 0);
}

void D3D11Interface::EnableDepthBuffer()
{
	mpD3DDeviceContext->OMSetDepthStencilState(mpDepthStencilState, 0);
}

void D3D11Interface::DisableDepthBuffer()
{
	mpD3DDeviceContext->OMSetDepthStencilState(mpDepthStencilStateDisable, 0);
}

void D3D11Interface::BeginImGuiScene()
{
	// Start the Dear ImGui frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
}

void D3D11Interface::EndImGuiScene()
{
	// Disable depth testing
	DisableDepthBuffer();

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	// Enable depth testing again
	EnableDepthBuffer();
}

void D3D11Interface::DrawIndexPrimitives(unsigned int InNumVertices)
{
	mpD3DDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mpD3DDeviceContext->DrawIndexed(InNumVertices, 0, 0);
}

ShaderResource* D3D11Interface::CreateShader() const
{
	return new D3D11ShaderResource();
}

TextureResource* D3D11Interface::CreateTexture() const
{
	return new D3D2DTextureResource();
}

RenderTextureResource* D3D11Interface::CreateRenderTexture() const
{
	return new D3D11RenderTextureResource();
}

VertexBuffer* D3D11Interface::CreateVertexBuffer() const
{
	return new D3D11VertexBuffer;
}

IndexBuffer* D3D11Interface::CreateIndexBuffer() const
{
	return new D3D11IndexBuffer;
}

StagingTextureResource* D3D11Interface::CreateStagingTexture() const
{
	return new D3D11StagingTextureResource;
}

///////////////////////////////
//     D3D11VertexBuffer     //
///////////////////////////////
D3D11VertexBuffer::~D3D11VertexBuffer()
{
	if(mpVertexBuffer)
	{
		mpVertexBuffer->Release();
		mpVertexBuffer = nullptr;
	}
}

void D3D11VertexBuffer::SetVertices(const void* pInVertices, unsigned int InDataSize, unsigned int InVertexStride)
{
	mVertexStride = InVertexStride;

	// Create the vertex buffer
	D3D11_BUFFER_DESC vertexBufferDesc = {};
	vertexBufferDesc.Usage             = D3D11_USAGE_IMMUTABLE;
	vertexBufferDesc.ByteWidth         = InDataSize;
	vertexBufferDesc.BindFlags         = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags    = 0;
	vertexBufferDesc.MiscFlags         = 0;

	D3D11_SUBRESOURCE_DATA vertexData = { pInVertices };

	ID3D11Device1* pD3DDevice = GD3DInterface->GetDevice();
	HRESULT hr = pD3DDevice->CreateBuffer(&vertexBufferDesc, &vertexData, &mpVertexBuffer);
	if (FAILED(hr))
	{
		printf("Unable to create vertex buffer for slices\n");
		return;
	}
}

void D3D11VertexBuffer::ActivateVertexBuffer()
{
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();

	unsigned int offset = 0;
	pD3DDeviceContext->IASetVertexBuffers(0, 1, &mpVertexBuffer, &mVertexStride, &offset);
}

//////////////////////////////
//     D3D11IndexBuffer     //
//////////////////////////////
D3D11IndexBuffer::~D3D11IndexBuffer()
{
	if(mpIndexBuffer)
	{
		mpIndexBuffer->Release();
		mpIndexBuffer = nullptr;
	}
}

bool D3D11IndexBuffer::SetIndices(const void* pInIndices, unsigned int InDataSize)
{
	ID3D11Device1* pD3DDevice = GD3DInterface->GetDevice();

	if(!mpIndexBuffer)
	{
		// Create the index buffer
		D3D11_BUFFER_DESC indexBufferDesc = {};
		indexBufferDesc.Usage             = D3D11_USAGE_DEFAULT;
		indexBufferDesc.ByteWidth         = InDataSize;
		indexBufferDesc.BindFlags         = D3D11_BIND_INDEX_BUFFER;
		indexBufferDesc.CPUAccessFlags    = 0;
		indexBufferDesc.MiscFlags         = 0;

		D3D11_SUBRESOURCE_DATA indexData = { pInIndices };
		HRESULT hr = pD3DDevice->CreateBuffer(&indexBufferDesc, &indexData, &mpIndexBuffer);
		if (FAILED(hr))
		{
			printf("Unable to create index buffer\n");
			return false;
		}
	}

	return mpIndexBuffer != nullptr;
}

void D3D11IndexBuffer::ActivateIndexBuffer()
{
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();
	pD3DDeviceContext->IASetIndexBuffer(mpIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
}

/////////////////////////////////
//     D3D11ShaderResource     //
/////////////////////////////////
static DXGI_FORMAT D3DInputMapping[(int)EVertexInputType::COUNT] =
{
	DXGI_FORMAT_R32_FLOAT,         //Type_Float,
	DXGI_FORMAT_R32_UINT,          //Type_UInt,
	DXGI_FORMAT_R32_SINT,          //Type_SInt,
	DXGI_FORMAT_R32G32_FLOAT,      //Type_Vector2,
	DXGI_FORMAT_R32G32B32_FLOAT,   //Type_Vector3,
	DXGI_FORMAT_R32G32B32A32_FLOAT,//Type_Vector4,
};

D3D11ShaderResource::~D3D11ShaderResource()
{
	if(mpVertexShader)
	{
		mpVertexShader->Release();
		mpVertexShader = nullptr;
	}

	if(mpPixelShader)
	{
		mpPixelShader->Release();
		mpPixelShader = nullptr;
	}

	if(mpSamplerState)
	{
		mpSamplerState->Release();
		mpPixelShader = nullptr;
	}

	if(mpInputLayout)
	{
		mpInputLayout->Release();
		mpInputLayout = nullptr;
	}

	if(mpVertexConstantBuffer)
	{
		mpVertexConstantBuffer->Release();
		mpVertexConstantBuffer = nullptr;
	}

}

bool D3D11ShaderResource::InitializeShader(const char* pInFilePath, const InputLayoutDescription& InInputLayout, const unsigned int InConstantsSize)
{
	ID3D11Device1* pD3DDevice = GD3DInterface->GetDevice();

	//Convert pFilePath to wchar
	CharToWChar filePath(pInFilePath);

	//Initialize vertex shader
	{
		ID3DBlob* vsBlob;
		ID3DBlob* errorBlob;
		HRESULT hr = D3DCompileFromFile(filePath.pWCharBuffer, nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
		if (FAILED(hr))
		{
			const char* pError = (const char*)errorBlob->GetBufferPointer();
			printf("Error: %s\n", pError);

			return false;
		}

		hr = pD3DDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &mpVertexShader);
		if (FAILED(hr))
		{
			printf("Error: Unable to vertex shader %s\n", pInFilePath);
			return false;
		}
	
		// Create input layout
		{
			unsigned int entryOffset = 0;
			vector<D3D11_INPUT_ELEMENT_DESC> d3dInputDescription;
			for (const InputLayoutEntry& entry : InInputLayout)
			{
				const DXGI_FORMAT format = D3DInputMapping[(int)entry.first];
				D3D11_INPUT_ELEMENT_DESC desc = { entry.second.c_str(), 0, format, 0, entryOffset, D3D11_INPUT_PER_VERTEX_DATA, 0 };
				d3dInputDescription.push_back(desc);
				entryOffset = D3D11_APPEND_ALIGNED_ELEMENT;
			}

			const unsigned int numEntries = (unsigned int)d3dInputDescription.size();
			hr = pD3DDevice->CreateInputLayout(d3dInputDescription.data(), numEntries, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &mpInputLayout);
			if (FAILED(hr))
			{
				printf("Error: Unable to input layout for %s\n", pInFilePath);
				return false;
			}
		}
	}

	//Pixel Shader
	{
		ID3DBlob* psBlob;
		ID3DBlob* errorBlob;
		HRESULT hr = D3DCompileFromFile(filePath.pWCharBuffer, nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
		if (FAILED(hr))
		{
			const char* pError = (const char*)errorBlob->GetBufferPointer();
			printf("Error: %s\n", pError);

			return false;
		}
		hr = pD3DDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &mpPixelShader);
		if (FAILED(hr))
		{
			printf("Error: Unable to create pixel shader %s\n", pInFilePath);
			return false;
		}
	}

	//Constants
	{
		D3D11_BUFFER_DESC constantBufferDesc = {};
		constantBufferDesc.ByteWidth         = InConstantsSize + 0xf & 0xfffffff0; // round constant buffer size to 16 byte boundary
		constantBufferDesc.Usage             = D3D11_USAGE_DYNAMIC;
		constantBufferDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		constantBufferDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = pD3DDevice->CreateBuffer(&constantBufferDesc, nullptr, &mpVertexConstantBuffer);
		if (FAILED(hr))
		{
			printf("Error: Unable to create constants for shader %s\n", pInFilePath);
			return false;
		}
	}

	//Sampler
	{

		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.ComparisonFunc     = D3D11_COMPARISON_NEVER;

		HRESULT hr = pD3DDevice->CreateSamplerState(&samplerDesc, &mpSamplerState);
		if(FAILED(hr))
		{
			printf("Error: Unable to create sampler state for %s\n", pInFilePath);
			return false;
		}
	}

	return true;
}

bool D3D11ShaderResource::ReCompileShader()
{
	return true;
}

struct RenderConstantBufferData
{
	DirectX::XMMATRIX ViewProjectMatrix;
	float OffsetX;
	float OffsetY;
};

void D3D11ShaderResource::ActivateShader(void* pInConstantsData, unsigned int InConstantsSize)
{
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();

	//Update constants
	if(pInConstantsData)
	{
		D3D11_MAPPED_SUBRESOURCE mappedSubresource;
		pD3DDeviceContext->Map(mpVertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
		{
			memcpy_s(mappedSubresource.pData, InConstantsSize, pInConstantsData, InConstantsSize);
		}
		pD3DDeviceContext->Unmap(mpVertexConstantBuffer, 0);
	}

	pD3DDeviceContext->VSSetConstantBuffers(0, 1, &mpVertexConstantBuffer);
	pD3DDeviceContext->PSSetConstantBuffers(0, 1, &mpVertexConstantBuffer);
	pD3DDeviceContext->IASetInputLayout(mpInputLayout);
	pD3DDeviceContext->VSSetShader(mpVertexShader, nullptr, 0);
	pD3DDeviceContext->PSSetShader(mpPixelShader, nullptr, 0);
	pD3DDeviceContext->PSSetSamplers(0, 1, &mpSamplerState);
}

//////////////////////////////////
//     D3D2DTextureResource     //
//////////////////////////////////
D3D2DTextureResource::~D3D2DTextureResource()
{
	ReleaseOldResources();
}

void D3D2DTextureResource::ReleaseOldResources()
{
	if (mbOwnsImageData)
	{
		delete[] mpImageData;
		mpImageData = nullptr;
	}

	if (mpModifiedImageData)
	{
		delete[] mpModifiedImageData;
		mpModifiedImageData = nullptr;
	}
}

bool D3D2DTextureResource::CreateTexture(const void* pInImageData, unsigned int InImageWidth, unsigned int InImageHeight)
{
	ID3D11Device1* pD3DDevice = GD3DInterface->GetDevice();

	ReleaseOldResources();
	
	mTextureWidth  = InImageWidth;
	mTextureHeight = InImageHeight;

	// Create texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width            = InImageWidth;
	desc.Height           = InImageHeight;
	desc.MipLevels        = 1;
	desc.ArraySize        = 1;
	desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage            = D3D11_USAGE_DEFAULT;
	desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags   = 0;

	mpTexture = nullptr;
	D3D11_SUBRESOURCE_DATA subResource;
	subResource.pSysMem          = pInImageData;
	subResource.SysMemPitch      = InImageWidth * sizeof(uint32_t);
	subResource.SysMemSlicePitch = 0;
	HRESULT hr = pD3DDevice->CreateTexture2D(&desc, &subResource, &mpTexture);
	if (FAILED(hr) || !mpTexture)
	{
		printf("Error: Unable to create texture\n");
		return false;
	}

	// Create texture view
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format                    = desc.Format;
	srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels       = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	hr = pD3DDevice->CreateShaderResourceView(mpTexture, &srvDesc, &mpShaderResource);
	if (FAILED(hr))
	{
		printf("Error: Unable to create texture resource\n");
		mpTexture->Release();
		return false;
	}

	return true;
}

void D3D2DTextureResource::UpdateTexture(const unsigned int* InImageData, unsigned int InWidth, unsigned int InHeight)
{
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();

	D3D11_BOX destRegion;
	destRegion.left   = 0;
	destRegion.right  = InWidth;
	destRegion.top    = 0;
	destRegion.bottom = InHeight;
	destRegion.front  = 0;
	destRegion.back   = 1;

	// Update the texture data
	pD3DDeviceContext->UpdateSubresource(mpTexture, 0, &destRegion, InImageData, InWidth * 4, 0);
}

void D3D2DTextureResource::ActivateTexture()
{
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();
	pD3DDeviceContext->PSSetShaderResources(0, 1, &mpShaderResource);
}

void D3D2DTextureResource::GetPixels(void* pInBuffer, unsigned int InBufferSize)
{
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	pD3DDeviceContext->Map(mpTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
	
	// Access the pixel data through the mappedResource.pData pointer
	unsigned char* pPixels = static_cast<unsigned char*>(mappedResource.pData);
	unsigned int pixelDataSize = mappedResource.RowPitch * mTextureHeight;
	memcpy_s(pInBuffer, InBufferSize, pPixels, pixelDataSize);
	// Process the pixel data as needed
	
	pD3DDeviceContext->Unmap(mpTexture, 0);
}

////////////////////////////////////////
//     D3D11RenderTextureResource     //
////////////////////////////////////////
D3D11RenderTextureResource::~D3D11RenderTextureResource()
{
	if(mpRenderTargetTexture)
	{
		mpRenderTargetTexture->Release();
		mpRenderTargetTexture = nullptr;
	}

	if(mpRenderTargetView)
	{
		mpRenderTargetView->Release();
		mpRenderTargetView = nullptr;
	}

	if(mpShaderResourceView)
	{
		mpShaderResourceView->Release();
		mpShaderResourceView = nullptr;
	}
}

bool D3D11RenderTextureResource::CreateRenderTexture(unsigned int InImageWidth, unsigned int InImageHeight)
{
	ID3D11Device1* pD3DDevice = GD3DInterface->GetDevice();

	/// <summary>
	//D3D11_SUBRESOURCE_DATA subResource;
	//subResource.pSysMem          = pInImageData;
//	subResource.SysMemPitch      = InImageWidth * sizeof(uint32_t);
//	subResource.SysMemSlicePitch = 0;
	/// </summary>
	/// <param name="InImageWidth"></param>
	/// <param name="InImageHeight"></param>
	/// <returns></returns>
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width              = InImageWidth;  // Width of the texture
	textureDesc.Height             = InImageHeight;  // Height of the texture
	textureDesc.MipLevels          = 1;  // Mipmap levels, 1 for a single texture
	textureDesc.ArraySize          = 1;  // The number of textures in the texture array
	textureDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;  // Pixel format of the texture
	textureDesc.SampleDesc.Count   = 1;  // The number of multisamples per pixel
	textureDesc.SampleDesc.Quality = 0;  // The image quality level
	textureDesc.Usage              = D3D11_USAGE_DEFAULT;  // How the texture is to be read from and written to
	textureDesc.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;  // Where the resource will be bind to
	textureDesc.CPUAccessFlags     = 0;  // No CPU access required
	textureDesc.MiscFlags          = 0;  // No additional flags

	HRESULT hr = pD3DDevice->CreateTexture2D(&textureDesc, nullptr, &mpRenderTargetTexture);
	if (FAILED(hr)) 
	{
		printf("Unable to create RenderTexture\n");
		return false;
	}

	//Create render target view
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format             = textureDesc.Format;  // Must be the same as the texture's format
	rtvDesc.ViewDimension      = D3D11_RTV_DIMENSION_TEXTURE2D;  // The resource type of the render target
	rtvDesc.Texture2D.MipSlice = 0;  // The index of the mipmap level to use

	hr = pD3DDevice->CreateRenderTargetView(mpRenderTargetTexture, &rtvDesc, &mpRenderTargetView);
	if (FAILED(hr)) 
	{
		printf("Unable to create renderTargetView\n");
		return false;
	}

	//Create shader view
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format                    = textureDesc.Format;  // Must be the same as the texture's format
	srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;  // The resource type of the shader resource
	srvDesc.Texture2D.MostDetailedMip = 0;  // The index of the most detailed mipmap level to use
	srvDesc.Texture2D.MipLevels       = 1;  // The maximum number of mipmap levels for the view

	hr = pD3DDevice->CreateShaderResourceView(mpRenderTargetTexture, &srvDesc, &mpShaderResourceView);
	if (FAILED(hr)) 
	{
		printf("Unable to create shaderResourceView\n");
		return false;
	}

	return true;
}

static D3D11_VIEWPORT oldViewport[1];

void D3D11RenderTextureResource::ActivateRenderTexture()
{
	const float backgroundColor[4] = { 0.025f, 0.025f, 0.025f, 1.0f };
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();
	pD3DDeviceContext->OMGetRenderTargets(1, &mpOldViewTarget, &mpOldDepthTarget);
	pD3DDeviceContext->OMSetRenderTargets(1, &mpRenderTargetView, nullptr);
	pD3DDeviceContext->ClearRenderTargetView(mpRenderTargetView, backgroundColor);

	UINT numViewports = 1;

	// Get the old viewport
	pD3DDeviceContext->RSGetViewports(&numViewports, oldViewport);

	// Set up the new viewport
	D3D11_VIEWPORT newViewport;
	newViewport.TopLeftX = 0;
	newViewport.TopLeftY = 0;
	newViewport.Width    = (float)810;//560;
	newViewport.Height   = (float)789;//560;
	newViewport.MinDepth = 0.0f;
	newViewport.MaxDepth = 1.0f;

	pD3DDeviceContext->RSSetViewports(1, &newViewport);

}

void D3D11RenderTextureResource::DeActivateRenderTexture()
{
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();

	if(mpOldViewTarget)
	{	
		pD3DDeviceContext->OMSetRenderTargets(1, &mpOldViewTarget, mpOldDepthTarget);
	}	

	pD3DDeviceContext->RSSetViewports(1, oldViewport);
}

void D3D11RenderTextureResource::CopyToTexture(TextureResource* pInDestinationTexture)
{
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();
	D3D2DTextureResource* pDestinationTexture = static_cast<D3D2DTextureResource*>(pInDestinationTexture);

	pD3DDeviceContext->CopyResource(pDestinationTexture->GetD3DTexture(), mpRenderTargetTexture);
}

/////////////////////////////////////////
//     D3D11StagingTextureResource     //
/////////////////////////////////////////
D3D11StagingTextureResource::~D3D11StagingTextureResource()
{
	if(mpStagingTexture)
	{
		mpStagingTexture->Release();
		mpStagingTexture = nullptr;
	}
}

bool D3D11StagingTextureResource::CreateStagingTexture(unsigned int InImageWidth, unsigned int InImageHeight)
{
	StagingTextureResource::CreateStagingTexture(InImageWidth, InImageHeight);

	D3D11_TEXTURE2D_DESC stagingDesc = {};
	stagingDesc.Width            = InImageWidth;
	stagingDesc.Height           = InImageHeight;
	stagingDesc.MipLevels        = 1;
	stagingDesc.ArraySize        = 1;
	stagingDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM; // Choose the appropriate format
	stagingDesc.SampleDesc.Count = 1;
	stagingDesc.Usage            = D3D11_USAGE_STAGING;
	stagingDesc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ; // Enable CPU access for reading

	ID3D11Device1* pD3DDevice = GD3DInterface->GetDevice();
	const HRESULT hr = pD3DDevice->CreateTexture2D(&stagingDesc, nullptr, &mpStagingTexture);
	if (FAILED(hr))
	{
		printf("Unable to create staging texture\n");
		return false;
	}

	return true;
}

void D3D11StagingTextureResource::CopyTextureToStagingTexture(TextureResource& InTextureToCopy)
{
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();

	pD3DDeviceContext->CopyResource(mpStagingTexture, ((D3D2DTextureResource*)&InTextureToCopy)->GetD3DTexture());
}

void D3D11StagingTextureResource::CopyStagingTextureToTexture(TextureResource& InDestinationTexture)
{
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();

	pD3DDeviceContext->CopyResource(((D3D2DTextureResource*)&InDestinationTexture)->GetD3DTexture(), mpStagingTexture);
}

char* D3D11StagingTextureResource::LockPixels(unsigned int* pOutRowPitch)
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();
	HRESULT hr = pD3DDeviceContext->Map(mpStagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);

	if (FAILED(hr))
	{	
		return nullptr;
	}

	if(pOutRowPitch)
	{
		*pOutRowPitch = mappedResource.RowPitch;
	}

	return (char*)mappedResource.pData;
}

void D3D11StagingTextureResource::UnlockPixels()
{
	ID3D11DeviceContext1* pD3DDeviceContext = GD3DInterface->GetDeviceContext();
	pD3DDeviceContext->Unmap(mpStagingTexture, 0);
}
