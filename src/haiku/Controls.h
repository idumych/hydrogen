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

#ifndef HAIKU_CONTROLS_H
#define HAIKU_CONTROLS_H

#include <functional>

#include <Control.h>
#include <String.h>

#include "MidiLearn.h"

#if __has_include(<Spinner.h>)
#	include <Spinner.h>
#else
#	include <private/interface/Spinner.h>
#endif

/** Upstream's Rotary: a 44×26 knob. Drag up or down (100 px is the
 * whole range), turn the wheel, or use the arrow keys; double-click
 * resets it to its default. "Center" knobs (pitch coarse/fine) are
 * drawn from the middle with − and + at their ends. Sends its message
 * while it changes; value() is the float value. */
class Knob : public BControl, public MidiLearnable
{
public:
	/** Small: upstream's 18×18 knob for FX sends, without end labels. */
	enum class Style { Normal, Center, Small };

	Knob( const char* sName, BMessage* pMessage, float fMin, float fMax, float fDefault,
		  Style style = Style::Normal, bool bIntegerSteps = false );

	float value() const { return m_fValue; }
	/** Sets the value without sending the message. */
	void setValue( float fValue );

	void Draw( BRect updateRect ) override;
	void MouseDown( BPoint where ) override;
	void MouseMoved( BPoint where, uint32 transit, const BMessage* pDrag ) override;
	void MouseUp( BPoint where ) override;
	void KeyDown( const char* pBytes, int32 nBytes ) override;
	bool GetToolTipAt( BPoint where, BToolTip** pTip ) override;
	void MessageReceived( BMessage* pMessage ) override;
	BSize MinSize() override;
	BSize MaxSize() override;

private:
	void change( float fValue );
	float step() const;
	BString valueText() const;

	float m_fMin, m_fMax, m_fDefault, m_fValue;
	Style m_style;
	bool m_bIntegerSteps;
	bool m_bDragging = false;
	float m_fDragStartY = 0;
	float m_fDragStartValue = 0;
};

/** A BSpinner whose numbers can be shown as text: "Off" for an off
 * value, or a note name like upstream's "C3 (36)". */
class FormattedSpinner : public BSpinner
{
public:
	using Formatter = std::function<BString( int32 )>;

	FormattedSpinner( const char* sName, const char* sLabel, BMessage* pMessage,
					  Formatter formatter );

	void SetValue( int32 nValue ) override;
	void SetValueFromText() override;

	/** Shows @a nOffValue as "Off". */
	static Formatter offAt( int32 nOffValue );
	/** Shows MIDI notes as "C3 (36)". */
	static Formatter midiNote();

private:
	Formatter m_formatter;
};

/** Scrolls a layout-managed view vertically. BScrollView sizes its
 * target to the visible area, which leaves nothing to scroll; this
 * viewport keeps the content at its preferred height instead and sets
 * the scroll bar's range. Put it in a BScrollView with a vertical bar. */
class ScrollPane : public BView
{
public:
	explicit ScrollPane( BView* pContent, orientation direction = B_VERTICAL );

	void FrameResized( float fWidth, float fHeight ) override;
	void AllAttached() override;
	/** Call after the content's layout changed size. */
	void contentChanged();

private:
	BView* m_pContent;
	orientation m_direction;
};

#endif
