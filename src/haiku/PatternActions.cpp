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

#include "PatternActions.h"

#include <Catalog.h>

#include <core/AudioEngine/AudioEngine.h>
#include <core/Basics/Drumkit.h>
#include <core/Basics/InstrumentList.h>
#include <core/Basics/Pattern.h>
#include <core/Basics/PatternList.h>
#include <core/Basics/Song.h>
#include <core/EventQueue.h>
#include <core/Hydrogen.h>

#include "Messages.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PatternActions"

using namespace H2Core;

namespace PatternActions
{

static BMessenger sObserver;

void setObserver( const BMessenger& observer )
{
	sObserver = observer;
}

static void notify()
{
	if ( sObserver.IsValid() ) {
		sObserver.SendMessage( kMsgPatternEdited );
	}
}

NoteSpec NoteSpec::from( const std::shared_ptr<Note>& pNote )
{
	NoteSpec spec;
	spec.nPosition = pNote->getPosition();
	spec.id = pNote->getInstrumentId();
	spec.sType = pNote->getType();
	spec.nLength = pNote->getLength();
	spec.fVelocity = pNote->getVelocity();
	spec.fPan = pNote->getPan();
	spec.fLeadLag = pNote->getLeadLag();
	spec.fProbability = pNote->getProbability();
	spec.key = pNote->getKey();
	spec.octave = pNote->getOctave();
	spec.bNoteOff = pNote->getNoteOff();
	return spec;
}

static std::shared_ptr<Pattern> pattern( int nPattern )
{
	auto pSong = Hydrogen::get_instance()->getSong();
	if ( pSong == nullptr || pSong->getPatternList() == nullptr ||
		 nPattern < 0 || nPattern >= pSong->getPatternList()->size() ) {
		return nullptr;
	}
	return pSong->getPatternList()->get( nPattern );
}

/** Upstream's PatternEditor::addOrRemoveNoteAction. */
static void addOrRemove( int nPattern, const NoteSpec& spec, bool bAdd )
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pSong = pHydrogen->getSong();
	auto pPattern = pattern( nPattern );
	if ( pPattern == nullptr || pSong->getDrumkit() == nullptr ||
		 ( spec.id == Instrument::EmptyId && spec.sType.isEmpty() ) ) {
		return;
	}

	pHydrogen->getAudioEngine()->lock( RIGHT_HERE );
	if ( bAdd ) {
		float fVelocity = spec.fVelocity;
		float fPan = spec.fPan;
		int nLength = spec.nLength;
		if ( spec.bNoteOff ) {
			fVelocity = VELOCITY_MIN;
			fPan = PAN_DEFAULT;
			nLength = 1;
		}
		std::shared_ptr<Instrument> pInstrument;
		if ( spec.id != Instrument::EmptyId ) {
			pInstrument = pSong->getDrumkit()->getInstruments()->find( spec.id );
		}
		auto pNote = std::make_shared<Note>( pInstrument, spec.nPosition, fVelocity, fPan,
											 nLength );
		pNote->setInstrumentId( spec.id );
		pNote->setType( spec.sType );
		pNote->setNoteOff( spec.bNoteOff );
		pNote->setLeadLag( spec.fLeadLag );
		pNote->setProbability( spec.fProbability );
		pNote->setKey( spec.key );
		pNote->setOctave( spec.octave );
		pPattern->insertNote( pNote );
	}
	else {
		// One candidate: remove it. Several stacked on one cell: remove
		// the one whose properties all match.
		std::vector<std::shared_ptr<Note>> candidates;
		const auto pNotes = pPattern->getNotes();
		for ( auto it = pNotes->lower_bound( spec.nPosition );
			  it != pNotes->end() && it->first <= spec.nPosition; ++it ) {
			const auto& pNote = it->second;
			if ( pNote != nullptr && pNote->getInstrumentId() == spec.id &&
				 pNote->getType() == spec.sType && pNote->getKey() == spec.key &&
				 pNote->getOctave() == spec.octave ) {
				candidates.push_back( pNote );
			}
		}
		std::shared_ptr<Note> pRemove;
		if ( candidates.size() == 1 ) {
			pRemove = candidates[ 0 ];
		}
		else {
			for ( const auto& pNote : candidates ) {
				if ( pNote->getLength() == spec.nLength &&
					 pNote->getVelocity() == spec.fVelocity && pNote->getPan() == spec.fPan &&
					 pNote->getLeadLag() == spec.fLeadLag &&
					 pNote->getProbability() == spec.fProbability &&
					 pNote->getNoteOff() == spec.bNoteOff ) {
					pRemove = pNote;
					break;
				}
			}
		}
		if ( pRemove != nullptr ) {
			pPattern->removeNote( pRemove );
		}
	}
	pHydrogen->getAudioEngine()->unlock();

	pHydrogen->setPatternModified( true, nPattern );
	notify();
}

class NoteCommand : public UndoCommand
{
public:
	NoteCommand( const char* sName, int nPattern, const NoteSpec& spec, bool bAdd )
		: UndoCommand( sName ), m_nPattern( nPattern ), m_spec( spec ), m_bAdd( bAdd ) {}

	void redo() override { addOrRemove( m_nPattern, m_spec, m_bAdd ); }
	void undo() override { addOrRemove( m_nPattern, m_spec, ! m_bAdd ); }

private:
	int m_nPattern;
	NoteSpec m_spec;
	bool m_bAdd;
};

std::unique_ptr<UndoCommand> addNote( int nPattern, const NoteSpec& note )
{
	return std::make_unique<NoteCommand>( B_TRANSLATE( "add note" ), nPattern, note, true );
}

std::unique_ptr<UndoCommand> removeNote( int nPattern, const NoteSpec& note )
{
	return std::make_unique<NoteCommand>( B_TRANSLATE( "delete note" ), nPattern, note, false );
}

/** Upstream's PatternEditor::editNotePropertiesAction, velocity only. */
static void applyVelocity( int nPattern, const NoteSpec& spec, float fVelocity )
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pPattern = pattern( nPattern );
	if ( pPattern == nullptr ) {
		return;
	}
	pHydrogen->getAudioEngine()->lock( RIGHT_HERE );
	auto pNote = pPattern->findNote( spec.nPosition, spec.id, spec.sType, spec.key, spec.octave );
	if ( pNote != nullptr ) {
		pNote->setVelocity( fVelocity );
	}
	pHydrogen->getAudioEngine()->unlock();
	pHydrogen->setPatternModified( true, nPattern );
	notify();
}

class VelocityCommand : public UndoCommand
{
public:
	VelocityCommand( int nPattern, const NoteSpec& spec, float fNew )
		: UndoCommand( B_TRANSLATE( "change velocity" ) )
		, m_nPattern( nPattern ), m_spec( spec ), m_fOld( spec.fVelocity ), m_fNew( fNew ) {}

	void redo() override { applyVelocity( m_nPattern, m_spec, m_fNew ); }
	void undo() override { applyVelocity( m_nPattern, m_spec, m_fOld ); }

private:
	int m_nPattern;
	NoteSpec m_spec;
	float m_fOld;
	float m_fNew;
};

void previewVelocity( int nPattern, const NoteSpec& note, float fVelocity )
{
	applyVelocity( nPattern, note, fVelocity );
}

std::unique_ptr<UndoCommand> setVelocity( int nPattern, const NoteSpec& note, float fNewVelocity )
{
	return std::make_unique<VelocityCommand>( nPattern, note, fNewVelocity );
}

/** Upstream's PatternEditorPanel::patternSizeChangedAction. */
static void applySize( int nPattern, int nLength, int nDenominator )
{
	auto pHydrogen = Hydrogen::get_instance();
	auto pPattern = pattern( nPattern );
	if ( pPattern == nullptr ) {
		return;
	}
	pHydrogen->getAudioEngine()->lock( RIGHT_HERE );
	pPattern->setLength( nLength );
	pPattern->setDenominator( nDenominator );
	pHydrogen->updateSongSize();
	pHydrogen->getAudioEngine()->unlock();
	pHydrogen->setPatternModified( true, nPattern );
	EventQueue::get_instance()->pushEvent( Event::Type::PatternChanged, -1 );
	notify();
}

class SizeCommand : public UndoCommand
{
public:
	SizeCommand( int nPattern, int nNewLength, int nNewDenominator )
		: UndoCommand( B_TRANSLATE( "change pattern size" ) )
		, m_nPattern( nPattern ), m_nNewLength( nNewLength ), m_nNewDenominator( nNewDenominator )
	{
		auto pPattern = pattern( nPattern );
		m_nOldLength = pPattern != nullptr ? pPattern->getLength() : nNewLength;
		m_nOldDenominator = pPattern != nullptr ? pPattern->getDenominator() : nNewDenominator;
	}

	void redo() override { applySize( m_nPattern, m_nNewLength, m_nNewDenominator ); }
	void undo() override { applySize( m_nPattern, m_nOldLength, m_nOldDenominator ); }

private:
	int m_nPattern;
	int m_nNewLength, m_nNewDenominator;
	int m_nOldLength, m_nOldDenominator;
};

std::unique_ptr<UndoCommand> setPatternSize( int nPattern, int nNewLength, int nNewDenominator )
{
	return std::make_unique<SizeCommand>( nPattern, nNewLength, nNewDenominator );
}

};
