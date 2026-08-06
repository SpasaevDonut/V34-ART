// Thin host adapters for the unmodified AdvancedFX command and math cores.

#include "art_internal.h"

#include "../third_party/advancedfx/shared/AfxConsole.h"
#include "../third_party/advancedfx/shared/StringTools.h"

#include <windows.h>
#include <string.h>

namespace
{
	void AdvancedFxMessage(const char * format, ...)
	{
		char text[4096];
		va_list args;
		va_start(args, format);
		_vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
		va_end(args);
		art::ArtConsoleMessage("%s", text);
	}

	void AdvancedFxDevMessage(int, const char * format, ...)
	{
		char text[4096];
		va_list args;
		va_start(args, format);
		_vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
		va_end(args);
		art::ArtConsoleMessage("%s", text);
	}
}

namespace advancedfx
{
	Con_Printf_t Message = AdvancedFxMessage;
	Con_Printf_t Warning = AdvancedFxMessage;
	Con_DevPrintf_t DevMessage = AdvancedFxDevMessage;
	Con_DevPrintf_t DevWarning = AdvancedFxDevMessage;

	CSubCommandArgs::CSubCommandArgs(ICommandArgs * commandArgs, int offset)
		: m_Offset(offset), m_CommandArgs(commandArgs)
	{
		m_Prefix = commandArgs && 0 < commandArgs->ArgC() ? commandArgs->ArgV(0) : "";
		for (int i = 1; commandArgs && i < offset && i < commandArgs->ArgC(); ++i)
		{
			m_Prefix += " ";
			m_Prefix += commandArgs->ArgV(i);
		}
	}

	int CSubCommandArgs::ArgC()
	{
		if (!m_CommandArgs)
			return 0;
		const int result = m_CommandArgs->ArgC() - m_Offset + 1;
		return 0 < result ? result : 1;
	}

	char const * CSubCommandArgs::ArgV(int index)
	{
		if (0 == index)
			return m_Prefix.c_str();
		return m_CommandArgs ? m_CommandArgs->ArgV(index + m_Offset - 1) : "";
	}
}

bool UTF8StringToWideString(char const * utf8Chars, std::wstring & outWideString)
{
	outWideString.clear();
	if (!utf8Chars)
		return false;
	const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		utf8Chars, -1, 0, 0);
	if (count <= 0)
		return false;
	std::wstring converted(static_cast<size_t>(count), L'\0');
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		utf8Chars, -1, &converted[0], count))
		return false;
	converted.resize(static_cast<size_t>(count - 1));
	outWideString.swap(converted);
	return true;
}

bool StringBeginsWith(char const * target, char const * beginning)
{
	if (!target || !beginning)
		return false;
	const size_t beginningLength = strlen(beginning);
	return 0 == strncmp(target, beginning, beginningLength);
}
