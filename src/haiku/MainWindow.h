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

#ifndef HAIKU_MAIN_WINDOW_H
#define HAIKU_MAIN_WINDOW_H

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <QString>

class BFilePanel;
class BMenu;
class BMenuItem;
class BMessageRunner;
class BSplitView;
class MixerWindow;
class PatternEditor;
class Rack;
class SongEditor;
class TransportBar;

/** The main window: menu bar, transport bar, song editor above the
 * pattern editor and instrument rack (see REFERENCE.md). Polls the
 * core's event queue every 50 ms and updates itself from it. */
class MainWindow : public BWindow
{
public:
	MainWindow();
	virtual ~MainWindow();

	void MessageReceived( BMessage* pMessage ) override;
	bool QuitRequested() override;
	void MenusBeginning() override;

	/** Opens a song file, asking about unsaved changes first. */
	void openSong( const BString& sPath );

private:
	BMenu* buildFileMenu();
	BMenu* buildEditMenu();
	BMenu* buildPatternMenu();
	BMenu* buildDrumkitMenu();
	BMenu* buildViewMenu();
	BMenu* buildOptionsMenu();
	BMenu* buildDebugMenu();
	BMenu* buildHelpMenu();

	void pollCore();
	void updateTitle();
	void updateRecentMenu();
	void updateUndoItems();

	/** Asks whether to save unsaved changes. Returns true if the caller
	 * may go on; if the user chose to save and a file panel had to be
	 * shown, @a pThen is resent once the song has been saved. */
	bool handleUnsavedChanges( BMessage* pThen );
	bool saveSong();
	void showSavePanel( BMessage* pThen );
	void doOpenSong( const BString& sPath );
	void doNewSong();

	void transport( uint32 what );

	/** (Re)starts the autosave timer if the setting changed. */
	void startAutosave();
	void autosave();
	void updateDrumkitMenu();
	void saveDrumkit();
	void exportDrumkit( const BString& sTarget );
	void importDrumkit( const BString& sSource );

	TransportBar* m_pTransportBar;
	PatternEditor* m_pPatternEditor;
	SongEditor* m_pSongEditor;
	Rack* m_pRack;
	MixerWindow* m_pMixer;
	BMenuItem* m_pMixerItem;
	BMenuItem* m_pRackItem;
	BMessenger m_settings;
	BMessenger m_exportSong;
	BMessenger m_exportMidi;
	BSplitView* m_pSplitView;
	BMenu* m_pRecentMenu;
	BMenu* m_pKitOpenMenu;
	BFilePanel* m_pKitExportPanel = nullptr;
	BMessageRunner* m_pAutosaveRunner = nullptr;
	int m_nAutosavesPerHour = -1;
	/** The last autosave file written, removed when the song's name changes. */
	QString m_sLastAutosave;
	BFilePanel* m_pKitImportPanel = nullptr;
	BMenuItem* m_pUndoItem;
	BMenuItem* m_pRedoItem;
	BFilePanel* m_pOpenPanel;
	BFilePanel* m_pSavePanel;
	BMessageRunner* m_pPollRunner;
	BMessage* m_pAfterSave;
};

#endif
