#pragma once

#include <stddef.h>

namespace art
{
	namespace logic
	{
		bool ParseViewmodelFov( const char *pText, float &value );
		float CalculateWidescreenHorizontalFov( float baseFov4x3, float aspectRatio );
		float CalculateVerticalFov( float horizontalFov, float aspectRatio );
		bool ParseRgbColor( const char *pText, int &red, int &green, int &blue );
		bool IsSafeName( const char *pText );
		bool FormatCaptureFileName( char *pOutput, size_t outputBytes,
			const char *pPrefix, const char *pPassName, int frame );
		bool EncodeTgaRle( const unsigned char *pSource, size_t sourceBytes,
			unsigned char *pDestination, size_t destinationCapacity, size_t &destinationBytes );
	}
}
