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

#ifndef MEDIA_KIT_DRIVER_H
#define MEDIA_KIT_DRIVER_H

#include <core/IO/AudioDriver.h>
#include <core/IO/NullDriver.h>

#if defined(H2CORE_HAVE_MEDIAKIT) || _DOXYGEN_

#include <atomic>

#include <MediaDefs.h>

#include <core/IO/MediaKitNode.h>


namespace H2Core
{

/** Audio output through the Haiku Media Kit.
 *
 * Hydrogen registers its own producer node, "Hydrogen" (MediaKitNode):
 * its "Main" output is connected to the system mixer at the mixer's
 * sample rate, its "Send" output carries the same mix for recorders and
 * other programs (connect it in Cortex, or from the other program). The
 * node asks Hydrogen for one buffer of the preferences' size at a time,
 * on the audio output's clock. */
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
	/** The node asks for the next @a nFrames of the mix. */
	void render( int32 nFrames, const float** ppLeft, const float** ppRight );

	audioProcessCallback m_processCallback;
	MediaKitNode* m_pNode;
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
