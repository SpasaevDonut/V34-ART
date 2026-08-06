#define WIN32_LEAN_AND_MEAN
// Attribution and third-party notices: ../THIRD_PARTY_NOTICES.md.
#include "../include/v34_art_version.h"

#include <windows.h>
#include <tlhelp32.h>

#include <conio.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#define ART_WIDEN_IMPL( value ) L##value
#define ART_WIDEN( value ) ART_WIDEN_IMPL( value )

namespace
{
	static const wchar_t *kTargetProcessName = L"hl2.exe";
	static const DWORD kInjectionTimeoutMs = 15000;
	static const int kSuccessCloseSeconds = 12;
	static const int kErrorCloseSeconds = 30;
	static HANDLE g_hConsole = INVALID_HANDLE_VALUE;
	static WORD g_DefaultConsoleColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

	enum LoaderColor
	{
		LOADER_COLOR_DEFAULT = 0,
		LOADER_COLOR_ORANGE,
		LOADER_COLOR_MUTED,
		LOADER_COLOR_SUCCESS,
		LOADER_COLOR_ERROR,
		LOADER_COLOR_WHITE
	};

	static void SetLoaderColor( LoaderColor color )
	{
		if ( g_hConsole == INVALID_HANDLE_VALUE )
			return;
		WORD attributes = g_DefaultConsoleColor;
		switch ( color )
		{
		case LOADER_COLOR_ORANGE:
			attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
			break;
		case LOADER_COLOR_MUTED:
			attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
			break;
		case LOADER_COLOR_SUCCESS:
			attributes = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
			break;
		case LOADER_COLOR_ERROR:
			attributes = FOREGROUND_RED | FOREGROUND_INTENSITY;
			break;
		case LOADER_COLOR_WHITE:
			attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
			break;
		default:
			break;
		}
		SetConsoleTextAttribute( g_hConsole, attributes );
	}

	static void InitializeConsole()
	{
		SetConsoleTitleW( ART_WIDEN( V34_ART_PRODUCT_NAME ) L" - Loader" );
		g_hConsole = GetStdHandle( STD_OUTPUT_HANDLE );
		if ( g_hConsole != INVALID_HANDLE_VALUE )
		{
			CONSOLE_SCREEN_BUFFER_INFO info = {};
			if ( GetConsoleScreenBufferInfo( g_hConsole, &info ) )
				g_DefaultConsoleColor = info.wAttributes;
		}
	}

	static void PrintBanner()
	{
		SetLoaderColor( LOADER_COLOR_ORANGE );
		wprintf( L"\n  +----------------------------------------------------------+\n" );
		wprintf( L"  |  " ART_WIDEN( V34_ART_PRODUCT_NAME ) L"                     |\n" );
		wprintf( L"  |  ART Loader v" ART_WIDEN( V34_ART_VERSION_STRING )
			L"  |  CS:S build 4044  |  Win32         |\n" );
		wprintf( L"  +----------------------------------------------------------+\n" );
		SetLoaderColor( LOADER_COLOR_MUTED );
		wprintf( L"  Keep the loader and matching DLL together, then start CS:S.\n\n" );
		SetLoaderColor( LOADER_COLOR_DEFAULT );
	}

	static void PrintStep( int number, const wchar_t *pName, const wchar_t *pDetail )
	{
		SetLoaderColor( LOADER_COLOR_ORANGE );
		wprintf( L"  [%d/4] %-14s", number, pName );
		SetLoaderColor( LOADER_COLOR_MUTED );
		wprintf( L"%s\n", pDetail );
		SetLoaderColor( LOADER_COLOR_DEFAULT );
	}

	static void PrintStepSuccess( const wchar_t *pDetail )
	{
		SetLoaderColor( LOADER_COLOR_SUCCESS );
		wprintf( L"        OK" );
		SetLoaderColor( LOADER_COLOR_DEFAULT );
		wprintf( L"  %s\n\n", pDetail );
	}

	class CHandle
	{
	public:
		CHandle() : m_Handle( NULL ) {}
		explicit CHandle( HANDLE handle ) : m_Handle( handle ) {}
		~CHandle()
		{
			if ( m_Handle && m_Handle != INVALID_HANDLE_VALUE )
				CloseHandle( m_Handle );
		}

		HANDLE Get() const { return m_Handle; }
		bool IsValid() const { return m_Handle && m_Handle != INVALID_HANDLE_VALUE; }

	private:
		CHandle( const CHandle & ) = delete;
		CHandle &operator=( const CHandle & ) = delete;

		HANDLE m_Handle;
	};

	static const wchar_t *BaseName( const wchar_t *pPath )
	{
		const wchar_t *pBackslash = wcsrchr( pPath, L'\\' );
		const wchar_t *pSlash = wcsrchr( pPath, L'/' );
		const wchar_t *pSeparator = pBackslash;
		if ( !pSeparator || ( pSlash && pSlash > pSeparator ) )
			pSeparator = pSlash;
		return pSeparator ? pSeparator + 1 : pPath;
	}

	static void PrintWin32Error( const wchar_t *pAction, DWORD error )
	{
		SetLoaderColor( LOADER_COLOR_ERROR );
		wchar_t *pSystemMessage = NULL;
		const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
		FormatMessageW( flags, NULL, error, 0, reinterpret_cast<wchar_t *>( &pSystemMessage ), 0, NULL );
		if ( pSystemMessage )
		{
			size_t length = wcslen( pSystemMessage );
			while ( length > 0 && ( pSystemMessage[length - 1] == L'\r' || pSystemMessage[length - 1] == L'\n' ) )
				pSystemMessage[--length] = L'\0';
			fwprintf( stderr, L"[ERROR] %s failed (Win32 error %lu): %s\n", pAction, error, pSystemMessage );
			LocalFree( pSystemMessage );
		}
		else
			fwprintf( stderr, L"[ERROR] %s failed (Win32 error %lu).\n", pAction, error );
		SetLoaderColor( LOADER_COLOR_DEFAULT );
	}

	static bool BuildMatchingDllPath( wchar_t *pDllPath, size_t pathCount )
	{
		const DWORD length = GetModuleFileNameW( NULL, pDllPath, static_cast<DWORD>( pathCount ) );
		if ( length == 0 || length >= pathCount )
		{
			PrintWin32Error( L"GetModuleFileNameW", length ? ERROR_INSUFFICIENT_BUFFER : GetLastError() );
			return false;
		}

		wchar_t *pFileName = const_cast<wchar_t *>( BaseName( pDllPath ) );
		wchar_t *pDot = wcsrchr( pFileName, L'.' );
		if ( pDot )
			*pDot = L'\0';

		if ( wcslen( pDllPath ) + 4 >= pathCount )
		{
			fwprintf( stderr, L"[ERROR] The loader path is too long to derive its matching DLL name.\n" );
			return false;
		}
		wcscat_s( pDllPath, pathCount, L".dll" );
		return true;
	}

	static DWORD FindTargetProcess()
	{
		CHandle snapshot( CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 ) );
		if ( !snapshot.IsValid() )
		{
			PrintWin32Error( L"CreateToolhelp32Snapshot(processes)", GetLastError() );
			return 0;
		}

		PROCESSENTRY32W entry = {};
		entry.dwSize = sizeof( entry );
		if ( !Process32FirstW( snapshot.Get(), &entry ) )
		{
			PrintWin32Error( L"Process32FirstW", GetLastError() );
			return 0;
		}

		do
		{
			if ( !_wcsicmp( entry.szExeFile, kTargetProcessName ) )
				return entry.th32ProcessID;
		}
		while ( Process32NextW( snapshot.Get(), &entry ) );

		return 0;
	}

	static HANDLE CreateModuleSnapshot( DWORD processId )
	{
		for ( int attempt = 0; attempt < 8; ++attempt )
		{
			HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId );
			if ( snapshot != INVALID_HANDLE_VALUE )
				return snapshot;
			if ( GetLastError() != ERROR_BAD_LENGTH )
				break;
			Sleep( 10 );
		}
		return INVALID_HANDLE_VALUE;
	}

	static uintptr_t FindRemoteModuleBase( DWORD processId, const wchar_t *pModuleName, const wchar_t *pFullPath,
		bool *pFoundMatchingDll )
	{
		if ( pFoundMatchingDll )
			*pFoundMatchingDll = false;

		CHandle snapshot( CreateModuleSnapshot( processId ) );
		if ( !snapshot.IsValid() )
			return 0;

		MODULEENTRY32W entry = {};
		entry.dwSize = sizeof( entry );
		if ( !Module32FirstW( snapshot.Get(), &entry ) )
			return 0;

		do
		{
			if ( pFoundMatchingDll && pFullPath &&
				( !_wcsicmp( entry.szExePath, pFullPath ) || !_wcsicmp( entry.szModule, BaseName( pFullPath ) ) ) )
				*pFoundMatchingDll = true;

			if ( pModuleName && !_wcsicmp( entry.szModule, pModuleName ) )
				return reinterpret_cast<uintptr_t>( entry.modBaseAddr );
		}
		while ( Module32NextW( snapshot.Get(), &entry ) );

		return 0;
	}

	static bool IsSameArchitecture( HANDLE targetProcess )
	{
		BOOL currentWow64 = FALSE;
		BOOL targetWow64 = FALSE;
		if ( !IsWow64Process( GetCurrentProcess(), &currentWow64 ) || !IsWow64Process( targetProcess, &targetWow64 ) )
		{
			PrintWin32Error( L"IsWow64Process", GetLastError() );
			return false;
		}

		if ( currentWow64 != targetWow64 )
		{
			fwprintf( stderr, L"[ERROR] Loader and hl2.exe architectures do not match. Use the Win32 loader for CSS v34.\n" );
			return false;
		}
		return true;
	}

	static LPTHREAD_START_ROUTINE ResolveRemoteLoadLibraryW( DWORD processId )
	{
		HMODULE kernel32 = GetModuleHandleW( L"kernel32.dll" );
		FARPROC loadLibraryW = kernel32 ? GetProcAddress( kernel32, "LoadLibraryW" ) : NULL;
		if ( !loadLibraryW )
		{
			PrintWin32Error( L"GetProcAddress(LoadLibraryW)", GetLastError() );
			return NULL;
		}

		MEMORY_BASIC_INFORMATION memory = {};
		if ( !VirtualQuery( reinterpret_cast<const void *>( loadLibraryW ), &memory, sizeof( memory ) ) )
		{
			PrintWin32Error( L"VirtualQuery(LoadLibraryW)", GetLastError() );
			return NULL;
		}

		HMODULE owner = static_cast<HMODULE>( memory.AllocationBase );
		wchar_t ownerPath[MAX_PATH];
		if ( !GetModuleFileNameW( owner, ownerPath, ARRAYSIZE( ownerPath ) ) )
		{
			PrintWin32Error( L"GetModuleFileNameW(LoadLibraryW owner)", GetLastError() );
			return NULL;
		}

		const uintptr_t remoteOwner = FindRemoteModuleBase( processId, BaseName( ownerPath ), NULL, NULL );
		if ( !remoteOwner )
		{
			fwprintf( stderr, L"[ERROR] Could not find %s in hl2.exe.\n", BaseName( ownerPath ) );
			return NULL;
		}

		const uintptr_t functionOffset = reinterpret_cast<uintptr_t>( loadLibraryW ) - reinterpret_cast<uintptr_t>( owner );
		return reinterpret_cast<LPTHREAD_START_ROUTINE>( remoteOwner + functionOffset );
	}

	static int LoadArtIntoGame()
	{
		PrintStep( 1, L"PACKAGE", L"Checking the matching DLL" );
		wchar_t dllPath[32768];
		if ( !BuildMatchingDllPath( dllPath, ARRAYSIZE( dllPath ) ) )
			return 1;

		const DWORD dllAttributes = GetFileAttributesW( dllPath );
		if ( dllAttributes == INVALID_FILE_ATTRIBUTES || ( dllAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
		{
			fwprintf( stderr, L"[ERROR] Matching DLL was not found. Keep %s beside this executable.\n", BaseName( dllPath ) );
			return 2;
		}
		PrintStepSuccess( BaseName( dllPath ) );

		PrintStep( 2, L"GAME", L"Looking for Counter-Strike: Source" );
		const DWORD processId = FindTargetProcess();
		if ( !processId )
		{
			fwprintf( stderr, L"[ERROR] hl2.exe is not running. Start Counter-Strike: Source first.\n" );
			return 3;
		}
		wchar_t targetDetail[96];
		swprintf_s( targetDetail, ARRAYSIZE( targetDetail ), L"hl2.exe found (PID %lu)", processId );
		PrintStepSuccess( targetDetail );

		PrintStep( 3, L"COMPATIBILITY", L"Checking access, architecture, and loaded modules" );
		const DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
			PROCESS_VM_WRITE | PROCESS_VM_READ;
		CHandle process( OpenProcess( access, FALSE, processId ) );
		if ( !process.IsValid() )
		{
			PrintWin32Error( L"OpenProcess", GetLastError() );
			fwprintf( stderr, L"Try running the loader as administrator if hl2.exe is elevated.\n" );
			return 4;
		}

		if ( !IsSameArchitecture( process.Get() ) )
			return 5;

		bool alreadyLoaded = false;
		FindRemoteModuleBase( processId, NULL, dllPath, &alreadyLoaded );
		if ( alreadyLoaded )
		{
			fwprintf( stderr, L"[ERROR] %s is already loaded in hl2.exe. Restart the game before injecting another build.\n",
				BaseName( dllPath ) );
			return 6;
		}
		PrintStepSuccess( L"Win32 target is ready; ART is not already loaded" );

		PrintStep( 4, L"INJECTION", L"Loading ART into the running game" );
		LPTHREAD_START_ROUTINE remoteLoadLibraryW = ResolveRemoteLoadLibraryW( processId );
		if ( !remoteLoadLibraryW )
			return 7;

		const SIZE_T pathBytes = ( wcslen( dllPath ) + 1 ) * sizeof( wchar_t );
		void *remotePath = VirtualAllocEx( process.Get(), NULL, pathBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
		if ( !remotePath )
		{
			PrintWin32Error( L"VirtualAllocEx", GetLastError() );
			return 8;
		}

		SIZE_T bytesWritten = 0;
		if ( !WriteProcessMemory( process.Get(), remotePath, dllPath, pathBytes, &bytesWritten ) || bytesWritten != pathBytes )
		{
			const DWORD error = GetLastError();
			VirtualFreeEx( process.Get(), remotePath, 0, MEM_RELEASE );
			PrintWin32Error( L"WriteProcessMemory", error );
			return 9;
		}

		CHandle thread( CreateRemoteThread( process.Get(), NULL, 0, remoteLoadLibraryW, remotePath, 0, NULL ) );
		if ( !thread.IsValid() )
		{
			const DWORD error = GetLastError();
			VirtualFreeEx( process.Get(), remotePath, 0, MEM_RELEASE );
			PrintWin32Error( L"CreateRemoteThread", error );
			return 10;
		}

		const DWORD waitResult = WaitForSingleObject( thread.Get(), kInjectionTimeoutMs );
		if ( waitResult != WAIT_OBJECT_0 )
		{
			if ( waitResult == WAIT_TIMEOUT )
				fwprintf( stderr, L"[ERROR] Injection did not finish within %lu milliseconds.\n", kInjectionTimeoutMs );
			else
				PrintWin32Error( L"WaitForSingleObject", GetLastError() );
			// The remote thread may still be using the path, so intentionally leave this small allocation intact.
			return 11;
		}

		DWORD remoteModule = 0;
		if ( !GetExitCodeThread( thread.Get(), &remoteModule ) )
		{
			const DWORD error = GetLastError();
			VirtualFreeEx( process.Get(), remotePath, 0, MEM_RELEASE );
			PrintWin32Error( L"GetExitCodeThread", error );
			return 12;
		}

		VirtualFreeEx( process.Get(), remotePath, 0, MEM_RELEASE );
		if ( !remoteModule )
		{
			fwprintf( stderr, L"[ERROR] LoadLibraryW returned NULL. Check that the DLL is Win32 and all dependencies are available.\n" );
			return 13;
		}

		wchar_t injectionDetail[160];
		swprintf_s( injectionDetail, ARRAYSIZE( injectionDetail ),
			L"%s loaded successfully at 0x%08lX", BaseName( dllPath ), remoteModule );
		PrintStepSuccess( injectionDetail );
		return 0;
	}
}

int wmain()
{
	InitializeConsole();
	PrintBanner();
	const int result = LoadArtIntoGame();
	if ( result == 0 )
	{
		SetLoaderColor( LOADER_COLOR_SUCCESS );
		wprintf( L"  READY\n" );
		SetLoaderColor( LOADER_COLOR_WHITE );
		wprintf( L"  ART is loaded. Return to the game and press Shift+F3.\n" );
		SetLoaderColor( LOADER_COLOR_MUTED );
		wprintf( L"  Console alternatives: art_status, art_help, art_start.\n\n" );
	}
	else
	{
		SetLoaderColor( LOADER_COLOR_ERROR );
		wprintf( L"\n  ART WAS NOT LOADED\n" );
		SetLoaderColor( LOADER_COLOR_WHITE );
		wprintf( L"  Fix the error above, then run this loader again.\n" );
		SetLoaderColor( LOADER_COLOR_MUTED );
		wprintf( L"  Common fixes: start CS:S first, keep the DLL beside the loader,\n" );
		wprintf( L"  and use the same privilege level as hl2.exe.\n\n" );
	}

	const int timeoutSeconds = result == 0 ? kSuccessCloseSeconds : kErrorCloseSeconds;
	for ( int seconds = timeoutSeconds; seconds > 0 && !_kbhit(); --seconds )
	{
		wprintf( L"\r  Press any key to close  |  automatic close in %2d seconds... ", seconds );
		fflush( stdout );
		Sleep( 1000 );
	}
	if ( _kbhit() )
		_getch();
	wprintf( L"\r                                                                  \r" );
	SetLoaderColor( LOADER_COLOR_DEFAULT );
	return result;
}
