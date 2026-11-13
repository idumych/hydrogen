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

#ifndef HAIKU_SAMPLE_EDITOR_H
#define HAIKU_SAMPLE_EDITOR_H

#include <memory>

#include <Messenger.h>
#include <Window.h>

#include <core/Basics/Instrument.h>
#include <core/Basics/Sample.h>
#include <core/License.h>

class BButton;
class BDecimalSpinner;
class BMenuField;
class BMessageRunner;
class BSlider;
class BSpinner;
class BStringView;
class BView;
class DetailView;
class OriginalView;
class TargetView;

/** The sample editor (upstream's SampleEditor): start, loop and end
 * frames, loop mode and count, Rubberband time stretch and pitch, and
 * volume and pan envelopes of one layer's sample. Edits are previewed
 * at once; Apply replaces the instrument by an edited copy as one
 * undoable step, Revert goes back to the applied state. */
class SampleEditorWindow : public BWindow
{
public:
	/** Opens the editor for layer @a nLayer of component @a nComponent of
	 * the selected instrument, replacing any editor already open. */
	static void open( int nComponent, int nLayer, BRect parentFrame, BMessenger owner );

	void MessageReceived( BMessage* pMessage ) override;
	bool QuitRequested() override;
	void WindowActivated( bool bActive ) override;

	enum class Marker { None, Start, Loop, End };
	enum class Envelope { Velocity, Pan };

	/** What the views read and change. */
	struct Edit {
		H2Core::Sample::Loops loops;
		H2Core::Sample::Rubberband rubberband;
		H2Core::Sample::VelocityEnvelope velocity;
		H2Core::Sample::PanEnvelope pan;
		bool operator==( const Edit& other ) const;
	};
	Edit m_edit;
	std::shared_ptr<H2Core::Sample> m_pOriginal;	///< the file, unedited
	std::shared_ptr<H2Core::Sample> m_pPreview;		///< the file with m_edit applied
	float m_fGain = 1;
	Marker m_selected = Marker::Start;
	Envelope m_envelope = Envelope::Velocity;
	bool m_bPlaying = false;
	/** Playhead in the original sample's frames, -1 when not playing. */
	double m_fPlayheadOriginal = -1;
	/** Playhead in the preview's frames, -1 when not playing it. */
	double m_fPlayheadTarget = -1;

	/** A marker moved; keeps start ≤ loop ≤ end by pushing the others. */
	void setMarker( Marker marker, long long nFrame );
	/** Envelope edited by the target view. */
	void envelopeChanged();

private:
	SampleEditorWindow( std::shared_ptr<H2Core::Instrument> pInstrument, int nComponent,
						int nLayer, BRect parentFrame, BMessenger owner );

	/** Reads the layer's settings; false if it's gone. */
	bool bind();
	void load();
	void updateControls();
	void updateTitle();
	void scheduleUpdate();
	void updatePreview();
	void apply();
	void startPlayback( bool bOriginal );
	void stopPlayback();
	void tick();
	std::shared_ptr<H2Core::Sample> render() const;

	BMessenger m_owner;
	H2Core::Instrument::Id m_nInstrumentId;
	int m_nComponent;
	int m_nLayer;
	std::shared_ptr<H2Core::Instrument> m_pInstrument;
	std::shared_ptr<H2Core::Sample> m_pLayerSample;	///< as last read from the layer
	QString m_sFilePath;
	H2Core::License m_license;
	Edit m_applied;
	bool m_bRubberbandAvailable;

	OriginalView* m_pOriginalView;
	DetailView* m_pDetailView;
	BSlider* m_pZoom;
	BSpinner* m_pStart;
	BSpinner* m_pLoop;
	BMenuField* m_pMode;
	BSpinner* m_pCount;
	BSpinner* m_pEnd;
	BView* m_pRubberbandBox;
	BMenuField* m_pLength;
	BStringView* m_pRatio;
	BDecimalSpinner* m_pPitch;
	BMenuField* m_pCrispness;
	BStringView* m_pNewLength;
	BMenuField* m_pEnvelopeMenu;
	TargetView* m_pTargetView;
	BButton* m_pPlay;
	BButton* m_pPlayOriginal;
	BButton* m_pRevert;
	BButton* m_pApply;
	BMessageRunner* m_pUpdateRunner = nullptr;
	BMessageRunner* m_pPlayRunner = nullptr;
	bool m_bPlayingOriginal = false;
	bool m_bUpdatePending = false;
	std::shared_ptr<H2Core::Instrument> m_pPlayInstrument;
	long long m_nLastRealtimeFrame = 0;
	double m_fPlayed = 0;
	double m_fPlayLength = 0;
	double m_fScaling = 1;
};

#endif
