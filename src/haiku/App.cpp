/*
 * Hydrogen
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

#include "App.h"

#include <cstdlib>

#if __has_include(<AboutWindow.h>)
#	include <AboutWindow.h>
#else
#	include <private/interface/AboutWindow.h>
#endif
#include <Catalog.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>

#include <QCoreApplication>

#include <core/AudioEngine/AudioEngine.h>
#include <core/Basics/Song.h>
#include <core/CoreActionController.h>
#include <core/EventQueue.h>
#include <core/Helpers/Filesystem.h>
#include <core/Hydrogen.h>
#include <core/Logger.h>
#include <core/Preferences/Preferences.h>
#include <core/Version.h>

#include "MainWindow.h"
#include "Settings.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "App"

using namespace H2Core;

const char* kAppSignature = "application/x-vnd.hydrogen";

App::App()
	: BApplication( kAppSignature )
	, m_pMainWindow( nullptr )
	, m_bCoreRunning( false )
{
}

App::~App()
{
	shutdownCore();
}

void App::bootCore()
{
	// The core's settings, songs and kits go where Haiku keeps them,
	// instead of ~/.config/hydrogen and ~/.local/share/hydrogen.
	BPath configFile = Settings::directory();
	configFile.Append( "hydrogen.conf" );
	const BPath userData = Settings::userDataDirectory();
	const BPath logFile = Settings::logFile();
	// H2_SYS_DATA points at an uninstalled data/ folder (e.g. the source tree).
	const char* sSysData = getenv( "H2_SYS_DATA" );
	// The core looks for LADSPA plugins in Linux paths unless told
	// otherwise: point it at Haiku's system and user plugin folders.
	if ( getenv( "LADSPA_PATH" ) == nullptr ) {
		BString sLadspa;
		for ( directory_which which : { B_USER_NONPACKAGED_LIB_DIRECTORY, B_USER_LIB_DIRECTORY,
										B_SYSTEM_NONPACKAGED_LIB_DIRECTORY, B_SYSTEM_LIB_DIRECTORY } ) {
			BPath path;
			if ( find_directory( which, &path ) == B_OK ) {
				path.Append( "ladspa" );
				if ( ! sLadspa.IsEmpty() ) {
					sLadspa << ":";
				}
				sLadspa << path.Path();
			}
		}
		setenv( "LADSPA_PATH", sLadspa.String(), 1 );
	}

	Logger* pLogger = Logger::bootstrap( Logger::Error | Logger::Warning,
										 QString::fromUtf8( logFile.Path() ), true, false );
	Base::bootstrap( pLogger, false );
	Filesystem::bootstrap( pLogger, sSysData != nullptr ? QString::fromUtf8( sSysData ) : "",
						   QString::fromUtf8( userData.Path() ),
						   QString::fromUtf8( configFile.Path() ),
						   QString::fromUtf8( logFile.Path() ) );
	Preferences::create_instance();
	Hydrogen::create_instance( -1 );
	auto pHydrogen = Hydrogen::get_instance();
	if ( pHydrogen->getAudioDriver() == nullptr ) {
		pHydrogen->restartAudioDriver();
	}
	m_bCoreRunning = true;
}

void App::shutdownCore()
{
	if ( ! m_bCoreRunning ) {
		return;
	}
	m_bCoreRunning = false;
	auto pHydrogen = Hydrogen::get_instance();
	if ( pHydrogen->getAudioEngine()->getState() == AudioEngine::State::Playing ) {
		pHydrogen->sequencerStop();
	}
	Preferences::get_instance()->save();
	delete pHydrogen;
	delete EventQueue::get_instance();
	Preferences::get_instance()->replaceInstance( nullptr );
	delete Logger::get_instance();
}

void App::ArgvReceived( int32 argc, char** argv )
{
	for ( int32 i = 1; i < argc; i++ ) {
		BEntry entry( argv[ i ], true );
		BPath path;
		if ( entry.Exists() && entry.GetPath( &path ) == B_OK ) {
			if ( m_pMainWindow != nullptr ) {
				BMessage message( B_REFS_RECEIVED );
				entry_ref ref;
				entry.GetRef( &ref );
				message.AddRef( "refs", &ref );
				BMessenger( m_pMainWindow ).SendMessage( &message );
			}
			else {
				m_sInitialSong = path.Path();
			}
		}
	}
}

void App::RefsReceived( BMessage* pMessage )
{
	entry_ref ref;
	if ( pMessage->FindRef( "refs", &ref ) != B_OK ) {
		return;
	}
	if ( m_pMainWindow != nullptr ) {
		BMessenger( m_pMainWindow ).SendMessage( pMessage );
	}
	else {
		m_sInitialSong = BPath( &ref ).Path();
	}
}

void App::ReadyToRun()
{
	bootCore();

	// Like upstream: the song given at launch, else the last one.
	auto pPref = Preferences::get_instance();
	QString sSong = QString::fromUtf8( m_sInitialSong.String() );
	if ( sSong.isEmpty() ) {
		sSong = pPref->getLastSongPath();
	}
	std::shared_ptr<Song> pSong;
	if ( ! sSong.isEmpty() ) {
		pSong = CoreActionController::loadSong( sSong );
	}
	if ( pSong == nullptr ) {
		pSong = Song::getEmptySong();
	}
	CoreActionController::setSong( pSong );
	Hydrogen::get_instance()->setSongModified( false );

	m_pMainWindow = new MainWindow();
	m_pMainWindow->Show();
	Hydrogen::get_instance()->setGUIState( Hydrogen::GUIState::ready );
}

void App::AboutRequested()
{
	BAboutWindow* pAbout = new BAboutWindow( B_TRANSLATE_SYSTEM_NAME( "Hydrogen" ),
											 kAppSignature );
	pAbout->AddDescription( B_TRANSLATE( "An advanced drum machine." ) );
	pAbout->SetVersion( get_version().c_str() );
	const char* authors[] = { "The hydrogen development team", nullptr };
	pAbout->AddCopyright( 2002, "Alessandro Cominu", nullptr );
	pAbout->AddAuthors( authors );
	const char* porters[] = { "idumych", nullptr };
	pAbout->AddText( B_TRANSLATE( "Haiku port:" ), porters );
	pAbout->AddExtraInfo( B_TRANSLATE( "Distributed under the GNU General Public License, "
									   "version 2 or later." ) );
	pAbout->Show();
}

bool App::QuitRequested()
{
	// The main window asks about unsaved changes and quits the app
	// when it closes.
	if ( m_pMainWindow != nullptr && m_pMainWindow->Lock() ) {
		const bool bQuit = m_pMainWindow->QuitRequested();
		if ( bQuit ) {
			m_pMainWindow->Quit();
			m_pMainWindow = nullptr;
		}
		else {
			m_pMainWindow->Unlock();
		}
		return bQuit;
	}
	return true;
}

int main( int argc, char** argv )
{
	// The core needs a QCoreApplication to exist (Filesystem asserts it).
	// Its event loop never runs: only the online importer's asynchronous
	// downloads would need it, and they aren't ported yet.
	QCoreApplication qtCore( argc, argv );

	App app;
	app.Run();
	return 0;
}
