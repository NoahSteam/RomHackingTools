#pragma once

#if 0
class GGFSShaderResource
{
public:
	bool InitializeShader(const WCHAR* pInShaderFile)
	{
		//Vertex shader
		{
			ID3DBlob* vsBlob;
			ID3DBlob* errorBlob;
			HRESULT hr = D3DCompileFromFile(pInShaderFile, nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
			if (FAILED(hr))
			{
				const char* pError = (const char*)errorBlob->GetBufferPointer();
				printf("Error: %s", pError);

				return false;
			}

			hr = g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &mpVertexShader);
			if (FAILED(hr))
			{
				return false;
			}

			D3D11_INPUT_ELEMENT_DESC inputElementDesc[] = // float3 position, float3 normal, float2 texcoord, float3 color
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,                            0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "SV_VertexID", 0, DXGI_FORMAT_R32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};

			hr = g_pd3dDevice->CreateInputLayout(inputElementDesc, ARRAYSIZE(inputElementDesc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &mpInputLayout);
			if (FAILED(hr))
			{
				return false;
			}
		}
		///////////////////////////////////////////////////////////////////////////////////////////////

		//Pixel Shader
		{
			ID3DBlob* psBlob;
			ID3DBlob* errorBlob;
			HRESULT hr = D3DCompileFromFile(pInShaderFile, nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
			if (FAILED(hr))
			{
				const char* pError = (const char*)errorBlob->GetBufferPointer();
				printf("Error: %s", pError);

				return false;
			}
			hr = g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &mpPixelShader);
			if (FAILED(hr))
			{
				return false;
			}
		}

		//Sampler
		{

			D3D11_SAMPLER_DESC samplerDesc = {};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

			g_pd3dDevice->CreateSamplerState(&samplerDesc, &mpSamplerState);
		}

		return true;
	}

	void ActivateShader()
	{
		GD3DDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		GD3DDeviceContext->IASetInputLayout(mpInputLayout);

		GD3DDeviceContext->VSSetShader(mpVertexShader, nullptr, 0);

		GD3DDeviceContext->PSSetShader(mpPixelShader, nullptr, 0);
		GD3DDeviceContext->PSSetSamplers(0, 1, &mpSamplerState);
	}

private:
	ID3D11VertexShader* mpVertexShader{ nullptr };
	ID3D11InputLayout*  mpInputLayout{ nullptr };
	ID3D11PixelShader*  mpPixelShader{ nullptr };
	ID3D11SamplerState* mpSamplerState{ nullptr };
};
#endif