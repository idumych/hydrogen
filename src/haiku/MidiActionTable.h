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


#ifndef HAIKU_MIDI_ACTION_TABLE_H
#define HAIKU_MIDI_ACTION_TABLE_H

#include <memory>
#include <vector>

#include <View.h>

#include <core/Midi/MidiAction.h>
#include <core/Midi/MidiEvent.h>

class BButton;
class BGridLayout;
class BMessageRunner;
class ScrollPane;

/** The Settings window's "MIDI actions" tab (upstream's MidiActionTable
 * in the MIDI control dialog): one row per binding of an incoming event
 * to an action and its parameters. Edits change the bindings at once and
 * are undoable; a row only becomes a binding once it's complete. */
class MidiActionTable : public BView
{
public:
	MidiActionTable();
	virtual ~MidiActionTable();

	void AttachedToWindow() override;
	void MessageReceived( BMessage* pMessage ) override;

	/** Rereads the bindings if they changed elsewhere (MIDI learn, undo). */
	void sync();

private:
	struct Row {
		H2Core::MidiEvent::Type type = H2Core::MidiEvent::Type::Null;
		int nParameter = 0;
		std::shared_ptr<MidiAction> pAction;

		bool isComplete() const;
	};

	void rebuild();
	/** Replaces row @a nRow by @a row, undoably. */
	void change( int nRow, const Row& row );
	void poll();

	std::vector<Row> m_rows;
	BGridLayout* m_pGrid;
	BView* m_pRows;
	ScrollPane* m_pPane;
	BButton* m_pAdd;
	int m_nLearning = -1;
	BMessageRunner* m_pRunner = nullptr;
};

#endif
