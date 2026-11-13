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

#ifndef HAIKU_SONG_ACTIONS_H
#define HAIKU_SONG_ACTIONS_H

#include <memory>

#include <String.h>

#include <QString>
#include <QStringList>

#include <core/License.h>

namespace H2Core {
	class Instrument;
};

#include "UndoStack.h"

/** Song-level edits as undoable commands, ported from upstream's
 * SE_addOrRemovePatternCellAction, SE_insertPatternAction,
 * SE_deletePatternAction and SE_modifyPatternPropertiesAction. The core
 * calls behind them push their own events (GridCellToggled,
 * SelectedPatternChanged, PatternChanged), which refresh the editors. */
namespace SongActions
{
	/** Toggles pattern @a nRow in bar @a nColumn. */
	std::unique_ptr<UndoCommand> toggleCell( int nColumn, int nRow );
	/** Adds an empty pattern after the selected one. */
	std::unique_ptr<UndoCommand> newPattern();
	/** Copies pattern @a nRow below it, in all the bars it plays in. */
	std::unique_ptr<UndoCommand> duplicatePattern( int nRow );
	/** Removes pattern @a nRow and its cells. */
	std::unique_ptr<UndoCommand> deletePattern( int nRow );
	std::unique_ptr<UndoCommand> renamePattern( int nRow, const BString& sName );
	/** Removes every cell of the arrangement. */
	std::unique_ptr<UndoCommand> clearSequence();
	/** Switches the song to the drumkit at @a sPath (upstream's
	 * SE_switchDrumkitAction). */
	std::unique_ptr<UndoCommand> switchDrumkit( const BString& sPath );
	/** Renames instrument @a nInstrument (SE_replaceInstrumentAction). */
	std::unique_ptr<UndoCommand> renameInstrument( int nInstrument, const BString& sName );
	/** Replaces an instrument by an edited copy (upstream's
	 * SE_replaceInstrumentAction): the component editor's structural edits. */
	std::unique_ptr<UndoCommand> replaceInstrument( std::shared_ptr<H2Core::Instrument> pNew,
													std::shared_ptr<H2Core::Instrument> pOld,
													const char* sName );
	/** Renames component @a nComponent of the selected instrument. */
	std::unique_ptr<UndoCommand> renameComponent( int nComponent, const BString& sOld,
												  const BString& sNew );
	/** What the properties window edits, for songs and patterns alike. */
	struct Properties {
		QString sPath;
		int nVersion = 0;
		QString sName, sAuthor, sNotes;
		H2Core::License license;
		QStringList tags;
	};
	Properties songProperties();
	Properties patternProperties( int nPattern );
	/** Upstream's SE_modifySongPropertiesAction / SE_modifyPatternPropertiesAction. */
	std::unique_ptr<UndoCommand> setSongProperties( const Properties& properties );
	std::unique_ptr<UndoCommand> setPatternProperties( int nPattern, const Properties& properties );
	/** Appends the pattern file at @a sPath to the song (SE_insertPatternAction). */
	std::unique_ptr<UndoCommand> appendPattern( const BString& sPath );
};

#endif
