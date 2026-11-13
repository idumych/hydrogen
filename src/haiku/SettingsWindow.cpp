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

#include "SettingsWindow.h"

#include <cmath>

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TabView.h>
#include <TextControl.h>

#include <core/AudioEngine/AudioEngine.h>
#include <core/Basics/Instrument.h>
#include <core/Basics/Event.h>
#include <core/CoreActionController.h>
#include <core/EventQueue.h>
#include <core/Hydrogen.h>
#include <core/IO/AudioDriver.h>
#include <core/IO/MediaKitDriver.h>
#include <core/IO/MidiBaseDriver.h>
#include <core/IO/NullDriver.h>
#include <core/Midi/Midi.h>
#include <core/Midi/MidiInstrumentMap.h>
#include <core/Preferences/Preferences.h>
#include <core/Preferences/Theme.h>
#include <core/Sampler/Interpolation.h>
#include <core/Sampler/Sampler.h>

#include "Colors.h"
#include "Controls.h"
#include "MidiActionTable.h"
#include "Settings.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SettingsWindow"

using namespace H2Core;

enum : uint32 {
	kMsgChanged = 'STch',			// a live setting changed
	kMsgAudioChanged = 'STac',		// needs a driver restart
	kMsgMidiChanged = 'STmc',
	kMsgApplyAudio = 'STaa',
	kMsgApplyMidi = 'STam',
	kMsgRevert = 'STrv',
	kMsgDefaults = 'STdf',
};

static const char* kFrameName = "settings frame";
static const int kSampleRates[] = { 44100, 48000, 88200, 96000 };

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

/** Everything the window shows, so Revert and Defaults can restore it. */
struct SettingsValues {
	bool bRelativePaths;
	int nMaxBars;
	int nAutosaves;
	QString sRubberband;
	Preferences::AudioDriver audioDriver;
	unsigned nBufferSize, nSampleRate, nPolyphony;
	float fMetronome;
	Preferences::MidiDriver midiDriver;
	QString sMidiInput, sMidiOutput;
	MidiInstrumentMap::Input noteMapping;
	bool bGlobalChannel;
	Midi::Channel noteChannel;
	bool bIgnoreNoteOff, bClockInput, bTransportInput, bFeedback, bClockOutput, bTransportOutput;
	Midi::Channel actionChannel, feedbackChannel;
	Preferences::MidiSendNoteOff noteOff;
	Colors::Source colors;
	float fFalloff;
	InterfaceTheme::ColoringMethod coloring;
	int nPatternColors;

	static SettingsValues from( const Preferences& p )
	{
		SettingsValues v;
		v.bRelativePaths = p.getUseRelativeFileNamesForPlaylists();
		v.nMaxBars = p.getMaxBars();
		v.nAutosaves = p.m_nAutosavesPerHour;
		v.sRubberband = p.m_sRubberBandCLIexecutable;
		v.audioDriver = p.m_audioDriver;
		v.nBufferSize = p.m_nBufferSize;
		v.nSampleRate = p.m_nSampleRate;
		v.nPolyphony = p.m_nMaxNotes;
		v.fMetronome = p.m_fMetronomeVolume;
		v.midiDriver = p.m_midiDriver;
		v.sMidiInput = p.m_sMidiPortName;
		v.sMidiOutput = p.m_sMidiOutputPortName;
		auto pMap = p.getMidiInstrumentMap();
		v.noteMapping = pMap != nullptr ? pMap->getInput() : MidiInstrumentMap::Input::AsOutput;
		v.bGlobalChannel = pMap != nullptr && pMap->getUseGlobalInputChannel();
		v.noteChannel = pMap != nullptr ? pMap->getGlobalInputChannel() : Midi::ChannelAll;
		v.bIgnoreNoteOff = p.m_bMidiNoteOffIgnore;
		v.bClockInput = p.getMidiClockInputHandling();
		v.bTransportInput = p.getMidiTransportInputHandling();
		v.bFeedback = p.m_bEnableMidiFeedback;
		v.bClockOutput = p.getMidiClockOutputSend();
		v.bTransportOutput = p.getMidiTransportOutputSend();
		v.actionChannel = p.m_midiActionChannel;
		v.feedbackChannel = p.getMidiFeedbackChannel();
		v.noteOff = p.getMidiSendNoteOff();
		v.colors = Colors::source();
		auto pInterface = p.getInterfaceTheme();
		v.fFalloff = pInterface != nullptr ? pInterface->m_fMixerFalloffSpeed : InterfaceTheme::FALLOFF_NORMAL;
		v.coloring = pInterface != nullptr ? pInterface->m_coloringMethod : InterfaceTheme::ColoringMethod::Custom;
		v.nPatternColors = pInterface != nullptr ? pInterface->m_nVisiblePatternColors : 18;
		return v;
	}
};

static BMenuField* menuField( const char* sName, const char* sLabel,
							  std::initializer_list<const char*> items, uint32 what )
{
	BPopUpMenu* pMenu = new BPopUpMenu( sName );
	int32 nIndex = 0;
	for ( const char* sItem : items ) {
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
}

SettingsWindow::SettingsWindow()
	: BWindow( Settings::frame( kFrameName, BRect( 0, 0, 480, 420 ) ), B_TRANSLATE( "Hydrogen settings" ),
			   B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE )
{
	BTabView* pTabs = new BTabView( "tabs", B_WIDTH_FROM_LABEL );
	pTabs->AddTab( buildGeneral() );
	pTabs->AddTab( buildAudio() );
	pTabs->AddTab( buildMidi() );
	m_pMidiActions = new MidiActionTable();
	pTabs->AddTab( m_pMidiActions );
	pTabs->AddTab( buildAppearance() );

	m_pRevert = new BButton( "revert", B_TRANSLATE( "Revert" ), new BMessage( kMsgRevert ) );
	m_pDefaults = new BButton( "defaults", B_TRANSLATE( "Defaults" ), new BMessage( kMsgDefaults ) );
	BLayoutBuilder::Group<>( this, B_VERTICAL )
		.SetInsets( B_USE_WINDOW_INSETS )
		.Add( pTabs )
		.AddGroup( B_HORIZONTAL )
			.Add( m_pDefaults )
			.Add( m_pRevert )
			.AddGlue()
		.End();
	if ( ! Settings::get().HasRect( kFrameName ) ) {
		CenterOnScreen();
	}

	m_pOriginal = std::make_unique<SettingsValues>( SettingsValues::from( *Preferences::get_instance() ) );
	load();
}

SettingsWindow::~SettingsWindow() = default;

BView* SettingsWindow::buildGeneral()
{
	m_pRelativePaths = new BCheckBox( "relative", B_TRANSLATE( "Use relative paths in playlists" ),
									  new BMessage( kMsgChanged ) );
	m_pMaxBars = new BSpinner( "max bars", B_TRANSLATE( "Maximum number of bars:" ), new BMessage( kMsgChanged ) );
	m_pMaxBars->SetRange( 1, 800 );
	// Upstream allows at most one autosave a second.
	m_pAutosaves = new FormattedSpinner( "autosaves", B_TRANSLATE( "Autosaves per hour:" ), new BMessage( kMsgChanged ),
										 FormattedSpinner::offAt( 0 ) );
	m_pAutosaves->SetRange( 0, 360 );
	m_pAutosaves->SetToolTip( B_TRANSLATE( "How often unsaved changes are saved to a hidden file next to the song" ) );
	m_pRubberband = new BTextControl( "rubberband", B_TRANSLATE( "Rubberband program:" ), "",
									  new BMessage( kMsgChanged ) );
	m_pRubberband->SetToolTip( B_TRANSLATE( "The rubberband command-line tool, used to fit samples to the tempo" ) );

	BGroupView* pView = new BGroupView( B_TRANSLATE( "General" ), B_VERTICAL );
	BLayoutBuilder::Group<>( pView )
		.SetInsets( B_USE_DEFAULT_SPACING )
		.Add( m_pRelativePaths )
		.Add( m_pMaxBars )
		.Add( m_pAutosaves )
		.Add( m_pRubberband )
		.AddGlue();
	return pView;
}

BView* SettingsWindow::buildAudio()
{
	BPopUpMenu* pDriverMenu = new BPopUpMenu( "audio driver" );
	auto addDriver = [&]( Preferences::AudioDriver driver ) {
		BMessage* pMessage = new BMessage( kMsgAudioChanged );
		pMessage->AddInt32( "driver", static_cast<int32>( driver ) );
		pDriverMenu->AddItem( new BMenuItem( toBString( Preferences::audioDriverToQString( driver ) ), pMessage ) );
	};
	addDriver( Preferences::AudioDriver::Auto );
	for ( auto driver : Preferences::getSupportedAudioDrivers() ) {
		addDriver( driver );
	}
	m_pAudioDriver = new BMenuField( "audio driver", B_TRANSLATE( "Audio driver:" ), pDriverMenu );
	m_pBufferSize = new BSpinner( "buffer", B_TRANSLATE( "Buffer size:" ), new BMessage( kMsgAudioChanged ) );
	m_pBufferSize->SetRange( 16, 5000 );
	m_pBufferSize->SetToolTip( B_TRANSLATE( "Frames per audio buffer; the Media Kit may use a different size" ) );
	m_pSampleRate = menuField( "sample rate", B_TRANSLATE( "Sample rate:" ),
							   { "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz" }, kMsgAudioChanged );
	BButton* pApply = new BButton( "apply audio", B_TRANSLATE( "Apply and restart driver" ),
								   new BMessage( kMsgApplyAudio ) );
	m_pAudioStatus = new BStringView( "audio status", "" );

	m_pPolyphony = new BSpinner( "polyphony", B_TRANSLATE( "Polyphony:" ), new BMessage( kMsgChanged ) );
	m_pPolyphony->SetRange( 1, 512 );
	m_pPolyphony->SetToolTip( B_TRANSLATE( "Notes that can sound at the same time" ) );
	m_pMetronome = new BSpinner( "metronome", B_TRANSLATE( "Metronome volume:" ), new BMessage( kMsgChanged ) );
	m_pMetronome->SetRange( 1, 100 );
	m_pInterpolation = menuField( "interpolation", B_TRANSLATE( "Resampling:" ),
								  { B_TRANSLATE( "Linear" ), B_TRANSLATE( "Cosine" ), B_TRANSLATE( "Third order" ),
									B_TRANSLATE( "Cubic" ), B_TRANSLATE( "Hermite" ) }, kMsgChanged );

	BBox* pDriverBox = new BBox( "driver box" );
	pDriverBox->SetLabel( B_TRANSLATE( "Driver" ) );
	BGroupView* pDriver = new BGroupView( B_VERTICAL );
	BLayoutBuilder::Group<>( pDriver )
		.SetInsets( B_USE_DEFAULT_SPACING )
		.AddGrid()
			.AddMenuField( m_pAudioDriver, 0, 0 )
			.Add( m_pBufferSize->CreateLabelLayoutItem(), 0, 1 )
			.Add( m_pBufferSize->CreateTextViewLayoutItem(), 1, 1 )
			.AddMenuField( m_pSampleRate, 0, 2 )
		.End()
		.Add( m_pAudioStatus )
		.AddGroup( B_HORIZONTAL )
			.AddGlue()
			.Add( pApply )
		.End();
	pDriverBox->AddChild( pDriver );

	BGroupView* pView = new BGroupView( B_TRANSLATE( "Audio" ), B_VERTICAL );
	BLayoutBuilder::Group<>( pView )
		.SetInsets( B_USE_DEFAULT_SPACING )
		.Add( pDriverBox )
		.AddGrid()
			.Add( m_pPolyphony->CreateLabelLayoutItem(), 0, 0 )
			.Add( m_pPolyphony->CreateTextViewLayoutItem(), 1, 0 )
			.Add( m_pMetronome->CreateLabelLayoutItem(), 0, 1 )
			.Add( m_pMetronome->CreateTextViewLayoutItem(), 1, 1 )
			.AddMenuField( m_pInterpolation, 0, 2 )
		.End()
		.AddGlue();
	return pView;
}

BView* SettingsWindow::buildMidi()
{
	BPopUpMenu* pDriverMenu = new BPopUpMenu( "midi driver" );
	for ( auto driver : { Preferences::MidiDriver::MidiKit, Preferences::MidiDriver::None } ) {
		BMessage* pMessage = new BMessage( kMsgMidiChanged );
		pMessage->AddInt32( "driver", static_cast<int32>( driver ) );
		const BString sName = driver == Preferences::MidiDriver::None
			? BString( B_TRANSLATE( "None" ) ) : toBString( Preferences::midiDriverToQString( driver ) );
		pDriverMenu->AddItem( new BMenuItem( sName, pMessage ) );
	}
	m_pMidiDriver = new BMenuField( "midi driver", B_TRANSLATE( "MIDI driver:" ), pDriverMenu );
	m_pMidiInput = new BMenuField( "midi input", B_TRANSLATE( "Input:" ), new BPopUpMenu( "input" ) );
	m_pMidiOutput = new BMenuField( "midi output", B_TRANSLATE( "Output:" ), new BPopUpMenu( "output" ) );
	BButton* pApply = new BButton( "apply midi", B_TRANSLATE( "Apply and restart driver" ),
								   new BMessage( kMsgApplyMidi ) );

	// Upstream's MIDI control dialog: how incoming notes reach instruments.
	m_pNoteMapping = menuField( "note mapping", B_TRANSLATE( "Notes play:" ),
								{ B_TRANSLATE( "No instrument" ), B_TRANSLATE( "The instrument's output note" ),
								  B_TRANSLATE( "Custom mapping" ), B_TRANSLATE( "The selected instrument" ),
								  B_TRANSLATE( "Instruments in order" ) }, kMsgChanged );
	BPopUpMenu* pChannelMenu = new BPopUpMenu( "note channel" );
	for ( int32 i = -2; i <= 16; i++ ) {
		BMessage* pMessage = new BMessage( kMsgChanged );
		pMessage->AddInt32( "channel", i );
		BString sLabel;
		if ( i == -2 ) {
			sLabel = B_TRANSLATE( "Each instrument's own" );
		}
		else if ( i == -1 ) {
			sLabel = B_TRANSLATE( "All" );
		}
		else if ( i == 0 ) {
			continue;
		}
		else {
			sLabel << i;
		}
		pChannelMenu->AddItem( new BMenuItem( sLabel, pMessage ) );
	}
	m_pNoteChannel = new BMenuField( "note channel", B_TRANSLATE( "Note channel:" ), pChannelMenu );
	m_pIgnoreNoteOff = new BCheckBox( "ignore note off", B_TRANSLATE( "Ignore note-off" ), new BMessage( kMsgChanged ) );

	m_pClockInput = new BCheckBox( "clock in", B_TRANSLATE( "Follow MIDI clock" ), new BMessage( kMsgChanged ) );
	m_pTransportInput = new BCheckBox( "transport in", B_TRANSLATE( "Follow start, stop, continue and song position" ),
									   new BMessage( kMsgChanged ) );
	auto allOrOff = []( int32 n ) {
		BString s;
		if ( n == -1 ) {
			s = B_TRANSLATE( "All" );
		}
		else if ( n == 0 ) {
			s = B_TRANSLATE( "Off" );
		}
		else {
			s << n;
		}
		return s;
	};
	m_pActionChannel = new FormattedSpinner( "action channel", B_TRANSLATE( "Channel for actions and clock:" ),
											 new BMessage( kMsgChanged ), allOrOff );
	m_pActionChannel->SetRange( -1, 16 );
	m_pFeedback = new BCheckBox( "feedback", B_TRANSLATE( "Send MIDI feedback" ), new BMessage( kMsgChanged ) );
	m_pClockOutput = new BCheckBox( "clock out", B_TRANSLATE( "Send MIDI clock" ), new BMessage( kMsgChanged ) );
	m_pTransportOutput = new BCheckBox( "transport out", B_TRANSLATE( "Send start, stop, continue and song position" ),
										new BMessage( kMsgChanged ) );
	m_pFeedbackChannel = new FormattedSpinner( "feedback channel", B_TRANSLATE( "Channel for feedback and clock:" ),
											   new BMessage( kMsgChanged ), allOrOff );
	m_pFeedbackChannel->SetRange( 0, 16 );
	m_pNoteOff = menuField( "note off", B_TRANSLATE( "Send note-off:" ),
							{ B_TRANSLATE( "Always" ), B_TRANSLATE( "For notes with a length" ), B_TRANSLATE( "Never" ) },
							kMsgChanged );

	BBox* pDriverBox = new BBox( "midi driver box" );
	pDriverBox->SetLabel( B_TRANSLATE( "Driver" ) );
	BGroupView* pDriver = new BGroupView( B_VERTICAL );
	BLayoutBuilder::Group<>( pDriver )
		.SetInsets( B_USE_DEFAULT_SPACING )
		.AddGrid()
			.AddMenuField( m_pMidiDriver, 0, 0 )
			.AddMenuField( m_pMidiInput, 0, 1 )
			.AddMenuField( m_pMidiOutput, 0, 2 )
		.End()
		.AddGroup( B_HORIZONTAL )
			.AddGlue()
			.Add( pApply )
		.End();
	pDriverBox->AddChild( pDriver );

	BGroupView* pView = new BGroupView( B_TRANSLATE( "MIDI" ), B_VERTICAL );
	BLayoutBuilder::Group<>( pView )
		.SetInsets( B_USE_DEFAULT_SPACING )
		.Add( pDriverBox )
		.AddGrid()
			.AddMenuField( m_pNoteMapping, 0, 0 )
			.AddMenuField( m_pNoteChannel, 0, 1 )
			.AddMenuField( m_pNoteOff, 0, 2 )
		.End()
		.Add( m_pIgnoreNoteOff )
		.AddGroup( B_HORIZONTAL )
			.AddGroup( B_VERTICAL, B_USE_HALF_ITEM_SPACING )
				.Add( m_pClockInput )
				.Add( m_pTransportInput )
				.Add( m_pActionChannel )
			.End()
			.AddGroup( B_VERTICAL, B_USE_HALF_ITEM_SPACING )
				.Add( m_pFeedback )
				.Add( m_pClockOutput )
				.Add( m_pTransportOutput )
				.Add( m_pFeedbackChannel )
			.End()
		.End()
		.AddGlue();
	return pView;
}

BView* SettingsWindow::buildAppearance()
{
	m_pSystemColors = new BCheckBox( "system colors", B_TRANSLATE( "Use system colors in editors" ),
									 new BMessage( kMsgChanged ) );
	m_pSystemColors->SetToolTip( B_TRANSLATE( "Otherwise the editors use Hydrogen's colour theme" ) );
	m_pFalloff = menuField( "falloff", B_TRANSLATE( "Meter falloff:" ),
							{ B_TRANSLATE( "Slow" ), B_TRANSLATE( "Normal" ), B_TRANSLATE( "Fast" ) }, kMsgChanged );
	m_pColoring = menuField( "coloring", B_TRANSLATE( "Song editor colors:" ),
							 { B_TRANSLATE( "Automatic" ), B_TRANSLATE( "From the palette" ) }, kMsgChanged );
	m_pPatternColors = new BSpinner( "pattern colors", B_TRANSLATE( "Palette colors used:" ),
									 new BMessage( kMsgChanged ) );
	m_pPatternColors->SetRange( 1, InterfaceTheme::nMaxPatternColors );

	BGroupView* pView = new BGroupView( B_TRANSLATE( "Appearance" ), B_VERTICAL );
	BLayoutBuilder::Group<>( pView )
		.SetInsets( B_USE_DEFAULT_SPACING )
		.Add( m_pSystemColors )
		.AddGrid()
			.AddMenuField( m_pFalloff, 0, 0 )
			.AddMenuField( m_pColoring, 0, 1 )
			.Add( m_pPatternColors->CreateLabelLayoutItem(), 0, 2 )
			.Add( m_pPatternColors->CreateTextViewLayoutItem(), 1, 2 )
		.End()
		.AddGlue();
	return pView;
}

void SettingsWindow::load()
{
	m_bLoading = true;
	auto pPref = Preferences::get_instance();
	const SettingsValues v = SettingsValues::from( *pPref );

	m_pRelativePaths->SetValue( v.bRelativePaths );
	m_pMaxBars->SetValue( v.nMaxBars );
	m_pAutosaves->SetValue( v.nAutosaves );
	m_pRubberband->SetText( toBString( v.sRubberband ) );

	for ( int32 i = 0; i < m_pAudioDriver->Menu()->CountItems(); i++ ) {
		BMenuItem* pItem = m_pAudioDriver->Menu()->ItemAt( i );
		int32 nDriver = -1;
		pItem->Message()->FindInt32( "driver", &nDriver );
		pItem->SetMarked( nDriver == static_cast<int32>( v.audioDriver ) );
	}
	m_pBufferSize->SetValue( v.nBufferSize );
	for ( int32 i = 0; i < 4; i++ ) {
		if ( kSampleRates[ i ] == (int) v.nSampleRate ) {
			mark( m_pSampleRate, i );
		}
	}
	m_pPolyphony->SetValue( v.nPolyphony );
	m_pMetronome->SetValue( (int) std::round( v.fMetronome * 100 ) );
	mark( m_pInterpolation, static_cast<int32>(
		Hydrogen::get_instance()->getAudioEngine()->getSampler()->getInterpolateMode() ) );

	for ( int32 i = 0; i < m_pMidiDriver->Menu()->CountItems(); i++ ) {
		BMenuItem* pItem = m_pMidiDriver->Menu()->ItemAt( i );
		int32 nDriver = -1;
		pItem->Message()->FindInt32( "driver", &nDriver );
		pItem->SetMarked( nDriver == static_cast<int32>( v.midiDriver ) );
	}
	updateMidiPorts();
	mark( m_pNoteMapping, static_cast<int32>( v.noteMapping ) );
	const int32 nChannel = ! v.bGlobalChannel ? -2 : static_cast<int32>( v.noteChannel );
	for ( int32 i = 0; i < m_pNoteChannel->Menu()->CountItems(); i++ ) {
		BMenuItem* pItem = m_pNoteChannel->Menu()->ItemAt( i );
		int32 n = 0;
		pItem->Message()->FindInt32( "channel", &n );
		pItem->SetMarked( n == nChannel );
	}
	m_pIgnoreNoteOff->SetValue( v.bIgnoreNoteOff );
	m_pClockInput->SetValue( v.bClockInput );
	m_pTransportInput->SetValue( v.bTransportInput );
	m_pActionChannel->SetValue( static_cast<int32>( v.actionChannel ) );
	m_pFeedback->SetValue( v.bFeedback );
	m_pClockOutput->SetValue( v.bClockOutput );
	m_pTransportOutput->SetValue( v.bTransportOutput );
	m_pFeedbackChannel->SetValue( std::max( 0, static_cast<int32>( v.feedbackChannel ) ) );
	mark( m_pNoteOff, static_cast<int32>( v.noteOff ) );

	m_pSystemColors->SetValue( v.colors == Colors::Source::System );
	mark( m_pFalloff, v.fFalloff <= InterfaceTheme::FALLOFF_SLOW ? 0 : v.fFalloff >= InterfaceTheme::FALLOFF_FAST ? 2 : 1 );
	mark( m_pColoring, v.coloring == InterfaceTheme::ColoringMethod::Automatic ? 0 : 1 );
	m_pPatternColors->SetValue( v.nPatternColors );
	m_pPatternColors->SetEnabled( v.coloring != InterfaceTheme::ColoringMethod::Automatic );

	updateAudioStatus();
	updateButtons();
	m_bLoading = false;
}

void SettingsWindow::updateAudioStatus()
{
	auto pDriver = Hydrogen::get_instance()->getAudioDriver();
	BString sStatus;
	if ( pDriver == nullptr || std::dynamic_pointer_cast<NullDriver>( pDriver ) != nullptr ) {
		sStatus = B_TRANSLATE( "No audio driver is running." );
	}
	else {
		const char* sName = std::dynamic_pointer_cast<MediaKitDriver>( pDriver ) != nullptr
			? "Media Kit" : B_TRANSLATE( "Audio driver" );
		const float fLatency = pDriver->getSampleRate() > 0
			? 1000.0f * pDriver->getLatency() / pDriver->getSampleRate() : 0;
		sStatus.SetToFormat( B_TRANSLATE( "%s running: %u Hz, %u frames, %.0f ms latency" ), sName,
							 pDriver->getSampleRate(), pDriver->getBufferSize(), fLatency );
	}
	m_pAudioStatus->SetText( sStatus );
}

void SettingsWindow::updateMidiPorts()
{
	auto pPref = Preferences::get_instance();
	auto pDriver = Hydrogen::get_instance()->getMidiDriver();
	// Hydrogen's input connects to other programs' outputs and vice versa.
	auto fill = [&]( BMenuField* pField, MidiBaseDriver::PortType type, const QString& sCurrent ) {
		BMenu* pMenu = pField->Menu();
		pMenu->RemoveItems( 0, pMenu->CountItems(), true );
		auto addPort = [&]( const BString& sLabel, const QString& sValue ) {
			BMessage* pMessage = new BMessage( kMsgMidiChanged );
			pMessage->AddString( "port", toBString( sValue ) );
			BMenuItem* pItem = new BMenuItem( sLabel, pMessage );
			pItem->SetMarked( sValue == sCurrent );
			pItem->SetTarget( this );
			pMenu->AddItem( pItem );
		};
		addPort( B_TRANSLATE( "None" ), Preferences::getNullMidiPort() );
		if ( pDriver != nullptr ) {
			for ( const QString& sPort : pDriver->getExternalPortList( type ) ) {
				addPort( toBString( sPort ), sPort );
			}
		}
		if ( pMenu->FindMarked() == nullptr ) {
			pMenu->ItemAt( 0 )->SetMarked( true );
		}
	};
	fill( m_pMidiInput, MidiBaseDriver::PortType::Output, pPref->m_sMidiPortName );
	fill( m_pMidiOutput, MidiBaseDriver::PortType::Input, pPref->m_sMidiOutputPortName );
}

void SettingsWindow::apply( const SettingsValues& v )
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pPref = Preferences::get_instance();
	const bool bAudioRestart = v.audioDriver != pPref->m_audioDriver ||
		v.nBufferSize != pPref->m_nBufferSize || v.nSampleRate != pPref->m_nSampleRate;
	const bool bMidiRestart = v.midiDriver != pPref->m_midiDriver ||
		v.sMidiInput != pPref->m_sMidiPortName || v.sMidiOutput != pPref->m_sMidiOutputPortName;

	pPref->setUseRelativeFileNamesForPlaylists( v.bRelativePaths );
	pPref->setMaxBars( v.nMaxBars );
	pPref->m_nAutosavesPerHour = v.nAutosaves;
	pPref->m_sRubberBandCLIexecutable = v.sRubberband;
	pPref->m_audioDriver = v.audioDriver;
	pPref->m_nBufferSize = v.nBufferSize;
	pPref->m_nSampleRate = v.nSampleRate;
	pPref->m_nMaxNotes = v.nPolyphony;
	pPref->m_fMetronomeVolume = v.fMetronome;
	if ( auto pMetronome = pHydrogen->getAudioEngine()->getMetronomeInstrument() ) {
		pMetronome->setVolume( v.fMetronome );
	}
	pPref->m_midiDriver = v.midiDriver;
	pPref->m_sMidiPortName = v.sMidiInput;
	pPref->m_sMidiOutputPortName = v.sMidiOutput;
	if ( auto pMap = pPref->getMidiInstrumentMap() ) {
		pMap->setInput( v.noteMapping );
		pMap->setUseGlobalInputChannel( v.bGlobalChannel );
		pMap->setGlobalInputChannel( v.noteChannel );
	}
	pPref->m_bMidiNoteOffIgnore = v.bIgnoreNoteOff;
	CoreActionController::setMidiClockInputHandling( v.bClockInput );
	pPref->setMidiTransportInputHandling( v.bTransportInput );
	pPref->m_bEnableMidiFeedback = v.bFeedback;
	CoreActionController::setMidiClockOutputSend( v.bClockOutput );
	pPref->setMidiTransportOutputSend( v.bTransportOutput );
	pPref->m_midiActionChannel = v.actionChannel;
	pPref->setMidiFeedbackChannel( v.feedbackChannel );
	pPref->setMidiSendNoteOff( v.noteOff );
	Colors::setSource( v.colors );
	auto pTheme = pPref->getThemeWritable();
	pTheme->m_pInterface->m_fMixerFalloffSpeed = v.fFalloff;
	pTheme->m_pInterface->m_coloringMethod = v.coloring;
	pTheme->m_pInterface->m_nVisiblePatternColors = v.nPatternColors;
	Colors::invalidate();

	if ( bAudioRestart ) {
		pHydrogen->restartAudioDriver();
	}
	if ( bMidiRestart ) {
		pHydrogen->restartMidiDriver();
	}
	// The editors redraw with the new colours and options.
	EventQueue::get_instance()->pushEvent( Event::Type::UpdatePreferences, 0 );
	load();
}

void SettingsWindow::updateButtons()
{
	const SettingsValues now = SettingsValues::from( *Preferences::get_instance() );
	const SettingsValues& o = *m_pOriginal;
	const bool bChanged = now.bRelativePaths != o.bRelativePaths || now.nMaxBars != o.nMaxBars || now.nAutosaves != o.nAutosaves ||
		now.sRubberband != o.sRubberband || now.audioDriver != o.audioDriver ||
		now.nBufferSize != o.nBufferSize || now.nSampleRate != o.nSampleRate ||
		now.nPolyphony != o.nPolyphony || now.fMetronome != o.fMetronome || now.midiDriver != o.midiDriver ||
		now.sMidiInput != o.sMidiInput || now.sMidiOutput != o.sMidiOutput ||
		now.noteMapping != o.noteMapping || now.bGlobalChannel != o.bGlobalChannel ||
		now.noteChannel != o.noteChannel || now.bIgnoreNoteOff != o.bIgnoreNoteOff ||
		now.bClockInput != o.bClockInput || now.bTransportInput != o.bTransportInput ||
		now.bFeedback != o.bFeedback || now.bClockOutput != o.bClockOutput ||
		now.bTransportOutput != o.bTransportOutput || now.actionChannel != o.actionChannel ||
		now.feedbackChannel != o.feedbackChannel || now.noteOff != o.noteOff || now.colors != o.colors ||
		now.fFalloff != o.fFalloff || now.coloring != o.coloring || now.nPatternColors != o.nPatternColors;
	m_pRevert->SetEnabled( bChanged );
}

void SettingsWindow::MessageReceived( BMessage* pMessage )
{
	if ( m_bLoading && ( pMessage->what == kMsgChanged || pMessage->what == kMsgAudioChanged ||
						 pMessage->what == kMsgMidiChanged ) ) {
		return;
	}
	auto pPref = Preferences::get_instance();
	auto pHydrogen = Hydrogen::get_instance();

	switch ( pMessage->what ) {
	case kMsgChanged: {
		// Read the live settings from the controls; drivers stay as they are.
		SettingsValues v = SettingsValues::from( *pPref );
		v.bRelativePaths = m_pRelativePaths->Value() == B_CONTROL_ON;
		v.nMaxBars = m_pMaxBars->Value();
		v.nAutosaves = m_pAutosaves->Value();
		v.sRubberband = QString::fromUtf8( m_pRubberband->Text() );
		v.nPolyphony = m_pPolyphony->Value();
		v.fMetronome = m_pMetronome->Value() / 100.0f;
		v.noteMapping = static_cast<MidiInstrumentMap::Input>( std::max( 0, markedIndex( m_pNoteMapping ) ) );
		int32 nChannel = -1;
		if ( BMenuItem* pItem = m_pNoteChannel->Menu()->FindMarked() ) {
			pItem->Message()->FindInt32( "channel", &nChannel );
		}
		v.bGlobalChannel = nChannel != -2;
		if ( v.bGlobalChannel ) {
			v.noteChannel = Midi::Channel( nChannel );
		}
		v.bIgnoreNoteOff = m_pIgnoreNoteOff->Value() == B_CONTROL_ON;
		v.bClockInput = m_pClockInput->Value() == B_CONTROL_ON;
		v.bTransportInput = m_pTransportInput->Value() == B_CONTROL_ON;
		v.actionChannel = Midi::Channel( m_pActionChannel->Value() );
		v.bFeedback = m_pFeedback->Value() == B_CONTROL_ON;
		v.bClockOutput = m_pClockOutput->Value() == B_CONTROL_ON;
		v.bTransportOutput = m_pTransportOutput->Value() == B_CONTROL_ON;
		v.feedbackChannel = Midi::Channel( m_pFeedbackChannel->Value() );
		v.noteOff = static_cast<Preferences::MidiSendNoteOff>( std::max( 0, markedIndex( m_pNoteOff ) ) );
		v.colors = m_pSystemColors->Value() == B_CONTROL_ON ? Colors::Source::System : Colors::Source::Hydrogen;
		const float falloffs[] = { InterfaceTheme::FALLOFF_SLOW, InterfaceTheme::FALLOFF_NORMAL,
								   InterfaceTheme::FALLOFF_FAST };
		v.fFalloff = falloffs[ std::clamp( markedIndex( m_pFalloff ), 0, 2 ) ];
		v.coloring = markedIndex( m_pColoring ) == 0 ? InterfaceTheme::ColoringMethod::Automatic
													 : InterfaceTheme::ColoringMethod::Custom;
		v.nPatternColors = m_pPatternColors->Value();
		apply( v );
		// Interpolation isn't a preference upstream: set it on the sampler.
		pHydrogen->getAudioEngine()->getSampler()->setInterpolateMode(
			static_cast<Interpolation::InterpolateMode>( std::max( 0, markedIndex( m_pInterpolation ) ) ) );
		break;
	}
	case kMsgAudioChanged:
	case kMsgMidiChanged:
		// Driver choices take effect with "Apply and restart driver".
		break;
	case kMsgApplyAudio: {
		SettingsValues v = SettingsValues::from( *pPref );
		if ( BMenuItem* pItem = m_pAudioDriver->Menu()->FindMarked() ) {
			int32 nDriver = 0;
			pItem->Message()->FindInt32( "driver", &nDriver );
			v.audioDriver = static_cast<Preferences::AudioDriver>( nDriver );
		}
		v.nBufferSize = m_pBufferSize->Value();
		v.nSampleRate = kSampleRates[ std::clamp( markedIndex( m_pSampleRate ), 0, 3 ) ];
		// Restart even if nothing changed: that is what the button says.
		pPref->m_audioDriver = Preferences::AudioDriver::None;
		apply( v );
		auto pDriver = pHydrogen->getAudioDriver();
		if ( pDriver == nullptr || std::dynamic_pointer_cast<NullDriver>( pDriver ) != nullptr ) {
			( new BAlert( B_TRANSLATE( "Audio" ), B_TRANSLATE( "Hydrogen couldn't start the audio driver.\n\n"
				"Check that the media services are running (Media preferences)." ),
						  B_TRANSLATE( "OK" ), nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT ) )->Go( nullptr );
		}
		break;
	}
	case kMsgApplyMidi: {
		SettingsValues v = SettingsValues::from( *pPref );
		if ( BMenuItem* pItem = m_pMidiDriver->Menu()->FindMarked() ) {
			int32 nDriver = 0;
			pItem->Message()->FindInt32( "driver", &nDriver );
			v.midiDriver = static_cast<Preferences::MidiDriver>( nDriver );
		}
		BString sPort;
		if ( BMenuItem* pItem = m_pMidiInput->Menu()->FindMarked() ) {
			pItem->Message()->FindString( "port", &sPort );
			v.sMidiInput = QString::fromUtf8( sPort.String() );
		}
		if ( BMenuItem* pItem = m_pMidiOutput->Menu()->FindMarked() ) {
			pItem->Message()->FindString( "port", &sPort );
			v.sMidiOutput = QString::fromUtf8( sPort.String() );
		}
		pPref->m_midiDriver = Preferences::MidiDriver::None == v.midiDriver
			? Preferences::MidiDriver::MidiKit : Preferences::MidiDriver::None;
		apply( v );
		break;
	}
	case kMsgRevert:
		apply( *m_pOriginal );
		break;
	case 'Hset':	// File > Settings… while open: come to the front
		Activate();
		return;
	case kMsgDefaults: {
		// Hydrogen's defaults for what this window shows; the rest stays.
		Preferences defaults;
		SettingsValues v = SettingsValues::from( defaults );
		v.colors = Colors::Source::Hydrogen;
		apply( v );
		break;
	}
	default:
		BWindow::MessageReceived( pMessage );
		return;
	}
	updateButtons();
}

void SettingsWindow::WindowActivated( bool bActive )
{
	BWindow::WindowActivated( bActive );
	// Other programs may have added or removed MIDI ports meanwhile.
	if ( bActive ) {
		m_bLoading = true;
		updateMidiPorts();
		updateAudioStatus();
		m_bLoading = false;
		// Bindings learned or undone meanwhile.
		m_pMidiActions->sync();
	}
}

bool SettingsWindow::QuitRequested()
{
	Settings::setFrame( kFrameName, Frame() );
	Settings::save();
	Preferences::get_instance()->save();
	return true;
}
