// TGA encoding, compression accounting, and asynchronous write-queue submission.

#include "art_internal.h"

#include <limits.h>

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

namespace art
{
	ArtQueueOptions g_ArtQueueOptions =
	{
		ART_QUEUE_DEFAULT_MAX_FILES,
		ART_QUEUE_DEFAULT_MAX_MEGABYTES,
		ART_QUEUE_DEFAULT_RESERVE_MEGABYTES
	};
	volatile LONG g_nArtTgaCompressionMode = ART_TGA_COMPRESSION_AUTO;
	ArtPipelineStatistics g_ArtPipelineStats = {};

	namespace
	{
		const unsigned __int64 kMegabyte = 1024ULL * 1024ULL;

		const char *TimingStageName( ArtTimingStage stage )
		{
			static const char *names[ART_TIMING_COUNT] =
				{ "render", "read", "encode", "write", "queue" };
			return stage >= 0 && stage < ART_TIMING_COUNT ? names[stage] : "unknown";
		}

		unsigned __int64 CounterToMicroseconds( unsigned __int64 counter )
		{
			LARGE_INTEGER frequency;
			if ( !QueryPerformanceFrequency( &frequency ) || frequency.QuadPart <= 0 )
				return 0;
			return counter * 1000000ULL / static_cast<unsigned __int64>( frequency.QuadPart );
		}

		void UpdatePeakQueue()
		{
			if ( g_ArtPipelineStats.pendingFiles > g_ArtPipelineStats.takePeakFiles )
				g_ArtPipelineStats.takePeakFiles = g_ArtPipelineStats.pendingFiles;
			if ( g_ArtPipelineStats.pendingBytes > g_ArtPipelineStats.takePeakBytes )
				g_ArtPipelineStats.takePeakBytes = g_ArtPipelineStats.pendingBytes;
			if ( g_ArtPipelineStats.pendingFiles > g_ArtPipelineStats.sessionPeakFiles )
				g_ArtPipelineStats.sessionPeakFiles = g_ArtPipelineStats.pendingFiles;
			if ( g_ArtPipelineStats.pendingBytes > g_ArtPipelineStats.sessionPeakBytes )
				g_ArtPipelineStats.sessionPeakBytes = g_ArtPipelineStats.pendingBytes;
		}

	}

	unsigned __int64 BeginArtStageTiming()
	{
		LARGE_INTEGER counter;
		if ( !QueryPerformanceCounter( &counter ) )
			return 0;
		return static_cast<unsigned __int64>( counter.QuadPart );
	}

	void EndArtStageTiming( ArtTimingStage stage, unsigned __int64 startCounter )
	{
		if ( stage < 0 || stage >= ART_TIMING_COUNT || !startCounter )
			return;
		LARGE_INTEGER counter;
		if ( !QueryPerformanceCounter( &counter ) ||
			static_cast<unsigned __int64>( counter.QuadPart ) < startCounter )
			return;
		const unsigned __int64 elapsed = CounterToMicroseconds(
			static_cast<unsigned __int64>( counter.QuadPart ) - startCounter );
		ArtStageTimingStatistics &statistics = g_ArtPipelineStats.stages[stage];
		statistics.takeTotalMicroseconds += elapsed;
		statistics.sessionTotalMicroseconds += elapsed;
		++statistics.takeSamples;
		++statistics.sessionSamples;
		const unsigned long boundedElapsed = elapsed > ULONG_MAX ?
			ULONG_MAX : static_cast<unsigned long>( elapsed );
		if ( boundedElapsed > statistics.takeMaxMicroseconds )
			statistics.takeMaxMicroseconds = boundedElapsed;
		if ( boundedElapsed > statistics.sessionMaxMicroseconds )
			statistics.sessionMaxMicroseconds = boundedElapsed;
	}

	void ResetArtPipelineTakeStatistics()
	{
		g_ArtPipelineStats.pendingFiles = 0;
		g_ArtPipelineStats.pendingBytes = 0;
		g_ArtPipelineStats.takePeakFiles = 0;
		g_ArtPipelineStats.takePeakBytes = 0;
		g_ArtPipelineStats.takeFlushes = 0;
		g_ArtPipelineStats.takeAllocationRetries = 0;
		g_ArtPipelineStats.takeAllocationFailures = 0;
		g_ArtPipelineStats.takeUncompressedBytes = 0;
		g_ArtPipelineStats.takeOutputBytes = 0;
		for ( int i = 0; i < ART_TIMING_COUNT; ++i )
		{
			g_ArtPipelineStats.stages[i].takeTotalMicroseconds = 0;
			g_ArtPipelineStats.stages[i].takeSamples = 0;
			g_ArtPipelineStats.stages[i].takeMaxMicroseconds = 0;
		}
	}

	void FlushArtWriteQueue( const char *pReason, bool force )
	{
		if ( !g_pFileSystem )
			return;
		const bool hadPendingWrites = g_ArtPipelineStats.pendingFiles != 0;
		if ( !hadPendingWrites && !force )
			return;

		LogMessage( "ASYNC QUEUE FLUSH BEGIN: reason='%s' pending_files=%lu pending_bytes=%I64u force=%d",
			pReason ? pReason : "unspecified", g_ArtPipelineStats.pendingFiles,
			g_ArtPipelineStats.pendingBytes, force ? 1 : 0 );
		const unsigned __int64 timing = BeginArtStageTiming();
		g_pFileSystem->AsyncFinishAllWrites();
		EndArtStageTiming( ART_TIMING_QUEUE, timing );
		if ( hadPendingWrites )
		{
			++g_ArtPipelineStats.takeFlushes;
			++g_ArtPipelineStats.sessionFlushes;
		}
		g_ArtPipelineStats.pendingFiles = 0;
		g_ArtPipelineStats.pendingBytes = 0;
		LogMessage( "ASYNC QUEUE FLUSH COMPLETE: reason='%s'", pReason ? pReason : "unspecified" );
	}

	bool EnsureArtQueueCapacity( size_t estimatedQueuedBytes, size_t estimatedAllocationBytes )
	{
		const unsigned __int64 maxBytes =
			static_cast<unsigned __int64>( InterlockedCompareExchange(
				&g_ArtQueueOptions.maxMegabytes, 0, 0 ) ) * kMegabyte;
		const unsigned long maxFiles = static_cast<unsigned long>( InterlockedCompareExchange(
			&g_ArtQueueOptions.maxFiles, 0, 0 ) );
		const unsigned __int64 reserveBytes =
			static_cast<unsigned __int64>( InterlockedCompareExchange(
				&g_ArtQueueOptions.reserveMegabytes, 0, 0 ) ) * kMegabyte;

		const bool fileLimit = g_ArtPipelineStats.pendingFiles > 0 &&
			g_ArtPipelineStats.pendingFiles + 1 > maxFiles;
		const bool byteLimit = g_ArtPipelineStats.pendingFiles > 0 &&
			g_ArtPipelineStats.pendingBytes + estimatedQueuedBytes > maxBytes;
		bool virtualMemoryLimit = false;
		MEMORYSTATUSEX memory = {};
		memory.dwLength = sizeof( memory );
		if ( GlobalMemoryStatusEx( &memory ) )
		{
			virtualMemoryLimit = g_ArtPipelineStats.pendingFiles > 0 &&
				memory.ullAvailVirtual < reserveBytes + estimatedAllocationBytes;
		}

		if ( fileLimit || byteLimit || virtualMemoryLimit )
		{
			const char *pReason = virtualMemoryLimit ? "virtual memory reserve" :
				fileLimit ? "file limit" : "byte limit";
			LogMessage( "ASYNC QUEUE BACKPRESSURE: reason='%s' pending_files=%lu pending_bytes=%I64u estimated_file_bytes=%Iu estimated_allocation_bytes=%Iu",
				pReason, g_ArtPipelineStats.pendingFiles, g_ArtPipelineStats.pendingBytes,
				estimatedQueuedBytes, estimatedAllocationBytes );
			FlushArtWriteQueue( pReason, false );
		}
		return true;
	}

	void NoteArtQueuedWrite( unsigned long bytes )
	{
		++g_ArtPipelineStats.pendingFiles;
		g_ArtPipelineStats.pendingBytes += bytes;
		UpdatePeakQueue();
	}

	void *AllocateArtCaptureMemory( size_t bytes, const char *pBufferName )
	{
		void *pMemory = malloc( bytes );
		if ( pMemory )
			return pMemory;

		++g_ArtPipelineStats.takeAllocationRetries;
		++g_ArtPipelineStats.sessionAllocationRetries;
		LogMessage( "CAPTURE ALLOCATION PRESSURE: buffer='%s' bytes=%Iu; flushing queue and retrying",
			pBufferName ? pBufferName : "unknown", bytes );
		FlushArtWriteQueue( "allocation retry", true );
		pMemory = malloc( bytes );
		if ( !pMemory )
		{
			++g_ArtPipelineStats.takeAllocationFailures;
			++g_ArtPipelineStats.sessionAllocationFailures;
			LogMessage( "CAPTURE ALLOCATION RETRY FAILED: buffer='%s' bytes=%Iu",
				pBufferName ? pBufferName : "unknown", bytes );
		}
		else
		{
			LogMessage( "CAPTURE ALLOCATION RETRY SUCCEEDED: buffer='%s' bytes=%Iu",
				pBufferName ? pBufferName : "unknown", bytes );
		}
		return pMemory;
	}

	bool EncodeArtTgaRle( const unsigned char *pSource, size_t sourceBytes,
		unsigned char *pDestination, size_t destinationCapacity, size_t &destinationBytes )
	{
		return logic::EncodeTgaRle( pSource, sourceBytes, pDestination,
			destinationCapacity, destinationBytes );
	}

	void RecordArtCompressionResult( unsigned long uncompressedBytes, unsigned long outputBytes )
	{
		g_ArtPipelineStats.takeUncompressedBytes += uncompressedBytes;
		g_ArtPipelineStats.takeOutputBytes += outputBytes;
		g_ArtPipelineStats.sessionUncompressedBytes += uncompressedBytes;
		g_ArtPipelineStats.sessionOutputBytes += outputBytes;
	}

	const char *ArtTgaCompressionModeName( LONG mode )
	{
		switch ( mode )
		{
		case ART_TGA_COMPRESSION_OFF: return "off";
		case ART_TGA_COMPRESSION_RLE: return "rle";
		default: return "auto";
		}
	}

	void PrintArtQueueStatus()
	{
		char pendingBytes[48];
		char peakBytes[48];
		FormatArtByteCount( g_ArtPipelineStats.pendingBytes, pendingBytes, sizeof( pendingBytes ) );
		FormatArtByteCount( g_ArtPipelineStats.takePeakBytes, peakBytes, sizeof( peakBytes ) );
		ArtConsoleMessage( "art_queue: pending upper bound %lu files / %s; take peak %lu files / %s.\n",
			g_ArtPipelineStats.pendingFiles, pendingBytes, g_ArtPipelineStats.takePeakFiles, peakBytes );
		ArtConsoleMessage( "art_queue limits: max_files %ld; max_mb %ld; reserve_mb %ld; take flushes %lu; allocation retries %lu; failures %lu.\n",
			InterlockedCompareExchange( &g_ArtQueueOptions.maxFiles, 0, 0 ),
			InterlockedCompareExchange( &g_ArtQueueOptions.maxMegabytes, 0, 0 ),
			InterlockedCompareExchange( &g_ArtQueueOptions.reserveMegabytes, 0, 0 ),
			g_ArtPipelineStats.takeFlushes, g_ArtPipelineStats.takeAllocationRetries,
			g_ArtPipelineStats.takeAllocationFailures );
	}

	void PrintArtPipelineStatistics()
	{
		for ( int i = 0; i < ART_TIMING_COUNT; ++i )
		{
			const ArtStageTimingStatistics &timing = g_ArtPipelineStats.stages[i];
			const double takeAverage = timing.takeSamples ?
				static_cast<double>( timing.takeTotalMicroseconds ) / timing.takeSamples / 1000.0 : 0.0;
			const double sessionAverage = timing.sessionSamples ?
				static_cast<double>( timing.sessionTotalMicroseconds ) / timing.sessionSamples / 1000.0 : 0.0;
			ArtConsoleMessage( "  %-6s take avg %.3f ms, max %.3f ms, total %.3f ms (%lu samples); session avg %.3f ms (%lu samples)\n",
				TimingStageName( static_cast<ArtTimingStage>( i ) ), takeAverage,
				timing.takeMaxMicroseconds / 1000.0,
				timing.takeTotalMicroseconds / 1000.0, timing.takeSamples,
				sessionAverage, timing.sessionSamples );
		}

		char uncompressed[48];
		char output[48];
		FormatArtByteCount( g_ArtPipelineStats.takeUncompressedBytes,
			uncompressed, sizeof( uncompressed ) );
		FormatArtByteCount( g_ArtPipelineStats.takeOutputBytes, output, sizeof( output ) );
		const double savings = g_ArtPipelineStats.takeUncompressedBytes ?
			100.0 * ( 1.0 - static_cast<double>( g_ArtPipelineStats.takeOutputBytes ) /
				static_cast<double>( g_ArtPipelineStats.takeUncompressedBytes ) ) : 0.0;
		ArtConsoleMessage( "  TGA compression: %s; take encoded %s -> %s (%.1f%% saved)\n",
			ArtTgaCompressionModeName( InterlockedCompareExchange(
				&g_nArtTgaCompressionMode, 0, 0 ) ), uncompressed, output, savings );
		PrintArtQueueStatus();
		ArtConsoleMessage( "  timing note: write measures async enqueue calls; queue measures backpressure/flush waits.\n" );
	}
}
