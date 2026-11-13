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

// h2miditest SONG
//
// End-to-end test of the Midi Kit driver. A forked peer process
// registers a producer and a consumer with the MIDI roster (the roster
// only shows an application other applications' endpoints, so they
// can't live in this process). Hydrogen's MIDI input and output are
// pointed at them, then
//  1. sends four Note On messages (note 36) and checks that Hydrogen
//     handled them and mapped them to an instrument, and
//  2. starts and stops the sequencer with MIDI transport output on and
//     checks that Start and Stop arrive at its consumer.
// Prints one PASS/FAIL line per check; exits non-zero if any failed.
// Set H2_SYS_DATA to use a data folder other than the installed one.

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

#include <MidiConsumer.h>
#include <MidiProducer.h>

#include <QCoreApplication>
#include <QFileInfo>

#include <core/AudioEngine/AudioEngine.h>
#include <core/Basics/Song.h>
#include <core/CoreActionController.h>
#include <core/EventQueue.h>
#include <core/Helpers/Filesystem.h>
#include <core/Hydrogen.h>
#include <core/IO/MidiBaseDriver.h>
#include <core/Logger.h>
#include <core/Midi/MidiInstrumentMap.h>
#include <core/Preferences/Preferences.h>

using namespace H2Core;

static const char* sProducerName = "h2miditest out";
static const char* sConsumerName = "h2miditest in";

class TransportCounter : public BMidiLocalConsumer
{
public:
	TransportCounter() : BMidiLocalConsumer( sConsumerName ) {}

	void SystemRealTime( uchar nStatus, bigtime_t ) override
	{
		if ( nStatus == B_START ) {
			m_nStarts++;
		}
		else if ( nStatus == B_STOP ) {
			m_nStops++;
		}
	}

	std::atomic<int> m_nStarts{ 0 };
	std::atomic<int> m_nStops{ 0 };
};

static void command( int fd, char c )
{
	if ( write( fd, &c, 1 ) != 1 ) {
		perror( "write to peer" );
	}
}

/** The peer process: owns the test endpoints. 'n' sends four notes,
 * 'q' answers with the Start/Stop counts and exits. */
static int runPeer( int fdIn, int fdOut )
{
	auto pProducer = new BMidiLocalProducer( sProducerName );
	auto pCounter = new TransportCounter();
	const bool bOk = pProducer->Register() == B_OK && pCounter->Register() == B_OK;
	command( fdOut, bOk ? 'r' : 'e' );

	char c;
	while ( bOk && read( fdIn, &c, 1 ) == 1 ) {
		if ( c == 'n' ) {
			for ( int i = 0; i < 4; i++ ) {
				pProducer->SprayNoteOn( 0, 36, 127 );
				usleep( 250000 );
				pProducer->SprayNoteOff( 0, 36, 0 );
				usleep( 250000 );
			}
		}
		else if ( c == 'q' ) {
			const int counts[ 2 ] = { pCounter->m_nStarts.load(), pCounter->m_nStops.load() };
			if ( write( fdOut, counts, sizeof( counts ) ) != sizeof( counts ) ) {
				perror( "write counts" );
			}
			break;
		}
	}

	pProducer->Unregister();
	pProducer->Release();
	pCounter->Unregister();
	pCounter->Release();
	return bOk ? 0 : 1;
}

static void drainEvents()
{
	while ( EventQueue::get_instance()->popEvent() != nullptr ) {
	}
}

static void wait( int nMs )
{
	for ( int i = 0; i < nMs / 50; i++ ) {
		drainEvents();
		usleep( 50000 );
	}
}

static bool check( bool bOk, const char* sWhat )
{
	printf( "%s: %s\n", bOk ? "PASS" : "FAIL", sWhat );
	fflush( stdout );
	return bOk;
}

int main( int argc, char** argv )
{
	if ( argc < 2 ) {
		fprintf( stderr, "usage: %s SONG\n", argv[ 0 ] );
		return 2;
	}
	// The core only accepts absolute paths.
	const QString sSong = QFileInfo( QString::fromLocal8Bit( argv[ 1 ] ) ).absoluteFilePath();

	// Commands go to the peer through one pipe, answers come back
	// through the other. Fork before any threads exist.
	int toPeer[ 2 ], fromPeer[ 2 ];
	if ( pipe( toPeer ) != 0 || pipe( fromPeer ) != 0 ) {
		perror( "pipe" );
		return 1;
	}
	const pid_t peer = fork();
	if ( peer == 0 ) {
		close( toPeer[ 1 ] );
		close( fromPeer[ 0 ] );
		return runPeer( toPeer[ 0 ], fromPeer[ 1 ] );
	}
	close( toPeer[ 0 ] );
	close( fromPeer[ 1 ] );
	char cReady = 0;
	if ( read( fromPeer[ 0 ], &cReady, 1 ) != 1 || cReady != 'r' ) {
		fprintf( stderr, "unable to register test endpoints (is midi_server running?)\n" );
		return 1;
	}

	QCoreApplication app( argc, argv );

	Logger* pLogger = Logger::bootstrap( Logger::Error | Logger::Warning, "", true, false );
	Base::bootstrap( pLogger, false );
	const char* sSysData = getenv( "H2_SYS_DATA" );
	Filesystem::bootstrap( pLogger, sSysData != nullptr ? sSysData : "", "", "", "" );
	Preferences::create_instance();
	auto pPref = Preferences::get_instance();
	pPref->m_audioDriver = Preferences::AudioDriver::MediaKit;
	pPref->m_midiDriver = Preferences::MidiDriver::MidiKit;
	pPref->m_sMidiPortName = sProducerName;
	pPref->m_sMidiOutputPortName = sConsumerName;
	pPref->setMidiTransportOutputSend( true );
	// The bundled kits leave each instrument's MIDI channel off, so notes
	// only reach an instrument once an input channel is chosen (the
	// "All" setting in the preferences).
	pPref->getMidiInstrumentMap()->setUseGlobalInputChannel( true );
	pPref->getMidiInstrumentMap()->setGlobalInputChannel( Midi::ChannelAll );

	Hydrogen::create_instance( -1 );
	auto pHydrogen = Hydrogen::get_instance();

	auto pSong = CoreActionController::loadSong( sSong, "" );
	if ( pSong == nullptr || ! CoreActionController::setSong( pSong ) ) {
		fprintf( stderr, "unable to load song [%s]\n", sSong.toLocal8Bit().data() );
		delete pHydrogen;
		return 1;
	}

	auto pDriver = pHydrogen->getMidiDriver();
	bool bAllOk = check( pDriver != nullptr, "Midi Kit driver created" );
	if ( pDriver == nullptr ) {
		delete pHydrogen;
		return 1;
	}
	bAllOk &= check( pDriver->isInputActive() && pDriver->isOutputActive(),
					 "input and output registered" );

	bool bListed = false;
	for ( const auto& sPort : pDriver->getExternalPortList( MidiBaseDriver::PortType::Output ) ) {
		bListed |= sPort == sProducerName;
	}
	bAllOk &= check( bListed, "test producer listed as an input source" );

	pDriver->clearHandledInput();
	wait( 300 );
	command( toPeer[ 1 ], 'n' );
	wait( 2500 );

	int nNoteOns = 0, nMapped = 0;
	for ( const auto& pInput : pDriver->getHandledInputs() ) {
		if ( pInput->type == MidiMessage::Type::NoteOn ) {
			nNoteOns++;
			if ( ! pInput->mappedInstruments.isEmpty() ) {
				nMapped++;
			}
		}
	}
	printf( "handled note-ons: %d, mapped to an instrument: %d\n", nNoteOns, nMapped );
	bAllOk &= check( nNoteOns == 4, "four Note On messages received" );
	bAllOk &= check( nMapped == 4, "each Note On triggered an instrument" );

	pHydrogen->sequencerPlay();
	wait( 1500 );
	pHydrogen->sequencerStop();
	wait( 500 );
	command( toPeer[ 1 ], 'q' );
	int counts[ 2 ] = { -1, -1 };
	if ( read( fromPeer[ 0 ], counts, sizeof( counts ) ) != sizeof( counts ) ) {
		fprintf( stderr, "no answer from peer\n" );
	}
	waitpid( peer, nullptr, 0 );
	printf( "transport received: %d start, %d stop\n", counts[ 0 ], counts[ 1 ] );
	bAllOk &= check( counts[ 0 ] == 1, "MIDI Start sent on play" );
	bAllOk &= check( counts[ 1 ] == 1, "MIDI Stop sent on stop" );

	pSong = nullptr;
	pDriver = nullptr;
	delete pHydrogen;
	delete EventQueue::get_instance();

	return bAllOk ? 0 : 1;
}
