#pragma once

//Maps to D3DInputMapping
enum class EVertexInputType
{
	Type_Float,
	Type_UInt,
	Type_SInt,
	Type_Vector2,
	Type_Vector3,
	Type_Vector4,
	COUNT
};

typedef std::pair<EVertexInputType, std::string> InputLayoutEntry;
typedef std::vector<InputLayoutEntry> InputLayoutDescription;

class VertexBuffer
{
public:
	//Not currently supported because they're not needed
	VertexBuffer(const VertexBuffer&) = delete;
	VertexBuffer(const VertexBuffer&&) = delete;
	VertexBuffer& operator=(const VertexBuffer&) = delete;

	VertexBuffer(){}
	virtual ~VertexBuffer(){}

	virtual void SetVertices(const void* pInVertices, unsigned int InDataSize, unsigned int InVertexStride) = 0;
	virtual void ActivateVertexBuffer() = 0;
};

class IndexBuffer
{
public:
	//Not currently supported because they're not needed
	IndexBuffer(const IndexBuffer&) = delete;
	IndexBuffer(const IndexBuffer&&) = delete;
	IndexBuffer& operator=(const IndexBuffer&) = delete;

	IndexBuffer(){}

	virtual ~IndexBuffer()
	{
	}

	virtual bool SetIndices(const void* pInIndices, unsigned int InDataSize) = 0;
	virtual void ActivateIndexBuffer() = 0;
};

class ShaderResource
{
public:
	//Not currently supported because they're not needed
	ShaderResource(const ShaderResource&) = delete;
	ShaderResource(const ShaderResource&&) = delete;
	ShaderResource& operator=(const ShaderResource&) = delete;

	ShaderResource(){}
	virtual ~ShaderResource() {}

	virtual bool InitializeShader(const char* pFilePath, const InputLayoutDescription& InInputLayout, unsigned int InConstantsSize) = 0;
	virtual bool ReCompileShader() = 0;
	virtual void ActivateShader(void* pInConstantsData, unsigned int InConstantsSize) = 0;
};

class TextureResource
{
public:
	//Not currently supported because they're not needed
	TextureResource(const TextureResource&) = delete;
	TextureResource(const TextureResource&&) = delete;
	TextureResource& operator=(const TextureResource&) = delete;

	TextureResource(){}
	virtual ~TextureResource() {}

	virtual bool CreateTexture(const void* pInImageData, unsigned int InImageWidth, unsigned int InImageHeight) = 0;
	virtual void UpdateTexture(const unsigned int* InImageData, unsigned int InWidth, unsigned int InHeight) = 0;
	virtual void ActivateTexture() = 0;
	virtual void GetPixels(void* pInBuffer, unsigned int InBufferSize) = 0;
	virtual unsigned int GetWidth() const = 0;
	virtual unsigned int GetHeight() const = 0;
};

class RenderTextureResource
{
public:
	//Not currently supported because they're not needed
	RenderTextureResource(const RenderTextureResource&) = delete;
	RenderTextureResource(const RenderTextureResource&&) = delete;
	RenderTextureResource& operator=(const RenderTextureResource&) = delete;

	RenderTextureResource() {}
	virtual ~RenderTextureResource() {}

	virtual bool CreateRenderTexture(unsigned int InImageWidth, unsigned int InImageHeight) = 0;
	virtual void ActivateRenderTexture() = 0;
	virtual void DeActivateRenderTexture() = 0;
	virtual void CopyToTexture(TextureResource* pInDestinationTexture) = 0;
};

class StagingTextureResource
{
public:
	//Not currently supported because they're not needed
	StagingTextureResource(const StagingTextureResource&) = delete;
	StagingTextureResource(const StagingTextureResource&&) = delete;
	StagingTextureResource& operator=(const StagingTextureResource&) = delete;

	StagingTextureResource(){}
	virtual ~StagingTextureResource(){}

	virtual bool CreateStagingTexture(unsigned int InImageWidth, unsigned int InImageHeight) = 0 {mTextureWidth = InImageWidth; mTextureHeight = InImageHeight; return true;}
	virtual void CopyTextureToStagingTexture(TextureResource& InTextureToCopy) = 0;
	virtual void CopyStagingTextureToTexture(TextureResource& InDestinationTexture) = 0;
	virtual char* LockPixels(unsigned int* pOutRowPitch) = 0;
	virtual void UnlockPixels() = 0;
	virtual unsigned int GetTextureWidth() const {return mTextureWidth;}
	virtual unsigned int GetTextureHeight() const {return mTextureHeight;}

protected:
	unsigned int mTextureWidth{0};
	unsigned int mTextureHeight{0};
};

class GraphicsInterface
{
public:
	virtual ~GraphicsInterface() {}

	virtual bool InitializeGraphics() = 0;
	virtual void OnRecreateResources(unsigned int /*InResizeWidth*/, unsigned int /*InResizeHeight*/) {};

	virtual void BeginScene() = 0;
	virtual void EndScene() = 0;

	virtual void BeginImGuiScene() = 0;
	virtual void EndImGuiScene() = 0;

	virtual void EnableDepthBuffer() = 0;
	virtual void DisableDepthBuffer() = 0;

	virtual float GetViewportWidth() const = 0;
	virtual float GetViewportHeight() const = 0;

	virtual void DrawIndexPrimitives(unsigned int InNumVertices) = 0;

	virtual ShaderResource* CreateShader() const = 0;
	virtual TextureResource* CreateTexture() const = 0;
	virtual VertexBuffer* CreateVertexBuffer() const = 0;
	virtual IndexBuffer* CreateIndexBuffer() const = 0;
	virtual RenderTextureResource* CreateRenderTexture() const = 0;
	virtual StagingTextureResource* CreateStagingTexture() const = 0;

protected:
	virtual void InitializeImGUI() {}
};

extern GraphicsInterface* GGraphicsInterface;
