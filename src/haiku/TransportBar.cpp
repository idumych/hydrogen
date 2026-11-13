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

#include "TransportBar.h"

#include <cmath>

#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <Shape.h>
#include <StringView.h>
#include <TextView.h>
#include <Window.h>

#if __has_include(<DecimalSpinner.h>)
#	include <DecimalSpinner.h>
#else
#	include <private/interface/DecimalSpinner.h>
#endif

#include <core/AudioEngine/AudioEngine.h>
#include <core/AudioEngine/Transport.h>
#include <core/Basics/Song.h>
#include <core/Globals.h>
#include <core/Hydrogen.h>

#include "Messages.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TransportBar"

using namespace H2Core;

GlyphButton::GlyphButton( const char* sName, Glyph glyph, BMessage* pMessage,
						  bool bToggle, float fSize )
	: BControl( sName, nullptr, pMessage, B_WILL_DRAW | B_NAVIGABLE )
	, m_glyph( glyph )
	, m_fSize( fSize )
	, m_bToggle( bToggle )
	, m_bTracking( false )
	, m_bPressed( false )
{
}

BSize GlyphButton::MinSize()
{
	return BSize( m_fSize, m_fSize );
}

BSize GlyphButton::MaxSize()
{
	return MinSize();
}

void GlyphButton::setGlyph( Glyph glyph )
{
	if ( m_glyph != glyph ) {
		m_glyph = glyph;
		Invalidate();
	}
}

void GlyphButton::Draw( BRect )
{
	BRect rect = Bounds();
	const rgb_color base = ui_color( B_CONTROL_BACKGROUND_COLOR );
	uint32 flags = be_control_look->Flags( this );
	if ( m_bPressed || ( m_bToggle && Value() == B_CONTROL_ON ) ) {
		flags |= BControlLook::B_ACTIVATED;
	}
	be_control_look->DrawButtonFrame( this, rect, rect, base, ViewColor(), flags );
	be_control_look->DrawButtonBackground( this, rect, rect, base, flags );

	rgb_color glyphColor = ui_color( B_CONTROL_TEXT_COLOR );
	if ( m_glyph == Glyph::Record && Value() == B_CONTROL_ON ) {
		glyphColor = { 220, 40, 40, 255 };
	}
	if ( ! IsEnabled() ) {
		glyphColor = tint_color( glyphColor, B_DISABLED_LABEL_TINT );
	}
	drawGlyph( rect, glyphColor );
}

void GlyphButton::drawGlyph( BRect rect, rgb_color color )
{
	SetHighColor( color );
	SetDrawingMode( B_OP_OVER );
	SetFlags( Flags() | B_SUBPIXEL_PRECISE );
	const BPoint c( ( rect.left + rect.right ) / 2, ( rect.top + rect.bottom ) / 2 );
	const float s = std::floor( rect.Height() * 0.22f );

	auto triangle = [&]( float x, float dir ) {
		BShape shape;
		shape.MoveTo( BPoint( x - dir * s * 0.5f, c.y - s ) );
		shape.LineTo( BPoint( x + dir * s * 0.8f, c.y ) );
		shape.LineTo( BPoint( x - dir * s * 0.5f, c.y + s ) );
		shape.Close();
		MovePenTo( B_ORIGIN );
		FillShape( &shape );
	};

	switch ( m_glyph ) {
	case Glyph::Play:
		triangle( c.x, 1 );
		break;
	case Glyph::Pause:
		FillRect( BRect( c.x - s * 0.8f, c.y - s, c.x - s * 0.25f, c.y + s ) );
		FillRect( BRect( c.x + s * 0.25f, c.y - s, c.x + s * 0.8f, c.y + s ) );
		break;
	case Glyph::Stop:
		FillRect( BRect( c.x - s * 0.8f, c.y - s * 0.8f, c.x + s * 0.8f, c.y + s * 0.8f ) );
		break;
	case Glyph::Record:
		FillEllipse( c, s * 0.75f, s * 0.75f );
		break;
	case Glyph::Rewind:
		triangle( c.x - s * 0.15f, -1 );
		triangle( c.x + s * 0.75f, -1 );
		break;
	case Glyph::Forward:
		triangle( c.x - s * 0.75f, 1 );
		triangle( c.x + s * 0.15f, 1 );
		break;
	case Glyph::Loop: {
		SetPenSize( 2 );
		StrokeArc( c, s, s * 0.8f, 30, 300 );
		BShape head;
		head.MoveTo( BPoint( c.x + s * 0.55f, c.y - s * 1.05f ) );
		head.LineTo( BPoint( c.x + s * 1.15f, c.y - s * 0.6f ) );
		head.LineTo( BPoint( c.x + s * 0.5f, c.y - s * 0.25f ) );
		head.Close();
		MovePenTo( B_ORIGIN );
		FillShape( &head );
		SetPenSize( 1 );
		break;
	}
	case Glyph::SongMode:
		// Three staggered bars: an arrangement of patterns.
		for ( int i = 0; i < 3; i++ ) {
			const float y = c.y - s + i * s * 0.75f;
			FillRect( BRect( c.x - s + i * s * 0.5f, y, c.x + i * s * 0.5f - s * 0.1f,
							 y + s * 0.45f ) );
		}
		break;
	case Glyph::PatternMode:
		// A 2×2 grid: one pattern.
		for ( int i = 0; i < 4; i++ ) {
			const float x = c.x - s + ( i % 2 ) * s * 1.05f;
			const float y = c.y - s + ( i / 2 ) * s * 1.05f;
			FillRect( BRect( x, y, x + s * 0.9f, y + s * 0.9f ) );
		}
		break;
	case Glyph::Add:
		FillRect( BRect( c.x - s, c.y - s * 0.2f, c.x + s, c.y + s * 0.2f ) );
		FillRect( BRect( c.x - s * 0.2f, c.y - s, c.x + s * 0.2f, c.y + s ) );
		break;
	case Glyph::Clear:
		// A bin: lid and body.
		FillRect( BRect( c.x - s, c.y - s, c.x + s, c.y - s * 0.7f ) );
		FillRect( BRect( c.x - s * 0.3f, c.y - s * 1.2f, c.x + s * 0.3f, c.y - s ) );
		StrokeRect( BRect( c.x - s * 0.75f, c.y - s * 0.55f, c.x + s * 0.75f, c.y + s ) );
		StrokeLine( BPoint( c.x, c.y - s * 0.3f ), BPoint( c.x, c.y + s * 0.75f ) );
		break;
	case Glyph::SinglePattern:
		StrokeRect( BRect( c.x - s, c.y - s * 0.4f, c.x + s, c.y + s * 0.4f ) );
		break;
	case Glyph::StackedPatterns:
		for ( int i = -1; i <= 1; i++ ) {
			StrokeRect( BRect( c.x - s, c.y + i * s * 0.65f - s * 0.25f, c.x + s,
							   c.y + i * s * 0.65f + s * 0.25f ) );
		}
		break;
	case Glyph::Lock:
		FillRect( BRect( c.x - s * 0.8f, c.y - s * 0.1f, c.x + s * 0.8f, c.y + s ) );
		SetPenSize( 2 );
		StrokeArc( BPoint( c.x, c.y - s * 0.3f ), s * 0.5f, s * 0.6f, 0, 180 );
		if ( Value() != B_CONTROL_ON ) {
			// Open shackle.
			StrokeLine( BPoint( c.x + s * 0.5f, c.y - s * 0.3f ), BPoint( c.x + s * 0.5f, c.y - s * 0.1f ) );
		}
		SetPenSize( 1 );
		break;
	}
}

void GlyphButton::MouseDown( BPoint )
{
	if ( ! IsEnabled() ) {
		return;
	}
	m_bTracking = true;
	m_bPressed = true;
	SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS );
	Invalidate();
}

void GlyphButton::MouseMoved( BPoint where, uint32, const BMessage* )
{
	if ( m_bTracking && m_bPressed != Bounds().Contains( where ) ) {
		m_bPressed = Bounds().Contains( where );
		Invalidate();
	}
}

void GlyphButton::MouseUp( BPoint where )
{
	if ( ! m_bTracking ) {
		return;
	}
	m_bTracking = false;
	m_bPressed = false;
	if ( Bounds().Contains( where ) ) {
		if ( m_bToggle ) {
			SetValue( Value() == B_CONTROL_ON ? B_CONTROL_OFF : B_CONTROL_ON );
		}
		Invoke();
	}
	Invalidate();
}

void GlyphButton::KeyDown( const char* pBytes, int32 nBytes )
{
	if ( pBytes[ 0 ] == B_SPACE || pBytes[ 0 ] == B_ENTER ) {
		if ( m_bToggle ) {
			SetValue( Value() == B_CONTROL_ON ? B_CONTROL_OFF : B_CONTROL_ON );
		}
		Invoke();
		return;
	}
	BControl::KeyDown( pBytes, nBytes );
}

TransportBar::TransportBar()
	: BView( "transport bar", B_WILL_DRAW )
	, m_bPlaying( false )
{
	SetViewUIColor( B_PANEL_BACKGROUND_COLOR );

	m_pSongModeButton = new GlyphButton( "song mode", GlyphButton::Glyph::SongMode,
										 new BMessage( kMsgSongMode ), true );
	m_pSongModeButton->SetToolTip( B_TRANSLATE( "Song mode" ) );
	m_pPatternModeButton = new GlyphButton( "pattern mode", GlyphButton::Glyph::PatternMode,
											new BMessage( kMsgPatternMode ), true );
	m_pPatternModeButton->SetToolTip( B_TRANSLATE( "Pattern mode" ) );

	m_pTimeDisplay = new BStringView( "time", "00:00:00.000" );
	BFont font( be_fixed_font );
	font.SetSize( std::round( be_plain_font->Size() * 1.6f ) );
	m_pTimeDisplay->SetFont( &font );
	m_pTimeDisplay->SetAlignment( B_ALIGN_CENTER );
	m_pTimeDisplay->SetExplicitMinSize(
		BSize( font.StringWidth( "00:00:00.000" ) + 20, B_SIZE_UNSET ) );
	m_pTimeDisplay->SetToolTip( B_TRANSLATE( "Elapsed time" ) );

	m_pRewindButton = new GlyphButton( "rewind", GlyphButton::Glyph::Rewind,
									   new BMessage( kMsgRewind ) );
	m_pRewindButton->SetToolTip( B_TRANSLATE( "Previous bar" ) );
	m_pRecordButton = new GlyphButton( "record", GlyphButton::Glyph::Record,
									   new BMessage( kMsgRecord ), true );
	m_pRecordButton->SetToolTip( B_TRANSLATE( "Record MIDI notes" ) );
	m_pPlayButton = new GlyphButton( "play", GlyphButton::Glyph::Play,
									 new BMessage( kMsgPlay ) );
	m_pPlayButton->SetToolTip( B_TRANSLATE( "Play/pause" ) );
	m_pStopButton = new GlyphButton( "stop", GlyphButton::Glyph::Stop,
									 new BMessage( kMsgStop ) );
	m_pStopButton->SetToolTip( B_TRANSLATE( "Stop" ) );
	m_pForwardButton = new GlyphButton( "forward", GlyphButton::Glyph::Forward,
										new BMessage( kMsgForward ) );
	m_pForwardButton->SetToolTip( B_TRANSLATE( "Next bar" ) );
	m_pLoopButton = new GlyphButton( "loop", GlyphButton::Glyph::Loop,
									 new BMessage( kMsgLoop ), true );
	m_pLoopButton->SetToolTip( B_TRANSLATE( "Loop song" ) );

	m_pBpmSpinner = new BDecimalSpinner( "bpm", B_TRANSLATE( "BPM:" ),
										 new BMessage( kMsgBpmChanged ) );
	m_pBpmSpinner->SetMinValue( MIN_BPM );
	m_pBpmSpinner->SetMaxValue( MAX_BPM );
	m_pBpmSpinner->SetPrecision( 2 );
	m_pBpmSpinner->SetStep( 1.0 );
	m_pBpmSpinner->SetValue( 120.0 );
	m_pBpmSpinner->SetToolTip( B_TRANSLATE( "Tempo in beats per minute" ) );
	// Upstream's tempo field is ~95 px; don't let it take the whole bar.
	m_pBpmSpinner->SetExplicitMaxSize( BSize(
		m_pBpmSpinner->StringWidth( B_TRANSLATE( "BPM:" ) ) +
		m_pBpmSpinner->StringWidth( "000.00" ) + 70, B_SIZE_UNSET ) );

	BLayoutBuilder::Group<>( this, B_HORIZONTAL, B_USE_HALF_ITEM_SPACING )
		.SetInsets( B_USE_HALF_ITEM_INSETS, 4, B_USE_HALF_ITEM_INSETS, 4 )
		.Add( m_pSongModeButton )
		.Add( m_pPatternModeButton )
		.AddStrut( B_USE_ITEM_SPACING )
		.Add( m_pTimeDisplay )
		.AddStrut( B_USE_ITEM_SPACING )
		.Add( m_pRewindButton )
		.Add( m_pRecordButton )
		.Add( m_pPlayButton )
		.Add( m_pStopButton )
		.Add( m_pForwardButton )
		.Add( m_pLoopButton )
		.AddStrut( B_USE_ITEM_SPACING )
		.Add( m_pBpmSpinner )
		.AddGlue();

	SetExplicitMinSize( BSize( B_SIZE_UNSET, 40 ) );
	SetExplicitMaxSize( BSize( B_SIZE_UNLIMITED, 40 ) );
}

void TransportBar::AttachedToWindow()
{
	BView::AttachedToWindow();
	for ( BControl* pControl : { (BControl*) m_pSongModeButton, (BControl*) m_pPatternModeButton,
			 (BControl*) m_pRewindButton, (BControl*) m_pRecordButton, (BControl*) m_pPlayButton,
			 (BControl*) m_pStopButton, (BControl*) m_pForwardButton, (BControl*) m_pLoopButton,
			 (BControl*) m_pBpmSpinner } ) {
		pControl->SetTarget( Window() );
	}
	update();
}

void TransportBar::Draw( BRect updateRect )
{
	// A separator line along the bottom, like the menu bar's.
	BRect bounds = Bounds();
	SetHighColor( tint_color( ViewColor(), B_DARKEN_2_TINT ) );
	StrokeLine( bounds.LeftBottom(), bounds.RightBottom() );
}

void TransportBar::update()
{
	auto pHydrogen = Hydrogen::get_instance();
	if ( pHydrogen == nullptr ) {
		return;
	}
	auto pAudioEngine = pHydrogen->getAudioEngine();

	const float fSeconds = pAudioEngine->getElapsedTime();
	const int nMSec = (int) ( ( fSeconds - (int) fSeconds ) * 1000.0 );
	BString sTime;
	sTime.SetToFormat( "%02d:%02d:%02d.%03d", (int) ( fSeconds / 3600.0 ),
					   (int) ( fSeconds / 60.0 ) % 60, ( (int) fSeconds ) % 60, nMSec );
	if ( sTime != m_pTimeDisplay->Text() ) {
		m_pTimeDisplay->SetText( sTime );
	}

	const bool bPlaying = pAudioEngine->getState() == AudioEngine::State::Playing;
	if ( bPlaying != m_bPlaying ) {
		m_bPlaying = bPlaying;
		m_pPlayButton->setGlyph( bPlaying ? GlyphButton::Glyph::Pause
										  : GlyphButton::Glyph::Play );
	}

	const bool bSongMode = pHydrogen->getMode() == Song::Mode::Song;
	m_pSongModeButton->SetValue( bSongMode ? B_CONTROL_ON : B_CONTROL_OFF );
	m_pPatternModeButton->SetValue( bSongMode ? B_CONTROL_OFF : B_CONTROL_ON );
	m_pRewindButton->SetEnabled( bSongMode );
	m_pForwardButton->SetEnabled( bSongMode );
	m_pLoopButton->SetEnabled( bSongMode );

	auto pSong = pHydrogen->getSong();
	if ( pSong != nullptr ) {
		m_pLoopButton->SetValue( pSong->getLoopMode() == Song::LoopMode::Enabled
									 ? B_CONTROL_ON : B_CONTROL_OFF );
	}
	m_pRecordButton->SetValue( pHydrogen->getRecordEnabled() ? B_CONTROL_ON
															 : B_CONTROL_OFF );

	// Don't overwrite the field while the user is typing in it.
	const float fBpm = pAudioEngine->getPlayhead()->getBpm();
	if ( ! m_pBpmSpinner->TextView()->IsFocus() &&
		 std::abs( m_pBpmSpinner->Value() - fBpm ) > 0.005 ) {
		m_pBpmSpinner->SetValue( fBpm );
	}
	m_pBpmSpinner->SetEnabled( pHydrogen->getTempoSource() == Hydrogen::Tempo::Song );
}
