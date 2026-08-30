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

	namespace
	{
		class D3D9DepthCopier
		{
		public:
			D3D9DepthCopier() :
				_copyState( nullptr ),
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

				HRESULT hr = device->BeginStateBlock();
				if ( FAILED( hr ) )
					return hr;

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
				if ( !device || !srcDepthSurface || width == 0 || height == 0 )
					return D3DERR_INVALIDCALL;

				HRESULT hr = EnsureBuffers( device, width, height );
				if ( FAILED( hr ) )
					return hr;

				IDirect3DTexture9 *srcTexture = nullptr;
				hr = srcDepthSurface->GetContainer( IID_PPV_ARGS( &srcTexture ) );
				if ( FAILED( hr ) || !srcTexture )
					return hr;

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

				D3DVIEWPORT9 vp = {};
				vp.X = 0;
				vp.Y = 0;
				vp.Width = width;
				vp.Height = height;
				vp.MinZ = 0.0f;
				vp.MaxZ = 1.0f;
				device->SetViewport( &vp );

				// D3D9 half-pixel offset calculation
				const float halfPixelOffset[2] = {
					-1.0f / static_cast<float>( width ),
					 1.0f / static_cast<float>( height )
				};

				struct Vertex
				{
					float x, y, z;
					float u, v;
				};

				Vertex vertices[3] = {
					{ -1.0f,  1.0f,  0.0f,  0.0f, 0.0f },
					{  3.0f,  1.0f,  0.0f,  2.0f, 0.0f },
					{ -1.0f, -3.0f,  0.0f,  0.0f, 2.0f }
				};

				for ( int i = 0; i < 3; ++i )
				{
					vertices[i].x += halfPixelOffset[0];
					vertices[i].y += halfPixelOffset[1];
				}

				device->SetFVF( D3DFVF_XYZ | D3DFVF_TEX1 );
				device->DrawPrimitiveUP( D3DPT_TRIANGLELIST, 1, vertices, sizeof( Vertex ) );

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
			IDirect3DTexture9 *_backupTexture;
			IDirect3DSurface9 *_backupSurface;
			IDirect3DSurface9 *_stagingSurface;
			uint32_t _backupWidth;
			uint32_t _backupHeight;
		};

		bool g_bIntzSupported = false;
		bool g_bIntzChecked = false;
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

		if ( desc.Format == D3DFMT_INTZ )
		{
			pCurrentSurface->Release();
			return true;
		}

		pCurrentSurface->Release();

		if ( desc.MultiSampleType != D3DMULTISAMPLE_NONE )
			return false;

		IDirect3DTexture9 *pIntzTexture = nullptr;
		hr = pDevice->CreateTexture(
			desc.Width,
			desc.Height,
			1,
			D3DUSAGE_DEPTHSTENCIL,
			D3DFMT_INTZ,
			D3DPOOL_DEFAULT,
			&pIntzTexture,
			nullptr
		);

		if ( FAILED( hr ) || !pIntzTexture )
			return false;

		IDirect3DSurface9 *pIntzSurface = nullptr;
		hr = pIntzTexture->GetSurfaceLevel( 0, &pIntzSurface );
		if ( SUCCEEDED( hr ) && pIntzSurface )
		{
			pDevice->SetDepthStencilSurface( pIntzSurface );
			LogMessage( "D3D9 DEPTH: activated INTZ depth buffer %ux%u", desc.Width, desc.Height );
			pIntzSurface->Release();
		}
		pIntzTexture->Release();

		return SUCCEEDED( hr );
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

		if ( g_bIntzSupported && Width > 512 && Height > 512 && MultiSample == D3DMULTISAMPLE_NONE )
		{
			IDirect3DTexture9 *pTexture = nullptr;
			HRESULT hr = pDevice->CreateTexture(
				Width,
				Height,
				1,
				D3DUSAGE_DEPTHSTENCIL,
				D3DFMT_INTZ,
				D3DPOOL_DEFAULT,
				&pTexture,
				nullptr
			);

			if ( SUCCEEDED( hr ) && pTexture )
			{
				IDirect3DSurface9 *pLevel0 = nullptr;
				hr = pTexture->GetSurfaceLevel( 0, &pLevel0 );
				pTexture->Release();

				if ( SUCCEEDED( hr ) && pLevel0 )
				{
					*ppSurface = pLevel0;
					LogMessage( "D3D9 DEPTH: substituted INTZ depth-stencil surface %ux%u", Width, Height );
					return D3D_OK;
				}
			}
		}

		if ( pOriginalCreate )
		{
			return pOriginalCreate( pDevice, Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle );
		}
		return pDevice->CreateDepthStencilSurface( Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle );
	}

	void HandleD3D9Present( IDirect3DDevice9 *pDevice, uint32_t backbufferWidth, uint32_t backbufferHeight )
	{
		( void )pDevice;
		( void )backbufferWidth;
		( void )backbufferHeight;
	}

	void HandleD3D9Reset()
	{
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
		float depthEnd
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

		const float nearPlane = ( zNear > 0.0f ) ? zNear : 3.0f;
		const float farPlane = ( zFar > nearPlane ) ? zFar : 20000.0f;
		const float rangeSpan = ( depthEnd > depthStart ) ? ( depthEnd - depthStart ) : 1.0f;
		const uint32_t surfWidth = g_DepthCopier.GetWidth();
		const uint32_t surfHeight = g_DepthCopier.GetHeight();

		const unsigned char *pSrcBytes = static_cast<const unsigned char *>( locked.pBits );

		for ( int row = 0; row < height; ++row )
		{
			const int srcRow = y + row;
			if ( srcRow < 0 || srcRow >= static_cast<int>( surfHeight ) )
				continue;

			const float *pSrcRowFloat = reinterpret_cast<const float *>( pSrcBytes + srcRow * locked.Pitch );
			unsigned char *pDstRow = pOutRgb + ( row * width * 3 );

			for ( int col = 0; col < width; ++col )
			{
				const int srcCol = x + col;
				float rawDepth = 1.0f;
				if ( srcCol >= 0 && srcCol < static_cast<int>( surfWidth ) )
				{
					rawDepth = pSrcRowFloat[srcCol];
				}

				float linearZ;
				if ( rawDepth >= 0.99999f || rawDepth <= 0.0f )
				{
					linearZ = farPlane;
				}
				else
				{
					linearZ = ( nearPlane * farPlane ) / ( farPlane - rawDepth * ( farPlane - nearPlane ) );
				}

				float norm = ( linearZ - depthStart ) / rangeSpan;
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
			art_depth_end.GetFloat()
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
}
