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

#ifndef HAIKU_COLORS_H
#define HAIKU_COLORS_H

#include <InterfaceDefs.h>

/** The colours the editor canvases draw with. Controls (buttons, menus,
 * text fields) always use the system look; the canvases use either
 * Hydrogen's colour theme from the core's preferences (the default) or
 * colours derived from the Haiku system colours, so they follow the
 * desktop theme and dark mode. */
struct EditorColors {
	rgb_color window;			///< outside the pattern, empty areas
	rgb_color background;		///< grid rows
	rgb_color alternateRow;
	rgb_color selectedRow;
	rgb_color gridText;
	rgb_color line;				///< row separators
	rgb_color line1, line2, line3, line4, line5;	///< beat … 1/64 lines
	rgb_color noteFull, noteDefault, noteHalf, noteZero, noteOff;
	rgb_color noteOutline;
	rgb_color instrumentRow, instrumentAlternateRow, instrumentSelectedRow;
	rgb_color instrumentText, instrumentSelectedText;
	rgb_color button, buttonText;
	rgb_color mute, muteText, solo, soloText;
	rgb_color playhead;
	rgb_color selection;
	rgb_color ruler, rulerText;
	// Song editor
	rgb_color songBackground, songAlternateRow, songSelectedRow, songVirtualRow;
	rgb_color songLine, songText, songSelectedText;
	rgb_color stackedOn, stackedOnNext, stackedOffNext;
	rgb_color highlight;		///< selected pattern's border, drop targets
	rgb_color midLight;			///< ruler beyond the song's end
	// Component editor
	rgb_color accent;			///< layer bars, waveform background
	rgb_color buttonRed;		///< layers without a sample, velocity gaps
	rgb_color windowText;
	rgb_color waveForm;
};

namespace Colors
{
	enum class Source { Hydrogen, System };

	Source source();
	void setSource( Source source );

	/** The current colours; rebuilt after setSource() or invalidate(). */
	const EditorColors& get();
	/** Call when the system colours or Hydrogen's theme changed. */
	void invalidate();

	/** QColor::darker()/lighter() equivalents (factor in percent). */
	rgb_color darker( rgb_color color, int nFactor );
	rgb_color lighter( rgb_color color, int nFactor );
	/** Upstream's computeNoteColor(): velocity 0..1 to a fill colour. */
	rgb_color noteColor( float fVelocity );
	/** Upstream's song editor cell colour for pattern row @a nRow of
	 * @a nPatterns: the theme's palette, or its automatic hue spread. */
	rgb_color patternColor( int nRow, int nPatterns );
};

#endif
