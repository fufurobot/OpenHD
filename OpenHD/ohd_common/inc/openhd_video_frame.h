//
// Created by consti10 on 06.12.22.
//

#ifndef OPENHD_OPENHD_OHD_COMMON_OPENHD_VIDEO_FRAME_H_
#define OPENHD_OPENHD_OHD_COMMON_OPENHD_VIDEO_FRAME_H_

#include <chrono>
#include <memory>
#include <sstream>
#include <vector>

namespace openhd {

// R.n this is the best name i can come up with
// This is not required to be exactly one frame, but should be
// already packetized into rtp fragments
// R.n it is always either h264,h265 or mjpeg fragmented using the RTP protocol
struct FragmentedVideoFrame {
  std::vector<std::shared_ptr<std::vector<uint8_t>>> rtp_fragments;
  // Time point of when this frame was produced, as early as possible.
  // ideally, this would be the time point when the frame was generated by the
  // CMOS - but r.n no platform supports measurements this deep.
  std::chrono::steady_clock::time_point creation_time =
      std::chrono::steady_clock::now();
  // OpenHD WB supprts changing encryption on the fly - and r.n no other
  // implementation exists. For the future: This hints that the link
  // implementation should encrypt the data as secure as possible even though
  // that might result in higher CPU load.
  bool enable_ultra_secure_encryption = false;
  std::shared_ptr<std::vector<uint8_t>> dirty_frame =
      nullptr;  // replaces fragments
  // Set to true if the stream is an intra stream
  bool is_intra_stream = false;
  // Set to true if this frame is an IDR frame and therefore we can safely drop
  // previous frame(s) without having complete corruption
  bool is_idr_frame = false;
  std::string to_string() const {
    int total_bytes = 0;
    for (auto& fragment : rtp_fragments) total_bytes += fragment->size();
    if (dirty_frame) total_bytes += dirty_frame->size();
    std::stringstream ss;
    ss << "Bytes:" << total_bytes << " Fragments:" << rtp_fragments.size();
    ss << " IDR:" << (is_idr_frame ? "Y" : "N");
    return ss.str();
  }
};
typedef std::function<void(int stream_index, const openhd::FragmentedVideoFrame&
                                                 fragmented_video_frame)>
    ON_ENCODE_FRAME_CB;

struct AudioPacket {
  std::shared_ptr<std::vector<uint8_t>> data;
};
typedef std::function<void(const openhd::AudioPacket& audio_packet)>
    ON_AUDIO_TX_DATA_PACKET;

}  // namespace openhd
#endif  // OPENHD_OPENHD_OHD_COMMON_OPENHD_VIDEO_FRAME_H_
