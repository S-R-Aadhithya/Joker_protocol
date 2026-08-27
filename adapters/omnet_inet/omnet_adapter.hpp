#pragma once
#include "joker/interface.hpp"
#include <omnetpp.h>
#include <vector>
#include <cstdint>

namespace joker {

// A NicAdapter implementation that integrates directly with OMNeT++'s INET Framework.
class OmnetAdapter : public NicAdapter {
public:
    explicit OmnetAdapter(omnetpp::cSimpleModule* module, MacAddress mac) 
        : module_(module), mac_(mac) {}

    void TransmitUnicast(const MacAddress& destination, const std::vector<uint8_t>& frame) override {
        // Create an INET-compatible cPacket
        omnetpp::cPacket* pkt = new omnetpp::cPacket("JokerUnicast");
        
        // In a real INET implementation, we would attach INET's MacAddressReq 
        // control info to instruct the lower 802.11 MAC layer on the destination.
        // pkt->addControlInfo(new inet::MacAddressReq(destination.ToString().c_str()));
        
        // For this skeleton, we assume gate 0 connects to the MAC layer
        module_->send(pkt, "lowerLayerOut");
    }

    void TransmitBroadcast(const std::vector<uint8_t>& frame) override {
        omnetpp::cPacket* pkt = new omnetpp::cPacket("JokerBroadcast");
        // pkt->addControlInfo(new inet::MacAddressReq("FF:FF:FF:FF:FF:FF"));
        module_->send(pkt, "lowerLayerOut");
    }

    void RegisterReceiveCallback(ReceiveCallback callback) override {
        callback_ = std::move(callback);
    }

    void Start() override {
        running_ = true;
    }
    
    void Stop() override {
        running_ = false;
    }

    [[nodiscard]] MacAddress GetMacAddress() const override {
        return mac_;
    }

    // Called by the OMNeT module's handleMessage when a packet arrives from the MAC layer.
    void HandleLowerMessage(omnetpp::cMessage* msg, bool is_candidate) {
        if (!running_ || !callback_) {
            delete msg;
            return;
        }

        // In a real INET implementation, we would extract the payload from the cPacket
        // std::vector<uint8_t> frame = ExtractPayload(msg);
        std::vector<uint8_t> frame; // Stub
        
        callback_(frame, mac_, is_candidate);
        delete msg;
    }

private:
    omnetpp::cSimpleModule* module_;
    MacAddress mac_;
    bool running_ = false;
    ReceiveCallback callback_;
};

} // namespace joker
