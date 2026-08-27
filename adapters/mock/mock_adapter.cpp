#include "adapters/mock/mock_adapter.hpp"

namespace joker {

void MockAdapter::TransmitUnicast(const MacAddress& destination, const std::vector<uint8_t>& frame) {
    if (!running_) return;
    std::lock_guard<std::mutex> lock(tx_mutex_);
    transmitted_frames_.push_back({destination, frame, false});
}

void MockAdapter::TransmitBroadcast(const std::vector<uint8_t>& frame) {
    if (!running_) return;
    std::lock_guard<std::mutex> lock(tx_mutex_);
    // Broadcast MAC address
    std::array<uint8_t, 6> bcast = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    transmitted_frames_.push_back({MacAddress(bcast), frame, true});
}

void MockAdapter::RegisterReceiveCallback(ReceiveCallback callback) {
    callback_ = std::move(callback);
}

void MockAdapter::Start() {
    running_ = true;
}

void MockAdapter::Stop() {
    running_ = false;
}

void MockAdapter::InjectIncomingFrame(const std::vector<uint8_t>& frame, bool is_candidate) {
    if (running_ && callback_) {
        callback_(frame, mac_, is_candidate);
    }
}

std::vector<MockAdapter::TransmittedFrame> MockAdapter::GetTransmittedFrames() const {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    return transmitted_frames_;
}

void MockAdapter::ClearTransmittedFrames() {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    transmitted_frames_.clear();
}

}  // namespace joker
