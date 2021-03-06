#pragma once
#include <map>
#include <d3d10.h>
#include <DirectXMath.h>
#include <string>
#include <memory>
using namespace DirectX;

class CDX10SystemRenderer;

class ID3DResource
{
public:
	virtual ~ID3DResource() {};

	virtual void OnDestroyDevice() {};
	virtual void OnCreateDevice() {};
	virtual void OnFreeResources() {};
	virtual void OnResizeDevice() {};
};

/************************************************************************/
/*    D3DTexture                                                        */
/************************************************************************/
class D3DTexture : public ID3DResource
{
public:
	D3DTexture(CDX10SystemRenderer *sys);
	virtual ~D3DTexture();

	bool Create(UINT width, UINT height, UINT bindflag, D3D10_USAGE usage, DXGI_FORMAT format, UINT mipLevels = 1);
	void Release();

	bool Lock(UINT level, D3D10_MAP typeofmap, D3D10_MAPPED_TEXTURE2D* mappedtexture);
	bool Unlock(UINT level);

	ID3D10ShaderResourceView* Get() { return m_shaderResourceView; };
	ID3D10Texture2D* GetResource() { return m_resource; }
	UINT GetWidth()  const { return m_width; }
	UINT GetHeight() const { return m_height; }
	DXGI_FORMAT GetFormat() const { return m_format; }

	virtual void OnDestroyDevice() override;
private:
	UINT m_bindflag;
	// creation parameters
	UINT      m_width;
	UINT      m_height;
	UINT      m_mipLevels;
	DXGI_FORMAT m_format;

	ID3D10Texture2D* m_resource;
	ID3D10ShaderResourceView* m_shaderResourceView;
	ID3D10RenderTargetView* m_renderTargetView;
	
	bool m_bLocked;
	CDX10SystemRenderer *m_rendererSystem;
};

/************************************************************************/
/*    D3DEffect                                                         */
/************************************************************************/
typedef std::map<std::string, std::string> DefinesMap;

class D3DEffect : public ID3DResource
{
public:
	D3DEffect(CDX10SystemRenderer *m_rendererSystem);
	virtual ~D3DEffect();

	bool Create(const std::string &effectString, DefinesMap* defines);
	void Release();

	bool SetFloatArray(std::string handle, const float* val, unsigned int count);
	bool SetMatrix(std::string handle, const XMFLOAT4X4& mat);
	bool SetTechnique(std::string handle);
	bool SetTexture(std::string handle, D3DTexture &texture);

	//bool Begin(UINT *passes, DWORD flags);
	bool BeginPass(UINT pass);
	//bool EndPass();
	//bool End();
	ID3D10EffectTechnique* GetTechnique() { if (m_technique) return m_technique; return NULL; }
	ID3D10Effect* Get() { return m_effect; }

	virtual void OnDestroyDevice() override;

private:
	bool         CreateEffect();
	std::string  m_effectString;

	//! Effect (shader) used when rendering.
	ID3D10Effect* m_effect;
	ID3D10EffectTechnique* m_technique;
	DefinesMap   m_defines;
	CDX10SystemRenderer *m_rendererSystem;
};

/************************************************************************/
/*    D3DVertexBuffer                                                   */
/************************************************************************/
class D3DVertexBuffer : public ID3DResource
{
public:
	enum type {
		NONINDEXED_BUFFER,
		INDEXED_BUFFER
	};

	D3DVertexBuffer(CDX10SystemRenderer *sys);
	virtual ~D3DVertexBuffer();

	bool CreateBuffer(UINT length, D3D10_USAGE usage = D3D10_USAGE_DEFAULT, void* data = NULL);
	bool CreateIndex(UINT length, D3D10_USAGE usage = D3D10_USAGE_DEFAULT, void* data = NULL);
	void Release();

	bool LockBuffer(D3D10_MAP mapType, void **data);
	void UnlockBuffer();
	bool LockIndex(D3D10_MAP mapType, void **data);
	void UnlockIndex();

	ID3D10Buffer* GetVertexBuffer() { return m_vertexBuffer; };
	ID3D10Buffer* GetIndexBuffer() const { return m_indexBuffer; };

	virtual void OnDestroyDevice() override;

	type		m_type;

private:

	bool		m_bIsValid;
	UINT		m_length;
	ID3D10Buffer* m_indexBuffer;
	ID3D10Buffer* m_vertexBuffer;
	CDX10SystemRenderer *m_rendererSystem;
};
