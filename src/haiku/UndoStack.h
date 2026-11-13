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

#ifndef HAIKU_UNDO_STACK_H
#define HAIKU_UNDO_STACK_H

#include <memory>
#include <vector>

#include <String.h>

/** One undoable edit. redo() is also what performs it the first time. */
class UndoCommand
{
public:
	explicit UndoCommand( const char* sName ) : m_sName( sName ) {}
	virtual ~UndoCommand() = default;

	virtual void redo() = 0;
	virtual void undo() = 0;

	/** Shown in the Edit menu, e.g. "Undo add note". */
	const BString& name() const { return m_sName; }

private:
	BString m_sName;
};

/** Several commands undone and redone as one, e.g. one velocity drag. */
class MacroCommand : public UndoCommand
{
public:
	explicit MacroCommand( const char* sName ) : UndoCommand( sName ) {}

	void add( std::unique_ptr<UndoCommand> pCommand ) { m_commands.push_back( std::move( pCommand ) ); }
	bool isEmpty() const { return m_commands.empty(); }

	void redo() override
	{
		for ( auto& pCommand : m_commands ) {
			pCommand->redo();
		}
	}
	void undo() override
	{
		for ( auto it = m_commands.rbegin(); it != m_commands.rend(); ++it ) {
			( *it )->undo();
		}
	}

private:
	std::vector<std::unique_ptr<UndoCommand>> m_commands;
};

/** The GUI's undo history. Upstream keeps it in the Qt GUI (a
 * QUndoStack), not in the core, so the native GUI needs its own. It is
 * cleared whenever a different song is set. */
class UndoStack
{
public:
	static UndoStack& get();

	/** Performs @a pCommand and records it, dropping anything that was
	 * undone before. */
	void push( std::unique_ptr<UndoCommand> pCommand );
	/** Records a command whose effect has already been applied (e.g. a
	 * stroke drawn live), without running it again. */
	void record( std::unique_ptr<UndoCommand> pCommand );
	void undo();
	void redo();
	void clear();

	bool canUndo() const { return m_nIndex > 0; }
	bool canRedo() const { return m_nIndex < m_commands.size(); }
	BString undoName() const;
	BString redoName() const;

private:
	UndoStack() = default;

	static constexpr size_t kLimit = 200;
	std::vector<std::unique_ptr<UndoCommand>> m_commands;
	/** Number of commands currently applied. */
	size_t m_nIndex = 0;
};

#endif
