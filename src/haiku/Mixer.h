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

#ifndef HAIKU_MIXER_H
#define HAIKU_MIXER_H

#include <vector>

#include <Window.h>

class BGroupView;
class BMessageRunner;
class FXPanel;
class MasterStrip;
class MixerStrip;
class ScrollPane;

/** Messages the main window forwards to the mixer. */
enum : uint32 {
	kMsgMixerRebuild = 'MXrb',		///< kit, song, effects or selection changed
	kMsgMixerUpdate = 'MXup',		///< strip parameters changed
	kMsgMixerMaster = 'MXms',		///< master settings changed
	kMsgMixerNotes = 'MXnr',		///< "strip" int32s: notes started on these strips
};

/** The mixer window (upstream's Mixer): one strip per instrument, the
 * effects slots and the master section. Peaks are polled every 50 ms;
 * like upstream, nothing in the mixer is undoable. Closing hides it. */
class MixerWindow : public BWindow
{
public:
	MixerWindow();
	virtual ~MixerWindow();

	void MessageReceived( BMessage* pMessage ) override;
	bool QuitRequested() override;
	void Show() override;
	void Hide() override;

	/** Saves the frame and visibility (called when the app quits). */
	void saveSettings();

private:
	void rebuild();
	void updatePeaks();

	BGroupView* m_pStrips;
	ScrollPane* m_pStripPane;
	std::vector<MixerStrip*> m_strips;
	FXPanel* m_pFX;
	MasterStrip* m_pMaster;
	BMessageRunner* m_pPeakRunner;
};

#endif
