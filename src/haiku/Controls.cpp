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

#include "Controls.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <Catalog.h>
#include <ScrollBar.h>
#include <TextView.h>
#include <Window.h>

#include "Colors.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Controls"

// ---------------------------------------------------------------------------
// Knob

static const float kKnobWidth = 44;
static const float kKnobHeight = 26;

Knob::Knob( const char* sName, BMessage* pMessage, float fMin, float fMax, float fDefault,
			Style style, bool bIntegerSteps )
	: BControl( sName, nullptr, pMessage, B_WILL_DRAW | B_NAVIGABLE )
	, m_fMin( fMin ), m_fMax( fMax ), m_fDefault( fDefault ), m_fValue( fDefault )
	, m_style( style ), m_bIntegerSteps( bIntegerSteps )
{
	SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
	SetToolTip( valueText() );
}

BSize Knob::MinSize()
{
	if ( m_style == Style::Small ) {
		return BSize( 17, 17 );
	}
	return BSize( kKnobWidth - 1, kKnobHeight - 1 );
}

BSize Knob::MaxSize()
{
	return MinSize();
}

BString Knob::valueText() const
{
	BString sText;
	sText.SetToFormat( m_bIntegerSteps ? "%.0f" : "%.2f", m_fValue );
	return sText;
}

float Knob::step() const
{
	return m_bIntegerSteps ? 1.0f : ( m_fMax - m_fMin ) / 100.0f;
}

void Knob::setValue( float fValue )
{
	fValue = std::clamp( fValue, m_fMin, m_fMax );
	if ( m_bIntegerSteps ) {
		fValue = std::round( fValue );
	}
	if ( fValue != m_fValue ) {
		m_fValue = fValue;
		SetToolTip( valueText() );
		Invalidate();
	}
}

void Knob::change( float fValue )
{
	fValue = std::clamp( fValue, m_fMin, m_fMax );
	if ( m_bIntegerSteps ) {
		fValue = std::round( fValue );
	}
	if ( std::abs( fValue ) < 1e-6f ) {
		fValue = 0;
	}
	if ( fValue == m_fValue ) {
		return;
	}
	setValue( fValue );
	if ( Message() != nullptr ) {
		BMessage message( *Message() );
		message.AddFloat( "value", m_fValue );
		Invoke( &message );
	}
}

void Knob::Draw( BRect )
{
	const BRect bounds = Bounds();
	SetHighColor( ViewColor() );
	FillRect( bounds );
	SetFlags( Flags() | B_SUBPIXEL_PRECISE );

	const bool bSmall = m_style == Style::Small;
	const BPoint c = bSmall ? BPoint( 9, 9 ) : BPoint( kKnobWidth / 2, 13 );
	const float fArc = bSmall ? 7.5f : 11;
	const float fFace = bSmall ? 5 : 8;
	const float fRange = std::max( 1e-6f, m_fMax - m_fMin );
	const float fFrac = ( m_fValue - m_fMin ) / fRange;
	const float fMid = ( m_fMin + m_fMax ) / 2;
	const rgb_color panel = ui_color( B_PANEL_BACKGROUND_COLOR );
	const rgb_color text = ui_color( B_PANEL_TEXT_COLOR );
	const bool bEnabled = IsEnabled();
	const rgb_color arc = bEnabled ? make_color( 220, 45, 45, 255 ) : tint_color( panel, B_DARKEN_3_TINT );

	// The track the value arc runs along, 210° clockwise to −29°.
	SetPenSize( 2 );
	SetHighColor( tint_color( panel, B_DARKEN_2_TINT ) );
	StrokeArc( c, fArc, fArc, -29, 239 );
	SetHighColor( arc );
	if ( m_style != Style::Center ) {
		if ( fFrac > 0 ) {
			StrokeArc( c, fArc, fArc, 210 - 239 * fFrac, 239 * fFrac );
		}
	}
	else {
		const float fAngle = 200 * ( m_fValue - fMid ) / fRange;
		if ( std::abs( fAngle ) > 0.01f ) {
			StrokeArc( c, fArc, fArc, fAngle > 0 ? 90 - fAngle : 90, std::abs( fAngle ) );
		}
		// Middle mark: green when exactly centred, like upstream.
		SetHighColor( m_fValue == fMid && bEnabled ? make_color( 60, 180, 60, 255 )
												   : tint_color( panel, B_DARKEN_4_TINT ) );
		StrokeArc( c, fArc, fArc, 88, 4 );
	}
	SetPenSize( 1 );

	// The knob and its pointer dot.
	const rgb_color face = ui_color( B_CONTROL_BACKGROUND_COLOR );
	SetHighColor( bEnabled ? face : tint_color( face, B_DARKEN_1_TINT ) );
	FillEllipse( c, fFace, fFace );
	SetHighColor( tint_color( panel, B_DARKEN_3_TINT ) );
	StrokeEllipse( c, fFace, fFace );
	if ( IsFocus() ) {
		SetHighColor( ui_color( B_KEYBOARD_NAVIGATION_COLOR ) );
		StrokeEllipse( c, fFace + 1, fFace + 1 );
	}
	const float fTheta = ( 255 * ( m_fValue - fMid ) / fRange ) * M_PI / 180;
	SetHighColor( bEnabled ? ui_color( B_CONTROL_TEXT_COLOR ) : tint_color( text, B_LIGHTEN_1_TINT ) );
	const float fDot = bSmall ? 3 : 5;
	FillEllipse( BPoint( c.x + fDot * std::sin( fTheta ), c.y - fDot * std::cos( fTheta ) ), bSmall ? 1.2f : 1.6f, bSmall ? 1.2f : 1.6f );

	if ( bSmall ) {
		return;
	}
	// End labels: small numbers for 0/1-style ranges, − and + otherwise.
	BFont small( be_plain_font );
	small.SetSize( 8 );
	SetFont( &small );
	SetHighColor( text );
	SetDrawingMode( B_OP_OVER );
	auto label = []( float f, const char* sSign ) {
		BString s;
		if ( f == std::floor( f ) && std::abs( f ) < 10 ) {
			s << (int) f;
		}
		else {
			s = sSign;
		}
		return s;
	};
	DrawString( label( m_fMin, "−" ), BPoint( 2, 24 ) );
	const BString sMax = label( m_fMax, "+" );
	DrawString( sMax, BPoint( kKnobWidth - 3 - StringWidth( sMax ), 24 ) );
	SetDrawingMode( B_OP_COPY );
}

bool Knob::GetToolTipAt( BPoint where, BToolTip** pTip )
{
	updateMidiToolTip( this );
	return BControl::GetToolTipAt( where, pTip );
}

void Knob::MouseDown( BPoint )
{
	if ( ! IsEnabled() || learnClick( this ) ) {
		return;
	}
	MakeFocus( true );
	int32 nClicks = 1;
	if ( Window()->CurrentMessage() != nullptr ) {
		Window()->CurrentMessage()->FindInt32( "clicks", &nClicks );
	}
	if ( nClicks == 2 ) {
		change( m_fDefault );
		return;
	}
	BPoint screen;
	uint32 nButtons;
	GetMouse( &screen, &nButtons, false );
	ConvertToScreen( &screen );
	m_bDragging = true;
	m_fDragStartY = screen.y;
	m_fDragStartValue = m_fValue;
	SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY );
}

void Knob::MouseMoved( BPoint where, uint32, const BMessage* )
{
	if ( ! m_bDragging ) {
		return;
	}
	BPoint screen = ConvertToScreen( where );
	// 100 px is the whole range; Shift for fine adjustment.
	const float fScale = ( modifiers() & B_SHIFT_KEY ) ? 0.2f : 1.0f;
	change( m_fDragStartValue -
			fScale * ( screen.y - m_fDragStartY ) / 100.0f * ( m_fMax - m_fMin ) );
}

void Knob::MouseUp( BPoint )
{
	m_bDragging = false;
}

void Knob::KeyDown( const char* pBytes, int32 nBytes )
{
	switch ( pBytes[ 0 ] ) {
	case B_UP_ARROW:
	case B_RIGHT_ARROW:
		change( m_fValue + step() );
		break;
	case B_DOWN_ARROW:
	case B_LEFT_ARROW:
		change( m_fValue - step() );
		break;
	case B_PAGE_UP:
		change( m_fValue + 5 * step() );
		break;
	case B_PAGE_DOWN:
		change( m_fValue - 5 * step() );
		break;
	case B_HOME:
		change( m_fMax );
		break;
	case B_END:
		change( m_fMin );
		break;
	default:
		BControl::KeyDown( pBytes, nBytes );
	}
}

void Knob::MessageReceived( BMessage* pMessage )
{
	if ( pMessage->what == B_MOUSE_WHEEL_CHANGED && IsEnabled() ) {
		float fDelta = 0;
		pMessage->FindFloat( "be:wheel_delta_y", &fDelta );
		change( m_fValue - fDelta * step() );
		return;
	}
	BControl::MessageReceived( pMessage );
}

// ---------------------------------------------------------------------------
// ScrollPane

ScrollPane::ScrollPane( BView* pContent, orientation direction )
	: BView( pContent->Name(), B_FRAME_EVENTS )
	, m_pContent( pContent )
	, m_direction( direction )
{
	SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
	AddChild( m_pContent );
	if ( direction == B_VERTICAL ) {
		SetExplicitMinSize( BSize( pContent->MinSize().width, 50 ) );
		SetExplicitPreferredSize( BSize( pContent->PreferredSize().width, 200 ) );
	}
	else {
		SetExplicitMinSize( BSize( 50, pContent->MinSize().height ) );
		SetExplicitPreferredSize( BSize( 200, pContent->PreferredSize().height ) );
	}
}

void ScrollPane::AllAttached()
{
	BView::AllAttached();
	contentChanged();
}

void ScrollPane::FrameResized( float, float )
{
	contentChanged();
}

void ScrollPane::contentChanged()
{
	const BRect bounds = Bounds();
	m_pContent->InvalidateLayout();
	m_pContent->MoveTo( 0, 0 );
	if ( m_direction == B_VERTICAL ) {
		const float fHeight = std::max( bounds.Height(), m_pContent->PreferredSize().height );
		m_pContent->ResizeTo( bounds.Width(), fHeight );
		if ( BScrollBar* pBar = ScrollBar( B_VERTICAL ) ) {
			pBar->SetRange( 0, std::max( 0.0f, fHeight - bounds.Height() ) );
			pBar->SetProportion( fHeight > 0 ? bounds.Height() / fHeight : 1 );
			pBar->SetSteps( 20, std::max( 20.0f, bounds.Height() - 20 ) );
		}
	}
	else {
		const float fWidth = std::max( bounds.Width(), m_pContent->PreferredSize().width );
		m_pContent->ResizeTo( fWidth, bounds.Height() );
		if ( BScrollBar* pBar = ScrollBar( B_HORIZONTAL ) ) {
			pBar->SetRange( 0, std::max( 0.0f, fWidth - bounds.Width() ) );
			pBar->SetProportion( fWidth > 0 ? bounds.Width() / fWidth : 1 );
			pBar->SetSteps( 56, std::max( 56.0f, bounds.Width() - 56 ) );
		}
	}
}

// ---------------------------------------------------------------------------
// FormattedSpinner

FormattedSpinner::FormattedSpinner( const char* sName, const char* sLabel, BMessage* pMessage,
									Formatter formatter )
	: BSpinner( sName, sLabel, pMessage )
	, m_formatter( std::move( formatter ) )
{
}

void FormattedSpinner::SetValue( int32 nValue )
{
	BSpinner::SetValue( nValue );
	if ( m_formatter ) {
		TextView()->SetText( m_formatter( Value() ) );
	}
}

void FormattedSpinner::SetValueFromText()
{
	const char* sText = TextView()->Text();
	int32 nValue = Value();
	if ( m_formatter && m_formatter( MinValue() ) == sText ) {
		nValue = MinValue();
	}
	else if ( const char* pParen = strchr( sText, '(' ) ) {
		nValue = atoi( pParen + 1 );
	}
	else {
		nValue = atoi( sText );
	}
	SetValue( nValue );
}

FormattedSpinner::Formatter FormattedSpinner::offAt( int32 nOffValue )
{
	return [nOffValue]( int32 nValue ) {
		BString s;
		if ( nValue == nOffValue ) {
			s = B_TRANSLATE( "Off" );
		}
		else {
			s << nValue;
		}
		return s;
	};
}

FormattedSpinner::Formatter FormattedSpinner::midiNote()
{
	return []( int32 nValue ) {
		static const char* kNames[] = { "C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A",
										"A♯", "B" };
		const int nOctave = nValue >= 0 ? nValue / 12 : ( nValue - 11 ) / 12;
		const int nKey = nValue - 12 * nOctave;
		BString s;
		s << kNames[ std::clamp( nKey, 0, 11 ) ] << nOctave << " (" << nValue << ")";
		return s;
	};
}
