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

#include "Rack.h"

#include <Catalog.h>
#include <ScrollView.h>
#include <StringView.h>

#include "InstrumentParameters.h"
#include "Settings.h"
#include "SoundLibrary.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Rack"

static const char* kTabName = "rack tab";

Rack::Rack()
	: BTabView( "instrument rack", B_WIDTH_FROM_LABEL )
{
	m_pParameters = new InstrumentParameters();
	// The parameters can be taller than the rack: let them scroll.
	BScrollView* pScroll = new BScrollView( B_TRANSLATE( "Parameters" ), m_pParameters, 0, false,
											true, B_NO_BORDER );
	AddTab( pScroll );

	// The component (sample layer) editor is the next step of the port.
	BStringView* pComponents = new BStringView( B_TRANSLATE( "Components" ),
		B_TRANSLATE( "The component editor isn't ported yet." ) );
	pComponents->SetAlignment( B_ALIGN_CENTER );
	AddTab( pComponents );

	m_pLibrary = new SoundLibrary();
	AddTab( m_pLibrary );

	SetExplicitSize( BSize( kWidth - 1, B_SIZE_UNSET ) );
	SetExplicitMinSize( BSize( kWidth - 1, 200 ) );
	SetExplicitMaxSize( BSize( kWidth - 1, B_SIZE_UNLIMITED ) );

	int32 nTab = 0;
	if ( Settings::get().FindInt32( kTabName, &nTab ) == B_OK && nTab >= 0 && nTab < CountTabs() ) {
		BTabView::Select( nTab );
	}
}

void Rack::Select( int32 nIndex )
{
	BTabView::Select( nIndex );
	// Like upstream, remember the last tab.
	if ( Settings::get().ReplaceInt32( kTabName, nIndex ) != B_OK ) {
		Settings::get().AddInt32( kTabName, nIndex );
	}
}

void Rack::refreshInstrument()
{
	m_pParameters->refresh();
}

void Rack::refreshLibrary()
{
	m_pLibrary->refresh();
}
