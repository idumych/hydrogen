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

#include "ComponentEditor.h"

#include <algorithm>
#include <cmath>

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Gradient.h>
#include <GradientLinear.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Polygon.h>
#include <StringView.h>
#include <Window.h>

#include <core/AudioEngine/AudioEngine.h>
#include <core/Basics/Event.h>
#include <core/Basics/Instrument.h>
#include <core/Basics/InstrumentComponent.h>
#include <core/Basics/InstrumentLayer.h>
#include <core/Basics/Note.h>
#include <core/Basics/Sample.h>
#include <core/Helpers/Filesystem.h>
#include <core/Hydrogen.h>
#include <core/Preferences/Preferences.h>
#include <core/Sampler/Sampler.h>

#include "Colors.h"
#include "Controls.h"
#include "NameWindow.h"
#include "SongActions.h"
#include "TransportBar.h"
#include "UndoStack.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ComponentEditor"

using namespace H2Core;

enum : uint32 {
	kMsgCollapse = 'CEco', kMsgAddComponent = 'CEac', kMsgDuplicateComponent = 'CEdc',
	kMsgDeleteComponent = 'CEdl', kMsgComponentMute = 'CEcm', kMsgComponentSolo = 'CEcs',
	kMsgComponentGain = 'CEcg', kMsgRenameComponent = 'CErc', kMsgDoRename = 'CEdr',
	kMsgAddLayer = 'CEal', kMsgReplaceLayer = 'CErl', kMsgDuplicateLayer = 'CEdu',
	kMsgDeleteLayer = 'CEde', kMsgLayerMute = 'CElm', kMsgLayerSolo = 'CEls',
	kMsgLayerGain = 'CElg', kMsgSelection = 'CEse', kMsgCoarse = 'CEpc', kMsgFine = 'CEpf',
	kMsgSamplesChosen = 'CEsc', kMsgLayerSelected = 'CElx',
};

enum class SampleAction : int32 { AddComponent, AddLayer, ReplaceLayer };

static const float kHeader = 20;		// layer preview: the numbered strip
static const float kLayerHeight = 16;
static const float kGrabMargin = 5;		// px around a range edge that grab it
static const float kWaveHeight = 58;

static BString toBString( const QString& s )
{
	return BString( s.toUtf8().constData() );
}

static BString fileName( const QString& sPath )
{
	return BPath( sPath.toUtf8().constData() ).Leaf();
}

/** Upstream's gradient: darker at the edges, lighter in the middle. */
static void fillGradient( BView* pView, BRect rect, rgb_color color )
{
	BGradientLinear gradient( rect.LeftTop(), rect.LeftBottom() );
	gradient.AddColor( Colors::darker( color, 130 ), 0 );
	gradient.AddColor( Colors::lighter( color, 130 ), 127 );
	gradient.AddColor( Colors::darker( color, 130 ), 255 );
	pView->FillRect( rect, gradient );
}

/** Shows folders and the audio formats Hydrogen can load. */
class AudioFilter : public BRefFilter
{
public:
	bool Filter( const entry_ref* pRef, BNode* pNode, struct stat_beos*, const char* ) override
	{
		if ( pNode->IsDirectory() ) {
			return true;
		}
		BEntry entry( pRef, true );
		if ( entry.IsDirectory() ) {
			return true;
		}
		const BString sName( pRef->name );
		for ( const auto& format : Filesystem::supportedAudioFormats() ) {
			const QString sSuffix = Filesystem::AudioFormatToSuffix( format, true );
			if ( ! sSuffix.isEmpty() && sName.IEndsWith( ( BString( "." ) << toBString( sSuffix ) ).String() ) ) {
				return true;
			}
		}
		return false;
	}
};

class ComponentPanel;

// ---------------------------------------------------------------------------
// LayerPreview: the layers' velocity ranges (upstream's LayerPreview).

class LayerPreview : public BView
{
public:
	LayerPreview( ComponentPanel* pPanel ) : BView( "layer preview", B_WILL_DRAW ), m_pPanel( pPanel )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
	}

	void setLayerCount( int nLayers )
	{
		const float fHeight = kHeader + 1 + std::max( nLayers, 1 ) * kLayerHeight;
		SetExplicitMinSize( BSize( 100, fHeight - 1 ) );
		SetExplicitMaxSize( BSize( B_SIZE_UNLIMITED, fHeight - 1 ) );
	}

	void Draw( BRect ) override;
	void MouseDown( BPoint where ) override;
	void MouseMoved( BPoint where, uint32 transit, const BMessage* pDrag ) override;
	void MouseUp( BPoint ) override;

private:
	enum class Drag { None, Start, End };
	int rowAt( float fY ) const { return fY < kHeader ? -1 : (int) ( ( fY - kHeader ) / kLayerHeight ); }

	ComponentPanel* m_pPanel;
	Drag m_drag = Drag::None;
};

// ---------------------------------------------------------------------------
// WaveView: the selected layer's sample (upstream's LayerWaveDisplay).

class WaveView : public BView
{
public:
	WaveView( ComponentPanel* pPanel )
		: BView( "wave view", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE ), m_pPanel( pPanel )
	{
		SetViewColor( B_TRANSPARENT_COLOR );
		SetExplicitMinSize( BSize( 100, kWaveHeight - 1 ) );
		SetExplicitMaxSize( BSize( B_SIZE_UNLIMITED, kWaveHeight - 1 ) );
	}

	void Draw( BRect ) override;
	void MouseDown( BPoint ) override;

private:
	/** Peaks per pixel column, recomputed when the sample, gain or width change. */
	void updatePeaks( const std::shared_ptr<InstrumentLayer>& pLayer, int nWidth );

	ComponentPanel* m_pPanel;
	std::shared_ptr<Sample> m_pPeakSample;
	float m_fPeakGain = -1;
	int m_nPeakWidth = -1;
	std::vector<float> m_max, m_min;
	bool m_bEnvelope = false;
};

// ---------------------------------------------------------------------------
// ComponentPanel: one component with its layers.

class ComponentPanel : public BBox
{
public:
	ComponentPanel( ComponentEditor* pEditor, int nIndex );

	void AttachedToWindow() override;
	void MessageReceived( BMessage* pMessage ) override;
	void update();

	std::shared_ptr<Instrument> instrument() const
	{
		return Hydrogen::get_instance()->getSelectedInstrument();
	}
	std::shared_ptr<InstrumentComponent> component() const
	{
		auto pInstrument = instrument();
		return pInstrument != nullptr ? pInstrument->getComponent( m_nIndex ) : nullptr;
	}
	int selectedLayer() const { return m_pEditor->m_selectedLayer[ m_nIndex ]; }
	void selectLayer( int nLayer )
	{
		m_pEditor->m_selectedLayer[ m_nIndex ] = nLayer;
		update();
	}
	std::shared_ptr<InstrumentLayer> layer() const
	{
		auto pComponent = component();
		return pComponent != nullptr ? pComponent->getLayer( selectedLayer() ) : nullptr;
	}
	/** Plays the component at @a fVelocity, or exactly @a pLayer if given. */
	void play( float fVelocity, std::shared_ptr<InstrumentLayer> pLayer );
	void chooseSamples( SampleAction action );

private:
	/** Upstream's copy-on-write edit: edit a copy of the instrument, then
	 * replace the original with it as one undo step. */
	template <typename Edit>
	void editCopy( const char* sName, Edit edit );
	void samplesChosen( BMessage* pMessage );

	ComponentEditor* m_pEditor;
	int m_nIndex;
	bool m_bExpanded;
	BStringView* m_pName;
	BButton* m_pCollapse;
	GlyphButton* m_pDeleteComponent;
	BButton* m_pMute;
	BButton* m_pSolo;
	Knob* m_pGain;
	LayerPreview* m_pPreview = nullptr;
	GlyphButton* m_pReplaceLayer = nullptr;
	GlyphButton* m_pDuplicateLayer = nullptr;
	GlyphButton* m_pDeleteLayer = nullptr;
	BButton* m_pLayerMute = nullptr;
	BButton* m_pLayerSolo = nullptr;
	Knob* m_pLayerGain = nullptr;
	BMenuField* m_pSelection = nullptr;
	WaveView* m_pWave = nullptr;
	BStringView* m_pPitchValue = nullptr;
	Knob* m_pCoarse = nullptr;
	Knob* m_pFine = nullptr;
	BFilePanel* m_pFilePanel = nullptr;
};

/** A component's name; double-click renames it. */
class ComponentName : public BStringView
{
public:
	ComponentName() : BStringView( "component name", "" )
	{
		SetFont( be_bold_font );
		SetToolTip( B_TRANSLATE( "Double-click to rename" ) );
		SetExplicitMaxSize( BSize( B_SIZE_UNLIMITED, B_SIZE_UNSET ) );
	}

	void MouseDown( BPoint ) override
	{
		int32 nClicks = 1;
		if ( Window()->CurrentMessage() != nullptr ) {
			Window()->CurrentMessage()->FindInt32( "clicks", &nClicks );
		}
		if ( nClicks == 2 ) {
			for ( BView* p = Parent(); p != nullptr; p = p->Parent() ) {
				if ( dynamic_cast<ComponentPanel*>( p ) != nullptr ) {
					BMessenger( p ).SendMessage( kMsgRenameComponent );
					break;
				}
			}
		}
	}
};

static BButton* toggleButton( const char* sLabel, uint32 what, const char* sTip )
{
	BButton* pButton = new BButton( sLabel, sLabel, new BMessage( what ) );
	pButton->SetBehavior( BButton::B_TOGGLE_BEHAVIOR );
	const float fSize = pButton->StringWidth( "MM" ) + 8;
	pButton->SetExplicitSize( BSize( fSize, B_SIZE_UNSET ) );
	pButton->SetToolTip( sTip );
	return pButton;
}

static GlyphButton* toolButton( GlyphButton::Glyph glyph, uint32 what, const char* sTip )
{
	GlyphButton* pButton = new GlyphButton( sTip, glyph, new BMessage( what ), false, 22 );
	pButton->SetToolTip( sTip );
	return pButton;
}

static BView* knobColumn( Knob* pKnob, const char* sLabel )
{
	BStringView* pLabel = new BStringView( nullptr, sLabel );
	BFont font( be_plain_font );
	font.SetSize( std::max( 9.0f, be_plain_font->Size() * 0.8f ) );
	pLabel->SetFont( &font );
	pLabel->SetAlignment( B_ALIGN_CENTER );
	BGroupView* pColumn = new BGroupView( B_VERTICAL, 0 );
	BLayoutBuilder::Group<>( pColumn ).Add( pKnob, 0 ).Add( pLabel );
	pKnob->SetExplicitAlignment( BAlignment( B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_TOP ) );
	return pColumn;
}

ComponentPanel::ComponentPanel( ComponentEditor* pEditor, int nIndex )
	: BBox( "component" ), m_pEditor( pEditor ), m_nIndex( nIndex )
{
	m_bExpanded = m_pEditor->m_expanded[ nIndex ];
	m_pCollapse = new BButton( "collapse", m_bExpanded ? "−" : "+", new BMessage( kMsgCollapse ) );
	m_pCollapse->SetExplicitSize( BSize( m_pCollapse->StringWidth( "+" ) + 16, B_SIZE_UNSET ) );
	m_pCollapse->SetToolTip( m_bExpanded ? B_TRANSLATE( "Collapse" ) : B_TRANSLATE( "Expand" ) );
	m_pName = new ComponentName();
	GlyphButton* pAdd = toolButton( GlyphButton::Glyph::Add, kMsgAddComponent,
									B_TRANSLATE( "New component from samples" ) );
	GlyphButton* pDuplicate = toolButton( GlyphButton::Glyph::Duplicate, kMsgDuplicateComponent,
										  B_TRANSLATE( "Duplicate component" ) );
	m_pDeleteComponent = toolButton( GlyphButton::Glyph::Clear, kMsgDeleteComponent,
									 B_TRANSLATE( "Delete component" ) );
	m_pMute = toggleButton( "M", kMsgComponentMute, B_TRANSLATE( "Mute component" ) );
	m_pSolo = toggleButton( "S", kMsgComponentSolo, B_TRANSLATE( "Solo component" ) );
	m_pGain = new Knob( "component gain", new BMessage( kMsgComponentGain ), 0, 5, 1 );
	m_pGain->SetToolTip( B_TRANSLATE( "Component gain" ) );

	BGroupView* pHeader = new BGroupView( B_HORIZONTAL, 2 );
	BLayoutBuilder::Group<>( pHeader )
		.Add( m_pCollapse )
		.Add( m_pName )
		.Add( pAdd )
		.Add( pDuplicate )
		.Add( m_pDeleteComponent )
		.Add( m_pMute )
		.Add( m_pSolo )
		.Add( m_pGain );

	BGroupLayout* pLayout = new BGroupLayout( B_VERTICAL, B_USE_HALF_ITEM_SPACING );
	SetLayout( pLayout );
	pLayout->SetInsets( 4, 4, 4, 4 );
	pLayout->AddView( pHeader );

	if ( m_bExpanded ) {
		m_pPreview = new LayerPreview( this );
		GlyphButton* pAddLayer = toolButton( GlyphButton::Glyph::Add, kMsgAddLayer,
											 B_TRANSLATE( "New layers from samples" ) );
		m_pReplaceLayer = toolButton( GlyphButton::Glyph::Open, kMsgReplaceLayer,
									  B_TRANSLATE( "Replace the layer's sample" ) );
		m_pDuplicateLayer = toolButton( GlyphButton::Glyph::Duplicate, kMsgDuplicateLayer,
										B_TRANSLATE( "Duplicate layer" ) );
		m_pDeleteLayer = toolButton( GlyphButton::Glyph::Clear, kMsgDeleteLayer,
									 B_TRANSLATE( "Delete layer" ) );
		m_pLayerMute = toggleButton( "M", kMsgLayerMute, B_TRANSLATE( "Mute layer" ) );
		m_pLayerSolo = toggleButton( "S", kMsgLayerSolo, B_TRANSLATE( "Solo layer" ) );
		m_pLayerGain = new Knob( "layer gain", new BMessage( kMsgLayerGain ), 0, 5, 1 );
		m_pLayerGain->SetToolTip( B_TRANSLATE( "Layer gain" ) );

		BPopUpMenu* pSelectionMenu = new BPopUpMenu( "selection" );
		const char* labels[] = { B_TRANSLATE( "First in velocity" ), B_TRANSLATE( "Round robin" ),
								 B_TRANSLATE( "Random" ) };
		for ( int32 i = 0; i < 3; i++ ) {
			BMessage* pMessage = new BMessage( kMsgSelection );
			pMessage->AddInt32( "index", i );
			pSelectionMenu->AddItem( new BMenuItem( labels[ i ], pMessage ) );
		}
		m_pSelection = new BMenuField( "selection", B_TRANSLATE( "Sample selection:" ), pSelectionMenu );
		m_pSelection->SetToolTip( B_TRANSLATE( "Which layer plays for a note" ) );

		m_pWave = new WaveView( this );
		m_pPitchValue = new BStringView( "pitch", "0.00" );
		m_pPitchValue->SetFont( be_fixed_font );
		m_pCoarse = new Knob( "layer coarse", new BMessage( kMsgCoarse ),
							  Instrument::fPitchOffsetMinimum + 0.5f,
							  Instrument::fPitchOffsetMaximum - 0.5f, 0, Knob::Style::Center, true );
		m_pFine = new Knob( "layer fine", new BMessage( kMsgFine ), -50, 50, 0, Knob::Style::Center );

		BGroupView* pToolbar = new BGroupView( B_HORIZONTAL, 2 );
		BLayoutBuilder::Group<>( pToolbar )
			.Add( pAddLayer )
			.AddStrut( 4 )
			.Add( m_pReplaceLayer )
			.Add( m_pDuplicateLayer )
			.Add( m_pDeleteLayer )
			.AddGlue()
			.Add( m_pLayerMute )
			.Add( m_pLayerSolo )
			.Add( m_pLayerGain );
		BGroupView* pPitch = new BGroupView( B_HORIZONTAL, B_USE_HALF_ITEM_SPACING );
		BLayoutBuilder::Group<>( pPitch )
			.Add( new BStringView( nullptr, B_TRANSLATE( "Pitch:" ) ) )
			.Add( m_pPitchValue )
			.AddGlue()
			.Add( knobColumn( m_pCoarse, B_TRANSLATE( "Coarse" ) ) )
			.Add( knobColumn( m_pFine, B_TRANSLATE( "Fine" ) ) );

		pLayout->AddView( m_pPreview );
		pLayout->AddView( pToolbar );
		pLayout->AddView( m_pSelection );
		pLayout->AddView( m_pWave );
		pLayout->AddView( pPitch );
	}
}

void ComponentPanel::AttachedToWindow()
{
	BBox::AttachedToWindow();
	for ( BView* pChild : std::initializer_list<BView*>{ m_pCollapse, m_pMute, m_pSolo, m_pGain,
			  m_pDeleteComponent, m_pReplaceLayer, m_pDuplicateLayer, m_pDeleteLayer, m_pLayerMute,
			  m_pLayerSolo, m_pLayerGain, m_pCoarse, m_pFine } ) {
		if ( auto pControl = dynamic_cast<BControl*>( pChild ) ) {
			pControl->SetTarget( this );
		}
	}
	for ( const char* sName : { B_TRANSLATE( "New component from samples" ),
								B_TRANSLATE( "Duplicate component" ),
								B_TRANSLATE( "New layers from samples" ) } ) {
		if ( auto pControl = dynamic_cast<BControl*>( FindView( sName ) ) ) {
			pControl->SetTarget( this );
		}
	}
	if ( m_pSelection != nullptr ) {
		m_pSelection->Menu()->SetTargetForItems( this );
	}
	update();
}

void ComponentPanel::update()
{
	auto pInstrument = instrument();
	auto pComponent = component();
	if ( pComponent == nullptr ) {
		return;
	}
	m_pName->SetText( toBString( pComponent->getName() ) );
	m_pMute->SetValue( pComponent->getIsMuted() );
	m_pSolo->SetValue( pComponent->getIsSoloed() );
	m_pGain->setValue( pComponent->getGain() );
	m_pDeleteComponent->SetEnabled( pInstrument->getComponents()->size() > 1 );
	if ( ! m_bExpanded ) {
		return;
	}

	const int nLayers = (int) pComponent->getLayers().size();
	auto& nSelected = m_pEditor->m_selectedLayer[ m_nIndex ];
	nSelected = std::clamp( nSelected, 0, std::max( 0, nLayers - 1 ) );
	m_pPreview->setLayerCount( nLayers );
	m_pPreview->Invalidate();
	m_pWave->Invalidate();

	m_pSelection->Menu()->ItemAt( static_cast<int32>( pComponent->getSelection() ) )->SetMarked( true );

	auto pLayer = layer();
	const bool bHasLayer = pLayer != nullptr;
	for ( BControl* pControl : std::initializer_list<BControl*>{ m_pReplaceLayer, m_pDuplicateLayer,
			  m_pDeleteLayer, m_pLayerMute, m_pLayerSolo, m_pLayerGain, m_pCoarse, m_pFine } ) {
		pControl->SetEnabled( bHasLayer );
	}
	if ( ! bHasLayer ) {
		m_pPitchValue->SetText( "" );
		return;
	}
	m_pLayerMute->SetValue( pLayer->getIsMuted() );
	m_pLayerSolo->SetValue( pLayer->getIsSoloed() );
	m_pLayerGain->setValue( pLayer->getGain() );
	// Upstream's split: coarse in semitones, fine in cents.
	const float fPitch = pLayer->getPitchOffset();
	const float fCoarse = std::round( fPitch );
	m_pCoarse->setValue( fCoarse );
	m_pFine->setValue( ( fPitch - fCoarse ) * 100 );
	BString sPitch;
	sPitch.SetToFormat( "%.2f", fPitch );
	m_pPitchValue->SetText( sPitch );
}

void ComponentPanel::play( float fVelocity, std::shared_ptr<InstrumentLayer> pLayer )
{
	auto pInstrument = instrument();
	auto pComponent = component();
	if ( pInstrument == nullptr || pComponent == nullptr || ! pComponent->hasSamples() ) {
		return;
	}
	auto pNote = std::make_shared<Note>( pInstrument, 0, std::clamp( fVelocity, VELOCITY_MIN, VELOCITY_MAX ) );
	std::shared_ptr<SelectedLayerInfo> pInfo;
	if ( pLayer != nullptr ) {
		pInfo = std::make_shared<SelectedLayerInfo>();
		pInfo->pLayer = pLayer;
	}
	pNote->setSelectedLayerInfo( pInfo, pComponent );
	Hydrogen::get_instance()->getAudioEngine()->getSampler()->noteOn( pNote );
}

template <typename Edit>
void ComponentPanel::editCopy( const char* sName, Edit edit )
{
	auto pOld = instrument();
	if ( pOld == nullptr ) {
		return;
	}
	auto pNew = std::make_shared<Instrument>( pOld );
	if ( ! edit( pNew, pNew->getComponent( m_nIndex ) ) ) {
		return;
	}
	UndoStack::get().push( SongActions::replaceInstrument( pNew, pOld, sName ) );
	Window()->PostMessage( 'Hped' );
}

void ComponentPanel::chooseSamples( SampleAction action )
{
	BMessage message( kMsgSamplesChosen );
	message.AddInt32( "action", static_cast<int32>( action ) );
	delete m_pFilePanel;
	m_pFilePanel = new BFilePanel( B_OPEN_PANEL, new BMessenger( this ), nullptr, B_FILE_NODE,
								   action != SampleAction::ReplaceLayer, &message, new AudioFilter() );
	QString sDir = Preferences::get_instance()->getLastOpenLayerDirectory();
	if ( action == SampleAction::ReplaceLayer && layer() != nullptr && layer()->getSample() != nullptr ) {
		BPath parent;
		BPath( layer()->getSample()->getFilePath().toUtf8().constData() ).GetParent( &parent );
		sDir = QString::fromUtf8( parent.Path() );
	}
	if ( ! sDir.isEmpty() ) {
		m_pFilePanel->SetPanelDirectory( sDir.toUtf8().constData() );
	}
	m_pFilePanel->Window()->SetTitle( action == SampleAction::ReplaceLayer
		? B_TRANSLATE( "Hydrogen: Replace sample" ) : B_TRANSLATE( "Hydrogen: Add samples" ) );
	m_pFilePanel->Show();
}

void ComponentPanel::samplesChosen( BMessage* pMessage )
{
	int32 nAction = 0;
	pMessage->FindInt32( "action", &nAction );
	std::vector<std::shared_ptr<Sample>> samples;
	entry_ref ref;
	BString sFailed;
	for ( int32 i = 0; pMessage->FindRef( "refs", i, &ref ) == B_OK; i++ ) {
		BPath path( &ref );
		auto pSample = Sample::load( QString::fromUtf8( path.Path() ) );
		if ( pSample == nullptr ) {
			sFailed << "\n" << path.Leaf();
			continue;
		}
		samples.push_back( pSample );
		BPath parent;
		path.GetParent( &parent );
		Preferences::get_instance()->setLastOpenLayerDirectory( QString::fromUtf8( parent.Path() ) );
	}
	if ( ! sFailed.IsEmpty() ) {
		BString sText( B_TRANSLATE( "Hydrogen couldn't load these samples:" ) );
		sText << sFailed;
		( new BAlert( B_TRANSLATE( "Samples" ), sText, B_TRANSLATE( "OK" ), nullptr, nullptr,
					  B_WIDTH_AS_USUAL, B_STOP_ALERT ) )->Go( nullptr );
	}
	if ( samples.empty() ) {
		return;
	}

	switch ( static_cast<SampleAction>( nAction ) ) {
	case SampleAction::AddComponent:
		editCopy( B_TRANSLATE( "add component" ), [&]( auto pNew, auto ) {
			auto pComponent = std::make_shared<InstrumentComponent>( "New component" );
			pNew->addComponent( pComponent );
			for ( const auto& pSample : samples ) {
				pNew->addLayer( pComponent, std::make_shared<InstrumentLayer>( pSample ), -1,
								Event::Trigger::Default );
			}
			// Several samples at once: spread their velocity ranges evenly.
			if ( samples.size() > 1 ) {
				pComponent->setAutoVelocity();
			}
			return true;
		} );
		break;
	case SampleAction::AddLayer: {
		const int nInsert = layer() != nullptr ? selectedLayer() + 1 : -1;
		editCopy( B_TRANSLATE( "add layers" ), [&]( auto pNew, auto pComponent ) {
			int nIndex = nInsert;
			for ( const auto& pSample : samples ) {
				pNew->addLayer( pComponent, std::make_shared<InstrumentLayer>( pSample ), nIndex,
								Event::Trigger::Default );
				if ( nIndex >= 0 ) {
					nIndex++;
				}
			}
			if ( samples.size() > 1 ) {
				pComponent->setAutoVelocity();
			}
			return true;
		} );
		break;
	}
	case SampleAction::ReplaceLayer: {
		const int nLayer = selectedLayer();
		editCopy( B_TRANSLATE( "replace sample" ), [&]( auto pNew, auto pComponent ) {
			auto pLayer = pComponent->getLayer( nLayer );
			if ( pLayer == nullptr ) {
				return false;
			}
			pNew->setSample( pComponent, pLayer, samples[ 0 ], Event::Trigger::Default );
			return true;
		} );
		break;
	}
	}
}

void ComponentPanel::MessageReceived( BMessage* pMessage )
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pComponent = component();
	auto pLayer = layer();
	float fValue = 0;
	pMessage->FindFloat( "value", &fValue );
	const int nSelected = selectedLayer();

	switch ( pMessage->what ) {
	case kMsgCollapse:
		m_pEditor->m_expanded[ m_nIndex ] = ! m_bExpanded;
		// Rebuilding deletes this panel: do it after this message.
		BMessenger( m_pEditor ).SendMessage( 'CErb' );
		return;
	case kMsgRenameComponent:
		if ( pComponent != nullptr ) {
			BMessage reply( kMsgDoRename );
			( new NameWindow( B_TRANSLATE( "Rename component" ), B_TRANSLATE( "Rename" ),
							  toBString( pComponent->getName() ), BMessenger( this ), reply,
							  Window()->Frame() ) )->Show();
		}
		return;
	case kMsgDoRename: {
		BString sName;
		if ( pComponent != nullptr && pMessage->FindString( "name", &sName ) == B_OK &&
			 ! sName.Trim().IsEmpty() ) {
			if ( auto pCommand = SongActions::renameComponent( m_nIndex, toBString( pComponent->getName() ),
															   sName ) ) {
				UndoStack::get().push( std::move( pCommand ) );
				Window()->PostMessage( 'Hped' );
			}
		}
		return;
	}
	case kMsgAddComponent:
		chooseSamples( SampleAction::AddComponent );
		return;
	case kMsgAddLayer:
		chooseSamples( SampleAction::AddLayer );
		return;
	case kMsgReplaceLayer:
		chooseSamples( pLayer != nullptr ? SampleAction::ReplaceLayer : SampleAction::AddLayer );
		return;
	case kMsgSamplesChosen:
		samplesChosen( pMessage );
		return;
	case kMsgDuplicateComponent:
		editCopy( B_TRANSLATE( "duplicate component" ), [&]( auto pNew, auto pNewComponent ) {
			auto pCopy = std::make_shared<InstrumentComponent>( pNewComponent );
			pCopy->setName( Filesystem::appendNumberOrIncrement( pCopy->getName() ) );
			pNew->addComponent( pCopy );
			return true;
		} );
		return;
	case kMsgDeleteComponent:
		editCopy( B_TRANSLATE( "delete component" ), [&]( auto pNew, auto ) {
			if ( pNew->getComponents()->size() <= 1 ) {
				return false;
			}
			pNew->removeComponent( m_nIndex );
			return true;
		} );
		return;
	case kMsgDuplicateLayer:
		editCopy( B_TRANSLATE( "duplicate layer" ), [&]( auto pNew, auto pNewComponent ) {
			auto pOriginal = pNewComponent->getLayer( nSelected );
			if ( pOriginal == nullptr ) {
				return false;
			}
			pNew->addLayer( pNewComponent, std::make_shared<InstrumentLayer>( pOriginal ),
							nSelected + 1, Event::Trigger::Suppress );
			m_pEditor->m_selectedLayer[ m_nIndex ] = nSelected + 1;
			return true;
		} );
		return;
	case kMsgDeleteLayer:
		editCopy( B_TRANSLATE( "delete layer" ), [&]( auto pNew, auto pNewComponent ) {
			if ( pNewComponent->getLayer( nSelected ) == nullptr ) {
				return false;
			}
			pNew->removeLayer( pNewComponent, nSelected, Event::Trigger::Default );
			return true;
		} );
		return;
	case kMsgLayerSelected:
		update();
		return;
	}

	// Direct changes, like upstream: no undo, but the kit is modified.
	if ( pComponent == nullptr ) {
		BBox::MessageReceived( pMessage );
		return;
	}
	switch ( pMessage->what ) {
	case kMsgComponentMute:
		pComponent->setIsMuted( m_pMute->Value() == B_CONTROL_ON );
		break;
	case kMsgComponentSolo:
		pComponent->setIsSoloed( m_pSolo->Value() == B_CONTROL_ON );
		break;
	case kMsgComponentGain:
		pComponent->setGain( fValue );
		break;
	case kMsgSelection: {
		int32 nIndex = 0;
		pMessage->FindInt32( "index", &nIndex );
		pComponent->setSelection( static_cast<InstrumentComponent::Selection>( nIndex ) );
		break;
	}
	case kMsgLayerMute:
		if ( pLayer != nullptr ) {
			pLayer->setIsMuted( m_pLayerMute->Value() == B_CONTROL_ON );
		}
		break;
	case kMsgLayerSolo:
		if ( pLayer != nullptr ) {
			pLayer->setIsSoloed( m_pLayerSolo->Value() == B_CONTROL_ON );
		}
		break;
	case kMsgLayerGain:
		if ( pLayer != nullptr ) {
			pLayer->setGain( fValue );
		}
		break;
	case kMsgCoarse:
	case kMsgFine:
		if ( pLayer != nullptr ) {
			pLayer->setPitchOffset( std::round( m_pCoarse->value() ) + m_pFine->value() / 100.0f );
		}
		break;
	default:
		BBox::MessageReceived( pMessage );
		return;
	}
	pHydrogen->setDrumkitModified( true );
	update();
}

// ---------------------------------------------------------------------------
// LayerPreview

void LayerPreview::Draw( BRect )
{
	const EditorColors& c = Colors::get();
	const BRect bounds = Bounds();
	const float w = bounds.Width();
	SetHighColor( c.window );
	FillRect( bounds );
	auto pComponent = m_pPanel->component();
	const auto layers = pComponent != nullptr ? pComponent->getLayers()
											  : std::vector<std::shared_ptr<InstrumentLayer>>();
	const int nLayers = (int) layers.size();
	const int nSelected = m_pPanel->selectedLayer();
	BFont font( be_plain_font );
	font.SetSize( std::max( 9.0f, be_plain_font->Size() * 0.85f ) );
	SetFont( &font );
	font_height fh;
	font.GetHeight( &fh );
	SetDrawingMode( B_OP_ALPHA );
	SetBlendingMode( B_PIXEL_ALPHA, B_ALPHA_OVERLAY );

	// Header: red where no layer covers the velocity, then each layer's
	// range shaded by its rank and numbered.
	SetHighColor( c.buttonRed );
	FillRect( BRect( 0, 0, w, kHeader - 1 ) );
	std::vector<int> order( nLayers );
	for ( int i = 0; i < nLayers; i++ ) {
		order[ i ] = i;
	}
	std::sort( order.begin(), order.end(), [&]( int a, int b ) {
		return layers[ a ]->getEndVelocity() > layers[ b ]->getEndVelocity();
	} );
	for ( int nRank = 0; nRank < nLayers; nRank++ ) {
		const int i = order[ nRank ];
		const float x1 = std::floor( layers[ i ]->getStartVelocity() * w );
		const float x2 = std::floor( layers[ i ]->getEndVelocity() * w );
		const int nShade = nLayers > 1 ? 100 - 45 + (int) std::round( ( nLayers - 1 - nRank ) * 90.0f / ( nLayers - 1 ) )
									   : 100;
		const rgb_color shade = nShade >= 100 ? Colors::lighter( c.window, nShade ) : Colors::darker( c.window, 10000 / nShade );
		SetHighColor( shade );
		FillRect( BRect( x1, 0, x2, kHeader - 1 ) );
		SetHighColor( i == nSelected ? c.highlight : Colors::darker( c.windowText, 145 ) );
		StrokeRect( BRect( x1, 0, x2, kHeader - 1 ) );
		BString sNumber;
		sNumber << i + 1;
		SetHighColor( c.windowText );
		DrawString( sNumber, BPoint( ( x1 + x2 - StringWidth( sNumber ) ) / 2,
									 ( kHeader + fh.ascent - fh.descent ) / 2 ) );
	}

	if ( nLayers == 0 ) {
		SetHighColor( c.windowText );
		DrawString( "< - >", BPoint( 10, kHeader + ( kLayerHeight + fh.ascent - fh.descent ) / 2 ) );
	}
	for ( int i = 0; i < nLayers; i++ ) {
		const auto& pLayer = layers[ i ];
		const float y = kHeader + 1 + i * kLayerHeight;
		const float x1 = std::floor( pLayer->getStartVelocity() * w );
		const float x2 = std::floor( pLayer->getEndVelocity() * w );
		BString sLabel;
		if ( pLayer->getSample() == nullptr ) {
			SetHighColor( c.buttonRed );
			FillRect( BRect( x1, y, x2, y + kLayerHeight - 1 ) );
			sLabel << i + 1 << ": " << toBString( pLayer->getFallbackSampleFileName() );
		}
		else {
			const rgb_color bar = pLayer->getIsMuted() || pComponent->getIsMuted() ? c.mute
				: pLayer->getIsSoloed() ? c.solo : c.accent;
			fillGradient( this, BRect( x1, y, x2, y + kLayerHeight - 1 ), bar );
			sLabel << i + 1 << ": " << fileName( pLayer->getSample()->getFilePath() );
		}
		rgb_color text = c.windowText;
		text.alpha = 155;
		SetHighColor( text );
		font.TruncateString( &sLabel, B_TRUNCATE_END, w - 14 );
		DrawString( sLabel, BPoint( 10, y + ( kLayerHeight + fh.ascent - fh.descent ) / 2 ) );
		SetHighColor( i == nSelected ? c.highlight : Colors::darker( c.windowText, 145 ) );
		StrokeRect( BRect( 1, y, w - 1, y + kLayerHeight - 1 ) );
	}
	SetDrawingMode( B_OP_COPY );
	SetHighColor( 0, 0, 0 );
	StrokeRect( bounds );
}

void LayerPreview::MouseDown( BPoint where )
{
	auto pComponent = m_pPanel->component();
	if ( pComponent == nullptr ) {
		return;
	}
	const float w = Bounds().Width();
	const float fVelocity = std::clamp( where.x / w, 0.0f, 1.0f );
	const auto layers = pComponent->getLayers();
	int32 nClicks = 1;
	if ( Window()->CurrentMessage() != nullptr ) {
		Window()->CurrentMessage()->FindInt32( "clicks", &nClicks );
	}
	const int nRow = rowAt( where.y );

	// Grab an edge of the selected layer's range to drag it.
	if ( auto pSelected = m_pPanel->layer(); pSelected != nullptr && nRow == m_pPanel->selectedLayer() ) {
		if ( std::abs( where.x - pSelected->getStartVelocity() * w ) <= kGrabMargin ) {
			m_drag = Drag::Start;
		}
		else if ( std::abs( where.x - pSelected->getEndVelocity() * w ) <= kGrabMargin ) {
			m_drag = Drag::End;
		}
		if ( m_drag != Drag::None ) {
			SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS );
			return;
		}
	}

	if ( nRow < 0 ) {
		// The header plays the component as the sampler would choose,
		// and selects the topmost layer covering that velocity.
		m_pPanel->play( fVelocity, nullptr );
		for ( int i = 0; i < (int) layers.size(); i++ ) {
			if ( layers[ i ]->getStartVelocity() * w < where.x && where.x < layers[ i ]->getEndVelocity() * w ) {
				m_pPanel->selectLayer( i );
				break;
			}
		}
		return;
	}
	if ( nRow >= (int) layers.size() ) {
		return;
	}
	m_pPanel->selectLayer( nRow );
	if ( nClicks == 2 ) {
		m_pPanel->chooseSamples( SampleAction::ReplaceLayer );
		return;
	}
	m_pPanel->play( fVelocity, layers[ nRow ] );
}

void LayerPreview::MouseMoved( BPoint where, uint32, const BMessage* )
{
	auto pLayer = m_pPanel->layer();
	if ( m_drag == Drag::None || pLayer == nullptr ) {
		return;
	}
	const float fVelocity = std::clamp( where.x / Bounds().Width(), 0.0f, 1.0f );
	if ( m_drag == Drag::Start && fVelocity < pLayer->getEndVelocity() ) {
		pLayer->setStartVelocity( fVelocity );
	}
	else if ( m_drag == Drag::End && fVelocity > pLayer->getStartVelocity() ) {
		pLayer->setEndVelocity( fVelocity );
	}
	BString sTip;
	sTip.SetToFormat( "%.2f (MIDI %d)", fVelocity, std::clamp( (int) ( fVelocity * 127 ), 0, 127 ) + 1 );
	SetToolTip( sTip );
	ShowToolTip( ToolTip() );
	Invalidate();
}

void LayerPreview::MouseUp( BPoint )
{
	if ( m_drag != Drag::None ) {
		m_drag = Drag::None;
		SetToolTip( (const char*) nullptr );
		Hydrogen::get_instance()->setDrumkitModified( true );
	}
}

// ---------------------------------------------------------------------------
// WaveView

void WaveView::updatePeaks( const std::shared_ptr<InstrumentLayer>& pLayer, int nWidth )
{
	auto pSample = pLayer != nullptr ? pLayer->getSample() : nullptr;
	const float fGain = pLayer != nullptr ? pLayer->getGain() : 0;
	if ( pSample == m_pPeakSample && fGain == m_fPeakGain && nWidth == m_nPeakWidth ) {
		return;
	}
	m_pPeakSample = pSample;
	m_fPeakGain = fGain;
	m_nPeakWidth = nWidth;
	m_max.assign( nWidth, 0 );
	m_min.assign( nWidth, 0 );
	if ( pSample == nullptr || pSample->getData_L() == nullptr || nWidth <= 0 ) {
		return;
	}
	// Upstream: envelope (max/min per column) when the sample is longer
	// than the view, else one frame per pixel; left channel, scaled by gain.
	const long long nFrames = pSample->getFrames();
	const float* pData = pSample->getData_L();
	const float fScale = ( Bounds().Height() / 2 ) * fGain;
	m_bEnvelope = nFrames > nWidth;
	if ( m_bEnvelope ) {
		const long long nStep = (long long) std::ceil( (double) nFrames / nWidth );
		for ( int x = 0; x < nWidth; x++ ) {
			float fMax = 0, fMin = 0;
			for ( long long i = x * nStep; i < std::min( nFrames, ( x + 1 ) * nStep ); i++ ) {
				fMax = std::max( fMax, pData[ i ] * fScale );
				fMin = std::min( fMin, pData[ i ] * fScale );
			}
			m_max[ x ] = fMax;
			m_min[ x ] = fMin;
		}
	}
	else {
		for ( int x = 0; x < nWidth && x < nFrames; x++ ) {
			m_max[ x ] = pData[ x ] * fScale;
		}
	}
}

void WaveView::Draw( BRect )
{
	const EditorColors& c = Colors::get();
	const BRect bounds = Bounds();
	auto pLayer = m_pPanel->layer();
	const rgb_color bg = pLayer != nullptr && pLayer->getIsMuted() ? c.mute
		: pLayer != nullptr && pLayer->getIsSoloed() ? c.solo : c.accent;
	fillGradient( this, bounds, bg );
	SetHighColor( 0, 0, 0 );
	StrokeRect( bounds );
	const float fCenter = std::floor( bounds.Height() / 2 );
	SetDrawingMode( B_OP_ALPHA );
	SetHighColor( 0, 0, 0, 80 );
	const pattern dotted = { { 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 } };
	StrokeLine( BPoint( 1, fCenter ), BPoint( bounds.right - 1, fCenter ), dotted );
	SetDrawingMode( B_OP_COPY );

	const int nWidth = (int) bounds.Width();
	updatePeaks( pLayer, nWidth );
	if ( m_pPeakSample == nullptr ) {
		return;
	}
	SetHighColor( c.waveForm );
	if ( m_bEnvelope ) {
		BPolygon polygon;
		for ( int x = 0; x < nWidth; x++ ) {
			polygon.AddPoints( new BPoint( x, fCenter - m_max[ x ] ), 1 );
		}
		for ( int x = nWidth - 1; x >= 0; x-- ) {
			polygon.AddPoints( new BPoint( x, fCenter - m_min[ x ] ), 1 );
		}
		FillPolygon( &polygon );
	}
	else {
		for ( int x = 1; x < nWidth; x++ ) {
			StrokeLine( BPoint( x - 1, fCenter - m_max[ x - 1 ] ), BPoint( x, fCenter - m_max[ x ] ) );
		}
	}

	// The sample's file name, bold, at the top.
	BString sName = fileName( m_pPeakSample->getFilePath() );
	BFont font( be_bold_font );
	SetFont( &font );
	font_height fh;
	font.GetHeight( &fh );
	SetDrawingMode( B_OP_ALPHA );
	rgb_color text = c.windowText;
	text.alpha = 200;
	SetHighColor( text );
	DrawString( sName, BPoint( ( bounds.Width() - StringWidth( sName ) ) / 2, 2 + fh.ascent ) );
	SetDrawingMode( B_OP_COPY );
}

void WaveView::MouseDown( BPoint )
{
	int32 nClicks = 1;
	if ( Window()->CurrentMessage() != nullptr ) {
		Window()->CurrentMessage()->FindInt32( "clicks", &nClicks );
	}
	// Upstream opens the sample editor (not ported yet); with no layer,
	// double-click adds one.
	if ( nClicks == 2 && m_pPanel->layer() == nullptr ) {
		m_pPanel->chooseSamples( SampleAction::AddLayer );
	}
}

// ---------------------------------------------------------------------------
// ComponentEditor

ComponentEditor::ComponentEditor()
	: BView( B_TRANSLATE( "Components" ), 0 )
{
	SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
	BGroupLayout* pLayout = new BGroupLayout( B_VERTICAL, B_USE_HALF_ITEM_SPACING );
	SetLayout( pLayout );
	pLayout->SetInsets( B_USE_HALF_ITEM_INSETS );
}

void ComponentEditor::rebuild()
{
	auto pInstrument = Hydrogen::get_instance()->getSelectedInstrument();
	BGroupLayout* pLayout = static_cast<BGroupLayout*>( GetLayout() );
	for ( ComponentPanel* pPanel : m_panels ) {
		pPanel->RemoveSelf();
		delete pPanel;
	}
	m_panels.clear();
	while ( pLayout->CountItems() > 0 ) {
		BLayoutItem* pItem = pLayout->RemoveItem( (int32) 0 );
		if ( pItem->View() != nullptr ) {
			pItem->View()->RemoveSelf();
			delete pItem->View();
		}
		delete pItem;
	}

	if ( pInstrument == nullptr ) {
		pLayout->AddView( new BStringView( nullptr, B_TRANSLATE( "No instrument" ) ) );
	}
	else {
		// A different instrument starts with its first layers selected.
		const int nId = static_cast<int>( pInstrument->getId() );
		const size_t nComponents = pInstrument->getComponents()->size();
		if ( nId != m_nInstrumentId ) {
			m_expanded.assign( nComponents, true );
			m_selectedLayer.assign( nComponents, 0 );
			m_nInstrumentId = nId;
		}
		m_expanded.resize( nComponents, true );
		m_selectedLayer.resize( nComponents, 0 );
		for ( size_t i = 0; i < nComponents; i++ ) {
			ComponentPanel* pPanel = new ComponentPanel( this, (int) i );
			m_panels.push_back( pPanel );
			pLayout->AddView( pPanel );
		}
	}
	pLayout->AddItem( BSpaceLayoutItem::CreateGlue() );
	if ( auto pPane = dynamic_cast<ScrollPane*>( Parent() ) ) {
		if ( ! pPane->IsHidden( pPane ) && pPane->Bounds().Width() > 10 ) {
			pPane->contentChanged();
		}
	}
}

void ComponentEditor::AttachedToWindow()
{
	BView::AttachedToWindow();
	// The core sends no events before the GUI is ready: build now.
	rebuild();
}

void ComponentEditor::MessageReceived( BMessage* pMessage )
{
	if ( pMessage->what == 'CErb' ) {
		rebuild();
		return;
	}
	BView::MessageReceived( pMessage );
}

void ComponentEditor::refresh()
{
	for ( ComponentPanel* pPanel : m_panels ) {
		pPanel->update();
	}
}
