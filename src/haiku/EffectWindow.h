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

#ifndef HAIKU_EFFECT_WINDOW_H
#define HAIKU_EFFECT_WINDOW_H

#include <memory>
#include <vector>

#include <Messenger.h>
#include <String.h>
#include <Window.h>

class BCheckBox;
class BGroupView;
class BMenuField;
class BMessageRunner;
class BStringView;
class ParamView;
class ScrollPane;

namespace H2Core
{
class LadspaFX;
}

/** One effect slot's window (upstream's LadspaFXProperties): the plugin
 * menu, on/off, a fader per input control port (a check box for toggle
 * ports) and a meter per output port. Changes apply at once and, like
 * everything in the mixer upstream, aren't undoable. */
class EffectWindow : public BWindow
{
public:
	/** Shows slot @a nFX's window, opening it if needed. @a mixer gets
	 * kMsgMixerRebuild when the slot changes. */
	static void open( int nFX, BMessenger mixer );

	/** Puts the plugin @a sLabel from library @a sFile (empty: none) into
	 * slot @a nFX (upstream's selectFXBtnClicked/removeFXBtnClicked). */
	static void setPlugin( int nFX, const BString& sFile, const BString& sLabel );

	void MessageReceived( BMessage* pMessage ) override;
	bool QuitRequested() override;

private:
	EffectWindow( int nFX, BMessenger mixer );

	std::shared_ptr<H2Core::LadspaFX> effect() const;
	void rebuild();
	void updateHeader();
	void updateOutputs();

	int m_nFX;
	BMessenger m_mixer;
	std::shared_ptr<H2Core::LadspaFX> m_pShown;
	bool m_bShownEnabled = false;
	BMenuField* m_pPlugin;
	BCheckBox* m_pActive;
	BStringView* m_pInfo;
	BGroupView* m_pPorts;
	ScrollPane* m_pPane;
	std::vector<ParamView*> m_inputs;
	std::vector<BStringView*> m_inputValues;
	std::vector<ParamView*> m_outputs;
	std::vector<BStringView*> m_outputValues;
	BMessageRunner* m_pRunner = nullptr;
};

#endif
