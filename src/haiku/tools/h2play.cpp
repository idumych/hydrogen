/*
 * Hydrogen
 * Copyright(c) 2008-2026 The hydrogen development team [hydrogen-devel@lists.sourceforge.net]
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

// h2play SONG SECONDS [DRIVER]
//
// Test tool for the Haiku port: plays a song through an audio driver
// (default "Media Kit") for a number of seconds, then prints what the
// driver negotiated and how many xruns it counted. Used to check the
// Media Kit driver end to end before any UI exists. Set H2_SYS_DATA to
// use a data folder other than the installed one.

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <QCoreApplication>
#include <QFileInfo>

#include <core/AudioEngine/AudioEngine.h>
#include <core/Basics/Song.h>
#include <core/CoreActionController.h>
#include <core/EventQueue.h>
#include <core/Helpers/Filesystem.h>
#include <core/Hydrogen.h>
#include <core/IO/AudioDriver.h>
#include <core/Logger.h>
#include <core/Preferences/Preferences.h>

using namespace H2Core;

int main( int argc, char** argv )
{
	if ( argc < 3 ) {
		fprintf( stderr, "usage: %s SONG SECONDS [DRIVER]\n", argv[ 0 ] );
		return 2;
	}
	// The core only accepts absolute paths.
	const QString sSong = QFileInfo( QString::fromLocal8Bit( argv[ 1 ] ) ).absoluteFilePath();
	const int nSeconds = atoi( argv[ 2 ] );
	const QString sDriver = argc > 3 ? QString::fromLocal8Bit( argv[ 3 ] )
									 : QString( "mediakit" );

	QCoreApplication app( argc, argv );

	Logger* pLogger = Logger::bootstrap(
		Logger::Error | Logger::Warning | Logger::Info, "", true, false );
	Base::bootstrap( pLogger, false );
	// H2_SYS_DATA points at an uninstalled data/ folder (e.g. the source tree).
	const char* sSysData = getenv( "H2_SYS_DATA" );
	Filesystem::bootstrap( pLogger, sSysData != nullptr ? sSysData : "", "", "", "" );
	Preferences::create_instance();
	auto pPref = Preferences::get_instance();
	pPref->m_audioDriver = Preferences::parseAudioDriver( sDriver );
	if ( pPref->m_audioDriver == Preferences::AudioDriver::None ) {
		fprintf( stderr, "unknown driver [%s]\n", sDriver.toLocal8Bit().data() );
		return 2;
	}

	Hydrogen::create_instance( -1 );
	auto pHydrogen = Hydrogen::get_instance();

	auto pSong = CoreActionController::loadSong( sSong, "" );
	if ( pSong == nullptr || ! CoreActionController::setSong( pSong ) ) {
		fprintf( stderr, "unable to load song [%s]\n", sSong.toLocal8Bit().data() );
		delete pHydrogen;
		return 1;
	}

	auto pDriver = pHydrogen->getAudioEngine()->getAudioDriver();
	if ( pDriver == nullptr ) {
		fprintf( stderr, "no audio driver\n" );
		delete pHydrogen;
		return 1;
	}
	printf( "driver: %s\n", pDriver->toQString( "", true ).toLocal8Bit().data() );
	printf( "sample rate: %u, buffer: %u frames, latency: %d frames\n",
			pDriver->getSampleRate(), pDriver->getBufferSize(),
			pDriver->getLatency() );
	fflush( stdout );

	pHydrogen->sequencerPlay();
	for ( int i = 0; i < nSeconds * 10; i++ ) {
		// Drain the queue so it doesn't fill up while nothing reads it.
		while ( EventQueue::get_instance()->popEvent() != nullptr ) {
		}
		usleep( 100000 );
	}
	pHydrogen->sequencerStop();

	printf( "state after play: %s, xruns: %d\n",
			AudioEngine::StateToQString(
				pHydrogen->getAudioEngine()->getState() ).toLocal8Bit().data(),
			pDriver->getXRuns() );

	pSong = nullptr;
	pDriver = nullptr;
	delete pHydrogen;
	delete EventQueue::get_instance();
	return 0;
}
