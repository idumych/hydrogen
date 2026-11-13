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

#ifndef HAIKU_PATTERN_ACTIONS_H
#define HAIKU_PATTERN_ACTIONS_H

#include <Messenger.h>

#include <core/Basics/Instrument.h>
#include <core/Basics/Note.h>

#include "UndoStack.h"

/** Pattern edits as undoable commands, ported from upstream's
 * PatternEditor::addOrRemoveNoteAction, editNotePropertiesAction and
 * PatternEditorPanel::patternSizeChangedAction. Each locks the audio
 * engine around the change and marks the pattern modified. The core
 * sends no event for note edits, so the observer (the main window)
 * gets kMsgPatternEdited instead. */
namespace PatternActions
{
	/** Everything needed to recreate or find one note. */
	struct NoteSpec {
		int nPosition = 0;
		H2Core::Instrument::Id id = H2Core::Instrument::EmptyId;
		H2Core::Instrument::Type sType;
		int nLength = LENGTH_ENTIRE_SAMPLE;
		float fVelocity = VELOCITY_DEFAULT;
		float fPan = PAN_DEFAULT;
		float fLeadLag = LEAD_LAG_DEFAULT;
		float fProbability = PROBABILITY_DEFAULT;
		H2Core::Note::Key key = H2Core::Note::KeyDefault;
		H2Core::Note::Octave octave = H2Core::Note::OctaveDefault;
		bool bNoteOff = false;

		static NoteSpec from( const std::shared_ptr<H2Core::Note>& pNote );
	};

	void setObserver( const BMessenger& observer );

	/** Adds a note, or removes the matching one, from pattern @a nPattern. */
	std::unique_ptr<UndoCommand> addNote( int nPattern, const NoteSpec& note );
	std::unique_ptr<UndoCommand> removeNote( int nPattern, const NoteSpec& note );
	std::unique_ptr<UndoCommand> setVelocity( int nPattern, const NoteSpec& note,
											  float fNewVelocity );
	/** Sets a velocity without recording it, for live feedback while
	 * dragging; record the drag with setVelocity() when it ends. */
	void previewVelocity( int nPattern, const NoteSpec& note, float fVelocity );
	std::unique_ptr<UndoCommand> setPatternSize( int nPattern, int nNewLength,
												 int nNewDenominator );
};

#endif
