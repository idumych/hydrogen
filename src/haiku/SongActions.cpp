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

#include "SongActions.h"

#include <Catalog.h>

#include <core/Basics/Drumkit.h>
#include <core/Basics/GridPoint.h>
#include <core/Basics/Instrument.h>
#include <core/Basics/InstrumentList.h>
#include <core/Basics/Pattern.h>
#include <core/Basics/PatternList.h>
#include <core/Basics/Song.h>
#include <core/CoreActionController.h>
#include <core/Helpers/Filesystem.h>
#include <core/Hydrogen.h>
#include <core/SoundLibrary/SoundLibraryDatabase.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SongActions"

using namespace H2Core;

namespace SongActions
{

static std::shared_ptr<Song> song()
{
	return Hydrogen::get_instance()->getSong();
}

static std::shared_ptr<Pattern> patternAt( int nRow )
{
	auto pSong = song();
	if ( pSong == nullptr || nRow < 0 || nRow >= pSong->getPatternList()->size() ) {
		return nullptr;
	}
	return pSong->getPatternList()->get( nRow );
}

/** Bars that pattern @a nRow plays in. */
static std::vector<int> columnsOf( int nRow )
{
	std::vector<int> columns;
	auto pSong = song();
	auto pPattern = patternAt( nRow );
	if ( pPattern == nullptr ) {
		return columns;
	}
	auto pColumns = pSong->getPatternGroupVector();
	for ( int i = 0; i < (int) pColumns->size(); i++ ) {
		if ( ( *pColumns )[ i ]->index( pPattern ) != -1 ) {
			columns.push_back( i );
		}
	}
	return columns;
}

// Toggling is its own inverse, like upstream's Toggle action.
class CellCommand : public UndoCommand
{
public:
	CellCommand( int nColumn, int nRow )
		: UndoCommand( B_TRANSLATE( "toggle cell" ) ), m_nColumn( nColumn ), m_nRow( nRow ) {}

	void redo() override { CoreActionController::toggleGridCell( GridPoint( m_nColumn, m_nRow ) ); }
	void undo() override { redo(); }

private:
	int m_nColumn, m_nRow;
};

std::unique_ptr<UndoCommand> toggleCell( int nColumn, int nRow )
{
	return std::make_unique<CellCommand>( nColumn, nRow );
}

/** Inserts a copy of a pattern at a position; undo removes it. */
class InsertCommand : public UndoCommand
{
public:
	InsertCommand( const char* sName, int nPosition, std::shared_ptr<Pattern> pPattern )
		: UndoCommand( sName ), m_nPosition( nPosition ), m_pPattern( pPattern ) {}

	void redo() override
	{
		CoreActionController::setPattern( std::make_shared<Pattern>( m_pPattern ), m_nPosition, false );
	}
	void undo() override { CoreActionController::removePattern( m_nPosition ); }

private:
	int m_nPosition;
	std::shared_ptr<Pattern> m_pPattern;
};

/** Removes a pattern; undo puts a copy back at its position. */
class RemoveCommand : public UndoCommand
{
public:
	RemoveCommand( int nPosition, std::shared_ptr<Pattern> pPattern )
		: UndoCommand( B_TRANSLATE( "delete pattern" ) )
		, m_nPosition( nPosition ), m_pPattern( std::make_shared<Pattern>( pPattern ) ) {}

	void redo() override { CoreActionController::removePattern( m_nPosition ); }
	void undo() override
	{
		CoreActionController::setPattern( std::make_shared<Pattern>( m_pPattern ), m_nPosition, false );
	}

private:
	int m_nPosition;
	std::shared_ptr<Pattern> m_pPattern;
};

std::unique_ptr<UndoCommand> newPattern()
{
	auto pSong = song();
	if ( pSong == nullptr ) {
		return nullptr;
	}
	auto pPatterns = pSong->getPatternList();
	// Upstream names new patterns "Pattern N" and takes author and licence
	// from the song (its properties dialog comes later in the port).
	auto pPattern = std::make_shared<Pattern>();
	pPattern->setName( pPatterns->findUnusedPatternName(
		QString( "Pattern %1" ).arg( pPatterns->size() + 1 ) ) );
	pPattern->setAuthor( pSong->getAuthor() );
	pPattern->setLicense( pSong->getLicense() );
	const int nSelected = Hydrogen::get_instance()->getSelectedPatternNumber();
	const int nPosition = nSelected >= 0 ? nSelected + 1 : pPatterns->size();
	return std::make_unique<InsertCommand>( B_TRANSLATE( "new pattern" ), nPosition, pPattern );
}

std::unique_ptr<UndoCommand> duplicatePattern( int nRow )
{
	auto pOriginal = patternAt( nRow );
	if ( pOriginal == nullptr ) {
		return nullptr;
	}
	auto pMacro = std::make_unique<MacroCommand>( B_TRANSLATE( "duplicate pattern" ) );
	auto pCopy = std::make_shared<Pattern>( pOriginal );
	pMacro->add( std::make_unique<InsertCommand>( "", nRow + 1, pCopy ) );
	for ( int nColumn : columnsOf( nRow ) ) {
		pMacro->add( toggleCell( nColumn, nRow + 1 ) );
	}
	return pMacro;
}

std::unique_ptr<UndoCommand> deletePattern( int nRow )
{
	auto pPattern = patternAt( nRow );
	if ( pPattern == nullptr ) {
		return nullptr;
	}
	auto pMacro = std::make_unique<MacroCommand>( B_TRANSLATE( "delete pattern" ) );
	for ( int nColumn : columnsOf( nRow ) ) {
		pMacro->add( toggleCell( nColumn, nRow ) );
	}
	pMacro->add( std::make_unique<RemoveCommand>( nRow, pPattern ) );
	return pMacro;
}

class RenameCommand : public UndoCommand
{
public:
	RenameCommand( int nRow, const QString& sOld, const QString& sNew )
		: UndoCommand( B_TRANSLATE( "rename pattern" ) ), m_nRow( nRow ), m_sOld( sOld ), m_sNew( sNew ) {}

	void redo() override { apply( m_sNew ); }
	void undo() override { apply( m_sOld ); }

private:
	void apply( const QString& sName )
	{
		auto pPattern = patternAt( m_nRow );
		if ( pPattern == nullptr ) {
			return;
		}
		CoreActionController::setPatternProperties(
			pPattern->getPath(), pPattern->getVersion(), sName, pPattern->getAuthor(),
			pPattern->getInfo(), pPattern->getLicense(), pPattern->getTags(), m_nRow );
	}

	int m_nRow;
	QString m_sOld, m_sNew;
};

std::unique_ptr<UndoCommand> renamePattern( int nRow, const BString& sName )
{
	auto pPattern = patternAt( nRow );
	if ( pPattern == nullptr ) {
		return nullptr;
	}
	const QString sNew = song()->getPatternList()->findUnusedPatternName(
		QString::fromUtf8( sName.String() ), nRow );
	if ( sNew == pPattern->getName() ) {
		return nullptr;
	}
	return std::make_unique<RenameCommand>( nRow, pPattern->getName(), sNew );
}

std::unique_ptr<UndoCommand> clearSequence()
{
	auto pSong = song();
	if ( pSong == nullptr ) {
		return nullptr;
	}
	auto pMacro = std::make_unique<MacroCommand>( B_TRANSLATE( "clear pattern sequence" ) );
	auto pColumns = pSong->getPatternGroupVector();
	auto pPatterns = pSong->getPatternList();
	// Last bar first, so removing cells never shifts a bar still to come.
	for ( int nColumn = (int) pColumns->size() - 1; nColumn >= 0; nColumn-- ) {
		for ( int nRow = 0; nRow < pPatterns->size(); nRow++ ) {
			if ( ( *pColumns )[ nColumn ]->index( pPatterns->get( nRow ) ) != -1 ) {
				pMacro->add( toggleCell( nColumn, nRow ) );
			}
		}
	}
	return pMacro->isEmpty() ? nullptr : std::move( pMacro );
}

class DrumkitCommand : public UndoCommand
{
public:
	DrumkitCommand( std::shared_ptr<Drumkit> pNew, std::shared_ptr<Drumkit> pOld )
		: UndoCommand( B_TRANSLATE( "load drumkit" ) ), m_pNew( pNew ), m_pOld( pOld ) {}

	void redo() override { CoreActionController::setDrumkit( m_pNew ); }
	void undo() override { CoreActionController::setDrumkit( m_pOld ); }

private:
	std::shared_ptr<Drumkit> m_pNew, m_pOld;
};

std::unique_ptr<UndoCommand> switchDrumkit( const BString& sPath )
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pSong = song();
	if ( pSong == nullptr ) {
		return nullptr;
	}
	auto pKit = pHydrogen->getSoundLibraryDatabase()->getDrumkit( QString::fromUtf8( sPath.String() ) );
	if ( pKit == nullptr ) {
		return nullptr;
	}
	// Like upstream: the song gets its own copy, in the song's context.
	auto pCopy = std::make_shared<Drumkit>( pKit );
	pCopy->setContext( Filesystem::Context::Song );
	return std::make_unique<DrumkitCommand>( pCopy, pSong->getDrumkit() );
}

class InstrumentCommand : public UndoCommand
{
public:
	InstrumentCommand( const char* sName, std::shared_ptr<Instrument> pNew,
					   std::shared_ptr<Instrument> pOld )
		: UndoCommand( sName ), m_pNew( pNew ), m_pOld( pOld ) {}

	void redo() override { CoreActionController::replaceInstrument( m_pNew, m_pOld ); }
	void undo() override { CoreActionController::replaceInstrument( m_pOld, m_pNew ); }

private:
	std::shared_ptr<Instrument> m_pNew, m_pOld;
};

std::unique_ptr<UndoCommand> renameInstrument( int nInstrument, const BString& sName )
{
	auto pSong = song();
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		return nullptr;
	}
	auto pOld = pSong->getDrumkit()->getInstruments()->get( nInstrument );
	if ( pOld == nullptr || pOld->getName() == QString::fromUtf8( sName.String() ) ) {
		return nullptr;
	}
	auto pNew = std::make_shared<Instrument>( pOld );
	pNew->setName( QString::fromUtf8( sName.String() ) );
	return std::make_unique<InstrumentCommand>( B_TRANSLATE( "rename instrument" ), pNew, pOld );
}

std::unique_ptr<UndoCommand> replaceInstrument( std::shared_ptr<Instrument> pNew,
												std::shared_ptr<Instrument> pOld, const char* sName )
{
	return std::make_unique<InstrumentCommand>( sName, pNew, pOld );
}

class ComponentNameCommand : public UndoCommand
{
public:
	ComponentNameCommand( int nComponent, const QString& sOld, const QString& sNew )
		: UndoCommand( B_TRANSLATE( "rename component" ) )
		, m_nComponent( nComponent ), m_sOld( sOld ), m_sNew( sNew ) {}

	void redo() override { CoreActionController::renameComponent( m_nComponent, m_sNew ); }
	void undo() override { CoreActionController::renameComponent( m_nComponent, m_sOld ); }

private:
	int m_nComponent;
	QString m_sOld, m_sNew;
};

std::unique_ptr<UndoCommand> renameComponent( int nComponent, const BString& sOld, const BString& sNew )
{
	if ( sOld == sNew ) {
		return nullptr;
	}
	return std::make_unique<ComponentNameCommand>( nComponent, QString::fromUtf8( sOld.String() ),
												   QString::fromUtf8( sNew.String() ) );
}

std::unique_ptr<UndoCommand> appendPattern( const BString& sPath )
{
	auto pSong = song();
	if ( pSong == nullptr ) {
		return nullptr;
	}
	auto pPattern = CoreActionController::loadPattern( QString::fromUtf8( sPath.String() ) );
	if ( pPattern == nullptr ) {
		return nullptr;
	}
	return std::make_unique<InsertCommand>( B_TRANSLATE( "add pattern" ),
											pSong->getPatternList()->size(), pPattern );
}

};
