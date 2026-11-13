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
#include <SoundPlayer.h>

#include <core/Preferences/Preferences.h>

namespace H2Core
{

MediaKitDriver::MediaKitDriver( audioProcessCallback processCallback )
		: AudioDriver()
		, m_processCallback( processCallback )
		, m_pPlayer( nullptr )
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

	m_pOut_L = new float[ MAX_BUFFER_SIZE ];
	m_pOut_R = new float[ MAX_BUFFER_SIZE ];
	std::fill_n( m_pOut_L, MAX_BUFFER_SIZE, 0.0f );
	std::fill_n( m_pOut_R, MAX_BUFFER_SIZE, 0.0f );

	media_raw_audio_format format = media_raw_audio_format::wildcard;
	format.frame_rate = m_nSampleRate;
	format.channel_count = 2;
	format.format = media_raw_audio_format::B_AUDIO_FLOAT;
	format.byte_order = B_MEDIA_HOST_ENDIAN;
	format.buffer_size = m_nBufferSize * 2 * sizeof( float );

	m_pPlayer = new BSoundPlayer( &format, "Hydrogen", playBuffer, nullptr, this );
	status_t err = m_pPlayer->InitCheck();
	if ( err != B_OK ) {
		ERRORLOG( QString( "Unable to create sound player: %1" ).arg( strerror( err ) ) );
		disconnect();
		return 1;
	}

	const media_raw_audio_format& actual = m_pPlayer->Format();
	if ( actual.channel_count != 2 ) {
		ERRORLOG( QString( "Media Kit gave %1 channels, need 2" )
				  .arg( actual.channel_count ) );
		disconnect();
		return 1;
	}
	if ( (unsigned) actual.frame_rate != m_nSampleRate ) {
		WARNINGLOG( QString( "Couldn't get sample rate %1, using %2 instead" )
					.arg( m_nSampleRate ).arg( actual.frame_rate ) );
		m_nSampleRate = (unsigned) actual.frame_rate;
	}
	INFOLOG( QString( "Media Kit format: %1 Hz, format 0x%2, %3 byte buffers" )
			 .arg( actual.frame_rate ).arg( actual.format, 0, 16 )
			 .arg( actual.buffer_size ) );

	err = m_pPlayer->Start();
	if ( err != B_OK ) {
		ERRORLOG( QString( "Unable to start sound player: %1" ).arg( strerror( err ) ) );
		disconnect();
		return 1;
	}
	m_pPlayer->SetHasData( true );

	return 0;
}

void MediaKitDriver::disconnect()
{
	if ( m_pPlayer != nullptr ) {
		m_pPlayer->SetHasData( false );
		m_pPlayer->Stop();
		delete m_pPlayer;
		m_pPlayer = nullptr;
	}

	delete[] m_pOut_L;
	m_pOut_L = nullptr;

	delete[] m_pOut_R;
	m_pOut_R = nullptr;
}

void MediaKitDriver::playBuffer( void* pCookie, void* pBuffer, size_t nSize,
								 const media_raw_audio_format& format )
{
	static_cast<MediaKitDriver*>( pCookie )->fill( pBuffer, nSize, format );
}

void MediaKitDriver::fill( void* pBuffer, size_t nSize,
						   const media_raw_audio_format& format )
{
	const bigtime_t start = system_time();

	const size_t nSampleSize = format.format & media_raw_audio_format::B_AUDIO_SIZE_MASK;
	const size_t nFrameSize = nSampleSize * 2;
	size_t nFramesLeft = nSize / nFrameSize;
	const size_t nTotalFrames = nFramesLeft;
	char* pOut = static_cast<char*>( pBuffer );

	while ( nFramesLeft > 0 ) {
		const size_t nFrames = std::min( nFramesLeft, (size_t) MAX_BUFFER_SIZE );
		m_processCallback( nFrames, nullptr );

		switch ( format.format ) {
		case media_raw_audio_format::B_AUDIO_FLOAT: {
			float* p = reinterpret_cast<float*>( pOut );
			for ( size_t i = 0; i < nFrames; i++ ) {
				*p++ = m_pOut_L[ i ];
				*p++ = m_pOut_R[ i ];
			}
			break;
		}
		case media_raw_audio_format::B_AUDIO_INT: {
			int32* p = reinterpret_cast<int32*>( pOut );
			for ( size_t i = 0; i < nFrames; i++ ) {
				*p++ = (int32) ( std::clamp( m_pOut_L[ i ], -1.0f, 1.0f ) * 2147483520.0f );
				*p++ = (int32) ( std::clamp( m_pOut_R[ i ], -1.0f, 1.0f ) * 2147483520.0f );
			}
			break;
		}
		case media_raw_audio_format::B_AUDIO_SHORT: {
			int16* p = reinterpret_cast<int16*>( pOut );
			for ( size_t i = 0; i < nFrames; i++ ) {
				*p++ = (int16) ( std::clamp( m_pOut_L[ i ], -1.0f, 1.0f ) * 32767.0f );
				*p++ = (int16) ( std::clamp( m_pOut_R[ i ], -1.0f, 1.0f ) * 32767.0f );
			}
			break;
		}
		default:
			memset( pOut, 0, nFrames * nFrameSize );
			break;
		}

		pOut += nFrames * nFrameSize;
		nFramesLeft -= nFrames;
	}

	// A callback that runs longer than the audio it produced has
	// missed its deadline: the mixer will have played silence.
	if ( format.frame_rate > 0 &&
		 system_time() - start > (bigtime_t) ( nTotalFrames * 1000000.0 / format.frame_rate ) ) {
		m_nXRuns++;
	}
}

unsigned MediaKitDriver::getBufferSize()
{
	if ( m_pPlayer == nullptr ) {
		return m_nBufferSize;
	}
	const media_raw_audio_format& format = m_pPlayer->Format();
	const size_t nFrameSize = ( format.format & media_raw_audio_format::B_AUDIO_SIZE_MASK ) * 2;
	if ( nFrameSize == 0 ) {
		return m_nBufferSize;
	}
	return std::min( (unsigned) ( format.buffer_size / nFrameSize ), (unsigned) MAX_BUFFER_SIZE );
}

unsigned MediaKitDriver::getSampleRate()
{
	return m_nSampleRate;
}

int MediaKitDriver::getLatency()
{
	if ( m_pPlayer == nullptr ) {
		return 0;
	}
	return std::max( (int) ( m_pPlayer->Latency() * (double) m_nSampleRate / 1000000.0 ), 0 );
}

int MediaKitDriver::getXRuns() const
{
	return m_nXRuns;
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
