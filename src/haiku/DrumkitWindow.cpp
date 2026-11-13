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

#include "DrumkitWindow.h"

#include <algorithm>
#include <set>

#include <Alert.h>
#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <FilePanel.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <Path.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TabView.h>
#include <TextControl.h>
#include <TextView.h>
#include <TranslationUtils.h>

#if __has_include(<Spinner.h>)
#	include <Spinner.h>
#else
#	include <private/interface/Spinner.h>
#endif

#include <QDir>
#include <QFileInfo>
#include <QTextDocument>

#include <core/Basics/Drumkit.h>
#include <core/Basics/Instrument.h>
#include <core/Basics/InstrumentList.h>
#include <core/Basics/Song.h>
#include <core/EventQueue.h>
#include <core/Helpers/Filesystem.h>
#include <core/Hydrogen.h>
#include <core/Preferences/Preferences.h>
#include <core/SoundLibrary/SoundLibraryDatabase.h>

#include "Controls.h"
#include "LicenseField.h"
#include "SongActions.h"
#include "UndoStack.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DrumkitWindow"

using namespace H2Core;

enum : uint32 {
	kMsgApply = 'DWap',
	kMsgName = 'DWnm',
	kMsgFolder = 'DWfo',
	kMsgChooseFolder = 'DWcf',
	kMsgFolderChosen = 'DWfc',
	kMsgChooseImage = 'DWci',
	kMsgImageChosen = 'DWic',
	kMsgClearImage = 'DWcl',
	kMsgImageEdited = 'DWie',
	kMsgTypeEdited = 'DWte',
};

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

static QString toQString( const char* s )
{
	return QString::fromUtf8( s );
}

static void alert( const BString& sText, alert_type type = B_WARNING_ALERT )
{
	BAlert* pAlert = new BAlert( B_TRANSLATE( "Drumkit" ), sText, B_TRANSLATE( "OK" ), nullptr, nullptr,
								 B_WIDTH_AS_USUAL, type );
	pAlert->SetFlags( pAlert->Flags() | B_CLOSE_ON_ESCAPE );
	pAlert->Go();
}

/** Only folders, for the Save as folder panel. */
class FolderFilter : public BRefFilter
{
public:
	bool Filter( const entry_ref*, BNode* pNode, struct stat_beos*, const char* ) override
	{
		return pNode->IsDirectory();
	}
};

/** Folders and the image types upstream accepts. */
class ImageFilter : public BRefFilter
{
public:
	bool Filter( const entry_ref* pRef, BNode* pNode, struct stat_beos*, const char* ) override
	{
		if ( pNode->IsDirectory() ) {
			return true;
		}
		const BString sName( pRef->name );
		return sName.IEndsWith( ".png" ) || sName.IEndsWith( ".jpg" ) || sName.IEndsWith( ".jpeg" );
	}
};

// ---------------------------------------------------------------------------
// ImagePreview: the kit's image scaled to fit (upstream's preview label).

class ImagePreview : public BView
{
public:
	ImagePreview() : BView( "image preview", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE )
	{
		SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
		SetExplicitMinSize( BSize( 200, 75 ) );
		SetExplicitPreferredSize( BSize( 320, 160 ) );
	}

	void setPath( const QString& sPath )
	{
		m_pBitmap.reset();
		m_sMessage = "";
		if ( ! sPath.isEmpty() ) {
			if ( ! QFileInfo::exists( sPath ) ) {
				m_sMessage = B_TRANSLATE( "The image file wasn't found." );
			}
			else {
				m_pBitmap.reset( BTranslationUtils::GetBitmap( sPath.toUtf8().constData() ) );
				if ( m_pBitmap == nullptr ) {
					m_sMessage = B_TRANSLATE( "The image couldn't be loaded." );
				}
			}
		}
		Invalidate();
	}

	void Draw( BRect ) override
	{
		const BRect b = Bounds();
		SetHighUIColor( B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT );
		StrokeRect( b );
		if ( m_pBitmap != nullptr ) {
			const BRect source = m_pBitmap->Bounds();
			const float fScale = std::min( { ( b.Width() - 4 ) / source.Width(), ( b.Height() - 4 ) / source.Height(), 1.0f } );
			const float fWidth = source.Width() * fScale, fHeight = source.Height() * fScale;
			const BRect target( ( b.Width() - fWidth ) / 2, ( b.Height() - fHeight ) / 2,
								( b.Width() + fWidth ) / 2, ( b.Height() + fHeight ) / 2 );
			SetDrawingMode( B_OP_ALPHA );
			DrawBitmap( m_pBitmap.get(), source, target, B_FILTER_BITMAP_BILINEAR );
			SetDrawingMode( B_OP_COPY );
		}
		else if ( ! m_sMessage.IsEmpty() ) {
			SetHighUIColor( B_PANEL_TEXT_COLOR );
			DrawString( m_sMessage, BPoint( ( b.Width() - StringWidth( m_sMessage ) ) / 2, b.Height() / 2 ) );
		}
	}

private:
	std::unique_ptr<BBitmap> m_pBitmap;
	BString m_sMessage;
};

// ---------------------------------------------------------------------------
// DrumkitWindow

static BMessenger sWindow;

void DrumkitWindow::open( Mode mode, BRect parentFrame, BMessenger owner )
{
	const auto pSong = Hydrogen::get_instance()->getSong();
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		return;
	}
	// One at a time: both modes edit the same kit.
	if ( sWindow.IsValid() ) {
		sWindow.SendMessage( B_QUIT_REQUESTED );
	}
	DrumkitWindow* pWindow = new DrumkitWindow( mode, parentFrame, owner );
	sWindow = BMessenger( pWindow );
	pWindow->Show();
}

DrumkitWindow::DrumkitWindow( Mode mode, BRect parentFrame, BMessenger owner )
	: BWindow( BRect( 0, 0, 560, 600 ),
			   mode == Mode::SaveAs ? B_TRANSLATE( "Save drumkit as" ) : B_TRANSLATE( "Drumkit properties" ),
			   B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE )
	, m_mode( mode )
	, m_owner( owner )
	, m_pKit( Hydrogen::get_instance()->getSong()->getDrumkit() )
{
	BTabView* pTabs = new BTabView( "tabs", B_WIDTH_FROM_LABEL );
	pTabs->SetBorder( B_NO_BORDER );
	BView* pGeneral = buildGeneral();
	pGeneral->SetName( B_TRANSLATE( "General" ) );
	pTabs->AddTab( pGeneral );
	BView* pTypes = buildTypes();
	pTypes->SetName( B_TRANSLATE( "Instrument types" ) );
	pTabs->AddTab( pTypes );

	BButton* pCancel = new BButton( "cancel", B_TRANSLATE( "Cancel" ), new BMessage( B_QUIT_REQUESTED ) );
	BButton* pApply = new BButton( "apply", mode == Mode::SaveAs ? B_TRANSLATE( "Save" ) : B_TRANSLATE( "Apply" ),
								   new BMessage( kMsgApply ) );
	SetDefaultButton( pApply );

	BLayoutBuilder::Group<>( this, B_VERTICAL )
		.SetInsets( 0, B_USE_SMALL_SPACING, 0, B_USE_WINDOW_SPACING )
		.Add( pTabs )
		.AddGroup( B_HORIZONTAL )
			.SetInsets( B_USE_WINDOW_SPACING, 0, B_USE_WINDOW_SPACING, 0 )
			.AddGlue()
			.Add( pCancel )
			.Add( pApply )
		.End();

	m_pLicense->updateVisibility();
	m_pImageLicense->updateVisibility();
	m_pPreview->setPath( m_pKit->getAbsoluteImagePath() );
	m_pName->MakeFocus( true );
	CenterIn( parentFrame );
}

DrumkitWindow::~DrumkitWindow()
{
	delete m_pImagePanel;
	delete m_pFolderPanel;
}

QString DrumkitWindow::defaultFolder() const
{
	return QDir( Filesystem::userDrumkitsDir() )
		.absoluteFilePath( Filesystem::validateFilePath( toQString( m_pName->Text() ) ) );
}

BView* DrumkitWindow::buildGeneral()
{
	const auto& pKit = m_pKit;
	m_pName = new BTextControl( "name", B_TRANSLATE( "Name:" ), toBString( pKit->getName() ), nullptr );
	m_pName->SetModificationMessage( new BMessage( kMsgName ) );
	m_pVersion = new BSpinner( "version", B_TRANSLATE( "Version:" ), nullptr );
	m_pVersion->SetRange( 0, 300 );
	m_pVersion->SetValue( pKit->getVersion() );
	m_pAuthor = new BTextControl( "author", B_TRANSLATE( "Author:" ), toBString( pKit->getAuthor() ), nullptr );
	m_pLicense = std::make_unique<LicenseField>( "license", B_TRANSLATE( "License:" ), pKit->getLicense() );

	// Upstream saves the notes as Qt rich text; show them as plain text.
	QString sInfo = pKit->getInfo();
	if ( Qt::mightBeRichText( sInfo ) ) {
		QTextDocument document;
		document.setHtml( sInfo );
		sInfo = document.toPlainText();
	}
	m_pNotes = new BTextView( "notes" );
	m_pNotes->SetText( toBString( sInfo ) );
	m_pNotes->SetWordWrap( true );
	BScrollView* pNotes = new BScrollView( "notes scroll", m_pNotes, 0, false, true );
	pNotes->SetExplicitMinSize( BSize( 200, 70 ) );
	m_pTags = new BTextControl( "tags", B_TRANSLATE( "Tags:" ), toBString( pKit->getTags().join( ", " ) ), nullptr );
	m_pTags->SetToolTip( B_TRANSLATE( "Separate tags with commas" ) );

	m_pImage = new BTextControl( "image", B_TRANSLATE( "Image:" ), toBString( pKit->getAbsoluteImagePath() ), nullptr );
	m_pImage->SetModificationMessage( new BMessage( kMsgImageEdited ) );
	BButton* pChoose = new BButton( "choose image", B_TRANSLATE( "Choose" B_UTF8_ELLIPSIS ), new BMessage( kMsgChooseImage ) );
	BButton* pClear = new BButton( "clear image", B_TRANSLATE( "Clear" ), new BMessage( kMsgClearImage ) );
	m_pPreview = new ImagePreview();
	m_pImageLicense = std::make_unique<LicenseField>( "image license", B_TRANSLATE( "Image license:" ),
													  pKit->getImageLicense() );

	BGroupView* pView = new BGroupView( B_VERTICAL );
	BGridLayout* pGrid = nullptr;
	BLayoutBuilder::Group<>( pView )
		.SetInsets( B_USE_WINDOW_SPACING )
		.AddGrid()
			.GetLayout( &pGrid )
		.End()
		.AddGlue();
	// Column 1 holds the fields; the Choose…/Clear buttons get columns 2
	// and 3, which the other rows span.
	int32 nRow = 0;
	auto field = [&]( BLayoutItem* pLabel, BLayoutItem* pField ) {
		if ( pLabel != nullptr ) {
			pGrid->AddItem( pLabel, 0, nRow );
		}
		pGrid->AddItem( pField, 1, nRow++, 3 );
	};
	auto fieldView = [&]( BView* pLabel, BView* pField ) {
		if ( pLabel != nullptr ) {
			pGrid->AddView( pLabel, 0, nRow );
		}
		pGrid->AddView( pField, 1, nRow++, 3 );
	};
	if ( m_mode == Mode::SaveAs ) {
		m_pFolder = new BTextControl( "folder", B_TRANSLATE( "Folder:" ), toBString( defaultFolder() ), nullptr );
		m_pFolder->SetModificationMessage( new BMessage( kMsgFolder ) );
		BButton* pFolder = new BButton( "choose folder", B_TRANSLATE( "Choose" B_UTF8_ELLIPSIS ),
										new BMessage( kMsgChooseFolder ) );
		pGrid->AddItem( m_pFolder->CreateLabelLayoutItem(), 0, nRow );
		pGrid->AddItem( m_pFolder->CreateTextViewLayoutItem(), 1, nRow );
		pGrid->AddView( pFolder, 2, nRow++ );
	}
	field( m_pName->CreateLabelLayoutItem(), m_pName->CreateTextViewLayoutItem() );
	field( m_pVersion->CreateLabelLayoutItem(), m_pVersion->CreateTextViewLayoutItem() );
	field( m_pAuthor->CreateLabelLayoutItem(), m_pAuthor->CreateTextViewLayoutItem() );
	field( m_pLicense->menuField()->CreateLabelLayoutItem(), m_pLicense->menuField()->CreateMenuBarLayoutItem() );
	fieldView( nullptr, m_pLicense->textControl() );
	fieldView( new BStringView( nullptr, B_TRANSLATE( "Notes:" ) ), pNotes );
	field( m_pTags->CreateLabelLayoutItem(), m_pTags->CreateTextViewLayoutItem() );
	pGrid->AddItem( m_pImage->CreateLabelLayoutItem(), 0, nRow );
	pGrid->AddItem( m_pImage->CreateTextViewLayoutItem(), 1, nRow );
	pGrid->AddView( pChoose, 2, nRow );
	pGrid->AddView( pClear, 3, nRow++ );
	fieldView( nullptr, m_pPreview );
	field( m_pImageLicense->menuField()->CreateLabelLayoutItem(),
		   m_pImageLicense->menuField()->CreateMenuBarLayoutItem() );
	fieldView( nullptr, m_pImageLicense->textControl() );
	pGrid->SetColumnWeight( 1, 1 );
	return pView;
}

BView* DrumkitWindow::buildTypes()
{
	// Upstream's Types tab: ID, instrument, type; types must be unique.
	BGroupView* pRows = new BGroupView( B_VERTICAL, B_USE_HALF_ITEM_SPACING );
	BGridLayout* pGrid = nullptr;
	BLayoutBuilder::Group<>( pRows )
		.SetInsets( B_USE_WINDOW_SPACING )
		.AddGrid( B_USE_ITEM_SPACING, B_USE_HALF_ITEM_SPACING )
			.GetLayout( &pGrid )
		.End()
		.AddGlue();
	BFont bold( be_bold_font );
	auto header = [&bold]( const char* sText ) {
		BStringView* pView = new BStringView( nullptr, sText );
		pView->SetFont( &bold );
		return pView;
	};
	pGrid->AddView( header( B_TRANSLATE( "ID" ) ), 0, 0 );
	pGrid->AddView( header( B_TRANSLATE( "Instrument" ) ), 1, 0 );
	pGrid->AddView( header( B_TRANSLATE( "Type" ) ), 2, 0 );
	int32 nRow = 1;
	for ( const auto& pInstrument : *m_pKit->getInstruments() ) {
		if ( pInstrument == nullptr ) {
			continue;
		}
		BString sId;
		sId << static_cast<int>( pInstrument->getId() );
		pGrid->AddView( new BStringView( nullptr, sId ), 0, nRow );
		pGrid->AddView( new BStringView( nullptr, toBString( pInstrument->getName() ) ), 1, nRow );
		BTextControl* pType = new BTextControl( "type", nullptr, toBString( pInstrument->getType() ), nullptr );
		pType->SetModificationMessage( new BMessage( kMsgTypeEdited ) );
		pType->SetExplicitMinSize( BSize( 180, B_SIZE_UNSET ) );
		pGrid->AddView( pType, 2, nRow++ );
		m_types.push_back( pType );
	}
	pGrid->SetColumnWeight( 2, 1 );
	ScrollPane* pPane = new ScrollPane( pRows );
	pPane->SetExplicitMinSize( BSize( 300, 200 ) );
	return pPane;
}

bool DrumkitWindow::validate()
{
	BString sName( m_pName->Text() );
	if ( sName.Trim().IsEmpty() ) {
		alert( B_TRANSLATE( "Please enter a name." ) );
		m_pName->MakeFocus( true );
		return false;
	}
	if ( ! m_pLicense->isConsistent() || ! m_pImageLicense->isConsistent() ) {
		BAlert* pAlert = new BAlert( B_TRANSLATE( "Drumkit" ),
									 B_TRANSLATE( "A licence text doesn't match the licence chosen in its menu." ),
									 B_TRANSLATE( "Cancel" ), B_TRANSLATE( "Use it anyway" ), nullptr,
									 B_WIDTH_AS_USUAL, B_WARNING_ALERT );
		pAlert->SetShortcut( 0, B_ESCAPE );
		if ( pAlert->Go() != 1 ) {
			return false;
		}
	}
	std::set<QString> types;
	bool bDuplicate = false;
	for ( BTextControl* pType : m_types ) {
		const QString sType = toQString( pType->Text() ).trimmed();
		const bool bTaken = ! sType.isEmpty() && ! types.insert( sType ).second;
		pType->MarkAsInvalid( bTaken );
		bDuplicate = bDuplicate || bTaken;
	}
	if ( bDuplicate ) {
		alert( B_TRANSLATE( "Instrument types must be unique." ) );
		return false;
	}
	if ( m_mode == Mode::SaveAs ) {
		const QString sFolder = toQString( m_pFolder->Text() ).trimmed();
		if ( sFolder.isEmpty() || ! QFileInfo( sFolder ).isAbsolute() ) {
			alert( B_TRANSLATE( "Please choose a folder to save the drumkit in." ) );
			return false;
		}
		const auto context = Filesystem::DetermineContext( sFolder );
		if ( context == Filesystem::Context::System ) {
			alert( B_TRANSLATE( "The system's drumkits can't be changed. Please choose another folder." ) );
			return false;
		}
		if ( QFileInfo::exists( Filesystem::drumkitPathFromDir( sFolder ) ) ) {
			BString sText( B_TRANSLATE( "A drumkit is already saved in \"%folder%\". Do you want to replace it?" ) );
			sText.ReplaceFirst( "%folder%", toBString( sFolder ) );
			BAlert* pAlert = new BAlert( B_TRANSLATE( "Drumkit" ), sText, B_TRANSLATE( "Cancel" ),
										 B_TRANSLATE( "Replace" ), nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT );
			pAlert->SetShortcut( 0, B_ESCAPE );
			if ( pAlert->Go() != 1 ) {
				return false;
			}
		}
	}
	return true;
}

void DrumkitWindow::apply()
{
	const auto pSong = Hydrogen::get_instance()->getSong();
	if ( pSong == nullptr || pSong->getDrumkit() != m_pKit ) {
		alert( B_TRANSLATE( "The song's drumkit changed meanwhile; please open the window again." ), B_STOP_ALERT );
		Quit();
		return;
	}
	if ( ! validate() ) {
		return;
	}

	auto pNew = std::make_shared<Drumkit>( m_pKit );
	const QString sAuthor = toQString( m_pAuthor->Text() );
	BString sName( m_pName->Text() );
	pNew->setName( toQString( sName.Trim() ) );
	pNew->setVersion( m_pVersion->Value() );
	pNew->setAuthor( sAuthor );
	pNew->setInfo( toQString( m_pNotes->Text() ) );
	// Only a changed licence is replaced, so its attribution survives.
	const License license = m_pLicense->license( sAuthor );
	if ( license.getLicenseString() != m_pKit->getLicense().getLicenseString() ) {
		pNew->setLicense( license );
	}
	const License imageLicense = m_pImageLicense->license( sAuthor );
	if ( imageLicense.getLicenseString() != m_pKit->getImageLicense().getLicenseString() ) {
		pNew->setImageLicense( imageLicense );
	}
	QStringList tags;
	for ( const QString& sTag : toQString( m_pTags->Text() ).split( ',' ) ) {
		if ( ! sTag.trimmed().isEmpty() ) {
			tags << sTag.trimmed();
		}
	}
	pNew->setTags( tags );
	size_t nType = 0;
	for ( const auto& pInstrument : *pNew->getInstruments() ) {
		if ( pInstrument != nullptr && nType < m_types.size() ) {
			pInstrument->setType( toQString( m_types[ nType++ ]->Text() ).trimmed() );
		}
	}

	// A new image: the song's kit keeps a copy in the cache, as upstream,
	// so the song doesn't depend on where it came from. Nothing is
	// deleted (upstream could delete the image it was copying).
	const QString sImage = toQString( m_pImage->Text() ).trimmed();
	if ( sImage.isEmpty() ) {
		pNew->setImage( "" );
	}
	else if ( sImage != m_pKit->getAbsoluteImagePath() ) {
		if ( ! QFileInfo( sImage ).isFile() ) {
			BString sText( B_TRANSLATE( "The image \"%file%\" wasn't found." ) );
			sText.ReplaceFirst( "%file%", toBString( sImage ) );
			alert( sText );
			return;
		}
		const QString sCached = Filesystem::addUniquePrefix(
			QDir( Filesystem::cacheDir() ).absoluteFilePath( QFileInfo( sImage ).fileName() ) );
		if ( ! Filesystem::fileCopy( sImage, sCached, false, true ) ) {
			alert( B_TRANSLATE( "The image couldn't be copied." ), B_STOP_ALERT );
			return;
		}
		pNew->setImage( sCached );
	}

	if ( m_mode == Mode::SaveAs ) {
		// Written from a copy, so the song's kit keeps its samples' paths
		// (upstream's save() rewrote them in the song's kit).
		const QString sPath = Filesystem::drumkitPathFromDir( toQString( m_pFolder->Text() ).trimmed() );
		pNew->setPath( sPath );
		auto pDisk = std::make_shared<Drumkit>( pNew );
		if ( ! pDisk->save( sPath ) ) {
			BString sText( B_TRANSLATE( "The drumkit couldn't be saved in \"%folder%\"." ) );
			sText.ReplaceFirst( "%folder%", toBString( Filesystem::drumkitDirFromPath( sPath ) ) );
			alert( sText, B_STOP_ALERT );
			return;
		}
		pNew->setIsModified( false );
		Preferences::get_instance()->setLastSaveDrumkitAsDirectory(
			QFileInfo( Filesystem::drumkitDirFromPath( sPath ) ).absolutePath() );
		Hydrogen::get_instance()->getSoundLibraryDatabase()->updateDrumkits( Event::Trigger::Default );
	}

	UndoStack::get().push( SongActions::editDrumkit(
		pNew, m_mode == Mode::SaveAs ? B_TRANSLATE( "save drumkit as" ) : B_TRANSLATE( "edit drumkit properties" ) ) );
	if ( m_mode == Mode::SaveAs ) {
		EventQueue::get_instance()->pushEvent( Event::Type::DrumkitIsModified, -1 );
	}
	m_owner.SendMessage( 'Hped' );
	Quit();
}

void DrumkitWindow::MessageReceived( BMessage* pMessage )
{
	switch ( pMessage->what ) {
	case LicenseField::kMsgChosen:
		if ( ! m_pLicense->chosen( pMessage ) ) {
			m_pImageLicense->chosen( pMessage );
		}
		break;
	case kMsgName:
		// The Save as folder follows the name until it's edited.
		if ( m_pFolder != nullptr && ! m_bFolderEdited ) {
			m_pFolder->SetModificationMessage( nullptr );
			m_pFolder->SetText( toBString( defaultFolder() ) );
			m_pFolder->SetModificationMessage( new BMessage( kMsgFolder ) );
		}
		break;
	case kMsgFolder:
		m_bFolderEdited = true;
		break;
	case kMsgChooseFolder: {
		if ( m_pFolderPanel == nullptr ) {
			static FolderFilter filter;
			m_pFolderPanel = new BFilePanel( B_OPEN_PANEL, new BMessenger( this ), nullptr, B_DIRECTORY_NODE,
											 false, new BMessage( kMsgFolderChosen ), &filter, true );
			m_pFolderPanel->Window()->SetTitle( B_TRANSLATE( "Choose a parent folder" ) );
		}
		m_pFolderPanel->SetPanelDirectory( Filesystem::userDrumkitsDir().toUtf8().constData() );
		m_pFolderPanel->Show();
		break;
	}
	case kMsgFolderChosen: {
		entry_ref ref;
		if ( pMessage->FindRef( "refs", &ref ) == B_OK ) {
			// The kit goes into a folder named after it inside the chosen one.
			BPath path( &ref );
			const QString sFolder = QDir( toQString( path.Path() ) )
				.absoluteFilePath( Filesystem::validateFilePath( toQString( m_pName->Text() ) ) );
			m_pFolder->SetText( toBString( sFolder ) );
			m_bFolderEdited = true;
		}
		break;
	}
	case kMsgChooseImage: {
		if ( m_pImagePanel == nullptr ) {
			static ImageFilter filter;
			m_pImagePanel = new BFilePanel( B_OPEN_PANEL, new BMessenger( this ), nullptr, B_FILE_NODE, false,
											new BMessage( kMsgImageChosen ), &filter, true );
			m_pImagePanel->Window()->SetTitle( B_TRANSLATE( "Choose an image" ) );
		}
		// Upstream started at the drumkit.xml file instead of its folder.
		QString sStart = QFileInfo( toQString( m_pImage->Text() ) ).absolutePath();
		if ( toQString( m_pImage->Text() ).isEmpty() || ! QDir( sStart ).exists() ) {
			sStart = m_pKit->getPath().isEmpty() ? QDir::homePath()
												 : Filesystem::drumkitDirFromPath( m_pKit->getPath() );
		}
		m_pImagePanel->SetPanelDirectory( sStart.toUtf8().constData() );
		m_pImagePanel->Show();
		break;
	}
	case kMsgImageChosen: {
		entry_ref ref;
		if ( pMessage->FindRef( "refs", &ref ) == B_OK ) {
			m_pImage->SetText( BPath( &ref ).Path() );
			m_pPreview->setPath( toQString( m_pImage->Text() ) );
		}
		break;
	}
	case kMsgClearImage:
		m_pImage->SetText( "" );
		m_pPreview->setPath( "" );
		break;
	case kMsgImageEdited:
		m_pPreview->setPath( toQString( m_pImage->Text() ).trimmed() );
		break;
	case kMsgTypeEdited:
		break;
	case kMsgApply:
		apply();
		break;
	default:
		BWindow::MessageReceived( pMessage );
	}
}
