#pragma once

#include "video_player.h"

class VideoPlayer;

class VideoDecoder {
public:
    enum class BufferedDecodeResult {
        FrameReady,
        EndOfStream,
        RecoverableError
    };

    VideoDecoder(VideoPlayer* player);
    ~VideoDecoder();

    bool Initialize();
    void Cleanup();
    void ResetBufferedDecodeState();
    bool DecodeNextFrame(bool presentFrame, bool scheduleDisplay = true, bool generateImage = true);
    BufferedDecodeResult DecodeNextBufferedFrame(AVFrame* outputFrame, double& pts,
                                                  int64_t& frameNumber);

private:
    enum class BufferedReceiveResult {
        FrameReady,
        FrameDiscarded,
        NeedPacket,
        EndOfStream,
        RecoverableError
    };

    BufferedReceiveResult ReceiveBufferedFrame(AVFrame* outputFrame, double& pts,
                                                int64_t& frameNumber);

    VideoPlayer* m_player;
    // avcodec_send_packet(EAGAIN) means the decoder must be drained before the
    // exact same packet is retried. Dropping that packet wedges pipelined and
    // hardware decoders after their first few frames.
    bool m_bufferedPacketPending = false;
    bool m_bufferedDemuxEof = false;
    bool m_bufferedFlushSent = false;
    // Moderate high-speed playback should absorb short decoder hiccups by
    // dropping stale output frames. Seeking on the first late packet creates
    // a visible GOP-sized jump, so only escalate after lag is sustained.
    int64_t m_moderateLagStartNs = 0;
    double m_lastHighSpeedFrameDeliveryPts = -1.0;
};
