#include "art_logic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
	int g_failures = 0;

	void Expect( bool condition, const char *pName )
	{
		if ( condition )
			printf( "PASS: %s\n", pName );
		else
		{
			printf( "FAIL: %s\n", pName );
			++g_failures;
		}
	}
}

int main()
{
	using namespace art::logic;

	float fov = 0.0f;
	Expect( ParseViewmodelFov( "74", fov ) && fabs( fov - 74.0f ) < 0.001f,
		"viewmodel FOV accepts default" );
	Expect( ParseViewmodelFov( " 90.5 ", fov ) && fabs( fov - 90.5f ) < 0.001f,
		"viewmodel FOV accepts whitespace and decimals" );
	Expect( ParseViewmodelFov( "1", fov ) && ParseViewmodelFov( "179", fov ),
		"viewmodel FOV accepts inclusive boundaries" );
	Expect( !ParseViewmodelFov( "0", fov ) && !ParseViewmodelFov( "180", fov ) &&
		!ParseViewmodelFov( "90 degrees", fov ) && !ParseViewmodelFov( "nan", fov ),
		"viewmodel FOV rejects invalid values" );
	const float horizontalFov16x9 =
		CalculateWidescreenHorizontalFov( 90.0f, 16.0f / 9.0f );
	Expect( fabs( horizontalFov16x9 - 106.2602f ) < 0.001f,
		"Source FOV converts 90 at 16:9 to 106.26 horizontal" );
	Expect( fabs( CalculateVerticalFov( horizontalFov16x9, 16.0f / 9.0f ) -
		73.7398f ) < 0.001f,
		"Source FOV calculates the matching vertical angle" );
	Expect( CalculateWidescreenHorizontalFov( 0.0f, 16.0f / 9.0f ) == 0.0f &&
		CalculateVerticalFov( 90.0f, 0.0f ) == 0.0f,
		"Source FOV conversion rejects invalid inputs" );

	int red = 0;
	int green = 0;
	int blue = 0;
	Expect( ParseRgbColor( "0 255 0", red, green, blue ) && red == 0 && green == 255 && blue == 0,
		"RGB parser accepts default green" );
	Expect( ParseRgbColor( "255 0 255", red, green, blue ) && red == 255 && green == 0 && blue == 255,
		"RGB parser accepts inclusive boundaries" );
	Expect( !ParseRgbColor( "-1 0 0", red, green, blue ) &&
		!ParseRgbColor( "0 256 0", red, green, blue ) &&
		!ParseRgbColor( "0 255 0 extra", red, green, blue ), "RGB parser rejects invalid values" );

	Expect( IsSafeName( "dust2_Mid-01" ), "safe-name validator accepts supported characters" );
	Expect( !IsSafeName( "" ) && !IsSafeName( "bad/name" ) && !IsSafeName( "two words" ),
		"safe-name validator rejects empty and unsafe names" );

	char fileName[128];
	Expect( FormatCaptureFileName( fileName, sizeof( fileName ), "", "clear", 0 ) &&
		!strcmp( fileName, "clear_0000.tga" ), "default screenshot filename" );
	Expect( FormatCaptureFileName( fileName, sizeof( fileName ), "movie", "depth", 12 ) &&
		!strcmp( fileName, "movie_depth_0012.tga" ), "prefixed screenshot filename" );
	char tinyBuffer[8];
	Expect( !FormatCaptureFileName( tinyBuffer, sizeof( tinyBuffer ), "movie", "players", 1 ),
		"filename formatter reports truncation" );
	Expect( !FormatCaptureFileName( fileName, sizeof( fileName ), "", "clear", -1 ),
		"filename formatter rejects negative frames" );

	unsigned char repeatedTga[27] = {};
	repeatedTga[2] = 2;
	repeatedTga[12] = 3;
	repeatedTga[14] = 1;
	repeatedTga[16] = 24;
	repeatedTga[18] = 1; repeatedTga[19] = 2; repeatedTga[20] = 3;
	repeatedTga[21] = 1; repeatedTga[22] = 2; repeatedTga[23] = 3;
	repeatedTga[24] = 4; repeatedTga[25] = 5; repeatedTga[26] = 6;
	unsigned char rleOutput[64] = {};
	size_t rleBytes = 0;
	Expect( EncodeTgaRle( repeatedTga, sizeof( repeatedTga ),
		rleOutput, sizeof( rleOutput ), rleBytes ) &&
		rleBytes == 26 && rleOutput[2] == 10 &&
		rleOutput[18] == 0x81 &&
		rleOutput[19] == 1 && rleOutput[20] == 2 && rleOutput[21] == 3 &&
		rleOutput[22] == 0x00 &&
		rleOutput[23] == 4 && rleOutput[24] == 5 && rleOutput[25] == 6,
		"TGA RLE emits run and raw packets" );

	unsigned char rawTga[27] = {};
	rawTga[2] = 2;
	rawTga[12] = 3;
	rawTga[14] = 1;
	rawTga[16] = 24;
	for ( int i = 0; i < 9; ++i )
		rawTga[18 + i] = static_cast<unsigned char>( i + 1 );
	memset( rleOutput, 0, sizeof( rleOutput ) );
	rleBytes = 0;
	Expect( EncodeTgaRle( rawTga, sizeof( rawTga ),
		rleOutput, sizeof( rleOutput ), rleBytes ) &&
		rleBytes == 28 && rleOutput[18] == 0x02 &&
		!memcmp( rleOutput + 19, rawTga + 18, 9 ),
		"TGA RLE emits a raw packet for distinct pixels" );

	size_t rejectedBytes = 123;
	rawTga[2] = 10;
	Expect( !EncodeTgaRle( rawTga, sizeof( rawTga ),
		rleOutput, sizeof( rleOutput ), rejectedBytes ) && rejectedBytes == 0,
		"TGA RLE rejects compressed input" );
	rawTga[2] = 2;
	Expect( !EncodeTgaRle( rawTga, sizeof( rawTga ),
		rleOutput, 20, rejectedBytes ),
		"TGA RLE rejects insufficient output capacity" );

	unsigned char twoRowTga[36] = {};
	twoRowTga[2] = 2;
	twoRowTga[12] = 3;
	twoRowTga[14] = 2;
	twoRowTga[16] = 24;
	for ( int i = 0; i < 18; i += 3 )
	{
		twoRowTga[18 + i] = 9;
		twoRowTga[19 + i] = 8;
		twoRowTga[20 + i] = 7;
	}
	memset( rleOutput, 0, sizeof( rleOutput ) );
	rleBytes = 0;
	Expect( EncodeTgaRle( twoRowTga, sizeof( twoRowTga ),
		rleOutput, sizeof( rleOutput ), rleBytes ) &&
		rleBytes == 26 && rleOutput[18] == 0x82 &&
		rleOutput[22] == 0x82,
		"TGA RLE never crosses scanline boundaries" );

	if ( g_failures )
	{
		printf( "%d test(s) failed.\n", g_failures );
		return 1;
	}
	printf( "All ART logic tests passed.\n" );
	return 0;
}
