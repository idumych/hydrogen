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

#ifndef HAIKU_COMPONENT_EDITOR_H
#define HAIKU_COMPONENT_EDITOR_H

#include <vector>

#include <View.h>

class ComponentPanel;

/** The rack's "Components" tab (upstream's ComponentEditor): one panel
 * per component of the selected instrument, each with its layers'
 * velocity ranges, the selected layer's waveform and settings.
 * Structural edits (components, layers, samples) replace the instrument
 * by an edited copy and are undoable, as upstream does; gains, mute,
 * solo, pitch and velocity ranges change the instrument directly. */
class ComponentEditor : public BView
{
public:
	ComponentEditor();

	void AttachedToWindow() override;
	void MessageReceived( BMessage* pMessage ) override;

	/** Components or layers changed: rebuild the panels. */
	void rebuild();
	/** Values changed: update the panels in place. */
	void refresh();

	/** Per component, kept across rebuilds: expanded? selected layer? */
	std::vector<bool> m_expanded;
	std::vector<int> m_selectedLayer;

private:
	std::vector<ComponentPanel*> m_panels;
	int m_nInstrumentId = -2;
};

#endif
