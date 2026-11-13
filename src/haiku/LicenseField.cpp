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

#include "LicenseField.h"

#include <Catalog.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <TextControl.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "LicenseField"

using namespace H2Core;

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

LicenseField::LicenseField( const char* sName, const char* sLabel, const License& license )
{
	BPopUpMenu* pMenu = new BPopUpMenu( sName );
	// The item index is the licence type, as in upstream's combo box.
	for ( int i = License::CC_0; i <= License::Unspecified; i++ ) {
		BString sItem;
		if ( i == License::Other ) {
			sItem = B_TRANSLATE( "Other" );
		}
		else if ( i == License::Unspecified ) {
			sItem = B_TRANSLATE( "Unspecified" );
		}
		else {
			sItem = toBString( License::LicenseTypeToQString( static_cast<License::LicenseType>( i ) ) );
			if ( i == License::CC_0 ) {
				sItem << " " << B_TRANSLATE( "(public domain)" );
			}
		}
		BMessage* pMessage = new BMessage( kMsgChosen );
		pMessage->AddPointer( "field", this );
		pMessage->AddInt32( "type", i );
		BMenuItem* pItem = new BMenuItem( sItem, pMessage );
		pItem->SetMarked( i == license.getType() );
		pMenu->AddItem( pItem );
	}
	m_pMenu = new BMenuField( sName, sLabel, pMenu );
	m_pText = new BTextControl( "license text", nullptr, toBString( license.getLicenseString() ), nullptr );
	m_pText->SetToolTip( B_TRANSLATE( "The licence as it is written into the file" ) );
}

int32 LicenseField::type() const
{
	int32 nType = License::Unspecified;
	if ( BMenuItem* pItem = m_pMenu->Menu()->FindMarked() ) {
		pItem->Message()->FindInt32( "type", &nType );
	}
	return nType;
}

bool LicenseField::chosen( BMessage* pMessage )
{
	void* pField = nullptr;
	if ( pMessage->FindPointer( "field", &pField ) != B_OK || pField != this ) {
		return false;
	}
	const int32 nType = type();
	// Picking a licence fills in its standard text, like upstream.
	if ( nType != License::Other ) {
		m_pText->SetText( toBString( License::LicenseTypeToQString( static_cast<License::LicenseType>( nType ) ) ) );
	}
	updateVisibility();
	return true;
}

License LicenseField::license( const QString& sCopyrightHolder ) const
{
	return License( type() == License::Unspecified ? QString() : QString::fromUtf8( m_pText->Text() ),
					sCopyrightHolder );
}

bool LicenseField::isConsistent() const
{
	return type() == License::Unspecified || License( QString::fromUtf8( m_pText->Text() ) ).getType() == type();
}

void LicenseField::setEnabled( bool bEnabled )
{
	m_pMenu->SetEnabled( bEnabled );
	m_pText->SetEnabled( bEnabled );
}

void LicenseField::updateVisibility()
{
	// IsHidden() alone also counts a window that isn't shown yet.
	const bool bHidden = m_pText->IsHidden( m_pText );
	if ( type() == License::Unspecified ) {
		if ( ! bHidden ) {
			m_pText->Hide();
		}
	}
	else if ( bHidden ) {
		m_pText->Show();
	}
}
