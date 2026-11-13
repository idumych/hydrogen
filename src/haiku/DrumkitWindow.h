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

#ifndef HAIKU_DRUMKIT_WINDOW_H
#define HAIKU_DRUMKIT_WINDOW_H

#include <memory>
#include <vector>

#include <Messenger.h>
#include <Window.h>

#include <QString>

class BFilePanel;
class BSpinner;
class BTextControl;
class BTextView;
class ImagePreview;
class LicenseField;

namespace H2Core
{
class Drumkit;
}

/** The song drumkit's properties (upstream's DrumkitPropertiesDialog):
 * name, version, author, licence, notes, tags, image and its licence,
 * and the instruments' types. In Properties mode Apply changes the
 * song's kit as one undoable step; in Save as mode the kit is also
 * written to a folder of the sound library. */
class DrumkitWindow : public BWindow
{
public:
	enum class Mode { Properties, SaveAs };

	/** Opens the window for the song's drumkit. */
	static void open( Mode mode, BRect parentFrame, BMessenger owner );

	virtual ~DrumkitWindow();

	void MessageReceived( BMessage* pMessage ) override;

private:
	DrumkitWindow( Mode mode, BRect parentFrame, BMessenger owner );

	BView* buildGeneral();
	BView* buildTypes();
	/** The folder for Save as: the user's drumkits folder and the name. */
	QString defaultFolder() const;
	bool validate();
	void apply();

	Mode m_mode;
	BMessenger m_owner;
	std::shared_ptr<H2Core::Drumkit> m_pKit;	///< the song's kit when opened
	BTextControl* m_pFolder = nullptr;
	bool m_bFolderEdited = false;
	BTextControl* m_pName;
	BSpinner* m_pVersion;
	BTextControl* m_pAuthor;
	std::unique_ptr<LicenseField> m_pLicense;
	BTextView* m_pNotes;
	BTextControl* m_pTags;
	BTextControl* m_pImage;
	ImagePreview* m_pPreview;
	std::unique_ptr<LicenseField> m_pImageLicense;
	std::vector<BTextControl*> m_types;
	BFilePanel* m_pImagePanel = nullptr;
	BFilePanel* m_pFolderPanel = nullptr;
};

#endif
