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

#include "ExportWindows.h"

#include <algorithm>

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <StatusBar.h>
#include <TextControl.h>

#if __has_include(<DecimalSpinner.h>)
#	include <DecimalSpinner.h>
#else
#	include <private/interface/DecimalSpinner.h>
#endif

#include <QDir>
#include <QFileInfo>

#include <core/AudioEngine/AudioEngine.h>
#include <core/AudioEngine/Transport.h>
#include <core/Basics/Drumkit.h>
#include <core/Basics/Instrument.h>
#include <core/Basics/InstrumentComponent.h>
#include <core/Basics/InstrumentLayer.h>
#include <core/Basics/InstrumentList.h>
#include <core/Basics/Note.h>
#include <core/Basics/Sample.h>
#include <core/Basics/Song.h>
#include <core/Hydrogen.h>
#include <core/IO/DiskWriterDriver.h>
#include <core/Midi/SMF.h>
#include <core/Preferences/Preferences.h>
#include <core/Sampler/Sampler.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ExportWindows"

using namespace H2Core;

enum : uint32 {
	kMsgNameChanged = 'XWnc',
	kMsgFormat = 'XWfm',
	kMsgChoose = 'XWch',
	kMsgExport = 'XWex',
	kMsgTimeline = 'XWtl',
	kMsgRubberband = 'XWrb',
	kMsgInterpolation = 'XWip',
	kMsgSettingChanged = 'XWsc',
};

enum ExportMode { kSingleTrack, kSeparateTracks, kBoth };

static const int kSampleRates[] = { 22050, 44100, 48000, 88200, 96000, 192000 };
static const int kSampleDepths[] = { 8, 16, 24, 32 };

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

static QString toQString( const char* s )
{
	return QString::fromUtf8( s );
}

static BMenuField* menuField( const char* sName, const char* sLabel,
							  const std::vector<BString>& items, uint32 what )
{
	BPopUpMenu* pMenu = new BPopUpMenu( sName );
	int32 nIndex = 0;
	for ( const BString& sItem : items ) {
		BMessage* pMessage = new BMessage( what );
		pMessage->AddInt32( "index", nIndex++ );
		pMenu->AddItem( new BMenuItem( sItem, pMessage ) );
	}
	return new BMenuField( sName, sLabel, pMenu );
}

static int32 markedIndex( BMenuField* pField )
{
	return pField->Menu()->IndexOf( pField->Menu()->FindMarked() );
}

static void mark( BMenuField* pField, int32 nIndex )
{
	if ( BMenuItem* pItem = pField->Menu()->ItemAt( nIndex ) ) {
		pItem->SetMarked( true );
	}
	else if ( BMenuItem* pFirst = pField->Menu()->ItemAt( 0 ) ) {
		pFirst->SetMarked( true );
	}
}

static void warn( const BString& sText, alert_type type = B_WARNING_ALERT )
{
	BAlert* pAlert = new BAlert( "Hydrogen", sText, B_TRANSLATE( "OK" ), nullptr, nullptr,
								 B_WIDTH_AS_USUAL, type );
	pAlert->SetFlags( pAlert->Flags() | B_CLOSE_ON_ESCAPE );
	pAlert->Go();
}

/** The song's name as a file name, upstream's createDefaultFileName(). */
static QString defaultBaseName()
{
	const auto pSong = Hydrogen::get_instance()->getSong();
	if ( pSong == nullptr ) {
		return QString();
	}
	QString sName = pSong->getName();
	if ( sName.isEmpty() ) {
		sName = QFileInfo( pSong->getPath() ).completeBaseName();
	}
	sName.replace( '*', "_" ).replace( '/', "_" );
	sName.replace( Filesystem::sSongSuffix, "" );
	if ( sName.isEmpty() ) {
		sName = "untitled";
	}
	return sName;
}

/** @a sPath made absolute against @a sDirectory, for names typed
 * without a folder. */
static QString absolutePath( const QString& sPath, const QString& sDirectory )
{
	QFileInfo info( sPath );
	if ( info.isAbsolute() ) {
		return info.absoluteFilePath();
	}
	return QDir( sDirectory ).absoluteFilePath( sPath );
}

/** Checks that @a sPath's folder exists and can be written to. */
static bool checkFolder( const QString& sPath )
{
	const QDir dir = QFileInfo( sPath ).absoluteDir();
	if ( ! dir.exists() ) {
		BString sText( B_TRANSLATE( "The folder \"%folder%\" doesn't exist." ) );
		sText.ReplaceFirst( "%folder%", toBString( dir.absolutePath() ) );
		warn( sText );
		return false;
	}
	if ( ! Filesystem::dirWritable( dir.absolutePath(), true ) ) {
		BString sText( B_TRANSLATE( "You can't write to the folder \"%folder%\"." ) );
		sText.ReplaceFirst( "%folder%", toBString( dir.absolutePath() ) );
		warn( sText );
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Export song

struct FormatInfo {
	Filesystem::AudioFormat format;
	const char* sLabel;
};

// Upstream's order. Only those the core was built with are offered.
static const FormatInfo kFormats[] = {
	{ Filesystem::AudioFormat::Flac, "FLAC (Free Lossless Audio Codec)" },
	{ Filesystem::AudioFormat::Opus, "Opus (Opus Compressed Audio)" },
	{ Filesystem::AudioFormat::Ogg, "Ogg (Vorbis Compressed Audio)" },
	{ Filesystem::AudioFormat::Mp3, "MP3 (MP3 Compressed Audio)" },
	{ Filesystem::AudioFormat::Wav, "WAV (Waveform Audio)" },
	{ Filesystem::AudioFormat::Aiff, "AIFF (Audio Interchange File Format)" },
	{ Filesystem::AudioFormat::Au, "AU (Sun/NeXT Audio)" },
	{ Filesystem::AudioFormat::Caf, "CAF (Core Audio Format)" },
	{ Filesystem::AudioFormat::Voc, "VOC (Creative Voice)" },
	{ Filesystem::AudioFormat::W64, "W64 (Sonic Foundry's 64 bit RIFF/WAV)" },
};

/** Formats with a compression level instead of sample rate and depth. */
static bool isCompressed( Filesystem::AudioFormat format )
{
	return format == Filesystem::AudioFormat::Flac || format == Filesystem::AudioFormat::Opus ||
		   format == Filesystem::AudioFormat::Ogg || format == Filesystem::AudioFormat::Mp3;
}

/** Aif and Aifc are Aiff, as far as the menu is concerned. */
static Filesystem::AudioFormat normalized( Filesystem::AudioFormat format )
{
	if ( format == Filesystem::AudioFormat::Aif || format == Filesystem::AudioFormat::Aifc ) {
		return Filesystem::AudioFormat::Aiff;
	}
	return format;
}

static bool songUsesRubberband()
{
	const auto pSong = Hydrogen::get_instance()->getSong();
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		return false;
	}
	for ( const auto& pInstrument : *pSong->getDrumkit()->getInstruments() ) {
		if ( pInstrument == nullptr ) {
			continue;
		}
		for ( const auto& pComponent : *pInstrument ) {
			if ( pComponent == nullptr ) {
				continue;
			}
			for ( const auto& pLayer : *pComponent ) {
				if ( pLayer != nullptr && pLayer->getSample() != nullptr &&
					 pLayer->getSample()->getRubberband().bUse ) {
					return true;
				}
			}
		}
	}
	return false;
}

static bool instrumentHasNotes( const std::shared_ptr<Song>& pSong,
								const std::shared_ptr<Instrument>& pInstrument )
{
	for ( const auto& pNote : pSong->getAllNotes() ) {
		if ( pNote != nullptr && pNote->getInstrumentId() == pInstrument->getId() ) {
			return true;
		}
	}
	return false;
}

ExportSongWindow::ExportSongWindow( BRect parentFrame )
	: BWindow( BRect( 0, 0, 480, 300 ), B_TRANSLATE( "Export song" ), B_TITLED_WINDOW,
			   B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE )
{
	auto pHydrogen = Hydrogen::get_instance();
	const auto pSong = pHydrogen->getSong();
	const auto pPref = Preferences::get_instance();

	m_bOldTimeline = pSong != nullptr && pSong->getIsTimelineActivated();
	m_nOldRubberband = pPref->getRubberBandBatchMode();
	m_oldInterpolation = pHydrogen->getAudioEngine()->getSampler()->getInterpolateMode();

	m_pFileName = new BTextControl( "file", B_TRANSLATE( "File:" ), "", nullptr );
	m_pFileName->SetModificationMessage( new BMessage( kMsgNameChanged ) );
	m_pFileName->TextView()->SetExplicitMinSize( BSize( be_plain_font->StringWidth( "M" ) * 24, B_SIZE_UNSET ) );
	m_pChoose = new BButton( "choose", B_TRANSLATE( "Choose" B_UTF8_ELLIPSIS ),
							 new BMessage( kMsgChoose ) );

	const auto& supported = Filesystem::supportedAudioFormats();
	std::vector<BString> formatLabels;
	for ( const auto& info : kFormats ) {
		if ( std::find( supported.begin(), supported.end(), info.format ) != supported.end() ) {
			m_formats.push_back( info.format );
			formatLabels.push_back( info.sLabel );
		}
	}
	m_pFormat = menuField( "format", B_TRANSLATE( "Format:" ), formatLabels, kMsgFormat );

	std::vector<BString> rates, depths;
	for ( int nRate : kSampleRates ) {
		rates.push_back( BString() << nRate << " Hz" );
	}
	for ( int nDepth : kSampleDepths ) {
		depths.push_back( BString() << nDepth << " " << B_TRANSLATE( "bit" ) );
	}
	m_pSampleRate = menuField( "rate", B_TRANSLATE( "Sample rate:" ), rates, kMsgSettingChanged );
	m_pSampleDepth = menuField( "depth", B_TRANSLATE( "Sample depth:" ), depths, kMsgSettingChanged );
	m_pCompression = new BDecimalSpinner( "compression", B_TRANSLATE( "Compression:" ), nullptr );
	m_pCompression->SetMinValue( 0.0 );
	m_pCompression->SetMaxValue( 1.0 );
	m_pCompression->SetStep( 0.1 );
	m_pCompression->SetPrecision( 1 );
	m_pCompression->SetToolTip( B_TRANSLATE( "0 is the best quality, 1 the smallest file" ) );

	m_pMode = menuField( "mode", B_TRANSLATE( "Export:" ),
						 { B_TRANSLATE( "To a single track" ), B_TRANSLATE( "To separate tracks" ),
						   B_TRANSLATE( "Both" ) },
						 kMsgSettingChanged );
	m_pTimeline = new BCheckBox( "timeline", B_TRANSLATE( "Use the timeline's tempo changes" ),
								 new BMessage( kMsgTimeline ) );
	m_pRubberband = new BCheckBox( "rubberband", B_TRANSLATE( "Recalculate Rubberband samples" ),
								   new BMessage( kMsgRubberband ) );
	m_pInterpolation = menuField( "interpolation", B_TRANSLATE( "Resampling:" ),
								  { B_TRANSLATE( "Linear" ), B_TRANSLATE( "Cosine" ), B_TRANSLATE( "Third order" ),
									B_TRANSLATE( "Cubic" ), B_TRANSLATE( "Hermite" ) },
								  kMsgInterpolation );

	m_pProgress = new BStatusBar( "progress" );
	m_pProgress->SetMaxValue( 100 );
	m_pExport = new BButton( "export", B_TRANSLATE( "Export" ), new BMessage( kMsgExport ) );
	m_pClose = new BButton( "close", B_TRANSLATE( "Close" ), new BMessage( B_QUIT_REQUESTED ) );
	SetDefaultButton( m_pExport );

	BLayoutBuilder::Group<>( this, B_VERTICAL )
		.SetInsets( B_USE_WINDOW_INSETS )
		.AddGroup( B_HORIZONTAL )
			.Add( m_pFileName )
			.Add( m_pChoose )
		.End()
		.AddGrid()
			.AddMenuField( m_pFormat, 0, 0 )
			.AddMenuField( m_pSampleRate, 0, 1 )
			.AddMenuField( m_pSampleDepth, 0, 2 )
			.Add( m_pCompression->CreateLabelLayoutItem(), 0, 3 )
			.Add( m_pCompression->CreateTextViewLayoutItem(), 1, 3 )
			.AddMenuField( m_pMode, 0, 4 )
			.AddMenuField( m_pInterpolation, 0, 5 )
			.Add( m_pTimeline, 1, 6 )
			.Add( m_pRubberband, 1, 7 )
		.End()
		.Add( m_pProgress )
		.AddGroup( B_HORIZONTAL )
			.AddGlue()
			.Add( m_pClose )
			.Add( m_pExport )
		.End();

	// The settings of the last export.
	int nFormat = 0;
	const auto previous = normalized( pPref->getExportFormat() );
	for ( size_t i = 0; i < m_formats.size(); i++ ) {
		if ( m_formats[ i ] == previous ) {
			nFormat = static_cast<int>( i );
		}
	}
	mark( m_pFormat, nFormat );
	mark( m_pSampleRate, pPref->getExportSampleRateIdx() );
	mark( m_pSampleDepth, pPref->getExportSampleDepthIdx() );
	m_pCompression->SetValue( std::clamp( pPref->getExportCompressionLevel(), 0.0f, 1.0f ) );
	mark( m_pMode, pPref->getExportModeIdx() );
	mark( m_pInterpolation, static_cast<int32>( m_oldInterpolation ) );
	m_pTimeline->SetValue( m_bOldTimeline );
	m_pTimeline->SetEnabled( pSong != nullptr );
	if ( songUsesRubberband() ) {
		m_pRubberband->SetValue( m_nOldRubberband != 0 );
	}
	else {
		m_pRubberband->SetEnabled( false );
		m_pRubberband->SetToolTip( B_TRANSLATE( "No sample in the current song uses Rubberband" ) );
	}

	const QString sSuffix = Filesystem::AudioFormatToSuffix( selectedFormat() );
	m_pFileName->SetText( toBString( absolutePath( defaultBaseName() + "." + sSuffix,
												   pPref->getLastExportSongDirectory() ) ) );
	formatChanged();
	CenterIn( parentFrame );
}

ExportSongWindow::~ExportSongWindow()
{
	delete m_pPanel;
}

Filesystem::AudioFormat ExportSongWindow::selectedFormat() const
{
	const int32 nIndex = markedIndex( m_pFormat );
	if ( nIndex < 0 || nIndex >= static_cast<int32>( m_formats.size() ) ) {
		return Filesystem::AudioFormat::Unknown;
	}
	return m_formats[ nIndex ];
}

void ExportSongWindow::formatChanged()
{
	const auto format = selectedFormat();
	const bool bCompressed = isCompressed( format );
	m_pSampleRate->SetEnabled( ! bCompressed && ! m_bExporting );
	m_pSampleDepth->SetEnabled( ! bCompressed && ! m_bExporting );
	m_pCompression->SetEnabled( bCompressed && ! m_bExporting );

	// VOC can't hold more than 16 bit.
	const bool bVoc = format == Filesystem::AudioFormat::Voc;
	BMenu* pDepths = m_pSampleDepth->Menu();
	for ( int32 i = 2; i < pDepths->CountItems(); i++ ) {
		pDepths->ItemAt( i )->SetEnabled( ! bVoc );
	}
	if ( bVoc && markedIndex( m_pSampleDepth ) > 1 ) {
		mark( m_pSampleDepth, 1 );
	}

	// The file name follows the format.
	if ( format == Filesystem::AudioFormat::Unknown ) {
		return;
	}
	const QString sSuffix = Filesystem::AudioFormatToSuffix( format );
	QString sName = toQString( m_pFileName->Text() );
	if ( sName.isEmpty() ) {
		return;
	}
	const QFileInfo info( sName );
	if ( normalized( Filesystem::AudioFormatFromSuffix( sName, true ) ) == format ) {
		return;
	}
	if ( Filesystem::AudioFormatFromSuffix( sName, true ) != Filesystem::AudioFormat::Unknown ) {
		sName.chop( info.suffix().size() + 1 );
	}
	m_pFileName->SetText( toBString( sName + "." + sSuffix ) );
}

void ExportSongWindow::fileNameChanged()
{
	const QString sName = toQString( m_pFileName->Text() );
	const auto format = normalized( Filesystem::AudioFormatFromSuffix( sName, true ) );
	const bool bKnown = std::find( m_formats.begin(), m_formats.end(), format ) != m_formats.end();
	m_pExport->SetEnabled( bKnown && ! m_bExporting );
	if ( bKnown && format != selectedFormat() ) {
		mark( m_pFormat, static_cast<int32>(
			std::find( m_formats.begin(), m_formats.end(), format ) - m_formats.begin() ) );
		formatChanged();
	}
}

void ExportSongWindow::showPanel()
{
	if ( m_pPanel == nullptr ) {
		m_pPanel = new BFilePanel( B_SAVE_PANEL, new BMessenger( this ), nullptr, 0, false,
								   nullptr, nullptr, true );
		m_pPanel->Window()->SetTitle( B_TRANSLATE( "Export song" ) );
	}
	const QFileInfo info( toQString( m_pFileName->Text() ) );
	QString sDirectory = info.absolutePath();
	if ( ! QDir( sDirectory ).exists() ) {
		sDirectory = Preferences::get_instance()->getLastExportSongDirectory();
	}
	if ( ! Filesystem::dirWritable( sDirectory, true ) ) {
		sDirectory = QDir::homePath();
	}
	m_pPanel->SetPanelDirectory( sDirectory.toUtf8().constData() );
	m_pPanel->SetSaveText( toBString( info.fileName() ) );
	m_pPanel->Show();
}

bool ExportSongWindow::confirmReplace( const BString& sPath, bool bOfferAll )
{
	if ( m_bReplaceAll ) {
		return true;
	}
	BString sText( B_TRANSLATE( "The file \"%file%\" already exists. Do you want to replace it?" ) );
	sText.ReplaceFirst( "%file%", BPath( sPath ).Leaf() );
	BAlert* pAlert = new BAlert( B_TRANSLATE( "Export song" ), sText, B_TRANSLATE( "Cancel" ),
								 bOfferAll ? B_TRANSLATE( "Replace all" ) : nullptr,
								 B_TRANSLATE( "Replace" ), B_WIDTH_AS_USUAL, B_WARNING_ALERT );
	pAlert->SetShortcut( 0, B_ESCAPE );
	const int32 nChoice = pAlert->Go();
	if ( bOfferAll && nChoice == 1 ) {
		m_bReplaceAll = true;
	}
	// Without "Replace all", "Replace" is button 1.
	return bOfferAll ? nChoice != 0 : nChoice == 1;
}

void ExportSongWindow::startExport()
{
	auto pHydrogen = Hydrogen::get_instance();
	const auto pSong = pHydrogen->getSong();
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr || m_bExporting ) {
		return;
	}
	auto pPref = Preferences::get_instance();
	const auto format = selectedFormat();
	if ( format == Filesystem::AudioFormat::Unknown ) {
		return;
	}
	const QString sPath = absolutePath( toQString( m_pFileName->Text() ),
										pPref->getLastExportSongDirectory() );
	if ( ! checkFolder( sPath ) ) {
		return;
	}
	if ( toBString( sPath ) != m_pFileName->Text() ) {
		m_pFileName->SetText( toBString( sPath ) );
	}
	// The save panel asked about replacing that file already.
	const bool bFromPanel = toBString( sPath ) == m_sPanelPath;

	pPref->setLastExportSongDirectory( QFileInfo( sPath ).absolutePath() );
	pPref->setExportModeIdx( std::max<int32>( 0, markedIndex( m_pMode ) ) );
	pPref->setExportFormat( format );
	pPref->setExportCompressionLevel( m_pCompression->Value() );
	pPref->setExportSampleRateIdx( std::max<int32>( 0, markedIndex( m_pSampleRate ) ) );
	pPref->setExportSampleDepthIdx( std::max<int32>( 0, markedIndex( m_pSampleDepth ) ) );

	int nSampleRate = kSampleRates[ std::max<int32>( 0, markedIndex( m_pSampleRate ) ) ];
	int nSampleDepth = kSampleDepths[ std::max<int32>( 0, markedIndex( m_pSampleDepth ) ) ];
	// What the core's encoders accept (upstream's choices).
	if ( format == Filesystem::AudioFormat::Ogg || format == Filesystem::AudioFormat::Opus ) {
		nSampleDepth = 32;
		nSampleRate = 48000;
	}
	else if ( format == Filesystem::AudioFormat::Flac || format == Filesystem::AudioFormat::Mp3 ) {
		nSampleDepth = 16;
		nSampleRate = 48000;
	}
	else if ( format == Filesystem::AudioFormat::Voc ) {
		nSampleDepth = std::min( 16, nSampleDepth );
	}

	const int nMode = std::max<int32>( 0, markedIndex( m_pMode ) );
	m_bReplaceAll = false;
	m_nInstrument = 0;
	m_sTemplate = toBString( sPath );
	m_bTrackouts = nMode != kSingleTrack;

	// Asked before anything starts: the core can't be stopped midway.
	if ( nMode != kSeparateTracks && QFileInfo::exists( sPath ) && ! bFromPanel &&
		 ! confirmReplace( m_sTemplate, nMode == kBoth ) ) {
		return;
	}

	auto pInstruments = pSong->getDrumkit()->getInstruments();
	if ( nMode == kSeparateTracks ) {
		bool bAnyNotes = false;
		for ( const auto& pInstrument : *pInstruments ) {
			bAnyNotes = bAnyNotes || ( pInstrument != nullptr && instrumentHasNotes( pSong, pInstrument ) );
		}
		if ( ! bAnyNotes ) {
			warn( B_TRANSLATE( "No instrument of the song plays any notes." ) );
			return;
		}
	}

	if ( ! pHydrogen->startExportSession( nSampleRate, nSampleDepth, m_pCompression->Value() ) ) {
		warn( B_TRANSLATE( "The song couldn't be exported." ), B_STOP_ALERT );
		return;
	}
	m_bSessionActive = true;
	setExporting( true );
	m_pProgress->Reset();

	if ( nMode == kSeparateTracks ) {
		if ( ! exportNextTrack() ) {
			progress( 100 );
		}
		return;
	}
	for ( const auto& pInstrument : *pInstruments ) {
		if ( pInstrument != nullptr ) {
			pInstrument->setCurrentlyExported( true );
		}
	}
	pHydrogen->startExportSong( sPath );
}

bool ExportSongWindow::exportNextTrack()
{
	auto pHydrogen = Hydrogen::get_instance();
	const auto pSong = pHydrogen->getSong();
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		return false;
	}
	auto pInstruments = pSong->getDrumkit()->getInstruments();
	while ( m_nInstrument < pInstruments->size() ) {
		const auto pInstrument = pInstruments->get( m_nInstrument++ );
		if ( pInstrument == nullptr || ! instrumentHasNotes( pSong, pInstrument ) ) {
			continue;
		}

		// "song.flac" becomes "song-Kick.flac"; the ID tells instruments
		// of the same name apart.
		int nSameName = 0;
		for ( const auto& pOther : *pInstruments ) {
			if ( pOther != nullptr && pOther->getName() == pInstrument->getName() ) {
				nSameName++;
			}
		}
		QString sInstrument = pInstrument->getName();
		sInstrument.replace( '/', "_" );
		if ( nSameName > 1 ) {
			sInstrument += QString( "_%1" ).arg( static_cast<int>( pInstrument->getId() ) );
		}
		const QString sTemplate = toQString( m_sTemplate );
		const QFileInfo info( sTemplate );
		const QString sPath = QDir( info.absolutePath() ).absoluteFilePath(
			QString( "%1-%2.%3" ).arg( info.completeBaseName() ).arg( sInstrument ).arg( info.suffix() ) );

		if ( QFileInfo::exists( sPath ) && ! confirmReplace( toBString( sPath ), true ) ) {
			return false;
		}
		pHydrogen->stopExportSong();
		for ( const auto& pOther : *pInstruments ) {
			if ( pOther != nullptr ) {
				pOther->setCurrentlyExported( pOther == pInstrument );
			}
		}
		BString sStatus( B_TRANSLATE( "Exporting %instrument%" B_UTF8_ELLIPSIS ) );
		sStatus.ReplaceFirst( "%instrument%", toBString( pInstrument->getName() ) );
		m_pProgress->Reset( sStatus );
		pHydrogen->startExportSong( sPath );
		return true;
	}
	return false;
}

void ExportSongWindow::progress( int nValue )
{
	if ( ! m_bExporting ) {
		return;
	}
	if ( nValue < 0 ) {
		failed();
		return;
	}
	// Upstream clamped with min() here, which kept its bar at 0.
	const float fValue = std::clamp( nValue, 0, 100 );
	m_pProgress->Update( fValue - m_pProgress->CurrentValue() );
	if ( nValue < 100 ) {
		return;
	}

	auto pHydrogen = Hydrogen::get_instance();
	const auto pDriver =
		std::dynamic_pointer_cast<DiskWriterDriver>( pHydrogen->getAudioEngine()->getAudioDriver() );
	if ( pDriver != nullptr && pDriver->m_bWritingFailed ) {
		failed();
		return;
	}
	if ( m_bTrackouts && exportNextTrack() ) {
		return;
	}
	pHydrogen->stopExportSong();
	pHydrogen->stopExportSession();
	m_bSessionActive = false;
	setExporting( false );
	m_pProgress->Reset( B_TRANSLATE( "Done" ) );
	m_pProgress->Update( 100 );
}

void ExportSongWindow::failed()
{
	auto pHydrogen = Hydrogen::get_instance();
	pHydrogen->stopExportSong();
	if ( m_bSessionActive ) {
		pHydrogen->stopExportSession();
		m_bSessionActive = false;
	}
	setExporting( false );
	m_pProgress->Reset();
	warn( B_TRANSLATE( "The song couldn't be exported." ), B_STOP_ALERT );
}

void ExportSongWindow::setExporting( bool bExporting )
{
	m_bExporting = bExporting;
	m_pTimeline->SetEnabled( ! bExporting && Hydrogen::get_instance()->getSong() != nullptr );
	m_pClose->SetEnabled( ! bExporting );
	m_pChoose->SetEnabled( ! bExporting );
	m_pFileName->SetEnabled( ! bExporting );
	m_pFormat->SetEnabled( ! bExporting );
	m_pMode->SetEnabled( ! bExporting );
	m_pInterpolation->SetEnabled( ! bExporting );
	m_pRubberband->SetEnabled( ! bExporting && songUsesRubberband() );
	m_pExport->SetEnabled( ! bExporting );
	formatChanged();
	if ( ! bExporting ) {
		fileNameChanged();
	}
}

bool ExportSongWindow::QuitRequested()
{
	// The DiskWriter can't be interrupted.
	if ( m_bExporting ) {
		return false;
	}
	auto pHydrogen = Hydrogen::get_instance();
	auto pPref = Preferences::get_instance();
	const auto pSong = pHydrogen->getSong();
	if ( m_bSessionActive ) {
		pHydrogen->stopExportSong();
		pHydrogen->stopExportSession();
		m_bSessionActive = false;
	}
	if ( pPref->getRubberBandBatchMode() && pSong != nullptr && pSong->getDrumkit() != nullptr ) {
		pHydrogen->getAudioEngine()->lock( RIGHT_HERE );
		pSong->getDrumkit()->recalculateRubberband(
			pHydrogen->getAudioEngine()->getPlayhead()->getBpm() );
		pHydrogen->getAudioEngine()->unlock();
	}
	pPref->setRubberBandBatchMode( m_nOldRubberband );
	if ( pSong != nullptr ) {
		pHydrogen->setIsTimelineActivated( m_bOldTimeline );
	}
	pHydrogen->getAudioEngine()->getSampler()->setInterpolateMode( m_oldInterpolation );
	return true;
}

void ExportSongWindow::MessageReceived( BMessage* pMessage )
{
	switch ( pMessage->what ) {
	case kMsgNameChanged:
		fileNameChanged();
		break;
	case kMsgFormat:
		formatChanged();
		break;
	case kMsgSettingChanged:
		break;
	case kMsgChoose:
		showPanel();
		break;
	case B_SAVE_REQUESTED: {
		entry_ref directory;
		BString sName;
		if ( pMessage->FindRef( "directory", &directory ) != B_OK ||
			 pMessage->FindString( "name", &sName ) != B_OK ) {
			break;
		}
		const BString sSuffix = BString( "." ) << toBString(
			Filesystem::AudioFormatToSuffix( selectedFormat() ) );
		const bool bAddSuffix = ! sName.IEndsWith( sSuffix );
		if ( bAddSuffix ) {
			sName << sSuffix;
		}
		BPath path( &directory );
		path.Append( sName );
		m_pFileName->SetText( path.Path() );
		// The panel asked about replacing the file, unless we renamed it.
		m_sPanelPath = bAddSuffix ? BString() : BString( path.Path() );
		break;
	}
	case kMsgExport:
		startExport();
		break;
	case kMsgExportProgress: {
		int32 nValue = 0;
		if ( pMessage->FindInt32( "value", &nValue ) == B_OK ) {
			progress( nValue );
		}
		break;
	}
	case kMsgTimeline:
		Hydrogen::get_instance()->setIsTimelineActivated( m_pTimeline->Value() == B_CONTROL_ON );
		break;
	case kMsgRubberband:
		Preferences::get_instance()->setRubberBandBatchMode( m_pRubberband->Value() == B_CONTROL_ON );
		break;
	case kMsgInterpolation:
		Hydrogen::get_instance()->getAudioEngine()->getSampler()->setInterpolateMode(
			static_cast<Interpolation::InterpolateMode>( std::max<int32>( 0, markedIndex( m_pInterpolation ) ) ) );
		break;
	default:
		BWindow::MessageReceived( pMessage );
	}
}

// ---------------------------------------------------------------------------
// Export MIDI file

enum MidiExportMode { kSmf1Single, kSmf1Multi, kSmf0 };

ExportMidiWindow::ExportMidiWindow( BRect parentFrame )
	: BWindow( BRect( 0, 0, 460, 160 ), B_TRANSLATE( "Export MIDI file" ), B_TITLED_WINDOW,
			   B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE )
{
	const auto pPref = Preferences::get_instance();
	m_pFileName = new BTextControl( "file", B_TRANSLATE( "File:" ), "", nullptr );
	m_pFileName->TextView()->SetExplicitMinSize( BSize( be_plain_font->StringWidth( "M" ) * 24, B_SIZE_UNSET ) );
	BButton* pChoose = new BButton( "choose", B_TRANSLATE( "Choose" B_UTF8_ELLIPSIS ),
									new BMessage( kMsgChoose ) );
	m_pMode = menuField( "mode", B_TRANSLATE( "Format:" ),
						 { B_TRANSLATE( "SMF1, all instruments in one track" ),
						   B_TRANSLATE( "SMF1, one track per instrument" ),
						   B_TRANSLATE( "SMF0, all events in one track" ) },
						 kMsgSettingChanged );
	m_pHumanize = new BCheckBox( "humanize", B_TRANSLATE( "Include humanization" ), nullptr );
	m_pHumanize->SetToolTip( B_TRANSLATE( "Write the random swing, timing and velocity changes into the file" ) );
	BButton* pExport = new BButton( "export", B_TRANSLATE( "Export" ), new BMessage( kMsgExport ) );
	BButton* pClose = new BButton( "close", B_TRANSLATE( "Close" ), new BMessage( B_QUIT_REQUESTED ) );
	SetDefaultButton( pExport );

	BLayoutBuilder::Group<>( this, B_VERTICAL )
		.SetInsets( B_USE_WINDOW_INSETS )
		.AddGroup( B_HORIZONTAL )
			.Add( m_pFileName )
			.Add( pChoose )
		.End()
		.AddGrid()
			.AddMenuField( m_pMode, 0, 0 )
			.Add( m_pHumanize, 1, 1 )
		.End()
		.AddGroup( B_HORIZONTAL )
			.AddGlue()
			.Add( pClose )
			.Add( pExport )
		.End();

	mark( m_pMode, pPref->getMidiExportMode() );
	m_pHumanize->SetValue( pPref->getMidiExportUseHumanization() );
	QString sDirectory = pPref->getLastExportMidiDirectory();
	if ( sDirectory.isEmpty() ) {
		sDirectory = QDir::homePath();
	}
	m_pFileName->SetText( toBString( absolutePath( defaultBaseName() + ".mid", sDirectory ) ) );
	CenterIn( parentFrame );
}

ExportMidiWindow::~ExportMidiWindow()
{
	delete m_pPanel;
}

void ExportMidiWindow::exportFile()
{
	const auto pSong = Hydrogen::get_instance()->getSong();
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		return;
	}
	auto pPref = Preferences::get_instance();
	QString sPath = absolutePath( toQString( m_pFileName->Text() ), pPref->getLastExportMidiDirectory() );
	if ( QFileInfo( sPath ).suffix().isEmpty() ) {
		sPath += ".mid";
	}
	if ( ! checkFolder( sPath ) ) {
		return;
	}
	if ( QFileInfo::exists( sPath ) && toBString( sPath ) != m_sPanelPath ) {
		BString sText( B_TRANSLATE( "The file \"%file%\" already exists. Do you want to replace it?" ) );
		sText.ReplaceFirst( "%file%", toBString( QFileInfo( sPath ).fileName() ) );
		BAlert* pAlert = new BAlert( B_TRANSLATE( "Export MIDI file" ), sText, B_TRANSLATE( "Cancel" ),
									 B_TRANSLATE( "Replace" ), nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT );
		pAlert->SetShortcut( 0, B_ESCAPE );
		if ( pAlert->Go() != 1 ) {
			return;
		}
	}

	const int nMode = std::max<int32>( 0, markedIndex( m_pMode ) );
	std::shared_ptr<SMFWriter> pWriter;
	switch ( nMode ) {
	case kSmf1Multi:
		pWriter = std::make_shared<SMF1WriterMulti>();
		break;
	case kSmf0:
		pWriter = std::make_shared<SMF0Writer>();
		break;
	case kSmf1Single:
	default:
		pWriter = std::make_shared<SMF1WriterSingle>();
		break;
	}
	const bool bHumanize = m_pHumanize->Value() == B_CONTROL_ON;
	pPref->setMidiExportMode( nMode );
	pPref->setMidiExportUseHumanization( bHumanize );
	pPref->setLastExportMidiDirectory( QFileInfo( sPath ).absolutePath() );
	pWriter->save( sPath, pSong, bHumanize );

	if ( ! QFileInfo::exists( sPath ) ) {
		warn( B_TRANSLATE( "The MIDI file couldn't be written." ), B_STOP_ALERT );
		return;
	}
	// SMF needs whole numerators and power-of-two denominators.
	const auto& failures = pWriter->getTimeSignatureFailures();
	if ( ! failures.empty() ) {
		BString sText( B_TRANSLATE( "The time signature of some columns had to be changed: a MIDI file "
									"needs a whole number on top and a power of two (1, 2, 4, 8…) below." ) );
		sText << "\n";
		for ( const auto& failure : failures ) {
			sText << "\n" << B_TRANSLATE( "Column" ) << " " << failure.nColumn << ": "
				  << failure.fOldNumerator << "/" << failure.nOldDenominator << " → "
				  << failure.nNewNumerator << "/" << failure.nNewDenominator;
		}
		warn( sText );
	}
	Quit();
}

void ExportMidiWindow::MessageReceived( BMessage* pMessage )
{
	switch ( pMessage->what ) {
	case kMsgSettingChanged:
		break;
	case kMsgChoose: {
		if ( m_pPanel == nullptr ) {
			m_pPanel = new BFilePanel( B_SAVE_PANEL, new BMessenger( this ), nullptr, 0, false,
									   nullptr, nullptr, true );
			m_pPanel->Window()->SetTitle( B_TRANSLATE( "Export MIDI file" ) );
		}
		const QFileInfo info( toQString( m_pFileName->Text() ) );
		QString sDirectory = info.absolutePath();
		if ( ! Filesystem::dirWritable( sDirectory, true ) ) {
			sDirectory = QDir::homePath();
		}
		m_pPanel->SetPanelDirectory( sDirectory.toUtf8().constData() );
		m_pPanel->SetSaveText( toBString( info.fileName() ) );
		m_pPanel->Show();
		break;
	}
	case B_SAVE_REQUESTED: {
		entry_ref directory;
		BString sName;
		if ( pMessage->FindRef( "directory", &directory ) != B_OK ||
			 pMessage->FindString( "name", &sName ) != B_OK ) {
			break;
		}
		const bool bAddSuffix = ! sName.IEndsWith( ".mid" ) && ! sName.IEndsWith( ".midi" );
		if ( bAddSuffix ) {
			sName << ".mid";
		}
		BPath path( &directory );
		path.Append( sName );
		m_pFileName->SetText( path.Path() );
		// The panel asked about replacing it, unless we changed the name.
		m_sPanelPath = bAddSuffix ? BString() : BString( path.Path() );
		break;
	}
	case kMsgExport:
		exportFile();
		break;
	default:
		BWindow::MessageReceived( pMessage );
	}
}
