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

#ifndef HAIKU_PROPERTIES_WINDOW_H
#define HAIKU_PROPERTIES_WINDOW_H

#include <Messenger.h>
#include <Window.h>

class BMenuField;
class BSpinner;
class BTextControl;
class BTextView;

/** Song or pattern properties (upstream's SongPropertiesDialog and
 * PatternPropertiesDialog): name, version, author, licence, notes and
 * tags. "Apply" changes them as one undoable step. */
class PropertiesWindow : public BWindow
{
public:
	/** @a nPattern -1 edits the song, otherwise that pattern. */
	PropertiesWindow( int nPattern, BRect parentFrame, BMessenger owner );

	void MessageReceived( BMessage* pMessage ) override;

private:
	void updateLicenseText();

	int m_nPattern;
	BMessenger m_owner;
	BTextControl* m_pName;
	BSpinner* m_pVersion;
	BTextControl* m_pAuthor;
	BMenuField* m_pLicense;
	BTextControl* m_pLicenseText;
	BTextView* m_pNotes;
	BTextControl* m_pTags;
};

#endif
