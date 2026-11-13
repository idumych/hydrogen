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


#include "MidiLearn.h"

#include <algorithm>

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <StringView.h>
#include <ToolTip.h>
#include <View.h>
#include <Window.h>

#include <core/Hydrogen.h>
#include <core/Midi/Midi.h>
#include <core/Midi/MidiEventMap.h>
#include <core/Preferences/Preferences.h>

#include "UndoStack.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MidiLearn"

using namespace H2Core;

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

// ---------------------------------------------------------------------------
// MidiLearnable

void MidiLearnable::setMidiAction( std::shared_ptr<MidiAction> pAction )
{
	m_pMidiAction = pAction;
}

bool MidiLearnable::learnClick( BView* pView )
{
	if ( ( modifiers() & B_SHIFT_KEY ) == 0 ) {
		return false;
	}
	MidiLearn::open( m_pMidiAction, pView );
	return true;
}

void MidiLearnable::updateMidiToolTip( BView* pView )
{
	if ( m_pMidiAction == nullptr ) {
		return;
	}
	// The control's own text, which it may have changed since (knobs and
	// faders show their value); not what this put there last time.
	BString sCurrent;
	if ( auto pTip = dynamic_cast<BTextToolTip*>( pView->ToolTip() ) ) {
		sCurrent = pTip->Text();
	}
	if ( sCurrent != m_sComposedToolTip ) {
		m_sBaseToolTip = sCurrent;
	}
	// Upstream's format: "MIDI: <ACTION>" and one "bound to" line per event.
	BString sText( m_sBaseToolTip );
	if ( ! sText.IsEmpty() ) {
		sText << "\n\n";
	}
	sText << "MIDI: " << MidiLearn::actionText( m_pMidiAction );
	const auto bindings = MidiLearn::bindings( m_pMidiAction );
	if ( bindings.empty() ) {
		sText << "\n" << B_TRANSLATE( "Not bound (Shift-click to bind)" );
	}
	for ( const auto& binding : bindings ) {
		sText << "\n" << B_TRANSLATE( "Bound to" ) << " " << MidiLearn::bindingText( binding );
	}
	m_sComposedToolTip = sText;
	pView->SetToolTip( sText );
}

// ---------------------------------------------------------------------------
// Bindings

namespace MidiLearn
{

static BMessenger sUndoTarget;

void setUndoTarget( BMessenger target )
{
	sUndoTarget = target;
}

void undoChanged()
{
	sUndoTarget.SendMessage( 'Hped' );
}

std::vector<Binding> bindings( const std::shared_ptr<MidiAction>& pAction )
{
	std::vector<Binding> result;
	auto pMap = Preferences::get_instance()->getMidiEventMap();
	if ( pAction == nullptr || pMap == nullptr ) {
		return result;
	}
	for ( const auto& binding : pMap->getRegisteredMidiEvents( pAction ) ) {
		result.push_back( binding );
	}
	return result;
}

BString bindingText( const Binding& binding )
{
	BString sText = toBString( MidiEvent::TypeToQString( binding.first ) );
	if ( binding.first == MidiEvent::Type::Note || binding.first == MidiEvent::Type::CC ) {
		sText << " " << static_cast<int>( binding.second );
	}
	return sText;
}

BString actionText( const std::shared_ptr<MidiAction>& pAction )
{
	if ( pAction == nullptr ) {
		return BString();
	}
	BString sText = toBString( MidiAction::typeToQString( pAction->getType() ) );
	// The parameters the action has, guarded: the getters assert otherwise.
	const auto requires = pAction->getRequires();
	if ( requires & MidiAction::RequiresInstrument ) {
		sText << " " << pAction->getInstrument();
	}
	if ( requires & MidiAction::RequiresFx ) {
		sText << " " << pAction->getFx();
	}
	return sText;
}

class BindingCommand : public UndoCommand
{
public:
	BindingCommand( const char* sName, const Binding& binding, std::shared_ptr<MidiAction> pAction, bool bAdd )
		: UndoCommand( sName ), m_binding( binding ), m_pAction( pAction ), m_bAdd( bAdd ) {}

	void redo() override { apply( m_bAdd ); }
	void undo() override { apply( ! m_bAdd ); }

private:
	void apply( bool bAdd )
	{
		auto pMap = Preferences::get_instance()->getMidiEventMap();
		if ( bAdd ) {
			pMap->registerEvent( m_binding.first, m_binding.second, m_pAction, Event::Trigger::Default );
		}
		else {
			pMap->removeRegisteredEvent( m_binding.first, m_binding.second, m_pAction );
		}
	}

	Binding m_binding;
	std::shared_ptr<MidiAction> m_pAction;
	bool m_bAdd;
};

std::unique_ptr<UndoCommand> bind( const Binding& binding, std::shared_ptr<MidiAction> pAction, bool bAdd )
{
	return std::make_unique<BindingCommand>( bAdd ? B_TRANSLATE( "bind MIDI event" ) : B_TRANSLATE( "unbind MIDI event" ),
											 binding, pAction, bAdd );
}

std::unique_ptr<UndoCommand> clear( std::shared_ptr<MidiAction> pAction )
{
	auto pMacro = std::make_unique<MacroCommand>( B_TRANSLATE( "clear MIDI bindings" ) );
	for ( const auto& binding : bindings( pAction ) ) {
		pMacro->add( bind( binding, pAction, false ) );
	}
	return pMacro;
}

// ---------------------------------------------------------------------------
// The learn window (upstream's MidiSenseWidget)

enum : uint32 {
	kMsgPoll = 'MLpo',
	kMsgClear = 'MLcl',
};

class LearnWindow : public BWindow
{
public:
	LearnWindow( std::shared_ptr<MidiAction> pAction, BRect near )
		: BWindow( BRect( 0, 0, 340, 160 ), B_TRANSLATE( "Bind MIDI event" ), B_FLOATING_WINDOW,
				   B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE )
		, m_pAction( pAction )
	{
		BStringView* pName = new BStringView( "action", pAction != nullptr ? actionText( pAction )
																	   : BString( B_TRANSLATE( "No MIDI action" ) ) );
		pName->SetFont( be_bold_font );
		m_pBound = new BStringView( "bound", "" );
		m_pStatus = new BStringView( "status", "" );
		m_pClear = new BButton( "clear", B_TRANSLATE( "Clear bindings" ), new BMessage( kMsgClear ) );
		BButton* pCancel = new BButton( "cancel", B_TRANSLATE( "Cancel" ), new BMessage( B_QUIT_REQUESTED ) );

		BLayoutBuilder::Group<>( this, B_VERTICAL )
			.SetInsets( B_USE_WINDOW_INSETS )
			.Add( pName )
			.Add( m_pBound )
			.Add( m_pStatus )
			.AddGroup( B_HORIZONTAL )
				.Add( m_pClear )
				.AddGlue()
				.Add( pCancel )
			.End();

		updateBindings();
		if ( pAction == nullptr ) {
			// Upstream: "This element is not MIDI operable."
			m_pStatus->SetText( B_TRANSLATE( "This control can't be controlled by MIDI." ) );
		}
		else {
			const auto channel = Preferences::get_instance()->m_midiActionChannel;
			BString sStatus;
			if ( channel == Midi::ChannelAll ) {
				sStatus = B_TRANSLATE( "Waiting for MIDI input" B_UTF8_ELLIPSIS );
			}
			else {
				// Controllers on other channels never arrive here (core).
				sStatus.SetToFormat( B_TRANSLATE( "Waiting for MIDI input on channel %d" B_UTF8_ELLIPSIS ),
									 static_cast<int>( channel ) );
			}
			m_pStatus->SetText( sStatus );
			auto pHydrogen = Hydrogen::get_instance();
			pHydrogen->setLastMidiEvent( MidiEvent::Type::Null );
			pHydrogen->setLastMidiEventParameter( Midi::ParameterInvalid );
			BMessage poll( kMsgPoll );
			m_pRunner = new BMessageRunner( BMessenger( this ), &poll, 100000 );
		}
		MoveTo( near.LeftBottom() + BPoint( 0, 8 ) );
		MoveOnScreen();
	}

	~LearnWindow() { delete m_pRunner; }

	void MessageReceived( BMessage* pMessage ) override
	{
		switch ( pMessage->what ) {
		case kMsgPoll: {
			auto pHydrogen = Hydrogen::get_instance();
			const auto type = pHydrogen->getLastMidiEvent();
			if ( type == MidiEvent::Type::Null ) {
				break;
			}
			const Binding binding( type, pHydrogen->getLastMidiEventParameter() );
			const auto existing = bindings( m_pAction );
			if ( std::find( existing.begin(), existing.end(), binding ) == existing.end() ) {
				// A copy, as upstream: the control's action may change.
				UndoStack::get().push( bind( binding, std::make_shared<MidiAction>( m_pAction ), true ) );
				undoChanged();
			}
			Quit();
			break;
		}
		case kMsgClear:
			UndoStack::get().push( clear( m_pAction ) );
			undoChanged();
			updateBindings();
			break;
		default:
			BWindow::MessageReceived( pMessage );
		}
	}

private:
	void updateBindings()
	{
		const auto existing = bindings( m_pAction );
		BString sBound;
		if ( existing.empty() ) {
			sBound = B_TRANSLATE( "Not bound yet." );
		}
		else {
			sBound = B_TRANSLATE( "Bound to:" );
			for ( size_t i = 0; i < existing.size(); i++ ) {
				sBound << ( i == 0 ? " " : ", " ) << bindingText( existing[ i ] );
			}
		}
		m_pBound->SetText( sBound );
		m_pClear->SetEnabled( ! existing.empty() );
	}

	std::shared_ptr<MidiAction> m_pAction;
	BStringView* m_pBound;
	BStringView* m_pStatus;
	BButton* m_pClear;
	BMessageRunner* m_pRunner = nullptr;
};

static BMessenger sLearnWindow;

void open( std::shared_ptr<MidiAction> pAction, BView* pSource )
{
	if ( sLearnWindow.IsValid() ) {
		sLearnWindow.SendMessage( B_QUIT_REQUESTED );
	}
	BRect near = pSource->ConvertToScreen( pSource->Bounds() );
	LearnWindow* pWindow = new LearnWindow( pAction, near );
	sLearnWindow = BMessenger( pWindow );
	pWindow->Show();
}

}
