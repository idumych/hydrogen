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

#ifndef HAIKU_SONG_EDITOR_H
#define HAIKU_SONG_EDITOR_H

#include <View.h>

class BScrollBar;
class GlyphButton;
class PatternListView;
class SongGrid;
class SongRuler;

/** What the song editor's views share. Geometry follows upstream: a
 * 10 px margin before bar 0, `gridWidth` px per bar (16, zoomable from
 * 8 to 16), `rowHeight` px per pattern row (18), maxBars columns. */
struct SongEditorState {
	static constexpr float kMargin = 10;
	static constexpr float kListWidth = 200;
	static constexpr float kRulerHeight = 25;

	float fGridWidth = 16;
	float fRowHeight = 18;
	int nMaxBars = 400;
	int nPatterns = 0;
	int nColumns = 0;			///< bars in the song
	int nSelected = -1;
	/** Playhead in bars (fractional), or -1 when not shown. */
	float fPlayhead = -1;

	void reload();
	float columnToX( int nColumn ) const { return kMargin + nColumn * fGridWidth; }
	/** Bar under @a fX, or -1 left of the grid. */
	int xToColumn( float fX ) const;
	float width() const { return kMargin + nMaxBars * fGridWidth + 1; }
};

/** The song editor: toolbar | ruler, then pattern list | arrangement
 * grid, scrolling together (see REFERENCE.md). */
class SongEditor : public BView
{
public:
	SongEditor();

	void AttachedToWindow() override;
	void MessageReceived( BMessage* pMessage ) override;

	/** Patterns, cells, selection or modes changed: reload and redraw. */
	void refresh();
	/** Called every poll tick; moves the playhead in song mode. */
	void updatePlayhead();
	void gridScrolled( BPoint offset );
	void updateScrollRanges();
	void zoom( bool bIn );
	/** Opens the rename window for pattern @a nRow. */
	void renamePattern( int nRow );

	SongEditorState& state() { return m_state; }

private:
	void updateToolbar();

	SongEditorState m_state;
	GlyphButton* m_pNewButton;
	GlyphButton* m_pClearButton;
	GlyphButton* m_pSingleButton;
	GlyphButton* m_pStackedButton;
	GlyphButton* m_pLockButton;
	PatternListView* m_pPatternList;
	SongRuler* m_pRuler;
	SongGrid* m_pGrid;
	BScrollBar* m_pHScroll;
	BScrollBar* m_pVScroll;
};

#endif
