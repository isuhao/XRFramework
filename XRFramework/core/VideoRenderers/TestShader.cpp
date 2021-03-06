#include "stdafxf.h"
#include "TestShader.h"
#include "render/RenderSystemDX.h"
#include "filesystem/File.h"

TestShader::~TestShader()
{

}

bool TestShader::Create()
{
	if (!CreateVertexBuffer(8, sizeof(CUSTOMVERTEX), 2)) 
	{
		LOGFATAL("Failed to create Vertex Buffer. (No memory?)");
		return false;
	}

	if (!CreateIndexBuffer(5)) 
	{
		LOGFATAL("Failed to create Vertex Buffer. (No memory?)");
		return false;
	}

	if (!LoadEffect("special://app/data/test.fx", nullptr)) 
	{
		m_effect.Release();
		return false;
	}
	m_effect.SetTechnique("ColorTech");
	BuildVertexLayout();

	return true;
}

bool TestShader::Render()
{
	PrepareParameters();
	SetShaderParameters();
	Execute(NULL, 0);
	return true;
}

void TestShader::PrepareParameters()
{
	CUSTOMVERTEX* v;
	if (LockVertexBuffer((void**)&v)) 
	{
		v[0].position = XMFLOAT4(300.0f, 20.0f, 1.0f, 1.0f); //left top
		v[0].color = XMFLOAT4(0.5f, 0.5f, 1.0f, 1.0f);

		v[1].position = XMFLOAT4(600.0f, 20.0f, 1.0f, 1.0f); //right top
		v[1].color = XMFLOAT4(0.8f, 0.2f, 1.0f, 1.0f);

		v[2].position = XMFLOAT4(300.0f, 320.0f, 1.0f, 1.0f);//left bottom
		v[2].color = XMFLOAT4(0.1f, 0.9f, 1.0f, 1.0f);

		v[3].position = XMFLOAT4(600.0f, 320.0f, 1.0f, 1.0f); //right bottom
		v[3].color = XMFLOAT4(0.5f, 0.8f, 4.0f, 1.0f);

		v[4].position = XMFLOAT4(300.0f, 380.0f, 1.0f, 1.0f);
		v[4].color = XMFLOAT4(0.5f, 0.5f, 1.0f, 1.0f);
		v[5].position = XMFLOAT4(600.0f, 380.0f, 1.0f, 1.0f);
		v[5].color = XMFLOAT4(0.8f, 0.2f, 1.0f, 1.0f);
		v[6].position = XMFLOAT4(300.0f, 500.0f, 1.0f, 1.0f);
		v[6].color = XMFLOAT4(0.1f, 0.9f, 1.0f, 1.0f);
		v[7].position = XMFLOAT4(600.0f, 500.0f, 1.0f, 1.0f);
		v[7].color = XMFLOAT4(0.5f, 0.8f, 4.0f, 1.0f);


		UnlockVertexBuffer();
	}

	int* f;
	if (LockIndexBuffer((void**)&f)) 
	{
		f[0] = 3; 
		f[1] = 2;
		f[2] = 0;
		f[3] = 1;
		f[4] = 3;

		UnlockIndexBuffer();
	}
}

void TestShader::SetShaderParameters()
{
	m_effect.SetMatrix("gW", m_rendererSystem->m_world);
	m_effect.SetMatrix("gV", m_rendererSystem->m_view);
	m_effect.SetMatrix("gP", m_rendererSystem->m_projection);
}

bool TestShader::BuildVertexLayout()
{
	// Define the input layout
	D3D10_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D10_INPUT_PER_VERTEX_DATA, 0 },
	};
	UINT numElements = ARRAYSIZE(layout);

	D3D10_PASS_DESC pDesc;
	m_effect.GetTechnique()->GetPassByIndex(0)->GetDesc(&pDesc);

	ID3D10Device* pDevice = m_rendererSystem->GetDevice();
	HRESULT hr = pDevice->CreateInputLayout(layout, numElements, pDesc.pIAInputSignature, pDesc.IAInputSignatureSize, &m_inputLayout);
	if (SUCCEEDED(hr))
		return true;

	LOGERR("Failed to create input Layout.");
	SAFE_RELEASE(m_inputLayout);
	return false;
}
