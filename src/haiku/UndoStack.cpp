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

#include "UndoStack.h"

UndoStack& UndoStack::get()
{
	static UndoStack sStack;
	return sStack;
}

void UndoStack::push( std::unique_ptr<UndoCommand> pCommand )
{
	pCommand->redo();
	record( std::move( pCommand ) );
}

void UndoStack::record( std::unique_ptr<UndoCommand> pCommand )
{
	m_commands.resize( m_nIndex );
	m_commands.push_back( std::move( pCommand ) );
	if ( m_commands.size() > kLimit ) {
		m_commands.erase( m_commands.begin() );
	}
	m_nIndex = m_commands.size();
}

void UndoStack::undo()
{
	if ( canUndo() ) {
		m_commands[ --m_nIndex ]->undo();
	}
}

void UndoStack::redo()
{
	if ( canRedo() ) {
		m_commands[ m_nIndex++ ]->redo();
	}
}

void UndoStack::clear()
{
	m_commands.clear();
	m_nIndex = 0;
}

BString UndoStack::undoName() const
{
	return canUndo() ? m_commands[ m_nIndex - 1 ]->name() : BString();
}

BString UndoStack::redoName() const
{
	return canRedo() ? m_commands[ m_nIndex ]->name() : BString();
}
