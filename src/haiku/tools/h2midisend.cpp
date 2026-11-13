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

// h2midisend cc|note|pc NUMBER [VALUE] [CHANNEL]
//
// Sends one MIDI message to the running Hydrogen's "Hydrogen input"
// endpoint: for testing MIDI learn and actions without a controller.
// CHANNEL is 1..16 (default 1); VALUE is the CC value or note velocity
// (default 127).

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <OS.h>
#include <midi2/MidiConsumer.h>
#include <midi2/MidiProducer.h>
#include <midi2/MidiRoster.h>

int main( int argc, char** argv )
{
	if ( argc < 3 ) {
		fprintf( stderr, "usage: %s cc|note|pc NUMBER [VALUE] [CHANNEL]\n", argv[ 0 ] );
		return 2;
	}
	const int nNumber = atoi( argv[ 2 ] );
	const int nValue = argc > 3 ? atoi( argv[ 3 ] ) : 127;
	const int nChannel = ( argc > 4 ? atoi( argv[ 4 ] ) : 1 ) - 1;

	BMidiConsumer* pTarget = nullptr;
	int32 nId = 0;
	while ( BMidiEndpoint* pEndpoint = BMidiRoster::NextEndpoint( &nId ) ) {
		if ( pEndpoint->IsConsumer() && strcmp( pEndpoint->Name(), "Hydrogen input" ) == 0 ) {
			pTarget = static_cast<BMidiConsumer*>( pEndpoint );
			break;
		}
		pEndpoint->Release();
	}
	if ( pTarget == nullptr ) {
		fprintf( stderr, "\"Hydrogen input\" not found; is Hydrogen running with the Midi Kit driver?\n" );
		return 1;
	}
	BMidiLocalProducer* pProducer = new BMidiLocalProducer( "h2midisend" );
	pProducer->Register();
	pProducer->Connect( pTarget );
	snooze( 50000 );
	if ( strcmp( argv[ 1 ], "cc" ) == 0 ) {
		pProducer->SprayControlChange( nChannel, nNumber, nValue );
	}
	else if ( strcmp( argv[ 1 ], "note" ) == 0 ) {
		pProducer->SprayNoteOn( nChannel, nNumber, nValue );
		snooze( 100000 );
		pProducer->SprayNoteOff( nChannel, nNumber, 0 );
	}
	else if ( strcmp( argv[ 1 ], "pc" ) == 0 ) {
		pProducer->SprayProgramChange( nChannel, nNumber );
	}
	else {
		fprintf( stderr, "unknown message type %s\n", argv[ 1 ] );
	}
	snooze( 100000 );
	pProducer->Disconnect( pTarget );
	pProducer->Unregister();
	pProducer->Release();
	pTarget->Release();
	return 0;
}
