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

#ifndef HAIKU_SETTINGS_H
#define HAIKU_SETTINGS_H

#include <Message.h>
#include <Path.h>
#include <Rect.h>

/** Settings of the native GUI that the core doesn't keep (window
 * frames, split positions), as a flattened BMessage in
 * ~/config/settings/Hydrogen/Window settings. The core's own
 * preferences live next to it in hydrogen.conf. */
namespace Settings
{
	/** ~/config/settings/Hydrogen, created if missing. */
	BPath directory();
	/** ~/config/settings/Hydrogen/data (songs, kits, patterns), created
	 * if missing. */
	BPath userDataDirectory();
	BPath logFile();

	BMessage& get();
	void save();

	/** The stored frame for @a name if it fits on screen, else @a fallback. */
	BRect frame( const char* sName, BRect fallback );
	void setFrame( const char* sName, BRect frame );
};

#endif
