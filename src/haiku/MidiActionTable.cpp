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


#include "MidiActionTable.h"

#include <Button.h>
#include <Catalog.h>
#include <GridLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <Window.h>

#if __has_include(<DecimalSpinner.h>)
#	include <DecimalSpinner.h>
#	include <Spinner.h>
#else
#	include <private/interface/DecimalSpinner.h>
#	include <private/interface/Spinner.h>
#endif

#include <core/Hydrogen.h>
#include <core/Midi/Midi.h>
#include <core/Midi/MidiActionManager.h>
#include <core/Midi/MidiEventMap.h>
#include <core/Preferences/Preferences.h>

#include "Controls.h"
#include "MidiLearn.h"
#include "UndoStack.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MidiActionTable"

using namespace H2Core;

enum : uint32 {
	kMsgLearn = 'MTle',
	kMsgEvent = 'MTev',
	kMsgNumber = 'MTnu',
	kMsgAction = 'MTac',
	kMsgParameter = 'MTpa',
	kMsgRemove = 'MTrm',
	kMsgAdd = 'MTad',
	kMsgPoll = 'MTpo',
};

/** The parameters an action can have, in upstream's order. */
enum class Field : int32 { Instrument, Fx, Component, Layer, Pattern, Song, Factor };

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

static bool hasNumber( MidiEvent::Type type )
{
	return type == MidiEvent::Type::Note || type == MidiEvent::Type::CC;
}

bool MidiActionTable::Row::isComplete() const
{
	return type != MidiEvent::Type::Null && pAction != nullptr && pAction->getType() != MidiAction::Type::Null;
}

/** Swaps one binding for another; either side may be incomplete. */
class ReplaceCommand : public UndoCommand
{
public:
	struct Binding {
		MidiEvent::Type type;
		int nParameter;
		std::shared_ptr<MidiAction> pAction;
		bool bComplete;
	};

	ReplaceCommand( const Binding& oldBinding, const Binding& newBinding )
		: UndoCommand( B_TRANSLATE( "change MIDI binding" ) ), m_old( oldBinding ), m_new( newBinding ) {}

	void redo() override { swap( m_old, m_new ); }
	void undo() override { swap( m_new, m_old ); }

private:
	static void swap( const Binding& from, const Binding& to )
	{
		auto pMap = Preferences::get_instance()->getMidiEventMap();
		if ( from.bComplete ) {
			pMap->removeRegisteredEvent( from.type, Midi::parameterFromIntClamp( from.nParameter ), from.pAction );
		}
		if ( to.bComplete ) {
			pMap->registerEvent( to.type, Midi::parameterFromIntClamp( to.nParameter ), to.pAction,
								 Event::Trigger::Default );
		}
	}

	Binding m_old, m_new;
};

MidiActionTable::MidiActionTable()
	: BView( B_TRANSLATE( "MIDI actions" ), 0 )
{
	SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
	m_pRows = new BView( "rows", 0 );
	m_pRows->SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
	m_pGrid = new BGridLayout( B_USE_HALF_ITEM_SPACING, B_USE_HALF_ITEM_SPACING );
	m_pRows->SetLayout( m_pGrid );
	m_pGrid->SetInsets( B_USE_HALF_ITEM_INSETS );
	m_pPane = new ScrollPane( m_pRows );

	m_pAdd = new BButton( "add", B_TRANSLATE( "Add binding" ), new BMessage( kMsgAdd ) );
	BStringView* pHint = new BStringView( "hint", B_TRANSLATE( "Shift-click a knob, fader or button to bind it." ) );
	BLayoutBuilder::Group<>( this, B_VERTICAL )
		.SetInsets( B_USE_DEFAULT_SPACING )
		.Add( m_pPane )
		.AddGroup( B_HORIZONTAL )
			.Add( m_pAdd )
			.AddGlue()
			.Add( pHint )
		.End();
}

MidiActionTable::~MidiActionTable()
{
	delete m_pRunner;
}

void MidiActionTable::AttachedToWindow()
{
	BView::AttachedToWindow();
	m_pAdd->SetTarget( this );
	sync();
}

void MidiActionTable::sync()
{
	std::vector<Row> rows;
	for ( const auto& pEvent : Preferences::get_instance()->getMidiEventMap()->getMidiEvents() ) {
		if ( pEvent == nullptr ) {
			continue;
		}
		Row row;
		row.type = pEvent->getType();
		row.nParameter = std::max( 0, static_cast<int>( pEvent->getParameter() ) );
		row.pAction = pEvent->getMidiAction();
		rows.push_back( row );
	}
	// Rows being filled in aren't bindings yet: keep them.
	for ( const auto& row : m_rows ) {
		if ( ! row.isComplete() ) {
			rows.push_back( row );
		}
	}
	auto same = []( const Row& a, const Row& b ) {
		return a.type == b.type && a.nParameter == b.nParameter && a.pAction == b.pAction;
	};
	if ( rows.size() == m_rows.size() && std::equal( rows.begin(), rows.end(), m_rows.begin(), same ) &&
		 m_pGrid->CountItems() > 0 ) {
		return;
	}
	m_rows = rows;
	rebuild();
}

void MidiActionTable::rebuild()
{
	while ( m_pGrid->CountItems() > 0 ) {
		BLayoutItem* pItem = m_pGrid->RemoveItem( (int32) 0 );
		if ( BView* pView = pItem->View() ) {
			pView->RemoveSelf();
			delete pView;
		}
		delete pItem;
	}

	BFont bold( be_bold_font );
	const char* headers[] = { "", B_TRANSLATE( "Event" ), B_TRANSLATE( "Number" ), B_TRANSLATE( "Action" ),
							  B_TRANSLATE( "Parameters" ) };
	for ( int32 i = 0; i < 5; i++ ) {
		BStringView* pHeader = new BStringView( nullptr, headers[ i ] );
		pHeader->SetFont( &bold );
		m_pGrid->AddView( pHeader, i, 0 );
	}

	auto pManager = Hydrogen::get_instance()->getMidiActionManager();
	for ( size_t i = 0; i < m_rows.size(); i++ ) {
		const Row& row = m_rows[ i ];
		const int32 nGridRow = static_cast<int32>( i ) + 1;
		auto message = [i]( uint32 what ) {
			BMessage* pMessage = new BMessage( what );
			pMessage->AddInt32( "row", static_cast<int32>( i ) );
			return pMessage;
		};

		BButton* pLearn = new BButton( "learn", static_cast<int>( i ) == m_nLearning ? B_TRANSLATE( "Waiting" B_UTF8_ELLIPSIS )
																				 : B_TRANSLATE( "Learn" ),
									   message( kMsgLearn ) );
		pLearn->SetToolTip( B_TRANSLATE( "Take the event from the next incoming MIDI message" ) );
		m_pGrid->AddView( pLearn, 0, nGridRow );

		BPopUpMenu* pEvents = new BPopUpMenu( "event" );
		for ( const QString& sType : MidiEvent::getAllTypes() ) {
			BMessage* pMessage = message( kMsgEvent );
			pMessage->AddString( "type", toBString( sType ) );
			BMenuItem* pItem = new BMenuItem( sType.isEmpty() ? B_TRANSLATE( "None" ) : toBString( sType ).String(), pMessage );
			pItem->SetMarked( MidiEvent::QStringToType( sType ) == row.type );
			pEvents->AddItem( pItem );
		}
		m_pGrid->AddView( new BMenuField( "event", nullptr, pEvents ), 1, nGridRow );

		BSpinner* pNumber = new BSpinner( "number", nullptr, message( kMsgNumber ) );
		pNumber->SetRange( 0, 127 );
		pNumber->SetValue( row.nParameter );
		pNumber->SetEnabled( hasNumber( row.type ) );
		pNumber->TextView()->SetExplicitMaxSize( BSize( be_plain_font->StringWidth( "127" ) + 12, B_SIZE_UNSET ) );
		m_pGrid->AddView( pNumber, 2, nGridRow );

		BPopUpMenu* pActions = new BPopUpMenu( "action" );
		BMenuItem* pNone = new BMenuItem( B_TRANSLATE( "None" ), message( kMsgAction ) );
		pNone->SetMarked( row.pAction == nullptr || row.pAction->getType() == MidiAction::Type::Null );
		pActions->AddItem( pNone );
		for ( const auto& type : pManager->getMidiActions() ) {
			// Driven by the MIDI clock, not by a binding (upstream listed it).
			if ( type == MidiAction::Type::TimingClockTick || type == MidiAction::Type::Null ) {
				continue;
			}
			BMessage* pMessage = message( kMsgAction );
			pMessage->AddInt32( "action", static_cast<int32>( type ) );
			BMenuItem* pItem = new BMenuItem( toBString( MidiAction::typeToQString( type ) ), pMessage );
			pItem->SetMarked( row.pAction != nullptr && row.pAction->getType() == type );
			pActions->AddItem( pItem );
		}
		m_pGrid->AddView( new BMenuField( "action", nullptr, pActions ), 3, nGridRow );

		// The parameters this action has (upstream's Para. 1–3).
		BGroupView* pParameters = new BGroupView( B_HORIZONTAL, B_USE_HALF_ITEM_SPACING );
		if ( row.pAction != nullptr ) {
			const auto nRequires = row.pAction->getRequires();
			auto field = [&]( MidiAction::Requires flag, Field which, const char* sLabel, int nMin, int nValue ) {
				if ( ( nRequires & flag ) == 0 ) {
					return;
				}
				BMessage* pMessage = message( kMsgParameter );
				pMessage->AddInt32( "field", static_cast<int32>( which ) );
				BSpinner* pSpinner = which == Field::Instrument
										 ? new FormattedSpinner( "parameter", sLabel, pMessage, []( int32 n ) {
											   return n < 0 ? BString( B_TRANSLATE( "Selected" ) ) : BString() << n;
										   } )
										 : new BSpinner( "parameter", sLabel, pMessage );
				pSpinner->SetRange( nMin, 999 );
				pSpinner->SetValue( nValue );
				pSpinner->TextView()->SetExplicitMaxSize( BSize( be_plain_font->StringWidth( "Selected" ) + 12, B_SIZE_UNSET ) );
				pParameters->GroupLayout()->AddView( pSpinner );
			};
			field( MidiAction::RequiresInstrument, Field::Instrument, B_TRANSLATE( "Instrument:" ), -1,
				   ( nRequires & MidiAction::RequiresInstrument ) ? row.pAction->getInstrument() : 0 );
			field( MidiAction::RequiresFx, Field::Fx, B_TRANSLATE( "FX:" ), 0,
				   ( nRequires & MidiAction::RequiresFx ) ? row.pAction->getFx() : 0 );
			field( MidiAction::RequiresComponent, Field::Component, B_TRANSLATE( "Component:" ), 0,
				   ( nRequires & MidiAction::RequiresComponent ) ? row.pAction->getComponent() : 0 );
			field( MidiAction::RequiresLayer, Field::Layer, B_TRANSLATE( "Layer:" ), 0,
				   ( nRequires & MidiAction::RequiresLayer ) ? row.pAction->getLayer() : 0 );
			field( MidiAction::RequiresPattern, Field::Pattern, B_TRANSLATE( "Pattern:" ), 0,
				   ( nRequires & MidiAction::RequiresPattern ) ? row.pAction->getPattern() : 0 );
			field( MidiAction::RequiresSong, Field::Song, B_TRANSLATE( "Song:" ), 0,
				   ( nRequires & MidiAction::RequiresSong ) ? row.pAction->getSong() : 0 );
			if ( nRequires & MidiAction::RequiresFactor ) {
				// A float upstream edited as a whole number.
				BMessage* pMessage = message( kMsgParameter );
				pMessage->AddInt32( "field", static_cast<int32>( Field::Factor ) );
				BDecimalSpinner* pFactor = new BDecimalSpinner( "factor", B_TRANSLATE( "Factor:" ), pMessage );
				pFactor->SetMinValue( 0 );
				pFactor->SetMaxValue( 999 );
				pFactor->SetStep( 0.1 );
				pFactor->SetPrecision( 1 );
				pFactor->SetValue( row.pAction->getFactor() );
				pFactor->TextView()->SetExplicitMaxSize( BSize( be_plain_font->StringWidth( "999.9" ) + 12, B_SIZE_UNSET ) );
				pParameters->GroupLayout()->AddView( pFactor );
			}
		}
		pParameters->GroupLayout()->AddItem( BSpaceLayoutItem::CreateGlue() );
		m_pGrid->AddView( pParameters, 4, nGridRow );

		BButton* pRemove = new BButton( "remove", B_TRANSLATE( "Remove" ), message( kMsgRemove ) );
		m_pGrid->AddView( pRemove, 5, nGridRow );
	}
	if ( m_rows.empty() ) {
		m_pGrid->AddView( new BStringView( nullptr, B_TRANSLATE( "Nothing is bound to MIDI yet." ) ), 1, 1, 4 );
	}
	m_pGrid->SetColumnWeight( 4, 1 );
	m_pGrid->AddItem( BSpaceLayoutItem::CreateGlue(), 0, static_cast<int32>( m_rows.size() ) + 2 );

	// Targets: the rows are rebuilt, so set them every time.
	for ( int32 i = 0; i < m_pGrid->CountItems(); i++ ) {
		BView* pView = static_cast<BLayout*>( m_pGrid )->ItemAt( i )->View();
		if ( pView == nullptr ) {
			continue;
		}
		if ( auto pControl = dynamic_cast<BControl*>( pView ) ) {
			pControl->SetTarget( this );
		}
		else if ( auto pField = dynamic_cast<BMenuField*>( pView ) ) {
			pField->Menu()->SetTargetForItems( this );
		}
		for ( int32 j = 0; BView* pChild = pView->ChildAt( j ); j++ ) {
			if ( auto pControl = dynamic_cast<BControl*>( pChild ) ) {
				pControl->SetTarget( this );
			}
		}
	}
	m_pPane->contentChanged();
	// The whole row in view; the window grows for it.
	m_pPane->SetExplicitMinSize( BSize( m_pRows->PreferredSize().width + B_V_SCROLL_BAR_WIDTH + 4, 200 ) );
}

void MidiActionTable::change( int nRow, const Row& row )
{
	if ( nRow < 0 || nRow >= static_cast<int>( m_rows.size() ) ) {
		return;
	}
	const Row& old = m_rows[ nRow ];
	const bool bWasComplete = old.isComplete();
	const bool bIsComplete = row.isComplete();
	if ( bWasComplete || bIsComplete ) {
		UndoStack::get().push( std::make_unique<ReplaceCommand>(
			ReplaceCommand::Binding{ old.type, old.nParameter, old.pAction, bWasComplete },
			ReplaceCommand::Binding{ row.type, row.nParameter, row.pAction, bIsComplete } ) );
		MidiLearn::undoChanged();
	}
	m_rows[ nRow ] = row;
}

void MidiActionTable::poll()
{
	auto pHydrogen = Hydrogen::get_instance();
	const auto type = pHydrogen->getLastMidiEvent();
	if ( m_nLearning < 0 || m_nLearning >= static_cast<int>( m_rows.size() ) ) {
		delete m_pRunner;
		m_pRunner = nullptr;
		m_nLearning = -1;
		return;
	}
	if ( type == MidiEvent::Type::Null ) {
		return;
	}
	Row row = m_rows[ m_nLearning ];
	row.type = type;
	row.nParameter = std::max( 0, static_cast<int>( pHydrogen->getLastMidiEventParameter() ) );
	const int nRow = m_nLearning;
	m_nLearning = -1;
	delete m_pRunner;
	m_pRunner = nullptr;
	change( nRow, row );
	rebuild();
}

void MidiActionTable::MessageReceived( BMessage* pMessage )
{
	int32 nRow = -1;
	pMessage->FindInt32( "row", &nRow );
	const bool bValidRow = nRow >= 0 && nRow < static_cast<int32>( m_rows.size() );
	switch ( pMessage->what ) {
	case kMsgAdd:
		m_rows.push_back( Row() );
		rebuild();
		break;
	case kMsgRemove:
		if ( bValidRow ) {
			if ( m_rows[ nRow ].isComplete() ) {
				UndoStack::get().push( MidiLearn::bind(
					MidiLearn::Binding( m_rows[ nRow ].type, Midi::parameterFromIntClamp( m_rows[ nRow ].nParameter ) ),
					m_rows[ nRow ].pAction, false ) );
				MidiLearn::undoChanged();
			}
			m_rows.erase( m_rows.begin() + nRow );
			if ( m_nLearning == nRow ) {
				m_nLearning = -1;
			}
			rebuild();
		}
		break;
	case kMsgLearn:
		if ( bValidRow ) {
			m_nLearning = m_nLearning == nRow ? -1 : nRow;
			auto pHydrogen = Hydrogen::get_instance();
			pHydrogen->setLastMidiEvent( MidiEvent::Type::Null );
			pHydrogen->setLastMidiEventParameter( Midi::ParameterInvalid );
			delete m_pRunner;
			m_pRunner = nullptr;
			if ( m_nLearning >= 0 ) {
				BMessage poll( kMsgPoll );
				m_pRunner = new BMessageRunner( BMessenger( this ), &poll, 100000 );
			}
			rebuild();
		}
		break;
	case kMsgPoll:
		poll();
		break;
	case kMsgEvent:
		if ( bValidRow ) {
			BString sType;
			pMessage->FindString( "type", &sType );
			Row row = m_rows[ nRow ];
			row.type = MidiEvent::QStringToType( QString::fromUtf8( sType.String() ) );
			change( nRow, row );
			rebuild();
		}
		break;
	case kMsgNumber: {
		void* pSource = nullptr;
		if ( bValidRow && pMessage->FindPointer( "source", &pSource ) == B_OK ) {
			Row row = m_rows[ nRow ];
			const int nValue = static_cast<BSpinner*>( pSource )->Value();
			// Spinners echo SetValue(); only real changes count.
			if ( nValue != row.nParameter ) {
				row.nParameter = nValue;
				change( nRow, row );
			}
		}
		break;
	}
	case kMsgAction:
		if ( bValidRow ) {
			int32 nType = -1;
			Row row = m_rows[ nRow ];
			if ( pMessage->FindInt32( "action", &nType ) == B_OK ) {
				auto pAction = std::make_shared<MidiAction>( static_cast<MidiAction::Type>( nType ) );
				// Parameters start valid: the core rejects unset ones.
				const auto nRequires = pAction->getRequires();
				const auto pOld = row.pAction;
				const auto oldRequires = pOld != nullptr ? pOld->getRequires() : MidiAction::RequiresNone;
				auto keep = [&]( MidiAction::Requires flag, auto get, auto set, auto fallback ) {
					if ( nRequires & flag ) {
						( pAction.get()->*set )( ( oldRequires & flag ) ? ( pOld.get()->*get )() : fallback );
					}
				};
				keep( MidiAction::RequiresInstrument, &MidiAction::getInstrument, &MidiAction::setInstrument, 0 );
				keep( MidiAction::RequiresFx, &MidiAction::getFx, &MidiAction::setFx, 0 );
				keep( MidiAction::RequiresComponent, &MidiAction::getComponent, &MidiAction::setComponent, 0 );
				keep( MidiAction::RequiresLayer, &MidiAction::getLayer, &MidiAction::setLayer, 0 );
				keep( MidiAction::RequiresPattern, &MidiAction::getPattern, &MidiAction::setPattern, 0 );
				keep( MidiAction::RequiresSong, &MidiAction::getSong, &MidiAction::setSong, 0 );
				keep( MidiAction::RequiresFactor, &MidiAction::getFactor, &MidiAction::setFactor, 1.0f );
				row.pAction = pAction;
			}
			else {
				row.pAction = nullptr;
			}
			change( nRow, row );
			rebuild();
		}
		break;
	case kMsgParameter: {
		int32 nField = 0;
		void* pSource = nullptr;
		if ( ! bValidRow || m_rows[ nRow ].pAction == nullptr || pMessage->FindInt32( "field", &nField ) != B_OK ||
			 pMessage->FindPointer( "source", &pSource ) != B_OK ) {
			break;
		}
		Row row = m_rows[ nRow ];
		auto pAction = std::make_shared<MidiAction>( row.pAction );
		const Field field = static_cast<Field>( nField );
		bool bChanged = false;
		if ( field == Field::Factor ) {
			const float fValue = static_cast<float>( static_cast<BDecimalSpinner*>( pSource )->Value() );
			bChanged = fValue != pAction->getFactor();
			pAction->setFactor( fValue );
		}
		else {
			const int nValue = static_cast<BSpinner*>( pSource )->Value();
			switch ( field ) {
			case Field::Instrument:
				bChanged = nValue != pAction->getInstrument();
				pAction->setInstrument( nValue );
				break;
			case Field::Fx:
				bChanged = nValue != pAction->getFx();
				pAction->setFx( nValue );
				break;
			case Field::Component:
				bChanged = nValue != pAction->getComponent();
				pAction->setComponent( nValue );
				break;
			case Field::Layer:
				bChanged = nValue != pAction->getLayer();
				pAction->setLayer( nValue );
				break;
			case Field::Pattern:
				bChanged = nValue != pAction->getPattern();
				pAction->setPattern( nValue );
				break;
			case Field::Song:
				bChanged = nValue != pAction->getSong();
				pAction->setSong( nValue );
				break;
			default:
				break;
			}
		}
		if ( bChanged ) {
			row.pAction = pAction;
			change( nRow, row );
		}
		break;
	}
	default:
		BView::MessageReceived( pMessage );
	}
}
