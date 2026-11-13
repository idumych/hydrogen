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

#include "PatternEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <StringView.h>
#include <Window.h>

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
#include <core/Basics/Instrument.h>
#include <core/Basics/InstrumentList.h>
#include <core/Basics/Note.h>
#include <core/Basics/Pattern.h>
#include <core/Basics/PatternList.h>
#include <core/Basics/Song.h>
#include <core/CoreActionController.h>
#include <core/Globals.h>
#include <core/Hydrogen.h>
#include <core/Preferences/Preferences.h>
#include <core/Sampler/Sampler.h>

#include "Colors.h"
#include "PatternActions.h"
#include "UndoStack.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PatternEditor"

using namespace H2Core;

enum : uint32 {
	kMsgSizeChanged = 'PEsz',
	kMsgResolution = 'PErs',
};

// Upstream's resolution menu: {resolution, triplets}; resolution 192 is "off".
static const struct { const char* sLabel; int nResolution; bool bTriplets; } kResolutions[] = {
	{ "1/4", 4, false }, { "1/8", 8, false }, { "1/16", 16, false }, { "1/32", 32, false },
	{ "1/64", 64, false }, { nullptr, 0, false },
	{ "1/4T", 8, true }, { "1/8T", 16, true }, { "1/16T", 32, true }, { "1/32T", 64, true },
	{ nullptr, 0, false }, { "off", 4 * nTicksPerQuarter, false },
};

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

// ---------------------------------------------------------------------------
// PatternEditorState

void PatternEditorState::reload()
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pSong = pHydrogen->getSong();
	auto pPref = Preferences::get_instance();

	pPattern = nullptr;
	rows.clear();
	nPattern = pHydrogen->getSelectedPatternNumber();
	if ( pSong != nullptr && pSong->getPatternList() != nullptr && nPattern >= 0 &&
		 nPattern < pSong->getPatternList()->size() ) {
		pPattern = pSong->getPatternList()->get( nPattern );
	}
	if ( pSong != nullptr && pSong->getDrumkit() != nullptr ) {
		auto pInstruments = pSong->getDrumkit()->getInstruments();
		for ( int i = 0; i < pInstruments->size(); i++ ) {
			rows.push_back( pInstruments->get( i ) );
		}
	}
	nSelectedRow = pHydrogen->getSelectedInstrumentNumber();
	fGridWidth = std::max( 1u, pPref->getPatternEditorGridWidth() );
	fRowHeight = std::max( 12u, pPref->getPatternEditorGridHeight() );
	nResolution = pPref->getPatternEditorGridResolution();
	bTriplets = pPref->isPatternEditorUsingTriplets();
	bQuantize = pPref->getQuantizeEvents() && nResolution < 4 * nTicksPerQuarter;
}

int PatternEditorState::length() const
{
	return pPattern != nullptr ? pPattern->getLength() : 4 * nTicksPerQuarter;
}

int PatternEditorState::granularity() const
{
	if ( nResolution <= 0 ) {
		return 1;
	}
	return std::max( 1, 4 * 4 * nTicksPerQuarter / ( ( bTriplets ? 3 : 4 ) * nResolution ) );
}

int PatternEditorState::xToTick( float fX ) const
{
	const int nStep = bQuantize ? granularity() : 1;
	const float fStepWidth = fGridWidth * nStep;
	const int nTick = (int) std::floor( ( fX - kMargin + fStepWidth / 2 ) / fStepWidth ) * nStep;
	if ( nTick < 0 || nTick >= length() ) {
		return -1;
	}
	return nTick;
}

float PatternEditorState::width() const
{
	return kMargin + fGridWidth * length() + 1;
}

bool PatternEditorState::rowPlays( int nRow ) const
{
	if ( nRow < 0 || nRow >= (int) rows.size() ) {
		return false;
	}
	const auto& pInstrument = rows[ nRow ];
	bool bAnySoloed = false;
	for ( const auto& p : rows ) {
		bAnySoloed |= p->isSoloed();
	}
	return pInstrument->hasSamples() && ! pInstrument->isMuted() &&
		( ! bAnySoloed || pInstrument->isSoloed() );
}

/** The row a note belongs to, or -1 for notes of instruments not in the kit. */
static int rowOf( const PatternEditorState& state, const std::shared_ptr<Note>& pNote )
{
	for ( size_t i = 0; i < state.rows.size(); i++ ) {
		if ( state.rows[ i ]->getId() == pNote->getInstrumentId() ) {
			return (int) i;
		}
	}
	return -1;
}

/** Upstream's grid line hierarchy: beats darkest, finer steps lighter. */
static rgb_color gridLineColor( const PatternEditorState& state, int nTick )
{
	const EditorColors& c = Colors::get();
	if ( state.bTriplets ) {
		return ( nTick / state.granularity() ) % 3 == 0 ? c.line1 : c.line3;
	}
	if ( nTick % 48 == 0 ) return c.line1;
	if ( nTick % 24 == 0 ) return c.line2;
	if ( nTick % 12 == 0 ) return c.line3;
	if ( nTick % 6 == 0 ) return c.line4;
	return c.line5;
}

static void drawPlayhead( BView* pView, const PatternEditorState& state, float fHeight )
{
	if ( state.nPlayheadTick < 0 ) {
		return;
	}
	const float fX = state.tickToX( state.nPlayheadTick );
	pView->SetHighColor( Colors::get().playhead );
	pView->SetPenSize( 2 );
	pView->StrokeLine( BPoint( fX, 0 ), BPoint( fX, fHeight ) );
	pView->SetPenSize( 1 );
}

static void previewInstrument( const std::shared_ptr<Instrument>& pInstrument, float fVelocity )
{
	if ( pInstrument == nullptr || ! Preferences::get_instance()->getHearNewNotes() ) {
		return;
	}
	Hydrogen::get_instance()->getAudioEngine()->getSampler()->noteOn(
		std::make_shared<Note>( pInstrument, 0, fVelocity ) );
}

// ---------------------------------------------------------------------------
// KitLabel: the drumkit's name above the instrument list.

class KitLabel : public BView
{
public:
	KitLabel( PatternEditorState& state )
		: BView( "kit label", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE ), m_state( state )
	{
		SetExplicitSize( BSize( PatternEditorState::kListWidth - 1,
								PatternEditorState::kRulerHeight - 1 ) );
	}

	void Draw( BRect ) override
	{
		const EditorColors& c = Colors::get();
		BRect bounds = Bounds();
		SetHighColor( c.instrumentAlternateRow );
		FillRect( bounds );
		BString sName;
		auto pSong = Hydrogen::get_instance()->getSong();
		if ( pSong != nullptr && pSong->getDrumkit() != nullptr ) {
			sName = toBString( pSong->getDrumkit()->getName() );
		}
		BFont font( be_bold_font );
		SetFont( &font );
		font_height fh;
		font.GetHeight( &fh );
		SetHighColor( c.instrumentText );
		SetLowColor( c.instrumentAlternateRow );
		DrawString( sName, BPoint( ( bounds.Width() - StringWidth( sName ) ) / 2,
								   ( bounds.Height() + fh.ascent - fh.descent ) / 2 ) );
	}

private:
	PatternEditorState& m_state;
};

// ---------------------------------------------------------------------------
// InstrumentRows: one row per instrument, name | M | S (upstream's sidebar).

class InstrumentRows : public BView
{
public:
	static constexpr float kButtonWidth = 19;

	InstrumentRows( PatternEditorState& state )
		: BView( "instrument list", B_WILL_DRAW ), m_state( state )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitMinSize( BSize( PatternEditorState::kListWidth - 1, 40 ) );
		SetExplicitMaxSize( BSize( PatternEditorState::kListWidth - 1, B_SIZE_UNLIMITED ) );
	}

	void Draw( BRect updateRect ) override
	{
		const EditorColors& c = Colors::get();
		const float h = m_state.fRowHeight;
		const float w = Bounds().Width();
		BFont font( be_bold_font );
		SetFont( &font );
		font_height fh;
		font.GetHeight( &fh );

		const int nFirst = std::max( 0, (int) ( updateRect.top / h ) );
		const int nLast = std::min( (int) m_state.rows.size() - 1, (int) ( updateRect.bottom / h ) );
		for ( int i = nFirst; i <= nLast; i++ ) {
			const auto& pInstrument = m_state.rows[ i ];
			const bool bSelected = i == m_state.nSelectedRow;
			const rgb_color bg = bSelected ? c.instrumentSelectedRow
				: ( i % 2 ? c.instrumentAlternateRow : c.instrumentRow );
			BRect row( 0, i * h, w, ( i + 1 ) * h - 1 );
			SetHighColor( bg );
			FillRect( row );
			SetHighColor( Colors::darker( bg, 130 ) );
			StrokeLine( row.LeftBottom(), row.RightBottom() );

			BString sName = toBString( pInstrument->getName() );
			const float fNameWidth = w - 2 * kButtonWidth - 12;
			font.TruncateString( &sName, B_TRUNCATE_END, fNameWidth );
			SetHighColor( bSelected ? c.instrumentSelectedText : c.instrumentText );
			SetLowColor( bg );
			DrawString( sName, BPoint( 10, row.top + ( h + fh.ascent - fh.descent ) / 2 ) );

			drawButton( muteRect( i ), "M", pInstrument->isMuted(), c.mute, c.muteText );
			drawButton( soloRect( i ), "S", pInstrument->isSoloed(), c.solo, c.soloText );
		}
		// Below the last instrument.
		const float fBottom = m_state.rows.size() * h;
		if ( updateRect.bottom >= fBottom ) {
			SetHighColor( c.window );
			FillRect( BRect( 0, fBottom, w, std::max( updateRect.bottom, fBottom ) ) );
		}
	}

	void MouseDown( BPoint where ) override
	{
		const int nRow = (int) ( where.y / m_state.fRowHeight );
		if ( nRow < 0 || nRow >= (int) m_state.rows.size() ) {
			return;
		}
		const auto& pInstrument = m_state.rows[ nRow ];
		if ( muteRect( nRow ).Contains( where ) ) {
			CoreActionController::setStripIsMuted( nRow, ! pInstrument->isMuted(), false );
		}
		else if ( soloRect( nRow ).Contains( where ) ) {
			CoreActionController::setStripIsSoloed( nRow, ! pInstrument->isSoloed(), false );
		}
		else {
			// Like upstream: the click position sets the preview's velocity.
			previewInstrument( pInstrument,
				std::clamp( where.x / ( Bounds().Width() - 2 * kButtonWidth ), 0.0f, 1.0f ) );
		}
		Hydrogen::get_instance()->setSelectedInstrumentNumber( nRow );
	}

private:
	BRect muteRect( int nRow ) const
	{
		const float w = Bounds().Width();
		const float y = nRow * m_state.fRowHeight;
		return BRect( w - 2 * kButtonWidth + 1, y + 1, w - kButtonWidth - 1, y + m_state.fRowHeight - 2 );
	}

	BRect soloRect( int nRow ) const
	{
		const float w = Bounds().Width();
		const float y = nRow * m_state.fRowHeight;
		return BRect( w - kButtonWidth + 1, y + 1, w - 1, y + m_state.fRowHeight - 2 );
	}

	void drawButton( BRect rect, const char* sLabel, bool bOn, rgb_color on, rgb_color onText )
	{
		const EditorColors& c = Colors::get();
		SetHighColor( bOn ? on : c.button );
		FillRect( rect );
		SetHighColor( Colors::darker( bOn ? on : c.button, 150 ) );
		StrokeRect( rect );
		font_height fh;
		GetFontHeight( &fh );
		SetHighColor( bOn ? onText : on );
		SetLowColor( bOn ? on : c.button );
		DrawString( sLabel, BPoint( rect.left + ( rect.Width() - StringWidth( sLabel ) ) / 2 + 1,
									rect.top + ( rect.Height() + fh.ascent - fh.descent ) / 2 + 1 ) );
	}

	PatternEditorState& m_state;
};

// ---------------------------------------------------------------------------
// PatternRuler: beat numbers above the grid; clicking locates there.

class PatternRuler : public BView
{
public:
	PatternRuler( PatternEditorState& state )
		: BView( "pattern ruler", B_WILL_DRAW ), m_state( state )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitMinSize( BSize( 100, PatternEditorState::kRulerHeight - 1 ) );
		SetExplicitMaxSize( BSize( B_SIZE_UNLIMITED, PatternEditorState::kRulerHeight - 1 ) );
	}

	void Draw( BRect updateRect ) override
	{
		const EditorColors& c = Colors::get();
		const float h = Bounds().Height();
		SetHighColor( c.ruler );
		FillRect( updateRect );
		const float fEnd = m_state.tickToX( m_state.length() );
		if ( updateRect.right > fEnd ) {
			SetHighColor( c.window );
			FillRect( BRect( fEnd, 0, updateRect.right, h ) );
		}

		SetHighColor( c.rulerText );
		SetLowColor( c.ruler );
		const int nStep = m_state.granularity();
		for ( int nTick = 0; nTick <= m_state.length(); nTick += nStep ) {
			const float fX = m_state.tickToX( nTick );
			if ( fX < updateRect.left - 30 || fX > updateRect.right + 1 ) {
				continue;
			}
			if ( nTick % nTicksPerQuarter == 0 ) {
				StrokeLine( BPoint( fX, h - 12 ), BPoint( fX, h ) );
				if ( nTick < m_state.length() ) {
					BString sBeat;
					sBeat << nTick / nTicksPerQuarter + 1;
					DrawString( sBeat, BPoint( fX + 3, h - 6 ) );
				}
			}
			else {
				StrokeLine( BPoint( fX, h - 5 ), BPoint( fX, h ) );
			}
		}
		drawPlayhead( this, m_state, h );
	}

	void MouseDown( BPoint where ) override
	{
		const int nTick = m_state.xToTick( where.x );
		if ( nTick >= 0 && Hydrogen::get_instance()->getMode() == Song::Mode::Pattern ) {
			CoreActionController::locateToTick( nTick );
		}
	}

private:
	PatternEditorState& m_state;
};

// ---------------------------------------------------------------------------
// DrumGrid: the notes. Click toggles a note; Shift+click adds a note-off.

class DrumGrid : public BView
{
public:
	DrumGrid( PatternEditor* pEditor )
		: BView( "drum grid", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE )
		, m_pEditor( pEditor ), m_state( pEditor->state() )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitMinSize( BSize( 200, 60 ) );
	}

	void ScrollTo( BPoint where ) override
	{
		BView::ScrollTo( where );
		m_pEditor->gridScrolled( where );
	}

	void FrameResized( float, float ) override
	{
		m_pEditor->updateScrollRanges();
		m_pEditor->gridScrolled( Bounds().LeftTop() );
	}

	void Draw( BRect updateRect ) override
	{
		const EditorColors& c = Colors::get();
		const float h = m_state.fRowHeight;
		const float fEnd = m_state.tickToX( m_state.length() );
		const int nRows = (int) m_state.rows.size();

		// Row backgrounds, and the area after the pattern's end.
		for ( int i = std::max( 0, (int) ( updateRect.top / h ) );
			  i < nRows && i * h <= updateRect.bottom; i++ ) {
			const rgb_color bg = i == m_state.nSelectedRow ? c.selectedRow
				: ( i % 2 ? c.alternateRow : c.background );
			SetHighColor( bg );
			FillRect( BRect( updateRect.left, i * h, std::min( updateRect.right, fEnd ), ( i + 1 ) * h - 1 ) );
		}
		SetHighColor( c.window );
		if ( updateRect.right > fEnd ) {
			FillRect( BRect( fEnd + 1, updateRect.top, updateRect.right, updateRect.bottom ) );
		}
		if ( updateRect.bottom >= nRows * h ) {
			FillRect( BRect( updateRect.left, nRows * h, updateRect.right, updateRect.bottom ) );
		}

		// Grid lines in the lower 30% of each row, row separators dotted.
		const int nStep = m_state.granularity();
		BeginLineArray( 4096 );
		for ( int nTick = 0; nTick <= m_state.length(); nTick += nStep ) {
			const float fX = m_state.tickToX( nTick );
			if ( fX < updateRect.left || fX > updateRect.right ) {
				continue;
			}
			const rgb_color line = gridLineColor( m_state, nTick );
			for ( int i = std::max( 0, (int) ( updateRect.top / h ) );
				  i < nRows && i * h <= updateRect.bottom; i++ ) {
				AddLine( BPoint( fX, i * h + std::round( h * 0.7f ) ), BPoint( fX, ( i + 1 ) * h - 1 ), line );
			}
		}
		EndLineArray();
		SetHighColor( c.line );
		SetLowColor( c.background );
		const pattern dotted = { { 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 } };
		for ( int i = std::max( 0, (int) ( updateRect.top / h ) );
			  i < nRows && i * h <= updateRect.bottom; i++ ) {
			StrokeLine( BPoint( updateRect.left, ( i + 1 ) * h - 1 ),
						BPoint( std::min( updateRect.right, fEnd ), ( i + 1 ) * h - 1 ), dotted );
		}

		drawNotes( updateRect );
		drawPlayhead( this, m_state, Bounds().bottom );
	}

	void MouseDown( BPoint where ) override
	{
		MakeFocus( true );
		const int nRow = (int) ( where.y / m_state.fRowHeight );
		if ( nRow < 0 || nRow >= (int) m_state.rows.size() || m_state.pPattern == nullptr ) {
			return;
		}
		int32 nButtons = B_PRIMARY_MOUSE_BUTTON;
		if ( Window()->CurrentMessage() != nullptr ) {
			Window()->CurrentMessage()->FindInt32( "buttons", &nButtons );
		}
		if ( nRow != m_state.nSelectedRow ) {
			Hydrogen::get_instance()->setSelectedInstrumentNumber( nRow );
		}
		if ( ! ( nButtons & B_PRIMARY_MOUSE_BUTTON ) ) {
			return;
		}

		// A note close enough to the click: delete it.
		auto pHit = noteAt( where, nRow );
		if ( pHit != nullptr ) {
			UndoStack::get().push( PatternActions::removeNote(
				m_state.nPattern, PatternActions::NoteSpec::from( pHit ) ) );
			return;
		}

		const int nTick = m_state.xToTick( where.x );
		if ( nTick < 0 ) {
			return;
		}
		const auto& pInstrument = m_state.rows[ nRow ];
		PatternActions::NoteSpec spec;
		spec.nPosition = nTick;
		spec.id = pInstrument->getId();
		spec.sType = pInstrument->getType();
		spec.bNoteOff = ( modifiers() & B_SHIFT_KEY ) != 0;
		if ( ! spec.bNoteOff ) {
			previewInstrument( pInstrument, VELOCITY_DEFAULT );
		}
		UndoStack::get().push( PatternActions::addNote( m_state.nPattern, spec ) );
	}

	void MouseWheelChanged( BMessage* pMessage )
	{
		float fDeltaY = 0;
		pMessage->FindFloat( "be:wheel_delta_y", &fDeltaY );
		m_pEditor->zoom( fDeltaY < 0 );
	}

	void MessageReceived( BMessage* pMessage ) override
	{
		// Command+wheel zooms (HIG); a plain wheel scrolls as usual.
		if ( pMessage->what == B_MOUSE_WHEEL_CHANGED && ( modifiers() & B_COMMAND_KEY ) ) {
			MouseWheelChanged( pMessage );
			return;
		}
		BView::MessageReceived( pMessage );
	}

private:
	/** Upstream's getCursorMargin(): how far from a note a click still hits it. */
	float hitMargin() const
	{
		if ( ! m_state.bQuantize ) {
			return 4;
		}
		if ( m_state.nResolution < 32 ) {
			return 10;
		}
		return m_state.nResolution < 4 * nTicksPerQuarter ? 5 : 4;
	}

	std::shared_ptr<Note> noteAt( BPoint where, int nRow ) const
	{
		std::shared_ptr<Note> pBest;
		float fBest = hitMargin() + 0.5f;
		for ( const auto& [ nPos, pNote ] : *m_state.pPattern->getNotes() ) {
			if ( pNote == nullptr || rowOf( m_state, pNote ) != nRow ) {
				continue;
			}
			const float fDistance = std::abs( m_state.tickToX( nPos ) - where.x );
			if ( fDistance < fBest ) {
				fBest = fDistance;
				pBest = pNote;
			}
		}
		return pBest;
	}

	void drawNotes( BRect updateRect )
	{
		if ( m_state.pPattern == nullptr ) {
			return;
		}
		const EditorColors& c = Colors::get();
		const float h = m_state.fRowHeight;
		SetFlags( Flags() | B_SUBPIXEL_PRECISE );
		SetDrawingMode( B_OP_OVER );
		std::map<std::pair<int, int>, int> stacked;
		for ( const auto& [ nPos, pNote ] : *m_state.pPattern->getNotes() ) {
			if ( pNote == nullptr ) {
				continue;
			}
			const int nRow = rowOf( m_state, pNote );
			if ( nRow < 0 ) {
				continue;
			}
			const float fX = m_state.tickToX( nPos );
			const float fY = nRow * h + h / 2;
			const float fTail = pNote->getLength() != LENGTH_ENTIRE_SAMPLE && ! pNote->getNoteOff()
				? m_state.fGridWidth * pNote->getLength() - 1 : 0;
			if ( fX + fTail + 6 < updateRect.left || fX - 6 > updateRect.right ||
				 fY + 6 < updateRect.top || fY - 6 > updateRect.bottom ) {
				continue;
			}
			const rgb_color fill = pNote->getNoteOff() ? c.noteOff
														: Colors::noteColor( pNote->getVelocity() );
			if ( fTail > 0 ) {
				SetHighColor( fill );
				FillRect( BRect( fX, fY - 1, fX + fTail, fY + 1 ) );
				SetHighColor( c.noteOutline );
				StrokeRect( BRect( fX, fY - 2, fX + fTail, fY + 2 ) );
				StrokeLine( BPoint( fX + fTail, fY - 4 ), BPoint( fX + fTail, fY + 4 ) );
			}
			SetHighColor( fill );
			FillEllipse( BPoint( fX, fY ), 4, 4 );
			SetHighColor( c.noteOutline );
			StrokeEllipse( BPoint( fX, fY ), 4, 4 );
			stacked[ { nRow, nPos } ]++;
		}
		// Several notes on one cell: "N×" to the left, like upstream.
		SetHighColor( c.gridText );
		SetDrawingMode( B_OP_OVER );
		BFont small( be_plain_font );
		small.SetSize( std::max( 8.0f, be_plain_font->Size() * 0.75f ) );
		SetFont( &small );
		for ( const auto& [ key, nCount ] : stacked ) {
			if ( nCount > 1 ) {
				BString sCount;
				sCount << nCount << "×";
				DrawString( sCount, BPoint( m_state.tickToX( key.second ) - 6 - StringWidth( sCount ),
											key.first * h + h / 2 + 4 ) );
			}
		}
		SetDrawingMode( B_OP_COPY );
	}

	PatternEditor* m_pEditor;
	PatternEditorState& m_state;
};

// ---------------------------------------------------------------------------
// VelocityLane: velocity bars of the selected instrument's notes. Drag
// across them to set velocities; one drag is one undo step.

class VelocityLane : public BView
{
public:
	VelocityLane( PatternEditorState& state )
		: BView( "velocity lane", B_WILL_DRAW ), m_state( state ), m_bDragging( false )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitMinSize( BSize( 100, PatternEditorState::kVelocityHeight - 1 ) );
		SetExplicitMaxSize( BSize( B_SIZE_UNLIMITED, PatternEditorState::kVelocityHeight - 1 ) );
	}

	void Draw( BRect updateRect ) override
	{
		const EditorColors& c = Colors::get();
		const float h = Bounds().Height();
		const float fEnd = m_state.tickToX( m_state.length() );
		SetHighColor( c.background );
		FillRect( updateRect );
		if ( updateRect.right > fEnd ) {
			SetHighColor( c.window );
			FillRect( BRect( fEnd + 1, 0, updateRect.right, h ) );
		}

		const pattern dotted = { { 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 } };
		SetLowColor( c.background );
		for ( int nTick = 0; nTick <= m_state.length(); nTick += m_state.granularity() ) {
			const float fX = m_state.tickToX( nTick );
			if ( fX >= updateRect.left && fX <= updateRect.right ) {
				SetHighColor( gridLineColor( m_state, nTick ) );
				StrokeLine( BPoint( fX, 0 ), BPoint( fX, h ), dotted );
			}
		}
		SetHighColor( c.line5 );
		for ( int i = 1; i < 10; i++ ) {
			const float fY = std::round( h * i / 10 );
			StrokeLine( BPoint( updateRect.left, fY ), BPoint( std::min( fEnd, updateRect.right ), fY ),
						dotted );
		}

		if ( m_state.pPattern != nullptr ) {
			SetDrawingMode( B_OP_OVER );
			for ( const auto& [ nPos, pNote ] : *m_state.pPattern->getNotes() ) {
				if ( pNote == nullptr || pNote->getNoteOff() ||
					 rowOf( m_state, pNote ) != m_state.nSelectedRow ) {
					continue;
				}
				const float fX = m_state.tickToX( nPos );
				const float fValue = std::round( pNote->getVelocity() * h );
				BRect bar( fX - 2, h - fValue, fX + 2, h );
				SetHighColor( Colors::noteColor( pNote->getVelocity() ) );
				FillRoundRect( bar, 2, 2 );
				SetHighColor( c.noteOutline );
				StrokeRoundRect( bar, 2, 2 );
			}
			SetDrawingMode( B_OP_COPY );
		}
		drawPlayhead( this, m_state, h );
	}

	void MouseDown( BPoint where ) override
	{
		if ( m_state.pPattern == nullptr || m_state.nSelectedRow < 0 ) {
			return;
		}
		m_bDragging = true;
		m_fLastX = where.x;
		m_original.clear();
		SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS );
		paint( where );
	}

	void MouseMoved( BPoint where, uint32, const BMessage* ) override
	{
		if ( m_bDragging ) {
			paint( where );
		}
	}

	void MouseUp( BPoint ) override
	{
		if ( ! m_bDragging ) {
			return;
		}
		m_bDragging = false;
		auto pMacro = std::make_unique<MacroCommand>( B_TRANSLATE( "change velocity" ) );
		for ( const auto& [ nPos, edit ] : m_original ) {
			pMacro->add( PatternActions::setVelocity( m_state.nPattern, edit.first, edit.second ) );
		}
		if ( ! pMacro->isEmpty() ) {
			UndoStack::get().push( std::move( pMacro ) );
		}
		m_original.clear();
	}

private:
	/** Sets every note of the selected row between the last and the
	 * current pointer position to the pointer's height. */
	void paint( BPoint where )
	{
		const float h = Bounds().Height();
		const float fVelocity = std::clamp( ( h - where.y ) / h, 0.0f, 1.0f );
		const float fFrom = std::min( m_fLastX, where.x ) - 4;
		const float fTo = std::max( m_fLastX, where.x ) + 4;
		m_fLastX = where.x;
		for ( const auto& [ nPos, pNote ] : *m_state.pPattern->getNotes() ) {
			if ( pNote == nullptr || pNote->getNoteOff() ||
				 rowOf( m_state, pNote ) != m_state.nSelectedRow ) {
				continue;
			}
			const float fX = m_state.tickToX( nPos );
			if ( fX < fFrom || fX > fTo ) {
				continue;
			}
			auto spec = PatternActions::NoteSpec::from( pNote );
			if ( m_original.find( nPos ) == m_original.end() ) {
				m_original[ nPos ] = { spec, fVelocity };
			}
			else {
				m_original[ nPos ].second = fVelocity;
			}
			PatternActions::previewVelocity( m_state.nPattern, spec, fVelocity );
		}
	}

	PatternEditorState& m_state;
	bool m_bDragging;
	float m_fLastX = 0;
	/** Per position: the note as it was before the drag, and its new velocity. */
	std::map<int, std::pair<PatternActions::NoteSpec, float>> m_original;
};

// ---------------------------------------------------------------------------
// PatternEditor

PatternEditor::PatternEditor()
	: BView( "pattern editor", 0 )
	, m_bUpdatingHeader( false )
{
	SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
	m_state.reload();

	// Upstream: the pattern's name, centred above the grid.
	m_pPatternName = new BStringView( "pattern name", "" );
	m_pPatternName->SetFont( be_bold_font );
	m_pPatternName->SetAlignment( B_ALIGN_CENTER );
	m_pPatternName->SetExplicitMaxSize( BSize( B_SIZE_UNLIMITED, B_SIZE_UNSET ) );

	m_pNumerator = new BDecimalSpinner( "numerator", B_TRANSLATE( "Size:" ),
										new BMessage( kMsgSizeChanged ) );
	m_pNumerator->SetPrecision( 1 );
	m_pNumerator->SetMinValue( 0.1 );
	m_pNumerator->SetMaxValue( 16.0 );
	m_pNumerator->SetStep( 1.0 );
	m_pNumerator->SetToolTip( B_TRANSLATE( "Beats in the pattern" ) );
	m_pDenominator = new BSpinner( "denominator", "/", new BMessage( kMsgSizeChanged ) );
	m_pDenominator->SetMinValue( 1 );
	m_pDenominator->SetMaxValue( 192 );
	m_pDenominator->SetToolTip( B_TRANSLATE( "Beat length (4 is a quarter note)" ) );

	BPopUpMenu* pResolutionMenu = new BPopUpMenu( "resolution" );
	for ( size_t i = 0; i < sizeof( kResolutions ) / sizeof( kResolutions[ 0 ] ); i++ ) {
		if ( kResolutions[ i ].sLabel == nullptr ) {
			pResolutionMenu->AddSeparatorItem();
			continue;
		}
		BMessage* pMessage = new BMessage( kMsgResolution );
		pMessage->AddInt32( "index", (int32) i );
		const char* sLabel = strcmp( kResolutions[ i ].sLabel, "off" ) == 0
			? B_TRANSLATE( "Off" ) : kResolutions[ i ].sLabel;
		pResolutionMenu->AddItem( new BMenuItem( sLabel, pMessage ) );
	}
	m_pResolutionField = new BMenuField( "resolution", B_TRANSLATE( "Grid:" ), pResolutionMenu );
	m_pResolutionField->SetToolTip( B_TRANSLATE( "Note positions snap to this grid" ) );

	auto pKitLabel = new KitLabel( m_state );
	m_pInstrumentList = new InstrumentRows( m_state );
	m_pRuler = new PatternRuler( m_state );
	m_pGrid = new DrumGrid( this );
	m_pVelocity = new VelocityLane( m_state );
	m_pVScroll = new BScrollBar( "vertical", m_pGrid, 0, 0, B_VERTICAL );
	m_pHScroll = new BScrollBar( "horizontal", m_pGrid, 0, 0, B_HORIZONTAL );
	auto pVelocityLabel = new BStringView( "velocity label", B_TRANSLATE( "Velocity" ) );
	pVelocityLabel->SetAlignment( B_ALIGN_CENTER );
	pVelocityLabel->SetExplicitSize( BSize( PatternEditorState::kListWidth - 1,
											PatternEditorState::kVelocityHeight - 1 ) );

	BLayoutBuilder::Group<>( this, B_VERTICAL, 0 )
		.AddGroup( B_HORIZONTAL, B_USE_HALF_ITEM_SPACING )
			.SetInsets( B_USE_HALF_ITEM_INSETS, 2, B_USE_HALF_ITEM_INSETS, 2 )
			.Add( m_pPatternName )
			.Add( m_pNumerator )
			.Add( m_pDenominator )
			.AddStrut( B_USE_ITEM_SPACING )
			.Add( m_pResolutionField )
		.End()
		.AddGrid( 0.0f, 0.0f )
			.Add( pKitLabel, 0, 0 )
			.Add( m_pRuler, 1, 0 )
			.Add( m_pInstrumentList, 0, 1 )
			.Add( m_pGrid, 1, 1 )
			.Add( m_pVScroll, 2, 1 )
			.Add( pVelocityLabel, 0, 2 )
			.Add( m_pVelocity, 1, 2 )
			.Add( m_pHScroll, 1, 3 )
		.End();

	m_pNumerator->SetExplicitMaxSize( BSize( m_pNumerator->StringWidth( "Size: 00.0" ) + 60,
											 B_SIZE_UNSET ) );
	m_pDenominator->SetExplicitMaxSize( BSize( m_pDenominator->StringWidth( "/ 000" ) + 60,
											   B_SIZE_UNSET ) );
}

void PatternEditor::AttachedToWindow()
{
	BView::AttachedToWindow();
	m_pResolutionField->Menu()->SetTargetForItems( this );
	m_pNumerator->SetTarget( this );
	m_pDenominator->SetTarget( this );
	refresh();
}

void PatternEditor::updateHeader()
{
	m_bUpdatingHeader = true;

	m_pPatternName->SetText( m_state.pPattern != nullptr
		? toBString( m_state.pPattern->getName() ).String() : B_TRANSLATE( "No pattern" ) );

	const bool bHasPattern = m_state.pPattern != nullptr;
	m_pNumerator->SetEnabled( bHasPattern );
	m_pDenominator->SetEnabled( bHasPattern );
	if ( bHasPattern ) {
		m_pNumerator->SetValue( m_state.pPattern->numerator() );
		m_pDenominator->SetValue( m_state.pPattern->getDenominator() );
	}

	for ( size_t i = 0; i < sizeof( kResolutions ) / sizeof( kResolutions[ 0 ] ); i++ ) {
		if ( kResolutions[ i ].sLabel != nullptr &&
			 kResolutions[ i ].nResolution == m_state.nResolution &&
			 kResolutions[ i ].bTriplets == m_state.bTriplets ) {
			for ( int32 j = 0; j < m_pResolutionField->Menu()->CountItems(); j++ ) {
				BMenuItem* pItem = m_pResolutionField->Menu()->ItemAt( j );
				int32 nIndex = -1;
				if ( pItem->Message() != nullptr &&
					 pItem->Message()->FindInt32( "index", &nIndex ) == B_OK && nIndex == (int32) i ) {
					pItem->SetMarked( true );
				}
			}
		}
	}
	m_bUpdatingHeader = false;
}

void PatternEditor::refresh()
{
	m_state.reload();
	updateHeader();
	updateScrollRanges();
	for ( BView* pView : { (BView*) m_pInstrumentList, (BView*) m_pRuler, (BView*) m_pGrid,
						   (BView*) m_pVelocity } ) {
		pView->Invalidate();
	}
	if ( BView* pKitLabel = FindView( "kit label" ) ) {
		pKitLabel->Invalidate();
	}
}

void PatternEditor::redrawNotes()
{
	m_pGrid->Invalidate();
	m_pVelocity->Invalidate();
}

void PatternEditor::updatePlayhead()
{
	auto pAudioEngine = Hydrogen::get_instance()->getAudioEngine();
	long nTick = -1;
	if ( pAudioEngine->getState() == AudioEngine::State::Playing && m_state.pPattern != nullptr &&
		 pAudioEngine->getPlayingPatterns() != nullptr &&
		 pAudioEngine->getPlayingPatterns()->index( m_state.pPattern ) != -1 ) {
		nTick = pAudioEngine->getPlayhead()->getPatternTickPosition();
	}
	if ( nTick == m_state.nPlayheadTick ) {
		return;
	}
	// Repaint a strip around the old and the new position only.
	auto strip = [this]( long n ) {
		const float fX = m_state.tickToX( n );
		return BRect( fX - 3, -1e6, fX + 3, 1e6 );
	};
	BRect dirty;
	if ( m_state.nPlayheadTick >= 0 ) {
		dirty = strip( m_state.nPlayheadTick );
	}
	if ( nTick >= 0 ) {
		dirty = dirty.IsValid() ? dirty | strip( nTick ) : strip( nTick );
	}
	m_state.nPlayheadTick = nTick;
	for ( BView* pView : { (BView*) m_pRuler, (BView*) m_pGrid, (BView*) m_pVelocity } ) {
		BRect bounds = pView->Bounds();
		pView->Invalidate( BRect( dirty.left, bounds.top, dirty.right, bounds.bottom ) );
	}
}

void PatternEditor::gridScrolled( BPoint offset )
{
	m_pRuler->ScrollTo( BPoint( offset.x, 0 ) );
	m_pVelocity->ScrollTo( BPoint( offset.x, 0 ) );
	m_pInstrumentList->ScrollTo( BPoint( 0, offset.y ) );
}

void PatternEditor::updateScrollRanges()
{
	const BRect bounds = m_pGrid->Bounds();
	const float fWidth = m_state.width();
	const float fHeight = m_state.rows.size() * m_state.fRowHeight;
	m_pHScroll->SetRange( 0, std::max( 0.0f, fWidth - bounds.Width() ) );
	m_pHScroll->SetProportion( fWidth > 0 ? std::min( 1.0f, bounds.Width() / fWidth ) : 1 );
	m_pHScroll->SetSteps( m_state.fGridWidth * m_state.granularity(), bounds.Width() * 0.8f );
	m_pVScroll->SetRange( 0, std::max( 0.0f, fHeight - bounds.Height() ) );
	m_pVScroll->SetProportion( fHeight > 0 ? std::min( 1.0f, bounds.Height() / fHeight ) : 1 );
	m_pVScroll->SetSteps( m_state.fRowHeight, bounds.Height() * 0.8f );
}

void PatternEditor::zoom( bool bIn )
{
	auto pPref = Preferences::get_instance();
	unsigned nWidth = pPref->getPatternEditorGridWidth();
	nWidth = bIn ? std::min( 24u, nWidth * 2 ) : std::max( 1u, nWidth / 2 );
	pPref->setPatternEditorGridWidth( nWidth );
	refresh();
}

void PatternEditor::MessageReceived( BMessage* pMessage )
{
	switch ( pMessage->what ) {
	case kMsgSizeChanged: {
		if ( m_bUpdatingHeader || m_state.pPattern == nullptr ) {
			break;
		}
		const double fNumerator = m_pNumerator->Value();
		const int nDenominator = m_pDenominator->Value();
		// Upstream: length = 4 quarters / denominator × numerator, in ticks.
		const int nLength = (int) std::round( 4.0 * nTicksPerQuarter / nDenominator * fNumerator );
		if ( nLength != m_state.pPattern->getLength() ||
			 nDenominator != m_state.pPattern->getDenominator() ) {
			UndoStack::get().push(
				PatternActions::setPatternSize( m_state.nPattern, nLength, nDenominator ) );
		}
		break;
	}
	case kMsgResolution: {
		int32 nIndex;
		if ( pMessage->FindInt32( "index", &nIndex ) == B_OK ) {
			auto pPref = Preferences::get_instance();
			pPref->setPatternEditorGridResolution( kResolutions[ nIndex ].nResolution );
			pPref->setPatternEditorUsingTriplets( kResolutions[ nIndex ].bTriplets );
			refresh();
		}
		break;
	}
	case B_COLORS_UPDATED:
		Colors::invalidate();
		refresh();
		break;
	default:
		BView::MessageReceived( pMessage );
	}
}
