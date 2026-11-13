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

#ifndef HAIKU_INSTRUMENT_PARAMETERS_H
#define HAIKU_INSTRUMENT_PARAMETERS_H

#include <View.h>

class BCheckBox;
class BStringView;
class FormattedSpinner;
class Knob;
class NameBanner;

/** The rack's "Parameters" tab (upstream's InstrumentEditor): the
 * selected instrument's envelope, gain, filter, pitch, MIDI output and
 * hi-hat settings. Like upstream, changes go straight to the instrument
 * and mark the drumkit modified; only renaming is undoable. */
class InstrumentParameters : public BView
{
public:
	InstrumentParameters();

	void AttachedToWindow() override;
	void MessageReceived( BMessage* pMessage ) override;

	/** Reloads every control from the selected instrument. */
	void refresh();

private:
	NameBanner* m_pName;
	Knob* m_pAttack;
	Knob* m_pDecay;
	Knob* m_pSustain;
	Knob* m_pRelease;
	BStringView* m_pGainValue;
	Knob* m_pGain;
	FormattedSpinner* m_pMuteGroup;
	BCheckBox* m_pAutoStop;
	BCheckBox* m_pApplyVelocity;
	BCheckBox* m_pFilterActive;
	Knob* m_pCutoff;
	Knob* m_pResonance;
	BStringView* m_pPitchValue;
	Knob* m_pCoarse;
	Knob* m_pFine;
	Knob* m_pRandom;
	FormattedSpinner* m_pMidiChannel;
	FormattedSpinner* m_pMidiNote;
	FormattedSpinner* m_pHihatGroup;
	FormattedSpinner* m_pHihatMin;
	FormattedSpinner* m_pHihatMax;
	bool m_bUpdating;
};

#endif
