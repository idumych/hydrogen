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

#include "SampleEditor.h"

#include <algorithm>
#include <cmath>

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <GradientLinear.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <StringView.h>

#if __has_include(<DecimalSpinner.h>)
#	include <DecimalSpinner.h>
#	include <Spinner.h>
#else
#	include <private/interface/DecimalSpinner.h>
#	include <private/interface/Spinner.h>
#endif

#include <core/AudioEngine/AudioEngine.h>
#include <core/AudioEngine/Transport.h>
#include <core/Basics/Drumkit.h>
#include <core/Basics/InstrumentComponent.h>
#include <core/Basics/InstrumentLayer.h>
#include <core/Basics/InstrumentList.h>
#include <core/Basics/Note.h>
#include <core/Basics/Song.h>
#include <core/Helpers/Filesystem.h>
#include <core/Hydrogen.h>
#include <core/IO/AudioDriver.h>
#include <core/Preferences/Preferences.h>
#include <core/Sampler/Sampler.h>

#include "Colors.h"
#include "Settings.h"
#include "SongActions.h"
#include "UndoStack.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SampleEditor"

using namespace H2Core;
using Marker = SampleEditorWindow::Marker;

enum : uint32 {
	kMsgMarkerSpinner = 'SEms',
	kMsgMode = 'SEmo',
	kMsgCount = 'SEco',
	kMsgLength = 'SEle',
	kMsgPitch = 'SEpi',
	kMsgCrispness = 'SEcr',
	kMsgEnvelope = 'SEen',
	kMsgZoom = 'SEzo',
	kMsgPlay = 'SEpl',
	kMsgPlayOriginal = 'SEpo',
	kMsgRevert = 'SErv',
	kMsgApply = 'SEap',
	kMsgUpdate = 'SEup',
	kMsgTick = 'SEtk',
};

// The envelope editor's coordinate system: Sample::applyVelocity() and
// applyPan() read points as pixels of upstream's 841×91 target view, and
// songs store them so. The view scales to it.
static const int kTargetWidth = 841;
static const int kTargetHeight = 91;
static const int kPanCenter = 45;

static const rgb_color kStartColor = { 0, 219, 0, 255 };
static const rgb_color kLoopColor = { 219, 219, 0, 255 };
static const rgb_color kEndColor = { 217, 68, 0, 255 };
static const rgb_color kVelocityColor = { 219, 110, 0, 255 };
static const rgb_color kPanColor = { 234, 130, 255, 255 };

static const float kHandleWidth = 15;
static const float kHandleHeight = 20;
static const float kHandleMargin = 5;
static const float kGrabDistance = 10;
static const int kDetailWidth = 180;
static const char* kFrameName = "sample editor";

// Upstream's length menu: Off, 1/64 … 1/2, then 1 … 32 beats.
static const float kFractions[] = { 1.0f / 64, 1.0f / 32, 1.0f / 16, 1.0f / 8, 1.0f / 4, 1.0f / 2 };
static const int kLengthItems = 39;

static float lengthForIndex( int nIndex )
{
	if ( nIndex >= 1 && nIndex <= 6 ) {
		return kFractions[ nIndex - 1 ];
	}
	return nIndex == 7 ? 1.0f : static_cast<float>( nIndex - 6 );
}

static int indexForLength( const Sample::Rubberband& rubberband )
{
	if ( ! rubberband.bUse ) {
		return 0;
	}
	for ( int i = 0; i < 6; i++ ) {
		if ( std::fabs( rubberband.fLengthInBeats - kFractions[ i ] ) < 1e-6 ) {
			return i + 1;
		}
	}
	if ( rubberband.fLengthInBeats >= 1 ) {
		return std::clamp( static_cast<int>( rubberband.fLengthInBeats + 6 ), 7, kLengthItems - 1 );
	}
	return 0;
}

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

static rgb_color markerColor( Marker marker )
{
	switch ( marker ) {
	case Marker::Start:
		return kStartColor;
	case Marker::Loop:
		return kLoopColor;
	default:
		return kEndColor;
	}
}

static long long markerFrame( const Sample::Loops& loops, Marker marker )
{
	switch ( marker ) {
	case Marker::Start:
		return loops.nStartFrame;
	case Marker::Loop:
		return loops.nLoopFrame;
	case Marker::End:
		return loops.nEndFrame;
	default:
		return 0;
	}
}

static void fillGradient( BView* pView, BRect rect, rgb_color color )
{
	BGradientLinear gradient( rect.LeftTop(), rect.LeftBottom() );
	gradient.AddColor( Colors::darker( color, 130 ), 0 );
	gradient.AddColor( Colors::lighter( color, 130 ), 127 );
	gradient.AddColor( Colors::darker( color, 130 ), 255 );
	pView->FillRect( rect, gradient );
}

/** Upstream's WaveDisplay: gradient, border, dotted centre line and the
 * peaks of @a pData (max/min per column) in @a rect. */
static void drawWave( BView* pView, BRect rect, const float* pData, long long nFrames,
					  float fGain, rgb_color background )
{
	const EditorColors& c = Colors::get();
	fillGradient( pView, rect, background );
	pView->SetHighColor( 0, 0, 0 );
	pView->StrokeRect( rect );
	const float fCenter = std::floor( rect.top + rect.Height() / 2 );
	const pattern dotted = { { 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 } };
	pView->SetDrawingMode( B_OP_ALPHA );
	pView->SetHighColor( 0, 0, 0, 80 );
	pView->StrokeLine( BPoint( rect.left + 1, fCenter ), BPoint( rect.right - 1, fCenter ), dotted );
	pView->SetDrawingMode( B_OP_COPY );
	if ( pData == nullptr || nFrames <= 0 ) {
		return;
	}
	const int nWidth = static_cast<int>( rect.Width() ) + 1;
	const float fScale = ( rect.Height() / 2 ) * fGain;
	pView->SetHighColor( c.waveForm );
	for ( int x = 0; x < nWidth; x++ ) {
		const long long nFrom = x * nFrames / nWidth;
		const long long nTo = std::max( nFrom + 1, ( x + 1 ) * nFrames / nWidth );
		float fMax = 0, fMin = 0;
		for ( long long i = nFrom; i < std::min( nTo, nFrames ); i++ ) {
			fMax = std::max( fMax, pData[ i ] );
			fMin = std::min( fMin, pData[ i ] );
		}
		const float fTop = std::max( rect.top + 1, fCenter - fMax * fScale );
		const float fBottom = std::min( rect.bottom - 1, fCenter - fMin * fScale );
		pView->StrokeLine( BPoint( rect.left + x, fTop ), BPoint( rect.left + x, fBottom ) );
	}
}

static rgb_color layerBackground()
{
	return Colors::get().accent;
}

// ---------------------------------------------------------------------------
// OriginalView: both channels of the file with the start, loop and end
// markers (upstream's SampleWaveDisplay, left above right).

class OriginalView : public BView
{
public:
	OriginalView( SampleEditorWindow* pEditor )
		: BView( "original", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE ), m_pEditor( pEditor )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitMinSize( BSize( 400, 2 * 100 + 4 ) );
	}

	void Draw( BRect ) override;
	void MouseDown( BPoint where ) override;
	void MouseMoved( BPoint where, uint32 transit, const BMessage* ) override;
	void MouseUp( BPoint ) override { m_bDragging = false; }

private:
	BRect channelRect( int nChannel ) const
	{
		const BRect b = Bounds();
		const float fHeight = std::floor( ( b.Height() - 4 ) / 2 );
		return nChannel == 0 ? BRect( 0, 0, b.right, fHeight )
							 : BRect( 0, b.bottom - fHeight, b.right, b.bottom );
	}
	long long frames() const
	{
		return m_pEditor->m_pOriginal != nullptr ? m_pEditor->m_pOriginal->getFrames() : 0;
	}
	float frameToX( double fFrame ) const
	{
		return frames() > 0 ? std::floor( fFrame * ( Bounds().Width() + 1 ) / frames() ) : 0;
	}
	long long xToFrame( float x ) const
	{
		return static_cast<long long>( x * frames() / ( Bounds().Width() + 1 ) );
	}
	/** The handle of @a marker: S and L on the left channel, E on the right. */
	BRect handleRect( Marker marker ) const;
	Marker hit( BPoint where ) const;

	SampleEditorWindow* m_pEditor;
	Marker m_hovered = Marker::None;
	bool m_bDragging = false;
};

BRect OriginalView::handleRect( Marker marker ) const
{
	const float x = frameToX( markerFrame( m_pEditor->m_edit.loops, marker ) );
	switch ( marker ) {
	case Marker::Start: {
		const BRect r = channelRect( 0 );
		return BRect( x, r.top + kHandleMargin, x + kHandleWidth, r.top + kHandleMargin + kHandleHeight );
	}
	case Marker::Loop: {
		const BRect r = channelRect( 0 );
		return BRect( x, r.bottom - kHandleMargin - kHandleHeight, x + kHandleWidth, r.bottom - kHandleMargin );
	}
	case Marker::End: {
		const BRect r = channelRect( 1 );
		return BRect( x - kHandleWidth, r.bottom - kHandleMargin - kHandleHeight, x, r.bottom - kHandleMargin );
	}
	default:
		return BRect();
	}
}

Marker OriginalView::hit( BPoint where ) const
{
	for ( Marker marker : { Marker::Start, Marker::Loop, Marker::End } ) {
		BRect r = handleRect( marker );
		r.InsetBy( -4, -4 );
		if ( r.Contains( where ) ) {
			return marker;
		}
	}
	// Else the nearest line, if it's close and the only one that close.
	Marker best = Marker::None;
	float fBest = kGrabDistance;
	bool bTie = false;
	for ( Marker marker : { Marker::Start, Marker::Loop, Marker::End } ) {
		const float fDistance = std::fabs( frameToX( markerFrame( m_pEditor->m_edit.loops, marker ) ) - where.x );
		if ( fDistance < fBest ) {
			best = marker;
			fBest = fDistance;
			bTie = false;
		}
		else if ( fDistance == fBest && best != Marker::None ) {
			bTie = true;
		}
	}
	return bTie ? Marker::None : best;
}

void OriginalView::Draw( BRect )
{
	const auto& pSample = m_pEditor->m_pOriginal;
	for ( int nChannel = 0; nChannel < 2; nChannel++ ) {
		drawWave( this, channelRect( nChannel ),
				  pSample == nullptr ? nullptr : ( nChannel == 0 ? pSample->getData_L() : pSample->getData_R() ),
				  frames(), m_pEditor->m_fGain, layerBackground() );
	}
	SetHighUIColor( B_PANEL_BACKGROUND_COLOR );
	FillRect( BRect( 0, channelRect( 0 ).bottom + 1, Bounds().right, channelRect( 1 ).top - 1 ) );

	SetDrawingMode( B_OP_ALPHA );
	SetBlendingMode( B_PIXEL_ALPHA, B_ALPHA_OVERLAY );
	for ( Marker marker : { Marker::Start, Marker::Loop, Marker::End } ) {
		rgb_color color = markerColor( marker );
		if ( m_pEditor->m_bPlaying ) {
			color = tint_color( color, B_LIGHTEN_1_TINT );
		}
		const float x = frameToX( markerFrame( m_pEditor->m_edit.loops, marker ) );
		if ( marker == m_hovered && ! m_pEditor->m_bPlaying ) {
			SetHighColor( color.red, color.green, color.blue, 110 );
			FillRect( BRect( x - 3, 0, x + 3, Bounds().bottom ) );
		}
		SetHighColor( color.red, color.green, color.blue, 200 );
		StrokeLine( BPoint( x, 0 ), BPoint( x, Bounds().bottom ) );

		// A slanted tab with the marker's letter.
		const BRect r = handleRect( marker );
		const bool bLeft = marker == Marker::End;
		BPoint points[ 4 ];
		if ( bLeft ) {
			points[ 0 ] = BPoint( r.right, r.top );
			points[ 1 ] = BPoint( r.left, r.top + 3 );
			points[ 2 ] = BPoint( r.left, r.bottom - 3 );
			points[ 3 ] = BPoint( r.right, r.bottom );
		}
		else {
			points[ 0 ] = BPoint( r.left, r.top );
			points[ 1 ] = BPoint( r.right, r.top + 3 );
			points[ 2 ] = BPoint( r.right, r.bottom - 3 );
			points[ 3 ] = BPoint( r.left, r.bottom );
		}
		FillPolygon( points, 4 );
		SetHighColor( 0, 0, 0, 255 );
		BFont font( be_bold_font );
		font.SetSize( 11 );
		SetFont( &font );
		const char* sLetter = marker == Marker::Start ? "S" : marker == Marker::Loop ? "L" : "E";
		font_height height;
		font.GetHeight( &height );
		DrawString( sLetter, BPoint( r.left + ( r.Width() - font.StringWidth( sLetter ) ) / 2,
									 r.top + ( r.Height() + height.ascent - height.descent ) / 2 ) );
	}
	SetDrawingMode( B_OP_COPY );

	if ( m_pEditor->m_fPlayheadOriginal >= 0 ) {
		SetHighColor( Colors::get().playhead );
		const float x = frameToX( m_pEditor->m_fPlayheadOriginal );
		StrokeLine( BPoint( x, 0 ), BPoint( x, Bounds().bottom ) );
	}
}

void OriginalView::MouseDown( BPoint where )
{
	if ( m_pEditor->m_bPlaying || frames() == 0 ) {
		return;
	}
	const Marker marker = hit( where );
	if ( marker == Marker::None ) {
		return;
	}
	m_bDragging = true;
	SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS );
	m_pEditor->m_selected = marker;
	m_pEditor->setMarker( marker, markerFrame( m_pEditor->m_edit.loops, marker ) );
}

void OriginalView::MouseMoved( BPoint where, uint32 transit, const BMessage* )
{
	if ( m_bDragging ) {
		const float x = std::clamp( where.x, 0.0f, Bounds().right );
		m_pEditor->setMarker( m_pEditor->m_selected, xToFrame( x ) );
		return;
	}
	const Marker hovered = transit == B_EXITED_VIEW || m_pEditor->m_bPlaying ? Marker::None : hit( where );
	if ( hovered != m_hovered ) {
		m_hovered = hovered;
		Invalidate();
	}
}

// ---------------------------------------------------------------------------
// DetailView: one frame per pixel around the selected marker or the
// playhead (upstream's DetailWaveDisplay), zoomed vertically.

class DetailView : public BView
{
public:
	DetailView( SampleEditorWindow* pEditor )
		: BView( "detail", B_WILL_DRAW ), m_pEditor( pEditor )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitMinSize( BSize( kDetailWidth - 1, 2 * 100 + 4 ) );
		SetExplicitMaxSize( BSize( kDetailWidth - 1, B_SIZE_UNLIMITED ) );
	}

	void Draw( BRect ) override;

	float m_fZoom = 1;

private:
	SampleEditorWindow* m_pEditor;
};

void DetailView::Draw( BRect )
{
	const BRect b = Bounds();
	const float fHeight = std::floor( ( b.Height() - 4 ) / 2 );
	const BRect rects[ 2 ] = { BRect( 0, 0, b.right, fHeight ), BRect( 0, b.bottom - fHeight, b.right, b.bottom ) };
	SetHighUIColor( B_PANEL_BACKGROUND_COLOR );
	FillRect( BRect( 0, fHeight + 1, b.right, b.bottom - fHeight - 1 ) );

	const auto& pSample = m_pEditor->m_pOriginal;
	const bool bPlayhead = m_pEditor->m_fPlayheadOriginal >= 0;
	const long long nCenter = bPlayhead ? static_cast<long long>( m_pEditor->m_fPlayheadOriginal )
										: markerFrame( m_pEditor->m_edit.loops, m_pEditor->m_selected );
	const long long nFrames = pSample != nullptr ? pSample->getFrames() : 0;
	const int nHalf = kDetailWidth / 2;
	for ( int nChannel = 0; nChannel < 2; nChannel++ ) {
		const BRect r = rects[ nChannel ];
		fillGradient( this, r, layerBackground() );
		SetHighColor( 0, 0, 0 );
		StrokeRect( r );
		if ( pSample == nullptr ) {
			continue;
		}
		const float* pData = nChannel == 0 ? pSample->getData_L() : pSample->getData_R();
		const float fMid = std::floor( r.top + r.Height() / 2 );
		const float fScale = ( r.Height() / 2 ) * m_pEditor->m_fGain * m_fZoom;
		SetHighColor( Colors::get().waveForm );
		BPoint previous;
		bool bFirst = true;
		for ( int x = 0; x < kDetailWidth; x++ ) {
			const long long nFrame = nCenter - nHalf + x;
			if ( nFrame < 0 || nFrame >= nFrames ) {
				bFirst = true;
				continue;
			}
			const BPoint point( x, std::clamp( fMid - pData[ nFrame ] * fScale, r.top + 1, r.bottom - 1 ) );
			if ( ! bFirst ) {
				StrokeLine( previous, point );
			}
			previous = point;
			bFirst = false;
		}
		SetHighColor( bPlayhead ? Colors::get().playhead : markerColor( m_pEditor->m_selected ) );
		StrokeLine( BPoint( nHalf, r.top ), BPoint( nHalf, r.bottom ) );
	}
}

// ---------------------------------------------------------------------------
// TargetView: the edited sample with the volume and pan envelopes
// (upstream's TargetSection with its SampleEnvelope on top).

class TargetView : public BView
{
public:
	TargetView( SampleEditorWindow* pEditor )
		: BView( "target", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE ), m_pEditor( pEditor )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitMinSize( BSize( 400, kTargetHeight - 1 ) );
		SetExplicitMaxSize( BSize( B_SIZE_UNLIMITED, kTargetHeight - 1 ) );
	}

	void Draw( BRect ) override;
	void MouseDown( BPoint where ) override;
	void MouseMoved( BPoint where, uint32 transit, const BMessage* ) override;
	void MouseUp( BPoint ) override;

private:
	Sample::VelocityEnvelope& envelope( SampleEditorWindow::Envelope which ) const
	{
		return which == SampleEditorWindow::Envelope::Velocity ? m_pEditor->m_edit.velocity : m_pEditor->m_edit.pan;
	}
	BPoint toView( const EnvelopePoint& point ) const
	{
		return BPoint( std::round( point.nFrame * Bounds().Width() / kTargetWidth ),
					   std::round( point.nValue * Bounds().Height() / ( kTargetHeight - 1 ) ) );
	}
	EnvelopePoint fromView( BPoint where ) const
	{
		return EnvelopePoint(
			std::clamp( static_cast<int>( std::round( where.x * kTargetWidth / Bounds().Width() ) ), 0, kTargetWidth ),
			std::clamp( static_cast<int>( std::round( where.y * ( kTargetHeight - 1 ) / Bounds().Height() ) ), 0,
						kTargetHeight - 1 ) );
	}
	/** Index of the point near @a where in the active envelope, or -1. */
	int hit( BPoint where ) const;
	void drawEnvelope( SampleEditorWindow::Envelope which, bool bActive );

	SampleEditorWindow* m_pEditor;
	int m_nDragged = -1;
	int m_nHovered = -1;
	BPoint m_dragOrigin;
	bool m_bDragStarted = false;
};

int TargetView::hit( BPoint where ) const
{
	const auto& points = envelope( m_pEditor->m_envelope );
	int nBest = -1;
	float fBest = kGrabDistance + 1;
	for ( size_t i = 0; i < points.size(); i++ ) {
		const BPoint p = toView( points[ i ] );
		const float fDistance = std::fabs( p.x - where.x ) + std::fabs( p.y - where.y );
		if ( fDistance < fBest ) {
			fBest = fDistance;
			nBest = static_cast<int>( i );
		}
	}
	return nBest;
}

void TargetView::drawEnvelope( SampleEditorWindow::Envelope which, bool bActive )
{
	const auto& points = envelope( which );
	const rgb_color color = which == SampleEditorWindow::Envelope::Velocity ? kVelocityColor : kPanColor;
	const pattern dotted = { { 0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0x33, 0x33 } };
	const pattern fill = bActive ? B_SOLID_HIGH : B_MIXED_COLORS;
	SetHighColor( color );
	SetLowColor( 0, 0, 0, 0 );
	for ( size_t i = 1; i < points.size(); i++ ) {
		StrokeLine( toView( points[ i - 1 ] ), toView( points[ i ] ), bActive ? B_SOLID_HIGH : dotted );
	}
	for ( size_t i = 0; i < points.size(); i++ ) {
		const BPoint p = toView( points[ i ] );
		const bool bEnd = points[ i ].nFrame == 0 || points[ i ].nFrame == kTargetWidth;
		BRect r = bEnd ? BRect( p.x - 8, p.y - 4, p.x + 8, p.y + 4 ) : BRect( p.x - 4, p.y - 4, p.x + 4, p.y + 4 );
		const bool bHighlight = bActive && ( static_cast<int>( i ) == m_nDragged || static_cast<int>( i ) == m_nHovered );
		if ( bHighlight ) {
			r.InsetBy( -1, -1 );
			SetHighColor( Colors::get().highlight );
		}
		else {
			SetHighColor( color );
		}
		if ( bEnd ) {
			FillRect( r, fill );
		}
		else {
			FillEllipse( r, fill );
		}
		SetHighColor( 0, 0, 0 );
		if ( bEnd ) {
			StrokeRect( r );
		}
		else {
			StrokeEllipse( r );
		}
	}
}

void TargetView::Draw( BRect )
{
	const BRect b = Bounds();
	const float fHalf = std::floor( b.Height() / 2 );
	const auto& pSample = m_pEditor->m_pPreview;
	const long long nFrames = pSample != nullptr ? pSample->getFrames() : 0;
	drawWave( this, BRect( 0, 0, b.right, fHalf ), pSample != nullptr ? pSample->getData_L() : nullptr, nFrames,
			  m_pEditor->m_fGain, layerBackground() );
	drawWave( this, BRect( 0, fHalf + 1, b.right, b.bottom ), pSample != nullptr ? pSample->getData_R() : nullptr,
			  nFrames, m_pEditor->m_fGain, layerBackground() );

	// The other envelope dotted below the one being edited.
	using E = SampleEditorWindow::Envelope;
	const E other = m_pEditor->m_envelope == E::Velocity ? E::Pan : E::Velocity;
	drawEnvelope( other, false );
	drawEnvelope( m_pEditor->m_envelope, true );

	// The value of the point under the pointer or being dragged.
	const int nShown = m_nDragged >= 0 ? m_nDragged : m_nHovered;
	const auto& points = envelope( m_pEditor->m_envelope );
	if ( nShown >= 0 && nShown < static_cast<int>( points.size() ) ) {
		const EnvelopePoint& point = points[ nShown ];
		BString sValue;
		if ( m_pEditor->m_envelope == E::Velocity ) {
			sValue.SetToFormat( "%.2g", ( kTargetHeight - point.nValue ) / static_cast<double>( kTargetHeight ) );
		}
		else {
			sValue.SetToFormat( "%+.2g", ( kPanCenter - point.nValue ) / static_cast<double>( kPanCenter ) );
		}
		BFont font( be_plain_font );
		font.SetSize( 10 );
		SetFont( &font );
		const BPoint p = toView( point );
		const float fWidth = font.StringWidth( sValue ) + 8;
		BRect box( p.x + 8, p.y - 18, p.x + 8 + fWidth, p.y - 4 );
		if ( box.right > b.right ) {
			box.OffsetBy( -fWidth - 16, 0 );
		}
		if ( box.top < 0 ) {
			box.OffsetBy( 0, 22 );
		}
		SetHighColor( m_pEditor->m_envelope == E::Velocity ? kVelocityColor : kPanColor );
		FillRect( box );
		SetHighColor( 0, 0, 0 );
		StrokeRect( box );
		DrawString( sValue, BPoint( box.left + 4, box.bottom - 3 ) );
	}

	if ( m_pEditor->m_fPlayheadTarget >= 0 && nFrames > 0 ) {
		SetHighColor( Colors::get().playhead );
		const float x = std::floor( m_pEditor->m_fPlayheadTarget * b.Width() / nFrames );
		StrokeLine( BPoint( x, 0 ), BPoint( x, b.bottom ) );
	}
}

void TargetView::MouseDown( BPoint where )
{
	if ( m_pEditor->m_bPlaying ) {
		return;
	}
	int32 nButtons = B_PRIMARY_MOUSE_BUTTON;
	if ( Window()->CurrentMessage() != nullptr ) {
		Window()->CurrentMessage()->FindInt32( "buttons", &nButtons );
	}
	auto& points = envelope( m_pEditor->m_envelope );
	const int nHit = hit( where );

	if ( nButtons & B_SECONDARY_MOUSE_BUTTON ) {
		// Upstream: the end points go only with the whole envelope.
		if ( nHit < 0 ) {
			return;
		}
		const bool bEnd = points[ nHit ].nFrame == 0 || points[ nHit ].nFrame == kTargetWidth;
		if ( points.size() <= 2 ) {
			points.clear();
		}
		else if ( bEnd ) {
			return;
		}
		else {
			points.erase( points.begin() + nHit );
		}
		m_nHovered = -1;
		m_pEditor->envelopeChanged();
		return;
	}

	if ( nHit >= 0 ) {
		m_nDragged = nHit;
		m_dragOrigin = where;
		m_bDragStarted = false;
		SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS );
		Invalidate();
		return;
	}
	const EnvelopePoint point = fromView( where );
	if ( points.empty() ) {
		// The first click makes a flat envelope at that height.
		points.push_back( EnvelopePoint( 0, point.nValue ) );
		points.push_back( EnvelopePoint( kTargetWidth, point.nValue ) );
	}
	else {
		for ( const auto& existing : points ) {
			if ( existing.nFrame == point.nFrame ) {
				return;
			}
		}
		points.push_back( point );
		std::sort( points.begin(), points.end(), EnvelopePoint::Comparator() );
	}
	m_pEditor->envelopeChanged();
}

void TargetView::MouseMoved( BPoint where, uint32 transit, const BMessage* )
{
	auto& points = envelope( m_pEditor->m_envelope );
	if ( m_nDragged >= 0 && m_nDragged < static_cast<int>( points.size() ) ) {
		if ( ! m_bDragStarted ) {
			if ( std::fabs( where.x - m_dragOrigin.x ) + std::fabs( where.y - m_dragOrigin.y ) < 4 ) {
				return;
			}
			m_bDragStarted = true;
		}
		EnvelopePoint moved = fromView( where );
		EnvelopePoint& point = points[ m_nDragged ];
		// End points move only up and down; no two points share a column.
		if ( point.nFrame == 0 || point.nFrame == kTargetWidth ) {
			moved.nFrame = point.nFrame;
		}
		else {
			moved.nFrame = std::clamp( moved.nFrame, 1, kTargetWidth - 1 );
			for ( size_t i = 0; i < points.size(); i++ ) {
				if ( static_cast<int>( i ) != m_nDragged && points[ i ].nFrame == moved.nFrame ) {
					moved.nFrame = point.nFrame;
				}
			}
		}
		if ( moved == point ) {
			return;
		}
		point = moved;
		// Keep the order, and follow the dragged point through it.
		const EnvelopePoint dragged = point;
		std::sort( points.begin(), points.end(), EnvelopePoint::Comparator() );
		for ( size_t i = 0; i < points.size(); i++ ) {
			if ( points[ i ] == dragged ) {
				m_nDragged = static_cast<int>( i );
			}
		}
		m_pEditor->envelopeChanged();
		return;
	}
	const int nHovered = transit == B_EXITED_VIEW || m_pEditor->m_bPlaying ? -1 : hit( where );
	if ( nHovered != m_nHovered ) {
		m_nHovered = nHovered;
		Invalidate();
	}
}

void TargetView::MouseUp( BPoint )
{
	if ( m_nDragged >= 0 ) {
		m_nDragged = -1;
		Invalidate();
	}
}

// ---------------------------------------------------------------------------
// SampleEditorWindow

static BMessenger sEditor;

bool SampleEditorWindow::Edit::operator==( const Edit& other ) const
{
	return loops == other.loops && rubberband.bUse == other.rubberband.bUse &&
		   rubberband.fLengthInBeats == other.rubberband.fLengthInBeats &&
		   rubberband.fSemitonesToShift == other.rubberband.fSemitonesToShift &&
		   rubberband.nCrispness == other.rubberband.nCrispness && velocity == other.velocity && pan == other.pan;
}

void SampleEditorWindow::open( int nComponent, int nLayer, BRect parentFrame, BMessenger owner )
{
	auto pInstrument = Hydrogen::get_instance()->getSelectedInstrument();
	if ( pInstrument == nullptr || pInstrument->getComponent( nComponent ) == nullptr ) {
		return;
	}
	auto pLayer = pInstrument->getComponent( nComponent )->getLayer( nLayer );
	if ( pLayer == nullptr || pLayer->getSample() == nullptr ) {
		return;
	}
	// One editor, as upstream; the open one may ask about its changes.
	if ( sEditor.IsValid() ) {
		sEditor.SendMessage( B_QUIT_REQUESTED );
	}
	SampleEditorWindow* pWindow = new SampleEditorWindow( pInstrument, nComponent, nLayer, parentFrame, owner );
	if ( pWindow->m_pOriginal == nullptr ) {
		BString sText( B_TRANSLATE( "The sample \"%file%\" couldn't be loaded." ) );
		sText.ReplaceFirst( "%file%", toBString( pLayer->getSample()->getFilePath() ) );
		( new BAlert( B_TRANSLATE( "Sample editor" ), sText, B_TRANSLATE( "OK" ), nullptr, nullptr,
					  B_WIDTH_AS_USUAL, B_STOP_ALERT ) )->Go( nullptr );
		pWindow->Lock();
		pWindow->Quit();
		return;
	}
	sEditor = BMessenger( pWindow );
	pWindow->Show();
}

SampleEditorWindow::SampleEditorWindow( std::shared_ptr<Instrument> pInstrument, int nComponent, int nLayer,
										BRect parentFrame, BMessenger owner )
	: BWindow( BRect( 0, 0, 860, 600 ), B_TRANSLATE( "Sample editor" ), B_TITLED_WINDOW,
			   B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE )
	, m_owner( owner )
	, m_nInstrumentId( pInstrument->getId() )
	, m_nComponent( nComponent )
	, m_nLayer( nLayer )
{
	// librubberband isn't built on Haiku; the command line tool is used if
	// it's installed.
#ifdef H2CORE_HAVE_RUBBERBAND
	m_bRubberbandAvailable = true;
#else
	m_bRubberbandAvailable =
		Filesystem::fileExecutable( Preferences::get_instance()->m_sRubberBandCLIexecutable, true );
#endif

	m_pOriginalView = new OriginalView( this );
	m_pDetailView = new DetailView( this );
	m_pZoom = new BSlider( "zoom", nullptr, new BMessage( kMsgZoom ), 1, 10, B_VERTICAL );
	m_pZoom->SetToolTip( B_TRANSLATE( "Zoom the detail view" ) );
	m_pZoom->SetModificationMessage( new BMessage( kMsgZoom ) );

	auto spinner = []( const char* sName, const char* sLabel, uint32 what, const char* sTip ) {
		BSpinner* pSpinner = new BSpinner( sName, sLabel, new BMessage( what ) );
		pSpinner->SetToolTip( sTip );
		return pSpinner;
	};
	m_pStart = spinner( "start", B_TRANSLATE( "Start:" ), kMsgMarkerSpinner, B_TRANSLATE( "The frame the sample starts at" ) );
	m_pLoop = spinner( "loop", B_TRANSLATE( "Loop:" ), kMsgMarkerSpinner, B_TRANSLATE( "The frame the loop starts at" ) );
	m_pEnd = spinner( "end", B_TRANSLATE( "End:" ), kMsgMarkerSpinner, B_TRANSLATE( "The frame the sample and loop end at" ) );
	m_pCount = spinner( "count", B_TRANSLATE( "Loops:" ), kMsgCount, B_TRANSLATE( "How often the loop repeats" ) );
	m_pCount->SetRange( 0, 20000 );

	auto menuField = []( const char* sName, const char* sLabel, std::initializer_list<BString> items, uint32 what ) {
		BPopUpMenu* pMenu = new BPopUpMenu( sName );
		int32 nIndex = 0;
		for ( const BString& sItem : items ) {
			BMessage* pMessage = new BMessage( what );
			pMessage->AddInt32( "index", nIndex++ );
			pMenu->AddItem( new BMenuItem( sItem, pMessage ) );
		}
		return new BMenuField( sName, sLabel, pMenu );
	};
	m_pMode = menuField( "mode", B_TRANSLATE( "Mode:" ),
						 { B_TRANSLATE( "Forward" ), B_TRANSLATE( "Reverse" ), B_TRANSLATE( "Ping-pong" ) }, kMsgMode );

	// Rubberband
	BPopUpMenu* pLengthMenu = new BPopUpMenu( "length" );
	for ( int i = 0; i < kLengthItems; i++ ) {
		BString sLabel;
		if ( i == 0 ) {
			sLabel = B_TRANSLATE( "Off" );
		}
		else if ( i <= 6 ) {
			sLabel << "1/" << ( 64 >> ( i - 1 ) );
		}
		else {
			sLabel << static_cast<int>( lengthForIndex( i ) );
		}
		BMessage* pMessage = new BMessage( kMsgLength );
		pMessage->AddInt32( "index", i );
		pLengthMenu->AddItem( new BMenuItem( sLabel, pMessage ) );
	}
	m_pLength = new BMenuField( "length", B_TRANSLATE( "Length in beats:" ), pLengthMenu );
	m_pLength->SetToolTip( B_TRANSLATE( "Stretch the sample to this many beats at the current tempo" ) );
	m_pRatio = new BStringView( "ratio", "" );
	m_pPitch = new BDecimalSpinner( "pitch", B_TRANSLATE( "Pitch:" ), new BMessage( kMsgPitch ) );
	m_pPitch->SetMinValue( -36 );
	m_pPitch->SetMaxValue( 36 );
	m_pPitch->SetStep( 0.01 );
	m_pPitch->SetPrecision( 2 );
	m_pPitch->SetToolTip( B_TRANSLATE( "Semitones; the decimals are cents" ) );
	m_pCrispness = menuField( "crispness", B_TRANSLATE( "Crispness:" ), { "1", "2", "3", "4", "5" }, kMsgCrispness );
	m_pCrispness->SetToolTip( B_TRANSLATE( "Rubberband's transient handling" ) );

	BBox* pRubberband = new BBox( "rubberband box" );
	pRubberband->SetLabel( B_TRANSLATE( "Rubberband: tempo and pitch" ) );
	BGroupView* pRubberbandGroup = new BGroupView( B_HORIZONTAL );
	BLayoutBuilder::Group<>( pRubberbandGroup )
		.SetInsets( B_USE_ITEM_INSETS )
		.Add( m_pLength )
		.Add( m_pRatio )
		.AddGlue()
		.Add( m_pPitch )
		.Add( m_pCrispness );
	pRubberband->AddChild( pRubberbandGroup );
	m_pRubberbandBox = pRubberband;

	m_pNewLength = new BStringView( "new length", "" );
	m_pEnvelopeMenu = menuField( "envelope", B_TRANSLATE( "Envelope:" ),
								 { B_TRANSLATE( "Volume" ), B_TRANSLATE( "Pan" ) }, kMsgEnvelope );
	m_pEnvelopeMenu->SetToolTip( B_TRANSLATE( "Click to add a point, right-click to remove one" ) );
	m_pTargetView = new TargetView( this );

	m_pPlay = new BButton( "play", B_TRANSLATE( "Play" ), new BMessage( kMsgPlay ) );
	m_pPlayOriginal = new BButton( "play original", B_TRANSLATE( "Play original" ), new BMessage( kMsgPlayOriginal ) );
	m_pRevert = new BButton( "revert", B_TRANSLATE( "Revert" ), new BMessage( kMsgRevert ) );
	m_pApply = new BButton( "apply", B_TRANSLATE( "Apply" ), new BMessage( kMsgApply ) );

	BBox* pOriginalBox = new BBox( "original box" );
	pOriginalBox->SetLabel( B_TRANSLATE( "Original sample and loop" ) );
	BGroupView* pOriginal = new BGroupView( B_VERTICAL );
	BLayoutBuilder::Group<>( pOriginal )
		.SetInsets( B_USE_ITEM_INSETS )
		.AddGroup( B_HORIZONTAL )
			.Add( m_pOriginalView, 1 )
			.Add( m_pDetailView, 0 )
			.Add( m_pZoom, 0 )
		.End()
		.AddGroup( B_HORIZONTAL )
			.Add( m_pStart )
			.Add( m_pLoop )
			.Add( m_pMode )
			.Add( m_pCount )
			.Add( m_pEnd )
		.End();
	pOriginalBox->AddChild( pOriginal );

	BBox* pTargetBox = new BBox( "target box" );
	pTargetBox->SetLabel( B_TRANSLATE( "Result and envelopes" ) );
	BGroupView* pTarget = new BGroupView( B_VERTICAL );
	BLayoutBuilder::Group<>( pTarget )
		.SetInsets( B_USE_ITEM_INSETS )
		.AddGroup( B_HORIZONTAL )
			.Add( m_pNewLength )
			.AddGlue()
			.Add( m_pEnvelopeMenu )
		.End()
		.Add( m_pTargetView );
	pTargetBox->AddChild( pTarget );

	BLayoutBuilder::Group<>( this, B_VERTICAL )
		.SetInsets( B_USE_WINDOW_INSETS )
		.Add( pOriginalBox )
		.Add( m_pRubberbandBox )
		.Add( pTargetBox )
		.AddGroup( B_HORIZONTAL )
			.Add( m_pPlay )
			.Add( m_pPlayOriginal )
			.AddGlue()
			.Add( m_pRevert )
			.Add( m_pApply )
		.End();

	if ( ! m_bRubberbandAvailable ) {
		m_pRubberbandBox->Hide();
	}
	m_pEnvelopeMenu->Menu()->ItemAt( 0 )->SetMarked( true );
	if ( bind() ) {
		load();
	}
	// Remembered size and place, else centred over the main window.
	const BRect frame = Settings::frame( kFrameName, BRect() );
	if ( frame.IsValid() ) {
		MoveTo( frame.LeftTop() );
		ResizeTo( frame.Width(), frame.Height() );
	}
	else {
		CenterIn( parentFrame );
	}
}

bool SampleEditorWindow::bind()
{
	const auto pSong = Hydrogen::get_instance()->getSong();
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		return false;
	}
	auto pInstrument = pSong->getDrumkit()->getInstruments()->find( m_nInstrumentId );
	auto pComponent = pInstrument != nullptr ? pInstrument->getComponent( m_nComponent ) : nullptr;
	auto pLayer = pComponent != nullptr ? pComponent->getLayer( m_nLayer ) : nullptr;
	if ( pLayer == nullptr || pLayer->getSample() == nullptr ||
		 ( ! m_sFilePath.isEmpty() && pLayer->getSample()->getFilePath() != m_sFilePath ) ) {
		return false;
	}
	m_pInstrument = pInstrument;
	m_pLayerSample = pLayer->getSample();
	m_fGain = pLayer->getGain();
	return true;
}

void SampleEditorWindow::load()
{
	m_sFilePath = m_pLayerSample->getFilePath();
	m_license = m_pLayerSample->getLicense();
	auto pOriginal = std::make_shared<Sample>( m_sFilePath, m_license );
	m_pOriginal = pOriginal->load() ? pOriginal : nullptr;
	m_pPreview = m_pLayerSample;
	if ( m_pOriginal == nullptr ) {
		return;
	}

	m_edit.loops = m_pLayerSample->getLoops();
	if ( m_edit.loops.nStartFrame == 0 && m_edit.loops.nLoopFrame == 0 && m_edit.loops.nEndFrame == 0 ) {
		m_edit.loops.nEndFrame = m_pOriginal->getFrames() - 1;
	}
	m_edit.rubberband = m_pLayerSample->getRubberband();
	m_edit.velocity = m_pLayerSample->getVelocityEnvelope();
	m_edit.pan = m_pLayerSample->getPanEnvelope();
	if ( m_edit.velocity.empty() ) {
		m_edit.velocity = { EnvelopePoint( 0, 0 ), EnvelopePoint( kTargetWidth, 0 ) };
	}
	if ( m_edit.pan.empty() ) {
		m_edit.pan = { EnvelopePoint( 0, kPanCenter ), EnvelopePoint( kTargetWidth, kPanCenter ) };
	}
	m_applied = m_edit;

	const int32 nLast = static_cast<int32>( std::max<long long>( 0, m_pOriginal->getFrames() - 1 ) );
	for ( BSpinner* pSpinner : { m_pStart, m_pLoop, m_pEnd } ) {
		pSpinner->SetRange( 0, nLast );
	}
	updateTitle();
	updateControls();
}

void SampleEditorWindow::updateTitle()
{
	BString sTitle( B_TRANSLATE( "Sample editor" ) );
	sTitle << ": " << BPath( toBString( m_sFilePath ) ).Leaf();
	SetTitle( sTitle );
}

void SampleEditorWindow::updateControls()
{
	m_pStart->SetValue( static_cast<int32>( m_edit.loops.nStartFrame ) );
	m_pLoop->SetValue( static_cast<int32>( m_edit.loops.nLoopFrame ) );
	m_pEnd->SetValue( static_cast<int32>( m_edit.loops.nEndFrame ) );
	m_pCount->SetValue( m_edit.loops.nCount );
	if ( BMenuItem* pItem = m_pMode->Menu()->ItemAt( static_cast<int32>( m_edit.loops.mode ) ) ) {
		pItem->SetMarked( true );
	}
	if ( BMenuItem* pItem = m_pLength->Menu()->ItemAt( indexForLength( m_edit.rubberband ) ) ) {
		pItem->SetMarked( true );
	}
	m_pPitch->SetValue( m_edit.rubberband.fSemitonesToShift );
	if ( BMenuItem* pItem = m_pCrispness->Menu()->ItemAt( std::clamp( m_edit.rubberband.nCrispness, 0, 4 ) ) ) {
		pItem->SetMarked( true );
	}

	const bool bEditable = ! m_bPlaying;
	for ( BControl* pControl : std::initializer_list<BControl*>{ m_pStart, m_pLoop, m_pEnd, m_pCount } ) {
		pControl->SetEnabled( bEditable );
	}
	m_pMode->SetEnabled( bEditable );
	m_pLength->SetEnabled( bEditable );
	m_pPitch->SetEnabled( bEditable && m_edit.rubberband.bUse );
	m_pCrispness->SetEnabled( bEditable && m_edit.rubberband.bUse );
	m_pEnvelopeMenu->SetEnabled( bEditable );
	m_pZoom->SetEnabled( true );

	const bool bDirty = ! ( m_edit == m_applied );
	m_pApply->SetEnabled( bEditable && bDirty );
	m_pRevert->SetEnabled( bEditable && bDirty );
	m_pPlay->SetLabel( m_bPlaying && ! m_bPlayingOriginal ? B_TRANSLATE( "Stop" ) : B_TRANSLATE( "Play" ) );
	m_pPlayOriginal->SetLabel( m_bPlaying && m_bPlayingOriginal ? B_TRANSLATE( "Stop" )
																: B_TRANSLATE( "Play original" ) );

	BString sLength( B_TRANSLATE( "New length:" ) );
	sLength << " " << ( m_pPreview != nullptr ? m_pPreview->getFrames() : 0 ) << " " << B_TRANSLATE( "frames" );
	m_pNewLength->SetText( sLength );
	// Upstream's ratio of the stretched to the original length.
	if ( m_edit.rubberband.bUse && m_pPreview != nullptr && m_pOriginal != nullptr && m_pOriginal->getFrames() > 0 ) {
		BString sRatio;
		sRatio.SetToFormat( B_TRANSLATE( "Ratio: %.2f" ),
							static_cast<double>( m_pPreview->getFrames() ) / m_pOriginal->getFrames() );
		m_pRatio->SetText( sRatio );
	}
	else {
		m_pRatio->SetText( "" );
	}

	m_pOriginalView->Invalidate();
	m_pDetailView->Invalidate();
	m_pTargetView->Invalidate();
}

void SampleEditorWindow::setMarker( Marker marker, long long nFrame )
{
	if ( m_pOriginal == nullptr ) {
		return;
	}
	auto& loops = m_edit.loops;
	nFrame = std::clamp<long long>( nFrame, 0, m_pOriginal->getFrames() - 1 );
	switch ( marker ) {
	case Marker::Start:
		loops.nStartFrame = nFrame;
		loops.nLoopFrame = std::max( loops.nLoopFrame, nFrame );
		loops.nEndFrame = std::max( loops.nEndFrame, nFrame );
		break;
	case Marker::Loop:
		loops.nLoopFrame = nFrame;
		loops.nStartFrame = std::min( loops.nStartFrame, nFrame );
		loops.nEndFrame = std::max( loops.nEndFrame, nFrame );
		break;
	case Marker::End:
		loops.nEndFrame = nFrame;
		loops.nStartFrame = std::min( loops.nStartFrame, nFrame );
		loops.nLoopFrame = std::min( loops.nLoopFrame, nFrame );
		break;
	default:
		return;
	}
	m_selected = marker;
	scheduleUpdate();
	// All three spinners: upstream left the pushed ones stale.
	updateControls();
}

void SampleEditorWindow::envelopeChanged()
{
	scheduleUpdate();
	updateControls();
}

void SampleEditorWindow::scheduleUpdate()
{
	if ( m_bPlaying ) {
		m_bUpdatePending = true;
		return;
	}
	// Rendering reads the file again; wait until the edits pause.
	delete m_pUpdateRunner;
	BMessage update( kMsgUpdate );
	m_pUpdateRunner = new BMessageRunner( BMessenger( this ), &update, 20000, 1 );
}

std::shared_ptr<Sample> SampleEditorWindow::render() const
{
	auto pSample = std::make_shared<Sample>( m_sFilePath, m_license );
	pSample->setLoops( m_edit.loops );
	pSample->setRubberband( m_edit.rubberband );
	pSample->setVelocityEnvelope( m_edit.velocity );
	pSample->setPanEnvelope( m_edit.pan );
	const float fBpm = Hydrogen::get_instance()->getAudioEngine()->getPlayhead()->getBpm();
	return pSample->load( fBpm ) ? pSample : nullptr;
}

void SampleEditorWindow::updatePreview()
{
	delete m_pUpdateRunner;
	m_pUpdateRunner = nullptr;
	if ( m_bPlaying ) {
		m_bUpdatePending = true;
		return;
	}
	if ( auto pSample = render() ) {
		m_pPreview = pSample;
	}
	updateControls();
}

void SampleEditorWindow::apply()
{
	if ( ! bind() ) {
		( new BAlert( B_TRANSLATE( "Sample editor" ),
					  B_TRANSLATE( "The layer isn't there any more, so the changes can't be applied." ),
					  B_TRANSLATE( "OK" ), nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT ) )->Go( nullptr );
		return;
	}
	auto pSample = render();
	if ( pSample == nullptr ) {
		BString sText( B_TRANSLATE( "The sample \"%file%\" couldn't be loaded." ) );
		sText.ReplaceFirst( "%file%", toBString( m_sFilePath ) );
		( new BAlert( B_TRANSLATE( "Sample editor" ), sText, B_TRANSLATE( "OK" ), nullptr, nullptr,
					  B_WIDTH_AS_USUAL, B_STOP_ALERT ) )->Go( nullptr );
		return;
	}
	auto pNew = std::make_shared<Instrument>( m_pInstrument );
	auto pComponent = pNew->getComponent( m_nComponent );
	pNew->setSample( pComponent, pComponent->getLayer( m_nLayer ), pSample, Event::Trigger::Suppress );
	UndoStack::get().push( SongActions::replaceInstrument( pNew, m_pInstrument, B_TRANSLATE( "edit sample" ) ) );
	m_owner.SendMessage( 'Hped' );
	bind();
	m_pPreview = pSample;
	m_applied = m_edit;
	updateControls();
}

void SampleEditorWindow::startPlayback( bool bOriginal )
{
	auto pAudioEngine = Hydrogen::get_instance()->getAudioEngine();
	auto pSample = bOriginal ? m_pOriginal : m_pPreview;
	if ( pSample == nullptr || pSample->getFrames() == 0 || pAudioEngine->getAudioDriver() == nullptr ) {
		return;
	}
	m_pPlayInstrument = Instrument::from( pSample );
	m_pPlayInstrument->setIsPreviewInstrument( true );
	auto pNote = std::make_shared<Note>( m_pPlayInstrument, 0, VELOCITY_MAX, PAN_DEFAULT, LENGTH_ENTIRE_SAMPLE );
	pAudioEngine->getSampler()->previewInstrument( m_pPlayInstrument, pNote );

	m_bPlaying = true;
	m_bPlayingOriginal = bOriginal;
	m_nLastRealtimeFrame = pAudioEngine->getRealtimeFrame();
	m_fPlayed = 0;
	m_fPlayLength = static_cast<double>( pSample->getFrames() );
	// The preview's frames back to the original's, through the loops.
	const auto& loops = m_edit.loops;
	const double fSource = static_cast<double>( loops.nEndFrame - loops.nStartFrame ) +
						   static_cast<double>( loops.nCount ) * ( loops.nEndFrame - loops.nLoopFrame );
	m_fScaling = bOriginal || m_fPlayLength <= 0 ? 1 : fSource / m_fPlayLength;
	m_fPlayheadOriginal = bOriginal ? 0 : static_cast<double>( loops.nStartFrame );
	m_fPlayheadTarget = bOriginal ? -1 : 0;
	delete m_pPlayRunner;
	BMessage tick( kMsgTick );
	m_pPlayRunner = new BMessageRunner( BMessenger( this ), &tick, 20000 );
	updateControls();
}

void SampleEditorWindow::stopPlayback()
{
	delete m_pPlayRunner;
	m_pPlayRunner = nullptr;
	if ( m_pPlayInstrument != nullptr ) {
		Hydrogen::get_instance()->getAudioEngine()->getSampler()->releasePlayingNotes( m_pPlayInstrument );
		m_pPlayInstrument = nullptr;
	}
	m_bPlaying = false;
	m_fPlayheadOriginal = -1;
	m_fPlayheadTarget = -1;
	updateControls();
	if ( m_bUpdatePending ) {
		m_bUpdatePending = false;
		scheduleUpdate();
	}
}

void SampleEditorWindow::tick()
{
	auto pAudioEngine = Hydrogen::get_instance()->getAudioEngine();
	auto pDriver = pAudioEngine->getAudioDriver();
	auto pSample = m_bPlayingOriginal ? m_pOriginal : m_pPreview;
	if ( ! m_bPlaying || pDriver == nullptr || pDriver->getSampleRate() == 0 || pSample == nullptr ) {
		stopPlayback();
		return;
	}
	const long long nNow = pAudioEngine->getRealtimeFrame();
	m_fPlayed += static_cast<double>( nNow - m_nLastRealtimeFrame ) * pSample->getSampleRate() / pDriver->getSampleRate();
	m_nLastRealtimeFrame = nNow;
	if ( m_fPlayed >= m_fPlayLength ) {
		stopPlayback();
		return;
	}
	if ( m_bPlayingOriginal ) {
		m_fPlayheadOriginal = m_fPlayed;
	}
	else {
		m_fPlayheadTarget = m_fPlayed;
		const auto& loops = m_edit.loops;
		const double fSource = m_fPlayed * m_fScaling;
		const double fFirst = static_cast<double>( loops.nEndFrame - loops.nStartFrame );
		const double fLoop = static_cast<double>( loops.nEndFrame - loops.nLoopFrame );
		if ( fSource < fFirst || fLoop <= 0 ) {
			m_fPlayheadOriginal = std::min<double>( loops.nStartFrame + fSource, loops.nEndFrame );
		}
		else {
			const double fInLoop = fSource - fFirst;
			const double fOffset = std::fmod( fInLoop, fLoop );
			switch ( m_edit.loops.mode ) {
			case Sample::Loops::Mode::Reverse:
				m_fPlayheadOriginal = loops.nEndFrame - fOffset;
				break;
			case Sample::Loops::Mode::PingPong:
				m_fPlayheadOriginal = static_cast<long long>( fInLoop / fLoop ) % 2 == 0 ? loops.nEndFrame - fOffset
																						  : loops.nLoopFrame + fOffset;
				break;
			case Sample::Loops::Mode::Forward:
			default:
				m_fPlayheadOriginal = loops.nLoopFrame + fOffset;
				break;
			}
		}
	}
	m_pOriginalView->Invalidate();
	m_pDetailView->Invalidate();
	m_pTargetView->Invalidate();
}

void SampleEditorWindow::WindowActivated( bool bActive )
{
	BWindow::WindowActivated( bActive );
	if ( ! bActive || m_bPlaying || m_pOriginal == nullptr ) {
		return;
	}
	// Edited elsewhere meanwhile (undo, another kit)? Follow it unless
	// there are changes here.
	auto pBefore = m_pLayerSample;
	if ( ! bind() ) {
		return;
	}
	if ( m_pLayerSample != pBefore && m_edit == m_applied ) {
		load();
	}
}

bool SampleEditorWindow::QuitRequested()
{
	if ( m_bPlaying ) {
		stopPlayback();
	}
	if ( m_pOriginal != nullptr && ! ( m_edit == m_applied ) ) {
		BAlert* pAlert = new BAlert( B_TRANSLATE( "Sample editor" ),
									 B_TRANSLATE( "Apply the changes to the sample before closing?" ),
									 B_TRANSLATE( "Cancel" ), B_TRANSLATE( "Don't apply" ), B_TRANSLATE( "Apply" ),
									 B_WIDTH_AS_USUAL, B_WARNING_ALERT );
		pAlert->SetShortcut( 0, B_ESCAPE );
		switch ( pAlert->Go() ) {
		case 0:
			return false;
		case 2:
			apply();
			break;
		default:
			break;
		}
	}
	delete m_pUpdateRunner;
	m_pUpdateRunner = nullptr;
	Settings::setFrame( kFrameName, Frame() );
	return true;
}

void SampleEditorWindow::MessageReceived( BMessage* pMessage )
{
	int32 nIndex = 0;
	pMessage->FindInt32( "index", &nIndex );
	switch ( pMessage->what ) {
	case kMsgMarkerSpinner: {
		void* pSource = nullptr;
		pMessage->FindPointer( "source", &pSource );
		const Marker marker = pSource == m_pStart ? Marker::Start : pSource == m_pLoop ? Marker::Loop
															  : pSource == m_pEnd ? Marker::End : Marker::None;
		if ( marker == Marker::None ) {
			break;
		}
		BSpinner* pSpinner = static_cast<BSpinner*>( pSource );
		// SetValue() echoes; only real changes count.
		if ( pSpinner->Value() != markerFrame( m_edit.loops, marker ) ) {
			setMarker( marker, pSpinner->Value() );
		}
		else if ( m_selected != marker ) {
			m_selected = marker;
			m_pDetailView->Invalidate();
		}
		break;
	}
	case kMsgCount:
		if ( m_pCount->Value() != m_edit.loops.nCount ) {
			m_edit.loops.nCount = m_pCount->Value();
			scheduleUpdate();
			updateControls();
		}
		break;
	case kMsgMode:
		if ( static_cast<int32>( m_edit.loops.mode ) != nIndex ) {
			m_edit.loops.mode = static_cast<Sample::Loops::Mode>( nIndex );
			scheduleUpdate();
			updateControls();
		}
		break;
	case kMsgLength: {
		// Upstream's "Off" did nothing when the length was already 4.
		auto& rubberband = m_edit.rubberband;
		const bool bUse = nIndex != 0;
		const float fLength = bUse ? lengthForIndex( nIndex ) : 4.0f;
		if ( bUse != rubberband.bUse || fLength != rubberband.fLengthInBeats ) {
			// A sample that never used Rubberband carries the core's default
			// pitch of 1 semitone; stretching shouldn't transpose it too.
			if ( bUse && ! rubberband.bUse && ! m_pLayerSample->getRubberband().bUse ) {
				rubberband.fSemitonesToShift = 0;
			}
			rubberband.bUse = bUse;
			rubberband.fLengthInBeats = fLength;
			scheduleUpdate();
			updateControls();
		}
		break;
	}
	case kMsgPitch:
		if ( std::fabs( m_pPitch->Value() - m_edit.rubberband.fSemitonesToShift ) >= 0.0001 ) {
			m_edit.rubberband.fSemitonesToShift = static_cast<float>( m_pPitch->Value() );
			scheduleUpdate();
			updateControls();
		}
		break;
	case kMsgCrispness:
		// Upstream never connected this menu.
		if ( nIndex != m_edit.rubberband.nCrispness ) {
			m_edit.rubberband.nCrispness = nIndex;
			scheduleUpdate();
			updateControls();
		}
		break;
	case kMsgEnvelope:
		m_envelope = nIndex == 0 ? Envelope::Velocity : Envelope::Pan;
		m_pTargetView->Invalidate();
		break;
	case kMsgZoom:
		m_pDetailView->m_fZoom = static_cast<float>( m_pZoom->Value() );
		m_pDetailView->Invalidate();
		break;
	case kMsgUpdate:
		updatePreview();
		break;
	case kMsgPlay:
	case kMsgPlayOriginal: {
		const bool bOriginal = pMessage->what == kMsgPlayOriginal;
		const bool bWasSame = m_bPlaying && m_bPlayingOriginal == bOriginal;
		if ( m_bPlaying ) {
			stopPlayback();
		}
		if ( ! bWasSame ) {
			// Play what's shown: render pending edits first.
			if ( m_pUpdateRunner != nullptr ) {
				updatePreview();
			}
			startPlayback( bOriginal );
		}
		break;
	}
	case kMsgTick:
		tick();
		break;
	case kMsgRevert:
		m_edit = m_applied;
		m_pPreview = m_pLayerSample;
		delete m_pUpdateRunner;
		m_pUpdateRunner = nullptr;
		updateControls();
		break;
	case kMsgApply:
		apply();
		break;
	default:
		BWindow::MessageReceived( pMessage );
	}
}
