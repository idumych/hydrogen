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

#include <core/IO/MediaKitDriver.h>
#if defined(H2CORE_HAVE_MEDIAKIT) || _DOXYGEN_

#include <algorithm>
#include <cstring>

#include <OS.h>
#include <MediaRoster.h>
#include <TimeSource.h>

#include <core/Preferences/Preferences.h>

namespace H2Core
{

MediaKitDriver::MediaKitDriver( audioProcessCallback processCallback )
		: AudioDriver()
		, m_processCallback( processCallback )
		, m_pNode( nullptr )
		, m_pOut_L( nullptr )
		, m_pOut_R( nullptr )
		, m_nBufferSize( MAX_BUFFER_SIZE )
		, m_nXRuns( 0 )
{
	m_nSampleRate = Preferences::get_instance()->m_nSampleRate;
}

MediaKitDriver::~MediaKitDriver()
{
	disconnect();
}

int MediaKitDriver::init( unsigned nBufferSize )
{
	m_nBufferSize = std::clamp( nBufferSize, 64u, (unsigned) MAX_BUFFER_SIZE );
	return 0;
}

//
// Connect
// return 0: Ok
// return 1: Generic error
//
int MediaKitDriver::connect()
{
	INFOLOG( "[connect]" );

	status_t err = B_OK;
	BMediaRoster* pRoster = BMediaRoster::Roster( &err );
	if ( pRoster == nullptr || err != B_OK ) {
		ERRORLOG( QString( "No media roster: %1" ).arg( strerror( err ) ) );
		return 1;
	}
	media_node mixer;
	err = pRoster->GetAudioMixer( &mixer );
	if ( err != B_OK ) {
		ERRORLOG( QString( "No system mixer: %1" ).arg( strerror( err ) ) );
		return 1;
	}
	media_input mixerInput;
	int32 nInputs = 0;
	err = pRoster->GetFreeInputsFor( mixer, &mixerInput, 1, &nInputs, B_MEDIA_RAW_AUDIO );
	if ( err != B_OK || nInputs < 1 ) {
		ERRORLOG( "The system mixer has no free input" );
		pRoster->ReleaseNode( mixer );
		return 1;
	}
	// The mixer's rate, so it doesn't resample (as BSoundPlayer did).
	const float fMixerRate = mixerInput.format.u.raw_audio.frame_rate;
	if ( fMixerRate > 0 && (unsigned) fMixerRate != m_nSampleRate ) {
		WARNINGLOG( QString( "Using the system mixer's sample rate %1 instead of %2" )
					.arg( fMixerRate ).arg( m_nSampleRate ) );
		m_nSampleRate = (unsigned) fMixerRate;
	}

	m_pOut_L = new float[ MAX_BUFFER_SIZE ];
	m_pOut_R = new float[ MAX_BUFFER_SIZE ];
	std::fill_n( m_pOut_L, MAX_BUFFER_SIZE, 0.0f );
	std::fill_n( m_pOut_R, MAX_BUFFER_SIZE, 0.0f );

	m_pNode = new MediaKitNode( "Hydrogen", (float) m_nSampleRate, (int32) m_nBufferSize,
								[this]( int32 nFrames, const float** ppLeft, const float** ppRight ) {
									render( nFrames, ppLeft, ppRight );
								} );
	err = pRoster->RegisterNode( m_pNode );
	if ( err != B_OK ) {
		ERRORLOG( QString( "Unable to register the node: %1" ).arg( strerror( err ) ) );
		m_pNode->Release();
		m_pNode = nullptr;
		pRoster->ReleaseNode( mixer );
		disconnect();
		return 1;
	}

	// The audio output's clock, which the mixer also follows.
	media_node timeSource;
	if ( pRoster->GetTimeSource( &timeSource ) == B_OK ) {
		pRoster->SetTimeSourceFor( m_pNode->Node().node, timeSource.node );
		pRoster->ReleaseNode( timeSource );
	}

	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	format.u.raw_audio.frame_rate = (float) m_nSampleRate;
	format.u.raw_audio.channel_count = 2;
	format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
	format.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
	format.u.raw_audio.buffer_size = m_nBufferSize * 2 * sizeof( float );
	media_output connectedOutput;
	media_input connectedInput;
	err = pRoster->Connect( m_pNode->output( MediaKitNode::Main ).source, mixerInput.destination, &format,
							&connectedOutput, &connectedInput );
	pRoster->ReleaseNode( mixer );
	if ( err != B_OK ) {
		ERRORLOG( QString( "Unable to connect to the system mixer: %1" ).arg( strerror( err ) ) );
		disconnect();
		return 1;
	}
	INFOLOG( QString( "Media Kit node: %1 Hz, %2 frames per buffer, Main connected to the system mixer" )
			 .arg( m_nSampleRate ).arg( m_nBufferSize ) );

	bigtime_t latency = 0;
	pRoster->GetLatencyFor( m_pNode->Node(), &latency );
	BTimeSource* pTimeSource = pRoster->MakeTimeSourceFor( m_pNode->Node() );
	const bigtime_t start = pTimeSource != nullptr ? pTimeSource->Now() + latency + 5000 : 0;
	if ( pTimeSource != nullptr ) {
		pTimeSource->Release();
	}
	err = pRoster->StartNode( m_pNode->Node(), start );
	if ( err != B_OK ) {
		ERRORLOG( QString( "Unable to start the node: %1" ).arg( strerror( err ) ) );
		disconnect();
		return 1;
	}
	return 0;
}

void MediaKitDriver::disconnect()
{
	if ( m_pNode != nullptr ) {
		BMediaRoster* pRoster = BMediaRoster::CurrentRoster();
		if ( pRoster != nullptr ) {
			pRoster->StopNode( m_pNode->Node(), 0, true );
			// Every connection, also those other programs made to Send.
			for ( int32 i = 0; i < MediaKitNode::OutputCount; i++ ) {
				const media_output& output = m_pNode->output( static_cast<MediaKitNode::Output>( i ) );
				if ( output.destination == media_destination::null ) {
					continue;
				}
				// The consumer's node owns the destination port.
				media_node consumer;
				if ( pRoster->GetNodeFor( output.destination.port, &consumer ) == B_OK ) {
					pRoster->Disconnect( m_pNode->Node().node, output.source, consumer.node,
										 output.destination );
					pRoster->ReleaseNode( consumer );
				}
			}
			pRoster->UnregisterNode( m_pNode );
		}
		m_pNode->Release();
		m_pNode = nullptr;
	}

	delete[] m_pOut_L;
	m_pOut_L = nullptr;

	delete[] m_pOut_R;
	m_pOut_R = nullptr;
}

void MediaKitDriver::render( int32 nFrames, const float** ppLeft, const float** ppRight )
{
	const bigtime_t start = system_time();
	const uint32_t nCount = std::min( (uint32_t) nFrames, (uint32_t) MAX_BUFFER_SIZE );
	m_processCallback( nCount, nullptr );
	*ppLeft = m_pOut_L;
	*ppRight = m_pOut_R;
	// Rendering for longer than the audio lasts misses the deadline: the
	// mixer will have played silence.
	if ( system_time() - start > (bigtime_t) ( nCount * 1000000.0 / m_nSampleRate ) ) {
		m_nXRuns++;
	}
}

unsigned MediaKitDriver::getBufferSize()
{
	return m_nBufferSize;
}

unsigned MediaKitDriver::getSampleRate()
{
	return m_nSampleRate;
}

int MediaKitDriver::getLatency()
{
	if ( m_pNode == nullptr ) {
		return 0;
	}
	return std::max( (int) ( m_pNode->latency() * (double) m_nSampleRate / 1000000.0 ), 0 );
}

int MediaKitDriver::getXRuns() const
{
	// Late notices and missing buffers count too.
	return m_nXRuns + ( m_pNode != nullptr ? m_pNode->xruns() : 0 );
}

float* MediaKitDriver::getOut_L()
{
	return m_pOut_L;
}

float* MediaKitDriver::getOut_R()
{
	return m_pOut_R;
}

QString MediaKitDriver::toQString( const QString& sPrefix, bool bShort ) const {
	QString s = Base::sPrintIndention;
	QString sOutput;
	if ( ! bShort ) {
		sOutput = QString( "%1[MediaKitDriver]\n" ).arg( sPrefix )
			.append( QString( "%1%2m_nSampleRate: %3\n" ).arg( sPrefix ).arg( s )
					 .arg( m_nSampleRate ) )
			.append( QString( "%1%2m_nBufferSize: %3\n" ).arg( sPrefix ).arg( s )
					 .arg( m_nBufferSize ) )
			.append( QString( "%1%2m_nXRuns: %3\n" ).arg( sPrefix ).arg( s )
					 .arg( m_nXRuns.load() ) );
	} else {
		sOutput = QString( "[MediaKitDriver]" )
			.append( QString( " m_nSampleRate: %1" ).arg( m_nSampleRate ) )
			.append( QString( ", m_nBufferSize: %1" ).arg( m_nBufferSize ) )
			.append( QString( ", m_nXRuns: %1" ).arg( m_nXRuns.load() ) );
	}

	return sOutput;
}

};

#endif
