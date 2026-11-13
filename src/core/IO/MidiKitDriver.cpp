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

#include <MidiConsumer.h>
#include <MidiProducer.h>
#include <MidiRoster.h>

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

	void Data( uchar* pData, size_t nLength, bool bAtomic, bigtime_t ) override
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

MidiKitDriver::MidiKitDriver()
	: MidiBaseDriver()
	, m_pConsumer( nullptr )
	, m_pProducer( nullptr )
	, m_nExternalProducerId( -1 )
	, m_nExternalConsumerId( -1 )
{
}

MidiKitDriver::~MidiKitDriver()
{
	close();
}

void MidiKitDriver::open()
{
	INFOLOG( "[open]" );
	const auto pPref = Preferences::get_instance();

	m_pConsumer = new MidiKitConsumer( this );
	if ( m_pConsumer->Register() != B_OK ) {
		ERRORLOG( "Unable to register Midi Kit consumer" );
	}
	m_pProducer = new BMidiLocalProducer( "Hydrogen output" );
	if ( m_pProducer->Register() != B_OK ) {
		ERRORLOG( "Unable to register Midi Kit producer" );
	}

	const QString sInputPort = pPref->m_sMidiPortName;
	if ( ! sInputPort.isEmpty() && sInputPort != Preferences::getNullMidiPort() ) {
		BMidiEndpoint* pEndpoint = findEndpoint( sInputPort, true );
		if ( pEndpoint == nullptr ) {
			ERRORLOG( QString( "MIDI input port [%1] not found" ).arg( sInputPort ) );
		}
		else {
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
	}

	const QString sOutputPort = pPref->m_sMidiOutputPortName;
	if ( ! sOutputPort.isEmpty() && sOutputPort != Preferences::getNullMidiPort() ) {
		BMidiEndpoint* pEndpoint = findEndpoint( sOutputPort, false );
		if ( pEndpoint == nullptr ) {
			ERRORLOG( QString( "MIDI output port [%1] not found" ).arg( sOutputPort ) );
		}
		else {
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
	}
}

void MidiKitDriver::close()
{
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
