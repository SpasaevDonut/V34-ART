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
		static const DWORD kPsCopyDepthBytecode[] = {
			0xFFFF0200,                                      // ps_2_0
			0x0200001F, 0x80000000, 0x900F0000,             // dcl_2d s0
			0x0200001F, 0x80000005, 0x80030000,             // dcl t0.xy
			0x03000042, 0x800F0000, 0x80E40000, 0x90E40000, // texld r0, t0, s0
			0x02000001, 0x800F0800, 0x80E40000,             // mov oC0, r0
			0x0000FFFF                                      // end
		};

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

		class D3D9DepthCopier
		{
		public:
			D3D9DepthCopier() :
				_copyState( nullptr ),
				_pixelShader( nullptr ),
				_backupTexture( nullptr ),
				_backupSurface( nullptr ),
				_stagingSurface( nullptr ),
				_backupWidth( 0 ),
				_backupHeight( 0 )
			{
			}

			~D3D9DepthCopier()
			{
				Reset();
			}

			void Reset()
			{
				if ( _copyState )
				{
					_copyState->Release();
					_copyState = nullptr;
				}
				if ( _pixelShader )
				{
					_pixelShader->Release();
					_pixelShader = nullptr;
				}
				if ( _stagingSurface )
				{
					_stagingSurface->Release();
					_stagingSurface = nullptr;
				}
				if ( _backupSurface )
				{
					_backupSurface->Release();
					_backupSurface = nullptr;
				}
				if ( _backupTexture )
				{
					_backupTexture->Release();
					_backupTexture = nullptr;
				}
				_backupWidth = 0;
				_backupHeight = 0;
			}

			HRESULT InitCopyStateBlock( IDirect3DDevice9 *device )
			{
				if ( _copyState )
					return D3D_OK;
				if ( !device )
					return D3DERR_INVALIDCALL;

				if ( !_pixelShader )
				{
					device->CreatePixelShader( kPsCopyDepthBytecode, &_pixelShader );
				}

				HRESULT hr = device->BeginStateBlock();
				if ( FAILED( hr ) )
					return hr;

				if ( _pixelShader )
					device->SetPixelShader( _pixelShader );
				else
					device->SetPixelShader( nullptr );
				device->SetVertexShader( nullptr );

				device->SetRenderState( D3DRS_ZENABLE, FALSE );
				device->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
				device->SetRenderState( D3DRS_FILLMODE, D3DFILL_SOLID );
				device->SetRenderState( D3DRS_ALPHATESTENABLE, FALSE );
				device->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
				device->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
				device->SetRenderState( D3DRS_FOGENABLE, FALSE );
				device->SetRenderState( D3DRS_STENCILENABLE, FALSE );
				device->SetRenderState( D3DRS_CLIPPING, FALSE );
				device->SetRenderState( D3DRS_LIGHTING, FALSE );
				device->SetRenderState( D3DRS_CLIPPLANEENABLE, 0 );
				device->SetRenderState( D3DRS_MULTISAMPLEANTIALIAS, FALSE );
				device->SetRenderState( D3DRS_COLORWRITEENABLE,
					D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
					D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA );
				device->SetRenderState( D3DRS_SCISSORTESTENABLE, FALSE );
				device->SetRenderState( D3DRS_SRGBWRITEENABLE, FALSE );

				device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
				device->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
				device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
				device->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
				device->SetTextureStageState( 0, D3DTSS_TEXCOORDINDEX, 0 );
				device->SetTextureStageState( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );

				device->SetSamplerState( 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
				device->SetSamplerState( 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
				device->SetSamplerState( 0, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP );
				device->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT );
				device->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_POINT );
				device->SetSamplerState( 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE );
				device->SetSamplerState( 0, D3DSAMP_SRGBTEXTURE, FALSE );

				const D3DMATRIX identityMatrix = {
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f,
					0.0f, 0.0f, 0.0f, 1.0f
				};
				device->SetTransform( D3DTS_VIEW, &identityMatrix );
				device->SetTransform( D3DTS_PROJECTION, &identityMatrix );
				device->SetTransform( D3DTS_WORLD, &identityMatrix );

				// Source Engine water clip plane reset
				const float zeroClipPlane[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				device->SetClipPlane( 0, zeroClipPlane );

				for ( DWORD i = 1; i < 8; ++i )
					device->SetTexture( i, nullptr );

				return device->EndStateBlock( &_copyState );
			}

			HRESULT EnsureBuffers( IDirect3DDevice9 *device, uint32_t width, uint32_t height )
			{
				if ( _backupTexture && _stagingSurface && _backupWidth == width && _backupHeight == height )
					return D3D_OK;

				if ( _stagingSurface )
				{
					_stagingSurface->Release();
					_stagingSurface = nullptr;
				}
				if ( _backupSurface )
				{
					_backupSurface->Release();
					_backupSurface = nullptr;
				}
				if ( _backupTexture )
				{
					_backupTexture->Release();
					_backupTexture = nullptr;
				}

				HRESULT hr = device->CreateTexture(
					width,
					height,
					1,
					D3DUSAGE_RENDERTARGET,
					D3DFMT_R32F,
					D3DPOOL_DEFAULT,
					&_backupTexture,
					nullptr
				);

				if ( FAILED( hr ) || !_backupTexture )
				{
					// Fallback to A8R8G8B8 if R32F render target is unsupported
					hr = device->CreateTexture(
						width,
						height,
						1,
						D3DUSAGE_RENDERTARGET,
						D3DFMT_A8R8G8B8,
						D3DPOOL_DEFAULT,
						&_backupTexture,
						nullptr
					);
				}

				if ( SUCCEEDED( hr ) && _backupTexture )
				{
					_backupWidth = width;
					_backupHeight = height;
					hr = _backupTexture->GetSurfaceLevel( 0, &_backupSurface );

					if ( SUCCEEDED( hr ) && _backupSurface )
					{
						D3DSURFACE_DESC desc = {};
						_backupSurface->GetDesc( &desc );
						hr = device->CreateOffscreenPlainSurface(
							width,
							height,
							desc.Format,
							D3DPOOL_SYSTEMMEM,
							&_stagingSurface,
							nullptr
						);
					}
				}

				return hr;
			}

			HRESULT CopyDepthBuffer( IDirect3DDevice9 *device, IDirect3DSurface9 *srcDepthSurface, uint32_t width, uint32_t height )
			{
				if ( !device || width == 0 || height == 0 )
					return D3DERR_INVALIDCALL;

				HRESULT hr = EnsureBuffers( device, width, height );
				if ( FAILED( hr ) )
					return hr;

				IDirect3DTexture9 *srcTexture = nullptr;
				if ( srcDepthSurface )
				{
					srcDepthSurface->GetContainer( IID_PPV_ARGS( &srcTexture ) );
				}
				if ( !srcTexture && g_pMainIntzTexture )
				{
					srcTexture = g_pMainIntzTexture;
					srcTexture->AddRef();
				}
				if ( !srcTexture )
				{
					LogMessage( "D3D9 DEPTH: CopyDepthBuffer could not obtain source INTZ texture" );
					return D3DERR_INVALIDCALL;
				}

				InitCopyStateBlock( device );

				IDirect3DStateBlock9 *savedState = nullptr;
				if ( SUCCEEDED( device->CreateStateBlock( D3DSBT_ALL, &savedState ) ) && savedState )
				{
					savedState->Capture();
				}

				if ( _copyState )
					_copyState->Apply();

				device->SetRenderTarget( 0, _backupSurface );
				device->SetDepthStencilSurface( nullptr );
				device->SetTexture( 0, srcTexture );
				if ( _pixelShader )
					device->SetPixelShader( _pixelShader );

				D3DVIEWPORT9 vp = {};
				vp.X = 0;
				vp.Y = 0;
				vp.Width = width;
				vp.Height = height;
				vp.MinZ = 0.0f;
				vp.MaxZ = 1.0f;
				device->SetViewport( &vp );

				struct VertexRHW
				{
					float x, y, z, rhw;
					float u, v;
				};

				const float w = static_cast<float>( width );
				const float h = static_cast<float>( height );

				VertexRHW vertices[4] = {
					{ -0.5f,     -0.5f,     0.0f, 1.0f, 0.0f, 0.0f },
					{ w - 0.5f,  -0.5f,     0.0f, 1.0f, 1.0f, 0.0f },
					{ -0.5f,     h - 0.5f,  0.0f, 1.0f, 0.0f, 1.0f },
					{ w - 0.5f,  h - 0.5f,  0.0f, 1.0f, 1.0f, 1.0f }
				};

				device->SetFVF( D3DFVF_XYZRHW | D3DFVF_TEX1 );
				device->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, 2, vertices, sizeof( VertexRHW ) );

				device->SetTexture( 0, nullptr );

				if ( savedState )
				{
					savedState->Apply();
					savedState->Release();
				}

				srcTexture->Release();
				return D3D_OK;
			}

			IDirect3DSurface9 *GetBackupSurface() const { return _backupSurface; }
			IDirect3DSurface9 *GetStagingSurface() const { return _stagingSurface; }
			uint32_t GetWidth() const { return _backupWidth; }
			uint32_t GetHeight() const { return _backupHeight; }

		private:
			IDirect3DStateBlock9 *_copyState;
			IDirect3DPixelShader9 *_pixelShader;
			IDirect3DTexture9 *_backupTexture;
			IDirect3DSurface9 *_backupSurface;
			IDirect3DSurface9 *_stagingSurface;
			uint32_t _backupWidth;
			uint32_t _backupHeight;
		};

		D3D9DepthCopier g_DepthCopier;
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
		g_DepthCopier.Reset();
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
		g_DepthCopier.Reset();
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

		IDirect3DSurface9 *pBackup = g_DepthCopier.GetBackupSurface();
		IDirect3DSurface9 *pStaging = g_DepthCopier.GetStagingSurface();
		if ( !pBackup || !pStaging )
			return false;

		HRESULT hr = pDevice->GetRenderTargetData( pBackup, pStaging );
		if ( FAILED( hr ) )
		{
			LogMessage( "D3D9 DEPTH: GetRenderTargetData failed hr=0x%08lX", hr );
			return false;
		}

		D3DLOCKED_RECT locked = {};
		hr = pStaging->LockRect( &locked, nullptr, D3DLOCK_READONLY );
		if ( FAILED( hr ) || !locked.pBits )
		{
			LogMessage( "D3D9 DEPTH: LockRect failed hr=0x%08lX", hr );
			return false;
		}

		D3DSURFACE_DESC desc = {};
		pStaging->GetDesc( &desc );
		const bool isFloat = ( desc.Format == D3DFMT_R32F );

		const float nearPlane = ( zNear > 0.0f ) ? zNear : 3.0f;
		const float farPlane = ( zFar > nearPlane ) ? zFar : 20000.0f;
		const float startVal = depthStart;
		const float endVal = ( depthEnd > depthStart ) ? depthEnd : ( depthStart + 1.0f );
		const float rangeSpan = endVal - startVal;
		const uint32_t surfWidth = g_DepthCopier.GetWidth();
		const uint32_t surfHeight = g_DepthCopier.GetHeight();

		const unsigned char *pSrcBytes = static_cast<const unsigned char *>( locked.pBits );

		for ( int row = 0; row < height; ++row )
		{
			const int srcRow = y + row;
			if ( srcRow < 0 || srcRow >= static_cast<int>( surfHeight ) )
				continue;

			unsigned char *pDstRow = pOutRgb + ( row * width * 3 );

			for ( int col = 0; col < width; ++col )
			{
				const int srcCol = x + col;
				float rawDepth = 1.0f;
				if ( srcCol >= 0 && srcCol < static_cast<int>( surfWidth ) )
				{
					if ( isFloat )
					{
						const float *pRowFloat = reinterpret_cast<const float *>( pSrcBytes + srcRow * locked.Pitch );
						rawDepth = pRowFloat[srcCol];
					}
					else
					{
						// D3DFMT_A8R8G8B8/X8R8G8B8 format fallback
						const unsigned char *pPixel = pSrcBytes + srcRow * locked.Pitch + srcCol * 4;
						rawDepth = static_cast<float>( pPixel[2] ) / 255.0f;
					}
				}

				float linearZ;
				if ( rawDepth >= 0.99999f )
				{
					linearZ = farPlane;
				}
				else if ( rawDepth <= 0.00001f )
				{
					linearZ = nearPlane;
				}
				else
				{
					linearZ = ( nearPlane * farPlane ) / ( farPlane - rawDepth * ( farPlane - nearPlane ) );
				}

				float norm;
				if ( depthCurve == ART_DEPTH_CURVE_LOGARITHMIC )
				{
					if ( startVal > 0.001f && endVal > startVal )
					{
						const float zClamped = ( linearZ < startVal ) ? startVal : ( ( linearZ > endVal ) ? endVal : linearZ );
						norm = logf( zClamped / startVal ) / logf( endVal / startVal );
					}
					else
					{
						const float clampedOffset = ( linearZ < startVal ) ? 0.0f : ( ( linearZ > endVal ) ? rangeSpan : ( linearZ - startVal ) );
						norm = logf( 1.0f + clampedOffset ) / logf( 1.0f + rangeSpan );
					}
				}
				else
				{
					norm = ( linearZ - startVal ) / rangeSpan;
				}

				if ( norm < 0.0f ) norm = 0.0f;
				if ( norm > 1.0f ) norm = 1.0f;

				const unsigned char byteVal = static_cast<unsigned char>( norm * 255.0f + 0.5f );
				const int outIdx = col * 3;
				pDstRow[outIdx] = byteVal;
				pDstRow[outIdx + 1] = byteVal;
				pDstRow[outIdx + 2] = byteVal;
			}
		}

		pStaging->UnlockRect();
		return true;
	}

	bool CapturePreciseDepthPass( const CViewSetup &view, const char *pPassName )
	{
		if ( view.width <= 0 || view.height <= 0 )
			return false;

		IDirect3DDevice9 *pDevice = GetArtGuiDevice();
		if ( !pDevice )
			return false;

		EnsureActiveDepthSurfaceIsIntz( pDevice );

		IDirect3DSurface9 *pActiveDepth = nullptr;
		HRESULT hr = pDevice->GetDepthStencilSurface( &pActiveDepth );
		if ( FAILED( hr ) || !pActiveDepth )
			return false;

		hr = g_DepthCopier.CopyDepthBuffer( pDevice, pActiveDepth, view.width, view.height );
		pActiveDepth->Release();
		if ( FAILED( hr ) )
		{
			LogMessage( "PRECISE DEPTH: CopyDepthBuffer failed hr=0x%08lX", hr );
			return false;
		}

		const size_t pixelCount = static_cast<size_t>( view.width ) * static_cast<size_t>( view.height );
		const size_t pixelBytes = pixelCount * 3;
		const LONG compressionMode = InterlockedCompareExchange(
			&g_nArtTgaCompressionMode, ART_TGA_COMPRESSION_AUTO, ART_TGA_COMPRESSION_AUTO );
		const size_t rlePacketOverhead = static_cast<size_t>( view.height ) *
			( ( static_cast<size_t>( view.width ) + 127 ) / 128 );
		const size_t pixelBufferBytes = ( compressionMode == ART_TGA_COMPRESSION_OFF ) ?
			pixelBytes : pixelBytes + 1024 + rlePacketOverhead;
		const size_t maxTgaBytes = 1024 + pixelCount * 4;

		EnsureArtQueueCapacity( pixelBytes + 1024, pixelBufferBytes + maxTgaBytes );

		unsigned char *pPixels = static_cast<unsigned char *>(
			AllocateArtCaptureMemory( pixelBufferBytes, "precise depth frame readback" ) );
		if ( !pPixels )
			return false;

		const LONG depthCurve = InterlockedCompareExchange( &g_nArtDepthCurve, 0, 0 );
		const unsigned __int64 readTiming = BeginArtStageTiming();
		const bool readOk = ReadPreciseDepthPixels(
			pDevice,
			view.x,
			view.y,
			view.width,
			view.height,
			pPixels,
			view.zNear,
			view.zFar,
			art_depth_start.GetFloat(),
			art_depth_end.GetFloat(),
			depthCurve
		);
		EndArtStageTiming( ART_TIMING_READ, readTiming );

		if ( !readOk )
		{
			free( pPixels );
			LogMessage( "PRECISE DEPTH: read pixels failed for frame=%d", g_nFrame );
			return false;
		}

		if ( InterlockedCompareExchange( &g_nArtOutputMode, ART_OUTPUT_MODE_TGA, ART_OUTPUT_MODE_TGA ) == ART_OUTPUT_MODE_FFMPEG )
		{
			const bool ffmpegOk = WriteArtFfmpegFrame( pPassName, pPixels, view.width, view.height );
			free( pPixels );
			return ffmpegOk;
		}

		void *pTga = AllocateArtCaptureMemory( maxTgaBytes, "TGA encode" );
		if ( !pTga )
		{
			free( pPixels );
			return false;
		}

		const unsigned __int64 encodeTiming = BeginArtStageTiming();
		CUtlBuffer buffer( pTga, static_cast<int>( maxTgaBytes ) );
		const bool encoded = TGAWriter::WriteToBuffer( pPixels, buffer, view.width, view.height,
			IMAGE_FORMAT_RGB888, IMAGE_FORMAT_RGB888 );
		if ( !encoded )
		{
			EndArtStageTiming( ART_TIMING_ENCODE, encodeTiming );
			free( pPixels );
			free( pTga );
			return false;
		}

		const unsigned long uncompressedBytes = static_cast<unsigned long>( buffer.TellPut() );
		unsigned char *pOutput = static_cast<unsigned char *>( buffer.Base() );
		unsigned long outputBytes = uncompressedBytes;
		bool usingRle = false;

		if ( compressionMode != ART_TGA_COMPRESSION_OFF )
		{
			size_t rleBytes = 0;
			const bool rleEncoded = EncodeArtTgaRle(
				static_cast<const unsigned char *>( buffer.Base() ),
				static_cast<size_t>( buffer.TellPut() ), pPixels, pixelBufferBytes, rleBytes );
			if ( rleEncoded &&
				( compressionMode == ART_TGA_COMPRESSION_RLE ||
					rleBytes < static_cast<size_t>( uncompressedBytes ) ) )
			{
				pOutput = pPixels;
				outputBytes = static_cast<unsigned long>( rleBytes );
				usingRle = true;
				free( buffer.Base() );
			}
		}

		if ( !usingRle )
			free( pPixels );
		EndArtStageTiming( ART_TIMING_ENCODE, encodeTiming );

		char path[MAX_PATH];
		if ( !BuildCapturePath( path, sizeof( path ), pPassName, g_nFrame ) )
		{
			free( pOutput );
			return false;
		}

		const unsigned __int64 writeTiming = BeginArtStageTiming();
		const FSAsyncStatus_t status = g_pFileSystem->AsyncWrite(
			path, pOutput, static_cast<int>( outputBytes ), true );
		EndArtStageTiming( ART_TIMING_WRITE, writeTiming );

		if ( status < FSASYNC_OK )
		{
			free( pOutput );
			return false;
		}

		NoteArtQueuedWrite( outputBytes );
		RecordArtCompressionResult( uncompressedBytes, outputBytes );
		RecordArtCapturedFile( pPassName, g_nFrame, outputBytes, view.width, view.height,
			view.fov, view.fovViewmodel );
		return true;
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
		if ( !srcTexture )
		{
			IDirect3DSurface9 *pActiveDepth = nullptr;
			if ( SUCCEEDED( pDevice->GetDepthStencilSurface( &pActiveDepth ) ) && pActiveDepth )
			{
				pActiveDepth->GetContainer( IID_PPV_ARGS( &srcTexture ) );
				pActiveDepth->Release();
			}
		}
		if ( !srcTexture )
			return false;

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
		if ( !s_pPreviewShader )
		{
			pDevice->CreatePixelShader( reinterpret_cast<const DWORD *>( s_kPsDepthPreviewBytecode ), &s_pPreviewShader );
		}
		if ( !s_pPreviewShader )
			return false;

		IDirect3DStateBlock9 *savedState = nullptr;
		if ( SUCCEEDED( pDevice->CreateStateBlock( D3DSBT_ALL, &savedState ) ) && savedState )
		{
			savedState->Capture();
		}

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
		pDevice->SetRenderState( D3DRS_COLORWRITEENABLE, 0x0000000F );
		pDevice->SetRenderState( D3DRS_SRGBWRITEENABLE, FALSE );

		pDevice->SetTexture( 0, srcTexture );
		pDevice->SetSamplerState( 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
		pDevice->SetSamplerState( 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
		pDevice->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT );
		pDevice->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_POINT );
		pDevice->SetSamplerState( 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE );
		pDevice->SetSamplerState( 0, D3DSAMP_SRGBTEXTURE, FALSE );

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

		return true;
	}
}
