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

#ifndef HAIKU_SETTINGS_WINDOW_H
#define HAIKU_SETTINGS_WINDOW_H

#include <memory>

#include <Window.h>

class BButton;
class BCheckBox;
class BMenuField;
class BSpinner;
class BStringView;
class BTextControl;
class FormattedSpinner;
class MidiActionTable;
struct SettingsValues;

/** The Settings window (upstream's PreferencesDialog plus the options of
 * its MIDI control dialog), following the Haiku HIG: changes apply as
 * they are made, except driver changes, which "Apply and restart
 * driver" makes (it interrupts audio). "Revert" restores the values the
 * window opened with, "Defaults" Hydrogen's defaults. */
class SettingsWindow : public BWindow
{
public:
	SettingsWindow();
	virtual ~SettingsWindow();

	void MessageReceived( BMessage* pMessage ) override;
	bool QuitRequested() override;
	void WindowActivated( bool bActive ) override;

private:
	BView* buildGeneral();
	BView* buildAudio();
	BView* buildMidi();
	BView* buildAppearance();

	/** Fills every control from the preferences. */
	void load();
	void updateAudioStatus();
	void updateMidiPorts();
	void apply( const SettingsValues& values );
	void updateButtons();

	std::unique_ptr<SettingsValues> m_pOriginal;
	bool m_bLoading = false;

	// General
	BCheckBox* m_pRelativePaths;
	BSpinner* m_pMaxBars;
	FormattedSpinner* m_pAutosaves;
	BTextControl* m_pRubberband;
	// Audio
	BMenuField* m_pAudioDriver;
	BSpinner* m_pBufferSize;
	BMenuField* m_pSampleRate;
	BStringView* m_pAudioStatus;
	BSpinner* m_pPolyphony;
	BSpinner* m_pMetronome;
	BMenuField* m_pInterpolation;
	// MIDI
	BMenuField* m_pMidiDriver;
	BMenuField* m_pMidiInput;
	BMenuField* m_pMidiOutput;
	BMenuField* m_pNoteMapping;
	BMenuField* m_pNoteChannel;
	BCheckBox* m_pIgnoreNoteOff;
	BCheckBox* m_pClockInput;
	BCheckBox* m_pTransportInput;
	FormattedSpinner* m_pActionChannel;
	BCheckBox* m_pFeedback;
	BCheckBox* m_pClockOutput;
	BCheckBox* m_pTransportOutput;
	FormattedSpinner* m_pFeedbackChannel;
	BMenuField* m_pNoteOff;
	// Appearance
	BCheckBox* m_pSystemColors;
	BMenuField* m_pFalloff;
	BMenuField* m_pColoring;
	BSpinner* m_pPatternColors;

	MidiActionTable* m_pMidiActions;

	BButton* m_pRevert;
	BButton* m_pDefaults;
};

#endif
