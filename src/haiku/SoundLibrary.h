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

#ifndef HAIKU_SOUND_LIBRARY_H
#define HAIKU_SOUND_LIBRARY_H

#include <View.h>

class BTabView;
class BTextControl;
class LibraryList;

/** The rack's "Library" tab (upstream's SoundLibraryPanel): drumkits,
 * patterns and songs found by the core's sound library database,
 * grouped by where they live (session, custom folders, user, system).
 * Clicking an instrument of a kit previews it; double-click or the
 * context menu loads a kit, opens a song or adds a pattern. */
class SoundLibrary : public BView
{
public:
	SoundLibrary();

	void AttachedToWindow() override;
	void MessageReceived( BMessage* pMessage ) override;

	/** The database changed: rebuild all three lists. */
	void refresh();

private:
	BTextControl* m_pSearch;
	BTabView* m_pTabs;
	LibraryList* m_pDrumkits;
	LibraryList* m_pPatterns;
	LibraryList* m_pSongs;
};

#endif
