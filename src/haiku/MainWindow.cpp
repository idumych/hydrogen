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

#include <algorithm>
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
#include <SeparatorItem.h>
#include <SplitView.h>
#include <Url.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#if __has_include(<DecimalSpinner.h>)
#	include <DecimalSpinner.h>
#else
#	include <private/interface/DecimalSpinner.h>
#endif

#include <core/AudioEngine/AudioEngine.h>
#include <core/AudioEngine/Transport.h>
#include <core/Basics/Drumkit.h>
#include <core/Basics/Event.h>
#include <core/Basics/PatternList.h>
#include <core/Basics/Song.h>
#include <core/CoreActionController.h>
#include <core/EventQueue.h>
#include <core/Helpers/Filesystem.h>
#include <core/Hydrogen.h>
#include <core/IO/NullDriver.h>
#include <core/Preferences/Preferences.h>
#include <core/SoundLibrary/SoundLibraryDatabase.h>

#include "Colors.h"
#include "DrumkitWindow.h"
#include "ExportWindows.h"
#include "Messages.h"
#include "Mixer.h"
#include "PatternActions.h"
#include "PatternEditor.h"
#include "PropertiesWindow.h"
#include "Rack.h"
#include "Settings.h"
#include "SettingsWindow.h"
#include "SongActions.h"
#include "SongEditor.h"
#include "TransportBar.h"
#include "UndoStack.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"

using namespace H2Core;

static const char* kFrameName = "main window frame";
static const char* kSplitName = "main window split";
static const char* kRackHiddenName = "rack hidden";

/** Folders and .h2drumkit files, for Drumkit > Import. */
class DrumkitFileFilter : public BRefFilter
{
public:
	bool Filter( const entry_ref* pRef, BNode* pNode, struct stat_beos*, const char* ) override
	{
		return pNode->IsDirectory() || BString( pRef->name ).IEndsWith( ".h2drumkit" );
	}
};

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

	// A new window is hidden and its looper starts with the first Show():
	// Hide() + Show() starts it while keeping it hidden.
	const bool bMixerShown = Settings::get().GetBool( "mixer shown", false );
	m_pMixer = new MixerWindow();
	m_pMixer->Hide();
	m_pMixer->Show();
	if ( bMixerShown && m_pMixer->Lock() ) {
		m_pMixer->Show();
		m_pMixer->Unlock();
	}

	BMessage poll( kMsgPollCore );
	m_pPollRunner = new BMessageRunner( BMessenger( this ), &poll, 50000 );
}

MainWindow::~MainWindow()
{
	delete m_pPollRunner;
	delete m_pOpenPanel;
	delete m_pSavePanel;
	delete m_pKitExportPanel;
	delete m_pKitImportPanel;
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
	pMenu->FindItem( kMsgExportLilyPond )->SetEnabled( false );
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
	BMenu* pMenu = new BMenu( B_TRANSLATE( "Pattern" ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "New" ), new BMessage( kMsgPatternNew ) ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Open" B_UTF8_ELLIPSIS ), nullptr ) );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Properties" B_UTF8_ELLIPSIS ),
								   new BMessage( kMsgPatternProperties ) ) );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Save" ), nullptr ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Save as" B_UTF8_ELLIPSIS ), nullptr ) );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Duplicate" ), new BMessage( kMsgPatternDuplicate ) ) );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Online import" B_UTF8_ELLIPSIS ), nullptr ) );
	// Not ported yet: the items without a message.
	for ( int32 i = 0; BMenuItem* pItem = pMenu->ItemAt( i ); i++ ) {
		if ( pItem->Message() == nullptr && dynamic_cast<BSeparatorItem*>( pItem ) == nullptr ) {
			pItem->SetEnabled( false );
		}
	}
	return pMenu;
}

BMenu* MainWindow::buildDrumkitMenu()
{
	BMenu* pMenu = new BMenu( B_TRANSLATE( "Drumkit" ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "New" B_UTF8_ELLIPSIS ), new BMessage( kMsgKitNew ) ) );
	// The installed kits, filled when the menu opens (upstream: a dialog
	// with the sound library tree).
	m_pKitOpenMenu = new BMenu( B_TRANSLATE( "Open" ) );
	pMenu->AddItem( m_pKitOpenMenu );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Properties" B_UTF8_ELLIPSIS ), new BMessage( kMsgKitProperties ) ) );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Save" ), new BMessage( kMsgKitSave ) ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Save as" B_UTF8_ELLIPSIS ), new BMessage( kMsgKitSaveAs ) ) );
	pMenu->AddSeparatorItem();
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Export" B_UTF8_ELLIPSIS ), new BMessage( kMsgKitExport ) ) );
	pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Import" B_UTF8_ELLIPSIS ), new BMessage( kMsgKitImport ) ) );
	BMenuItem* pOnline = new BMenuItem( B_TRANSLATE( "Online import" B_UTF8_ELLIPSIS ), nullptr );
	// Not ported yet.
	pOnline->SetEnabled( false );
	pMenu->AddItem( pOnline );
	return pMenu;
}

void MainWindow::updateDrumkitMenu()
{
	while ( BMenuItem* pItem = m_pKitOpenMenu->RemoveItem( (int32) 0 ) ) {
		delete pItem;
	}
	auto pHydrogen = Hydrogen::get_instance();
	const auto pSong = pHydrogen->getSong();
	// The database's keys and kits' paths may name the folder or its
	// drumkit.xml; compare folders.
	auto folder = []( const QString& sPath ) {
		return sPath.endsWith( ".xml" ) ? Filesystem::drumkitDirFromPath( sPath ) : sPath;
	};
	const QString sCurrent = pSong != nullptr && pSong->getDrumkit() != nullptr
								 ? folder( pSong->getDrumkit()->getPath() ) : QString();
	// System kits first, then the user's and custom ones, each by name.
	std::vector<std::pair<QString, std::shared_ptr<Drumkit>>> system, user;
	std::vector<QDir> seen;
	for ( const auto& [ sPath, pKit ] : pHydrogen->getSoundLibraryDatabase()->getDrumkitDatabase() ) {
		if ( pKit == nullptr ) {
			continue;
		}
		// The same folder may be listed under two spellings.
		const QDir dir( folder( sPath ) );
		if ( std::find( seen.begin(), seen.end(), dir ) != seen.end() ) {
			continue;
		}
		seen.push_back( dir );
		( Filesystem::DetermineContext( sPath ) == Filesystem::Context::System ? system : user ).emplace_back( sPath, pKit );
	}
	auto byName = []( const auto& a, const auto& b ) {
		return a.second->getName().compare( b.second->getName(), Qt::CaseInsensitive ) < 0;
	};
	std::sort( system.begin(), system.end(), byName );
	std::sort( user.begin(), user.end(), byName );
	for ( const auto* pGroup : { &system, &user } ) {
		if ( pGroup == &user && ! system.empty() && ! user.empty() ) {
			m_pKitOpenMenu->AddSeparatorItem();
		}
		for ( const auto& [ sPath, pKit ] : *pGroup ) {
			BMessage* pMessage = new BMessage( kMsgKitOpen );
			pMessage->AddString( "path", toBString( sPath ) );
			BMenuItem* pItem = new BMenuItem( toBString( pKit->getName() ), pMessage );
			pItem->SetMarked( ! sCurrent.isEmpty() && QDir( sCurrent ) == QDir( folder( sPath ) ) );
			m_pKitOpenMenu->AddItem( pItem );
		}
	}
	if ( m_pKitOpenMenu->CountItems() == 0 ) {
		BMenuItem* pItem = new BMenuItem( B_TRANSLATE( "No drumkits installed" ), nullptr );
		pItem->SetEnabled( false );
		m_pKitOpenMenu->AddItem( pItem );
	}
	m_pKitOpenMenu->SetTargetForItems( this );
}

void MainWindow::saveDrumkit()
{
	auto pHydrogen = Hydrogen::get_instance();
	const auto pSong = pHydrogen->getSong();
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		return;
	}
	auto pKit = pSong->getDrumkit();
	// A new kit, or one of the system's: ask where (upstream's Save as).
	const QString sPath = pKit->getPath();
	const auto context = sPath.isEmpty() ? Filesystem::Context::Song : Filesystem::DetermineContext( sPath );
	if ( sPath.isEmpty() || context == Filesystem::Context::System ||
		 context == Filesystem::Context::SessionReadOnly ) {
		DrumkitWindow::open( DrumkitWindow::Mode::SaveAs, Frame(), BMessenger( this ) );
		return;
	}
	// From a copy: save() points the samples at the kit's folder.
	auto pCopy = std::make_shared<Drumkit>( pKit );
	if ( ! pCopy->save( sPath ) ) {
		BString sText( B_TRANSLATE( "The drumkit couldn't be saved in \"%folder%\"." ) );
		sText.ReplaceFirst( "%folder%", toBString( Filesystem::drumkitDirFromPath( sPath ) ) );
		( new BAlert( B_TRANSLATE( "Drumkit" ), sText, B_TRANSLATE( "OK" ), nullptr, nullptr, B_WIDTH_AS_USUAL,
					  B_STOP_ALERT ) )->Go();
		return;
	}
	pKit->setIsModified( false );
	EventQueue::get_instance()->pushEvent( Event::Type::DrumkitIsModified, -1 );
	pHydrogen->getSoundLibraryDatabase()->updateDrumkits( Event::Trigger::Default );
}

void MainWindow::exportDrumkit( const BString& sTarget )
{
	const auto pSong = Hydrogen::get_instance()->getSong();
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		return;
	}
	auto fail = [this]( const char* sText ) {
		( new BAlert( B_TRANSLATE( "Drumkit" ), sText, B_TRANSLATE( "OK" ), nullptr, nullptr, B_WIDTH_AS_USUAL,
					  B_STOP_ALERT ) )->Go();
	};
	// As upstream: the kit as it is in the song, saved to a temporary
	// folder and packed from there.
	QTemporaryDir kitDir, outDir;
	if ( ! kitDir.isValid() || ! outDir.isValid() ) {
		fail( B_TRANSLATE( "The drumkit couldn't be exported." ) );
		return;
	}
	auto pCopy = std::make_shared<Drumkit>( pSong->getDrumkit() );
	pCopy->setPath( Filesystem::drumkitPathFromDir( kitDir.path() ) );
	if ( ! pCopy->save() ) {
		fail( B_TRANSLATE( "The drumkit couldn't be exported." ) );
		return;
	}
	// exportTo() takes a folder and names the file itself; upstream passed
	// the chosen file name and got a folder of that name.
	if ( ! pCopy->exportTo( outDir.path() ) ) {
		fail( B_TRANSLATE( "The drumkit couldn't be exported." ) );
		return;
	}
	const QString sMade = QDir( outDir.path() ).absoluteFilePath( pCopy->getExportName() + Filesystem::sDrumkitSuffix );
	const QString sFile = QString::fromUtf8( sTarget.String() );
	QFile::remove( sFile );
	if ( ! QFile::rename( sMade, sFile ) && ! QFile::copy( sMade, sFile ) ) {
		fail( B_TRANSLATE( "The exported drumkit couldn't be written to the chosen file." ) );
		return;
	}
	Preferences::get_instance()->setLastExportDrumkitDirectory( QFileInfo( sFile ).absolutePath() );
	BString sText( B_TRANSLATE( "The drumkit was exported to \"%file%\"." ) );
	sText.ReplaceFirst( "%file%", sTarget );
	( new BAlert( B_TRANSLATE( "Drumkit" ), sText, B_TRANSLATE( "OK" ), nullptr, nullptr, B_WIDTH_AS_USUAL,
				  B_INFO_ALERT ) )->Go( nullptr );
}

void MainWindow::importDrumkit( const BString& sSource )
{
	auto pHydrogen = Hydrogen::get_instance();
	const QString sFile = QString::fromUtf8( sSource.String() );
	Preferences::get_instance()->setLastImportDrumkitDirectory( QFileInfo( sFile ).absolutePath() );
	QString sInstalled;
	bool bEncodingIssues = false;
	if ( ! Drumkit::install( sFile, "", &sInstalled, &bEncodingIssues, false ) ) {
		BString sText( B_TRANSLATE( "The drumkit \"%file%\" couldn't be imported." ) );
		sText.ReplaceFirst( "%file%", sSource );
		( new BAlert( B_TRANSLATE( "Drumkit" ), sText, B_TRANSLATE( "OK" ), nullptr, nullptr, B_WIDTH_AS_USUAL,
					  B_STOP_ALERT ) )->Go();
		return;
	}
	pHydrogen->getSoundLibraryDatabase()->updateDrumkits( Event::Trigger::Default );
	// Loaded at once, as upstream; undoable.
	// install() reports the kit's folder (upstream took its parent).
	const QString sFolder = sInstalled.endsWith( ".xml" ) ? Filesystem::drumkitDirFromPath( sInstalled ) : sInstalled;
	// By the database's own spelling of the folder: getDrumkit() adds any
	// other spelling as a second, custom kit.
	QString sKey = sFolder;
	for ( const auto& [ sPath, pKit ] : pHydrogen->getSoundLibraryDatabase()->getDrumkitDatabase() ) {
		const QString sKitFolder = sPath.endsWith( ".xml" ) ? Filesystem::drumkitDirFromPath( sPath ) : sPath;
		if ( QDir( sKitFolder ) == QDir( sFolder ) ) {
			sKey = sPath;
			break;
		}
	}
	if ( auto pCommand = SongActions::switchDrumkit( toBString( sKey ) ) ) {
		UndoStack::get().push( std::move( pCommand ) );
		updateUndoItems();
	}
	BString sText( B_TRANSLATE( "The drumkit was imported to \"%folder%\"." ) );
	sText.ReplaceFirst( "%folder%", toBString( sFolder ) );
	if ( bEncodingIssues ) {
		sText << "\n\n" << B_TRANSLATE( "Some file names couldn't be converted and may be wrong." );
	}
	( new BAlert( B_TRANSLATE( "Drumkit" ), sText, B_TRANSLATE( "OK" ), nullptr, nullptr, B_WIDTH_AS_USUAL,
				  bEncodingIssues ? B_WARNING_ALERT : B_INFO_ALERT ) )->Go( nullptr );
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
	m_pMixerItem = pMenu->FindItem( kMsgShowMixer );
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
			 pMenu->ItemAt( i )->Message()->what != kMsgShowRack &&
			 pMenu->ItemAt( i )->Message()->what != kMsgShowMixer ) {
			pMenu->ItemAt( i )->SetEnabled( false );
		}
	}
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
	updateDrumkitMenu();
	m_pRackItem->SetMarked( ! m_pRack->IsHidden() );
	if ( m_pMixer->Lock() ) {
		m_pMixerItem->SetMarked( ! m_pMixer->IsHidden() );
		m_pMixer->Unlock();
	}
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
	bool bComponents = false;
	bool bMixerRebuild = false;
	bool bMixerUpdate = false;
	bool bMixerMaster = false;
	BMessage notes( kMsgMixerNotes );
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
		case Event::Type::InstrumentLayerChanged:
			bComponents = true;
			break;
		case Event::Type::NoteRender:
			notes.AddInt32( "strip", pEvent->getValue() );
			break;
		case Event::Type::EffectChanged:
			bMixerRebuild = true;
			break;
		case Event::Type::MixerSettingsChanged:
			bMixerMaster = true;
			break;
		case Event::Type::DrumkitLoaded:
		case Event::Type::DrumkitIsModified:
		case Event::Type::InstrumentMuteSoloChanged:
		case Event::Type::InstrumentParametersChanged:
			bMixerUpdate = true;
			[[fallthrough]];
		case Event::Type::PatternChanged:
		case Event::Type::PatternEditorLocked:
		case Event::Type::PatternIsModified:
		case Event::Type::SelectedInstrumentChanged:
		case Event::Type::SelectedPatternChanged:
		case Event::Type::SongModeActivation:
		case Event::Type::UpdatePreferences:
			// Colours or options changed in the settings window.
			Colors::invalidate();
			bEditors = true;
			bInstrument = true;
			bMixerRebuild = true;
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
		case Event::Type::AudioExportProgress: {
			BMessage progress( kMsgExportProgress );
			progress.AddInt32( "value", pEvent->getValue() );
			m_exportSong.SendMessage( &progress );
			break;
		}
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
	if ( bEditors || bTitle ) {
		// Kit loaded, instrument selected or replaced, song changed.
		bComponents = true;
	}
	if ( bComponents ) {
		m_pRack->rebuildComponents();
	}
	// Kit loaded, song changed, selection moved: the mixer redoes its strips.
	if ( bTitle || bComponents ) {
		bMixerRebuild = true;
	}
	if ( bMixerRebuild ) {
		m_pMixer->PostMessage( kMsgMixerRebuild );
	}
	else if ( bMixerUpdate || bEditors ) {
		m_pMixer->PostMessage( kMsgMixerUpdate );
	}
	if ( bMixerMaster || bTitle ) {
		m_pMixer->PostMessage( kMsgMixerMaster );
	}
	if ( notes.HasInt32( "strip" ) ) {
		m_pMixer->PostMessage( &notes );
	}
	if ( bInstrument || bTitle || bEditors ) {
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
	case kMsgKitNew: {
		BAlert* pAlert = new BAlert( B_TRANSLATE( "Drumkit" ),
									 B_TRANSLATE( "Replace the song's drumkit with an empty one?" ),
									 B_TRANSLATE( "Cancel" ), B_TRANSLATE( "Replace" ), nullptr,
									 B_WIDTH_AS_USUAL, B_WARNING_ALERT );
		pAlert->SetShortcut( 0, B_ESCAPE );
		if ( pAlert->Go() == 1 ) {
			if ( auto pCommand = SongActions::newDrumkit() ) {
				UndoStack::get().push( std::move( pCommand ) );
				updateUndoItems();
			}
		}
		break;
	}
	case kMsgKitOpen: {
		BString sPath;
		if ( pMessage->FindString( "path", &sPath ) == B_OK ) {
			if ( auto pCommand = SongActions::switchDrumkit( sPath ) ) {
				UndoStack::get().push( std::move( pCommand ) );
				updateUndoItems();
			}
		}
		break;
	}
	case kMsgKitProperties:
	case kMsgKitSaveAs:
		DrumkitWindow::open( pMessage->what == kMsgKitSaveAs ? DrumkitWindow::Mode::SaveAs
															 : DrumkitWindow::Mode::Properties,
							 Frame(), BMessenger( this ) );
		break;
	case kMsgKitSave:
		saveDrumkit();
		break;
	case kMsgKitExport: {
		const auto pSong = Hydrogen::get_instance()->getSong();
		if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
			break;
		}
		if ( m_pKitExportPanel == nullptr ) {
			m_pKitExportPanel = new BFilePanel( B_SAVE_PANEL, new BMessenger( this ), nullptr, 0, false,
												new BMessage( kMsgKitExportTo ) );
		}
		BString sTitle( B_TRANSLATE( "Export drumkit" ) );
		sTitle << " " << toBString( pSong->getDrumkit()->getName() );
		m_pKitExportPanel->Window()->SetTitle( sTitle );
		QString sDir = Preferences::get_instance()->getLastExportDrumkitDirectory();
		if ( ! Filesystem::dirWritable( sDir, true ) ) {
			sDir = QDir::homePath();
		}
		m_pKitExportPanel->SetPanelDirectory( toBString( sDir ).String() );
		m_pKitExportPanel->SetSaveText(
			toBString( pSong->getDrumkit()->getExportName() + Filesystem::sDrumkitSuffix ) );
		m_pKitExportPanel->Show();
		break;
	}
	case kMsgKitExportTo: {
		entry_ref dir;
		BString sName;
		if ( pMessage->FindRef( "directory", &dir ) == B_OK && pMessage->FindString( "name", &sName ) == B_OK ) {
			if ( ! sName.IEndsWith( toBString( Filesystem::sDrumkitSuffix ) ) ) {
				sName << toBString( Filesystem::sDrumkitSuffix );
			}
			BPath path( &dir );
			path.Append( sName );
			exportDrumkit( path.Path() );
		}
		break;
	}
	case kMsgKitImport: {
		if ( m_pKitImportPanel == nullptr ) {
			static DrumkitFileFilter filter;
			m_pKitImportPanel = new BFilePanel( B_OPEN_PANEL, new BMessenger( this ), nullptr, B_FILE_NODE, false,
												new BMessage( kMsgKitImportFrom ), &filter );
			m_pKitImportPanel->Window()->SetTitle( B_TRANSLATE( "Import drumkit" ) );
		}
		QString sDir = Preferences::get_instance()->getLastImportDrumkitDirectory();
		if ( ! QDir( sDir ).exists() ) {
			sDir = QDir::homePath();
		}
		m_pKitImportPanel->SetPanelDirectory( toBString( sDir ).String() );
		m_pKitImportPanel->Show();
		break;
	}
	case kMsgKitImportFrom: {
		entry_ref ref;
		if ( pMessage->FindRef( "refs", &ref ) == B_OK ) {
			importDrumkit( BPath( &ref ).Path() );
		}
		break;
	}
	case kMsgSongProperties:
		( new PropertiesWindow( -1, Frame(), BMessenger( this ) ) )->Show();
		break;
	case kMsgPatternNew:
		UndoStack::get().push( SongActions::newPattern() );
		updateUndoItems();
		break;
	case kMsgPatternProperties:
	case kMsgPatternDuplicate: {
		const int nPattern = Hydrogen::get_instance()->getSelectedPatternNumber();
		const auto pSong = Hydrogen::get_instance()->getSong();
		if ( pSong == nullptr || nPattern < 0 || nPattern >= pSong->getPatternList()->size() ) {
			break;
		}
		if ( pMessage->what == kMsgPatternProperties ) {
			( new PropertiesWindow( nPattern, Frame(), BMessenger( this ) ) )->Show();
		}
		else if ( auto pCommand = SongActions::duplicatePattern( nPattern ) ) {
			UndoStack::get().push( std::move( pCommand ) );
			updateUndoItems();
		}
		break;
	}
	case kMsgExportSong:
	case kMsgExportMidi: {
		BMessenger& messenger = pMessage->what == kMsgExportSong ? m_exportSong : m_exportMidi;
		// One of each; bring it forward if it is open.
		if ( messenger.LockTarget() ) {
			BLooper* pLooper = nullptr;
			messenger.Target( &pLooper );
			BWindow* pOpen = dynamic_cast<BWindow*>( pLooper );
			if ( pOpen != nullptr ) {
				pOpen->Activate();
			}
			pLooper->Unlock();
			break;
		}
		BWindow* pWindow = pMessage->what == kMsgExportSong
							   ? static_cast<BWindow*>( new ExportSongWindow( Frame() ) )
							   : static_cast<BWindow*>( new ExportMidiWindow( Frame() ) );
		messenger = BMessenger( pWindow );
		pWindow->Show();
		break;
	}
	case kMsgSettings:
		// One settings window; bring it forward if it is open.
		if ( m_settings.IsValid() ) {
			m_settings.SendMessage( kMsgSettings );
		}
		else {
			SettingsWindow* pWindow = new SettingsWindow();
			m_settings = BMessenger( pWindow );
			pWindow->Show();
		}
		break;

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

	case kMsgShowMixer:
		if ( m_pMixer->Lock() ) {
			if ( m_pMixer->IsHidden() ) {
				m_pMixer->Show();
			}
			else {
				m_pMixer->Hide();
			}
			m_pMixer->Unlock();
		}
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
	// The mixer only hides when closed: quit it here, keeping its state.
	if ( m_pMixer->Lock() ) {
		m_pMixer->saveSettings();
		m_pMixer->Quit();
		m_pMixer = nullptr;
	}
	Settings::get().RemoveName( kSplitName );
	for ( int32 i = 0; i < 2; i++ ) {
		Settings::get().AddFloat( kSplitName, m_pSplitView->ItemWeight( i ) );
	}
	Settings::save();
	return true;
}
