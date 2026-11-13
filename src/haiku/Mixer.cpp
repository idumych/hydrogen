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

#include "Mixer.h"

#include <algorithm>
#include <cmath>

#include <Catalog.h>
#include <GradientLinear.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <ScrollView.h>

#include <core/AudioEngine/AudioEngine.h>
#include <core/Basics/Drumkit.h>
#include <core/Basics/Instrument.h>
#include <core/Basics/InstrumentList.h>
#include <core/Basics/Note.h>
#include <core/Basics/Song.h>
#include <core/CoreActionController.h>
#include <core/FX/Effects.h>
#include <core/FX/LadspaFX.h>
#include <core/Globals.h>
#include <core/Hydrogen.h>
#include <core/IO/AudioDriver.h>
#include <core/Preferences/Preferences.h>
#include <core/Preferences/Theme.h>
#include <core/Sampler/Sampler.h>

#include "Colors.h"
#include "Controls.h"
#include "EffectWindow.h"
#include "Settings.h"
#include "TransportBar.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Mixer"

using namespace H2Core;

enum : uint32 {
	kMsgPeaks = 'MXpk',
	kMsgPlay = 'MXpl', kMsgMute = 'MXmu', kMsgSolo = 'MXso', kMsgPan = 'MXpa', kMsgSend = 'MXfx',
	kMsgVolume = 'MXvo', kMsgSelect = 'MXse',
	kMsgMasterVolume = 'MXmv', kMsgMasterMute = 'MXmm', kMsgHumanizeVelocity = 'MXhv',
	kMsgHumanizeTime = 'MXht', kMsgSwing = 'MXsw', kMsgShowFX = 'MXsf', kMsgShowPeaks = 'MXsp',
	kMsgBypass = 'MXby', kMsgChooseFX = 'MXcf', kMsgReturn = 'MXre',
};

static const float kStripWidth = 56;
static const float kStripHeight = 254;
static const float kMixerHeight = 284;
static const float kFXWidth = 213;
static const float kMasterWidth = 126;
static const int kLEDCycles = 2;	// 50 ms ticks the note LED stays on

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

static float falloffSpeed()
{
	auto pInterface = Preferences::get_instance()->getInterfaceTheme();
	return pInterface != nullptr ? pInterface->m_fMixerFalloffSpeed : InterfaceTheme::FALLOFF_NORMAL;
}

/** Upstream's peak readout: the highest peak, held a while, then
 * decaying by 0.05 per tick; red above 1.0 (clipping). */
struct PeakReadout {
	float fOldMax = 0;
	int nHold = 0;
	BString sText = "0.00";
	bool bRed = false;

	/** Returns true when the text or colour changed. */
	bool update( float fMax )
	{
		const BString sOld = sText;
		const bool bOldRed = bRed;
		if ( fMax >= fOldMax ) {
			const float fShown = fMax < 0.1f ? 0 : fMax;
			sText.SetToFormat( "%.2f", fShown );
			nHold = (int) ( falloffSpeed() * 20 - 2 );
			fOldMax = fMax;
		}
		else if ( nHold < 0 ) {
			fOldMax = std::max( 0.0f, fOldMax - 0.05f );
			sText.SetToFormat( "%.2f", fMax < 0.1f ? 0.0f : fMax );
		}
		else {
			nHold--;
		}
		bRed = fOldMax > 1.0f;
		return sText != sOld || bRed != bOldRed;
	}
};

static void drawReadout( BView* pView, BRect rect, const PeakReadout& readout )
{
	pView->SetHighColor( 49, 53, 61 );
	pView->FillRect( rect );
	pView->SetHighColor( Colors::darker( make_color( 49, 53, 61 ), 150 ) );
	pView->StrokeRect( rect );
	BFont font( be_fixed_font );
	font.SetSize( std::max( 9.0f, be_fixed_font->Size() * 0.9f ) );
	pView->SetFont( &font );
	font_height fh;
	font.GetHeight( &fh );
	pView->SetHighColor( readout.bRed ? make_color( 240, 70, 70 ) : make_color( 220, 225, 235 ) );
	pView->SetLowColor( 49, 53, 61 );
	pView->DrawString( readout.sText, BPoint( rect.right - 3 - pView->StringWidth( readout.sText ),
											  rect.top + ( rect.Height() + fh.ascent - fh.descent ) / 2 ) );
}

// ---------------------------------------------------------------------------
// TagButton: the canvas-style M / S / BYP / FX / Peak toggles.

class TagButton : public BControl, public MidiLearnable
{
public:
	TagButton( const char* sLabel, BMessage* pMessage, rgb_color on, rgb_color onText, BSize size )
		: BControl( sLabel, sLabel, pMessage, B_WILL_DRAW ), m_on( on ), m_onText( onText )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitSize( size );
		ResizeTo( size );
	}

	void Draw( BRect ) override
	{
		const EditorColors& c = Colors::get();
		BRect r = Bounds();
		const bool bOn = Value() == B_CONTROL_ON;
		const rgb_color bg = ! IsEnabled() ? Colors::darker( c.window, 110 )
			: bOn ? m_on : Colors::darker( c.button, 130 );
		SetHighColor( bg );
		FillRoundRect( r, 2, 2 );
		SetHighColor( Colors::darker( bg, 160 ) );
		StrokeRoundRect( r, 2, 2 );
		BFont font( be_bold_font );
		font.SetSize( std::max( 8.0f, std::min( be_bold_font->Size(), r.Height() - 2 ) ) );
		SetFont( &font );
		font_height fh;
		font.GetHeight( &fh );
		SetHighColor( ! IsEnabled() ? Colors::darker( c.windowText, 160 ) : bOn ? m_onText : c.windowText );
		SetLowColor( bg );
		DrawString( Label(), BPoint( ( r.Width() - StringWidth( Label() ) ) / 2 + 1,
									 ( r.Height() + fh.ascent - fh.descent ) / 2 + 1 ) );
	}

	bool GetToolTipAt( BPoint where, BToolTip** pTip ) override
	{
		updateMidiToolTip( this );
		return BControl::GetToolTipAt( where, pTip );
	}

	void MouseDown( BPoint ) override
	{
		if ( IsEnabled() && ! learnClick( this ) ) {
			SetValue( Value() == B_CONTROL_ON ? B_CONTROL_OFF : B_CONTROL_ON );
			Invoke();
		}
	}

private:
	rgb_color m_on, m_onText;
};

// ---------------------------------------------------------------------------
// FaderView: upstream's linear 0..1.5 fader with an L/R peak meter.

class FaderView : public BControl, public MidiLearnable
{
public:
	FaderView( const char* sName, BMessage* pMessage, BSize size, bool bMaster )
		: BControl( sName, nullptr, pMessage, B_WILL_DRAW | B_NAVIGABLE ), m_bMaster( bMaster )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitSize( size );
		ResizeTo( size );
	}

	float value() const { return m_fValue; }
	void setValue( float f )
	{
		f = std::clamp( f, 0.0f, kMax );
		if ( f != m_fValue ) {
			m_fValue = f;
			BString sTip;
			sTip.SetToFormat( "%.2f", m_fValue );
			SetToolTip( sTip );
			Invalidate();
		}
	}
	float peakL() const { return m_fPeakL; }
	float peakR() const { return m_fPeakR; }
	/** Peaks are linear amplitudes, shown from 0.01 to 1.0 like upstream. */
	void setPeaks( float fL, float fR )
	{
		fL = std::clamp( fL, kMinPeak, 1.0f );
		fR = std::clamp( fR, kMinPeak, 1.0f );
		if ( fL != m_fPeakL || fR != m_fPeakR ) {
			m_fPeakL = fL;
			m_fPeakR = fR;
			Invalidate();
		}
	}

	void Draw( BRect ) override
	{
		const EditorColors& c = Colors::get();
		const BRect b = Bounds();
		SetHighColor( Colors::darker( c.window, 140 ) );
		FillRect( b );
		const float fBarWidth = m_bMaster ? 7 : 6.5f;
		const float xL = m_bMaster ? 1 : 1.5f;
		const float xR = m_bMaster ? 12 : b.Width() - 1.5f - fBarWidth;
		const float fTop = 2, fBottom = b.bottom - 2;
		// Upstream's gradient: red at the top, yellow, then green.
		BGradientLinear gradient( BPoint( 0, fTop ), BPoint( 0, fBottom ) );
		gradient.AddColor( make_color( 230, 40, 40 ), 0 );
		gradient.AddColor( make_color( 230, 40, 40 ), 25 );
		gradient.AddColor( make_color( 240, 220, 40 ), 38 );
		gradient.AddColor( make_color( 240, 220, 40 ), 89 );
		gradient.AddColor( make_color( 60, 200, 60 ), 102 );
		gradient.AddColor( make_color( 60, 200, 60 ), 255 );
		auto bar = [&]( float x, float fPeak ) {
			const float fHeight = ( fPeak - kMinPeak ) / ( 1.0f - kMinPeak ) * ( fBottom - fTop );
			if ( fHeight > 0.5f ) {
				FillRect( BRect( x, fBottom - fHeight, x + fBarWidth, fBottom ), gradient );
			}
		};
		bar( xL, m_fPeakL );
		bar( xR, m_fPeakR );
		SetHighColor( 0, 0, 0 );
		StrokeRect( b );

		// The knob.
		const float fKnobW = m_bMaster ? 19 : 15;
		const float fKnobH = 29;
		const float x = m_bMaster ? b.Width() - fKnobW - 1 : ( b.Width() - fKnobW ) / 2;
		const float y = b.Height() - 1 - ( b.Height() - fKnobH ) * ( m_fValue / kMax ) - fKnobH;
		BRect knob( x, y, x + fKnobW, y + fKnobH );
		const rgb_color face = ui_color( B_CONTROL_BACKGROUND_COLOR );
		BGradientLinear knobGradient( knob.LeftTop(), knob.LeftBottom() );
		knobGradient.AddColor( tint_color( face, B_LIGHTEN_1_TINT ), 0 );
		knobGradient.AddColor( tint_color( face, B_DARKEN_2_TINT ), 255 );
		FillRoundRect( knob, 2, 2, knobGradient );
		SetHighColor( IsFocus() ? ui_color( B_KEYBOARD_NAVIGATION_COLOR ) : tint_color( face, B_DARKEN_4_TINT ) );
		StrokeRoundRect( knob, 2, 2 );
		const float fMid = std::round( knob.top + fKnobH / 2 );
		StrokeLine( BPoint( knob.left + 3, fMid ), BPoint( knob.right - 3, fMid ) );
	}

	bool GetToolTipAt( BPoint where, BToolTip** pTip ) override
	{
		updateMidiToolTip( this );
		return BControl::GetToolTipAt( where, pTip );
	}

	void MouseDown( BPoint where ) override
	{
		if ( learnClick( this ) ) {
			return;
		}
		MakeFocus( true );
		int32 nClicks = 1;
		if ( Window()->CurrentMessage() != nullptr ) {
			Window()->CurrentMessage()->FindInt32( "clicks", &nClicks );
		}
		// Double-click: unity gain.
		if ( nClicks == 2 ) {
			change( 1.0f );
			return;
		}
		m_bDragging = true;
		SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS );
		follow( where );
	}
	void MouseMoved( BPoint where, uint32, const BMessage* ) override
	{
		if ( m_bDragging ) {
			follow( where );
		}
	}
	void MouseUp( BPoint ) override { m_bDragging = false; }

	void KeyDown( const char* pBytes, int32 nBytes ) override
	{
		const float fStep = kMax / 100;
		switch ( pBytes[ 0 ] ) {
		case B_UP_ARROW: change( m_fValue + fStep ); break;
		case B_DOWN_ARROW: change( m_fValue - fStep ); break;
		case B_PAGE_UP: change( m_fValue + 5 * fStep ); break;
		case B_PAGE_DOWN: change( m_fValue - 5 * fStep ); break;
		default: BControl::KeyDown( pBytes, nBytes );
		}
	}

	void MessageReceived( BMessage* pMessage ) override
	{
		if ( pMessage->what == B_MOUSE_WHEEL_CHANGED ) {
			float fDelta = 0;
			pMessage->FindFloat( "be:wheel_delta_y", &fDelta );
			change( m_fValue - fDelta * kMax / 100 );
			return;
		}
		BControl::MessageReceived( pMessage );
	}

private:
	static constexpr float kMax = 1.5f;
	static constexpr float kMinPeak = 0.01f;

	/** Upstream: the knob jumps to where you click (absolute position). */
	void follow( BPoint where )
	{
		const float h = Bounds().Height();
		change( std::clamp( ( h - where.y ) / h, 0.0f, 1.0f ) * kMax );
	}

	void change( float f )
	{
		f = std::clamp( f, 0.0f, kMax );
		if ( f == m_fValue ) {
			return;
		}
		setValue( f );
		BMessage message( *Message() );
		message.AddFloat( "value", m_fValue );
		Invoke( &message );
	}

	bool m_bMaster;
	float m_fValue = 1.0f;
	float m_fPeakL = kMinPeak, m_fPeakR = kMinPeak;
	bool m_bDragging = false;
};

// ---------------------------------------------------------------------------
// MixerStrip: one instrument (upstream's MixerLine, 56×254).

class MixerStrip : public BView
{
public:
	MixerStrip( int nStrip ) : BView( "mixer strip", B_WILL_DRAW ), m_nStrip( nStrip )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitSize( BSize( kStripWidth - 1, kStripHeight - 1 ) );
		const EditorColors& c = Colors::get();

		auto message = [this]( uint32 what, int32 nExtra = -1 ) {
			BMessage* pMessage = new BMessage( what );
			pMessage->AddInt32( "strip", m_nStrip );
			if ( nExtra >= 0 ) {
				pMessage->AddInt32( "send", nExtra );
			}
			return pMessage;
		};
		m_pPlay = new GlyphButton( "play", GlyphButton::Glyph::Play, message( kMsgPlay ), false, 16 );
		m_pPlay->SetToolTip( B_TRANSLATE( "Play the instrument (secondary click stops it)" ) );
		place( m_pPlay, 6, 0 );
		m_pMute = new TagButton( "M", message( kMsgMute ), c.mute, c.muteText, BSize( 21, 14 ) );
		m_pMute->SetToolTip( B_TRANSLATE( "Mute" ) );
		place( m_pMute, 5, 18 );
		m_pSolo = new TagButton( "S", message( kMsgSolo ), c.solo, c.soloText, BSize( 21, 14 ) );
		m_pSolo->SetToolTip( B_TRANSLATE( "Solo" ) );
		place( m_pSolo, 29, 18 );
		m_pPan = new Knob( "pan", message( kMsgPan ), PAN_MIN, PAN_MAX, 0, Knob::Style::Center );
		m_pPan->SetToolTip( B_TRANSLATE( "Pan" ) );
		place( m_pPan, 6, 34 );
		for ( int i = 0; i < MAX_FX; i++ ) {
			m_pSends[ i ] = new Knob( "send", message( kMsgSend, i ), 0, 1, 0, Knob::Style::Small );
			BString sTip;
			sTip.SetToFormat( B_TRANSLATE( "Effect %d send" ), i + 1 );
			m_pSends[ i ]->SetToolTip( sTip );
			place( m_pSends[ i ], i % 2 == 0 ? 9 : 30, 63 + 20 * ( i / 2 ) );
		}
		m_pFader = new FaderView( "volume", message( kMsgVolume ), BSize( 22, 116 ), false );
		m_pFader->SetToolTip( B_TRANSLATE( "Volume" ) );
		place( m_pFader, 27, 132 );

		// Upstream's MixerLine::updateActions(): the strip's index.
		auto action = [nStrip]( MidiAction::Type type ) {
			auto pAction = std::make_shared<MidiAction>( type );
			pAction->setInstrument( nStrip );
			return pAction;
		};
		m_pMute->setMidiAction( action( MidiAction::Type::StripMuteToggle ) );
		m_pSolo->setMidiAction( action( MidiAction::Type::StripSoloToggle ) );
		m_pPan->setMidiAction( action( MidiAction::Type::PanAbsolute ) );
		for ( int i = 0; i < MAX_FX; i++ ) {
			auto pSend = action( MidiAction::Type::EffectLevelAbsolute );
			pSend->setFx( i );
			m_pSends[ i ]->setMidiAction( pSend );
		}
		m_pFader->setMidiAction( action( MidiAction::Type::StripVolumeAbsolute ) );
	}

	void AttachedToWindow() override
	{
		BView::AttachedToWindow();
		for ( BControl* pControl : std::initializer_list<BControl*>{ m_pPlay, m_pMute, m_pSolo, m_pPan,
				  m_pSends[ 0 ], m_pSends[ 1 ], m_pSends[ 2 ], m_pSends[ 3 ], m_pFader } ) {
			if ( pControl != nullptr ) {
				pControl->SetTarget( Window() );
			}
		}
		update();
	}

	std::shared_ptr<Instrument> instrument() const
	{
		auto pSong = Hydrogen::get_instance()->getSong();
		if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
			return nullptr;
		}
		return pSong->getDrumkit()->getInstruments()->get( m_nStrip );
	}

	void update()
	{
		auto pInstrument = instrument();
		if ( pInstrument == nullptr ) {
			return;
		}
		m_pMute->SetValue( pInstrument->isMuted() );
		m_pSolo->SetValue( pInstrument->isSoloed() );
		m_pPan->setValue( pInstrument->getPan() );
		for ( int i = 0; i < MAX_FX; i++ ) {
			m_pSends[ i ]->setValue( pInstrument->getFxLevel( i ) );
		}
		m_pFader->setValue( pInstrument->getVolume() );
		m_sName = toBString( pInstrument->getName() );
		m_bSelected = Hydrogen::get_instance()->getSelectedInstrumentNumber() == m_nStrip;
		Invalidate();
	}

	/** Upstream's MixerLine::updatePeaks(), every 50 ms. */
	void updatePeaks( bool bShowPeaks )
	{
		auto pInstrument = instrument();
		if ( pInstrument == nullptr ) {
			return;
		}
		float fL = bShowPeaks ? pInstrument->getPeak_L() : 0.0f;
		float fR = bShowPeaks ? pInstrument->getPeak_R() : 0.0f;
		pInstrument->setPeak_L( 0.0f );
		pInstrument->setPeak_R( 0.0f );
		const float fFalloff = falloffSpeed();
		if ( fL < m_pFader->peakL() ) {
			fL = m_pFader->peakL() / fFalloff;
		}
		if ( fR < m_pFader->peakR() ) {
			fR = m_pFader->peakR() / fFalloff;
		}
		m_pFader->setPeaks( fL, fR );
		if ( m_readout.update( std::max( fL, fR ) ) ) {
			Invalidate( kReadoutRect );
		}
		if ( m_nLEDCycles > 0 && --m_nLEDCycles == 0 ) {
			Invalidate( kLEDRect );
		}
	}

	void noteStarted()
	{
		m_nLEDCycles = kLEDCycles;
		Invalidate( kLEDRect );
	}

	void Draw( BRect ) override
	{
		const EditorColors& c = Colors::get();
		const BRect b = Bounds();
		SetHighColor( m_bSelected ? Colors::lighter( c.window, 125 ) : c.window );
		FillRect( b );
		SetHighColor( Colors::darker( c.window, 150 ) );
		StrokeLine( b.RightTop(), b.RightBottom() );

		// Note activity and selection LEDs.
		SetHighColor( m_nLEDCycles > 0 ? make_color( 120, 230, 90 ) : Colors::darker( c.window, 130 ) );
		FillRect( BRect( kLEDRect.left, kLEDRect.top, kLEDRect.left + 4, kLEDRect.bottom ) );
		SetHighColor( m_bSelected ? c.highlight : Colors::darker( c.window, 130 ) );
		FillRoundRect( BRect( kLEDRect.left + 9, kLEDRect.top + 2, kLEDRect.right, kLEDRect.bottom - 2 ), 2, 2 );

		drawReadout( this, kReadoutRect, m_readout );

		// The name, written upwards beside the fader.
		BFont font( be_plain_font );
		font.SetRotation( 90 );
		SetFont( &font );
		font_height fh;
		font.GetHeight( &fh );
		BString sName = m_sName;
		font.TruncateString( &sName, B_TRUNCATE_END, 114 );
		SetHighColor( 230, 230, 230 );
		SetDrawingMode( B_OP_OVER );
		DrawString( sName, BPoint( 6 + fh.ascent, 246 ) );
		SetDrawingMode( B_OP_COPY );
	}

	void MouseDown( BPoint where ) override
	{
		// The name area selects the instrument, like upstream.
		if ( BRect( 4, 130, 24, 250 ).Contains( where ) ) {
			Hydrogen::get_instance()->setSelectedInstrumentNumber( m_nStrip );
		}
	}

private:
	static inline const BRect kLEDRect = BRect( 26, 3, 49, 14 );
	static inline const BRect kReadoutRect = BRect( 8, 106, 48, 124 );

	void place( BView* pView, float x, float y )
	{
		AddChild( pView );
		pView->MoveTo( x, y );
		// Placed by hand, not by a layout: give it its natural size.
		pView->ResizeTo( pView->MinSize() );
	}

	int m_nStrip;
	GlyphButton* m_pPlay;
	TagButton* m_pMute;
	TagButton* m_pSolo;
	Knob* m_pPan;
	Knob* m_pSends[ MAX_FX ] = {};
	FaderView* m_pFader;
	BString m_sName;
	bool m_bSelected = false;
	int m_nLEDCycles = 0;
	PeakReadout m_readout;
};

// ---------------------------------------------------------------------------
// FXPanel: the effect slots (upstream's LadspaFXLine × MAX_FX).

class FXPanel : public BView
{
public:
	FXPanel() : BView( "effects", B_WILL_DRAW )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitSize( BSize( kFXWidth - 1, kMixerHeight - 1 ) );
		const EditorColors& c = Colors::get();
		for ( int i = 0; i < MAX_FX; i++ ) {
			const float y = 43 * i + 84;
			BMessage* pBypass = new BMessage( kMsgBypass );
			pBypass->AddInt32( "fx", i );
			m_pBypass[ i ] = new TagButton( B_TRANSLATE( "Bypass" ), pBypass, c.mute, c.muteText,
											BSize( 46, 14 ) );
			AddChild( m_pBypass[ i ] );
			m_pBypass[ i ]->MoveTo( 22, y + 25 );
			BMessage* pChoose = new BMessage( kMsgChooseFX );
			pChoose->AddInt32( "fx", i );
			m_pChoose[ i ] = new TagButton( B_TRANSLATE( "Edit" B_UTF8_ELLIPSIS ), pChoose, c.button,
											c.buttonText, BSize( 62, 14 ) );
			AddChild( m_pChoose[ i ] );
			m_pChoose[ i ]->MoveTo( 72, y + 25 );
			BMessage* pReturn = new BMessage( kMsgReturn );
			pReturn->AddInt32( "fx", i );
			m_pReturn[ i ] = new Knob( "return", pReturn, 0, 1, 1 );
			m_pReturn[ i ]->SetToolTip( B_TRANSLATE( "Return level" ) );
			AddChild( m_pReturn[ i ] );
			m_pReturn[ i ]->MoveTo( 137, y + 4 );
			m_pReturn[ i ]->ResizeTo( m_pReturn[ i ]->MinSize() );
		}
	}

	void AttachedToWindow() override
	{
		BView::AttachedToWindow();
		for ( int i = 0; i < MAX_FX; i++ ) {
			m_pBypass[ i ]->SetTarget( Window() );
			m_pChoose[ i ]->SetTarget( Window() );
			m_pReturn[ i ]->SetTarget( Window() );
		}
		update();
	}

	void update()
	{
		auto pSong = Hydrogen::get_instance()->getSong();
		for ( int i = 0; i < MAX_FX; i++ ) {
			std::shared_ptr<LadspaFX> pFX;
#ifdef H2CORE_HAVE_LADSPA
			if ( pSong != nullptr ) {
				pFX = pSong->getEffects()->getLadspaFX( i );
			}
#endif
			m_sNames[ i ] = pFX != nullptr ? toBString( pFX->getPluginName() ) : BString( B_TRANSLATE( "No plugin" ) );
			m_pBypass[ i ]->SetEnabled( pFX != nullptr );
			m_pBypass[ i ]->SetValue( pFX != nullptr && ! pFX->isEnabled() );
			m_pReturn[ i ]->SetEnabled( pFX != nullptr && pFX->isEnabled() );
			if ( pFX != nullptr ) {
				m_pReturn[ i ]->setValue( pFX->getVolume() );
			}
#ifndef H2CORE_HAVE_LADSPA
			m_pChoose[ i ]->SetEnabled( false );
#endif
		}
		Invalidate();
	}

	void Draw( BRect ) override
	{
		const EditorColors& c = Colors::get();
		const BRect b = Bounds();
		SetHighColor( Colors::darker( c.window, 115 ) );
		FillRect( b );
		BFont bold( be_bold_font );
		SetFont( &bold );
		SetHighColor( c.windowText );
		DrawString( B_TRANSLATE( "Effects" ), BPoint( 13, 74 ) );
		BFont plain( be_plain_font );
		plain.SetSize( std::max( 9.0f, be_plain_font->Size() * 0.85f ) );
		SetFont( &plain );
		for ( int i = 0; i < MAX_FX; i++ ) {
			const float y = 43 * i + 84;
			BRect line( 13, y, 13 + 193, y + 42 );
			SetHighColor( c.window );
			FillRoundRect( line, 3, 3 );
			SetHighColor( Colors::darker( c.window, 150 ) );
			StrokeRoundRect( line, 3, 3 );
			BRect name( 22, y + 6, 132, y + 21 );
			SetHighColor( 49, 53, 61 );
			FillRect( name );
			BString sName = m_sNames[ i ];
			plain.TruncateString( &sName, B_TRUNCATE_END, name.Width() - 6 );
			SetHighColor( 220, 225, 235 );
			SetLowColor( 49, 53, 61 );
			DrawString( sName, BPoint( name.left + 3, name.bottom - 4 ) );
		}
	}

private:
	TagButton* m_pBypass[ MAX_FX ];
	TagButton* m_pChoose[ MAX_FX ];
	Knob* m_pReturn[ MAX_FX ];
	BString m_sNames[ MAX_FX ];
};

// ---------------------------------------------------------------------------
// MasterStrip: master volume, humanize and swing (upstream's MasterLine).

class MasterStrip : public BView
{
public:
	MasterStrip() : BView( "master", B_WILL_DRAW )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitSize( BSize( kMasterWidth - 1, kMixerHeight - 1 ) );
		const EditorColors& c = Colors::get();
		m_pMute = new TagButton( "M", new BMessage( kMsgMasterMute ), c.mute, c.muteText, BSize( 41, 16 ) );
		m_pMute->SetToolTip( B_TRANSLATE( "Mute all" ) );
		place( m_pMute, 20, 31 );
		m_pFader = new FaderView( "master volume", new BMessage( kMsgMasterVolume ), BSize( 33, 188 ), true );
		m_pFader->SetToolTip( B_TRANSLATE( "Master volume" ) );
		place( m_pFader, 24, 75 );
		m_pVelocity = new Knob( "humanize velocity", new BMessage( kMsgHumanizeVelocity ), 0, 1, 0 );
		place( m_pVelocity, 70, 88 );
		m_pTime = new Knob( "humanize time", new BMessage( kMsgHumanizeTime ), 0, 1, 0 );
		place( m_pTime, 70, 125 );
		m_pSwing = new Knob( "swing", new BMessage( kMsgSwing ), 0, 1, 0 );
		m_pSwing->SetToolTip( B_TRANSLATE( "16th-note swing" ) );
		place( m_pSwing, 70, 162 );
		m_pShowFX = new TagButton( B_TRANSLATE( "Effects" ), new BMessage( kMsgShowFX ), c.accent,
								   c.windowText, BSize( 52, 15 ) );
		m_pShowFX->SetToolTip( B_TRANSLATE( "Show the effects" ) );
		place( m_pShowFX, 66, 230 );
		m_pShowPeaks = new TagButton( B_TRANSLATE( "Peaks" ), new BMessage( kMsgShowPeaks ), c.accent,
									  c.windowText, BSize( 52, 15 ) );
		m_pShowPeaks->SetToolTip( B_TRANSLATE( "Show instrument peaks" ) );
		place( m_pShowPeaks, 66, 249 );

		// Upstream's MasterLine.
		m_pMute->setMidiAction( std::make_shared<MidiAction>( MidiAction::Type::MuteToggle ) );
		m_pFader->setMidiAction( std::make_shared<MidiAction>( MidiAction::Type::MasterVolumeAbsolute ) );
		m_pVelocity->setMidiAction( std::make_shared<MidiAction>( MidiAction::Type::HumanizationVelocityAbsolute ) );
		m_pTime->setMidiAction( std::make_shared<MidiAction>( MidiAction::Type::HumanizationTimingAbsolute ) );
		m_pSwing->setMidiAction( std::make_shared<MidiAction>( MidiAction::Type::HumanizationSwingAbsolute ) );
	}

	void AttachedToWindow() override
	{
		BView::AttachedToWindow();
		for ( BControl* pControl : std::initializer_list<BControl*>{ m_pMute, m_pFader, m_pVelocity,
				  m_pTime, m_pSwing, m_pShowFX, m_pShowPeaks } ) {
			pControl->SetTarget( Window() );
		}
		update();
	}

	void update()
	{
		auto pSong = Hydrogen::get_instance()->getSong();
		auto pPref = Preferences::get_instance();
		if ( pSong != nullptr ) {
			m_pFader->setValue( pSong->getVolume() );
			m_pMute->SetValue( pSong->getIsMuted() );
			m_pVelocity->setValue( pSong->getHumanizeVelocityValue() );
			m_pTime->setValue( pSong->getHumanizeTimeValue() );
			m_pSwing->setValue( pSong->getSwingFactor() );
		}
		m_pShowFX->SetValue( pPref->isFXTabVisible() );
		m_pShowPeaks->SetValue( pPref->showInstrumentPeaks() );
	}

	void updatePeaks( bool bShowPeaks )
	{
		auto pAudioEngine = Hydrogen::get_instance()->getAudioEngine();
		float fL = bShowPeaks ? pAudioEngine->getMasterPeak_L() : 0.0f;
		float fR = bShowPeaks ? pAudioEngine->getMasterPeak_R() : 0.0f;
		pAudioEngine->setMasterPeak_L( 0.0f );
		pAudioEngine->setMasterPeak_R( 0.0f );
		const float fFalloff = falloffSpeed();
		if ( fL < m_pFader->peakL() ) {
			fL = m_pFader->peakL() / fFalloff;
		}
		if ( fR < m_pFader->peakR() ) {
			fR = m_pFader->peakR() / fFalloff;
		}
		m_pFader->setPeaks( fL, fR );
		if ( m_readout.update( std::max( fL, fR ) ) ) {
			Invalidate( kReadoutRect );
		}
	}

	void Draw( BRect ) override
	{
		const EditorColors& c = Colors::get();
		SetHighColor( c.window );
		FillRect( Bounds() );
		BFont bold( be_bold_font );
		SetFont( &bold );
		SetHighColor( c.windowText );
		DrawString( B_TRANSLATE( "Master" ), BPoint( 18, 20 ) );
		drawReadout( this, kReadoutRect, m_readout );
		BFont small( be_plain_font );
		small.SetSize( std::max( 8.0f, be_plain_font->Size() * 0.75f ) );
		SetFont( &small );
		SetDrawingMode( B_OP_OVER );
		DrawString( B_TRANSLATE( "Humanize" ), BPoint( 66, 85 ) );
		const char* labels[] = { B_TRANSLATE( "Velocity" ), B_TRANSLATE( "Timing" ), B_TRANSLATE( "Swing" ) };
		for ( int i = 0; i < 3; i++ ) {
			DrawString( labels[ i ], BPoint( 92 - StringWidth( labels[ i ] ) / 2, 123 + 37 * i ) );
		}
		SetDrawingMode( B_OP_COPY );
	}

private:
	static inline const BRect kReadoutRect = BRect( 22, 51, 59, 68 );

	void place( BView* pView, float x, float y )
	{
		AddChild( pView );
		pView->MoveTo( x, y );
		// Placed by hand, not by a layout: give it its natural size.
		pView->ResizeTo( pView->MinSize() );
	}

	TagButton* m_pMute;
	FaderView* m_pFader;
	Knob* m_pVelocity;
	Knob* m_pTime;
	Knob* m_pSwing;
	TagButton* m_pShowFX;
	TagButton* m_pShowPeaks;
	PeakReadout m_readout;
};

// ---------------------------------------------------------------------------
// MixerWindow

static const char* kFrameName = "mixer frame";
static const char* kShownName = "mixer shown";

MixerWindow::MixerWindow()
	: BWindow( Settings::frame( kFrameName, BRect( 60, 400, 60 + 829, 400 + kMixerHeight + 14 ) ),
			   B_TRANSLATE( "Mixer" ), B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE )
{
	m_pStrips = new BGroupView( B_HORIZONTAL, 0 );
	m_pStripPane = new ScrollPane( m_pStrips, B_HORIZONTAL );
	m_pStripPane->SetExplicitMinSize( BSize( 4 * kStripWidth, kMixerHeight - B_H_SCROLL_BAR_HEIGHT - 1 ) );
	BScrollView* pScroll = new BScrollView( "strips", m_pStripPane, 0, true, false, B_NO_BORDER );
	m_pFX = new FXPanel();
	m_pMaster = new MasterStrip();

	BLayoutBuilder::Group<>( this, B_HORIZONTAL, 0 )
		.Add( pScroll )
		.Add( m_pFX )
		.Add( m_pMaster );
	if ( ! Preferences::get_instance()->isFXTabVisible() ) {
		m_pFX->Hide();
	}

	// Built on the window's own thread once it runs (see Show()).
	BMessage peaks( kMsgPeaks );
	m_pPeakRunner = new BMessageRunner( BMessenger( this ), &peaks, 50000 );
}

MixerWindow::~MixerWindow()
{
	delete m_pPeakRunner;
}

void MixerWindow::rebuild()
{
	auto pSong = Hydrogen::get_instance()->getSong();
	const int nInstruments = pSong != nullptr && pSong->getDrumkit() != nullptr
		? pSong->getDrumkit()->getInstruments()->size() : 0;
	if ( nInstruments != (int) m_strips.size() ) {
		for ( MixerStrip* pStrip : m_strips ) {
			pStrip->RemoveSelf();
			delete pStrip;
		}
		m_strips.clear();
		for ( int i = 0; i < nInstruments; i++ ) {
			MixerStrip* pStrip = new MixerStrip( i );
			m_strips.push_back( pStrip );
			m_pStrips->GroupLayout()->AddView( pStrip );
		}
		m_pStripPane->contentChanged();
	}
	for ( MixerStrip* pStrip : m_strips ) {
		pStrip->update();
	}
	m_pFX->update();
	m_pMaster->update();
}

void MixerWindow::updatePeaks()
{
	if ( IsHidden() || IsMinimized() ) {
		return;
	}
	const bool bShowPeaks = Preferences::get_instance()->showInstrumentPeaks();
	for ( MixerStrip* pStrip : m_strips ) {
		pStrip->updatePeaks( bShowPeaks );
	}
	m_pMaster->updatePeaks( bShowPeaks );
}

void MixerWindow::Show()
{
	BWindow::Show();
	// Show() may come from another thread: rebuild in the window's own.
	PostMessage( kMsgMixerRebuild );
}

void MixerWindow::Hide()
{
	BWindow::Hide();
	saveSettings();
}

bool MixerWindow::QuitRequested()
{
	// Closing the mixer hides it; the app quits it.
	if ( ! IsHidden() ) {
		Hide();
	}
	return false;
}

void MixerWindow::saveSettings()
{
	Settings::setFrame( kFrameName, Frame() );
	Settings::get().RemoveName( kShownName );
	Settings::get().AddBool( kShownName, ! IsHidden() );
}

void MixerWindow::MessageReceived( BMessage* pMessage )
{
	auto pHydrogen = Hydrogen::get_instance();
	int32 nStrip = -1;
	pMessage->FindInt32( "strip", &nStrip );
	float fValue = 0;
	pMessage->FindFloat( "value", &fValue );
	auto pSong = pHydrogen->getSong();
	auto pStripInstrument = [&]() -> std::shared_ptr<Instrument> {
		if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
			return nullptr;
		}
		return pSong->getDrumkit()->getInstruments()->get( nStrip );
	};

	switch ( pMessage->what ) {
	case kMsgPeaks:
		updatePeaks();
		break;
	case kMsgMixerRebuild:
		rebuild();
		break;
	case kMsgMixerUpdate:
		for ( MixerStrip* pStrip : m_strips ) {
			pStrip->update();
		}
		break;
	case kMsgMixerMaster:
		m_pMaster->update();
		break;
	case kMsgMixerNotes:
		for ( int32 i = 0; pMessage->FindInt32( "strip", i, &nStrip ) == B_OK; i++ ) {
			if ( nStrip >= 0 && nStrip < (int32) m_strips.size() ) {
				m_strips[ nStrip ]->noteStarted();
			}
		}
		break;

	case kMsgPlay: {
		auto pInstrument = pStripInstrument();
		if ( pInstrument == nullptr ) {
			break;
		}
		int32 nButtons = B_PRIMARY_MOUSE_BUTTON;
		if ( BMessage* pCurrent = CurrentMessage() ) {
			pCurrent->FindInt32( "buttons", &nButtons );
		}
		pHydrogen->setSelectedInstrumentNumber( nStrip );
		// Upstream: play at full velocity; the secondary button stops it.
		auto pNote = std::make_shared<Note>( pInstrument, 0, nButtons & B_SECONDARY_MOUSE_BUTTON ? 0.0f : 1.0f );
		if ( nButtons & B_SECONDARY_MOUSE_BUTTON ) {
			pNote->setNoteOff( true );
		}
		pHydrogen->getAudioEngine()->getSampler()->noteOn( pNote );
		break;
	}
	case kMsgMute:
		if ( auto pInstrument = pStripInstrument() ) {
			CoreActionController::setStripIsMuted( nStrip, ! pInstrument->isMuted(), true );
		}
		break;
	case kMsgSolo:
		if ( auto pInstrument = pStripInstrument() ) {
			CoreActionController::setStripIsSoloed( nStrip, ! pInstrument->isSoloed(), true );
		}
		break;
	case kMsgPan:
		CoreActionController::setStripPanSym( nStrip, fValue, true );
		break;
	case kMsgSend: {
		int32 nSend = 0;
		pMessage->FindInt32( "send", &nSend );
		CoreActionController::setStripEffectLevel( nStrip, nSend, fValue, true );
		break;
	}
	case kMsgVolume:
		CoreActionController::setStripVolume( nStrip, fValue, true );
		break;

	case kMsgMasterVolume:
		CoreActionController::setMasterVolume( fValue );
		pHydrogen->setSongModified( true );
		break;
	case kMsgMasterMute:
		if ( pSong != nullptr ) {
			CoreActionController::setMasterIsMuted( ! pSong->getIsMuted() );
		}
		break;
	case kMsgHumanizeVelocity:
		CoreActionController::setHumanizeVelocity( fValue );
		break;
	case kMsgHumanizeTime:
		CoreActionController::setHumanizeTime( fValue );
		break;
	case kMsgSwing:
		CoreActionController::setSwing( fValue );
		break;
	case kMsgShowFX: {
		auto pPref = Preferences::get_instance();
		pPref->setFXTabVisible( ! pPref->isFXTabVisible() );
		if ( pPref->isFXTabVisible() ) {
			m_pFX->Show();
		}
		else {
			m_pFX->Hide();
		}
		m_pMaster->update();
		break;
	}
	case kMsgShowPeaks: {
		auto pPref = Preferences::get_instance();
		pPref->setInstrumentPeaks( ! pPref->showInstrumentPeaks() );
		m_pMaster->update();
		break;
	}

#ifdef H2CORE_HAVE_LADSPA
	case kMsgBypass: {
		int32 nFX = 0;
		pMessage->FindInt32( "fx", &nFX );
		if ( pSong != nullptr ) {
			if ( auto pFX = pSong->getEffects()->getLadspaFX( nFX ) ) {
				pFX->setEnabled( ! pFX->isEnabled() );
				pHydrogen->setSongModified( true );
			}
		}
		m_pFX->update();
		break;
	}
	case kMsgReturn: {
		int32 nFX = 0;
		pMessage->FindInt32( "fx", &nFX );
		if ( pSong != nullptr ) {
			if ( auto pFX = pSong->getEffects()->getLadspaFX( nFX ) ) {
				pFX->setVolume( fValue );
				pHydrogen->setSongModified( true );
			}
		}
		break;
	}
	case kMsgChooseFX: {
		// Upstream's "Edit" button: the slot's window, with the plugin menu.
		int32 nFX = 0;
		pMessage->FindInt32( "fx", &nFX );
		EffectWindow::open( nFX, BMessenger( this ) );
		break;
	}
#endif
	default:
		BWindow::MessageReceived( pMessage );
	}
}
