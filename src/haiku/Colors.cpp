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

#include "Colors.h"

#include <algorithm>
#include <cmath>

#include <core/Basics/Note.h>
#include <core/Preferences/Preferences.h>
#include <core/Preferences/Theme.h>

#include "Settings.h"

using namespace H2Core;

namespace Colors
{

static const char* kSourceName = "canvas colors";
static EditorColors sColors;
static bool sValid = false;

Source source()
{
	return Settings::get().GetBool( kSourceName, false ) ? Source::System : Source::Hydrogen;
}

void setSource( Source newSource )
{
	Settings::get().RemoveName( kSourceName );
	Settings::get().AddBool( kSourceName, newSource == Source::System );
	invalidate();
}

void invalidate()
{
	sValid = false;
}

static rgb_color fromQ( const QColor& color )
{
	return make_color( color.red(), color.green(), color.blue(), 255 );
}

static rgb_color mix( rgb_color a, rgb_color b, float f )
{
	auto lerp = [f]( uint8 x, uint8 y ) { return (uint8) std::round( x + ( y - x ) * f ); };
	return make_color( lerp( a.red, b.red ), lerp( a.green, b.green ), lerp( a.blue, b.blue ), 255 );
}

rgb_color darker( rgb_color color, int nFactor )
{
	if ( nFactor <= 0 ) {
		return color;
	}
	auto scale = [nFactor]( uint8 v ) { return (uint8) std::min( 255, v * 100 / nFactor ); };
	return make_color( scale( color.red ), scale( color.green ), scale( color.blue ), 255 );
}

rgb_color lighter( rgb_color color, int nFactor )
{
	// Like Qt: scale the value up; once it saturates, wash out towards white.
	const int nMax = std::max( { color.red, color.green, color.blue, (uint8) 1 } );
	const float fScale = nFactor / 100.0f;
	const float fOver = std::max( 0.0f, nMax * fScale - 255.0f ) / 255.0f;
	auto scale = [&]( uint8 v ) {
		const float f = std::min( 255.0f, v * fScale );
		return (uint8) std::round( f + ( 255.0f - f ) * fOver );
	};
	return make_color( scale( color.red ), scale( color.green ), scale( color.blue ), 255 );
}

static void fromHydrogenTheme( EditorColors& c )
{
	auto pPref = Preferences::get_instance();
	auto pTheme = pPref != nullptr ? pPref->getColorTheme() : nullptr;
	const ColorTheme defaults;
	const ColorTheme& t = pTheme != nullptr ? *pTheme : defaults;

	c.window = fromQ( t.m_windowColor );
	c.background = fromQ( t.m_patternEditor_backgroundColor );
	c.alternateRow = fromQ( t.m_patternEditor_alternateRowColor );
	c.selectedRow = fromQ( t.m_patternEditor_selectedRowColor );
	c.gridText = fromQ( t.m_patternEditor_textColor );
	c.line = fromQ( t.m_patternEditor_lineColor );
	c.line1 = fromQ( t.m_patternEditor_line1Color );
	c.line2 = fromQ( t.m_patternEditor_line2Color );
	c.line3 = fromQ( t.m_patternEditor_line3Color );
	c.line4 = fromQ( t.m_patternEditor_line4Color );
	c.line5 = fromQ( t.m_patternEditor_line5Color );
	c.noteFull = fromQ( t.m_patternEditor_noteVelocityFullColor );
	c.noteDefault = fromQ( t.m_patternEditor_noteVelocityDefaultColor );
	c.noteHalf = fromQ( t.m_patternEditor_noteVelocityHalfColor );
	c.noteZero = fromQ( t.m_patternEditor_noteVelocityZeroColor );
	c.noteOff = fromQ( t.m_patternEditor_noteOffColor );
	c.noteOutline = make_color( 0, 0, 0, 255 );
	c.instrumentRow = fromQ( t.m_patternEditor_instrumentRowColor );
	c.instrumentAlternateRow = fromQ( t.m_patternEditor_instrumentAlternateRowColor );
	c.instrumentSelectedRow = fromQ( t.m_patternEditor_instrumentSelectedRowColor );
	c.instrumentText = fromQ( t.m_patternEditor_instrumentRowTextColor );
	c.instrumentSelectedText = fromQ( t.m_patternEditor_instrumentSelectedRowTextColor );
	c.button = fromQ( t.m_widgetColor );
	c.buttonText = fromQ( t.m_widgetTextColor );
	c.mute = fromQ( t.m_muteColor );
	c.muteText = fromQ( t.m_muteTextColor );
	c.solo = fromQ( t.m_soloColor );
	c.soloText = fromQ( t.m_soloTextColor );
	c.playhead = fromQ( t.m_playheadColor );
	c.selection = fromQ( t.m_selectionHighlightColor );
	c.ruler = darker( c.alternateRow, 120 );
	c.rulerText = fromQ( t.m_patternEditor_textColor );
	c.songBackground = fromQ( t.m_songEditor_backgroundColor );
	c.songAlternateRow = fromQ( t.m_songEditor_alternateRowColor );
	c.songSelectedRow = fromQ( t.m_songEditor_selectedRowColor );
	c.songVirtualRow = fromQ( t.m_songEditor_virtualRowColor );
	c.songLine = fromQ( t.m_songEditor_lineColor );
	c.songText = fromQ( t.m_songEditor_textColor );
	c.songSelectedText = fromQ( t.m_songEditor_selectedRowTextColor );
	c.stackedOn = fromQ( t.m_songEditor_stackedModeOnColor );
	c.stackedOnNext = fromQ( t.m_songEditor_stackedModeOnNextColor );
	c.stackedOffNext = fromQ( t.m_songEditor_stackedModeOffNextColor );
	c.highlight = fromQ( t.m_highlightColor );
	c.midLight = fromQ( t.m_midLightColor );
	c.accent = fromQ( t.m_accentColor );
	c.buttonRed = fromQ( t.m_buttonRedColor );
	c.windowText = fromQ( t.m_windowTextColor );
	c.waveForm = fromQ( t.m_waveFormColor );
}

static void fromSystem( EditorColors& c )
{
	const rgb_color doc = ui_color( B_DOCUMENT_BACKGROUND_COLOR );
	const rgb_color text = ui_color( B_DOCUMENT_TEXT_COLOR );
	const rgb_color panel = ui_color( B_PANEL_BACKGROUND_COLOR );
	const rgb_color selected = ui_color( B_LIST_SELECTED_BACKGROUND_COLOR );
	const bool bDark = doc.Brightness() < 128;

	c.window = tint_color( panel, B_DARKEN_1_TINT );
	c.background = doc;
	c.alternateRow = mix( doc, text, 0.06f );
	c.selectedRow = mix( doc, selected, 0.5f );
	c.gridText = text;
	c.line = mix( doc, text, 0.45f );
	c.line1 = mix( doc, text, 0.55f );
	c.line2 = mix( doc, text, 0.40f );
	c.line3 = mix( doc, text, 0.30f );
	c.line4 = mix( doc, text, 0.22f );
	c.line5 = mix( doc, text, 0.15f );
	c.noteFull = make_color( 230, 70, 70, 255 );
	c.noteDefault = text;
	c.noteHalf = ui_color( B_CONTROL_HIGHLIGHT_COLOR );
	c.noteZero = doc;
	c.noteOff = ui_color( B_LINK_TEXT_COLOR );
	c.noteOutline = text;
	c.instrumentRow = panel;
	c.instrumentAlternateRow = tint_color( panel, bDark ? B_LIGHTEN_1_TINT : B_DARKEN_1_TINT );
	c.instrumentSelectedRow = selected;
	c.instrumentText = ui_color( B_PANEL_TEXT_COLOR );
	c.instrumentSelectedText = ui_color( B_LIST_SELECTED_ITEM_TEXT_COLOR );
	c.button = ui_color( B_CONTROL_BACKGROUND_COLOR );
	c.buttonText = ui_color( B_CONTROL_TEXT_COLOR );
	// Mute and solo keep their meaning: red and yellow.
	c.mute = make_color( 200, 45, 45, 255 );
	c.muteText = make_color( 255, 255, 255, 255 );
	c.solo = make_color( 225, 180, 50, 255 );
	c.soloText = make_color( 0, 0, 0, 255 );
	c.playhead = text;
	c.selection = selected;
	c.ruler = panel;
	c.rulerText = ui_color( B_PANEL_TEXT_COLOR );
	c.songBackground = doc;
	c.songAlternateRow = mix( doc, text, 0.06f );
	c.songSelectedRow = mix( doc, selected, 0.5f );
	c.songVirtualRow = mix( doc, make_color( 200, 160, 90, 255 ), 0.3f );
	c.songLine = mix( doc, text, 0.35f );
	c.songText = text;
	c.songSelectedText = ui_color( B_LIST_SELECTED_ITEM_TEXT_COLOR );
	c.stackedOn = make_color( 110, 170, 110, 255 );
	c.stackedOnNext = make_color( 240, 223, 175, 255 );
	c.stackedOffNext = make_color( 230, 90, 90, 255 );
	c.highlight = ui_color( B_KEYBOARD_NAVIGATION_COLOR );
	c.midLight = tint_color( panel, B_DARKEN_1_TINT );
	c.accent = ui_color( B_CONTROL_HIGHLIGHT_COLOR );
	c.buttonRed = make_color( 230, 100, 100, 255 );
	c.windowText = text;
	c.waveForm = mix( text, doc, 0.15f );
}

const EditorColors& get()
{
	if ( ! sValid ) {
		if ( source() == Source::System ) {
			fromSystem( sColors );
		}
		else {
			fromHydrogenTheme( sColors );
		}
		sValid = true;
	}
	return sColors;
}

rgb_color patternColor( int nRow, int nPatterns )
{
	auto pPref = Preferences::get_instance();
	auto pInterface = pPref != nullptr ? pPref->getInterfaceTheme() : nullptr;
	if ( pInterface != nullptr && pInterface->m_coloringMethod == InterfaceTheme::ColoringMethod::Automatic ) {
		const int nSteps = std::max( 1, nPatterns );
		const int nHue = ( ( nRow % nSteps ) * ( 300 / nSteps ) + 213 ) % 300;
		QColor color;
		color.setHsv( nHue, 156, 249 );
		return fromQ( color );
	}
	if ( pInterface != nullptr && ! pInterface->m_patternColors.empty() &&
		 pInterface->m_nVisiblePatternColors > 0 ) {
		const int nIndex = std::clamp( nRow % pInterface->m_nVisiblePatternColors, 0,
									   (int) pInterface->m_patternColors.size() - 1 );
		return fromQ( pInterface->m_patternColors[ nIndex ] );
	}
	return make_color( 67, 96, 131, 255 );
}

rgb_color noteColor( float fVelocity )
{
	const EditorColors& c = get();
	if ( fVelocity >= 1.0f ) {
		return c.noteFull;
	}
	if ( fVelocity >= VELOCITY_DEFAULT ) {
		return mix( c.noteDefault, c.noteFull, ( fVelocity - 0.8f ) / 0.2f );
	}
	if ( fVelocity >= 0.5f ) {
		return mix( c.noteHalf, c.noteDefault, ( fVelocity - 0.5f ) / 0.3f );
	}
	return mix( c.noteZero, c.noteHalf, std::max( 0.0f, fVelocity ) / 0.5f );
}

};
