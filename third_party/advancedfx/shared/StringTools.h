#pragma once

#include <string>

bool UTF8StringToWideString(char const * utf8Chars, std::wstring & outWideString);
bool StringBeginsWith(char const * target, char const * beginning);
