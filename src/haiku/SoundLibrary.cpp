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

#include "SoundLibrary.h"

#include <algorithm>

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <OutlineListView.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <TabView.h>
#include <TextControl.h>
#include <Window.h>

#include <core/AudioEngine/AudioEngine.h>
#include <core/Basics/Drumkit.h>
#include <core/Basics/Instrument.h>
#include <core/Basics/InstrumentList.h>
#include <core/Basics/Note.h>
#include <core/Basics/Song.h>
#include <core/Hydrogen.h>
#include <core/Sampler/Sampler.h>
#include <core/SoundLibrary/DrumkitInfo.h>
#include <core/SoundLibrary/InstrumentInfo.h>
#include <core/SoundLibrary/SoundLibraryDatabase.h>
#include <core/SoundLibrary/SoundLibraryInfo.h>

#include "Messages.h"
#include "SongActions.h"
#include "UndoStack.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SoundLibrary"

using namespace H2Core;

enum : uint32 {
	kMsgSearch = 'SLse',
	kMsgRescan = 'SLrs',
	kMsgSelect = 'SLsl',
	kMsgInvoke = 'SLiv',
};

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

/** One line of a library list: a group heading, an artifact or (under a
 * drumkit) one of its instruments. */
class LibraryItem : public BStringItem
{
public:
	enum class Kind { Group, Drumkit, Pattern, Song, Instrument };

	LibraryItem( const BString& sText, Kind kind, const BString& sPath = "",
				 Instrument::Id id = Instrument::EmptyId, uint32 nLevel = 0 )
		: BStringItem( sText, nLevel, kind == Kind::Group ), m_kind( kind ), m_sPath( sPath ), m_id( id ) {}

	void DrawItem( BView* pOwner, BRect frame, bool bComplete ) override
	{
		// Group headings in bold, like upstream.
		if ( m_kind == Kind::Group ) {
			pOwner->SetFont( be_bold_font );
		}
		BStringItem::DrawItem( pOwner, frame, bComplete );
		pOwner->SetFont( be_plain_font );
	}

	Kind kind() const { return m_kind; }
	const BString& path() const { return m_sPath; }
	Instrument::Id id() const { return m_id; }

private:
	Kind m_kind;
	BString m_sPath;
	Instrument::Id m_id;
};

/** An outline list with a context menu on the secondary button. */
class LibraryList : public BOutlineListView
{
public:
	LibraryList( const char* sName ) : BOutlineListView( sName, B_SINGLE_SELECTION_LIST ) {}

	void MouseDown( BPoint where ) override
	{
		int32 nButtons = B_PRIMARY_MOUSE_BUTTON;
		if ( Window()->CurrentMessage() != nullptr ) {
			Window()->CurrentMessage()->FindInt32( "buttons", &nButtons );
		}
		BOutlineListView::MouseDown( where );
		if ( ! ( nButtons & B_SECONDARY_MOUSE_BUTTON ) ) {
			return;
		}
		const int32 nIndex = IndexOf( where );
		auto pItem = dynamic_cast<LibraryItem*>( ItemAt( nIndex ) );
		if ( pItem == nullptr || pItem->kind() == LibraryItem::Kind::Group ||
			 pItem->kind() == LibraryItem::Kind::Instrument ) {
			return;
		}
		Select( nIndex );
		const char* sAction = pItem->kind() == LibraryItem::Kind::Drumkit ? B_TRANSLATE( "Load" )
			: pItem->kind() == LibraryItem::Kind::Song ? B_TRANSLATE( "Open" )
			: B_TRANSLATE( "Add to song" );
		BPopUpMenu* pMenu = new BPopUpMenu( "library", false, false );
		pMenu->SetAsyncAutoDestruct( true );
		pMenu->AddItem( new BMenuItem( sAction, new BMessage( kMsgInvoke ) ) );
		pMenu->SetTargetForItems( Target() );
		pMenu->Go( ConvertToScreen( where ), true, false, true );
	}

	/** Fills the list from @a infos, grouped by context, filtered by @a sFilter. */
	void fill( const std::vector<std::shared_ptr<SoundLibraryInfo>>& infos, LibraryItem::Kind kind,
			   const BString& sFilter )
	{
		MakeEmpty();
		struct Group { const char* sName; std::vector<std::shared_ptr<SoundLibraryInfo>> infos; };
		Group groups[] = { { B_TRANSLATE( "Session" ), {} }, { B_TRANSLATE( "Custom" ), {} },
						   { B_TRANSLATE( "User" ), {} }, { B_TRANSLATE( "System" ), {} } };
		for ( const auto& pInfo : infos ) {
			if ( pInfo == nullptr || ! matches( pInfo, sFilter ) ) {
				continue;
			}
			switch ( pInfo->getContext() ) {
			case Filesystem::Context::System: groups[ 3 ].infos.push_back( pInfo ); break;
			case Filesystem::Context::User: groups[ 2 ].infos.push_back( pInfo ); break;
			case Filesystem::Context::Custom: groups[ 1 ].infos.push_back( pInfo ); break;
			default: groups[ 0 ].infos.push_back( pInfo ); break;
			}
		}
		for ( auto& group : groups ) {
			if ( group.infos.empty() ) {
				continue;
			}
			std::sort( group.infos.begin(), group.infos.end(), []( const auto& a, const auto& b ) {
				return label( a ).ICompare( label( b ) ) < 0;
			} );
			auto pGroup = new LibraryItem( group.sName, LibraryItem::Kind::Group );
			AddItem( pGroup );
			for ( const auto& pInfo : group.infos ) {
				auto pItem = new LibraryItem( label( pInfo ), kind, toBString( pInfo->getPath() ),
											  Instrument::EmptyId, 1 );
				AddItem( pItem );
				auto pKit = std::dynamic_pointer_cast<DrumkitInfo>( pInfo );
				if ( pKit == nullptr ) {
					continue;
				}
				// A kit's instruments, collapsed: "Name [Type]".
				pItem->SetExpanded( false );
				for ( const auto& pInstrument : pKit->getInstrumentInfos() ) {
					BString sText = toBString( pInstrument->getName() );
					if ( ! pInstrument->getType().isEmpty() ) {
						sText << " [" << toBString( pInstrument->getType() ) << "]";
					}
					AddUnder( new LibraryItem( sText, LibraryItem::Kind::Instrument,
											   toBString( pInfo->getPath() ), pInstrument->getId(), 2 ),
							  pItem );
				}
			}
		}
	}

private:
	static BString label( const std::shared_ptr<SoundLibraryInfo>& pInfo )
	{
		return toBString( pInfo->getLabel().isEmpty() ? pInfo->getName() : pInfo->getLabel() );
	}

	/** Upstream searches names and tags, ignoring case. */
	static bool matches( const std::shared_ptr<SoundLibraryInfo>& pInfo, const BString& sFilter )
	{
		if ( sFilter.IsEmpty() ) {
			return true;
		}
		if ( label( pInfo ).IFindFirst( sFilter ) >= 0 ) {
			return true;
		}
		for ( const QString& sTag : pInfo->getTags() ) {
			if ( toBString( sTag ).IFindFirst( sFilter ) >= 0 ) {
				return true;
			}
		}
		if ( auto pKit = std::dynamic_pointer_cast<DrumkitInfo>( pInfo ) ) {
			for ( const auto& pInstrument : pKit->getInstrumentInfos() ) {
				if ( toBString( pInstrument->getName() ).IFindFirst( sFilter ) >= 0 ) {
					return true;
				}
			}
		}
		return false;
	}
};

/** Plays an instrument of a library kit, as upstream does on click. */
static void preview( const BString& sKitPath, Instrument::Id id )
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pKit = pHydrogen->getSoundLibraryDatabase()->getDrumkit( QString::fromUtf8( sKitPath.String() ) );
	if ( pKit == nullptr ) {
		return;
	}
	auto pOriginal = pKit->getInstruments()->find( id );
	if ( pOriginal == nullptr ) {
		return;
	}
	auto pInstrument = std::make_shared<Instrument>( pOriginal );
	auto pSong = pHydrogen->getSong();
	pInstrument->loadSamples( pSong != nullptr ? pSong->getBpm() : 120 );
	pInstrument->setIsPreviewInstrument( true );
	pInstrument->setId( Instrument::EmptyId );
	auto pNote = std::make_shared<Note>( pInstrument, 0, VELOCITY_MAX, PAN_DEFAULT, LENGTH_ENTIRE_SAMPLE );
	pHydrogen->getAudioEngine()->getSampler()->previewInstrument( pInstrument, pNote );
}

SoundLibrary::SoundLibrary()
	: BView( B_TRANSLATE( "Library" ), 0 )
{
	SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
	m_pSearch = new BTextControl( "search", nullptr, "", new BMessage( kMsgSearch ) );
	m_pSearch->SetModificationMessage( new BMessage( kMsgSearch ) );
	m_pSearch->TextView()->SetToolTip( B_TRANSLATE( "Search names and tags" ) );
	BButton* pRescan = new BButton( "rescan", B_TRANSLATE( "Rescan" ), new BMessage( kMsgRescan ) );
	pRescan->SetToolTip( B_TRANSLATE( "Look for new or removed kits, patterns and songs" ) );

	m_pDrumkits = new LibraryList( B_TRANSLATE( "Drumkits" ) );
	m_pPatterns = new LibraryList( B_TRANSLATE( "Patterns" ) );
	m_pSongs = new LibraryList( B_TRANSLATE( "Songs" ) );
	m_pTabs = new BTabView( "library tabs", B_WIDTH_FROM_LABEL );
	for ( LibraryList* pList : { m_pDrumkits, m_pPatterns, m_pSongs } ) {
		BScrollView* pScroll = new BScrollView( pList->Name(), pList, 0, false, true, B_NO_BORDER );
		m_pTabs->AddTab( pScroll );
	}
	m_pTabs->SetBorder( B_NO_BORDER );

	BLayoutBuilder::Group<>( this, B_VERTICAL, B_USE_HALF_ITEM_SPACING )
		.SetInsets( B_USE_HALF_ITEM_INSETS )
		.AddGroup( B_HORIZONTAL, B_USE_HALF_ITEM_SPACING )
			.Add( m_pSearch )
			.Add( pRescan )
		.End()
		.Add( m_pTabs );
}

void SoundLibrary::AttachedToWindow()
{
	BView::AttachedToWindow();
	m_pSearch->SetTarget( this );
	if ( BView* pRescan = FindView( "rescan" ) ) {
		static_cast<BButton*>( pRescan )->SetTarget( this );
	}
	for ( LibraryList* pList : { m_pDrumkits, m_pPatterns, m_pSongs } ) {
		pList->SetTarget( this );
		pList->SetSelectionMessage( new BMessage( kMsgSelect ) );
		pList->SetInvocationMessage( new BMessage( kMsgInvoke ) );
	}
	refresh();
}

void SoundLibrary::refresh()
{
	auto pDatabase = Hydrogen::get_instance()->getSoundLibraryDatabase();
	if ( pDatabase == nullptr ) {
		return;
	}
	const BString sFilter( m_pSearch->Text() );
	m_pDrumkits->fill( pDatabase->getDrumkitInfos(), LibraryItem::Kind::Drumkit, sFilter );
	m_pPatterns->fill( pDatabase->getPatternInfos(), LibraryItem::Kind::Pattern, sFilter );
	m_pSongs->fill( pDatabase->getSongInfos(), LibraryItem::Kind::Song, sFilter );
}

void SoundLibrary::MessageReceived( BMessage* pMessage )
{
	auto selected = [this]() -> LibraryItem* {
		LibraryList* pList = m_pTabs->Selection() == 1 ? m_pPatterns
			: m_pTabs->Selection() == 2 ? m_pSongs : m_pDrumkits;
		return dynamic_cast<LibraryItem*>( pList->ItemAt( pList->CurrentSelection() ) );
	};

	switch ( pMessage->what ) {
	case kMsgSearch:
		refresh();
		break;
	case kMsgRescan:
		Hydrogen::get_instance()->getSoundLibraryDatabase()->update();
		break;
	case kMsgSelect:
		if ( auto pItem = selected(); pItem != nullptr && pItem->kind() == LibraryItem::Kind::Instrument ) {
			preview( pItem->path(), pItem->id() );
		}
		break;
	case kMsgInvoke: {
		auto pItem = selected();
		if ( pItem == nullptr ) {
			break;
		}
		std::unique_ptr<UndoCommand> pCommand;
		switch ( pItem->kind() ) {
		case LibraryItem::Kind::Drumkit:
			pCommand = SongActions::switchDrumkit( pItem->path() );
			break;
		case LibraryItem::Kind::Pattern:
			pCommand = SongActions::appendPattern( pItem->path() );
			break;
		case LibraryItem::Kind::Song: {
			// Through the window, which offers to save unsaved changes first.
			BMessage open( kMsgOpenRecent );
			open.AddString( "path", pItem->path() );
			Window()->PostMessage( &open );
			break;
		}
		case LibraryItem::Kind::Instrument:
			preview( pItem->path(), pItem->id() );
			break;
		default:
			break;
		}
		if ( pCommand != nullptr ) {
			UndoStack::get().push( std::move( pCommand ) );
			Window()->PostMessage( kMsgPatternEdited );
		}
		break;
	}
	default:
		BView::MessageReceived( pMessage );
	}
}
