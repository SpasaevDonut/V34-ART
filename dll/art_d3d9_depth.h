#pragma once

#include "art_internal.h"
#include <d3d9.h>
#include <cstdint>

#ifndef D3DFMT_INTZ
#define D3DFMT_INTZ ((D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z'))
#endif
#ifndef D3DFMT_RAWZ
#define D3DFMT_RAWZ ((D3DFORMAT)MAKEFOURCC('R', 'A', 'W', 'Z'))
#endif
#ifndef D3DFMT_DF24
#define D3DFMT_DF24 ((D3DFORMAT)MAKEFOURCC('D', 'F', '2', '4'))
#endif

namespace art
{
	enum ArtDepthMode
	{
		ART_DEPTH_MODE_FOG = 0,
		ART_DEPTH_MODE_D3D9 = 1
	};

	static const LONG ART_DEPTH_FOG = ART_DEPTH_MODE_FOG;
	static const LONG ART_DEPTH_D3D9 = ART_DEPTH_MODE_D3D9;
	static const LONG ART_DEPTH_PRECISE = ART_DEPTH_MODE_D3D9;

	extern volatile LONG g_nArtDepthMode;

	// Mode naming & parsing
	const char *GetArtDepthModeName( LONG mode );
	LONG ArtDepthModeFromName( const char *pName );

	// Subsystem initialization and capabilities
	bool InitializeD3D9Depth( IDirect3D9 *pD3D );
	void ShutdownD3D9Depth();
	bool IsD3D9IntzSupported();
	bool IsD3D9DepthCaptureReady();

	// Direct3D 9 hook handlers
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
	);

	void HandleD3D9Present( IDirect3DDevice9 *pDevice, uint32_t backbufferWidth, uint32_t backbufferHeight );
	void HandleD3D9Reset();

	HRESULT HandleD3D9SetDepthStencilSurface(
		IDirect3DDevice9 *pDevice,
		IDirect3DSurface9 *pNewZStencil,
		HRESULT ( WINAPI *pOriginalSet )( IDirect3DDevice9 *, IDirect3DSurface9 * )
	);

	// Ensures active depth-stencil is an INTZ texture surface
	bool EnsureActiveDepthSurfaceIsIntz( IDirect3DDevice9 *pDevice );

	// Depth pixel readback & linearization
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
	);

	// Multi-pass capture helper
	bool CapturePreciseDepthPass( const CViewSetup &view, const char *pPassName );
}
