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

#include "PropertiesWindow.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#if __has_include(<Spinner.h>)
#	include <Spinner.h>
#else
#	include <private/interface/Spinner.h>
#endif

#include <core/Basics/PatternList.h>
#include <core/Basics/Song.h>
#include <core/Hydrogen.h>
#include <core/License.h>

#include "SongActions.h"
#include "UndoStack.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PropertiesWindow"

using namespace H2Core;

enum : uint32 {
	kMsgApply = 'PWap',
	kMsgLicense = 'PWli',
};

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

static QString toQString( const char* s )
{
	return QString::fromUtf8( s );
}

PropertiesWindow::PropertiesWindow( int nPattern, BRect parentFrame, BMessenger owner )
	: BWindow( BRect( 0, 0, 420, 360 ),
			   nPattern < 0 ? B_TRANSLATE( "Song properties" ) : B_TRANSLATE( "Pattern properties" ),
			   B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE | B_NOT_ZOOMABLE )
	, m_nPattern( nPattern ), m_owner( owner )
{
	const SongActions::Properties p = nPattern < 0 ? SongActions::songProperties()
												   : SongActions::patternProperties( nPattern );

	m_pName = new BTextControl( "name", B_TRANSLATE( "Name:" ), toBString( p.sName ), nullptr );
	m_pVersion = new BSpinner( "version", B_TRANSLATE( "Version:" ), nullptr );
	m_pVersion->SetRange( 0, 300 );
	m_pVersion->SetValue( p.nVersion );
	m_pAuthor = new BTextControl( "author", B_TRANSLATE( "Author:" ), toBString( p.sAuthor ), nullptr );

	// Upstream's licence menu: the item index is the licence type.
	BPopUpMenu* pLicenseMenu = new BPopUpMenu( "license" );
	for ( int i = License::CC_0; i <= License::Unspecified; i++ ) {
		BString sLabel;
		if ( i == License::Other ) {
			sLabel = B_TRANSLATE( "Other" );
		}
		else if ( i == License::Unspecified ) {
			sLabel = B_TRANSLATE( "Unspecified" );
		}
		else {
			sLabel = toBString( License::LicenseTypeToQString( static_cast<License::LicenseType>( i ) ) );
			if ( i == License::CC_0 ) {
				sLabel << " " << B_TRANSLATE( "(public domain)" );
			}
		}
		BMessage* pMessage = new BMessage( kMsgLicense );
		pMessage->AddInt32( "type", i );
		BMenuItem* pItem = new BMenuItem( sLabel, pMessage );
		pItem->SetMarked( i == p.license.getType() );
		pLicenseMenu->AddItem( pItem );
	}
	m_pLicense = new BMenuField( "license", B_TRANSLATE( "License:" ), pLicenseMenu );
	m_pLicenseText = new BTextControl( "license text", nullptr, toBString( p.license.getLicenseString() ), nullptr );
	m_pLicenseText->SetToolTip( B_TRANSLATE( "The licence as it is written into the file" ) );

	m_pNotes = new BTextView( "notes" );
	m_pNotes->SetText( toBString( p.sNotes ) );
	m_pNotes->SetWordWrap( true );
	BScrollView* pNotesScroll = new BScrollView( "notes scroll", m_pNotes, 0, false, true );
	pNotesScroll->SetExplicitMinSize( BSize( 200, 80 ) );
	m_pTags = new BTextControl( "tags", B_TRANSLATE( "Tags:" ), toBString( p.tags.join( ", " ) ), nullptr );
	m_pTags->SetToolTip( B_TRANSLATE( "Separate tags with commas" ) );

	BButton* pCancel = new BButton( "cancel", B_TRANSLATE( "Cancel" ), new BMessage( B_QUIT_REQUESTED ) );
	BButton* pApply = new BButton( "apply", B_TRANSLATE( "Apply" ), new BMessage( kMsgApply ) );
	SetDefaultButton( pApply );

	BStringView* pPathLabel = new BStringView( nullptr, B_TRANSLATE( "File:" ) );
	BStringView* pPath = new BStringView( "path", toBString( p.sPath ) );
	pPath->SetTruncation( B_TRUNCATE_MIDDLE );
	pPath->SetExplicitMaxSize( BSize( B_SIZE_UNLIMITED, B_SIZE_UNSET ) );

	BLayoutBuilder::Group<>( this, B_VERTICAL )
		.SetInsets( B_USE_WINDOW_INSETS )
		.AddGrid()
			.AddTextControl( m_pName, 0, 0 )
			.Add( m_pVersion->CreateLabelLayoutItem(), 0, 1 )
			.Add( m_pVersion->CreateTextViewLayoutItem(), 1, 1 )
			.AddTextControl( m_pAuthor, 0, 2 )
			.AddMenuField( m_pLicense, 0, 3 )
			.Add( m_pLicenseText, 1, 4 )
			.Add( new BStringView( nullptr, B_TRANSLATE( "Notes:" ) ), 0, 5 )
			.Add( pNotesScroll, 1, 5 )
			.AddTextControl( m_pTags, 0, 6 )
			.Add( pPathLabel, 0, 7 )
			.Add( pPath, 1, 7 )
		.End()
		.AddGroup( B_HORIZONTAL )
			.AddGlue()
			.Add( pCancel )
			.Add( pApply )
		.End();

	// Patterns that were never saved on their own have no file.
	if ( p.sPath.isEmpty() ) {
		pPathLabel->Hide();
		pPath->Hide();
	}
	updateLicenseText();
	m_pName->MakeFocus( true );
	CenterIn( parentFrame );
}

void PropertiesWindow::updateLicenseText()
{
	BMenuItem* pItem = m_pLicense->Menu()->FindMarked();
	int32 nType = License::Unspecified;
	if ( pItem != nullptr ) {
		pItem->Message()->FindInt32( "type", &nType );
	}
	// Upstream hides the text for "Unspecified".
	if ( nType == License::Unspecified ) {
		if ( ! m_pLicenseText->IsHidden( m_pLicenseText ) ) {
			m_pLicenseText->Hide();
		}
	}
	else if ( m_pLicenseText->IsHidden( m_pLicenseText ) ) {
		m_pLicenseText->Show();
	}
}

void PropertiesWindow::MessageReceived( BMessage* pMessage )
{
	switch ( pMessage->what ) {
	case kMsgLicense: {
		int32 nType = License::Unspecified;
		pMessage->FindInt32( "type", &nType );
		// Picking a licence fills in its standard text, like upstream.
		if ( nType != License::Other ) {
			m_pLicenseText->SetText( toBString(
				License::LicenseTypeToQString( static_cast<License::LicenseType>( nType ) ) ) );
		}
		updateLicenseText();
		break;
	}
	case kMsgApply: {
		SongActions::Properties p = m_nPattern < 0 ? SongActions::songProperties()
												   : SongActions::patternProperties( m_nPattern );
		BString sName( m_pName->Text() );
		if ( sName.Trim().IsEmpty() ) {
			( new BAlert( B_TRANSLATE( "Properties" ), B_TRANSLATE( "Please enter a name." ),
						  B_TRANSLATE( "OK" ), nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT ) )->Go( nullptr );
			m_pName->MakeFocus( true );
			return;
		}
		p.sName = toQString( sName );
		if ( m_nPattern >= 0 ) {
			// Pattern names are unique within a song.
			auto pSong = Hydrogen::get_instance()->getSong();
			p.sName = pSong->getPatternList()->findUnusedPatternName( p.sName, m_nPattern );
		}
		p.nVersion = m_pVersion->Value();
		p.sAuthor = toQString( m_pAuthor->Text() );
		int32 nType = License::Unspecified;
		if ( BMenuItem* pItem = m_pLicense->Menu()->FindMarked() ) {
			pItem->Message()->FindInt32( "type", &nType );
		}
		p.license = License( nType == License::Unspecified ? QString() : toQString( m_pLicenseText->Text() ) );
		p.license.setCopyrightHolder( p.sAuthor );
		p.sNotes = toQString( m_pNotes->Text() );
		p.tags.clear();
		for ( const QString& sTag : toQString( m_pTags->Text() ).split( ',' ) ) {
			if ( ! sTag.trimmed().isEmpty() ) {
				p.tags << sTag.trimmed();
			}
		}
		UndoStack::get().push( m_nPattern < 0 ? SongActions::setSongProperties( p )
											  : SongActions::setPatternProperties( m_nPattern, p ) );
		m_owner.SendMessage( 'Hped' );	// the Edit menu's undo label
		Quit();
		break;
	}
	default:
		BWindow::MessageReceived( pMessage );
	}
}
