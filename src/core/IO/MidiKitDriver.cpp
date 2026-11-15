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

#include <core/IO/MidiKitDriver.h>
#if defined(H2CORE_HAVE_MIDIKIT) || _DOXYGEN_

#include <algorithm>
#include <chrono>

#include <Looper.h>
#include <Messenger.h>
#include <MidiConsumer.h>
#include <MidiProducer.h>
#include <MidiRoster.h>

#include <core/Helpers/Time.h>
#include <core/Midi/MidiMessage.h>
#include <core/Preferences/Preferences.h>

namespace H2Core
{

/** Receives raw MIDI bytes from the roster and hands complete messages
 * to the driver's input queue. Runs in the consumer's own thread. */
class MidiKitConsumer : public BMidiLocalConsumer
{
public:
	MidiKitConsumer( MidiKitDriver* pDriver )
		: BMidiLocalConsumer( "Hydrogen input" )
		, m_pDriver( pDriver ) {}

	void Data( uchar* pData, size_t nLength, bool bAtomic, bigtime_t time ) override
	{
		// Non-atomic data is a partial stream; the roster delivers every
		// complete message (SysEx included) as one atomic block.
		if ( ! bAtomic || nLength == 0 || pData[ 0 ] < 0x80 ) {
			return;
		}

		MidiMessage msg;
		if ( pData[ 0 ] == 0xF0 ) {
			msg.setType( MidiMessage::Type::Sysex );
			msg.setSysexData( std::vector<unsigned char>( pData, pData + nLength ) );
		}
		else {
			msg.setChannel( MidiMessage::deriveChannel( pData[ 0 ] ) );
			msg.setType( MidiMessage::deriveType( pData[ 0 ] ) );
			if ( nLength > 1 ) {
				msg.setData1( Midi::parameterFromIntClamp( pData[ 1 ] ) );
			}
			if ( nLength > 2 ) {
				msg.setData2( Midi::parameterFromIntClamp( pData[ 2 ] ) );
			}
		}
		// Senders may schedule ahead (PowerMix sends its clock 20 ms early)
		// and the roster hands events over at once: wait for the event's
		// time, so it acts when it was meant to, and stamp it with that
		// time rather than when this thread got to it — MIDI clock tempo is
		// measured between ticks only ~20 ms apart.
		if ( time > 0 ) {
			if ( time > system_time() ) {
				snooze_until( time, B_SYSTEM_TIMEBASE );
			}
			const bigtime_t nAge = system_time() - time;
			msg.setTimePoint( Clock::now() - std::chrono::microseconds( std::max<bigtime_t>( 0, nAge ) ) );
		}
		m_pDriver->enqueueInputMessage( msg );
	}

private:
	MidiKitDriver* m_pDriver;
};

// BMidiRoster::NextEndpoint() only lists endpoints of other applications,
// so Hydrogen can't find (and loop back into) its own.

/** Finds another application's endpoint by name. The caller owns the
 * returned reference and must Release() it. */
static BMidiEndpoint* findEndpoint( const QString& sName, bool bProducer )
{
	int32 nId = 0;
	while ( BMidiEndpoint* pEndpoint = BMidiRoster::NextEndpoint( &nId ) ) {
		if ( pEndpoint->IsProducer() == bProducer &&
			 sName == QString::fromUtf8( pEndpoint->Name() ) ) {
			return pEndpoint;
		}
		pEndpoint->Release();
	}
	return nullptr;
}

/** Hears about Midi Kit endpoints coming and going, so the driver can
 * follow a port whose program restarts (upstream's drivers connect
 * once, at open()). */
class MidiKitWatcher : public BLooper
{
public:
	MidiKitWatcher( MidiKitDriver* pDriver ) : BLooper( "Midi Kit watcher" ), m_pDriver( pDriver ) {}

	void MessageReceived( BMessage* pMessage ) override
	{
		if ( pMessage->what != B_MIDI_EVENT ) {
			BLooper::MessageReceived( pMessage );
			return;
		}
		int32 nOp = B_MIDI_NO_OP;
		int32 nId = -1;
		pMessage->FindInt32( "be:op", &nOp );
		pMessage->FindInt32( "be:id", &nId );
		if ( nOp == B_MIDI_REGISTERED ) {
			m_pDriver->endpointsChanged( -1 );
		}
		else if ( nOp == B_MIDI_UNREGISTERED ) {
			m_pDriver->endpointsChanged( nId );
		}
	}

private:
	MidiKitDriver* m_pDriver;
};

MidiKitDriver::MidiKitDriver()
	: MidiBaseDriver()
	, m_pConsumer( nullptr )
	, m_pProducer( nullptr )
	, m_nExternalProducerId( -1 )
	, m_nExternalConsumerId( -1 )
	, m_pWatcher( nullptr )
{
}

MidiKitDriver::~MidiKitDriver()
{
	close();
}

void MidiKitDriver::open()
{
	INFOLOG( "[open]" );

	m_pConsumer = new MidiKitConsumer( this );
	if ( m_pConsumer->Register() != B_OK ) {
		ERRORLOG( "Unable to register Midi Kit consumer" );
	}
	m_pProducer = new BMidiLocalProducer( "Hydrogen output" );
	if ( m_pProducer->Register() != B_OK ) {
		ERRORLOG( "Unable to register Midi Kit producer" );
	}

	{
		std::lock_guard<std::mutex> lock( m_mutex );
		connectInput();
		connectOutput();
	}

	m_pWatcher = new MidiKitWatcher( this );
	m_pWatcher->Run();
	BMessenger watcher( m_pWatcher );
	BMidiRoster::StartWatching( &watcher );
}

void MidiKitDriver::connectInput()
{
	const QString sInputPort = Preferences::get_instance()->m_sMidiPortName;
	if ( m_pConsumer == nullptr || m_nExternalProducerId >= 0 || sInputPort.isEmpty() ||
		 sInputPort == Preferences::getNullMidiPort() ) {
		return;
	}
	BMidiEndpoint* pEndpoint = findEndpoint( sInputPort, true );
	if ( pEndpoint == nullptr ) {
		WARNINGLOG( QString( "MIDI input port [%1] not found (yet)" ).arg( sInputPort ) );
		return;
	}
	auto pProducer = static_cast<BMidiProducer*>( pEndpoint );
	if ( pProducer->Connect( m_pConsumer ) == B_OK ) {
		m_nExternalProducerId = pProducer->ID();
		INFOLOG( QString( "Connected MIDI input [%1]" ).arg( sInputPort ) );
	}
	else {
		ERRORLOG( QString( "Unable to connect MIDI input [%1]" ).arg( sInputPort ) );
	}
	pEndpoint->Release();
}

void MidiKitDriver::connectOutput()
{
	const QString sOutputPort = Preferences::get_instance()->m_sMidiOutputPortName;
	if ( m_pProducer == nullptr || m_nExternalConsumerId >= 0 || sOutputPort.isEmpty() ||
		 sOutputPort == Preferences::getNullMidiPort() ) {
		return;
	}
	BMidiEndpoint* pEndpoint = findEndpoint( sOutputPort, false );
	if ( pEndpoint == nullptr ) {
		WARNINGLOG( QString( "MIDI output port [%1] not found (yet)" ).arg( sOutputPort ) );
		return;
	}
	auto pConsumer = static_cast<BMidiConsumer*>( pEndpoint );
	if ( m_pProducer->Connect( pConsumer ) == B_OK ) {
		m_nExternalConsumerId = pConsumer->ID();
		INFOLOG( QString( "Connected MIDI output [%1]" ).arg( sOutputPort ) );
	}
	else {
		ERRORLOG( QString( "Unable to connect MIDI output [%1]" ).arg( sOutputPort ) );
	}
	pEndpoint->Release();
}

void MidiKitDriver::endpointsChanged( int32_t nGoneId )
{
	std::lock_guard<std::mutex> lock( m_mutex );
	if ( nGoneId >= 0 && nGoneId == m_nExternalProducerId ) {
		INFOLOG( "MIDI input port went away" );
		m_nExternalProducerId = -1;
	}
	if ( nGoneId >= 0 && nGoneId == m_nExternalConsumerId ) {
		INFOLOG( "MIDI output port went away" );
		m_nExternalConsumerId = -1;
	}
	connectInput();
	connectOutput();
}

void MidiKitDriver::close()
{
	// First the watcher, so it can't reconnect what's being closed.
	if ( m_pWatcher != nullptr ) {
		BMidiRoster::StopWatching();
		m_pWatcher->Lock();
		m_pWatcher->Quit();
		m_pWatcher = nullptr;
	}
	std::lock_guard<std::mutex> lock( m_mutex );
	// Unregistering disconnects everything attached to our endpoints,
	// and the roster holds the last reference until Release().
	if ( m_pConsumer != nullptr ) {
		m_pConsumer->Unregister();
		m_pConsumer->Release();
		m_pConsumer = nullptr;
	}
	if ( m_pProducer != nullptr ) {
		m_pProducer->Unregister();
		m_pProducer->Release();
		m_pProducer = nullptr;
	}
	m_nExternalProducerId = -1;
	m_nExternalConsumerId = -1;
}

std::vector<QString> MidiKitDriver::getExternalPortList( const PortType& portType )
{
	// An external *output* port is a producer that can feed our input
	// and vice versa (see PreferencesDialog).
	const bool bProducers = portType == PortType::Output;

	std::vector<QString> portList;
	int32 nId = 0;
	while ( BMidiEndpoint* pEndpoint = BMidiRoster::NextEndpoint( &nId ) ) {
		if ( pEndpoint->IsProducer() == bProducers ) {
			portList.push_back( QString::fromUtf8( pEndpoint->Name() ) );
		}
		pEndpoint->Release();
	}
	return portList;
}

bool MidiKitDriver::isInputActive() const
{
	return m_pConsumer != nullptr && m_pConsumer->IsValid();
}

bool MidiKitDriver::isOutputActive() const
{
	return m_pProducer != nullptr && m_pProducer->IsValid();
}

// Midi::Channel within Hydrogen represent user-facing values [1,16]
// while the Midi Kit, like the MIDI standard, counts from 0.
void MidiKitDriver::sendControlChangeMessage( const MidiMessage& msg )
{
	if ( m_pProducer == nullptr ) {
		return;
	}
	m_pProducer->SprayControlChange(
		static_cast<int>( msg.getChannel() ) - 1,
		static_cast<int>( msg.getData1() ),
		static_cast<int>( msg.getData2() ) );
}

void MidiKitDriver::sendNoteOnMessage( const MidiMessage& msg )
{
	if ( m_pProducer == nullptr ) {
		return;
	}
	m_pProducer->SprayNoteOn(
		static_cast<int>( msg.getChannel() ) - 1,
		static_cast<int>( msg.getData1() ),
		static_cast<int>( msg.getData2() ) );
}

void MidiKitDriver::sendNoteOffMessage( const MidiMessage& msg )
{
	if ( m_pProducer == nullptr ) {
		return;
	}
	m_pProducer->SprayNoteOff(
		static_cast<int>( msg.getChannel() ) - 1,
		static_cast<int>( msg.getData1() ),
		static_cast<int>( msg.getData2() ) );
}

void MidiKitDriver::sendSystemRealTimeMessage( const MidiMessage& msg )
{
	if ( m_pProducer == nullptr ) {
		return;
	}

	uchar nStatus;
	switch ( msg.getType() ) {
	case MidiMessage::Type::Start:
		nStatus = B_START;
		break;
	case MidiMessage::Type::Continue:
		nStatus = B_CONTINUE;
		break;
	case MidiMessage::Type::Stop:
		nStatus = B_STOP;
		break;
	case MidiMessage::Type::TimingClock:
		nStatus = B_TIMING_CLOCK;
		break;
	default:
		ERRORLOG( QString( "Unsupported event [%1]" )
				  .arg( MidiMessage::TypeToQString( msg.getType() ) ) );
		return;
	}
	m_pProducer->SpraySystemRealTime( nStatus );
}

QString MidiKitDriver::toQString( const QString& sPrefix, bool bShort ) const
{
	QString s = Base::sPrintIndention;
	QString sOutput;
	if ( ! bShort ) {
		sOutput = QString( "%1[MidiKitDriver]\n" ).arg( sPrefix )
			.append( QString( "%1%2m_nExternalProducerId: %3\n" ).arg( sPrefix ).arg( s )
					 .arg( m_nExternalProducerId ) )
			.append( QString( "%1%2m_nExternalConsumerId: %3\n" ).arg( sPrefix ).arg( s )
					 .arg( m_nExternalConsumerId ) );
	} else {
		sOutput = QString( "[MidiKitDriver]" )
			.append( QString( " m_nExternalProducerId: %1" ).arg( m_nExternalProducerId ) )
			.append( QString( ", m_nExternalConsumerId: %1" ).arg( m_nExternalConsumerId ) );
	}

	return sOutput;
}

};

#endif
