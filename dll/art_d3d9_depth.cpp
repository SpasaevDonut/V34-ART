// Direct3D 9 hardware depth buffer extraction, tracking, and linearization.

#include "art_d3d9_depth.h"
#include "art_ffmpeg.h"
#include "art_gui.h"
#include <algorithm>
#include <vector>
#include <cmath>

#pragma comment( lib, "d3d9.lib" )

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

namespace art
{
	volatile LONG g_nArtDepthMode = ART_DEPTH_MODE_FOG;
	volatile LONG g_nArtDepthCurve = ART_DEPTH_CURVE_LINEAR;

	const char *GetArtDepthModeName( LONG mode )
	{
		switch ( mode )
		{
		case ART_DEPTH_MODE_D3D9:
			return "d3d9";
		default:
			return "fog";
		}
	}

	LONG ArtDepthModeFromName( const char *pName )
	{
		if ( !pName )
			return ART_DEPTH_MODE_FOG;
		if ( !Q_stricmp( pName, "d3d9" ) || !Q_stricmp( pName, "precise" ) ||
			!Q_stricmp( pName, "hardware" ) || !Q_stricmp( pName, "intz" ) )
		{
			return ART_DEPTH_MODE_D3D9;
		}
		return ART_DEPTH_MODE_FOG;
	}

	const char *GetArtDepthCurveName( LONG curve )
	{
		switch ( curve )
		{
		case ART_DEPTH_CURVE_LOGARITHMIC:
			return "logarithmic";
		default:
			return "linear";
		}
	}

	LONG ArtDepthCurveFromName( const char *pName )
	{
		if ( !pName )
			return ART_DEPTH_CURVE_LINEAR;
		if ( !Q_stricmp( pName, "log" ) || !Q_stricmp( pName, "logarithmic" ) ||
			!Q_stricmp( pName, "1" ) )
		{
			return ART_DEPTH_CURVE_LOGARITHMIC;
		}
		return ART_DEPTH_CURVE_LINEAR;
	}

	namespace
	{

		static bool g_bIntzSupported = false;
		static bool g_bIntzChecked = false;

		static IDirect3DTexture9 *g_pMainIntzTexture = nullptr;
		static IDirect3DSurface9 *g_pMainIntzSurface = nullptr;
		static uint32_t g_nMainIntzWidth = 0;
		static uint32_t g_nMainIntzHeight = 0;

		IDirect3DSurface9 *GetOrCreateMainIntzSurface( IDirect3DDevice9 *pDevice, UINT width, UINT height )
		{
			if ( !pDevice || !g_bIntzSupported || width < 512 || height < 512 )
				return nullptr;

			if ( g_pMainIntzSurface && g_pMainIntzTexture && g_nMainIntzWidth == width && g_nMainIntzHeight == height )
			{
				return g_pMainIntzSurface;
			}

			if ( g_pMainIntzSurface )
			{
				g_pMainIntzSurface->Release();
				g_pMainIntzSurface = nullptr;
			}
			if ( g_pMainIntzTexture )
			{
				g_pMainIntzTexture->Release();
				g_pMainIntzTexture = nullptr;
			}
			g_nMainIntzWidth = 0;
			g_nMainIntzHeight = 0;

			HRESULT hr = pDevice->CreateTexture(
				width,
				height,
				1,
				D3DUSAGE_DEPTHSTENCIL,
				D3DFMT_INTZ,
				D3DPOOL_DEFAULT,
				&g_pMainIntzTexture,
				nullptr
			);

			if ( SUCCEEDED( hr ) && g_pMainIntzTexture )
			{
				hr = g_pMainIntzTexture->GetSurfaceLevel( 0, &g_pMainIntzSurface );
				if ( SUCCEEDED( hr ) && g_pMainIntzSurface )
				{
					g_nMainIntzWidth = width;
					g_nMainIntzHeight = height;
					LogMessage( "D3D9 DEPTH: Created Main INTZ Depth Texture & Surface %ux%u", width, height );
					return g_pMainIntzSurface;
				}
			}

			LogMessage( "D3D9 DEPTH: Failed to create INTZ texture %ux%u hr=0x%08lX", width, height, hr );
			return nullptr;
		}

		void ResetMainIntz()
		{
			if ( g_pMainIntzSurface )
			{
				g_pMainIntzSurface->Release();
				g_pMainIntzSurface = nullptr;
			}
			if ( g_pMainIntzTexture )
			{
				g_pMainIntzTexture->Release();
				g_pMainIntzTexture = nullptr;
			}
			g_nMainIntzWidth = 0;
			g_nMainIntzHeight = 0;
		}

		static const BYTE s_kPsDepthPreviewBytecode[] =
		{
			  0,   2, 255, 255, 254, 255,
			 46,   0,  67,  84,  65,  66,
			 28,   0,   0,   0, 139,   0,
			  0,   0,   0,   2, 255, 255,
			  3,   0,   0,   0,  28,   0,
			  0,   0,   0, 129,   0,   0,
			132,   0,   0,   0,  88,   0,
			  0,   0,   2,   0,   0,   0,
			  1,   0,   2,   0,  92,   0,
			  0,   0,   0,   0,   0,   0,
			108,   0,   0,   0,   2,   0,
			  1,   0,   1,   0,   6,   0,
			 92,   0,   0,   0,   0,   0,
			  0,   0, 111,   0,   0,   0,
			  3,   0,   0,   0,   1,   0,
			  2,   0, 116,   0,   0,   0,
			  0,   0,   0,   0,  99,  48,
			  0, 171,   1,   0,   3,   0,
			  1,   0,   4,   0,   1,   0,
			  0,   0,   0,   0,   0,   0,
			 99,  49,   0, 115,  48,   0,
			171, 171,   4,   0,  12,   0,
			  1,   0,   1,   0,   1,   0,
			  0,   0,   0,   0,   0,   0,
			112, 115,  95,  50,  95,  48,
			  0,  77, 105,  99, 114, 111,
			115, 111, 102, 116,  32,  40,
			 82,  41,  32,  72,  76,  83,
			 76,  32,  83, 104,  97, 100,
			101, 114,  32,  67, 111, 109,
			112, 105, 108, 101, 114,  32,
			 49,  48,  46,  49,   0, 171,
			 81,   0,   0,   5,   2,   0,
			 15, 160,  23, 183, 209,  56,
			 88, 255, 127, 191, 172, 197,
			 39,  55,   0,   0,   0,  63,
			 81,   0,   0,   5,   3,   0,
			 15, 160, 111,  18, 131,  58,
			  0,   0,   0,   0,   0,   0,
			128,  63,   0,   0,   0,   0,
			 31,   0,   0,   2,   0,   0,
			  0, 128,   0,   0,   3, 176,
			 31,   0,   0,   2,   0,   0,
			  0, 144,   0,   8,  15, 160,
			 66,   0,   0,   3,   0,   0,
			 15, 128,   0,   0, 228, 176,
			  0,   8, 228, 160,   5,   0,
			  0,   3,   0,   0,   2, 128,
			  0,   0,  85, 160,   0,   0,
			  0, 160,   2,   0,   0,   3,
			  0,   0,   4, 128,   0,   0,
			  0, 161,   0,   0,  85, 160,
			  4,   0,   0,   4,   0,   0,
			  4, 128,   0,   0,   0, 128,
			  0,   0, 170, 129,   0,   0,
			 85, 160,   6,   0,   0,   2,
			  0,   0,   4, 128,   0,   0,
			170, 128,   5,   0,   0,   3,
			  0,   0,   2, 128,   0,   0,
			170, 128,   0,   0,  85, 128,
			  2,   0,   0,   3,   0,   0,
			  4, 128,   0,   0,   0, 129,
			  2,   0, 170, 160,   2,   0,
			  0,   3,   0,   0,   1, 128,
			  0,   0,   0, 128,   2,   0,
			 85, 160,  88,   0,   0,   4,
			  0,   0,   2, 128,   0,   0,
			170, 128,   0,   0,   0, 160,
			  0,   0,  85, 128,  88,   0,
			  0,   4,   0,   0,   1, 128,
			  0,   0,   0, 128,   0,   0,
			 85, 160,   0,   0,  85, 128,
			 11,   0,   0,   3,   1,   0,
			  8, 128,   0,   0,   0, 128,
			  0,   0, 170, 160,   2,   0,
			  0,   3,   0,   0,   1, 128,
			  0,   0,   0, 128,   0,   0,
			170, 161,  10,   0,   0,   3,
			  0,   0,   2, 128,   0,   0,
			255, 160,   1,   0, 255, 128,
			  6,   0,   0,   2,   0,   0,
			  4, 128,   0,   0, 170, 160,
			  5,   0,   0,   3,   0,   0,
			  2, 128,   0,   0, 170, 128,
			  0,   0,  85, 128,   5,   0,
			  0,   3,   0,   0,   4, 128,
			  0,   0, 170, 128,   0,   0,
			255, 160,  15,   0,   0,   2,
			  0,   0,   4, 128,   0,   0,
			170, 128,   6,   0,   0,   2,
			  0,   0,   4, 128,   0,   0,
			170, 128,  15,   0,   0,   2,
			  0,   0,   2, 128,   0,   0,
			 85, 128,   5,   0,   0,   3,
			  0,   0,   2, 128,   0,   0,
			170, 128,   0,   0,  85, 128,
			 11,   0,   0,   3,   1,   0,
			  1, 128,   0,   0,   0, 128,
			  3,   0,  85, 160,   2,   0,
			  0,   3,   0,   0,   4, 128,
			  0,   0, 170, 161,   0,   0,
			255, 160,  11,   0,   0,   3,
			  1,   0,   2, 128,   0,   0,
			170, 128,   2,   0,   0, 160,
			 10,   0,   0,   3,   0,   0,
			  4, 128,   1,   0,  85, 128,
			  1,   0,   0, 128,   2,   0,
			  0,   3,   0,   0,   4, 128,
			  0,   0, 170, 128,   3,   0,
			170, 160,  15,   0,   0,   2,
			  0,   0,   4, 128,   0,   0,
			170, 128,   2,   0,   0,   3,
			  0,   0,   8, 128,   1,   0,
			 85, 128,   3,   0, 170, 160,
			  6,   0,   0,   2,   1,   0,
			  1, 128,   1,   0,  85, 128,
			  5,   0,   0,   3,   0,   0,
			  1, 128,   0,   0,   0, 128,
			  1,   0,   0, 128,  15,   0,
			  0,   2,   0,   0,   8, 128,
			  0,   0, 255, 128,   6,   0,
			  0,   2,   0,   0,   8, 128,
			  0,   0, 255, 128,   5,   0,
			  0,   3,   0,   0,   4, 128,
			  0,   0, 255, 128,   0,   0,
			170, 128,   2,   0,   0,   3,
			  0,   0,   8, 128,   0,   0,
			255, 161,   0,   0, 170, 160,
			 88,   0,   0,   4,   0,   0,
			  8, 128,   0,   0, 255, 128,
			  3,   0,  85, 161,   3,   0,
			170, 161,   1,   0,   0,   2,
			  1,   0,   4, 128,   0,   0,
			170, 160,   2,   0,   0,   3,
			  1,   0,   1, 128,   1,   0,
			170, 129,   3,   0,   0, 160,
			 88,   0,   0,   4,   0,   0,
			  8, 128,   1,   0,   0, 128,
			  3,   0,  85, 160,   0,   0,
			255, 128,  88,   0,   0,   4,
			  0,   0,   2, 128,   0,   0,
			255, 128,   0,   0, 170, 128,
			  0,   0,  85, 128,   1,   0,
			  0,   2,   0,   0,   8, 128,
			  2,   0, 255, 160,   2,   0,
			  0,   3,   0,   0,   4, 128,
			  0,   0, 255, 128,   1,   0,
			  0, 161,  88,   0,   0,   4,
			  0,   0,   1, 128,   0,   0,
			170, 128,   0,   0,   0, 128,
			  0,   0,  85, 128,   1,   0,
			  0,   2,   0,   0,  23, 128,
			  0,   0,   0, 128,   1,   0,
			  0,   2,   0,   0,   8, 128,
			  3,   0, 170, 160,   1,   0,
			  0,   2,   0,   8,  15, 128,
			  0,   0, 228, 128, 255, 255,
			  0,   0
		};

		static IDirect3DPixelShader9 *s_pPreviewShader = nullptr;

		void ResetDepthPreviewShader()
		{
			if ( s_pPreviewShader )
			{
				s_pPreviewShader->Release();
				s_pPreviewShader = nullptr;
			}
		}
	}

	bool InitializeD3D9Depth( IDirect3D9 *pD3D )
	{
		if ( !pD3D )
			return false;

		if ( !g_bIntzChecked )
		{
			HRESULT hr = pD3D->CheckDeviceFormat(
				D3DADAPTER_DEFAULT,
				D3DDEVTYPE_HAL,
				D3DFMT_X8R8G8B8,
				D3DUSAGE_DEPTHSTENCIL,
				D3DRTYPE_TEXTURE,
				D3DFMT_INTZ
			);
			g_bIntzSupported = SUCCEEDED( hr );
			g_bIntzChecked = true;
			LogMessage( "D3D9 DEPTH: INTZ support check result=0x%08lX supported=%d", hr, g_bIntzSupported ? 1 : 0 );
		}

		return g_bIntzSupported;
	}

	void ShutdownD3D9Depth()
	{
		ResetMainIntz();
		ResetDepthPreviewShader();
	}

	bool IsD3D9IntzSupported()
	{
		return g_bIntzSupported;
	}

	bool IsD3D9DepthCaptureReady()
	{
		return g_bIntzSupported;
	}

	bool EnsureActiveDepthSurfaceIsIntz( IDirect3DDevice9 *pDevice )
	{
		if ( !pDevice || !g_bIntzSupported )
			return false;

		IDirect3DSurface9 *pCurrentSurface = nullptr;
		HRESULT hr = pDevice->GetDepthStencilSurface( &pCurrentSurface );
		if ( FAILED( hr ) || !pCurrentSurface )
			return false;

		D3DSURFACE_DESC desc = {};
		pCurrentSurface->GetDesc( &desc );
		pCurrentSurface->Release();

		if ( desc.Format == D3DFMT_INTZ )
			return true;

		if ( desc.Width >= 512 && desc.Height >= 512 && desc.MultiSampleType == D3DMULTISAMPLE_NONE )
		{
			IDirect3DSurface9 *pIntz = GetOrCreateMainIntzSurface( pDevice, desc.Width, desc.Height );
			if ( pIntz )
			{
				pDevice->SetDepthStencilSurface( pIntz );
				LogMessage( "D3D9 DEPTH: explicitly activated INTZ depth buffer %ux%u", desc.Width, desc.Height );
				return true;
			}
		}

		return false;
	}

	HRESULT HandleD3D9SetDepthStencilSurface(
		IDirect3DDevice9 *pDevice,
		IDirect3DSurface9 *pNewZStencil,
		HRESULT ( WINAPI *pOriginalSet )( IDirect3DDevice9 *, IDirect3DSurface9 * )
	)
	{
		if ( !pDevice || !pOriginalSet )
			return D3DERR_INVALIDCALL;

		if ( pNewZStencil && g_bIntzSupported )
		{
			D3DSURFACE_DESC desc = {};
			if ( SUCCEEDED( pNewZStencil->GetDesc( &desc ) ) )
			{
				if ( desc.Format != D3DFMT_INTZ && desc.Width >= 512 && desc.Height >= 512 &&
					desc.MultiSampleType == D3DMULTISAMPLE_NONE )
				{
					IDirect3DSurface9 *pIntz = GetOrCreateMainIntzSurface( pDevice, desc.Width, desc.Height );
					if ( pIntz )
					{
						return pOriginalSet( pDevice, pIntz );
					}
				}
			}
		}

		return pOriginalSet( pDevice, pNewZStencil );
	}

	HRESULT HandleD3D9CreateDepthStencilSurface(
		IDirect3DDevice9 *pDevice,
		UINT Width,
		UINT Height,
		D3DFORMAT Format,
		D3DMULTISAMPLE_TYPE MultiSample,
		DWORD MultisampleQuality,
		BOOL Discard,
		IDirect3DSurface9 **ppSurface,
		HANDLE *pSharedHandle,
		HRESULT ( WINAPI *pOriginalCreate )( IDirect3DDevice9 *, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9 **, HANDLE * )
	)
	{
		if ( !pDevice || !ppSurface )
			return D3DERR_INVALIDCALL;

		if ( g_bIntzSupported && Width >= 512 && Height >= 512 && MultiSample == D3DMULTISAMPLE_NONE )
		{
			IDirect3DSurface9 *pIntz = GetOrCreateMainIntzSurface( pDevice, Width, Height );
			if ( pIntz )
			{
				pIntz->AddRef();
				*ppSurface = pIntz;
				LogMessage( "D3D9 DEPTH: CreateDepthStencilSurface substituted INTZ %ux%u", Width, Height );
				return D3D_OK;
			}
		}

		if ( pOriginalCreate )
		{
			return pOriginalCreate( pDevice, Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle );
		}
		return D3DERR_INVALIDCALL;
	}

	void HandleD3D9Present( IDirect3DDevice9 *pDevice, uint32_t backbufferWidth, uint32_t backbufferHeight )
	{
		( void )pDevice;
		( void )backbufferWidth;
		( void )backbufferHeight;
	}

	void HandleD3D9Reset()
	{
		ResetMainIntz();
		ResetDepthPreviewShader();
	}

	bool RenderPreciseDepthPreview(
		IDirect3DDevice9 *pDevice,
		int x,
		int y,
		int width,
		int height,
		float zNear,
		float zFar,
		float depthStart,
		float depthEnd,
		LONG depthCurve
	)
	{
		if ( !pDevice || width <= 0 || height <= 0 || !g_bIntzSupported )
			return false;

		IDirect3DTexture9 *srcTexture = g_pMainIntzTexture;
		bool releaseSrcTexture = false;
		if ( !srcTexture )
		{
			IDirect3DSurface9 *pActiveDepth = nullptr;
			if ( SUCCEEDED( pDevice->GetDepthStencilSurface( &pActiveDepth ) ) && pActiveDepth )
			{
				if ( SUCCEEDED( pActiveDepth->GetContainer( IID_PPV_ARGS( &srcTexture ) ) ) && srcTexture )
				{
					releaseSrcTexture = true;
				}
				pActiveDepth->Release();
			}
		}
		if ( !srcTexture )
			return false;

		if ( !s_pPreviewShader )
		{
			pDevice->CreatePixelShader( reinterpret_cast<const DWORD *>( s_kPsDepthPreviewBytecode ), &s_pPreviewShader );
		}
		if ( !s_pPreviewShader )
		{
			if ( releaseSrcTexture && srcTexture ) srcTexture->Release();
			return false;
		}

		IDirect3DStateBlock9 *savedState = nullptr;
		if ( SUCCEEDED( pDevice->CreateStateBlock( D3DSBT_ALL, &savedState ) ) && savedState )
		{
			savedState->Capture();
		}

		D3DVIEWPORT9 savedViewport = {};
		pDevice->GetViewport( &savedViewport );

		pDevice->SetRenderState( D3DRS_ZENABLE, FALSE );
		pDevice->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
		pDevice->SetRenderState( D3DRS_FILLMODE, D3DFILL_SOLID );
		pDevice->SetRenderState( D3DRS_ALPHATESTENABLE, FALSE );
		pDevice->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
		pDevice->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
		pDevice->SetRenderState( D3DRS_FOGENABLE, FALSE );
		pDevice->SetRenderState( D3DRS_STENCILENABLE, FALSE );
		pDevice->SetRenderState( D3DRS_CLIPPING, FALSE );
		pDevice->SetRenderState( D3DRS_LIGHTING, FALSE );
		pDevice->SetRenderState( D3DRS_CLIPPLANEENABLE, 0 );
		pDevice->SetRenderState( D3DRS_COLORWRITEENABLE, 0x0000000F );
		pDevice->SetRenderState( D3DRS_SRGBWRITEENABLE, FALSE );

		pDevice->SetTexture( 0, srcTexture );
		pDevice->SetSamplerState( 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
		pDevice->SetSamplerState( 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
		pDevice->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT );
		pDevice->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_POINT );
		pDevice->SetSamplerState( 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE );
		pDevice->SetSamplerState( 0, D3DSAMP_SRGBTEXTURE, FALSE );

		for ( DWORD i = 1; i < 8; ++i )
			pDevice->SetTexture( i, nullptr );

		pDevice->SetVertexShader( nullptr );
		pDevice->SetPixelShader( s_pPreviewShader );

		const float nearPlane = ( zNear > 0.0f ) ? zNear : 3.0f;
		const float farPlane = ( zFar > nearPlane ) ? zFar : 20000.0f;
		const float startVal = depthStart;
		const float endVal = ( depthEnd > depthStart ) ? depthEnd : ( depthStart + 1.0f );
		const float isLog = ( depthCurve == ART_DEPTH_CURVE_LOGARITHMIC ) ? 1.0f : 0.0f;
		const float rangeSpan = endVal - startVal;

		const float c0[4] = { nearPlane, farPlane, startVal, endVal };
		const float c1[4] = { isLog, rangeSpan, 0.0f, 0.0f };
		pDevice->SetPixelShaderConstantF( 0, c0, 1 );
		pDevice->SetPixelShaderConstantF( 1, c1, 1 );

		D3DVIEWPORT9 vp = {};
		vp.X = x;
		vp.Y = y;
		vp.Width = width;
		vp.Height = height;
		vp.MinZ = 0.0f;
		vp.MaxZ = 1.0f;
		pDevice->SetViewport( &vp );

		struct VertexRHW
		{
			float x, y, z, rhw;
			float u, v;
		};

		const float vx = static_cast<float>( x );
		const float vy = static_cast<float>( y );
		const float vw = static_cast<float>( width );
		const float vh = static_cast<float>( height );

		VertexRHW vertices[4] = {
			{ vx - 0.5f,      vy - 0.5f,      0.0f, 1.0f, 0.0f, 0.0f },
			{ vx + vw - 0.5f, vy - 0.5f,      0.0f, 1.0f, 1.0f, 0.0f },
			{ vx - 0.5f,      vy + vh - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
			{ vx + vw - 0.5f, vy + vh - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f }
		};

		pDevice->SetFVF( D3DFVF_XYZRHW | D3DFVF_TEX1 );
		pDevice->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, 2, vertices, sizeof( VertexRHW ) );

		pDevice->SetTexture( 0, nullptr );

		if ( savedState )
		{
			savedState->Apply();
			savedState->Release();
		}

		pDevice->SetViewport( &savedViewport );

		if ( releaseSrcTexture && srcTexture )
		{
			srcTexture->Release();
		}

		return true;
	}

	bool ReadPreciseDepthPixels(
		IDirect3DDevice9 *pDevice,
		int x,
		int y,
		int width,
		int height,
		unsigned char *pOutRgb,
		float zNear,
		float zFar,
		float depthStart,
		float depthEnd,
		LONG depthCurve
	)
	{
		if ( !pDevice || !pOutRgb || width <= 0 || height <= 0 )
			return false;

		const bool rendered = RenderPreciseDepthPreview(
			pDevice,
			x,
			y,
			width,
			height,
			zNear,
			zFar,
			depthStart,
			depthEnd,
			depthCurve
		);

		if ( !rendered )
			return false;

		if ( g_pMaterials )
		{
			g_pMaterials->ReadPixels( x, y, width, height, pOutRgb, IMAGE_FORMAT_RGB888 );
			return true;
		}

		return false;
	}

	bool CapturePreciseDepthPass( const CViewSetup &view, const char *pPassName )
	{
		if ( view.width <= 0 || view.height <= 0 )
			return false;

		IDirect3DDevice9 *pDevice = GetArtGuiDevice();
		if ( !pDevice )
		{
			LogMessage( "PRECISE DEPTH: GetArtGuiDevice returned null" );
			FlushLog();
			return false;
		}

		LogMessage( "PRECISE DEPTH: EnsureActiveDepthSurfaceIsIntz..." );
		FlushLog();
		EnsureActiveDepthSurfaceIsIntz( pDevice );

		const LONG depthCurve = InterlockedCompareExchange( &g_nArtDepthCurve, 0, 0 );
		LogMessage( "PRECISE DEPTH: Rendering depth preview quad to backbuffer..." );
		FlushLog();

		const bool previewOk = RenderPreciseDepthPreview(
			pDevice,
			view.x,
			view.y,
			view.width,
			view.height,
			view.zNear,
			view.zFar,
			art_depth_start.GetFloat(),
			art_depth_end.GetFloat(),
			depthCurve
		);

		if ( !previewOk )
		{
			LogMessage( "PRECISE DEPTH: RenderPreciseDepthPreview failed" );
			FlushLog();
			return false;
		}

		LogMessage( "PRECISE DEPTH: Capturing frame via CaptureTga..." );
		FlushLog();
		return CaptureTga( view, pPassName );
	}
}
