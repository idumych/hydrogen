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

#include "Settings.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Screen.h>

namespace Settings
{

static BMessage sSettings;
static bool sLoaded = false;

static BPath appDirectory( directory_which which )
{
	BPath path;
	if ( find_directory( which, &path, true ) == B_OK ) {
		path.Append( "Hydrogen" );
		create_directory( path.Path(), 0755 );
	}
	return path;
}

BPath directory()
{
	return appDirectory( B_USER_SETTINGS_DIRECTORY );
}

BPath userDataDirectory()
{
	// ~/config/data belongs to the read-only package tree, so user songs,
	// kits and patterns live with the settings, like upstream's macOS
	// "Application Support/Hydrogen/data".
	BPath path = directory();
	path.Append( "data" );
	create_directory( path.Path(), 0755 );
	return path;
}

BPath logFile()
{
	BPath path;
	if ( find_directory( B_USER_LOG_DIRECTORY, &path, true ) == B_OK ) {
		path.Append( "Hydrogen.log" );
	}
	return path;
}

static BPath settingsFile()
{
	BPath path = directory();
	path.Append( "Window settings" );
	return path;
}

BMessage& get()
{
	if ( ! sLoaded ) {
		BFile file( settingsFile().Path(), B_READ_ONLY );
		if ( file.InitCheck() != B_OK || sSettings.Unflatten( &file ) != B_OK ) {
			sSettings.MakeEmpty();
		}
		sLoaded = true;
	}
	return sSettings;
}

void save()
{
	BFile file( settingsFile().Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE );
	if ( file.InitCheck() == B_OK ) {
		get().Flatten( &file );
	}
}

BRect frame( const char* sName, BRect fallback )
{
	BRect stored;
	if ( get().FindRect( sName, &stored ) != B_OK || ! stored.IsValid() ) {
		return fallback;
	}
	// Keep at least part of the window, title tab included, on screen.
	const BRect screen = BScreen().Frame();
	if ( ! screen.Intersects( stored ) || stored.top < screen.top ||
		 stored.top > screen.bottom - 20 ) {
		return fallback;
	}
	return stored;
}

void setFrame( const char* sName, BRect frame )
{
	if ( get().ReplaceRect( sName, frame ) != B_OK ) {
		get().AddRect( sName, frame );
	}
}

};
