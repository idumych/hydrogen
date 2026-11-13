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

#include "MainWindow.h"

#include <cmath>

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MessageRunner.h>
#include <Path.h>
#include <Screen.h>
#include <SplitView.h>
#include <Url.h>

#if __has_include(<DecimalSpinner.h>)
#	include <DecimalSpinner.h>
#else
#	include <private/interface/DecimalSpinner.h>
#endif

#include <core/AudioEngine/AudioEngine.h>
#include <core/AudioEngine/Transport.h>
#include <core/Basics/Event.h>
#include <core/Basics/Song.h>
#include <core/CoreActionController.h>
#include <core/EventQueue.h>
#include <core/Helpers/Filesystem.h>
#include <core/Hydrogen.h>
#include <core/IO/NullDriver.h>
#include <core/Preferences/Preferences.h>

#include "Colors.h"
#include "Messages.h"
#include "PatternActions.h"
#include "PatternEditor.h"
#include "Rack.h"
#include "SongEditor.h"
#include "Settings.h"
#include "TransportBar.h"
#include "UndoStack.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"

using namespace H2Core;

static const char* kFrameName = "main window frame";
static const char* kSplitName = "main window split";
static const char* kRackHiddenName = "rack hidden";

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

static QString toQString( const BString& s )
{
	return QString::fromUtf8( s.String() );
}

/** Shows folders and Hydrogen songs in the open panel. */
class SongFilter : public BRefFilter
{
public:
	bool Filter( const entry_ref* pRef, BNode* pNode, struct stat_beos*,
				 const char* ) override
	{
		if ( pNode->IsDirectory() ) {
			return true;
		}
		BEntry entry( pRef, true );
		if ( entry.IsDirectory() ) {
			return true;
		}
		return BString( pRef->name ).IEndsWith(
			Filesystem::sSongSuffix.toUtf8().constData() );
	}
};

MainWindow::MainWindow()
	: BWindow( Settings::frame( kFrameName, BRect( 0, 0, 1000, 720 ) ),
			   B_TRANSLATE_SYSTEM_NAME( "Hydrogen" ), B_DOCUMENT_WINDOW,
			   B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE )
	, m_pOpenPanel( nullptr )
	, m_pSavePanel( nullptr )
	, m_pAfterSave( nullptr )
{
	BMenuBar* pMenuBar = new BMenuBar( "menu bar" );
	pMenuBar->AddItem( buildFileMenu() );
	pMenuBar->AddItem( buildEditMenu() );
	pMenuBar->AddItem( buildPatternMenu() );
	pMenuBar->AddItem( buildDrumkitMenu() );
	pMenuBar->AddItem( buildViewMenu() );
	pMenuBar->AddItem( buildOptionsMenu() );
	pMenuBar->AddItem( buildDebugMenu() );
	pMenuBar->AddItem( buildHelpMenu() );

	m_pTransportBar = new TransportBar();

	// Sizes from REFERENCE.md: song editor ~290 px high, rack 300 px wide.
	m_pSongEditor = new SongEditor();
	m_pPatternEditor = new PatternEditor();
	m_pRack = new Rack();
	if ( Settings::get().GetBool( kRackHiddenName, false ) ) {
		m_pRack->Hide();
	}

	m_pSplitView = new BSplitView( B_VERTICAL, 0 );
	BLayoutBuilder::Split<>( m_pSplitView )
		.Add( m_pSongEditor, 0.4f )
		.AddGroup( B_HORIZONTAL, 0, 0.6f )
			.Add( m_pPatternEditor )
			.Add( m_pRack )
		.End();
	float fWeight;
	for ( int32 i = 0; i < 2; i++ ) {
		if ( Settings::get().FindFloat( kSplitName, i, &fWeight ) == B_OK && fWeight > 0 ) {
			m_pSplitView->SetItemWeight( i, fWeight, false );
		}
	}

	BLayoutBuilder::Group<>( this, B_VERTICAL, 0 )
		.Add( pMenuBar )
		.Add( m_pTransportBar )
		.Add( m_pSplitView );

	if ( ! Settings::get().HasRect( kFrameName ) ) {
		CenterOnScreen();
	}

	updateTitle();
	updateRecentMenu();
	updateUndoItems();
	PatternActions::setObserver( BMessenger( this ) );

	BMessage poll( kMsgPollCore );
	m_pPollRunner = new BMessageRunner( BMessenger( this ), &poll, 50000 );
}

MainWindow::~MainWindow()
{
	delete m_pPollRunner;
	delete m_pOpenPanel;
	delete m_pSavePanel;
	delete m_pAfterSave;
}

BMenu* MainWindow::buildFileMenu()
{
	BMenu* pMenu = new BMenu( B_TRANSLATE( "File" ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "New" ), new BMessage( kMsgNewSong ), 'N' ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Open" B_UTF8_ELLIPSIS ),
								   new BMessage( kMsgOpenSong ), 'O' ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Open demo" B_UTF8_ELLIPSIS ),
								   new BMessage( kMsgOpenDemo ), 'D' ) );
	m_pRecentMenu = new BMenu( B_TRANSLATE( "Open recent" ) );
	pMenu->AddItem( m_pRecentMenu );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Save" ), new BMessage( kMsgSaveSong ), 'S' ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Save as" B_UTF8_ELLIPSIS ),
								   new BMessage( kMsgSaveSongAs ), 'S', B_SHIFT_KEY ) );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Song properties" B_UTF8_ELLIPSIS ),
								   new BMessage( kMsgSongProperties ) ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Export MIDI file" B_UTF8_ELLIPSIS ),
								   new BMessage( kMsgExportMidi ), 'M' ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Export song" B_UTF8_ELLIPSIS ),
								   new BMessage( kMsgExportSong ), 'E' ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Export LilyPond file" B_UTF8_ELLIPSIS ),
								   new BMessage( kMsgExportLilyPond ) ) );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "About Hydrogen" B_UTF8_ELLIPSIS ),
								   new BMessage( B_ABOUT_REQUESTED ) ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Settings" B_UTF8_ELLIPSIS ),
								   new BMessage( kMsgSettings ), ',' ) );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Quit" ), new BMessage( B_QUIT_REQUESTED ), 'Q' ) );

	pMenu->FindItem( B_ABOUT_REQUESTED )->SetTarget( be_app );
	// Not ported yet.
	for ( uint32 what : { kMsgSongProperties, kMsgExportMidi, kMsgExportSong,
						  kMsgExportLilyPond, kMsgSettings } ) {
		pMenu->FindItem( what )->SetEnabled( false );
	}
	return pMenu;
}

BMenu* MainWindow::buildEditMenu()
{
	BMenu* pMenu = new BMenu( B_TRANSLATE( "Edit" ) );
	m_pUndoItem = new BMenuItem( B_TRANSLATE( "Undo" ), new BMessage( kMsgUndo ), 'Z' );
	m_pRedoItem = new BMenuItem( B_TRANSLATE( "Redo" ), new BMessage( kMsgRedo ), 'Z',
								 B_SHIFT_KEY );
	pMenu->AddItem( m_pUndoItem );
	pMenu->AddItem( m_pRedoItem );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Undo history" ),
								   new BMessage( kMsgUndoHistory ) ) );
	// Not ported yet.
	pMenu->FindItem( kMsgUndoHistory )->SetEnabled( false );
	return pMenu;
}

static BMenu* disabledMenu( const char* sName, std::initializer_list<const char*> items )
{
	BMenu* pMenu = new BMenu( sName );
	for ( const char* sItem : items ) {
		if ( sItem == nullptr ) {
			pMenu->AddSeparatorItem();
			continue;
		}
		BMenuItem* pItem = new BMenuItem( sItem, nullptr );
		pItem->SetEnabled( false );
		pMenu->AddItem( pItem );
	}
	return pMenu;
}

BMenu* MainWindow::buildPatternMenu()
{
	// Not ported yet; the items show what's coming.
	return disabledMenu( B_TRANSLATE( "Pattern" ), {
		B_TRANSLATE( "New" B_UTF8_ELLIPSIS ), B_TRANSLATE( "Open" B_UTF8_ELLIPSIS ), nullptr,
		B_TRANSLATE( "Properties" B_UTF8_ELLIPSIS ), nullptr, B_TRANSLATE( "Save" ),
		B_TRANSLATE( "Save as" B_UTF8_ELLIPSIS ), nullptr, B_TRANSLATE( "Duplicate" ),
		nullptr, B_TRANSLATE( "Online import" B_UTF8_ELLIPSIS ) } );
}

BMenu* MainWindow::buildDrumkitMenu()
{
	return disabledMenu( B_TRANSLATE( "Drumkit" ), {
		B_TRANSLATE( "New" B_UTF8_ELLIPSIS ), B_TRANSLATE( "Open" B_UTF8_ELLIPSIS ), nullptr,
		B_TRANSLATE( "Properties" B_UTF8_ELLIPSIS ), nullptr, B_TRANSLATE( "Save" ),
		B_TRANSLATE( "Save as" B_UTF8_ELLIPSIS ), nullptr, B_TRANSLATE( "Export" B_UTF8_ELLIPSIS ),
		B_TRANSLATE( "Import" B_UTF8_ELLIPSIS ), B_TRANSLATE( "Online import" B_UTF8_ELLIPSIS ) } );
}

BMenu* MainWindow::buildViewMenu()
{
	BMenu* pMenu = new BMenu( B_TRANSLATE( "View" ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Playlist editor" ),
								   new BMessage( kMsgShowPlaylist ) ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Director" ),
								   new BMessage( kMsgShowDirector ), 'D', B_SHIFT_KEY ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Mixer" ),
								   new BMessage( kMsgShowMixer ), 'M', B_SHIFT_KEY ) );
	m_pRackItem = new BMenuItem( B_TRANSLATE( "Rack" ), new BMessage( kMsgShowRack ), 'I' );
	pMenu->AddItem( m_pRackItem );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Automation path" ),
								   new BMessage( kMsgShowAutomation ), 'A', B_SHIFT_KEY ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Playback track" ),
								   new BMessage( kMsgShowPlaybackTrack ) ) );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Full screen" ),
								   new BMessage( kMsgFullScreen ), 'F', B_SHIFT_KEY ) );
	for ( int32 i = 0; i < pMenu->CountItems(); i++ ) {
		if ( pMenu->ItemAt( i )->Message() != nullptr &&
			 pMenu->ItemAt( i )->Message()->what != kMsgFullScreen &&
			 pMenu->ItemAt( i )->Message()->what != kMsgShowRack ) {
			pMenu->ItemAt( i )->SetEnabled( false );
		}
	}
	// Editors in Hydrogen's colour theme (default) or the system colours.
	// Moves to the Settings window once that is ported.
	pMenu->AddSeparatorItem();
	m_pSystemColorsItem = new BMenuItem( B_TRANSLATE( "Use system colors in editors" ),
										 new BMessage( kMsgSystemColors ) );
	m_pSystemColorsItem->SetMarked( Colors::source() == Colors::Source::System );
	pMenu->AddItem( m_pSystemColorsItem );
	return pMenu;
}

BMenu* MainWindow::buildOptionsMenu()
{
	return disabledMenu( B_TRANSLATE( "Options" ),
						 { B_TRANSLATE( "MIDI control" B_UTF8_ELLIPSIS ) } );
}

BMenu* MainWindow::buildDebugMenu()
{
	return disabledMenu( B_TRANSLATE( "Debug" ), {
		B_TRANSLATE( "Show audio engine info" ), B_TRANSLATE( "Show filesystem info" ),
		nullptr, B_TRANSLATE( "Open log file" ) } );
}

BMenu* MainWindow::buildHelpMenu()
{
	BMenu* pMenu = new BMenu( B_TRANSLATE( "Help" ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "User guide" ), new BMessage( kMsgUserGuide ) ) );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Report a bug" ), new BMessage( kMsgReportBug ) ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Donate" ), new BMessage( kMsgDonate ) ) );
	return pMenu;
}

void MainWindow::MenusBeginning()
{
	updateRecentMenu();
	m_pRackItem->SetMarked( ! m_pRack->IsHidden() );
	updateUndoItems();
}

void MainWindow::updateUndoItems()
{
	// "Undo add note", as the HIG suggests, or plain "Undo" when empty.
	const UndoStack& stack = UndoStack::get();
	BString sUndo( B_TRANSLATE( "Undo" ) );
	if ( stack.canUndo() ) {
		sUndo << " " << stack.undoName();
	}
	BString sRedo( B_TRANSLATE( "Redo" ) );
	if ( stack.canRedo() ) {
		sRedo << " " << stack.redoName();
	}
	m_pUndoItem->SetLabel( sUndo );
	m_pUndoItem->SetEnabled( stack.canUndo() );
	m_pRedoItem->SetLabel( sRedo );
	m_pRedoItem->SetEnabled( stack.canRedo() );
}

void MainWindow::updateRecentMenu()
{
	m_pRecentMenu->RemoveItems( 0, m_pRecentMenu->CountItems(), true );
	auto pPref = Preferences::get_instance();
	if ( pPref == nullptr ) {
		return;
	}
	for ( const QString& sFile : pPref->getRecentFiles() ) {
		if ( sFile.isEmpty() ) {
			continue;
		}
		BMessage* pMessage = new BMessage( kMsgOpenRecent );
		pMessage->AddString( "path", toBString( sFile ) );
		m_pRecentMenu->AddItem( new BMenuItem( BPath( toBString( sFile ) ).Leaf(), pMessage ) );
	}
	m_pRecentMenu->SetEnabled( m_pRecentMenu->CountItems() > 0 );
}

void MainWindow::updateTitle()
{
	BString sTitle = B_TRANSLATE_SYSTEM_NAME( "Hydrogen" );
	auto pHydrogen = Hydrogen::get_instance();
	if ( pHydrogen != nullptr && pHydrogen->getSong() != nullptr ) {
		auto pSong = pHydrogen->getSong();
		BString sName = toBString( pSong->getName() );
		if ( sName.IsEmpty() ) {
			sName = B_TRANSLATE( "Untitled" );
		}
		sTitle.Prepend( ": " );
		sTitle.Prepend( sName );
		if ( pHydrogen->getSongModified() ) {
			sTitle.Prepend( "*" );
		}
	}
	SetTitle( sTitle );
}

void MainWindow::pollCore()
{
	auto pQueue = EventQueue::get_instance();
	if ( pQueue == nullptr ) {
		return;
	}
	bool bTitle = false;
	bool bEditors = false;
	bool bSong = false;
	bool bInstrument = false;
	bool bLibrary = false;
	while ( auto pEvent = pQueue->popEvent() ) {
		switch ( pEvent->getType() ) {
		case Event::Type::GridCellToggled:
		case Event::Type::NextPatternsChanged:
		case Event::Type::PlayingPatternsChanged:
		case Event::Type::SongSizeChanged:
		case Event::Type::StackedModeActivation:
		case Event::Type::Relocation:
		case Event::Type::State:
			bSong = true;
			break;
		case Event::Type::SoundLibraryChanged:
			bLibrary = true;
			break;
		case Event::Type::DrumkitLoaded:
		case Event::Type::DrumkitIsModified:
		case Event::Type::InstrumentMuteSoloChanged:
		case Event::Type::InstrumentParametersChanged:
		case Event::Type::PatternChanged:
		case Event::Type::PatternEditorLocked:
		case Event::Type::PatternIsModified:
		case Event::Type::SelectedInstrumentChanged:
		case Event::Type::SelectedPatternChanged:
		case Event::Type::SongModeActivation:
		case Event::Type::UpdatePreferences:
			bEditors = true;
			bInstrument = true;
			break;
		case Event::Type::UpdateSong:
			// A different song: its edits can't be undone any more.
			if ( pEvent->getValue() == 0 ) {
				UndoStack::get().clear();
				updateUndoItems();
			}
			bTitle = true;
			bEditors = true;
			break;
		case Event::Type::SongIsModified:
			bTitle = true;
			break;
		case Event::Type::Quit:
			PostMessage( B_QUIT_REQUESTED );
			break;
		default:
			// Everything else lands in an editor that isn't ported yet.
			break;
		}
	}
	if ( bTitle ) {
		updateTitle();
	}
	if ( bEditors ) {
		m_pPatternEditor->refresh();
	}
	if ( bEditors || bSong ) {
		m_pSongEditor->refresh();
	}
	if ( bInstrument || bTitle ) {
		m_pRack->refreshInstrument();
	}
	if ( bLibrary ) {
		m_pRack->refreshLibrary();
	}
	m_pPatternEditor->updatePlayhead();
	m_pSongEditor->updatePlayhead();
	m_pTransportBar->update();
}

void MainWindow::transport( uint32 what )
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pSong = pHydrogen->getSong();
	if ( pSong == nullptr ) {
		return;
	}

	auto driverMissing = [&]() {
		auto pDriver = pHydrogen->getAudioDriver();
		if ( pDriver != nullptr && std::dynamic_pointer_cast<NullDriver>( pDriver ) == nullptr ) {
			return false;
		}
		BAlert* pAlert = new BAlert( B_TRANSLATE( "No audio" ),
			B_TRANSLATE( "Hydrogen couldn't start an audio driver, so it can't play.\n\n"
						 "Check that the media services are running (Media preferences)." ),
			B_TRANSLATE( "OK" ), nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT );
		pAlert->Go( nullptr );
		return true;
	};

	const bool bSongMode = pHydrogen->getMode() == Song::Mode::Song;
	const int nColumns = pSong->getPatternGroupVector()->size();
	const int nColumn = pHydrogen->getAudioEngine()->getPlayhead()->getColumn();

	switch ( what ) {
	case kMsgPlay:
		if ( driverMissing() ) {
			break;
		}
		if ( pHydrogen->getAudioEngine()->getState() != AudioEngine::State::Playing ) {
			if ( Preferences::get_instance()->getCountIn() ) {
				CoreActionController::startCountIn();
			}
			else {
				pHydrogen->sequencerPlay();
			}
		}
		else {
			pHydrogen->sequencerStop();
		}
		break;
	case kMsgStop:
		if ( driverMissing() ) {
			break;
		}
		pHydrogen->sequencerStop();
		CoreActionController::locateToColumn( 0 );
		break;
	case kMsgRewind:
		if ( bSongMode ) {
			int nNew = nColumn > 0 ? nColumn - 1
				: ( pSong->getLoopMode() == Song::LoopMode::Enabled ? nColumns - 1 : 0 );
			CoreActionController::locateToColumn( std::max( nNew, 0 ) );
		}
		break;
	case kMsgForward:
		if ( bSongMode ) {
			if ( nColumn < nColumns - 1 ) {
				CoreActionController::locateToColumn( std::max( nColumn, 0 ) + 1 );
			}
			else if ( pSong->getLoopMode() == Song::LoopMode::Enabled ) {
				CoreActionController::locateToColumn( 0 );
			}
			else {
				transport( kMsgStop );
			}
		}
		break;
	}
	m_pTransportBar->update();
}

void MainWindow::MessageReceived( BMessage* pMessage )
{
	switch ( pMessage->what ) {
	case kMsgPollCore:
		pollCore();
		break;
	case kMsgUndo:
		UndoStack::get().undo();
		updateUndoItems();
		break;
	case kMsgRedo:
		UndoStack::get().redo();
		updateUndoItems();
		break;
	case kMsgPatternEdited:
		m_pPatternEditor->redrawNotes();
		updateUndoItems();
		break;
	case kMsgSystemColors: {
		const bool bSystem = ! m_pSystemColorsItem->IsMarked();
		m_pSystemColorsItem->SetMarked( bSystem );
		Colors::setSource( bSystem ? Colors::Source::System : Colors::Source::Hydrogen );
		Settings::save();
		m_pPatternEditor->refresh();
		m_pSongEditor->refresh();
		break;
	}

	case kMsgPlay:
	case kMsgStop:
	case kMsgRewind:
	case kMsgForward:
		transport( pMessage->what );
		break;
	case kMsgLoop: {
		int32 nValue = 0;
		BControl* pSource = nullptr;
		if ( pMessage->FindPointer( "source", (void**) &pSource ) == B_OK ) {
			nValue = pSource->Value();
		}
		CoreActionController::activateLoopMode( nValue == B_CONTROL_ON );
		m_pTransportBar->update();
		break;
	}
	case kMsgRecord:
		CoreActionController::toggleRecordMode();
		m_pTransportBar->update();
		break;
	case kMsgSongMode:
	case kMsgPatternMode:
		CoreActionController::activateSongMode( pMessage->what == kMsgSongMode );
		m_pTransportBar->update();
		break;
	case kMsgBpmChanged: {
		// The message's "be:value" is an int; the spinner holds a double.
		// Setting the spinner from the core sends this too: only a real
		// change may reach the core, or loading a song marks it modified.
		BDecimalSpinner* pSpinner = nullptr;
		auto pAudioEngine = Hydrogen::get_instance()->getAudioEngine();
		if ( pMessage->FindPointer( "source", (void**) &pSpinner ) == B_OK &&
			 std::abs( pSpinner->Value() - pAudioEngine->getPlayhead()->getBpm() ) > 0.005 ) {
			CoreActionController::setBpm( (float) pSpinner->Value() );
		}
		break;
	}

	case kMsgNewSong: {
		BMessage then( kMsgNewSong );
		then.AddBool( "confirmed", true );
		if ( pMessage->GetBool( "confirmed" ) || handleUnsavedChanges( &then ) ) {
			doNewSong();
		}
		break;
	}
	case kMsgOpenSong:
	case kMsgOpenDemo: {
		if ( m_pOpenPanel == nullptr ) {
			m_pOpenPanel = new BFilePanel( B_OPEN_PANEL, new BMessenger( this ), nullptr,
										   B_FILE_NODE, false, nullptr, new SongFilter() );
		}
		auto pPref = Preferences::get_instance();
		QString sDir = pMessage->what == kMsgOpenDemo ? Filesystem::demosDir()
													  : pPref->getLastOpenSongDirectory();
		if ( sDir.isEmpty() ) {
			sDir = Filesystem::userSongsDir();
		}
		m_pOpenPanel->SetPanelDirectory( toBString( sDir ).String() );
		m_pOpenPanel->Window()->SetTitle( pMessage->what == kMsgOpenDemo
			? B_TRANSLATE( "Hydrogen: Open demo" ) : B_TRANSLATE( "Hydrogen: Open" ) );
		m_pOpenPanel->Show();
		break;
	}
	case B_REFS_RECEIVED: {
		entry_ref ref;
		if ( pMessage->FindRef( "refs", &ref ) == B_OK ) {
			openSong( BPath( &ref ).Path() );
		}
		break;
	}
	case kMsgOpenRecent: {
		BString sPath;
		if ( pMessage->FindString( "path", &sPath ) == B_OK ) {
			openSong( sPath );
		}
		break;
	}
	case kMsgSaveAndThen: {
		BMessage then;
		if ( pMessage->FindMessage( "then", &then ) == B_OK ) {
			PostMessage( &then );
		}
		break;
	}
	case kMsgSaveSong:
		saveSong();
		break;
	case kMsgSaveSongAs:
		showSavePanel( nullptr );
		break;
	case B_SAVE_REQUESTED: {
		entry_ref dir;
		BString sName;
		if ( pMessage->FindRef( "directory", &dir ) != B_OK ||
			 pMessage->FindString( "name", &sName ) != B_OK ) {
			break;
		}
		const BString sSuffix = toBString( Filesystem::sSongSuffix );
		if ( ! sName.IEndsWith( sSuffix ) ) {
			sName << sSuffix;
		}
		BPath path( &dir );
		path.Append( sName );
		if ( ! CoreActionController::saveSongAs( toQString( path.Path() ), false ) ) {
			BAlert* pAlert = new BAlert( B_TRANSLATE( "Save" ),
				B_TRANSLATE( "Hydrogen couldn't save the song." ), B_TRANSLATE( "OK" ),
				nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT );
			pAlert->Go( nullptr );
			delete m_pAfterSave;
			m_pAfterSave = nullptr;
			break;
		}
		BPath parent;
		path.GetParent( &parent );
		Preferences::get_instance()->setLastSaveSongAsDirectory( toQString( parent.Path() ) );
		updateTitle();
		if ( m_pAfterSave != nullptr ) {
			PostMessage( m_pAfterSave );
			delete m_pAfterSave;
			m_pAfterSave = nullptr;
		}
		break;
	}
	case B_CANCEL:
		// The save panel was closed without saving: drop what was to follow.
		delete m_pAfterSave;
		m_pAfterSave = nullptr;
		break;

	case kMsgShowRack:
		if ( m_pRack->IsHidden() ) {
			m_pRack->Show();
		}
		else {
			m_pRack->Hide();
		}
		Settings::get().RemoveName( kRackHiddenName );
		Settings::get().AddBool( kRackHiddenName, m_pRack->IsHidden() );
		break;
	case kMsgFullScreen: {
		static BRect sNormalFrame;
		static bool sFullScreen = false;
		if ( ! sFullScreen ) {
			sNormalFrame = Frame();
			BScreen screen( this );
			MoveTo( screen.Frame().LeftTop() );
			ResizeTo( screen.Frame().Width(), screen.Frame().Height() );
			SetLook( B_NO_BORDER_WINDOW_LOOK );
		}
		else {
			SetLook( B_DOCUMENT_WINDOW_LOOK );
			MoveTo( sNormalFrame.LeftTop() );
			ResizeTo( sNormalFrame.Width(), sNormalFrame.Height() );
		}
		sFullScreen = ! sFullScreen;
		break;
	}

	case kMsgUserGuide:
		BUrl( "https://hydrogen-music.org/documentation/manual/manual_en.html", true )
			.OpenWithPreferredApplication();
		break;
	case kMsgReportBug:
		BUrl( "https://github.com/hydrogen-music/hydrogen/issues", true ).OpenWithPreferredApplication();
		break;
	case kMsgDonate:
		BUrl( "https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=sebastian%2emoors%40gmail%2ecom&lc=DE&item_name=Hydrogen%20donation&no_note=0&currency_code=EUR", true )
			.OpenWithPreferredApplication();
		break;

	default:
		BWindow::MessageReceived( pMessage );
	}
}

void MainWindow::openSong( const BString& sPath )
{
	BMessage then( kMsgOpenRecent );
	then.AddString( "path", sPath );
	then.AddBool( "confirmed", true );
	if ( ! handleUnsavedChanges( &then ) ) {
		return;
	}
	doOpenSong( sPath );
}

void MainWindow::doOpenSong( const BString& sPath )
{
	auto pSong = CoreActionController::loadSong( toQString( sPath ) );
	if ( pSong == nullptr || ! CoreActionController::setSong( pSong ) ) {
		BString sText( B_TRANSLATE( "Hydrogen couldn't open the song \"%path%\"." ) );
		sText.ReplaceFirst( "%path%", sPath );
		BAlert* pAlert = new BAlert( B_TRANSLATE( "Open" ), sText, B_TRANSLATE( "OK" ),
									 nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT );
		pAlert->Go( nullptr );
		return;
	}
	BPath parent;
	BPath( sPath ).GetParent( &parent );
	Preferences::get_instance()->setLastOpenSongDirectory( toQString( parent.Path() ) );
	updateTitle();
	updateRecentMenu();
}

void MainWindow::doNewSong()
{
	auto pSong = Song::getEmptySong();
	if ( pSong != nullptr ) {
		CoreActionController::setSong( pSong );
	}
	updateTitle();
}

bool MainWindow::saveSong()
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pSong = pHydrogen->getSong();
	if ( pSong == nullptr ) {
		return false;
	}
	const QString sPath = pSong->getPath();
	if ( sPath.isEmpty() || sPath == Filesystem::emptyPath( Filesystem::Artifact::Song ) ||
		 Filesystem::DetermineContext( sPath ) == Filesystem::Context::System ) {
		// Never saved, or a read-only demo: ask where to put it.
		showSavePanel( nullptr );
		return false;
	}
	if ( ! CoreActionController::saveSong( false ) ) {
		BAlert* pAlert = new BAlert( B_TRANSLATE( "Save" ),
			B_TRANSLATE( "Hydrogen couldn't save the song." ), B_TRANSLATE( "OK" ),
			nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT );
		pAlert->Go( nullptr );
		return false;
	}
	updateTitle();
	return true;
}

void MainWindow::showSavePanel( BMessage* pThen )
{
	delete m_pAfterSave;
	m_pAfterSave = pThen != nullptr ? new BMessage( *pThen ) : nullptr;

	if ( m_pSavePanel == nullptr ) {
		m_pSavePanel = new BFilePanel( B_SAVE_PANEL, new BMessenger( this ) );
		m_pSavePanel->Window()->SetTitle( B_TRANSLATE( "Hydrogen: Save as" ) );
	}
	auto pPref = Preferences::get_instance();
	QString sDir = pPref->getLastSaveSongAsDirectory();
	if ( sDir.isEmpty() ) {
		sDir = Filesystem::userSongsDir();
	}
	m_pSavePanel->SetPanelDirectory( toBString( sDir ).String() );
	auto pSong = Hydrogen::get_instance()->getSong();
	BString sName = pSong != nullptr ? toBString( pSong->getName() ) : BString();
	if ( sName.IsEmpty() ) {
		sName = B_TRANSLATE( "Untitled" );
	}
	sName << toBString( Filesystem::sSongSuffix );
	m_pSavePanel->SetSaveText( sName );
	m_pSavePanel->Show();
}

bool MainWindow::handleUnsavedChanges( BMessage* pThen )
{
	auto pHydrogen = Hydrogen::get_instance();
	if ( pHydrogen == nullptr || ! pHydrogen->getSongModified() ) {
		return true;
	}
	BString sName = toBString( pHydrogen->getSong()->getName() );
	if ( sName.IsEmpty() ) {
		sName = B_TRANSLATE( "Untitled" );
	}
	BString sText( B_TRANSLATE( "Save changes to the song \"%name%\"?" ) );
	sText.ReplaceFirst( "%name%", sName );
	BAlert* pAlert = new BAlert( B_TRANSLATE( "Unsaved changes" ), sText,
								 B_TRANSLATE( "Don't save" ), B_TRANSLATE( "Cancel" ),
								 B_TRANSLATE( "Save" ), B_WIDTH_AS_USUAL, B_OFFSET_SPACING,
								 B_WARNING_ALERT );
	pAlert->SetShortcut( 1, B_ESCAPE );
	switch ( pAlert->Go() ) {
	case 0:
		return true;
	case 2: {
		const QString sPath = pHydrogen->getSong()->getPath();
		if ( sPath.isEmpty() || sPath == Filesystem::emptyPath( Filesystem::Artifact::Song ) ||
			 Filesystem::DetermineContext( sPath ) == Filesystem::Context::System ) {
			// The save panel finishes the job and then resends @a pThen.
			showSavePanel( pThen );
			return false;
		}
		return saveSong();
	}
	default:
		return false;
	}
}

bool MainWindow::QuitRequested()
{
	// If a save panel has to be shown, the quit comes back once it saved.
	BMessage then( B_QUIT_REQUESTED );
	if ( ! handleUnsavedChanges( &then ) ) {
		return false;
	}

	Settings::setFrame( kFrameName, Frame() );
	Settings::get().RemoveName( kSplitName );
	for ( int32 i = 0; i < 2; i++ ) {
		Settings::get().AddFloat( kSplitName, m_pSplitView->ItemWeight( i ) );
	}
	Settings::save();
	return true;
}
