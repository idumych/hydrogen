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

#include "EffectWindow.h"

#include <algorithm>
#include <cmath>

#include <Catalog.h>
#include <CheckBox.h>
#include <Control.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>

#include <core/AudioEngine/AudioEngine.h>
#include <core/Basics/Song.h>
#include <core/FX/Effects.h>
#include <core/FX/LadspaFX.h>
#include <core/Globals.h>
#include <core/Hydrogen.h>
#include <core/IO/AudioDriver.h>

#include "Colors.h"
#include "Controls.h"
#include "Mixer.h"
#include "Settings.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "EffectWindow"

using namespace H2Core;

enum : uint32 {
	kMsgPlugin = 'EWpl',
	kMsgActive = 'EWac',
	kMsgPort = 'EWpo',
	kMsgToggle = 'EWto',
	kMsgPoll = 'EWpk',
};

static const float kColumnWidth = 64;
static const float kFaderHeight = 140;

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

/** Upstream's value display: more decimals for small values. */
static BString formatValue( float fValue )
{
	BString sValue;
	const float fMagnitude = std::fabs( fValue );
	sValue.SetToFormat( fMagnitude < 1 ? "%.2f" : fMagnitude < 100 ? "%.1f" : "%.0f", fValue );
	return sValue;
}

static BString frameName( int nFX )
{
	BString sName( "effect " );
	sName << nFX;
	return sName;
}

// ---------------------------------------------------------------------------
// ParamView: a vertical fader between a port's bounds (upstream's Fader),
// or a read-only meter for output ports.

class ParamView : public BControl
{
public:
	ParamView( BMessage* pMessage, float fLower, float fUpper, bool bInteger, bool bOutput )
		: BControl( "param", nullptr, pMessage, B_WILL_DRAW | B_NAVIGABLE )
		, m_fLower( fLower ), m_fUpper( fUpper > fLower ? fUpper : fLower + 1 )
		, m_bInteger( bInteger ), m_bOutput( bOutput )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitSize( BSize( 23, kFaderHeight ) );
		if ( bOutput ) {
			SetFlags( Flags() & ~B_NAVIGABLE );
		}
	}

	float value() const { return m_fValue; }
	void setValue( float fValue )
	{
		fValue = std::clamp( fValue, m_fLower, m_fUpper );
		if ( m_bInteger ) {
			fValue = std::round( fValue );
		}
		if ( fValue != m_fValue ) {
			m_fValue = fValue;
			Invalidate();
		}
	}
	void setDefault( float fDefault ) { m_fDefault = fDefault; }

	void Draw( BRect ) override
	{
		const EditorColors& c = Colors::get();
		const BRect b = Bounds();
		SetHighColor( Colors::darker( c.window, 140 ) );
		FillRoundRect( b, 3, 3 );
		BRect track( b.left + 8, b.top + 4, b.right - 8, b.bottom - 4 );
		SetHighColor( 30, 32, 38 );
		FillRect( track );
		const float fRatio = ( m_fValue - m_fLower ) / ( m_fUpper - m_fLower );
		const float y = std::round( track.bottom - fRatio * track.Height() );
		// Outputs fill like a meter; inputs show their level up to the knob.
		SetHighColor( m_bOutput ? c.highlight : Colors::lighter( c.accent, 120 ) );
		if ( y < track.bottom ) {
			FillRect( BRect( track.left, y, track.right, track.bottom ) );
		}
		if ( ! m_bOutput ) {
			BRect knob( b.left + 1, y - 4, b.right - 1, y + 4 );
			SetHighColor( IsEnabled() ? c.button : Colors::darker( c.button, 120 ) );
			FillRoundRect( knob, 2, 2 );
			SetHighColor( 0, 0, 0 );
			StrokeRoundRect( knob, 2, 2 );
			StrokeLine( BPoint( knob.left + 3, y ), BPoint( knob.right - 3, y ) );
			if ( IsFocus() && Window()->IsActive() ) {
				SetHighUIColor( B_KEYBOARD_NAVIGATION_COLOR );
				StrokeRoundRect( b, 3, 3 );
			}
		}
	}

	void MouseDown( BPoint where ) override
	{
		if ( m_bOutput || ! IsEnabled() ) {
			return;
		}
		MakeFocus( true );
		int32 nClicks = 1;
		if ( Window()->CurrentMessage() != nullptr ) {
			Window()->CurrentMessage()->FindInt32( "clicks", &nClicks );
		}
		// Double-click: the plugin's default (upstream: Ctrl+click).
		if ( nClicks == 2 ) {
			change( m_fDefault );
			return;
		}
		m_bDragging = true;
		SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS );
		change( valueAt( where.y ) );
	}

	void MouseMoved( BPoint where, uint32, const BMessage* ) override
	{
		if ( m_bDragging ) {
			change( valueAt( where.y ) );
		}
	}

	void MouseUp( BPoint ) override { m_bDragging = false; }

	void KeyDown( const char* bytes, int32 nBytes ) override
	{
		const float fStep = m_bInteger ? 1 : ( m_fUpper - m_fLower ) / 100;
		switch ( bytes[ 0 ] ) {
		case B_UP_ARROW:
			change( m_fValue + fStep );
			break;
		case B_DOWN_ARROW:
			change( m_fValue - fStep );
			break;
		case B_PAGE_UP:
			change( m_fValue + fStep * 10 );
			break;
		case B_PAGE_DOWN:
			change( m_fValue - fStep * 10 );
			break;
		case B_HOME:
			change( m_fUpper );
			break;
		case B_END:
			change( m_fLower );
			break;
		default:
			BControl::KeyDown( bytes, nBytes );
		}
	}

private:
	float valueAt( float y ) const
	{
		const BRect b = Bounds();
		const float fRatio = ( b.bottom - 4 - y ) / ( b.Height() - 8 );
		return m_fLower + std::clamp( fRatio, 0.0f, 1.0f ) * ( m_fUpper - m_fLower );
	}
	void change( float fValue )
	{
		const float fOld = m_fValue;
		setValue( fValue );
		if ( m_fValue != fOld ) {
			BMessage message( *Message() );
			message.AddFloat( "value", m_fValue );
			Invoke( &message );
		}
	}

	float m_fLower, m_fUpper;
	float m_fValue = 0;
	float m_fDefault = 0;
	bool m_bInteger, m_bOutput;
	bool m_bDragging = false;
};

// ---------------------------------------------------------------------------
// EffectWindow

static BMessenger sWindows[ MAX_FX ];

void EffectWindow::open( int nFX, BMessenger mixer )
{
	if ( nFX < 0 || nFX >= MAX_FX ) {
		return;
	}
	if ( sWindows[ nFX ].LockTarget() ) {
		BLooper* pLooper = nullptr;
		sWindows[ nFX ].Target( &pLooper );
		BWindow* pWindow = static_cast<BWindow*>( pLooper );
		if ( pWindow->IsHidden() ) {
			pWindow->Show();
		}
		pWindow->Activate();
		pLooper->Unlock();
		return;
	}
	EffectWindow* pWindow = new EffectWindow( nFX, mixer );
	sWindows[ nFX ] = BMessenger( pWindow );
	pWindow->Show();
}

void EffectWindow::setPlugin( int nFX, const BString& sFile, const BString& sLabel )
{
#ifdef H2CORE_HAVE_LADSPA
	auto pHydrogen = Hydrogen::get_instance();
	auto pSong = pHydrogen->getSong();
	if ( pSong == nullptr ) {
		return;
	}
	std::shared_ptr<LadspaFX> pFX;
	auto pDriver = pHydrogen->getAudioDriver();
	if ( ! sFile.IsEmpty() && pDriver != nullptr ) {
		pFX = LadspaFX::load( QString::fromUtf8( sFile.String() ), QString::fromUtf8( sLabel.String() ),
							  pDriver->getSampleRate() );
		if ( pFX != nullptr ) {
			pFX->setEnabled( true );
		}
	}
	pSong->getEffects()->setLadspaFX( pFX, nFX );
	pHydrogen->setSongModified( true );
	pHydrogen->restartLadspaFX();
#endif
}

EffectWindow::EffectWindow( int nFX, BMessenger mixer )
	: BWindow( BRect( 0, 0, 500, 260 ), "", B_TITLED_WINDOW,
			   B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE )
	, m_nFX( nFX )
	, m_mixer( mixer )
{
	BPopUpMenu* pMenu = new BPopUpMenu( "plugin" );
	m_pPlugin = new BMenuField( "plugin", B_TRANSLATE( "Plugin:" ), pMenu );
	m_pActive = new BCheckBox( "active", B_TRANSLATE( "Active" ), new BMessage( kMsgActive ) );
	m_pActive->SetToolTip( B_TRANSLATE( "Off bypasses the effect" ) );
	m_pInfo = new BStringView( "info", "" );
	m_pInfo->SetTruncation( B_TRUNCATE_MIDDLE );
	m_pInfo->SetExplicitMaxSize( BSize( B_SIZE_UNLIMITED, B_SIZE_UNSET ) );
	m_pPorts = new BGroupView( B_HORIZONTAL, B_USE_HALF_ITEM_SPACING );
	m_pPorts->GroupLayout()->SetInsets( B_USE_HALF_ITEM_INSETS );
	m_pPane = new ScrollPane( m_pPorts, B_HORIZONTAL );

	BLayoutBuilder::Group<>( this, B_VERTICAL )
		.SetInsets( B_USE_WINDOW_INSETS )
		.AddGroup( B_HORIZONTAL )
			.Add( m_pPlugin )
			.Add( m_pActive )
		.End()
		.Add( m_pInfo )
		.Add( m_pPane );

	rebuild();
	const BRect frame = Settings::frame( frameName( nFX ), BRect() );
	if ( frame.IsValid() ) {
		MoveTo( frame.LeftTop() );
		ResizeTo( frame.Width(), frame.Height() );
	}
	else {
		BRect mixerFrame;
		BLooper* pLooper = nullptr;
		if ( m_mixer.LockTarget() ) {
			m_mixer.Target( &pLooper );
			mixerFrame = static_cast<BWindow*>( pLooper )->Frame();
			pLooper->Unlock();
			CenterIn( mixerFrame );
		}
		else {
			CenterOnScreen();
		}
	}
	BMessage poll( kMsgPoll );
	m_pRunner = new BMessageRunner( BMessenger( this ), &poll, 100000 );
}

std::shared_ptr<LadspaFX> EffectWindow::effect() const
{
#ifdef H2CORE_HAVE_LADSPA
	auto pSong = Hydrogen::get_instance()->getSong();
	if ( pSong != nullptr ) {
		return pSong->getEffects()->getLadspaFX( m_nFX );
	}
#endif
	return nullptr;
}

void EffectWindow::rebuild()
{
	auto pFX = effect();
	m_pShown = pFX;

	// The plugin menu: "No plugin", then the installed ones by name.
	BMenu* pMenu = m_pPlugin->Menu();
	while ( BMenuItem* pItem = pMenu->RemoveItem( (int32) 0 ) ) {
		delete pItem;
	}
	BMenuItem* pNone = new BMenuItem( B_TRANSLATE( "No plugin" ), new BMessage( kMsgPlugin ) );
	pNone->SetMarked( pFX == nullptr );
	pMenu->AddItem( pNone );
#ifdef H2CORE_HAVE_LADSPA
	auto pSong = Hydrogen::get_instance()->getSong();
	if ( pSong != nullptr ) {
		auto plugins = pSong->getEffects()->getPluginList();
		std::sort( plugins.begin(), plugins.end(), []( const auto& a, const auto& b ) {
			return a->m_sName.compare( b->m_sName, Qt::CaseInsensitive ) < 0;
		} );
		if ( ! plugins.empty() ) {
			pMenu->AddSeparatorItem();
		}
		for ( const auto& pInfo : plugins ) {
			BMessage* pMessage = new BMessage( kMsgPlugin );
			pMessage->AddString( "file", toBString( pInfo->m_sFileName ) );
			pMessage->AddString( "label", toBString( pInfo->m_sLabel ) );
			BMenuItem* pItem = new BMenuItem( toBString( pInfo->m_sName ), pMessage );
			pItem->SetMarked( pFX != nullptr && pFX->getPluginLabel() == pInfo->m_sLabel &&
							  pFX->getLibraryPath() == pInfo->m_sFileName );
			pMenu->AddItem( pItem );
		}
	}
#endif

	// The ports.
	BGroupLayout* pLayout = m_pPorts->GroupLayout();
	while ( pLayout->CountItems() > 0 ) {
		BLayoutItem* pItem = pLayout->RemoveItem( (int32) 0 );
		if ( BView* pView = pItem->View() ) {
			pView->RemoveSelf();
			delete pView;
		}
		delete pItem;
	}
	m_inputs.clear();
	m_inputValues.clear();
	m_outputs.clear();
	m_outputValues.clear();

	auto column = [this]( const QString& sName, BView* pControl, BStringView* pValue ) {
		BGroupView* pColumn = new BGroupView( B_VERTICAL, 2 );
		BStringView* pName = new BStringView( "name", toBString( sName ) );
		pName->SetTruncation( B_TRUNCATE_END );
		pName->SetAlignment( B_ALIGN_CENTER );
		pName->SetToolTip( toBString( sName ) );
		pName->SetExplicitSize( BSize( kColumnWidth, B_SIZE_UNSET ) );
		BLayoutBuilder::Group<>( pColumn )
			.Add( pName );
		if ( pValue != nullptr ) {
			pValue->SetAlignment( B_ALIGN_CENTER );
			pValue->SetExplicitSize( BSize( kColumnWidth, B_SIZE_UNSET ) );
			pColumn->GroupLayout()->AddView( pValue );
		}
		BGroupView* pCenter = new BGroupView( B_HORIZONTAL, 0 );
		BLayoutBuilder::Group<>( pCenter ).AddGlue().Add( pControl ).AddGlue();
		pColumn->GroupLayout()->AddView( pCenter );
		pColumn->GroupLayout()->AddItem( BSpaceLayoutItem::CreateGlue() );
		m_pPorts->GroupLayout()->AddView( pColumn );
	};

	if ( pFX != nullptr ) {
		for ( size_t i = 0; i < pFX->inputControlPorts.size(); i++ ) {
			const auto& pPort = pFX->inputControlPorts[ i ];
			BMessage* pMessage = new BMessage( pPort->isToggle ? kMsgToggle : kMsgPort );
			pMessage->AddInt32( "port", static_cast<int32>( i ) );
			// Upstream left toggle ports as faders ("not implemented yet").
			if ( pPort->isToggle ) {
				BCheckBox* pCheck = new BCheckBox( "toggle", nullptr, pMessage );
				pCheck->SetValue( pPort->fControlValue > 0 );
				pCheck->SetTarget( this );
				column( pPort->sName, pCheck, nullptr );
				m_inputs.push_back( nullptr );
				m_inputValues.push_back( nullptr );
				continue;
			}
			ParamView* pFader = new ParamView( pMessage, pPort->fLowerBound, pPort->fUpperBound,
											   pPort->m_bIsInteger, false );
			pFader->setValue( pPort->fControlValue );
			pFader->setDefault( pPort->fDefaultValue );
			BString sTip;
			sTip << formatValue( pPort->fLowerBound ) << " … " << formatValue( pPort->fUpperBound );
			pFader->SetToolTip( sTip );
			BStringView* pValue = new BStringView( "value", formatValue( pPort->fControlValue ) );
			column( pPort->sName, pFader, pValue );
			m_inputs.push_back( pFader );
			m_inputValues.push_back( pValue );
		}
		if ( ! pFX->outputControlPorts.empty() ) {
			m_pPorts->GroupLayout()->AddView( new BSeparatorView( B_VERTICAL ) );
		}
		for ( size_t i = 0; i < pFX->outputControlPorts.size(); i++ ) {
			const auto& pPort = pFX->outputControlPorts[ i ];
			ParamView* pMeter = new ParamView( new BMessage(), pPort->fLowerBound, pPort->fUpperBound,
											   false, true );
			pMeter->setValue( pPort->fControlValue );
			pMeter->SetToolTip( B_TRANSLATE( "Output of the plugin" ) );
			BStringView* pValue = new BStringView( "value", formatValue( pPort->fControlValue ) );
			column( pPort->sName, pMeter, pValue );
			m_outputs.push_back( pMeter );
			m_outputValues.push_back( pValue );
		}
		if ( pFX->inputControlPorts.empty() && pFX->outputControlPorts.empty() ) {
			m_pPorts->GroupLayout()->AddView( new BStringView( nullptr, B_TRANSLATE( "This plugin has no controls." ) ) );
		}
	}
	else {
		m_pPorts->GroupLayout()->AddView( new BStringView( nullptr, B_TRANSLATE( "Choose a plugin from the menu." ) ) );
	}
	m_pPorts->GroupLayout()->AddItem( BSpaceLayoutItem::CreateGlue() );
	for ( ParamView* pFader : m_inputs ) {
		if ( pFader != nullptr ) {
			pFader->SetTarget( this );
		}
	}
	m_pPane->contentChanged();
	// As wide as the controls, up to the screen; wider plugins scroll.
	const float fMaxWidth = BScreen( this ).Frame().Width() - 80;
	m_pPane->SetExplicitMinSize( BSize( std::clamp( m_pPorts->PreferredSize().width, 300.0f, fMaxWidth ),
										m_pPorts->PreferredSize().height + B_H_SCROLL_BAR_HEIGHT + 2 ) );
	updateHeader();
}

void EffectWindow::updateHeader()
{
	auto pFX = effect();
	m_bShownEnabled = pFX != nullptr && pFX->isEnabled();
	BString sTitle;
	sTitle.SetToFormat( B_TRANSLATE( "Effect %d" ), m_nFX + 1 );
	if ( pFX != nullptr ) {
		sTitle << ": " << toBString( pFX->getPluginName() );
	}
	SetTitle( sTitle );
	m_pActive->SetEnabled( pFX != nullptr );
	m_pActive->SetValue( m_bShownEnabled );
	if ( pFX != nullptr ) {
		BString sInfo;
		sInfo << toBString( pFX->getPluginLabel() ) << " — " << toBString( pFX->getLibraryPath() );
		m_pInfo->SetText( sInfo );
	}
	else {
		m_pInfo->SetText( "" );
	}
}

void EffectWindow::updateOutputs()
{
	auto pFX = effect();
	// Changed elsewhere (another song, the mixer): show the new one.
	if ( pFX != m_pShown ) {
		rebuild();
		return;
	}
	if ( pFX == nullptr ) {
		return;
	}
	if ( pFX->isEnabled() != m_bShownEnabled ) {
		updateHeader();
	}
	for ( size_t i = 0; i < m_outputs.size() && i < pFX->outputControlPorts.size(); i++ ) {
		const float fValue = pFX->outputControlPorts[ i ]->fControlValue;
		m_outputs[ i ]->setValue( fValue );
		m_outputValues[ i ]->SetText( formatValue( fValue ) );
	}
}

bool EffectWindow::QuitRequested()
{
	Settings::setFrame( frameName( m_nFX ), Frame() );
	delete m_pRunner;
	m_pRunner = nullptr;
	return true;
}

void EffectWindow::MessageReceived( BMessage* pMessage )
{
	auto pHydrogen = Hydrogen::get_instance();
	switch ( pMessage->what ) {
	case kMsgPlugin: {
		BString sFile, sLabel;
		pMessage->FindString( "file", &sFile );
		pMessage->FindString( "label", &sLabel );
		setPlugin( m_nFX, sFile, sLabel );
		rebuild();
		m_mixer.SendMessage( kMsgMixerRebuild );
		break;
	}
	case kMsgActive:
		if ( auto pFX = effect() ) {
			pHydrogen->getAudioEngine()->lock( RIGHT_HERE );
			pFX->setEnabled( m_pActive->Value() == B_CONTROL_ON );
			pHydrogen->getAudioEngine()->unlock();
			pHydrogen->setSongModified( true );
			updateHeader();
			m_mixer.SendMessage( kMsgMixerRebuild );
		}
		break;
	case kMsgPort: {
		int32 nPort = 0;
		float fValue = 0;
		auto pFX = effect();
		if ( pFX == nullptr || pMessage->FindInt32( "port", &nPort ) != B_OK ||
			 pMessage->FindFloat( "value", &fValue ) != B_OK || nPort < 0 ||
			 nPort >= static_cast<int32>( pFX->inputControlPorts.size() ) ) {
			break;
		}
		pFX->inputControlPorts[ nPort ]->fControlValue = fValue;
		if ( m_inputValues[ nPort ] != nullptr ) {
			m_inputValues[ nPort ]->SetText( formatValue( fValue ) );
		}
		pHydrogen->setSongModified( true );
		break;
	}
	case kMsgToggle: {
		int32 nPort = 0;
		auto pFX = effect();
		void* pSource = nullptr;
		if ( pFX == nullptr || pMessage->FindInt32( "port", &nPort ) != B_OK ||
			 pMessage->FindPointer( "source", &pSource ) != B_OK || nPort < 0 ||
			 nPort >= static_cast<int32>( pFX->inputControlPorts.size() ) ) {
			break;
		}
		const auto& pPort = pFX->inputControlPorts[ nPort ];
		pPort->fControlValue = static_cast<BCheckBox*>( pSource )->Value() == B_CONTROL_ON ? pPort->fUpperBound
																							: pPort->fLowerBound;
		pHydrogen->setSongModified( true );
		break;
	}
	case kMsgPoll:
		updateOutputs();
		break;
	default:
		BWindow::MessageReceived( pMessage );
	}
}
