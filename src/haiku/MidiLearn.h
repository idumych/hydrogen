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


#ifndef HAIKU_MIDI_LEARN_H
#define HAIKU_MIDI_LEARN_H

#include <memory>
#include <utility>
#include <vector>

#include <Messenger.h>
#include <String.h>

#include <core/Midi/MidiAction.h>
#include <core/Midi/MidiEvent.h>

class BView;
class UndoCommand;

/** A control that can be bound to MIDI (upstream's MidiLearnable):
 * Shift+click opens the learn window, and the tooltip lists the action's
 * bindings. The control calls learnClick() first in MouseDown() and
 * updateMidiToolTip() in GetToolTipAt(). */
class MidiLearnable
{
public:
	virtual ~MidiLearnable() = default;

	/** nullptr: the control isn't MIDI operable. */
	void setMidiAction( std::shared_ptr<MidiAction> pAction );
	const std::shared_ptr<MidiAction>& midiAction() const { return m_pMidiAction; }

protected:
	/** Shift+click: opens the learn window. True if it was one. */
	bool learnClick( BView* pView );
	/** Rewrites @a pView's tooltip: its own text, then the bindings. */
	void updateMidiToolTip( BView* pView );

private:
	std::shared_ptr<MidiAction> m_pMidiAction;
	BString m_sBaseToolTip;
	BString m_sComposedToolTip;
};

namespace MidiLearn
{
	using Binding = std::pair<H2Core::MidiEvent::Type, H2Core::Midi::Parameter>;

	/** The main window, told when the undo stack changed. */
	void setUndoTarget( BMessenger target );
	void undoChanged();

	/** The events bound to @a pAction. */
	std::vector<Binding> bindings( const std::shared_ptr<MidiAction>& pAction );
	/** "CC 7", "NOTE 36", "MMC_PLAY". */
	BString bindingText( const Binding& binding );
	/** The action as upstream names it, e.g. "STRIP_VOLUME_ABSOLUTE 2". */
	BString actionText( const std::shared_ptr<MidiAction>& pAction );

	/** Undoable: binds or unbinds @a binding and @a pAction. */
	std::unique_ptr<UndoCommand> bind( const Binding& binding, std::shared_ptr<MidiAction> pAction, bool bAdd );
	/** Undoable: removes every binding of @a pAction. */
	std::unique_ptr<UndoCommand> clear( std::shared_ptr<MidiAction> pAction );

	/** Opens the "Bind MIDI event" window for @a pAction. */
	void open( std::shared_ptr<MidiAction> pAction, BView* pSource );
}

#endif
