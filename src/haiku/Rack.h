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

#ifndef HAIKU_RACK_H
#define HAIKU_RACK_H

#include <TabView.h>

class ComponentEditor;
class InstrumentParameters;
class SoundLibrary;

/** The instrument rack beside the pattern editor (upstream's Rack):
 * tabs Parameters, Components and Library, 302 px wide. */
class Rack : public BTabView
{
public:
	static constexpr float kWidth = 302;

	Rack();

	void Select( int32 nIndex ) override;

	/** The selected instrument or its parameters changed. */
	void refreshInstrument();
	/** Components or layers of the selected instrument changed. */
	void rebuildComponents();
	/** The sound library's database changed. */
	void refreshLibrary();

private:
	InstrumentParameters* m_pParameters;
	SoundLibrary* m_pLibrary;
	ComponentEditor* m_pComponents;
};

#endif
