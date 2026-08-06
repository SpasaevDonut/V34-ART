// Engine-independent parsing and FOV conversion helpers covered by unit tests.

#include "art_logic.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace art
{
	namespace logic
	{
		namespace
		{
			const char *SkipSpaces( const char *pText )
			{
				while ( pText && *pText && isspace( static_cast<unsigned char>( *pText ) ) )
					++pText;
				return pText;
			}

			bool ParseInteger( const char *&pText, int &value )
			{
				pText = SkipSpaces( pText );
				if ( !pText || !*pText )
					return false;
				char *pEnd = NULL;
				errno = 0;
				const long parsed = strtol( pText, &pEnd, 10 );
				if ( pEnd == pText || errno == ERANGE || parsed < 0 || parsed > 255 )
					return false;
				value = static_cast<int>( parsed );
				pText = pEnd;
				return true;
			}
		}

		bool ParseViewmodelFov( const char *pText, float &value )
		{
			pText = SkipSpaces( pText );
			if ( !pText || !*pText )
				return false;

			char *pEnd = NULL;
			errno = 0;
			const float parsed = strtof( pText, &pEnd );
			if ( pEnd == pText || errno == ERANGE )
				return false;
			pEnd = const_cast<char *>( SkipSpaces( pEnd ) );
			if ( *pEnd || parsed != parsed || parsed < 1.0f || parsed > 179.0f )
				return false;

			value = parsed;
			return true;
		}

		float CalculateWidescreenHorizontalFov( float baseFov4x3, float aspectRatio )
		{
			if ( baseFov4x3 <= 0.0f || baseFov4x3 >= 180.0f || aspectRatio <= 0.0f )
				return 0.0f;
			const float pi = 3.14159265358979323846f;
			const float baseAspect = 4.0f / 3.0f;
			const float radians = baseFov4x3 * pi / 180.0f;
			return 2.0f * atanf( tanf( radians * 0.5f ) *
				aspectRatio / baseAspect ) * 180.0f / pi;
		}

		float CalculateVerticalFov( float horizontalFov, float aspectRatio )
		{
			if ( horizontalFov <= 0.0f || horizontalFov >= 180.0f || aspectRatio <= 0.0f )
				return 0.0f;
			const float pi = 3.14159265358979323846f;
			const float radians = horizontalFov * pi / 180.0f;
			return 2.0f * atanf( tanf( radians * 0.5f ) /
				aspectRatio ) * 180.0f / pi;
		}

		bool ParseRgbColor( const char *pText, int &red, int &green, int &blue )
		{
			if ( !ParseInteger( pText, red ) || !ParseInteger( pText, green ) || !ParseInteger( pText, blue ) )
				return false;
			pText = SkipSpaces( pText );
			return pText && !*pText;
		}

		bool IsSafeName( const char *pText )
		{
			if ( !pText || !pText[0] )
				return false;
			for ( const unsigned char *p = reinterpret_cast<const unsigned char *>( pText ); *p; ++p )
			{
				const bool valid = ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) ||
					( *p >= '0' && *p <= '9' ) || *p == '_' || *p == '-';
				if ( !valid )
					return false;
			}
			return true;
		}

		bool FormatCaptureFileName( char *pOutput, size_t outputBytes,
			const char *pPrefix, const char *pPassName, int frame )
		{
			if ( !pOutput || !outputBytes || !pPassName || !pPassName[0] || frame < 0 )
				return false;
			const int written = pPrefix && pPrefix[0] ?
				_snprintf_s( pOutput, outputBytes, _TRUNCATE, "%s_%s_%04d.tga", pPrefix, pPassName, frame ) :
				_snprintf_s( pOutput, outputBytes, _TRUNCATE, "%s_%04d.tga", pPassName, frame );
			return written >= 0 && static_cast<size_t>( written ) < outputBytes;
		}

		bool EncodeTgaRle( const unsigned char *pSource, size_t sourceBytes,
			unsigned char *pDestination, size_t destinationCapacity, size_t &destinationBytes )
		{
			destinationBytes = 0;
			if ( !pSource || !pDestination || sourceBytes < 18 || destinationCapacity < 18 )
				return false;
			const unsigned int idLength = pSource[0];
			const unsigned int colorMapType = pSource[1];
			const unsigned int imageType = pSource[2];
			const unsigned int width = pSource[12] |
				( static_cast<unsigned int>( pSource[13] ) << 8 );
			const unsigned int height = pSource[14] |
				( static_cast<unsigned int>( pSource[15] ) << 8 );
			const unsigned int bitsPerPixel = pSource[16];
			if ( colorMapType != 0 || imageType != 2 || !width || !height ||
				( bitsPerPixel != 24 && bitsPerPixel != 32 ) )
				return false;

			const size_t bytesPerPixel = bitsPerPixel / 8;
			const size_t pixelCount = static_cast<size_t>( width ) * height;
			const size_t pixelOffset = 18 + idLength;
			if ( pixelCount > ( static_cast<size_t>( -1 ) - pixelOffset ) / bytesPerPixel )
				return false;
			const size_t rawBytes = pixelCount * bytesPerPixel;
			if ( pixelOffset + rawBytes > sourceBytes || pixelOffset > destinationCapacity )
				return false;

			memcpy( pDestination, pSource, pixelOffset );
			pDestination[2] = 10;
			size_t output = pixelOffset;
			for ( size_t row = 0; row < height; ++row )
			{
				size_t sourcePixel = row * width;
				const size_t rowEnd = sourcePixel + width;
				while ( sourcePixel < rowEnd )
				{
					const unsigned char *pCurrent =
						pSource + pixelOffset + sourcePixel * bytesPerPixel;
					size_t runLength = 1;
					while ( sourcePixel + runLength < rowEnd && runLength < 128 &&
						memcmp( pCurrent,
							pSource + pixelOffset +
								( sourcePixel + runLength ) * bytesPerPixel,
							bytesPerPixel ) == 0 )
					{
						++runLength;
					}

					if ( runLength >= 2 )
					{
						if ( output + 1 + bytesPerPixel > destinationCapacity )
							return false;
						pDestination[output++] =
							static_cast<unsigned char>( 0x80 | ( runLength - 1 ) );
						memcpy( pDestination + output, pCurrent, bytesPerPixel );
						output += bytesPerPixel;
						sourcePixel += runLength;
						continue;
					}

					const size_t rawStart = sourcePixel;
					size_t rawLength = 1;
					++sourcePixel;
					while ( sourcePixel < rowEnd && rawLength < 128 )
					{
						const unsigned char *pNext =
							pSource + pixelOffset + sourcePixel * bytesPerPixel;
						size_t nextRun = 1;
						while ( sourcePixel + nextRun < rowEnd && nextRun < 2 &&
							memcmp( pNext,
								pSource + pixelOffset +
									( sourcePixel + nextRun ) * bytesPerPixel,
								bytesPerPixel ) == 0 )
						{
							++nextRun;
						}
						if ( nextRun >= 2 )
							break;
						++rawLength;
						++sourcePixel;
					}
					const size_t packetBytes = rawLength * bytesPerPixel;
					if ( output + 1 + packetBytes > destinationCapacity )
						return false;
					pDestination[output++] = static_cast<unsigned char>( rawLength - 1 );
					memcpy( pDestination + output,
						pSource + pixelOffset + rawStart * bytesPerPixel, packetBytes );
					output += packetBytes;
				}
			}

			const size_t trailerBytes = sourceBytes - ( pixelOffset + rawBytes );
			if ( output + trailerBytes > destinationCapacity )
				return false;
			if ( trailerBytes )
			{
				memcpy( pDestination + output,
					pSource + pixelOffset + rawBytes, trailerBytes );
				output += trailerBytes;
			}
			destinationBytes = output;
			return true;
		}
	}
}
