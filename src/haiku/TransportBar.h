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

#ifndef HAIKU_TRANSPORT_BAR_H
#define HAIKU_TRANSPORT_BAR_H

#include <Control.h>
#include <View.h>

class BDecimalSpinner;
#include "MidiLearn.h"

class BStringView;

/** A toolbar button that draws its own glyph on the system control
 * look. Toggle buttons stay pressed while their value is on. */
class GlyphButton : public BControl, public MidiLearnable
{
public:
	enum class Glyph {
		Rewind, Record, Play, Pause, Stop, Forward, Loop, SongMode, PatternMode,
		Add, Clear, SinglePattern, StackedPatterns, Lock, Duplicate, Open
	};

	GlyphButton( const char* sName, Glyph glyph, BMessage* pMessage,
				 bool bToggle = false, float fSize = 32 );

	void Draw( BRect updateRect ) override;
	void MouseDown( BPoint where ) override;
	void MouseUp( BPoint where ) override;
	void MouseMoved( BPoint where, uint32 transit, const BMessage* pDrag ) override;
	void KeyDown( const char* pBytes, int32 nBytes ) override;
	bool GetToolTipAt( BPoint where, BToolTip** pTip ) override;
	BSize MinSize() override;
	BSize MaxSize() override;

	void setGlyph( Glyph glyph );

private:
	void drawGlyph( BRect rect, rgb_color color );

	Glyph m_glyph;
	float m_fSize;
	bool m_bToggle;
	bool m_bTracking;
	bool m_bPressed;
};

/** The 40 px toolbar under the menu bar: song/pattern mode, the time
 * display, transport, loop and tempo. Mirrors upstream's MainToolBar
 * (see REFERENCE.md). Sends its messages to the window. */
class TransportBar : public BView
{
public:
	TransportBar();

	void AttachedToWindow() override;
	void Draw( BRect updateRect ) override;

	/** Pulls state from the core: time, play state, modes, tempo. */
	void update();

private:
	GlyphButton* m_pSongModeButton;
	GlyphButton* m_pPatternModeButton;
	BStringView* m_pTimeDisplay;
	GlyphButton* m_pRewindButton;
	GlyphButton* m_pRecordButton;
	GlyphButton* m_pPlayButton;
	GlyphButton* m_pStopButton;
	GlyphButton* m_pForwardButton;
	GlyphButton* m_pLoopButton;
	BDecimalSpinner* m_pBpmSpinner;
	bool m_bPlaying;
};

#endif
