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

#include "SongEditor.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <Shape.h>
#include <TextControl.h>
#include <Window.h>

#include <core/AudioEngine/AudioEngine.h>
#include <core/AudioEngine/Transport.h>
#include <core/Basics/Pattern.h>
#include <core/Basics/PatternList.h>
#include <core/Basics/Song.h>
#include <core/CoreActionController.h>
#include <core/Globals.h>
#include <core/Hydrogen.h>
#include <core/Preferences/Preferences.h>

#include "Colors.h"
#include "SongActions.h"
#include "TransportBar.h"
#include "UndoStack.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SongEditor"

using namespace H2Core;

enum : uint32 {
	kMsgNewPattern = 'SEnp',
	kMsgDuplicatePattern = 'SEdp',
	kMsgRenamePattern = 'SErp',
	kMsgDoRename = 'SErn',
	kMsgDeletePattern = 'SEdl',
	kMsgClearSequence = 'SEcl',
	kMsgSelectedMode = 'SEsm',
	kMsgStackedMode = 'SEst',
	kMsgLock = 'SElk',
};

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

static std::shared_ptr<PatternList> patterns()
{
	auto pSong = Hydrogen::get_instance()->getSong();
	return pSong != nullptr ? pSong->getPatternList() : nullptr;
}

// ---------------------------------------------------------------------------
// SongEditorState

void SongEditorState::reload()
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pPref = Preferences::get_instance();
	auto pSong = pHydrogen->getSong();
	fGridWidth = std::clamp( (float) pPref->getSongEditorGridWidth(), 8.0f, 16.0f );
	fRowHeight = std::max( 12u, pPref->getSongEditorGridHeight() );
	nMaxBars = std::max( 1, pPref->getMaxBars() );
	nPatterns = pSong != nullptr ? pSong->getPatternList()->size() : 0;
	nColumns = pSong != nullptr ? (int) pSong->getPatternGroupVector()->size() : 0;
	nSelected = pHydrogen->getSelectedPatternNumber();
}

int SongEditorState::xToColumn( float fX ) const
{
	if ( fX < kMargin ) {
		return -1;
	}
	return std::min( nMaxBars - 1, (int) ( ( fX - kMargin ) / fGridWidth ) );
}

/** Which patterns play now and which are queued, read under the engine
 * lock once per poll so drawing never has to take it. */
struct PlayState {
	std::vector<char> active;
	std::vector<char> next;
	bool bStackedPatternMode = false;

	bool operator==( const PlayState& o ) const
	{
		return active == o.active && next == o.next && bStackedPatternMode == o.bStackedPatternMode;
	}
};

static PlayState sPlayState;

static PlayState readPlayState()
{
	PlayState state;
	auto pHydrogen = Hydrogen::get_instance();
	auto pPatterns = patterns();
	if ( pPatterns == nullptr ) {
		return state;
	}
	state.active.resize( pPatterns->size() );
	state.next.resize( pPatterns->size() );
	state.bStackedPatternMode = pHydrogen->getMode() == Song::Mode::Pattern &&
		pHydrogen->getPatternMode() == Song::PatternMode::Stacked;
	auto pAudioEngine = pHydrogen->getAudioEngine();
	pAudioEngine->lock( RIGHT_HERE );
	for ( int i = 0; i < pPatterns->size(); i++ ) {
		auto pPattern = pPatterns->get( i );
		state.active[ i ] = pAudioEngine->getPlayingPatterns()->index( pPattern ) != -1;
		state.next[ i ] = pAudioEngine->getNextPatterns()->index( pPattern ) != -1;
	}
	pAudioEngine->unlock();
	return state;
}

static void drawPlayheadLine( BView* pView, const SongEditorState& state, float fTop, float fBottom )
{
	if ( state.fPlayhead < 0 ) {
		return;
	}
	const float fX = state.columnToX( 0 ) + 1 + state.fPlayhead * state.fGridWidth;
	pView->SetHighColor( Colors::get().playhead );
	pView->SetPenSize( 2 );
	pView->StrokeLine( BPoint( fX, fTop ), BPoint( fX, fBottom ) );
	pView->SetPenSize( 1 );
}

// ---------------------------------------------------------------------------
// RenameWindow: asks for a pattern's new name.

class RenameWindow : public BWindow
{
public:
	RenameWindow( BMessenger target, int nRow, const BString& sName, BRect parentFrame )
		: BWindow( BRect( 0, 0, 300, 80 ), B_TRANSLATE( "Rename pattern" ), B_FLOATING_WINDOW_LOOK,
				   B_FLOATING_APP_WINDOW_FEEL,
				   B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE )
		, m_target( target ), m_nRow( nRow )
	{
		m_pName = new BTextControl( "name", B_TRANSLATE( "Name:" ), sName, nullptr );
		BButton* pCancel = new BButton( "cancel", B_TRANSLATE( "Cancel" ), new BMessage( B_QUIT_REQUESTED ) );
		BButton* pRename = new BButton( "rename", B_TRANSLATE( "Rename" ), new BMessage( kMsgDoRename ) );
		BLayoutBuilder::Group<>( this, B_VERTICAL )
			.SetInsets( B_USE_WINDOW_INSETS )
			.Add( m_pName )
			.AddGroup( B_HORIZONTAL )
				.AddGlue()
				.Add( pCancel )
				.Add( pRename )
			.End();
		SetDefaultButton( pRename );
		m_pName->MakeFocus( true );
		m_pName->TextView()->SelectAll();
		CenterIn( parentFrame );
	}

	void MessageReceived( BMessage* pMessage ) override
	{
		if ( pMessage->what == kMsgDoRename ) {
			BMessage rename( kMsgDoRename );
			rename.AddInt32( "row", m_nRow );
			rename.AddString( "name", m_pName->Text() );
			m_target.SendMessage( &rename );
			Quit();
			return;
		}
		BWindow::MessageReceived( pMessage );
	}

private:
	BMessenger m_target;
	int m_nRow;
	BTextControl* m_pName;
};

// ---------------------------------------------------------------------------
// PatternListView: pattern names with play indicators (upstream's
// SongEditorPatternList).

class PatternListView : public BView
{
public:
	PatternListView( SongEditor* pEditor )
		: BView( "pattern list", B_WILL_DRAW ), m_pEditor( pEditor ), m_state( pEditor->state() )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitMinSize( BSize( SongEditorState::kListWidth - 1, 30 ) );
		SetExplicitMaxSize( BSize( SongEditorState::kListWidth - 1, B_SIZE_UNLIMITED ) );
	}

	void Draw( BRect updateRect ) override
	{
		const EditorColors& c = Colors::get();
		const float h = m_state.fRowHeight;
		const float w = Bounds().Width();
		auto pPatterns = patterns();
		BFont font( be_bold_font );
		SetFont( &font );
		font_height fh;
		font.GetHeight( &fh );

		for ( int i = std::max( 0, (int) ( updateRect.top / h ) );
			  i < m_state.nPatterns && i * h <= updateRect.bottom; i++ ) {
			auto pPattern = pPatterns->get( i );
			const bool bSelected = i == m_state.nSelected;
			rgb_color bg = bSelected ? Colors::darker( c.songSelectedRow, 114 )
				: pPattern->isVirtual() ? c.songVirtualRow
				: ( i % 2 ? Colors::darker( c.songAlternateRow, 132 )
						  : Colors::darker( c.songBackground, 120 ) );
			BRect row( 0, i * h, w, ( i + 1 ) * h - 1 );
			SetHighColor( bg );
			FillRect( row );
			SetHighColor( bSelected ? c.highlight : c.window );
			StrokeLine( row.LeftTop(), row.RightTop() );
			if ( bSelected ) {
				StrokeLine( row.LeftBottom(), row.RightBottom() );
				StrokeLine( row.LeftTop(), row.LeftBottom() );
				StrokeLine( row.RightTop(), row.RightBottom() );
			}

			BString sName = toBString( pPattern->getName() );
			if ( pPattern->getIsModified() ) {
				sName << "*";
			}
			font.TruncateString( &sName, B_TRUNCATE_END, w - 30 );
			SetHighColor( bSelected ? c.songSelectedText : c.songText );
			SetLowColor( bg );
			DrawString( sName, BPoint( 25, row.top + ( h + fh.ascent - fh.descent ) / 2 ) );

			drawIndicator( row, i );
		}
		const float fBottom = m_state.nPatterns * h;
		if ( updateRect.bottom >= fBottom ) {
			SetHighColor( c.window );
			FillRect( BRect( 0, fBottom, w, updateRect.bottom ) );
		}
	}

	void MouseDown( BPoint where ) override
	{
		const int nRow = (int) ( where.y / m_state.fRowHeight );
		if ( nRow < 0 || nRow >= m_state.nPatterns ) {
			return;
		}
		int32 nButtons = B_PRIMARY_MOUSE_BUTTON, nClicks = 1;
		if ( BMessage* pCurrent = Window()->CurrentMessage() ) {
			pCurrent->FindInt32( "buttons", &nButtons );
			pCurrent->FindInt32( "clicks", &nClicks );
		}
		auto pHydrogen = Hydrogen::get_instance();
		const bool bStacked = pHydrogen->getMode() == Song::Mode::Pattern &&
			pHydrogen->getPatternMode() == Song::PatternMode::Stacked;

		if ( nButtons & B_SECONDARY_MOUSE_BUTTON ) {
			CoreActionController::selectPattern( nRow );
			showContextMenu( where, nRow );
		}
		else if ( bStacked && ( where.x < 15 || ( modifiers() & B_COMMAND_KEY ) ) ) {
			// The triangle (or Command+click) queues the pattern to play next.
			pHydrogen->toggleNextPattern( nRow );
		}
		else if ( nClicks == 2 ) {
			m_pEditor->renamePattern( nRow );
		}
		else {
			CoreActionController::selectPattern( nRow );
		}
	}

private:
	/** Upstream's stacked-mode triangle: playing, queued, or both. */
	void drawIndicator( BRect row, int nRow )
	{
		if ( nRow >= (int) sPlayState.active.size() ) {
			return;
		}
		const EditorColors& c = Colors::get();
		const bool bActive = sPlayState.active[ nRow ];
		const bool bNext = sPlayState.next[ nRow ];
		const rgb_color* pFill = nullptr;
		if ( bNext && bActive ) {
			pFill = &c.stackedOffNext;
		}
		else if ( bNext ) {
			pFill = &c.stackedOnNext;
		}
		else if ( bActive ) {
			pFill = &c.stackedOn;
		}
		else if ( ! sPlayState.bStackedPatternMode ) {
			return;
		}
		const float fTop = row.top + std::floor( ( row.Height() - 12 ) / 2 );
		BShape triangle;
		triangle.MoveTo( BPoint( 5, fTop ) );
		triangle.LineTo( BPoint( 13, fTop + 6 ) );
		triangle.LineTo( BPoint( 5, fTop + 12 ) );
		triangle.Close();
		MovePenTo( B_ORIGIN );
		if ( pFill != nullptr ) {
			SetHighColor( *pFill );
			FillShape( &triangle );
		}
		SetHighColor( 0, 0, 0 );
		StrokeShape( &triangle );
	}

	void showContextMenu( BPoint where, int nRow )
	{
		BPopUpMenu* pMenu = new BPopUpMenu( "pattern", false, false );
		pMenu->SetAsyncAutoDestruct( true );
		pMenu->AddItem( new BMenuItem( B_TRANSLATE( "New pattern" ), new BMessage( kMsgNewPattern ) ) );
		auto rowMessage = [nRow]( uint32 what ) {
			BMessage* pMessage = new BMessage( what );
			pMessage->AddInt32( "row", nRow );
			return pMessage;
		};
		pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Duplicate" ), rowMessage( kMsgDuplicatePattern ) ) );
		pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Rename" B_UTF8_ELLIPSIS ),
									   rowMessage( kMsgRenamePattern ) ) );
		pMenu->AddSeparatorItem();
		pMenu->AddItem( new BMenuItem( B_TRANSLATE( "Delete" ), rowMessage( kMsgDeletePattern ) ) );
		pMenu->SetTargetForItems( m_pEditor );
		pMenu->Go( ConvertToScreen( where ), true, false, true );
	}

	SongEditor* m_pEditor;
	SongEditorState& m_state;
};

// ---------------------------------------------------------------------------
// SongRuler: bar numbers; click or drag to move the playhead there.

class SongRuler : public BView
{
public:
	SongRuler( SongEditorState& state )
		: BView( "song ruler", B_WILL_DRAW ), m_state( state ), m_bDragging( false )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitMinSize( BSize( 100, SongEditorState::kRulerHeight - 1 ) );
		SetExplicitMaxSize( BSize( B_SIZE_UNLIMITED, SongEditorState::kRulerHeight - 1 ) );
	}

	void Draw( BRect updateRect ) override
	{
		const EditorColors& c = Colors::get();
		const float h = Bounds().Height();
		const float fEnd = m_state.columnToX( m_state.nColumns ) + 1;
		SetHighColor( Colors::darker( c.songAlternateRow, 115 ) );
		FillRect( BRect( updateRect.left, 0, std::min( updateRect.right, fEnd ), h ) );
		if ( updateRect.right > fEnd ) {
			SetHighColor( c.midLight );
			FillRect( BRect( fEnd, 0, updateRect.right, h ) );
		}
		SetHighColor( 35, 39, 51 );
		StrokeLine( BPoint( updateRect.left, h ), BPoint( updateRect.right, h ) );

		SetHighColor( c.songText );
		SetDrawingMode( B_OP_OVER );
		const int nFirst = std::max( 0, m_state.xToColumn( updateRect.left - 40 ) );
		const int nLast = std::min( m_state.nMaxBars, m_state.xToColumn( updateRect.right ) + 1 );
		for ( int i = nFirst; i <= nLast; i++ ) {
			const float fX = m_state.columnToX( i );
			StrokeLine( BPoint( fX, h - ( i % 4 == 0 ? 10 : 4 ) ), BPoint( fX, h ) );
			if ( i % 4 == 0 && i < m_state.nMaxBars ) {
				BString sBar;
				sBar << i + 1;
				DrawString( sBar, BPoint( fX + 2, h - 11 ) );
			}
		}
		SetDrawingMode( B_OP_COPY );

		// Playhead: a small triangle on the ruler plus its shaft.
		if ( m_state.fPlayhead >= 0 ) {
			const float fX = m_state.columnToX( 0 ) + 1 + m_state.fPlayhead * m_state.fGridWidth;
			BShape head;
			head.MoveTo( BPoint( fX - 5, h - 9 ) );
			head.LineTo( BPoint( fX + 5, h - 9 ) );
			head.LineTo( BPoint( fX, h - 1 ) );
			head.Close();
			MovePenTo( B_ORIGIN );
			SetHighColor( c.playhead );
			FillShape( &head );
		}
	}

	void MouseDown( BPoint where ) override
	{
		m_bDragging = true;
		SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS );
		locate( where );
	}

	void MouseMoved( BPoint where, uint32, const BMessage* ) override
	{
		if ( m_bDragging ) {
			locate( where );
		}
	}

	void MouseUp( BPoint ) override
	{
		m_bDragging = false;
	}

private:
	/** Like upstream: clicking the ruler in pattern mode switches to song mode. */
	void locate( BPoint where )
	{
		const int nColumn = m_state.xToColumn( where.x );
		if ( nColumn < 0 || nColumn > m_state.nColumns ) {
			return;
		}
		auto pHydrogen = Hydrogen::get_instance();
		if ( pHydrogen->getMode() == Song::Mode::Pattern ) {
			CoreActionController::activateSongMode( true );
		}
		CoreActionController::locateToColumn( nColumn );
	}

	SongEditorState& m_state;
	bool m_bDragging;
};

// ---------------------------------------------------------------------------
// SongGrid: the arrangement. Click toggles a cell; dragging toggles every
// cell the pointer crosses, as one undo step.

class SongGrid : public BView
{
public:
	SongGrid( SongEditor* pEditor )
		: BView( "song grid", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE )
		, m_pEditor( pEditor ), m_state( pEditor->state() )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitMinSize( BSize( 200, 40 ) );
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
		const float fRight = std::min( updateRect.right, m_state.width() );

		for ( int i = std::max( 0, (int) ( updateRect.top / h ) );
			  i < m_state.nPatterns && i * h <= updateRect.bottom; i++ ) {
			SetHighColor( i == m_state.nSelected ? c.songSelectedRow
						  : ( i % 2 ? c.songAlternateRow : c.songBackground ) );
			FillRect( BRect( updateRect.left, i * h, fRight, ( i + 1 ) * h - 1 ) );
		}
		SetHighColor( c.window );
		const float fBottom = m_state.nPatterns * h;
		if ( updateRect.bottom >= fBottom ) {
			FillRect( BRect( updateRect.left, fBottom, updateRect.right, updateRect.bottom ) );
		}
		if ( updateRect.right > m_state.width() ) {
			FillRect( BRect( m_state.width(), updateRect.top, updateRect.right, updateRect.bottom ) );
		}

		// Dotted lines at every bar and every row top.
		const pattern dotted = { { 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 } };
		SetHighColor( c.songLine );
		SetLowColor( c.songBackground );
		const int nFirst = std::max( 0, m_state.xToColumn( updateRect.left ) );
		const int nLast = std::min( m_state.nMaxBars, m_state.xToColumn( updateRect.right ) + 1 );
		for ( int i = nFirst; i <= nLast; i++ ) {
			const float fX = m_state.columnToX( i );
			StrokeLine( BPoint( fX, std::max( 0.0f, updateRect.top ) ),
						BPoint( fX, std::min( fBottom, updateRect.bottom ) ), dotted );
		}
		for ( int i = std::max( 0, (int) ( updateRect.top / h ) );
			  i <= m_state.nPatterns && i * h <= updateRect.bottom; i++ ) {
			StrokeLine( BPoint( updateRect.left, i * h ), BPoint( fRight, i * h ), dotted );
		}

		drawCells( updateRect );
		drawPlayheadLine( this, m_state, updateRect.top, std::min( fBottom, updateRect.bottom ) );
	}

	void MouseDown( BPoint where ) override
	{
		MakeFocus( true );
		int32 nButtons = B_PRIMARY_MOUSE_BUTTON;
		if ( BMessage* pCurrent = Window()->CurrentMessage() ) {
			pCurrent->FindInt32( "buttons", &nButtons );
		}
		if ( ! ( nButtons & B_PRIMARY_MOUSE_BUTTON ) ) {
			return;
		}
		m_pStroke = std::make_unique<MacroCommand>( B_TRANSLATE( "toggle cells" ) );
		m_last = BPoint( -1, -1 );
		m_lastCell = { -1, -1 };
		SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS );
		strokeTo( where );
	}

	void MouseMoved( BPoint where, uint32, const BMessage* ) override
	{
		if ( m_pStroke != nullptr ) {
			strokeTo( where );
		}
	}

	void MouseUp( BPoint ) override
	{
		if ( m_pStroke == nullptr ) {
			return;
		}
		// The cells are already toggled: record the stroke without redoing it.
		if ( ! m_pStroke->isEmpty() ) {
			UndoStack::get().record( std::move( m_pStroke ) );
		}
		m_pStroke = nullptr;
		Window()->PostMessage( 'Hped' );	// refresh the Edit menu's undo label
	}

	void MessageReceived( BMessage* pMessage ) override
	{
		// Command+wheel zooms (HIG); a plain wheel scrolls.
		if ( pMessage->what == B_MOUSE_WHEEL_CHANGED && ( modifiers() & B_COMMAND_KEY ) ) {
			float fDeltaY = 0;
			pMessage->FindFloat( "be:wheel_delta_y", &fDeltaY );
			m_pEditor->zoom( fDeltaY < 0 );
			return;
		}
		BView::MessageReceived( pMessage );
	}

private:
	/** Toggles each new cell along the line from the last pointer position. */
	void strokeTo( BPoint where )
	{
		const BPoint from = m_last.x < 0 ? where : m_last;
		m_last = where;
		const int nSteps = std::max( 1, (int) std::ceil( std::max( std::abs( where.x - from.x ),
																	  std::abs( where.y - from.y ) ) ) );
		for ( int i = 0; i <= nSteps; i++ ) {
			const BPoint p( from.x + ( where.x - from.x ) * i / nSteps,
							from.y + ( where.y - from.y ) * i / nSteps );
			const int nColumn = m_state.xToColumn( p.x );
			const int nRow = (int) std::floor( p.y / m_state.fRowHeight );
			if ( nColumn < 0 || nRow < 0 || nRow >= m_state.nPatterns ||
				 std::make_pair( nColumn, nRow ) == m_lastCell ) {
				continue;
			}
			m_lastCell = { nColumn, nRow };
			auto pCommand = SongActions::toggleCell( nColumn, nRow );
			pCommand->redo();
			m_pStroke->add( std::move( pCommand ) );
		}
	}

	void drawCells( BRect updateRect )
	{
		auto pSong = Hydrogen::get_instance()->getSong();
		if ( pSong == nullptr ) {
			return;
		}
		const float h = m_state.fRowHeight;
		auto pColumns = pSong->getPatternGroupVector();
		auto pPatterns = pSong->getPatternList();
		const int nFirst = std::max( 0, m_state.xToColumn( updateRect.left ) );
		const int nLast = std::min( (int) pColumns->size() - 1, m_state.xToColumn( updateRect.right ) );
		for ( int nColumn = nFirst; nColumn <= nLast; nColumn++ ) {
			auto pColumn = ( *pColumns )[ nColumn ];
			const int nLongest = std::max( 1, pColumn->longestPatternLength() );
			const float fX = m_state.columnToX( nColumn );
			for ( int j = 0; j < pColumn->size(); j++ ) {
				auto pPattern = pColumn->get( j );
				const int nRow = pPatterns->index( pPattern );
				if ( nRow < 0 ) {
					continue;
				}
				// Shorter patterns fill part of the bar, like upstream.
				const float fWidth = m_state.fGridWidth * pPattern->getLength() / nLongest;
				BRect cell( fX, nRow * h, fX + fWidth, ( nRow + 1 ) * h );
				SetHighColor( Colors::patternColor( nRow, m_state.nPatterns ) );
				FillRect( cell );
				SetHighColor( 0, 0, 0 );
				StrokeRect( cell );
			}
		}
	}

	SongEditor* m_pEditor;
	SongEditorState& m_state;
	std::unique_ptr<MacroCommand> m_pStroke;
	BPoint m_last;
	std::pair<int, int> m_lastCell;
};

// ---------------------------------------------------------------------------
// SongEditor

SongEditor::SongEditor()
	: BView( "song editor", 0 )
{
	SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
	m_state.reload();

	const float fButton = SongEditorState::kRulerHeight - 2;
	m_pNewButton = new GlyphButton( "new pattern", GlyphButton::Glyph::Add,
									new BMessage( kMsgNewPattern ), false, fButton );
	m_pNewButton->SetToolTip( B_TRANSLATE( "New pattern" ) );
	m_pClearButton = new GlyphButton( "clear", GlyphButton::Glyph::Clear,
									  new BMessage( kMsgClearSequence ), false, fButton );
	m_pClearButton->SetToolTip( B_TRANSLATE( "Clear pattern sequence" ) );
	m_pSingleButton = new GlyphButton( "selected mode", GlyphButton::Glyph::SinglePattern,
									   new BMessage( kMsgSelectedMode ), true, fButton );
	m_pSingleButton->SetToolTip( B_TRANSLATE( "Play the selected pattern" ) );
	m_pStackedButton = new GlyphButton( "stacked mode", GlyphButton::Glyph::StackedPatterns,
										new BMessage( kMsgStackedMode ), true, fButton );
	m_pStackedButton->SetToolTip( B_TRANSLATE( "Play several patterns together "
											   "(click their triangles)" ) );
	m_pLockButton = new GlyphButton( "lock", GlyphButton::Glyph::Lock,
									 new BMessage( kMsgLock ), true, fButton );
	m_pLockButton->SetToolTip( B_TRANSLATE( "Pattern editor follows playback" ) );

	m_pPatternList = new PatternListView( this );
	m_pRuler = new SongRuler( m_state );
	m_pGrid = new SongGrid( this );
	m_pVScroll = new BScrollBar( "song vertical", m_pGrid, 0, 0, B_VERTICAL );
	m_pHScroll = new BScrollBar( "song horizontal", m_pGrid, 0, 0, B_HORIZONTAL );

	BGroupView* pToolbar = new BGroupView( B_HORIZONTAL, 2 );
	BLayoutBuilder::Group<>( pToolbar )
		.SetInsets( 4, 1, 4, 1 )
		.Add( m_pNewButton )
		.Add( m_pClearButton )
		.AddStrut( 6 )
		.Add( m_pSingleButton )
		.Add( m_pStackedButton )
		.AddGlue()
		.Add( m_pLockButton );
	pToolbar->SetExplicitSize( BSize( SongEditorState::kListWidth - 1, SongEditorState::kRulerHeight - 1 ) );

	BLayoutBuilder::Grid<>( this, 0.0f, 0.0f )
		.Add( pToolbar, 0, 0 )
		.Add( m_pRuler, 1, 0 )
		.Add( m_pPatternList, 0, 1 )
		.Add( m_pGrid, 1, 1 )
		.Add( m_pVScroll, 2, 1 )
		.Add( m_pHScroll, 1, 2 );
}

void SongEditor::AttachedToWindow()
{
	BView::AttachedToWindow();
	for ( BControl* pControl : { (BControl*) m_pNewButton, (BControl*) m_pClearButton,
			 (BControl*) m_pSingleButton, (BControl*) m_pStackedButton, (BControl*) m_pLockButton } ) {
		pControl->SetTarget( this );
	}
	refresh();
}

void SongEditor::updateToolbar()
{
	auto pHydrogen = Hydrogen::get_instance();
	const bool bSongMode = pHydrogen->getMode() == Song::Mode::Song;
	const bool bStacked = pHydrogen->getPatternMode() == Song::PatternMode::Stacked;
	m_pSingleButton->SetValue( bStacked ? B_CONTROL_OFF : B_CONTROL_ON );
	m_pStackedButton->SetValue( bStacked ? B_CONTROL_ON : B_CONTROL_OFF );
	m_pSingleButton->SetEnabled( ! bSongMode );
	m_pStackedButton->SetEnabled( ! bSongMode );
	m_pLockButton->SetValue( pHydrogen->isPatternEditorLocked() ? B_CONTROL_ON : B_CONTROL_OFF );
	m_pLockButton->SetEnabled( bSongMode );
	m_pLockButton->Invalidate();
}

void SongEditor::refresh()
{
	m_state.reload();
	sPlayState = readPlayState();
	updateToolbar();
	updateScrollRanges();
	m_pPatternList->Invalidate();
	m_pRuler->Invalidate();
	m_pGrid->Invalidate();
}

void SongEditor::updatePlayhead()
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pSong = pHydrogen->getSong();
	auto pAudioEngine = pHydrogen->getAudioEngine();
	float fPlayhead = -1;
	if ( pSong != nullptr && pHydrogen->getMode() == Song::Mode::Song ) {
		// Upstream's SongEditorPositionRuler::updatePosition().
		pAudioEngine->lock( RIGHT_HERE );
		auto pPlayhead = pAudioEngine->getPlayhead();
		const int nColumn = pPlayhead->getColumn();
		auto pColumns = pSong->getPatternGroupVector();
		if ( nColumn == -1 ) {
			fPlayhead = pAudioEngine->isEndOfSongReached( pPlayhead ) ? pColumns->size() : 0;
		}
		else {
			int nLength = 4 * nTicksPerQuarter;
			if ( nColumn < (int) pColumns->size() && ( *pColumns )[ nColumn ]->size() > 0 ) {
				nLength = std::max( 1, ( *pColumns )[ nColumn ]->longestPatternLength() );
			}
			fPlayhead = nColumn + (float) pPlayhead->getPatternTickPosition() / nLength;
		}
		pAudioEngine->unlock();
		fPlayhead = std::max( 0.0f, fPlayhead );
	}

	const PlayState playState = readPlayState();
	if ( ! ( playState == sPlayState ) ) {
		sPlayState = playState;
		m_pPatternList->Invalidate();
	}

	if ( std::abs( fPlayhead - m_state.fPlayhead ) < 0.001f ) {
		return;
	}
	auto strip = [this]( float f ) {
		const float fX = m_state.columnToX( 0 ) + 1 + f * m_state.fGridWidth;
		return std::make_pair( fX - 7, fX + 7 );
	};
	for ( float f : { m_state.fPlayhead, fPlayhead } ) {
		if ( f < 0 ) {
			continue;
		}
		const auto [ fLeft, fRight ] = strip( f );
		m_pRuler->Invalidate( BRect( fLeft, 0, fRight, m_pRuler->Bounds().bottom ) );
		m_pGrid->Invalidate( BRect( fLeft, m_pGrid->Bounds().top, fRight, m_pGrid->Bounds().bottom ) );
	}
	m_state.fPlayhead = fPlayhead;
}

void SongEditor::gridScrolled( BPoint offset )
{
	m_pRuler->ScrollTo( BPoint( offset.x, 0 ) );
	m_pPatternList->ScrollTo( BPoint( 0, offset.y ) );
}

void SongEditor::updateScrollRanges()
{
	const BRect bounds = m_pGrid->Bounds();
	const float fWidth = m_state.width();
	const float fHeight = m_state.nPatterns * m_state.fRowHeight;
	m_pHScroll->SetRange( 0, std::max( 0.0f, fWidth - bounds.Width() ) );
	m_pHScroll->SetProportion( std::min( 1.0f, bounds.Width() / std::max( 1.0f, fWidth ) ) );
	m_pHScroll->SetSteps( m_state.fGridWidth, bounds.Width() * 0.8f );
	m_pVScroll->SetRange( 0, std::max( 0.0f, fHeight - bounds.Height() ) );
	m_pVScroll->SetProportion( std::min( 1.0f, bounds.Height() / std::max( 1.0f, fHeight ) ) );
	m_pVScroll->SetSteps( m_state.fRowHeight, bounds.Height() * 0.8f );
}

void SongEditor::zoom( bool bIn )
{
	auto pPref = Preferences::get_instance();
	const unsigned nWidth = std::clamp( pPref->getSongEditorGridWidth() + ( bIn ? 1 : -1 ), 8u, 16u );
	pPref->setSongEditorGridWidth( nWidth );
	refresh();
}

void SongEditor::renamePattern( int nRow )
{
	auto pPatterns = patterns();
	if ( pPatterns == nullptr || nRow < 0 || nRow >= pPatterns->size() ) {
		return;
	}
	( new RenameWindow( BMessenger( this ), nRow, toBString( pPatterns->get( nRow )->getName() ),
						Window()->Frame() ) )->Show();
}

void SongEditor::MessageReceived( BMessage* pMessage )
{
	auto push = []( std::unique_ptr<UndoCommand> pCommand ) {
		if ( pCommand != nullptr ) {
			UndoStack::get().push( std::move( pCommand ) );
		}
	};
	int32 nRow = -1;
	pMessage->FindInt32( "row", &nRow );
	auto pHydrogen = Hydrogen::get_instance();

	switch ( pMessage->what ) {
	case kMsgNewPattern:
		push( SongActions::newPattern() );
		break;
	case kMsgDuplicatePattern:
		push( SongActions::duplicatePattern( nRow ) );
		break;
	case kMsgDeletePattern:
		push( SongActions::deletePattern( nRow ) );
		break;
	case kMsgRenamePattern:
		renamePattern( nRow );
		break;
	case kMsgDoRename: {
		BString sName;
		if ( pMessage->FindString( "name", &sName ) == B_OK && ! sName.Trim().IsEmpty() ) {
			push( SongActions::renamePattern( nRow, sName ) );
		}
		break;
	}
	case kMsgClearSequence:
		// Undoable, so no confirmation (HIG).
		push( SongActions::clearSequence() );
		break;
	case kMsgSelectedMode:
	case kMsgStackedMode:
		pHydrogen->setPatternMode( pMessage->what == kMsgStackedMode ? Song::PatternMode::Stacked
																	 : Song::PatternMode::Selected );
		refresh();
		break;
	case kMsgLock:
		pHydrogen->setIsPatternEditorLocked( ! pHydrogen->isPatternEditorLocked() );
		updateToolbar();
		break;
	case B_COLORS_UPDATED:
		Colors::invalidate();
		refresh();
		break;
	default:
		BView::MessageReceived( pMessage );
		return;
	}
	// Refresh the Edit menu's undo label.
	Window()->PostMessage( 'Hped' );
}
