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

#ifndef MEDIA_KIT_NODE_H
#define MEDIA_KIT_NODE_H

#include <core/config.h>

#if defined(H2CORE_HAVE_MEDIAKIT) || _DOXYGEN_

#include <atomic>
#include <functional>

#include <BufferProducer.h>
#include <MediaEventLooper.h>

namespace H2Core
{

/** Hydrogen as a Media Kit producer node (the Media Kit's counterpart of
 * a JACK client): "Main" goes to the system mixer, "Send" carries the
 * same mix for any other consumer — a recorder, PowerMix, whatever Cortex
 * connects it to. Both outputs use one frame rate and buffer length; each
 * may have its own sample format. */
class MediaKitNode : public BBufferProducer, public BMediaEventLooper
{
public:
	/** Asked for @a nFrames of the mix; returns pointers to them. */
	using Render = std::function<void( int32 nFrames, const float** ppLeft, const float** ppRight )>;

	enum Output : int32 { Main = 0, Send = 1, OutputCount = 2 };

	MediaKitNode( const char* sName, float fFrameRate, int32 nFrames, Render render );
	virtual ~MediaKitNode();

	float frameRate() const { return m_fFrameRate; }
	int32 frames() const { return m_nFrames; }
	int xruns() const { return m_nLate; }
	bigtime_t latency() const { return m_latency + m_internalLatency; }
	const media_output& output( Output which ) const { return m_outputs[ which ]; }

	// BMediaNode
	BMediaAddOn* AddOn( int32* pInternalId ) const override;

protected:
	void NodeRegistered() override;
	status_t HandleMessage( int32 nCode, const void* pData, size_t nSize ) override;

	// BBufferProducer
	status_t FormatSuggestionRequested( media_type type, int32 nQuality, media_format* pFormat ) override;
	status_t FormatProposal( const media_source& output, media_format* pFormat ) override;
	status_t FormatChangeRequested( const media_source& source, const media_destination& destination,
									media_format* pFormat, int32* _deprecated_ ) override;
	status_t GetNextOutput( int32* pCookie, media_output* pOutput ) override;
	status_t DisposeOutputCookie( int32 nCookie ) override;
	status_t SetBufferGroup( const media_source& source, BBufferGroup* pGroup ) override;
	status_t GetLatency( bigtime_t* pLatency ) override;
	status_t PrepareToConnect( const media_source& source, const media_destination& destination,
							   media_format* pFormat, media_source* pSource, char* sName ) override;
	void Connect( status_t error, const media_source& source, const media_destination& destination,
				  const media_format& format, char* sName ) override;
	void Disconnect( const media_source& source, const media_destination& destination ) override;
	void LateNoticeReceived( const media_source& source, bigtime_t howMuch, bigtime_t performanceTime ) override;
	void EnableOutput( const media_source& source, bool bEnabled, int32* _deprecated_ ) override;
	void LatencyChanged( const media_source& source, const media_destination& destination,
						 bigtime_t newLatency, uint32 nFlags ) override;

	// BMediaEventLooper
	void HandleEvent( const media_timed_event* pEvent, bigtime_t lateness, bool bRealTimeEvent = false ) override;

private:
	struct OutputState {
		bool bConnected = false;
		bool bEnabled = true;
		BBufferGroup* pGroup = nullptr;
		bool bOwnGroup = false;
	};

	/** The format both outputs offer: float stereo at the node's rate. */
	media_format preferredFormat() const;
	/** Whether @a format fits: raw audio, stereo, the node's rate and
	 * buffer length (wildcards are filled in). */
	bool acceptFormat( media_format* pFormat ) const;
	int32 outputFor( const media_source& source ) const;
	void produce( bigtime_t eventTime );
	void sendTo( int32 nOutput, const float* pLeft, const float* pRight, bigtime_t startTime );

	float m_fFrameRate;
	int32 m_nFrames;
	Render m_render;
	media_output m_outputs[ OutputCount ];
	OutputState m_state[ OutputCount ];
	bigtime_t m_latency = 0;			///< downstream, worst of the outputs
	bigtime_t m_internalLatency = 0;	///< one buffer's processing time
	bigtime_t m_startTime = 0;
	int64 m_nFramesSent = 0;
	bool m_bRunning = false;
	std::atomic<int> m_nLate{ 0 };
};

};

#endif

#endif
