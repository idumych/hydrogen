/*
 * Hydrogen
 * Copyright(c) 2002-2008 by Alex >Comix< Cominu [comix@users.sourceforge.net]
 * Copyright(c) 2008-2026 The hydrogen development team [hydrogen-devel@lists.sourceforge.net]
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

#ifndef MEDIA_KIT_DRIVER_H
#define MEDIA_KIT_DRIVER_H

#include <core/IO/AudioDriver.h>
#include <core/IO/NullDriver.h>

#if defined(H2CORE_HAVE_MEDIAKIT) || _DOXYGEN_

#include <atomic>

#include <MediaDefs.h>

class BSoundPlayer;

namespace H2Core
{

/** Audio output through the Haiku Media Kit.
 *
 * A BSoundPlayer is a producer node connected to the system mixer.
 * Its callback asks Hydrogen for audio in chunks of at most
 * MAX_BUFFER_SIZE frames and interleaves them into the mixer's
 * buffer, converting to whatever sample format the connection ended
 * up with. The mixer resamples, so any sample rate is accepted. */
class MediaKitDriver : public Object<MediaKitDriver>, public AudioDriver
{
	H2_OBJECT(MediaKitDriver)
public:
	MediaKitDriver( audioProcessCallback processCallback );
	virtual ~MediaKitDriver();

	virtual int init( unsigned nBufferSize ) override;
	virtual int connect() override;
	virtual void disconnect() override;
	virtual unsigned getBufferSize() override;
	virtual unsigned getSampleRate() override;
	virtual int getLatency() override;
	virtual int getXRuns() const override;
	virtual float* getOut_L() override;
	virtual float* getOut_R() override;

	QString toQString( const QString& sPrefix = "", bool bShort = true ) const override;

private:
	static void playBuffer( void* pCookie, void* pBuffer, size_t nSize,
							const media_raw_audio_format& format );
	void fill( void* pBuffer, size_t nSize, const media_raw_audio_format& format );

	audioProcessCallback m_processCallback;
	BSoundPlayer* m_pPlayer;
	float* m_pOut_L;
	float* m_pOut_R;
	unsigned m_nSampleRate;
	unsigned m_nBufferSize;
	std::atomic<int> m_nXRuns;
};

};

#else

namespace H2Core
{

class MediaKitDriver : public NullDriver
{
	H2_OBJECT(MediaKitDriver)
public:
	MediaKitDriver( audioProcessCallback processCallback ) : NullDriver( processCallback ) {}
};

};

#endif

#endif
