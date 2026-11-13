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

#ifndef HAIKU_NAME_WINDOW_H
#define HAIKU_NAME_WINDOW_H

#include <Messenger.h>
#include <Window.h>

class BTextControl;

/** A small window asking for a name (renaming a pattern or an
 * instrument). On the action button it sends a copy of the reply
 * message with the entered text in "name" to the target. */
class NameWindow : public BWindow
{
public:
	NameWindow( const char* sTitle, const char* sAction, const char* sName,
				BMessenger target, const BMessage& reply, BRect parentFrame );

	void MessageReceived( BMessage* pMessage ) override;

private:
	BMessenger m_target;
	BMessage m_reply;
	BTextControl* m_pName;
};

#endif
