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

#include "NameWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <TextControl.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NameWindow"

static const uint32 kMsgAccept = 'NWok';

NameWindow::NameWindow( const char* sTitle, const char* sAction, const char* sName,
						BMessenger target, const BMessage& reply, BRect parentFrame )
	: BWindow( BRect( 0, 0, 300, 80 ), sTitle, B_FLOATING_WINDOW_LOOK, B_FLOATING_APP_WINDOW_FEEL,
			   B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE )
	, m_target( target ), m_reply( reply )
{
	m_pName = new BTextControl( "name", B_TRANSLATE( "Name:" ), sName, nullptr );
	BButton* pCancel = new BButton( "cancel", B_TRANSLATE( "Cancel" ), new BMessage( B_QUIT_REQUESTED ) );
	BButton* pAccept = new BButton( "accept", sAction, new BMessage( kMsgAccept ) );
	BLayoutBuilder::Group<>( this, B_VERTICAL )
		.SetInsets( B_USE_WINDOW_INSETS )
		.Add( m_pName )
		.AddGroup( B_HORIZONTAL )
			.AddGlue()
			.Add( pCancel )
			.Add( pAccept )
		.End();
	SetDefaultButton( pAccept );
	m_pName->MakeFocus( true );
	m_pName->TextView()->SelectAll();
	CenterIn( parentFrame );
}

void NameWindow::MessageReceived( BMessage* pMessage )
{
	if ( pMessage->what == kMsgAccept ) {
		BMessage reply( m_reply );
		reply.AddString( "name", m_pName->Text() );
		m_target.SendMessage( &reply );
		Quit();
		return;
	}
	BWindow::MessageReceived( pMessage );
}
