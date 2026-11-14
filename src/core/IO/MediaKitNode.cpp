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

#include <core/IO/MediaKitNode.h>

#if defined(H2CORE_HAVE_MEDIAKIT) || _DOXYGEN_

#include <algorithm>
#include <cmath>
#include <cstring>

#include <Buffer.h>
#include <MediaDefs.h>
#include <scheduler.h>
#include <BufferGroup.h>
#include <TimeSource.h>

namespace H2Core
{

static size_t sampleSize( uint32 nFormat )
{
	switch ( nFormat ) {
	case media_raw_audio_format::B_AUDIO_FLOAT:
	case media_raw_audio_format::B_AUDIO_INT:
		return 4;
	case media_raw_audio_format::B_AUDIO_SHORT:
		return 2;
	default:
		return 0;
	}
}

MediaKitNode::MediaKitNode( const char* sName, float fFrameRate, int32 nFrames, Render render )
	: BMediaNode( sName )
	, BBufferProducer( B_MEDIA_RAW_AUDIO )
	, BMediaEventLooper()
	, m_fFrameRate( fFrameRate )
	, m_nFrames( nFrames )
	, m_render( std::move( render ) )
{
	const char* names[ OutputCount ] = { "Main", "Send" };
	for ( int32 i = 0; i < OutputCount; i++ ) {
		m_outputs[ i ].destination = media_destination::null;
		m_outputs[ i ].format = preferredFormat();
		strlcpy( m_outputs[ i ].name, names[ i ], B_MEDIA_NAME_LENGTH );
	}
}

MediaKitNode::~MediaKitNode()
{
	BMediaEventLooper::Quit();
	for ( auto& state : m_state ) {
		if ( state.bOwnGroup ) {
			delete state.pGroup;
		}
	}
}

BMediaAddOn* MediaKitNode::AddOn( int32* pInternalId ) const
{
	// Owned by Hydrogen, not by an add-on.
	if ( pInternalId != nullptr ) {
		*pInternalId = 0;
	}
	return nullptr;
}

void MediaKitNode::NodeRegistered()
{
	for ( int32 i = 0; i < OutputCount; i++ ) {
		m_outputs[ i ].node = Node();
		m_outputs[ i ].source.port = ControlPort();
		m_outputs[ i ].source.id = i;
	}
	SetPriority( suggest_thread_priority( B_AUDIO_PLAYBACK ) );
	Run();
}

status_t MediaKitNode::HandleMessage( int32 nCode, const void* pData, size_t nSize )
{
	if ( BBufferProducer::HandleMessage( nCode, pData, nSize ) != B_OK &&
		 BMediaNode::HandleMessage( nCode, pData, nSize ) != B_OK ) {
		BMediaNode::HandleBadMessage( nCode, pData, nSize );
	}
	return B_OK;
}

media_format MediaKitNode::preferredFormat() const
{
	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	format.u.raw_audio.frame_rate = m_fFrameRate;
	format.u.raw_audio.channel_count = 2;
	format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
	format.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
	format.u.raw_audio.buffer_size = m_nFrames * 2 * sizeof( float );
	return format;
}

bool MediaKitNode::acceptFormat( media_format* pFormat ) const
{
	if ( pFormat->type == B_MEDIA_UNKNOWN_TYPE ) {
		pFormat->type = B_MEDIA_RAW_AUDIO;
	}
	if ( pFormat->type != B_MEDIA_RAW_AUDIO ) {
		return false;
	}
	auto& raw = pFormat->u.raw_audio;
	const auto& wild = media_raw_audio_format::wildcard;
	if ( raw.channel_count == wild.channel_count ) {
		raw.channel_count = 2;
	}
	if ( raw.frame_rate == wild.frame_rate ) {
		raw.frame_rate = m_fFrameRate;
	}
	if ( raw.format == wild.format ) {
		raw.format = media_raw_audio_format::B_AUDIO_FLOAT;
	}
	if ( raw.byte_order == wild.byte_order ) {
		raw.byte_order = B_MEDIA_HOST_ENDIAN;
	}
	const size_t nSample = sampleSize( raw.format );
	if ( raw.buffer_size == wild.buffer_size && nSample > 0 ) {
		raw.buffer_size = m_nFrames * 2 * nSample;
	}
	// One mix feeds both outputs: same rate and length of buffer.
	return raw.channel_count == 2 && std::fabs( raw.frame_rate - m_fFrameRate ) < 0.5f && nSample > 0 &&
		   raw.byte_order == B_MEDIA_HOST_ENDIAN && raw.buffer_size == m_nFrames * 2 * nSample;
}

int32 MediaKitNode::outputFor( const media_source& source ) const
{
	if ( source.port != ControlPort() || source.id < 0 || source.id >= OutputCount ) {
		return -1;
	}
	return source.id;
}

status_t MediaKitNode::FormatSuggestionRequested( media_type type, int32, media_format* pFormat )
{
	if ( type != B_MEDIA_RAW_AUDIO && type != B_MEDIA_UNKNOWN_TYPE ) {
		return B_MEDIA_BAD_FORMAT;
	}
	*pFormat = preferredFormat();
	return B_OK;
}

status_t MediaKitNode::FormatProposal( const media_source& output, media_format* pFormat )
{
	if ( outputFor( output ) < 0 ) {
		return B_MEDIA_BAD_SOURCE;
	}
	if ( ! acceptFormat( pFormat ) ) {
		*pFormat = preferredFormat();
		return B_MEDIA_BAD_FORMAT;
	}
	return B_OK;
}

status_t MediaKitNode::FormatChangeRequested( const media_source&, const media_destination&, media_format*, int32* )
{
	return B_ERROR;
}

status_t MediaKitNode::GetNextOutput( int32* pCookie, media_output* pOutput )
{
	if ( *pCookie < 0 || *pCookie >= OutputCount ) {
		return B_BAD_INDEX;
	}
	*pOutput = m_outputs[ *pCookie ];
	( *pCookie )++;
	return B_OK;
}

status_t MediaKitNode::DisposeOutputCookie( int32 )
{
	return B_OK;
}

status_t MediaKitNode::SetBufferGroup( const media_source& source, BBufferGroup* pGroup )
{
	const int32 nOutput = outputFor( source );
	if ( nOutput < 0 ) {
		return B_MEDIA_BAD_SOURCE;
	}
	OutputState& state = m_state[ nOutput ];
	if ( state.bOwnGroup ) {
		delete state.pGroup;
	}
	state.pGroup = pGroup;
	state.bOwnGroup = false;
	if ( pGroup == nullptr && state.bConnected ) {
		state.pGroup = new BBufferGroup( m_outputs[ nOutput ].format.u.raw_audio.buffer_size, 3 );
		state.bOwnGroup = true;
	}
	return B_OK;
}

status_t MediaKitNode::GetLatency( bigtime_t* pLatency )
{
	*pLatency = EventLatency() + SchedulingLatency();
	return B_OK;
}

status_t MediaKitNode::PrepareToConnect( const media_source& source, const media_destination& destination,
										 media_format* pFormat, media_source* pSource, char* sName )
{
	const int32 nOutput = outputFor( source );
	if ( nOutput < 0 ) {
		return B_MEDIA_BAD_SOURCE;
	}
	if ( m_outputs[ nOutput ].destination != media_destination::null ) {
		return B_MEDIA_ALREADY_CONNECTED;
	}
	if ( ! acceptFormat( pFormat ) ) {
		return B_MEDIA_BAD_FORMAT;
	}
	m_outputs[ nOutput ].destination = destination;
	m_outputs[ nOutput ].format = *pFormat;
	*pSource = m_outputs[ nOutput ].source;
	strlcpy( sName, m_outputs[ nOutput ].name, B_MEDIA_NAME_LENGTH );
	return B_OK;
}

void MediaKitNode::Connect( status_t error, const media_source& source, const media_destination& destination,
							const media_format& format, char* sName )
{
	const int32 nOutput = outputFor( source );
	if ( nOutput < 0 ) {
		return;
	}
	if ( error != B_OK ) {
		m_outputs[ nOutput ].destination = media_destination::null;
		m_outputs[ nOutput ].format = preferredFormat();
		return;
	}
	m_outputs[ nOutput ].destination = destination;
	m_outputs[ nOutput ].format = format;
	strlcpy( sName, m_outputs[ nOutput ].name, B_MEDIA_NAME_LENGTH );

	OutputState& state = m_state[ nOutput ];
	state.bConnected = true;
	state.bEnabled = true;
	if ( state.pGroup == nullptr ) {
		state.pGroup = new BBufferGroup( format.u.raw_audio.buffer_size, 3 );
		state.bOwnGroup = true;
	}

	// The worst downstream latency decides when buffers must be made;
	// rendering a buffer is allowed a quarter of its length.
	bigtime_t downstream = 0;
	media_node_id timeSource;
	FindLatencyFor( destination, &downstream, &timeSource );
	m_latency = std::max( m_latency, downstream );
	m_internalLatency = std::max<bigtime_t>( 2000, (bigtime_t) ( m_nFrames * 250000.0 / m_fFrameRate ) );
	SetEventLatency( m_latency + m_internalLatency );
	SetBufferDuration( (bigtime_t) ( m_nFrames * 1000000.0 / m_fFrameRate ) );
}

void MediaKitNode::Disconnect( const media_source& source, const media_destination& destination )
{
	const int32 nOutput = outputFor( source );
	if ( nOutput < 0 || m_outputs[ nOutput ].destination != destination ) {
		return;
	}
	m_outputs[ nOutput ].destination = media_destination::null;
	m_outputs[ nOutput ].format = preferredFormat();
	OutputState& state = m_state[ nOutput ];
	state.bConnected = false;
	if ( state.bOwnGroup ) {
		delete state.pGroup;
	}
	state.pGroup = nullptr;
	state.bOwnGroup = false;
}

void MediaKitNode::LateNoticeReceived( const media_source&, bigtime_t howMuch, bigtime_t )
{
	m_nLate++;
	// Allowed by the run mode: start earlier from now on (capped).
	if ( RunMode() == B_INCREASE_LATENCY ) {
		m_internalLatency = std::min<bigtime_t>( m_internalLatency + howMuch, 200000 );
		SetEventLatency( m_latency + m_internalLatency );
	}
}

void MediaKitNode::EnableOutput( const media_source& source, bool bEnabled, int32* )
{
	const int32 nOutput = outputFor( source );
	if ( nOutput >= 0 ) {
		m_state[ nOutput ].bEnabled = bEnabled;
	}
}

void MediaKitNode::LatencyChanged( const media_source& source, const media_destination&, bigtime_t newLatency, uint32 )
{
	if ( outputFor( source ) >= 0 && newLatency > m_latency ) {
		m_latency = newLatency;
		SetEventLatency( m_latency + m_internalLatency );
	}
}

void MediaKitNode::HandleEvent( const media_timed_event* pEvent, bigtime_t, bool )
{
	switch ( pEvent->type ) {
	case BTimedEventQueue::B_START:
		if ( ! m_bRunning ) {
			m_bRunning = true;
			m_startTime = pEvent->event_time;
			m_nFramesSent = 0;
			media_timed_event first( m_startTime, BTimedEventQueue::B_HANDLE_BUFFER );
			EventQueue()->AddEvent( first );
		}
		break;
	case BTimedEventQueue::B_STOP:
		m_bRunning = false;
		EventQueue()->FlushEvents( 0, BTimedEventQueue::B_ALWAYS, true, BTimedEventQueue::B_HANDLE_BUFFER );
		break;
	case BTimedEventQueue::B_HANDLE_BUFFER: {
		if ( ! m_bRunning ) {
			break;
		}
		produce( pEvent->event_time );
		m_nFramesSent += m_nFrames;
		// Counted from the start, so rounding doesn't drift.
		media_timed_event next( m_startTime + (bigtime_t) ( m_nFramesSent * 1000000.0 / m_fFrameRate ),
								BTimedEventQueue::B_HANDLE_BUFFER );
		EventQueue()->AddEvent( next );
		break;
	}
	default:
		break;
	}
}

void MediaKitNode::produce( bigtime_t eventTime )
{
	// Rendered even with nothing connected, so the song keeps time, as a
	// JACK client does.
	const float* pLeft = nullptr;
	const float* pRight = nullptr;
	m_render( m_nFrames, &pLeft, &pRight );
	if ( pLeft == nullptr || pRight == nullptr ) {
		return;
	}
	for ( int32 i = 0; i < OutputCount; i++ ) {
		sendTo( i, pLeft, pRight, eventTime );
	}
}

void MediaKitNode::sendTo( int32 nOutput, const float* pLeft, const float* pRight, bigtime_t startTime )
{
	const OutputState& state = m_state[ nOutput ];
	if ( ! state.bConnected || ! state.bEnabled || state.pGroup == nullptr ) {
		return;
	}
	const media_raw_audio_format& raw = m_outputs[ nOutput ].format.u.raw_audio;
	BBuffer* pBuffer = state.pGroup->RequestBuffer( raw.buffer_size, BufferDuration() );
	if ( pBuffer == nullptr ) {
		m_nLate++;
		return;
	}
	void* pData = pBuffer->Data();
	switch ( raw.format ) {
	case media_raw_audio_format::B_AUDIO_FLOAT: {
		float* p = static_cast<float*>( pData );
		for ( int32 i = 0; i < m_nFrames; i++ ) {
			*p++ = pLeft[ i ];
			*p++ = pRight[ i ];
		}
		break;
	}
	case media_raw_audio_format::B_AUDIO_INT: {
		int32* p = static_cast<int32*>( pData );
		for ( int32 i = 0; i < m_nFrames; i++ ) {
			*p++ = (int32) ( std::clamp( pLeft[ i ], -1.0f, 1.0f ) * 2147483520.0f );
			*p++ = (int32) ( std::clamp( pRight[ i ], -1.0f, 1.0f ) * 2147483520.0f );
		}
		break;
	}
	case media_raw_audio_format::B_AUDIO_SHORT: {
		int16* p = static_cast<int16*>( pData );
		for ( int32 i = 0; i < m_nFrames; i++ ) {
			*p++ = (int16) ( std::clamp( pLeft[ i ], -1.0f, 1.0f ) * 32767.0f );
			*p++ = (int16) ( std::clamp( pRight[ i ], -1.0f, 1.0f ) * 32767.0f );
		}
		break;
	}
	default:
		pBuffer->Recycle();
		return;
	}
	media_header* pHeader = pBuffer->Header();
	pHeader->type = B_MEDIA_RAW_AUDIO;
	pHeader->size_used = raw.buffer_size;
	pHeader->time_source = TimeSource()->ID();
	pHeader->start_time = startTime;
	if ( SendBuffer( pBuffer, m_outputs[ nOutput ].source, m_outputs[ nOutput ].destination ) != B_OK ) {
		pBuffer->Recycle();
	}
}

};

#endif
