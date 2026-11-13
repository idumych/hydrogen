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

#ifndef HAIKU_PATTERN_EDITOR_H
#define HAIKU_PATTERN_EDITOR_H

#include <memory>
#include <vector>

#include <View.h>

namespace H2Core {
	class Instrument;
	class Note;
	class Pattern;
};

class BDecimalSpinner;
class BMenuField;
class BPopUpMenu;
class BScrollBar;
class BSpinner;
class DrumGrid;
class InstrumentRows;
class PatternRuler;
class VelocityLane;

/** What the pattern editor's views share: the pattern being edited, its
 * rows (one per kit instrument) and the geometry. Geometry follows
 * upstream: a 42 px margin before tick 0, `gridWidth` px per tick (3 by
 * default, 144 px per beat) and `rowHeight` px per row (21). */
struct PatternEditorState {
	static constexpr float kMargin = 42;
	static constexpr float kRulerHeight = 25;
	static constexpr float kVelocityHeight = 100;
	static constexpr float kListWidth = 200;

	int nPattern = -1;
	std::shared_ptr<H2Core::Pattern> pPattern;
	std::vector<std::shared_ptr<H2Core::Instrument>> rows;
	int nSelectedRow = -1;
	float fGridWidth = 3;
	float fRowHeight = 21;
	int nResolution = 8;
	bool bTriplets = false;
	bool bQuantize = true;
	long nPlayheadTick = -1;

	/** Reloads pattern, rows and settings from the core. */
	void reload();
	int length() const;
	/** Ticks between grid positions at the current resolution. */
	int granularity() const;
	float tickToX( int nTick ) const { return kMargin + nTick * fGridWidth; }
	/** Nearest allowed tick for @a fX (quantized if enabled), or -1. */
	int xToTick( float fX ) const;
	float width() const;
	bool rowPlays( int nRow ) const;
};

/** The pattern editor panel: header (pattern, size, resolution), then
 * instrument list | ruler + note grid + velocity lane, scrolling
 * together. See REFERENCE.md for the measurements it follows. */
class PatternEditor : public BView
{
public:
	PatternEditor();

	void AttachedToWindow() override;
	void MessageReceived( BMessage* pMessage ) override;

	/** Pattern, kit or selection changed: reload and redraw all. */
	void refresh();
	/** Notes changed: redraw the canvases only. */
	void redrawNotes();
	/** Called every poll tick; moves the playhead while playing. */
	void updatePlayhead();
	/** Keeps list, ruler and lane aligned with the grid's scroll offset. */
	void gridScrolled( BPoint offset );
	/** Command+wheel zoom: changes ticks to pixels around @a fAnchorX. */
	void zoom( bool bIn );

	/** Grid resized or content changed size. */
	void updateScrollRanges();

	PatternEditorState& state() { return m_state; }

private:
	void updateHeader();

	PatternEditorState m_state;
	BMenuField* m_pPatternField;
	BPopUpMenu* m_pPatternMenu;
	BDecimalSpinner* m_pNumerator;
	BSpinner* m_pDenominator;
	BMenuField* m_pResolutionField;
	InstrumentRows* m_pInstrumentList;
	PatternRuler* m_pRuler;
	DrumGrid* m_pGrid;
	VelocityLane* m_pVelocity;
	BScrollBar* m_pHScroll;
	BScrollBar* m_pVScroll;
	bool m_bUpdatingHeader;
};

#endif
