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

#ifndef HAIKU_APP_H
#define HAIKU_APP_H

#include <Application.h>
#include <String.h>

class MainWindow;

extern const char* kAppSignature;

/** The native Haiku application. Boots the Hydrogen core with Haiku
 * paths (settings in ~/config/settings/Hydrogen, user data in its
 * data folder), opens the main window and routes songs
 * opened from Tracker or the command line to it. */
class App : public BApplication
{
public:
	App();
	virtual ~App();

	void ArgvReceived( int32 argc, char** argv ) override;
	void RefsReceived( BMessage* pMessage ) override;
	void ReadyToRun() override;
	void AboutRequested() override;
	bool QuitRequested() override;

private:
	void bootCore();
	void shutdownCore();

	MainWindow* m_pMainWindow;
	BString m_sInitialSong;
	bool m_bCoreRunning;
};

#endif
