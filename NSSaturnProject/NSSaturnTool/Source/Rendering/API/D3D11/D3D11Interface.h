#pragma once

#include "Rendering\API\GraphicsInterface.h"

struct ID3D11Buffer;
struct ID3D11DepthStencilView;
struct ID3D11Device1;
struct ID3D11DeviceContext1;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;
struct IDXGISwapChain1;
struct ID3D11VertexShader;
struct ID3D11InputLayout;
struct ID3D11PixelShader;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;
struct ID3D11RasterizerState1;
struct ID3D11DepthStencilState;

class D3D11VertexBuffer : public VertexBuffer
{
public:
	//Not currently supported because they're not needed
	D3D11VertexBuffer(const D3D11VertexBuffer&) = delete;
	D3D11VertexBuffer(const D3D11VertexBuffer&&) = delete;
	D3D11VertexBuffer& operator=(const D3D11VertexBuffer&) = delete;

	D3D11VertexBuffer(){}

	virtual ~D3D11VertexBuffer() override;
	virtual void SetVertices(const void* pInVertices, unsigned int InDataSize, unsigned int InVertexStride) override;
	virtual void ActivateVertexBuffer() override;

protected:
	ID3D11Buffer* mpVertexBuffer{nullptr};
	unsigned int mVertexStride{0};
};

class D3D11IndexBuffer : public IndexBuffer
{
public:
	//Not currently supported because they're not needed
	D3D11IndexBuffer(const D3D11IndexBuffer&) = delete;
	D3D11IndexBuffer(const D3D11IndexBuffer&&) = delete;
	D3D11IndexBuffer& operator=(const D3D11IndexBuffer&) = delete;

	D3D11IndexBuffer() {}

	virtual ~D3D11IndexBuffer() override;
	virtual bool SetIndices(const void* pInIndices, unsigned int InDataSize) override;
	virtual void ActivateIndexBuffer() override;

protected:
	ID3D11Buffer* mpIndexBuffer;
};

class D3D11ShaderResource : public ShaderResource
{
public:
	//Not currently supported because they're not needed
	D3D11ShaderResource(const D3D11ShaderResource&) = delete;
	D3D11ShaderResource(const D3D11ShaderResource&&) = delete;
	D3D11ShaderResource& operator=(const D3D11ShaderResource&) = delete;

	D3D11ShaderResource(){}
	virtual ~D3D11ShaderResource();

	virtual bool InitializeShader(const char* pFilePath, const InputLayoutDescription& InInputLayout, unsigned int InConstantsSize) override;
	virtual bool ReCompileShader() override;
	virtual void ActivateShader(void* pInConstantsData, unsigned int InConstantsSize) override;

private:
	ID3D11VertexShader* mpVertexShader{ nullptr };
	ID3D11PixelShader*  mpPixelShader{ nullptr };
	ID3D11SamplerState* mpSamplerState{ nullptr };
	ID3D11InputLayout* mpInputLayout{ nullptr };
	ID3D11Buffer* mpVertexConstantBuffer{ nullptr };
	std::string mShaderFilePath;
};

class D3D2DTextureResource : public TextureResource
{
public:
	//Not currently supported because they're not needed
	D3D2DTextureResource(const D3D2DTextureResource&) = delete;
	D3D2DTextureResource(const D3D2DTextureResource&&) = delete;
	D3D2DTextureResource& operator=(const D3D2DTextureResource&) = delete;

	D3D2DTextureResource(){}

	virtual ~D3D2DTextureResource();

	virtual bool CreateTexture(const void* pInImageData, unsigned int InImageWidth, unsigned int InImageHeight) override;
	virtual void UpdateTexture(const unsigned int* InImageData, unsigned int InWidth, unsigned int InHeight) override;
	virtual void ActivateTexture() override;
	virtual void GetPixels(void* pInBuffer, unsigned int InBufferSize) override;
	virtual unsigned int GetWidth() const {return mTextureWidth;}
	virtual unsigned int GetHeight() const {return mTextureHeight;}

	ID3D11Texture2D* GetD3DTexture() const {return mpTexture;}

private:
	void ReleaseOldResources();

	ID3D11Texture2D* mpTexture{ nullptr };
	ID3D11ShaderResourceView* mpShaderResource{ nullptr };
	unsigned int mTextureWidth{ 0 };
	unsigned int mTextureHeight{ 0 };
	unsigned int* mpImageData{ nullptr };
	unsigned int* mpModifiedImageData{ nullptr };
	bool mbOwnsImageData{ false };
};

class D3D11RenderTextureResource : public RenderTextureResource
{
public:
	//Not currently supported because they're not needed
	D3D11RenderTextureResource(const D3D11RenderTextureResource&) = delete;
	D3D11RenderTextureResource(const D3D11RenderTextureResource&&) = delete;
	D3D11RenderTextureResource& operator=(const D3D11RenderTextureResource&) = delete;

	D3D11RenderTextureResource(){}
	virtual ~D3D11RenderTextureResource();

	virtual bool CreateRenderTexture(unsigned int InImageWidth, unsigned int InImageHeight) override;
	virtual void ActivateRenderTexture() override;
	virtual void DeActivateRenderTexture() override;
	virtual void CopyToTexture(TextureResource* pInDestinationTexture) override;

private:
	ID3D11Texture2D* mpRenderTargetTexture{ nullptr };
	ID3D11RenderTargetView* mpRenderTargetView{ nullptr };
	ID3D11ShaderResourceView* mpShaderResourceView{ nullptr };
	ID3D11RenderTargetView* mpOldViewTarget{ nullptr };
	ID3D11DepthStencilView* mpOldDepthTarget{ nullptr };
};

class D3D11StagingTextureResource : public StagingTextureResource
{
public:
	virtual ~D3D11StagingTextureResource();

	virtual bool CreateStagingTexture(unsigned int InImageWidth, unsigned int InImageHeight) override;
	virtual void CopyTextureToStagingTexture(TextureResource& InTextureToCopy) override;
	virtual void CopyStagingTextureToTexture(TextureResource& InDestinationTexture) override;
	virtual char* LockPixels(unsigned int* pOutRowPitch) override;
	virtual void UnlockPixels() override;

protected:
	ID3D11Texture2D* mpStagingTexture = nullptr;
};

class D3D11Interface : public GraphicsInterface
{
public:
	static void CreateGraphicsInterface();
	static ID3D11Device1* GetDevice();
	static ID3D11DeviceContext1* GetDeviceContext();

	virtual ~D3D11Interface() override;

	virtual bool InitializeGraphics() override;
	virtual void OnRecreateResources(unsigned int InResizeWidth, unsigned int InResizeHeight) override;

	virtual void BeginScene() override;
	virtual void EndScene() override;

	virtual void BeginImGuiScene() override;
	virtual void EndImGuiScene() override;

	virtual void EnableDepthBuffer() override;
	virtual void DisableDepthBuffer() override;

	virtual float GetViewportWidth() const override {return mDepthBufferWidth;}
	virtual float GetViewportHeight() const override {return mDepthBufferHeight;}

	virtual void DrawIndexPrimitives(unsigned int InNumVertices) override;

	virtual ShaderResource* CreateShader() const override;
	virtual TextureResource* CreateTexture() const override;
	virtual VertexBuffer* CreateVertexBuffer() const override;
	virtual IndexBuffer* CreateIndexBuffer() const override;
	virtual RenderTextureResource* CreateRenderTexture() const override;
	virtual StagingTextureResource* CreateStagingTexture() const override;

protected:
	virtual void InitializeImGUI() override;

private:
	void CleanupRenderTarget();
	void CreateRenderTarget();
	void CleanupDeviceD3D();

private:
	ID3D11Device1*            mpD3DDevice{nullptr};
	ID3D11DeviceContext1*     mpD3DDeviceContext{ nullptr };
	IDXGISwapChain1*          mpSwapChange{nullptr};
	ID3D11RenderTargetView*   mpMainRenderTarget{nullptr};
	ID3D11Texture2D*          mpDepthBuffer{nullptr};
	ID3D11DepthStencilView*   mpDepthBufferView{nullptr};
	ID3D11RasterizerState1*   mpRasterizerState{nullptr};
	ID3D11DepthStencilState*  mpDepthStencilState{nullptr};
	ID3D11DepthStencilState*  mpDepthStencilStateDisable{nullptr};
	float                     mDepthBufferWidth{ 0.f };
	float                     mDepthBufferHeight{ 0.f };
};
