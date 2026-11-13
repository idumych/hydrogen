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

#ifndef HAIKU_MESSAGES_H
#define HAIKU_MESSAGES_H

#include <SupportDefs.h>

/** Message codes of the native GUI. */
enum : uint32 {
	// File
	kMsgNewSong = 'Hnew',
	kMsgOpenSong = 'Hopn',
	kMsgOpenDemo = 'Hdem',
	kMsgOpenRecent = 'Hrec',
	kMsgSaveSong = 'Hsav',
	kMsgSaveSongAs = 'Hsva',
	kMsgSongProperties = 'Hspr',
	kMsgExportMidi = 'Hxmi',
	kMsgExportSong = 'Hxsn',
	kMsgExportLilyPond = 'Hxly',
	kMsgSettings = 'Hset',

	// Edit
	kMsgUndo = 'Hund',
	kMsgRedo = 'Hred',
	kMsgUndoHistory = 'Hunh',

	// View
	kMsgShowPlaylist = 'Hvpl',
	kMsgShowDirector = 'Hvdi',
	kMsgShowMixer = 'Hvmx',
	kMsgShowRack = 'Hvrk',
	kMsgShowAutomation = 'Hvau',
	kMsgShowPlaybackTrack = 'Hvpt',
	kMsgFullScreen = 'Hvfs',

	// Help
	kMsgUserGuide = 'Hhug',
	kMsgReportBug = 'Hhbg',
	kMsgDonate = 'Hhdn',

	// Transport
	kMsgRewind = 'Trew',
	kMsgRecord = 'Trec',
	kMsgPlay = 'Tply',
	kMsgStop = 'Tstp',
	kMsgForward = 'Tfwd',
	kMsgLoop = 'Tlop',
	kMsgSongMode = 'Tsng',
	kMsgPatternMode = 'Tpat',
	kMsgBpmChanged = 'Tbpm',

	// Internal
	kMsgPollCore = 'Hpol',
	kMsgSaveAndThen = 'Hsat',
};

#endif
