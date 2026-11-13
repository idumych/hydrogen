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


#ifndef HAIKU_AUTOSAVE_H
#define HAIKU_AUTOSAVE_H

#include <QString>

/** Upstream's song autosave files: ".<name>.autosave.h2song" next to the
 * song (in the user's songs folder if that isn't writable), and
 * Filesystem::emptyPath() standing for a song that was never saved. */
namespace Autosave
{
	/** If an autosave of @a sSongPath has newer changes than the song,
	 * asks whether to recover them and returns its path if so, else ""
	 * (upstream's HydrogenApp::findAutoSaveFile()). */
	QString offerRecovery( const QString& sSongPath );

	/** Removes the autosave of @a sSongPath, once it's obsolete. */
	void remove( const QString& sSongPath );
}

#endif
