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

#include "InstrumentParameters.h"

#include <algorithm>
#include <cmath>

#include <Box.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <Window.h>

#include <core/Basics/Adsr.h>
#include <core/Basics/Instrument.h>
#include <core/CoreActionController.h>
#include <core/Hydrogen.h>
#include <core/Midi/Midi.h>

#include "Controls.h"
#include "NameWindow.h"
#include "SongActions.h"
#include "UndoStack.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InstrumentParameters"

using namespace H2Core;

enum : uint32 {
	kMsgAttack = 'IPat', kMsgDecay = 'IPde', kMsgSustain = 'IPsu', kMsgRelease = 'IPre',
	kMsgGain = 'IPga', kMsgMuteGroup = 'IPmg', kMsgAutoStop = 'IPas', kMsgApplyVelocity = 'IPav',
	kMsgFilterActive = 'IPfa', kMsgCutoff = 'IPcu', kMsgResonance = 'IPrs',
	kMsgCoarse = 'IPco', kMsgFine = 'IPfi', kMsgRandom = 'IPra',
	kMsgMidiChannel = 'IPmc', kMsgMidiNote = 'IPmn',
	kMsgHihatGroup = 'IPhg', kMsgHihatMin = 'IPhl', kMsgHihatMax = 'IPhh',
	kMsgRename = 'IPrn', kMsgDoRename = 'IPdr',
};

// Upstream's pitch knobs: coarse in whole semitones, fine ±0.5.
static const float kPitchFine = 0.5f;

/** The instrument's name, large and centred; double-click renames it. */
class NameBanner : public BStringView
{
public:
	NameBanner() : BStringView( "instrument name", "" )
	{
		BFont font( be_bold_font );
		font.SetSize( std::round( be_bold_font->Size() * 1.6f ) );
		SetFont( &font );
		SetAlignment( B_ALIGN_CENTER );
		SetExplicitMaxSize( BSize( B_SIZE_UNLIMITED, B_SIZE_UNSET ) );
		SetToolTip( B_TRANSLATE( "Double-click to rename" ) );
	}

	void MouseDown( BPoint ) override
	{
		int32 nClicks = 1;
		if ( Window()->CurrentMessage() != nullptr ) {
			Window()->CurrentMessage()->FindInt32( "clicks", &nClicks );
		}
		if ( nClicks == 2 && Parent() != nullptr ) {
			BMessenger( Parent() ).SendMessage( kMsgRename );
		}
	}
};

/** A knob with its label underneath. */
static BView* knobColumn( Knob* pKnob, const char* sLabel )
{
	BStringView* pLabel = new BStringView( nullptr, sLabel );
	BFont font( be_plain_font );
	font.SetSize( std::max( 9.0f, be_plain_font->Size() * 0.8f ) );
	pLabel->SetFont( &font );
	pLabel->SetAlignment( B_ALIGN_CENTER );
	BGroupView* pColumn = new BGroupView( B_VERTICAL, 0 );
	BLayoutBuilder::Group<>( pColumn )
		.Add( pKnob, 0 )
		.Add( pLabel );
	pKnob->SetExplicitAlignment( BAlignment( B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_TOP ) );
	return pColumn;
}

static BBox* section( const char* sTitle, BView* pContent )
{
	BBox* pBox = new BBox( sTitle );
	pBox->SetLabel( sTitle );
	BGroupLayout* pLayout = new BGroupLayout( B_VERTICAL );
	pBox->SetLayout( pLayout );
	pLayout->SetInsets( B_USE_HALF_ITEM_INSETS, B_USE_ITEM_INSETS, B_USE_HALF_ITEM_INSETS,
						B_USE_HALF_ITEM_INSETS );
	pLayout->AddView( pContent );
	return pBox;
}

static BStringView* readout()
{
	BStringView* pView = new BStringView( nullptr, "0.00" );
	pView->SetFont( be_fixed_font );
	pView->SetAlignment( B_ALIGN_RIGHT );
	pView->SetExplicitMinSize( BSize( be_fixed_font->StringWidth( "-00.00" ) + 6, B_SIZE_UNSET ) );
	return pView;
}

InstrumentParameters::InstrumentParameters()
	: BView( B_TRANSLATE( "Parameters" ), 0 )
	, m_bUpdating( false )
{
	SetViewUIColor( B_PANEL_BACKGROUND_COLOR );

	m_pName = new NameBanner();

	// Ranges and defaults from upstream's InstrumentEditor.
	m_pAttack = new Knob( "attack", new BMessage( kMsgAttack ), 0, 1, 0 );
	m_pDecay = new Knob( "decay", new BMessage( kMsgDecay ), 0, 1, 0 );
	m_pSustain = new Knob( "sustain", new BMessage( kMsgSustain ), 0, 1, 1 );
	m_pRelease = new Knob( "release", new BMessage( kMsgRelease ), 0, 1, 0.09f );

	m_pGainValue = readout();
	m_pGain = new Knob( "gain", new BMessage( kMsgGain ), 0, 5, 1 );
	m_pMuteGroup = new FormattedSpinner( "mute group", B_TRANSLATE( "Mute group:" ),
										 new BMessage( kMsgMuteGroup ),
										 FormattedSpinner::offAt( -1 ) );
	m_pMuteGroup->SetRange( -1, 100 );
	m_pMuteGroup->SetToolTip( B_TRANSLATE( "Instruments in one group cut each other off" ) );
	m_pAutoStop = new BCheckBox( "auto stop", B_TRANSLATE( "Auto-stop note" ),
								 new BMessage( kMsgAutoStop ) );
	m_pAutoStop->SetToolTip( B_TRANSLATE( "Stop the previous note when a new one starts" ) );
	m_pApplyVelocity = new BCheckBox( "apply velocity", B_TRANSLATE( "Apply velocity" ),
									  new BMessage( kMsgApplyVelocity ) );
	m_pApplyVelocity->SetToolTip( B_TRANSLATE( "Change the layers' gain based on velocity" ) );

	m_pFilterActive = new BCheckBox( "filter", B_TRANSLATE( "Active" ),
									 new BMessage( kMsgFilterActive ) );
	m_pCutoff = new Knob( "cutoff", new BMessage( kMsgCutoff ), 0, 1, 1 );
	m_pResonance = new Knob( "resonance", new BMessage( kMsgResonance ), 0, 1, 0 );

	m_pPitchValue = readout();
	m_pCoarse = new Knob( "coarse", new BMessage( kMsgCoarse ),
						  Instrument::fPitchOffsetMinimum + kPitchFine,
						  Instrument::fPitchOffsetMaximum - kPitchFine, 0, Knob::Style::Center, true );
	m_pFine = new Knob( "fine", new BMessage( kMsgFine ), -kPitchFine, kPitchFine, 0,
						Knob::Style::Center );
	m_pRandom = new Knob( "random", new BMessage( kMsgRandom ), 0, 1, 0 );

	m_pMidiChannel = new FormattedSpinner( "midi channel", B_TRANSLATE( "Channel:" ),
										   new BMessage( kMsgMidiChannel ),
										   FormattedSpinner::offAt( 0 ) );
	m_pMidiChannel->SetRange( 0, 16 );
	m_pMidiNote = new FormattedSpinner( "midi note", B_TRANSLATE( "Note:" ),
										new BMessage( kMsgMidiNote ), FormattedSpinner::midiNote() );
	m_pMidiNote->SetRange( 0, 127 );

	m_pHihatGroup = new FormattedSpinner( "hihat group", B_TRANSLATE( "Pressure group:" ),
										  new BMessage( kMsgHihatGroup ),
										  FormattedSpinner::offAt( -1 ) );
	m_pHihatGroup->SetRange( -1, 32 );
	m_pHihatMin = new FormattedSpinner( "hihat min", B_TRANSLATE( "Range:" ),
										new BMessage( kMsgHihatMin ), nullptr );
	m_pHihatMin->SetRange( 0, 127 );
	m_pHihatMax = new FormattedSpinner( "hihat max", "–", new BMessage( kMsgHihatMax ), nullptr );
	m_pHihatMax->SetRange( 0, 127 );

	BGroupView* pEnvelope = new BGroupView( B_HORIZONTAL, 0 );
	BLayoutBuilder::Group<>( pEnvelope )
		.Add( knobColumn( m_pAttack, B_TRANSLATE( "Attack" ) ) )
		.Add( knobColumn( m_pDecay, B_TRANSLATE( "Decay" ) ) )
		.Add( knobColumn( m_pSustain, B_TRANSLATE( "Sustain" ) ) )
		.Add( knobColumn( m_pRelease, B_TRANSLATE( "Release" ) ) );

	BGroupView* pGain = new BGroupView( B_VERTICAL, B_USE_HALF_ITEM_SPACING );
	BLayoutBuilder::Group<>( pGain )
		.AddGroup( B_HORIZONTAL, B_USE_HALF_ITEM_SPACING )
			.Add( m_pGainValue )
			.Add( knobColumn( m_pGain, B_TRANSLATE( "Gain" ) ) )
			.AddGlue()
			.Add( m_pMuteGroup )
		.End()
		.AddGroup( B_HORIZONTAL )
			.Add( m_pAutoStop )
			.Add( m_pApplyVelocity )
			.AddGlue()
		.End();

	BGroupView* pFilter = new BGroupView( B_HORIZONTAL, B_USE_HALF_ITEM_SPACING );
	BLayoutBuilder::Group<>( pFilter )
		.Add( m_pFilterActive )
		.AddGlue()
		.Add( knobColumn( m_pCutoff, B_TRANSLATE( "Cutoff" ) ) )
		.Add( knobColumn( m_pResonance, B_TRANSLATE( "Resonance" ) ) );

	BGroupView* pPitch = new BGroupView( B_HORIZONTAL, 0 );
	BLayoutBuilder::Group<>( pPitch )
		.Add( m_pPitchValue )
		.AddStrut( B_USE_HALF_ITEM_SPACING )
		.Add( knobColumn( m_pCoarse, B_TRANSLATE( "Coarse" ) ) )
		.Add( knobColumn( m_pFine, B_TRANSLATE( "Fine" ) ) )
		.Add( knobColumn( m_pRandom, B_TRANSLATE( "Random" ) ) );

	BGroupView* pMidi = new BGroupView( B_HORIZONTAL, B_USE_HALF_ITEM_SPACING );
	BLayoutBuilder::Group<>( pMidi )
		.Add( m_pMidiChannel )
		.Add( m_pMidiNote );

	BGroupView* pHihat = new BGroupView( B_VERTICAL, B_USE_HALF_ITEM_SPACING );
	BLayoutBuilder::Group<>( pHihat )
		.Add( m_pHihatGroup )
		.AddGroup( B_HORIZONTAL, B_USE_HALF_ITEM_SPACING )
			.Add( m_pHihatMin )
			.Add( m_pHihatMax )
		.End();

	BLayoutBuilder::Group<>( this, B_VERTICAL, B_USE_HALF_ITEM_SPACING )
		.SetInsets( B_USE_HALF_ITEM_INSETS )
		.Add( m_pName )
		.Add( section( B_TRANSLATE( "Envelope" ), pEnvelope ) )
		.Add( section( B_TRANSLATE( "Gain" ), pGain ) )
		.Add( section( B_TRANSLATE( "Filter" ), pFilter ) )
		.Add( section( B_TRANSLATE( "Pitch" ), pPitch ) )
		.Add( section( B_TRANSLATE( "MIDI output" ), pMidi ) )
		.Add( section( B_TRANSLATE( "Hi-hat" ), pHihat ) )
		.AddGlue();
}

void InstrumentParameters::AttachedToWindow()
{
	BView::AttachedToWindow();
	for ( BControl* pControl : std::initializer_list<BControl*>{
			  m_pAttack, m_pDecay, m_pSustain, m_pRelease, m_pGain, m_pMuteGroup, m_pAutoStop,
			  m_pApplyVelocity, m_pFilterActive, m_pCutoff, m_pResonance, m_pCoarse, m_pFine,
			  m_pRandom, m_pMidiChannel, m_pMidiNote, m_pHihatGroup, m_pHihatMin, m_pHihatMax } ) {
		pControl->SetTarget( this );
	}
	refresh();
}

void InstrumentParameters::refresh()
{
	auto pInstrument = Hydrogen::get_instance()->getSelectedInstrument();
	const bool bHas = pInstrument != nullptr;
	m_bUpdating = true;

	for ( BControl* pControl : std::initializer_list<BControl*>{
			  m_pAttack, m_pDecay, m_pSustain, m_pRelease, m_pGain, m_pMuteGroup, m_pAutoStop,
			  m_pApplyVelocity, m_pFilterActive, m_pCoarse, m_pFine, m_pRandom, m_pMidiChannel,
			  m_pMidiNote, m_pHihatGroup, m_pHihatMin, m_pHihatMax } ) {
		pControl->SetEnabled( bHas );
	}
	if ( ! bHas ) {
		m_pName->SetText( B_TRANSLATE( "No instrument" ) );
		m_pGainValue->SetText( "" );
		m_pPitchValue->SetText( "" );
		m_pCutoff->SetEnabled( false );
		m_pResonance->SetEnabled( false );
		m_bUpdating = false;
		return;
	}

	m_pName->SetText( pInstrument->getName().toUtf8().constData() );

	// Inverse of the knob mappings below.
	auto pAdsr = pInstrument->getAdsr();
	m_pAttack->setValue( std::sqrt( pAdsr->getAttack() / 100000.0f ) );
	m_pDecay->setValue( std::sqrt( pAdsr->getDecay() / 100000.0f ) );
	m_pSustain->setValue( pAdsr->getSustain() );
	m_pRelease->setValue( std::sqrt( std::max( pAdsr->getRelease() - 256.0f, 0.0f ) / 100000.0f ) );

	m_pGain->setValue( pInstrument->getGain() );
	BString sGain;
	sGain.SetToFormat( "%.2f", pInstrument->getGain() );
	m_pGainValue->SetText( sGain );
	m_pMuteGroup->SetValue( pInstrument->getMuteGroup() );
	m_pAutoStop->SetValue( pInstrument->isStopNotes() );
	m_pApplyVelocity->SetValue( pInstrument->getApplyVelocity() );

	m_pFilterActive->SetValue( pInstrument->isFilterActive() );
	m_pCutoff->setValue( pInstrument->getFilterCutoff() );
	m_pResonance->setValue( pInstrument->getFilterResonance() );
	m_pCutoff->SetEnabled( pInstrument->isFilterActive() );
	m_pResonance->SetEnabled( pInstrument->isFilterActive() );

	// Split the offset into coarse + fine the way upstream does, so a
	// fine knob resting at ±0.5 doesn't flip the coarse one.
	const float fPitch = pInstrument->getPitchOffset();
	float fCoarse = std::round( fPitch );
	if ( m_pFine->value() == kPitchFine && fPitch - std::trunc( fPitch ) == kPitchFine ) {
		fCoarse = std::trunc( fPitch );
	}
	else if ( m_pFine->value() == -kPitchFine && fPitch - std::trunc( fPitch ) == -kPitchFine ) {
		fCoarse = std::ceil( fPitch );
	}
	m_pCoarse->setValue( fCoarse );
	m_pFine->setValue( fPitch - fCoarse );
	BString sPitch;
	sPitch.SetToFormat( "%.2f", fPitch );
	m_pPitchValue->SetText( sPitch );
	m_pRandom->setValue( pInstrument->getRandomPitchFactor() );

	const int nChannel = static_cast<int>( pInstrument->getMidiOutChannel() );
	m_pMidiChannel->SetValue( nChannel >= 1 && nChannel <= 16 ? nChannel : 0 );
	m_pMidiNote->SetValue( static_cast<int>( pInstrument->getMidiOutNote() ) );

	m_pHihatGroup->SetValue( pInstrument->getHihatGrp() );
	m_pHihatMin->SetValue( static_cast<int>( pInstrument->getLowerCc() ) );
	m_pHihatMax->SetValue( static_cast<int>( pInstrument->getHigherCc() ) );

	m_bUpdating = false;
}

void InstrumentParameters::MessageReceived( BMessage* pMessage )
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pInstrument = pHydrogen->getSelectedInstrument();
	if ( pMessage->what == kMsgRename ) {
		if ( pInstrument != nullptr ) {
			BMessage reply( kMsgDoRename );
			reply.AddInt32( "instrument", pHydrogen->getSelectedInstrumentNumber() );
			( new NameWindow( B_TRANSLATE( "Rename instrument" ), B_TRANSLATE( "Rename" ),
							  pInstrument->getName().toUtf8().constData(), BMessenger( this ),
							  reply, Window()->Frame() ) )->Show();
		}
		return;
	}
	if ( pMessage->what == kMsgDoRename ) {
		int32 nInstrument = -1;
		BString sName;
		if ( pMessage->FindInt32( "instrument", &nInstrument ) == B_OK &&
			 pMessage->FindString( "name", &sName ) == B_OK && ! sName.Trim().IsEmpty() ) {
			if ( auto pCommand = SongActions::renameInstrument( nInstrument, sName ) ) {
				UndoStack::get().push( std::move( pCommand ) );
				Window()->PostMessage( 'Hped' );
			}
		}
		return;
	}
	if ( m_bUpdating || pInstrument == nullptr ) {
		BView::MessageReceived( pMessage );
		return;
	}

	float fValue = 0;
	pMessage->FindFloat( "value", &fValue );

	// BSpinner sends its message even when set from refresh(), and that
	// arrives after it: only a real change may touch the instrument, or
	// selecting an instrument would mark the drumkit modified.
	auto unchanged = [&]() {
		switch ( pMessage->what ) {
		case kMsgMuteGroup: return m_pMuteGroup->Value() == pInstrument->getMuteGroup();
		case kMsgMidiChannel: {
			const int n = static_cast<int>( pInstrument->getMidiOutChannel() );
			return m_pMidiChannel->Value() == ( n >= 1 && n <= 16 ? n : 0 );
		}
		case kMsgMidiNote:
			return m_pMidiNote->Value() == static_cast<int>( pInstrument->getMidiOutNote() );
		case kMsgHihatGroup: return m_pHihatGroup->Value() == pInstrument->getHihatGrp();
		case kMsgHihatMin:
			return m_pHihatMin->Value() == static_cast<int>( pInstrument->getLowerCc() );
		case kMsgHihatMax:
			return m_pHihatMax->Value() == static_cast<int>( pInstrument->getHigherCc() );
		default: return false;
		}
	};
	if ( unchanged() ) {
		return;
	}
	auto pAdsr = pInstrument->getAdsr();
	const int nIndex = pHydrogen->getSelectedInstrumentNumber();
	bool bRefresh = false;

	switch ( pMessage->what ) {
	case kMsgAttack:
		pAdsr->setAttack( 100000 * fValue * fValue );
		break;
	case kMsgDecay:
		pAdsr->setDecay( 100000 * fValue * fValue );
		break;
	case kMsgSustain:
		pAdsr->setSustain( fValue );
		break;
	case kMsgRelease:
		pAdsr->setRelease( 256.0 + 100000 * fValue * fValue );
		break;
	case kMsgGain:
		pInstrument->setGain( fValue );
		bRefresh = true;
		break;
	case kMsgMuteGroup:
		pInstrument->setMuteGroup( m_pMuteGroup->Value() );
		break;
	case kMsgAutoStop:
		pInstrument->setStopNotes( m_pAutoStop->Value() == B_CONTROL_ON );
		break;
	case kMsgApplyVelocity:
		pInstrument->setApplyVelocity( m_pApplyVelocity->Value() == B_CONTROL_ON );
		break;
	case kMsgFilterActive:
		pInstrument->setFilterActive( m_pFilterActive->Value() == B_CONTROL_ON );
		bRefresh = true;
		break;
	case kMsgCutoff:
		pInstrument->setFilterCutoff( fValue );
		break;
	case kMsgResonance:
		// Upstream keeps resonance below 0.95 even with the knob at 1.
		pInstrument->setFilterResonance( std::min( 0.95f, fValue ) );
		break;
	case kMsgCoarse:
	case kMsgFine:
		pInstrument->setPitchOffset( std::round( m_pCoarse->value() ) + m_pFine->value() );
		bRefresh = true;
		break;
	case kMsgRandom:
		pInstrument->setRandomPitchFactor( fValue );
		break;
	case kMsgMidiChannel:
		CoreActionController::setInstrumentMidiOutChannel(
			nIndex, Midi::channelFromInt( m_pMidiChannel->Value() ), nullptr );
		break;
	case kMsgMidiNote:
		CoreActionController::setInstrumentMidiOutNote(
			nIndex, Midi::noteFromIntClamp( m_pMidiNote->Value() ), nullptr );
		break;
	case kMsgHihatGroup:
		pInstrument->setHihatGrp( m_pHihatGroup->Value() );
		break;
	case kMsgHihatMin:
		pInstrument->setLowerCc( Midi::parameterFromIntClamp( m_pHihatMin->Value() ) );
		m_pHihatMax->SetMinValue( m_pHihatMin->Value() );
		break;
	case kMsgHihatMax:
		pInstrument->setHigherCc( Midi::parameterFromIntClamp( m_pHihatMax->Value() ) );
		m_pHihatMin->SetMaxValue( m_pHihatMax->Value() );
		break;
	default:
		BView::MessageReceived( pMessage );
		return;
	}
	pHydrogen->setDrumkitModified( true );
	if ( bRefresh ) {
		refresh();
	}
}
