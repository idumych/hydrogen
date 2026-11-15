/*
 * Hydrogen
 * Copyright(c) 2002-2008 by Alex >Comix< Cominu [comix@users.sourceforge.net]
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

#ifndef MIDI_KIT_DRIVER_H
#define MIDI_KIT_DRIVER_H

#include <core/IO/MidiBaseDriver.h>

#include <mutex>

#if defined(H2CORE_HAVE_MIDIKIT) || _DOXYGEN_

class BMidiLocalProducer;

namespace H2Core
{

class MidiKitConsumer;
class MidiKitWatcher;

/** MIDI input and output through the Haiku Midi Kit (libmidi2).
 *
 * Hydrogen registers a consumer ("Hydrogen input") and a producer
 * ("Hydrogen output") with the MIDI roster, so other applications
 * (PatchBay, sequencers) can connect to it. If the preferences name an
 * external port, the driver connects to it as well: the input port
 * names a producer, the output port a consumer. */
class MidiKitDriver : public Object<MidiKitDriver>,
					  public virtual MidiBaseDriver
{
	H2_OBJECT(MidiKitDriver)
public:
	MidiKitDriver();
	virtual ~MidiKitDriver();

	void close() override;
	std::vector<QString> getExternalPortList( const PortType& portType ) override;
	bool isInputActive() const override;
	bool isOutputActive() const override;
	void open() override;

	QString toQString( const QString& sPrefix = "", bool bShort = true ) const override;

	/** A Midi Kit endpoint appeared or went away (from the watcher):
	 * connects to the configured ports again, e.g. after the program
	 * providing one restarted. */
	void endpointsChanged( int32_t nGoneId );

private:
	/** Connect to the configured input/output port if it exists and we
	 * aren't connected to it yet. Call with m_mutex held. */
	void connectInput();
	void connectOutput();

	void sendControlChangeMessage( const MidiMessage& msg ) override;
	void sendNoteOnMessage( const MidiMessage& msg ) override;
	void sendNoteOffMessage( const MidiMessage& msg ) override;
	void sendSystemRealTimeMessage( const MidiMessage& msg ) override;

	MidiKitConsumer* m_pConsumer;
	BMidiLocalProducer* m_pProducer;
	/** Midi Kit IDs of the external endpoints we connected to, or -1. */
	int32_t m_nExternalProducerId;
	int32_t m_nExternalConsumerId;
	MidiKitWatcher* m_pWatcher;
	std::mutex m_mutex;
};

};

#endif

#endif
