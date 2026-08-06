// Source render hooks and synchronized multi-pass frame submission.

#include "art_gui.h"
#include "art_internal.h"
#include "art_hlae.h"

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

namespace art
{
	namespace
	{
		const int kViewRenderVtableIndex = 23;
		const int kDrawModelExVtableIndex = 19;
		const int kHudProcessInputVtableIndex = 7;
		const int kClientModeOverrideViewVtableIndex = 16;
	const int kClientModeGetViewmodelFovVtableIndex = 32;

		typedef void ( __thiscall *ViewRenderFn )( IBaseClientDLL *, vrect_t * );
		typedef int ( __thiscall *DrawModelExFn )( IVModelRender *, ModelRenderInfo_t & );
		typedef void ( __thiscall *ClientModeOverrideViewFn )( void *, CViewSetup * );
	typedef float ( __thiscall *ClientModeGetViewmodelFovFn )( void * );

		ViewRenderFn g_pOriginalViewRender = NULL;
		void **g_ppViewRenderSlot = NULL;
		DrawModelExFn g_pOriginalDrawModelEx = NULL;
		void **g_ppDrawModelExSlot = NULL;
		void *g_pClientMode = NULL;
	ClientModeOverrideViewFn g_pOriginalClientModeOverrideView = NULL;
	void **g_ppClientModeOverrideViewSlot = NULL;
		ClientModeGetViewmodelFovFn g_pOriginalClientModeGetViewmodelFov = NULL;
		void **g_ppClientModeGetViewmodelFovSlot = NULL;
		volatile LONG g_bPlayerPassActive = FALSE;
		volatile LONG g_bHidePlayersPassActive = FALSE;
		unsigned long g_nPlayerModelsAllowed = 0;
		unsigned long g_nPlayerOccludersDrawn = 0;
		unsigned long g_nPlayerFilterLogs = 0;

		struct OwnerOffsetCache
		{
			ClientClass *pClass;
			int offset;
			bool found;
		};

		OwnerOffsetCache g_OwnerOffsetCache[256];
		int g_nOwnerOffsetCacheCount = 0;

		bool IsPlayerClassName( const char *pName )
		{
			return pName && ( !Q_stricmp( pName, "CCSPlayer" ) || !Q_stricmp( pName, "CCSRagdoll" ) );
		}

		bool FindRecvPropOffset( RecvTable *pTable, const char *pName, int baseOffset,
			int recursionDepth, int &result )
		{
			if ( !pTable || recursionDepth > 16 )
				return false;
			for ( int i = 0; i < pTable->GetNumProps(); ++i )
			{
				RecvProp *pProp = pTable->GetProp( i );
				if ( !pProp )
					continue;
				const char *pPropName = pProp->GetName();
				if ( pPropName && !Q_stricmp( pPropName, pName ) )
				{
					result = baseOffset + pProp->GetOffset();
					return true;
				}
				if ( pProp->GetType() == DPT_DataTable && pProp->GetDataTable() &&
					FindRecvPropOffset( pProp->GetDataTable(), pName, baseOffset + pProp->GetOffset(),
						recursionDepth + 1, result ) )
				{
					return true;
				}
			}
			return false;
		}

		bool GetOwnerOffset( ClientClass *pClass, int &offset )
		{
			if ( !pClass )
				return false;
			for ( int i = 0; i < g_nOwnerOffsetCacheCount; ++i )
			{
				if ( g_OwnerOffsetCache[i].pClass == pClass )
				{
					offset = g_OwnerOffsetCache[i].offset;
					return g_OwnerOffsetCache[i].found;
				}
			}

			int foundOffset = 0;
			const bool found = FindRecvPropOffset( pClass->m_pRecvTable, "m_hOwnerEntity", 0, 0, foundOffset );
			if ( g_nOwnerOffsetCacheCount < ARRAYSIZE( g_OwnerOffsetCache ) )
			{
				OwnerOffsetCache &entry = g_OwnerOffsetCache[g_nOwnerOffsetCacheCount++];
				entry.pClass = pClass;
				entry.offset = foundOffset;
				entry.found = found;
			}
			offset = foundOffset;
			return found;
		}

		const char *GetEntityClassName( IClientNetworkable *pNetworkable )
		{
			ClientClass *pClass = pNetworkable ? pNetworkable->GetClientClass() : NULL;
			return pClass ? pClass->GetName() : "<none>";
		}

		bool IsPlayerPassEntity( int entityIndex )
		{
			if ( !g_pEntityList || entityIndex <= 0 )
				return false;
			IClientNetworkable *pNetworkable = g_pEntityList->GetClientNetworkable( entityIndex );
			if ( !pNetworkable )
				return false;
			ClientClass *pClass = pNetworkable->GetClientClass();
			if ( pClass && IsPlayerClassName( pClass->GetName() ) )
				return true;

			int ownerOffset = 0;
			if ( !pClass || !GetOwnerOffset( pClass, ownerOffset ) )
				return false;
			unsigned char *pBase = static_cast<unsigned char *>( pNetworkable->GetDataTableBasePtr() );
			if ( !pBase )
				return false;
			const unsigned long rawOwner = *reinterpret_cast<unsigned long *>( pBase + ownerOffset );
			CBaseHandle ownerHandle( rawOwner );
			if ( !ownerHandle.IsValid() )
				return false;
			IClientNetworkable *pOwner = g_pEntityList->GetClientNetworkableFromHandle( ownerHandle );
			return IsPlayerClassName( GetEntityClassName( pOwner ) );
		}

		int HandleHookedDrawModelEx( IVModelRender *pThis, ModelRenderInfo_t &info, DrawModelExFn pOriginalDrawModel )
		{
			if ( !pOriginalDrawModel )
				return 0;
			if ( IsArtGuiTerminating() )
				return pOriginalDrawModel( pThis, info );

			const bool playerPassActive = InterlockedCompareExchange( &g_bPlayerPassActive, FALSE, FALSE ) != FALSE;
			const bool hidePlayersActive = InterlockedCompareExchange( &g_bHidePlayersPassActive, FALSE, FALSE ) != FALSE;
			bool anyPlayerEntity = false;
			bool playerPassEntity = false;
			if ( playerPassActive || hidePlayersActive )
			{
				anyPlayerEntity = IsPlayerPassEntity( info.entity_index );
				if ( playerPassActive )
				{
					const bool worldWeaponModel = IsArtWorldWeaponModel( info );
					playerPassEntity = worldWeaponModel ? AreArtPlayersPassWorldWeaponsEnabled() : anyPlayerEntity;
				}
			}
			if ( hidePlayersActive && anyPlayerEntity )
				return 0;

			int guiResult = 0;
			if ( HandleArtGuiDrawModelEx( pThis, info, pOriginalDrawModel,
				playerPassActive, playerPassEntity, guiResult ) )
			{
				if ( playerPassEntity )
					++g_nPlayerModelsAllowed;
				return guiResult;
			}

			if ( !playerPassActive )
				return pOriginalDrawModel( pThis, info );

			IClientNetworkable *pNetworkable = g_pEntityList && info.entity_index > 0 ?
				g_pEntityList->GetClientNetworkable( info.entity_index ) : NULL;
			if ( playerPassEntity )
			{
				++g_nPlayerModelsAllowed;
				const MaterialFogMode_t previousFogMode = g_pMaterials->GetFogMode();
				g_pMaterials->FogMode( MATERIAL_FOG_NONE );
				const int result = pOriginalDrawModel( pThis, info );
				g_pMaterials->FogMode( previousFogMode );
				return result;
			}

			++g_nPlayerOccludersDrawn;
			if ( ShouldLogPassConVars() && g_nPlayerFilterLogs < 12 )
			{
				LogMessage( "PLAYER OCCLUDER DRAW: entity_index=%d class='%s' renderable=%p model=%p fog_mode=%d",
					info.entity_index, GetEntityClassName( pNetworkable ), info.pRenderable, info.pModel,
					static_cast<int>( g_pMaterials->GetFogMode() ) );
				++g_nPlayerFilterLogs;
			}
			return pOriginalDrawModel( pThis, info );
		}

		int __fastcall HookedDrawModelEx( IVModelRender *pThis, void *, ModelRenderInfo_t &info )
		{
			return HandleHookedDrawModelEx( pThis, info, g_pOriginalDrawModelEx );
		}


class HidePlayersRenderScope
		{
		public:
			HidePlayersRenderScope() { InterlockedExchange( &g_bHidePlayersPassActive, TRUE ); }
			~HidePlayersRenderScope() { InterlockedExchange( &g_bHidePlayersPassActive, FALSE ); }
		};

		class PlayerRenderFilterScope
		{
		public:
			PlayerRenderFilterScope()
			{
				g_nPlayerModelsAllowed = 0;
				g_nPlayerOccludersDrawn = 0;
				g_nPlayerFilterLogs = 0;
				InterlockedExchange( &g_bPlayerPassActive, TRUE );
			}

			~PlayerRenderFilterScope()
			{
				InterlockedExchange( &g_bPlayerPassActive, FALSE );
			}
		};

		class ObjectIdRenderScope
		{
		public:
			ObjectIdRenderScope() : active( BeginArtObjectIdPass() ) {}
			~ObjectIdRenderScope() { if ( active ) EndArtObjectIdPass(); }
			bool IsActive() const { return active; }
		private:
			bool active;
		};

		class ArtScopedStageTiming
		{
		public:
			explicit ArtScopedStageTiming( ArtTimingStage stage )
				: m_Stage( stage ), m_StartCounter( BeginArtStageTiming() ) {}
			~ArtScopedStageTiming() { EndArtStageTiming( m_Stage, m_StartCounter ); }
		private:
			ArtTimingStage m_Stage;
			unsigned __int64 m_StartCounter;
		};

		void StopAfterCaptureError()
		{
			LogMessage( "RECORDING ERROR: aborting at frame=%d root='%s'", g_nFrame, g_szTakeRoot );
			InterlockedExchange( &g_bRecording, FALSE );
			EndArtHlaeTakeExports();
			FlushArtWriteQueue( "capture error", true );
			FinishArtRecordingStatistics( true );
			ArtConsoleMessage( "art: recording aborted while writing frame %d.\n", g_nFrame );
			FlushLog();
			RunAutomaticArtValidation();
		}

		void StopAfterViewSetupError()
		{
			LogMessage( "RECORDING ERROR: aborting at frame=%d because GetPlayerView returned no usable view", g_nFrame );
			InterlockedExchange( &g_bRecording, FALSE );
			EndArtHlaeTakeExports();
			FlushArtWriteQueue( "view setup error", true );
			FinishArtRecordingStatistics( true );
			ArtConsoleMessage( "art: recording aborted because the active player view could not be obtained.\n" );
			FlushLog();
			RunAutomaticArtValidation();
		}

		void PaintFullScreenUi( const vrect_t *pRect )
		{
			CViewSetup view2d;
			view2d.x = pRect->x;
			view2d.y = pRect->y;
			view2d.width = pRect->width;
			view2d.height = pRect->height;
			Frustum uiFrustum;
			memset( uiFrustum, 0, sizeof( uiFrustum ) );
			g_pRenderView->Push2DView( view2d, 0, false, NULL, uiFrustum );
			g_pRenderView->VGui_Paint( PAINT_UIPANELS );
			g_pRenderView->PopView( uiFrustum );
		}

		void RenderPreviewPass( const CViewSetup &view, int clearFlags, const vrect_t *pRect,
			LONG preview, bool logDetails, const char *pContext )
		{
			const LONG passBit = PassBitFromPreview( preview );
			const LONG hudMask = InterlockedCompareExchange( &g_nHudMask, 0, 0 );
			const bool hudEnabled = ( hudMask & passBit ) != 0;
			if ( logDetails )
			{
				LogMessage( "PREVIEW RENDER BEGIN: pass='%s' context='%s' hud=%d hud_mask=0x%lX viewport=(%d,%d %dx%d) fov=%g viewmodel_fov=%g",
					PreviewName( preview ), pContext, hudEnabled ? 1 : 0, hudMask,
					view.x, view.y, view.width, view.height, view.fov, view.fovViewmodel );
			}

			g_pRenderView->SetMainView( view.origin, view.angles );
				if ( preview == ART_PREVIEW_NORMAL )
	{
		CConVarRestore vars;
		ApplyHudSetting( vars, hudMask, passBit );
		g_pMaterials->ClearColor4ub( 0, 0, 0, 255 );
		g_pClient->RenderViewEx( view, clearFlags,
			AddHudDrawFlag( RENDERVIEW_DRAWVIEWMODEL, hudMask, passBit ) );
	}
	else if ( preview == ART_PREVIEW_VIEWMODEL )
			{
				CViewSetup utilityView = view;
				utilityView.m_bDoBloomAndToneMapping = false;
				CConVarRestore vars;
				ApplyViewmodel( vars );
				ApplyHudSetting( vars, hudMask, passBit );
				g_pMaterials->ClearColor4ub( g_nViewmodelBackgroundRed,
					g_nViewmodelBackgroundGreen, g_nViewmodelBackgroundBlue, 255 );
				g_pClient->RenderViewEx( utilityView, clearFlags,
					AddHudDrawFlag( RENDERVIEW_DRAWVIEWMODEL, hudMask, passBit ) );
			}
			else if ( preview == ART_PREVIEW_PLAYERS )
			{
				CViewSetup utilityView = view;
				utilityView.m_bDoBloomAndToneMapping = false;
				CConVarRestore vars;
				ApplyPlayers( vars );
				ApplyHudSetting( vars, hudMask, passBit );
				g_pMaterials->ClearColor4ub( g_nPlayersBackgroundRed,
					g_nPlayersBackgroundGreen, g_nPlayersBackgroundBlue, 255 );
				{
					PlayerRenderFilterScope playerFilter;
					g_pClient->RenderViewEx( utilityView, clearFlags, AddHudDrawFlag( 0, hudMask, passBit ) );
				}
				if ( logDetails )
				{
					LogMessage( "PLAYER OCCLUSION RESULT: context='%s' players_drawn=%lu keyed_occluders_drawn=%lu",
						pContext, g_nPlayerModelsAllowed, g_nPlayerOccludersDrawn );
				}
			}
			else if ( preview == ART_PREVIEW_OBJECTID )
			{
				CViewSetup utilityView = view;
				utilityView.m_bDoBloomAndToneMapping = false;
				CConVarRestore vars;
				int worldRed = 0, worldGreen = 0, worldBlue = 0;
				int skyboxRed = 0, skyboxGreen = 0, skyboxBlue = 0;
				GetArtObjectIdColors( worldRed, worldGreen, worldBlue,
					skyboxRed, skyboxGreen, skyboxBlue );
				ApplyObjectId( vars, worldRed, worldGreen, worldBlue,
					skyboxRed, skyboxGreen, skyboxBlue );
				ApplyHudSetting( vars, hudMask, passBit );
				g_pMaterials->ClearColor4ub( skyboxRed, skyboxGreen, skyboxBlue, 255 );
				ObjectIdRenderScope objectIdScope;
				if ( objectIdScope.IsActive() )
					g_pClient->RenderViewEx( utilityView, clearFlags, AddHudDrawFlag( RENDERVIEW_DRAWVIEWMODEL, hudMask, passBit ) );
			}
			else if ( preview == ART_PREVIEW_DEPTH )
			{
				CViewSetup utilityView = view;
				utilityView.m_bDoBloomAndToneMapping = false;
				CConVarRestore vars;
				ApplyDepth( vars );
				ApplyHudSetting( vars, hudMask, passBit );
				g_pMaterials->ClearColor4ub( 255, 255, 255, 255 );
				g_pClient->RenderViewEx( utilityView, clearFlags, AddHudDrawFlag( 0, hudMask, passBit ) );
			}
			else if ( preview == ART_PREVIEW_CLEAR_NOPLAYERS )
			{
				CConVarRestore vars;
				ApplyHudSetting( vars, hudMask, passBit );
				g_pMaterials->ClearColor4ub( 0, 0, 0, 255 );
				HidePlayersRenderScope hidePlayers;
				g_pClient->RenderViewEx( view, clearFlags, AddHudDrawFlag( 0, hudMask, passBit ) );
			}
			else
			{
				CConVarRestore vars;
				ApplyHudSetting( vars, hudMask, passBit );
				g_pMaterials->ClearColor4ub( 0, 0, 0, 255 );
				g_pClient->RenderViewEx( view, clearFlags, AddHudDrawFlag( 0, hudMask, passBit ) );
			}

			PaintFullScreenUi( pRect );
			if ( logDetails )
				LogMessage( "PREVIEW RENDER COMPLETE: pass='%s' context='%s' hud=%d",
					PreviewName( preview ), pContext, hudEnabled ? 1 : 0 );
		}

		void __fastcall HookedViewRender( IBaseClientDLL *pThis, void *, vrect_t *pRect )
		{
			if ( IsArtGuiTerminating() )
			{
				if ( g_pOriginalViewRender ) g_pOriginalViewRender( pThis, pRect );
				return;
			}
			PublishArtValidationCompletion();
			MaintainArtVisualEffectsForRender();
			MaintainArtSkyboxChamsForRender();
			const unsigned long hookCall = ++g_nRenderHookCalls;
			const LONG recording = InterlockedCompareExchange( &g_bRecording, FALSE, FALSE );
			const LONG previewAtEntry = InterlockedCompareExchange( &g_nPreviewPass, ART_PREVIEW_NONE, ART_PREVIEW_NONE );
			if ( !recording && previewAtEntry == ART_PREVIEW_NONE && !IsArtGlobalFovOverrideActive() )
			{
				if ( hookCall <= 5 || hookCall % 600 == 0 )
					LogMessage( "VIEW_RENDER HOOK: call=%lu idle; forwarding original View_Render (idle calls logged first 5 and every 600)", hookCall );
				g_pOriginalViewRender( pThis, pRect );
				return;
			}

			const bool previewOnly = !recording;
			const unsigned long previewRenderCall = previewOnly ? ++g_nPreviewRenderCalls : 0;
			const bool logRender = recording || previewRenderCall <= 5 || previewRenderCall % 600 == 0;
			if ( InterlockedCompareExchange( &g_bRenderingArt, TRUE, FALSE ) != FALSE )
			{
				LogMessage( "VIEW_RENDER HOOK: call=%lu reentrant while art frame active; forwarding original View_Render", hookCall );
				g_pOriginalViewRender( pThis, pRect );
				return;
			}

			if ( logRender )
				LogMessage( "NORMAL FRAME UPDATE BEGIN: hook_call=%lu frame=%d preview_only=%d preview='%s' this=%p vrect=%p",
					hookCall, g_nFrame, previewOnly ? 1 : 0, PreviewName( previewAtEntry ), pThis, pRect );
			g_pOriginalViewRender( pThis, pRect );
			if ( logRender )
				LogMessage( "NORMAL FRAME UPDATE COMPLETE: hook_call=%lu frame=%d", hookCall, g_nFrame );

			CViewSetup view;
			const bool gotPlayerView = g_pClient->GetPlayerView( view );
			if ( logRender )
			{
				LogMessage( "GET PLAYER VIEW: success=%d viewport=(%d,%d %dx%d) fov=%g viewmodel_fov=%g zNear=%g zFar=%g",
					gotPlayerView ? 1 : 0, view.x, view.y, view.width, view.height, view.fov,
					view.fovViewmodel, view.zNear, view.zFar );
			}
			if ( !gotPlayerView || view.width <= 0 || view.height <= 0 )
			{
				if ( recording )
					StopAfterViewSetupError();
				else if ( logRender )
					LogMessage( "PREVIEW RENDER SKIPPED: GetPlayerView returned no usable view; original frame remains visible" );
				InterlockedExchange( &g_bRenderingArt, FALSE );
				return;
			}

				const bool globalFovApplied = ApplyArtGlobalFov( view );
	if ( globalFovApplied && logRender )
		LogMessage( "GLOBAL FOV APPLIED: hook_call=%lu fov=%g", hookCall, view.fov );

	const int clearFlags = VIEW_CLEAR_COLOR | VIEW_CLEAR_DEPTH;
			if ( previewOnly )
			{
				const LONG previewToRender = previewAtEntry == ART_PREVIEW_NONE ? ART_PREVIEW_NORMAL : previewAtEntry;
		RenderPreviewPass( view, clearFlags, pRect, previewToRender, logRender, "idle" );
				InterlockedExchange( &g_bRenderingArt, FALSE );
				if ( logRender )
					LogMessage( "RENDER GUARD RELEASED: hook_call=%lu preview_only=1 preview='%s'",
						hookCall, PreviewName( previewAtEntry ) );
				return;
			}

			const LONG recordMask = InterlockedCompareExchange( &g_nRecordMask, 0, 0 );
			const LONG hudMask = InterlockedCompareExchange( &g_nHudMask, 0, 0 );
			LogMessage( "ART FRAME BEGIN: hook_call=%lu frame=%d viewport=(%d,%d %dx%d) record_mask=0x%lX hud_mask=0x%lX",
				hookCall, g_nFrame, view.x, view.y, view.width, view.height, recordMask, hudMask );

			CViewSetup utilityView = view;
			utilityView.m_bDoBloomAndToneMapping = false;
				bool ok = true;
	if ( recordMask & ART_RECORD_NORMAL )
	{
		LogMessage( "PASS BEGIN: frame=%d pass='normal' clear_flags=0x%X hud=%d fov=%g",
			g_nFrame, clearFlags, ( hudMask & ART_RECORD_NORMAL ) ? 1 : 0, view.fov );
		CConVarRestore vars;
		ApplyHudSetting( vars, hudMask, ART_RECORD_NORMAL );
		g_pMaterials->ClearColor4ub( 0, 0, 0, 255 );
		{
			ArtScopedStageTiming renderTiming( ART_TIMING_RENDER );
			g_pClient->RenderViewEx( view, clearFlags,
				AddHudDrawFlag( RENDERVIEW_DRAWVIEWMODEL, hudMask, ART_RECORD_NORMAL ) );
		}
		LogMessage( "PASS RENDER COMPLETE: frame=%d pass='normal'", g_nFrame );
		ok = CaptureTga( view, "normal" );
		LogMessage( "PASS END: frame=%d pass='normal' success=%d", g_nFrame, ok ? 1 : 0 );
	}
	else
	{
		LogMessage( "PASS SKIP: frame=%d pass='normal' reason=disabled_by_art_record", g_nFrame );
	}

	if ( ok && ( recordMask & ART_RECORD_VIEWMODEL ) )
			{
				LogMessage( "PASS BEGIN: frame=%d pass='viewmodel' clear_flags=0x%X what_to_draw=RENDERVIEW_DRAWVIEWMODEL hud=%d rgb='%d %d %d' global_viewmodel_fov=%g rendered_viewmodel_fov=%g",
					g_nFrame, clearFlags, ( hudMask & ART_RECORD_VIEWMODEL ) ? 1 : 0,
					g_nViewmodelBackgroundRed, g_nViewmodelBackgroundGreen,
					g_nViewmodelBackgroundBlue, g_flViewmodelFov, utilityView.fovViewmodel );
				CConVarRestore vars;
				ApplyViewmodel( vars );
				ApplyHudSetting( vars, hudMask, ART_RECORD_VIEWMODEL );
				g_pMaterials->ClearColor4ub( g_nViewmodelBackgroundRed,
					g_nViewmodelBackgroundGreen, g_nViewmodelBackgroundBlue, 255 );
				{
					ArtScopedStageTiming renderTiming( ART_TIMING_RENDER );
					g_pClient->RenderViewEx( utilityView, clearFlags,
						AddHudDrawFlag( RENDERVIEW_DRAWVIEWMODEL, hudMask, ART_RECORD_VIEWMODEL ) );
				}
				LogMessage( "PASS RENDER COMPLETE: frame=%d pass='viewmodel'", g_nFrame );
				ok = CaptureTga( view, "viewmodel" );
				LogMessage( "PASS END: frame=%d pass='viewmodel' success=%d", g_nFrame, ok ? 1 : 0 );
			}
			else
			{
				LogMessage( "PASS SKIP: frame=%d pass='viewmodel' reason=disabled_by_art_record", g_nFrame );
			}

			if ( ok && ( recordMask & ART_RECORD_PLAYERS ) )
			{
				LogMessage( "PASS BEGIN: frame=%d pass='players' clear_flags=0x%X hud=%d rgb='%d %d %d' mode=keyed_world_depth+unfogged_players",
					g_nFrame, clearFlags, ( hudMask & ART_RECORD_PLAYERS ) ? 1 : 0,
					g_nPlayersBackgroundRed, g_nPlayersBackgroundGreen, g_nPlayersBackgroundBlue );
				CConVarRestore vars;
				ApplyPlayers( vars );
				ApplyHudSetting( vars, hudMask, ART_RECORD_PLAYERS );
				g_pMaterials->ClearColor4ub( g_nPlayersBackgroundRed,
					g_nPlayersBackgroundGreen, g_nPlayersBackgroundBlue, 255 );
				{
					PlayerRenderFilterScope playerFilter;
					ArtScopedStageTiming renderTiming( ART_TIMING_RENDER );
					g_pClient->RenderViewEx( utilityView, clearFlags,
						AddHudDrawFlag( 0, hudMask, ART_RECORD_PLAYERS ) );
				}
				LogMessage( "PASS RENDER COMPLETE: frame=%d pass='players' players_drawn=%lu keyed_occluders_drawn=%lu",
					g_nFrame, g_nPlayerModelsAllowed, g_nPlayerOccludersDrawn );
				ok = CaptureTga( view, "players" );
				LogMessage( "PASS END: frame=%d pass='players' success=%d", g_nFrame, ok ? 1 : 0 );
			}
			else if ( ok )
			{
				LogMessage( "PASS SKIP: frame=%d pass='players' reason=disabled_by_art_record", g_nFrame );
			}

			if ( ok && ( recordMask & ART_RECORD_OBJECTID ) )
			{
				LogMessage( "PASS BEGIN: frame=%d pass='objectid' clear_flags=0x%X hud=%d",
					g_nFrame, clearFlags, ( hudMask & ART_RECORD_OBJECTID ) ? 1 : 0 );
				CViewSetup utilityView = view;
				utilityView.m_bDoBloomAndToneMapping = false;
				CConVarRestore vars;
				int worldRed = 0, worldGreen = 0, worldBlue = 0;
				int skyboxRed = 0, skyboxGreen = 0, skyboxBlue = 0;
				GetArtObjectIdColors( worldRed, worldGreen, worldBlue,
					skyboxRed, skyboxGreen, skyboxBlue );
				ApplyObjectId( vars, worldRed, worldGreen, worldBlue,
					skyboxRed, skyboxGreen, skyboxBlue );
				ApplyHudSetting( vars, hudMask, ART_RECORD_OBJECTID );
				g_pMaterials->ClearColor4ub( skyboxRed, skyboxGreen, skyboxBlue, 255 );
				{
					ObjectIdRenderScope objectIdScope;
					if ( !objectIdScope.IsActive() )
						ok = false;
					else
					{
						ArtScopedStageTiming renderTiming( ART_TIMING_RENDER );
						g_pClient->RenderViewEx( utilityView, clearFlags, AddHudDrawFlag( RENDERVIEW_DRAWVIEWMODEL, hudMask, ART_RECORD_OBJECTID ) );
					}
				}
				if ( ok ) ok = CaptureTga( view, "objectid" );
				LogMessage( "PASS END: frame=%d pass='objectid' success=%d", g_nFrame, ok ? 1 : 0 );
			}
			else if ( ok )
			{
				LogMessage( "PASS SKIP: frame=%d pass='objectid' reason=disabled_by_art_record", g_nFrame );
			}

			if ( ok && ( recordMask & ART_RECORD_DEPTH ) )
			{
				LogMessage( "PASS BEGIN: frame=%d pass='depth' clear_flags=0x%X hud=%d fog_start=%g fog_end=%g",
					g_nFrame, clearFlags, ( hudMask & ART_RECORD_DEPTH ) ? 1 : 0,
					art_depth_start.GetFloat(), art_depth_end.GetFloat() );
				CConVarRestore vars;
				ApplyDepth( vars );
				ApplyHudSetting( vars, hudMask, ART_RECORD_DEPTH );
				g_pMaterials->ClearColor4ub( 255, 255, 255, 255 );
				{
					ArtScopedStageTiming renderTiming( ART_TIMING_RENDER );
					g_pClient->RenderViewEx( utilityView, clearFlags,
						AddHudDrawFlag( 0, hudMask, ART_RECORD_DEPTH ) );
				}
				LogMessage( "PASS RENDER COMPLETE: frame=%d pass='depth'", g_nFrame );
				ok = CaptureTga( view, "depth" );
				LogMessage( "PASS END: frame=%d pass='depth' success=%d", g_nFrame, ok ? 1 : 0 );
			}
			else if ( ok )
			{
				LogMessage( "PASS SKIP: frame=%d pass='depth' reason=disabled_by_art_record", g_nFrame );
			}

			if ( ok && ( recordMask & ART_RECORD_CLEAR_NOPLAYERS ) )
			{
				LogMessage( "PASS BEGIN: frame=%d pass='clear-noplayers' clear_flags=0x%X hud=%d",
					g_nFrame, clearFlags, ( hudMask & ART_RECORD_CLEAR_NOPLAYERS ) ? 1 : 0 );
				CConVarRestore vars;
				ApplyHudSetting( vars, hudMask, ART_RECORD_CLEAR_NOPLAYERS );
				g_pMaterials->ClearColor4ub( 0, 0, 0, 255 );
				{
					HidePlayersRenderScope hidePlayers;
					ArtScopedStageTiming renderTiming( ART_TIMING_RENDER );
					g_pClient->RenderViewEx( view, clearFlags, AddHudDrawFlag( 0, hudMask, ART_RECORD_CLEAR_NOPLAYERS ) );
				}
				ok = CaptureTga( view, "clear-noplayers" );
				LogMessage( "PASS END: frame=%d pass='clear-noplayers' success=%d", g_nFrame, ok ? 1 : 0 );
			}
			else if ( ok )
			{
				LogMessage( "PASS SKIP: frame=%d pass='clear-noplayers' reason=disabled_by_art_record", g_nFrame );
			}

			if ( ok && ( recordMask & ART_RECORD_CLEAR ) )
			{
				LogMessage( "PASS BEGIN: frame=%d pass='clear' clear_flags=0x%X hud=%d",
					g_nFrame, clearFlags, ( hudMask & ART_RECORD_CLEAR ) ? 1 : 0 );
				CConVarRestore vars;
				ApplyHudSetting( vars, hudMask, ART_RECORD_CLEAR );
				g_pMaterials->ClearColor4ub( 0, 0, 0, 255 );
				{
					ArtScopedStageTiming renderTiming( ART_TIMING_RENDER );
					g_pClient->RenderViewEx( view, clearFlags,
						AddHudDrawFlag( 0, hudMask, ART_RECORD_CLEAR ) );
				}
				LogMessage( "PASS RENDER COMPLETE: frame=%d pass='clear'", g_nFrame );
				ok = CaptureTga( view, "clear" );
				LogMessage( "PASS END: frame=%d pass='clear' success=%d", g_nFrame, ok ? 1 : 0 );
			}
			else if ( ok )
			{
				LogMessage( "PASS SKIP: frame=%d pass='clear' reason=disabled_by_art_record", g_nFrame );
			}

			if ( ok )
			{
				const LONG displayPreview = InterlockedCompareExchange( &g_nPreviewPass, ART_PREVIEW_NONE, ART_PREVIEW_NONE );
				if ( displayPreview != ART_PREVIEW_NONE )
				{
					RenderPreviewPass( view, clearFlags, pRect, displayPreview, true, "recording" );
				}
				else
				{
					LogMessage( "DISPLAY RESTORE BEGIN: frame=%d method=saved RenderView+UI fov=%g viewport=(%d,%d %dx%d)",
						g_nFrame, view.fov, view.x, view.y, view.width, view.height );
					g_pRenderView->SetMainView( view.origin, view.angles );
					g_pClient->RenderView( view, clearFlags, true );
					PaintFullScreenUi( pRect );
					LogMessage( "DISPLAY RESTORE COMPLETE: frame=%d method=saved RenderView+UI fov=%g", g_nFrame, view.fov );
				}
			}

			if ( ok )
			{
				++g_nFrame;
				RecordArtCompletedFrame();
				LogMessage( "ART FRAME COMPLETE: completed_frame=%d next_frame=%d", g_nFrame - 1, g_nFrame );
				FlushLog();
			}
			else
			{
				StopAfterCaptureError();
			}

			InterlockedExchange( &g_bRenderingArt, FALSE );
			LogMessage( "RENDER GUARD RELEASED: hook_call=%lu recording=%ld", hookCall,
				InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) );
		}

		bool IsClientModuleRange( const void *pAddress, SIZE_T size, bool requireExecutable )
		{
			if ( !pAddress || !size || !g_hClientModule )
				return false;
			MEMORY_BASIC_INFORMATION info;
			if ( !VirtualQuery( pAddress, &info, sizeof( info ) ) || info.AllocationBase != g_hClientModule ||
				info.State != MEM_COMMIT || ( info.Protect & PAGE_GUARD ) )
				return false;
			const DWORD baseProtection = info.Protect & 0xFF;
			if ( !baseProtection || baseProtection == PAGE_NOACCESS )
				return false;
			if ( requireExecutable && baseProtection != PAGE_EXECUTE && baseProtection != PAGE_EXECUTE_READ &&
				baseProtection != PAGE_EXECUTE_READWRITE && baseProtection != PAGE_EXECUTE_WRITECOPY )
				return false;

			const ULONG_PTR start = reinterpret_cast<ULONG_PTR>( pAddress );
			const ULONG_PTR end = start + size;
			const ULONG_PTR regionStart = reinterpret_cast<ULONG_PTR>( info.BaseAddress );
			const ULONG_PTR regionEnd = regionStart + info.RegionSize;
			return end >= start && start >= regionStart && end <= regionEnd;
		}

		bool ValidateClientModeCandidate( DWORD globalAddress, void *&pClientMode,
			void **&ppFovSlot, ClientModeGetViewmodelFovFn &pOriginalFov )
		{
			void **ppMode = reinterpret_cast<void **>( static_cast<ULONG_PTR>( globalAddress ) );
			if ( !IsClientModuleRange( ppMode, sizeof( *ppMode ), false ) )
				return false;
			void *pCandidate = *ppMode;
			if ( !IsClientModuleRange( pCandidate, sizeof( void * ), false ) )
				return false;
			void **pVtable = *reinterpret_cast<void ***>( pCandidate );
			if ( !IsClientModuleRange( pVtable,
				( kClientModeGetViewmodelFovVtableIndex + 1 ) * sizeof( void * ), false ) )
				return false;

			void **pCandidateSlot = &pVtable[kClientModeGetViewmodelFovVtableIndex];
			ClientModeGetViewmodelFovFn pCandidateFov =
				reinterpret_cast<ClientModeGetViewmodelFovFn>( *pCandidateSlot );
			if ( !IsClientModuleRange( reinterpret_cast<void *>( pCandidateFov ), 1, true ) )
				return false;
			const float originalFov = pCandidateFov( pCandidate );
			if ( originalFov < 73.9f || originalFov > 74.1f )
			{
				LogMessage( "CLIENT MODE CANDIDATE REJECTED: global=%p object=%p vtable=%p fov_slot=%p fov_function=%p returned_fov=%g expected=74",
					ppMode, pCandidate, pVtable, pCandidateSlot, pCandidateFov, originalFov );
				return false;
			}

			LogMessage( "CLIENT MODE CANDIDATE VALIDATED: global=%p object=%p vtable=%p fov_slot=%p fov_function=%p returned_fov=%g",
				ppMode, pCandidate, pVtable, pCandidateSlot, pCandidateFov, originalFov );
			pClientMode = pCandidate;
			ppFovSlot = pCandidateSlot;
			pOriginalFov = pCandidateFov;
			return true;
		}

		bool ResolveClientModeFovTarget()
		{
			void ***ppClientVtable = reinterpret_cast<void ***>( g_pClient );
			if ( !ppClientVtable || !*ppClientVtable )
			{
				LogMessage( "CLIENT MODE RESOLVE FAILED: VClient013 interface or vtable is null" );
				return false;
			}

			unsigned char *pHudProcessInput = reinterpret_cast<unsigned char *>(
				( *ppClientVtable )[kHudProcessInputVtableIndex] );
			if ( !IsClientModuleRange( pHudProcessInput, 1, true ) )
			{
				LogMessage( "CLIENT MODE RESOLVE FAILED: HudProcessInput=%p is not executable client.dll memory",
					pHudProcessInput );
				return false;
			}

			for ( int jump = 0; jump < 2 && IsClientModuleRange( pHudProcessInput, 5, true ) &&
				pHudProcessInput[0] == 0xE9; ++jump )
			{
				const LONG displacement = *reinterpret_cast<const LONG *>( pHudProcessInput + 1 );
				unsigned char *pTarget = pHudProcessInput + 5 + displacement;
				LogMessage( "CLIENT MODE RESOLVE: following HudProcessInput near jump from=%p to=%p", pHudProcessInput, pTarget );
				if ( !IsClientModuleRange( pTarget, 1, true ) )
					break;
				pHudProcessInput = pTarget;
			}

			LogMessage( "CLIENT MODE RESOLVE BEGIN: HudProcessInput=%p scan_bytes=32", pHudProcessInput );
			for ( int offset = 0; offset <= 26; ++offset )
			{
				if ( !IsClientModuleRange( pHudProcessInput + offset, 6, true ) )
					break;
				if ( pHudProcessInput[offset] == 0xCC || pHudProcessInput[offset] == 0xC3 ||
					pHudProcessInput[offset] == 0xC2 )
					break;

				DWORD globalAddress = 0;
				if ( pHudProcessInput[offset] == 0xA1 )
					globalAddress = *reinterpret_cast<const DWORD *>( pHudProcessInput + offset + 1 );
				else if ( pHudProcessInput[offset] == 0x8B &&
					( pHudProcessInput[offset + 1] & 0xC7 ) == 0x05 )
					globalAddress = *reinterpret_cast<const DWORD *>( pHudProcessInput + offset + 2 );
				else
					continue;

				LogMessage( "CLIENT MODE RESOLVE CANDIDATE: instruction=%p offset=%d opcode=0x%02X global=%p",
					pHudProcessInput + offset, offset, pHudProcessInput[offset],
					reinterpret_cast<void *>( static_cast<ULONG_PTR>( globalAddress ) ) );
				if ( ValidateClientModeCandidate( globalAddress, g_pClientMode,
					g_ppClientModeGetViewmodelFovSlot, g_pOriginalClientModeGetViewmodelFov ) )
					return true;
			}

			LogMessage( "CLIENT MODE RESOLVE FAILED: no validated g_pClientMode reference was found in HudProcessInput" );
			return false;
		}

			bool ReplaceClientModeVtableSlot( void **ppSlot, void *pExpected, void *pReplacement, const char *pName )
	{
		if ( !ppSlot || !pExpected || !pReplacement )
		{
			LogMessage( "CLIENT MODE SLOT INSTALL FAILED: name=%s slot=%p expected=%p replacement=%p",
				pName ? pName : "unknown", ppSlot, pExpected, pReplacement );
			return false;
		}
		if ( *ppSlot == pReplacement )
			return true;
		if ( *ppSlot != pExpected )
		{
			LogMessage( "CLIENT MODE SLOT INSTALL REFUSED: name=%s slot=%p current=%p expected=%p",
				pName ? pName : "unknown", ppSlot, *ppSlot, pExpected );
			return false;
		}

		DWORD oldProtect = 0;
		if ( !VirtualProtect( ppSlot, sizeof( void * ), PAGE_EXECUTE_READWRITE, &oldProtect ) )
		{
			LogMessage( "CLIENT MODE SLOT INSTALL FAILED: name=%s VirtualProtect error=%lu",
				pName ? pName : "unknown", GetLastError() );
			return false;
		}
		if ( *ppSlot != pExpected )
		{
			DWORD ignored = 0;
			VirtualProtect( ppSlot, sizeof( void * ), oldProtect, &ignored );
			LogMessage( "CLIENT MODE SLOT INSTALL RACE: name=%s slot changed to %p",
				pName ? pName : "unknown", *ppSlot );
			return false;
		}

		*ppSlot = pReplacement;
		const BOOL cacheFlushed = FlushInstructionCache( GetCurrentProcess(), ppSlot, sizeof( void * ) );
		DWORD ignored = 0;
		const BOOL protectionRestored = VirtualProtect( ppSlot, sizeof( void * ), oldProtect, &ignored );
		LogMessage( "CLIENT MODE SLOT INSTALL COMPLETE: name=%s slot=%p original=%p replacement=%p cache_flushed=%d protection_restored=%d",
			pName ? pName : "unknown", ppSlot, pExpected, pReplacement,
			cacheFlushed ? 1 : 0, protectionRestored ? 1 : 0 );
		return true;
	}

	void RestoreClientModeVtableSlot( void **ppSlot, void *pOriginal, void *pReplacement, const char *pName )
	{
		if ( !ppSlot || !pOriginal )
			return;
		if ( *ppSlot == pOriginal )
			return;
		if ( *ppSlot != pReplacement )
		{
			LogMessage( "CLIENT MODE SLOT REMOVE SKIPPED: name=%s slot=%p current=%p replacement=%p",
				pName ? pName : "unknown", ppSlot, *ppSlot, pReplacement );
			return;
		}

		DWORD oldProtect = 0;
		if ( !VirtualProtect( ppSlot, sizeof( void * ), PAGE_EXECUTE_READWRITE, &oldProtect ) )
		{
			LogMessage( "CLIENT MODE SLOT REMOVE FAILED: name=%s VirtualProtect error=%lu",
				pName ? pName : "unknown", GetLastError() );
			return;
		}
		if ( *ppSlot == pReplacement )
			*ppSlot = pOriginal;
		FlushInstructionCache( GetCurrentProcess(), ppSlot, sizeof( void * ) );
		DWORD ignored = 0;
		VirtualProtect( ppSlot, sizeof( void * ), oldProtect, &ignored );
		LogMessage( "CLIENT MODE SLOT REMOVE COMPLETE: name=%s slot=%p original=%p",
			pName ? pName : "unknown", ppSlot, pOriginal );
	}

	void __fastcall HookedClientModeOverrideView( void *pThis, void *, CViewSetup *pSetup )
	{
		if ( g_pOriginalClientModeOverrideView )
			g_pOriginalClientModeOverrideView( pThis, pSetup );
		if ( !pSetup )
			return;

		if ( InterlockedCompareExchange( &g_bGlobalFovOverride, FALSE, FALSE ) )
		{
			// HLAE-style zoom handling: apply the override only to an unzoomed view.
			if ( !InterlockedCompareExchange( &g_bGlobalFovHandleZoom, FALSE, FALSE ) ||
				pSetup->fov >= g_flGlobalFovMinUnzoomedFov )
			{
				pSetup->fov = g_flGlobalFov;
				pSetup->fovViewmodel = g_flGlobalFov;
			}
		}
	}
	float __fastcall HookedClientModeGetViewmodelFov( void *, void * )
	{
		if ( InterlockedCompareExchange( &g_bGlobalFovOverride, FALSE, FALSE ) )
			return g_flGlobalFov;
		return g_flViewmodelFov;
	}
	}

	bool InstallClientModeFovHook()
{
	LogMessage( "CLIENT MODE FOV HOOK INSTALL BEGIN: client_interface=%p hud_process_index=%d override_view_index=%d viewmodel_fov_index=%d configured_viewmodel_fov=%g",
		g_pClient, kHudProcessInputVtableIndex, kClientModeOverrideViewVtableIndex,
		kClientModeGetViewmodelFovVtableIndex, g_flViewmodelFov );
	for ( int attempt = 0; attempt < 50 && !ResolveClientModeFovTarget(); ++attempt )
	{
		if ( attempt == 0 || ( attempt + 1 ) % 10 == 0 )
			LogMessage( "CLIENT MODE FOV HOOK WAIT: g_pClientMode is not ready; attempt=%d/50 retrying in 100ms",
				attempt + 1 );
		Sleep( 100 );
	}
	if ( !g_pClientMode || !g_ppClientModeGetViewmodelFovSlot || !g_pOriginalClientModeGetViewmodelFov )
		return false;

	void **pClientModeVtable = *reinterpret_cast<void ***>( g_pClientMode );
	if ( !pClientModeVtable )
	{
		LogMessage( "CLIENT MODE FOV HOOK INSTALL FAILED: client-mode vtable is null" );
		return false;
	}
	g_ppClientModeOverrideViewSlot = &pClientModeVtable[kClientModeOverrideViewVtableIndex];
	g_pOriginalClientModeOverrideView = reinterpret_cast<ClientModeOverrideViewFn>(
		*g_ppClientModeOverrideViewSlot );
	if ( !IsClientModuleRange( reinterpret_cast<void *>( g_pOriginalClientModeOverrideView ), 1, true ) )
	{
		LogMessage( "CLIENT MODE FOV HOOK INSTALL FAILED: OverrideView function=%p is not executable client.dll memory",
			g_pOriginalClientModeOverrideView );
		g_ppClientModeOverrideViewSlot = NULL;
		g_pOriginalClientModeOverrideView = NULL;
		return false;
	}

	if ( !ReplaceClientModeVtableSlot( g_ppClientModeOverrideViewSlot,
		reinterpret_cast<void *>( g_pOriginalClientModeOverrideView ),
		reinterpret_cast<void *>( &HookedClientModeOverrideView ), "OverrideView" ) )
		return false;

	if ( !ReplaceClientModeVtableSlot( g_ppClientModeGetViewmodelFovSlot,
		reinterpret_cast<void *>( g_pOriginalClientModeGetViewmodelFov ),
		reinterpret_cast<void *>( &HookedClientModeGetViewmodelFov ), "GetViewmodelFov" ) )
	{
		RestoreClientModeVtableSlot( g_ppClientModeOverrideViewSlot,
			reinterpret_cast<void *>( g_pOriginalClientModeOverrideView ),
			reinterpret_cast<void *>( &HookedClientModeOverrideView ), "OverrideView" );
		return false;
	}

	LogMessage( "CLIENT MODE FOV HOOK INSTALL COMPLETE: object=%p override_slot=%p override_original=%p viewmodel_slot=%p viewmodel_original=%p",
		g_pClientMode, g_ppClientModeOverrideViewSlot, g_pOriginalClientModeOverrideView,
		g_ppClientModeGetViewmodelFovSlot, g_pOriginalClientModeGetViewmodelFov );
	return true;
}

void RemoveClientModeFovHook()
{
	LogMessage( "CLIENT MODE FOV HOOK REMOVE BEGIN: object=%p override_slot=%p viewmodel_slot=%p",
		g_pClientMode, g_ppClientModeOverrideViewSlot, g_ppClientModeGetViewmodelFovSlot );
	RestoreClientModeVtableSlot( g_ppClientModeGetViewmodelFovSlot,
		reinterpret_cast<void *>( g_pOriginalClientModeGetViewmodelFov ),
		reinterpret_cast<void *>( &HookedClientModeGetViewmodelFov ), "GetViewmodelFov" );
	RestoreClientModeVtableSlot( g_ppClientModeOverrideViewSlot,
		reinterpret_cast<void *>( g_pOriginalClientModeOverrideView ),
		reinterpret_cast<void *>( &HookedClientModeOverrideView ), "OverrideView" );
	g_ppClientModeOverrideViewSlot = NULL;
	g_pOriginalClientModeOverrideView = NULL;
	LogMessage( "CLIENT MODE FOV HOOK REMOVE COMPLETE" );
}

bool InstallModelRenderSlot( void **pVtable, int vtableIndex, DrawModelExFn &pOriginal,
		void **&ppSlot, void *pReplacement, const char *pLabel )
	{
		ppSlot = &pVtable[vtableIndex];
		pOriginal = reinterpret_cast<DrawModelExFn>( *ppSlot );
		LogMessage( "MODEL RENDER HOOK TARGET: method=%s index=%d slot=%p original=%p replacement=%p",
			pLabel, vtableIndex, ppSlot, pOriginal, pReplacement );

		MEMORY_BASIC_INFORMATION info;
		const SIZE_T queried = VirtualQuery( reinterpret_cast<void *>( pOriginal ), &info, sizeof( info ) );
		if ( !pOriginal || !queried || info.AllocationBase != g_hEngineModule )
		{
			LogMessage( "MODEL RENDER HOOK INSTALL FAILED: %s does not resolve inside engine.dll", pLabel );
			ppSlot = NULL;
			pOriginal = NULL;
			return false;
		}

		DWORD oldProtect = 0;
		if ( !VirtualProtect( ppSlot, sizeof( void * ), PAGE_EXECUTE_READWRITE, &oldProtect ) )
		{
			LogMessage( "MODEL RENDER HOOK INSTALL FAILED: %s VirtualProtect error=%lu", pLabel, GetLastError() );
			ppSlot = NULL;
			pOriginal = NULL;
			return false;
		}

		*ppSlot = pReplacement;
		FlushInstructionCache( GetCurrentProcess(), ppSlot, sizeof( void * ) );
		DWORD ignored = 0;
		VirtualProtect( ppSlot, sizeof( void * ), oldProtect, &ignored );
		LogMessage( "MODEL RENDER HOOK INSTALL COMPLETE: method=%s slot=%p", pLabel, ppSlot );
		return true;
	}

	void RemoveModelRenderSlot( void **&ppSlot, DrawModelExFn &pOriginal,
		void *pReplacement, const char *pLabel )
	{
		if ( !ppSlot || !pOriginal )
		{
			ppSlot = NULL;
			pOriginal = NULL;
			return;
		}

		DWORD oldProtect = 0;
		if ( VirtualProtect( ppSlot, sizeof( void * ), PAGE_EXECUTE_READWRITE, &oldProtect ) )
		{
			if ( *ppSlot == pReplacement )
			{
				*ppSlot = reinterpret_cast<void *>( pOriginal );
				LogMessage( "MODEL RENDER HOOK REMOVE: restored %s", pLabel );
			}
			else
			{
				LogMessage( "MODEL RENDER HOOK REMOVE: %s slot changed externally; value=%p", pLabel, *ppSlot );
			}
			FlushInstructionCache( GetCurrentProcess(), ppSlot, sizeof( void * ) );
			DWORD ignored = 0;
			VirtualProtect( ppSlot, sizeof( void * ), oldProtect, &ignored );
		}
		else
		{
			LogMessage( "MODEL RENDER HOOK REMOVE FAILED: %s VirtualProtect error=%lu", pLabel, GetLastError() );
		}
		ppSlot = NULL;
		pOriginal = NULL;
	}

	bool InstallModelRenderHook()
	{
		LogMessage( "MODEL RENDER HOOK INSTALL BEGIN: interface=%p draw_index=%d engine_module=%p",
			g_pModelRender, kDrawModelExVtableIndex, g_hEngineModule );
		void ***ppVtable = reinterpret_cast<void ***>( g_pModelRender );
		if ( !ppVtable || !*ppVtable )
		{
			LogMessage( "MODEL RENDER HOOK INSTALL FAILED: interface or vtable is null" );
			return false;
		}

		if ( !InstallModelRenderSlot( *ppVtable, kDrawModelExVtableIndex,
			g_pOriginalDrawModelEx, g_ppDrawModelExSlot,
			reinterpret_cast<void *>( &HookedDrawModelEx ), "DrawModelEx" ) )
		{
			return false;
		}

		LogMessage( "MODEL RENDER HOOK INSTALL COMPLETE: static_prop_hook=0" );
		return true;
	}

	void RemoveModelRenderHook()
	{
		InterlockedExchange( &g_bPlayerPassActive, FALSE );
		InterlockedExchange( &g_bHidePlayersPassActive, FALSE );
		RemoveModelRenderSlot( g_ppDrawModelExSlot, g_pOriginalDrawModelEx,
			reinterpret_cast<void *>( &HookedDrawModelEx ), "DrawModelEx" );
	}

	bool InstallViewRenderHook()
	{
		LogMessage( "VIEW_RENDER HOOK INSTALL BEGIN: client_interface=%p expected_vtable_index=%d client_module=%p",
			g_pClient, kViewRenderVtableIndex, g_hClientModule );
		void ***ppVtable = reinterpret_cast<void ***>( g_pClient );
		if ( !ppVtable || !*ppVtable )
		{
			LogMessage( "VIEW_RENDER HOOK INSTALL FAILED: client interface or vtable is null (interface=%p vtable=%p)",
				g_pClient, ppVtable ? *ppVtable : NULL );
			return false;
		}

		g_ppViewRenderSlot = &( *ppVtable )[kViewRenderVtableIndex];
		g_pOriginalViewRender = reinterpret_cast<ViewRenderFn>( *g_ppViewRenderSlot );
		LogMessage( "VIEW_RENDER HOOK TARGET: vtable=%p slot=%p original_View_Render=%p replacement=%p",
			*ppVtable, g_ppViewRenderSlot, g_pOriginalViewRender, &HookedViewRender );
		MEMORY_BASIC_INFORMATION info;
		const SIZE_T queried = VirtualQuery( reinterpret_cast<void *>( g_pOriginalViewRender ), &info, sizeof( info ) );
		LogMessage( "VIEW_RENDER HOOK VALIDATE: VirtualQuery bytes=%Iu allocation_base=%p base_address=%p region_size=%Iu state=0x%lX protect=0x%lX type=0x%lX expected_module=%p",
			queried, queried ? info.AllocationBase : NULL, queried ? info.BaseAddress : NULL,
			queried ? info.RegionSize : 0, queried ? info.State : 0, queried ? info.Protect : 0,
			queried ? info.Type : 0, g_hClientModule );
		if ( !queried || info.AllocationBase != g_hClientModule )
		{
			LogMessage( "VIEW_RENDER HOOK INSTALL FAILED: original View_Render does not resolve inside client.dll" );
			g_ppViewRenderSlot = NULL;
			g_pOriginalViewRender = NULL;
			return false;
		}

		DWORD oldProtect = 0;
		if ( !VirtualProtect( g_ppViewRenderSlot, sizeof( void * ), PAGE_EXECUTE_READWRITE, &oldProtect ) )
		{
			LogMessage( "VIEW_RENDER HOOK INSTALL FAILED: VirtualProtect RWX failed slot=%p error=%lu",
				g_ppViewRenderSlot, GetLastError() );
			return false;
		}
		LogMessage( "VIEW_RENDER HOOK PATCH: VirtualProtect succeeded slot=%p old_protect=0x%lX", g_ppViewRenderSlot, oldProtect );
		*g_ppViewRenderSlot = reinterpret_cast<void *>( &HookedViewRender );
		const BOOL cacheFlushed = FlushInstructionCache( GetCurrentProcess(), g_ppViewRenderSlot, sizeof( void * ) );
		DWORD ignored = 0;
		const BOOL protectionRestored = VirtualProtect( g_ppViewRenderSlot, sizeof( void * ), oldProtect, &ignored );
		LogMessage( "VIEW_RENDER HOOK INSTALL COMPLETE: slot_now=%p cache_flushed=%d protection_restored=%d restore_error=%lu",
			*g_ppViewRenderSlot, cacheFlushed ? 1 : 0, protectionRestored ? 1 : 0,
			protectionRestored ? 0 : GetLastError() );
		return true;
	}

	void RemoveViewRenderHook()
	{
		LogMessage( "VIEW_RENDER HOOK REMOVE BEGIN: slot=%p original=%p", g_ppViewRenderSlot, g_pOriginalViewRender );
		if ( !g_ppViewRenderSlot || !g_pOriginalViewRender )
		{
			LogMessage( "VIEW_RENDER HOOK REMOVE SKIPPED: hook state is incomplete" );
			return;
		}

		DWORD oldProtect = 0;
		if ( VirtualProtect( g_ppViewRenderSlot, sizeof( void * ), PAGE_EXECUTE_READWRITE, &oldProtect ) )
		{
			if ( *g_ppViewRenderSlot == reinterpret_cast<void *>( &HookedViewRender ) )
			{
				*g_ppViewRenderSlot = reinterpret_cast<void *>( g_pOriginalViewRender );
				LogMessage( "VIEW_RENDER HOOK REMOVE: original pointer restored" );
			}
			else
			{
				LogMessage( "VIEW_RENDER HOOK REMOVE: slot no longer points to this DLL; leaving value=%p unchanged",
					*g_ppViewRenderSlot );
			}
			FlushInstructionCache( GetCurrentProcess(), g_ppViewRenderSlot, sizeof( void * ) );
			DWORD ignored = 0;
			VirtualProtect( g_ppViewRenderSlot, sizeof( void * ), oldProtect, &ignored );
			LogMessage( "VIEW_RENDER HOOK REMOVE COMPLETE" );
		}
		else
		{
			LogMessage( "VIEW_RENDER HOOK REMOVE FAILED: VirtualProtect error=%lu", GetLastError() );
		}
	}

	bool IsViewRenderHookReady()
	{
		return g_ppViewRenderSlot && g_pOriginalViewRender &&
			*g_ppViewRenderSlot == reinterpret_cast<void *>( &HookedViewRender );
	}

	bool IsModelRenderHookReady()
	{
		return g_ppDrawModelExSlot && g_pOriginalDrawModelEx &&
			*g_ppDrawModelExSlot == reinterpret_cast<void *>( &HookedDrawModelEx );
	}

	bool IsClientModeFovHookReady()
{
	return g_pClientMode && g_ppClientModeOverrideViewSlot && g_pOriginalClientModeOverrideView &&
		*g_ppClientModeOverrideViewSlot == reinterpret_cast<void *>( &HookedClientModeOverrideView ) &&
		g_ppClientModeGetViewmodelFovSlot && g_pOriginalClientModeGetViewmodelFov &&
		*g_ppClientModeGetViewmodelFovSlot == reinterpret_cast<void *>( &HookedClientModeGetViewmodelFov );
}
void LogRenderHookState()
	{
		LogMessage( "DEBUG HOOKS: view_ready=%d view_slot=%p view_original=%p model_ready=%d model_slot=%p model_original=%p fov_ready=%d client_mode=%p fov_slot=%p fov_original=%p",
			IsViewRenderHookReady() ? 1 : 0, g_ppViewRenderSlot, g_pOriginalViewRender,
			IsModelRenderHookReady() ? 1 : 0, g_ppDrawModelExSlot, g_pOriginalDrawModelEx,
			IsClientModeFovHookReady() ? 1 : 0, g_pClientMode, g_ppClientModeGetViewmodelFovSlot,
			g_pOriginalClientModeGetViewmodelFov );
	}
}
