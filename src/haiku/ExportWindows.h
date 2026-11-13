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

#ifndef HAIKU_EXPORT_WINDOWS_H
#define HAIKU_EXPORT_WINDOWS_H

#include <vector>

#include <String.h>
#include <Window.h>

#include <core/Helpers/Filesystem.h>
#include <core/Sampler/Interpolation.h>

class BButton;
class BCheckBox;
class BDecimalSpinner;
class BFilePanel;
class BMenuField;
class BStatusBar;
class BTextControl;

/** Sent by the main window with the core's export progress ("value":
 * 0…100, -1 on failure). */
const uint32 kMsgExportProgress = 'Hxpg';

/** File > Export song (upstream's ExportSongDialog): renders the song
 * to an audio file, or one file per instrument, through the core's
 * DiskWriter driver. */
class ExportSongWindow : public BWindow
{
public:
	ExportSongWindow( BRect parentFrame );
	virtual ~ExportSongWindow();

	void MessageReceived( BMessage* pMessage ) override;
	bool QuitRequested() override;

private:
	H2Core::Filesystem::AudioFormat selectedFormat() const;
	void formatChanged();
	void fileNameChanged();
	void showPanel();
	void startExport();
	/** Starts the next instrument's file; false when none is left. */
	bool exportNextTrack();
	void progress( int nValue );
	void setExporting( bool bExporting );
	/** Asks before replacing @a sPath. False to stop. */
	bool confirmReplace( const BString& sPath, bool bOfferAll );
	void failed();

	BTextControl* m_pFileName;
	BMenuField* m_pFormat;
	BMenuField* m_pSampleRate;
	BMenuField* m_pSampleDepth;
	BDecimalSpinner* m_pCompression;
	BMenuField* m_pMode;
	BCheckBox* m_pTimeline;
	BCheckBox* m_pRubberband;
	BMenuField* m_pInterpolation;
	BStatusBar* m_pProgress;
	BButton* m_pExport;
	BButton* m_pClose;
	BButton* m_pChoose;
	BFilePanel* m_pPanel = nullptr;

	std::vector<H2Core::Filesystem::AudioFormat> m_formats;
	bool m_bExporting = false;
	bool m_bSessionActive = false;
	bool m_bTrackouts = false;
	bool m_bReplaceAll = false;
	/** The file chosen in the save panel, which asked about replacing it. */
	BString m_sPanelPath;
	int m_nInstrument = 0;
	BString m_sTemplate;

	bool m_bOldTimeline;
	int m_nOldRubberband;
	H2Core::Interpolation::InterpolateMode m_oldInterpolation;
};

/** File > Export MIDI file (upstream's ExportMidiDialog). */
class ExportMidiWindow : public BWindow
{
public:
	ExportMidiWindow( BRect parentFrame );
	virtual ~ExportMidiWindow();

	void MessageReceived( BMessage* pMessage ) override;

private:
	void exportFile();

	BTextControl* m_pFileName;
	BMenuField* m_pMode;
	BCheckBox* m_pHumanize;
	BFilePanel* m_pPanel = nullptr;
	BString m_sPanelPath;
};

#endif
