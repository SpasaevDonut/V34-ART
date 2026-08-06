// Optional diagnostic logging and colored Source console output.

#include "art_internal.h"

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

namespace art
{
	namespace
	{
		HANDLE g_hLogFile = INVALID_HANDLE_VALUE;
		CRITICAL_SECTION g_LogLock;
		volatile LONG g_bLogLockReady = FALSE;
		volatile LONG g_bDebugLogging = FALSE;
		bool g_bLogOpenedOnce = false;
		char g_szHostExePath[MAX_PATH] = "<unknown>";
		char g_szLogPath[MAX_PATH] = "<unavailable>";
		Color g_ConsoleMessageColor( 0, 255, 0, 255 );

		void ResolveLogPath()
		{
			const DWORD exeLength = GetModuleFileNameA( NULL, g_szHostExePath, sizeof( g_szHostExePath ) );
			if ( exeLength == 0 || exeLength >= sizeof( g_szHostExePath ) )
			{
				Q_strncpy( g_szHostExePath, "<GetModuleFileNameA failed>", sizeof( g_szHostExePath ) );
				Q_strncpy( g_szLogPath, "v34_art.log", sizeof( g_szLogPath ) );
				return;
			}

			Q_strncpy( g_szLogPath, g_szHostExePath, sizeof( g_szLogPath ) );
			char *pSlash = strrchr( g_szLogPath, '\\' );
			if ( !pSlash )
				pSlash = strrchr( g_szLogPath, '/' );
			if ( pSlash && static_cast<size_t>( pSlash - g_szLogPath ) + 1 +
				strlen( "v34_art.log" ) < sizeof( g_szLogPath ) )
			{
				Q_strncpy( pSlash + 1, "v34_art.log",
					static_cast<int>( sizeof( g_szLogPath ) - ( pSlash + 1 - g_szLogPath ) ) );
			}
			else
			{
				Q_strncpy( g_szLogPath, "v34_art.log", sizeof( g_szLogPath ) );
			}
		}
	}

	void ArtConsoleMessage( const char *pFormat, ... )
	{
		char message[1024];
		va_list args;
		va_start( args, pFormat );
		_vsnprintf_s( message, sizeof( message ), _TRUNCATE, pFormat, args );
		va_end( args );

		if ( g_pGameConsole )
			g_pGameConsole->ColorPrintf( g_ConsoleMessageColor, message );
		else
			Msg( "%s", message );
	}

	void InitializeLogging()
	{
		InitializeCriticalSection( &g_LogLock );
		InterlockedExchange( &g_bLogLockReady, TRUE );
		ResolveLogPath();
	}

	void ShutdownLogging()
	{
		FlushLog();
		DisableDebugLogging();
		InterlockedExchange( &g_bLogLockReady, FALSE );
		DeleteCriticalSection( &g_LogLock );
	}

	bool EnableDebugLogging( DWORD &error )
	{
		error = ERROR_SUCCESS;
		EnterCriticalSection( &g_LogLock );
		if ( IsDebugLoggingEnabled() && g_hLogFile != INVALID_HANDLE_VALUE )
		{
			LeaveCriticalSection( &g_LogLock );
			return true;
		}

		const DWORD creation = g_bLogOpenedOnce ? OPEN_ALWAYS : CREATE_ALWAYS;
		HANDLE hLogFile = CreateFileA( g_szLogPath, GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, creation, FILE_ATTRIBUTE_NORMAL, NULL );
		if ( hLogFile == INVALID_HANDLE_VALUE )
		{
			error = GetLastError();
			LeaveCriticalSection( &g_LogLock );
			return false;
		}

		if ( g_bLogOpenedOnce )
			SetFilePointer( hLogFile, 0, NULL, FILE_END );
		g_hLogFile = hLogFile;
		g_bLogOpenedOnce = true;
		InterlockedExchange( &g_bDebugLogging, TRUE );
		LeaveCriticalSection( &g_LogLock );
		return true;
	}

	void DisableDebugLogging()
	{
		EnterCriticalSection( &g_LogLock );
		InterlockedExchange( &g_bDebugLogging, FALSE );
		if ( g_hLogFile != INVALID_HANDLE_VALUE )
		{
			FlushFileBuffers( g_hLogFile );
			CloseHandle( g_hLogFile );
			g_hLogFile = INVALID_HANDLE_VALUE;
		}
		LeaveCriticalSection( &g_LogLock );
	}

	bool IsDebugLoggingEnabled()
	{
		return InterlockedCompareExchange( &g_bDebugLogging, FALSE, FALSE ) != FALSE;
	}

	void FlushLog()
	{
		if ( !IsDebugLoggingEnabled() )
			return;

		const bool lockReady = InterlockedCompareExchange( &g_bLogLockReady, FALSE, FALSE ) != FALSE;
		if ( lockReady )
			EnterCriticalSection( &g_LogLock );
		if ( IsDebugLoggingEnabled() && g_hLogFile != INVALID_HANDLE_VALUE )
			FlushFileBuffers( g_hLogFile );
		if ( lockReady )
			LeaveCriticalSection( &g_LogLock );
	}

	void LogMessage( const char *pFormat, ... )
	{
		if ( !IsDebugLoggingEnabled() )
			return;

		char message[2048];
		va_list args;
		va_start( args, pFormat );
		_vsnprintf_s( message, sizeof( message ), _TRUNCATE, pFormat, args );
		va_end( args );

		SYSTEMTIME time;
		GetLocalTime( &time );
		char prefix[128];
		_snprintf_s( prefix, sizeof( prefix ), _TRUNCATE,
			"[%04u-%02u-%02u %02u:%02u:%02u.%03u] [pid=%lu tid=%lu] ",
			time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
			time.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId() );

		const bool lockReady = InterlockedCompareExchange( &g_bLogLockReady, FALSE, FALSE ) != FALSE;
		if ( lockReady )
			EnterCriticalSection( &g_LogLock );

		if ( IsDebugLoggingEnabled() && g_hLogFile != INVALID_HANDLE_VALUE )
		{
			DWORD written = 0;
			WriteFile( g_hLogFile, prefix, static_cast<DWORD>( strlen( prefix ) ), &written, NULL );
			WriteFile( g_hLogFile, message, static_cast<DWORD>( strlen( message ) ), &written, NULL );
			WriteFile( g_hLogFile, "\r\n", 2, &written, NULL );
			OutputDebugStringA( prefix );
			OutputDebugStringA( message );
			OutputDebugStringA( "\n" );
		}

		if ( lockReady )
			LeaveCriticalSection( &g_LogLock );
	}

	const char *GetHostExecutablePath()
	{
		return g_szHostExePath;
	}

	const char *GetLogPath()
	{
		return g_szLogPath;
	}
}
