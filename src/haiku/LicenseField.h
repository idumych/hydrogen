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

#ifndef HAIKU_LICENSE_FIELD_H
#define HAIKU_LICENSE_FIELD_H

#include <SupportDefs.h>

#include <QString>

#include <core/License.h>

class BMenuField;
class BMessage;
class BTextControl;

/** Upstream's licence combo box with its licence string below: a menu of
 * licence types and the text written into the file, hidden for
 * "Unspecified". The window puts both views into its layout and passes
 * kMsgChosen messages to chosen(). */
class LicenseField
{
public:
	static const uint32 kMsgChosen = 'LFch';

	LicenseField( const char* sName, const char* sLabel, const H2Core::License& license );

	BMenuField* menuField() const { return m_pMenu; }
	BTextControl* textControl() const { return m_pText; }

	/** Handles a kMsgChosen message; false if it's another field's. */
	bool chosen( BMessage* pMessage );
	/** The licence as chosen, held by @a sCopyrightHolder. */
	H2Core::License license( const QString& sCopyrightHolder ) const;
	/** Whether the text matches the chosen type (upstream's check). */
	bool isConsistent() const;
	void setEnabled( bool bEnabled );
	/** Call once the views are in a window's layout. */
	void updateVisibility();

private:
	int32 type() const;

	BMenuField* m_pMenu;
	BTextControl* m_pText;
};

#endif
