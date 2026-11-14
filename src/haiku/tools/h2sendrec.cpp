/*
 * Hydrogen
 * Copyright(c) 2002-2008 by Alex >Comix< Cominu [comix@users.sourceforge.net]
 * Copyright(c) 2008-2026 The hydrogen development team [hydrogen-devel@lists.sourceforge.net]
 * Haiku port: Copyright(c) 2026 idumych (https://github.com/idumych)
 *
 * http://www.hydrogen-music.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see https://www.gnu.org/licenses
 *
 */

// h2sendrec SECONDS [OUTPUT]
//
// Records a running Hydrogen's "Send" output (or OUTPUT, e.g. "Main" if
// it was disconnected) with BMediaRecorder, as PowerMix records, and
// prints the frames received and their level: checks that other programs
// can take Hydrogen's audio from the Media Kit graph.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <MediaRecorder.h>
#include <MediaRoster.h>
#include <OS.h>

static int64 sFrames = 0;
static double sSquares = 0;
static float sPeak = 0;
static float sRate = 0;

static void record( void*, bigtime_t, void* pData, size_t nSize, const media_format& format )
{
	const auto& raw = format.u.raw_audio;
	if ( raw.format != media_raw_audio_format::B_AUDIO_FLOAT ) {
		return;
	}
	const float* p = static_cast<const float*>( pData );
	const size_t nSamples = nSize / sizeof( float );
	for ( size_t i = 0; i < nSamples; i++ ) {
		sSquares += p[ i ] * p[ i ];
		sPeak = std::max( sPeak, std::fabs( p[ i ] ) );
	}
	sFrames += nSamples / std::max<uint32>( 1, raw.channel_count );
	sRate = raw.frame_rate;
}

int main( int argc, char** argv )
{
	const double fSeconds = argc > 1 ? atof( argv[ 1 ] ) : 3;
	const char* sOutput = argc > 2 ? argv[ 2 ] : "Send";
	BMediaRoster* pRoster = BMediaRoster::Roster();
	// Killed programs can leave stale entries behind: take the first
	// "Hydrogen" node that answers.
	live_node_info nodes[ 8 ];
	int32 nCount = 8;
	if ( pRoster->GetLiveNodes( nodes, &nCount, nullptr, nullptr, "Hydrogen", B_BUFFER_PRODUCER ) != B_OK ||
		 nCount < 1 ) {
		fprintf( stderr, "no live \"Hydrogen\" node\n" );
		return 1;
	}
	media_output outputs[ 8 ];
	const media_output* pOutput = nullptr;
	media_node node;
	for ( int32 n = 0; n < nCount && pOutput == nullptr; n++ ) {
		int32 nOutputs = 0;
		if ( pRoster->GetFreeOutputsFor( nodes[ n ].node, outputs, 8, &nOutputs, B_MEDIA_RAW_AUDIO ) != B_OK ) {
			continue;
		}
		for ( int32 i = 0; i < nOutputs; i++ ) {
			printf( "free output: %s\n", outputs[ i ].name );
			if ( strcmp( outputs[ i ].name, sOutput ) == 0 ) {
				pOutput = &outputs[ i ];
				node = nodes[ n ].node;
			}
		}
	}
	if ( pOutput == nullptr ) {
		fprintf( stderr, "no free output \"%s\"\n", sOutput );
		return 1;
	}
	BMediaRecorder recorder( "h2sendrec", B_MEDIA_RAW_AUDIO );
	recorder.SetHooks( record, nullptr, nullptr );
	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
	status_t err = recorder.Connect( node, pOutput, &format );
	if ( err != B_OK ) {
		fprintf( stderr, "connect failed: %s\n", strerror( err ) );
		return 1;
	}
	recorder.Start();
	snooze( (bigtime_t) ( fSeconds * 1000000 ) );
	recorder.Stop();
	recorder.Disconnect();
	printf( "frames: %lld (%.2f s at %.0f Hz), rms %.4f, peak %.4f\n", (long long) sFrames,
			sRate > 0 ? sFrames / sRate : 0.0, sRate,
			sFrames > 0 ? std::sqrt( sSquares / ( sFrames * 2.0 ) ) : 0.0, sPeak );
	return 0;
}
