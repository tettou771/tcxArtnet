#pragma once

// =============================================================================
// tcxArtnetNode.h - convenience coordinator that bundles a sender + receiver
// =============================================================================
// ArtnetSender and ArtnetReceiver stay single-responsibility (one direction
// each). ArtnetNode composes them for the cases that span both directions:
//   - discovery (controller): sendPoll() out, ArtPollReply in -> getNodes()
//   - being discoverable (node): ArtPoll in -> ArtPollReply out (opt-in)
//
// Reach the underlying objects with sender()/receiver() for DMX I/O; this class
// only adds the ArtPoll coordination and the node identity you advertise.
// =============================================================================

#include "tcxArtnetConstants.h"
#include "tcxArtnetTypes.h"
#include "tcxArtnetSender.h"
#include "tcxArtnetReceiver.h"

#include "tc/events/tcEventListener.h"

#include <string>
#include <vector>

namespace tcx {

class ArtnetNode {
public:
    ArtnetNode() = default;
    ~ArtnetNode() { close(); }

    ArtnetNode(const ArtnetNode&) = delete;
    ArtnetNode& operator=(const ArtnetNode&) = delete;

    // Set the send destination (default broadcast) and bind the receive port.
    bool setup(const std::string& sendHost = ARTNET_BROADCAST, int port = ARTNET_PORT) {
        bool a = sender_.setup(sendHost, port);
        bool b = receiver_.setup(port);
        return a && b;
    }
    void close() {
        enablePollReply(false);
        sender_.close();
        receiver_.close();
    }

    // Underlying objects for DMX I/O (setChannel/send on the sender, getChannel/
    // onDmx on the receiver, etc.).
    ArtnetSender& sender() { return sender_; }
    ArtnetReceiver& receiver() { return receiver_; }

    // ------------------------------------------------------------------ discovery (controller)
    bool sendPoll() { return sender_.sendPoll(); }
    std::vector<ArtnetNodeInfo> getNodes() const { return receiver_.getNodes(); }
    void clearNodes() { receiver_.clearNodes(); }

    // ------------------------------------------------------------------ being discoverable (node)
    // Identity advertised in ArtPollReply. Chainable.
    ArtnetNode& setShortName(const std::string& s) { identity_.shortName = s; return *this; }
    ArtnetNode& setLongName(const std::string& s)  { identity_.longName = s; return *this; }
    ArtnetNode& setUniverses(const std::vector<int>& u) { identity_.universes = u; return *this; }
    ArtnetNode& setVendor(uint16_t esta) { identity_.esta = esta; return *this; }  // ESTA code
    ArtnetNode& setOem(uint16_t oem) { identity_.oem = oem; return *this; }
    ArtnetNode& setIdentity(const NodeIdentity& id) { identity_ = id; return *this; }
    const NodeIdentity& getIdentity() const { return identity_; }

    // Opt-in: when on, answer incoming ArtPoll with an ArtPollReply built from
    // the current identity (replied unicast to the poller). Off by default — a
    // plain receiver isn't obliged to be discoverable.
    void enablePollReply(bool on = true) {
        if (on) {
            if (pollReplyEnabled_) return;
            pollListener_ = receiver_.onPoll.listen(
                [this](std::string& host) { sender_.sendPollReply(identity_, host); });
            pollReplyEnabled_ = true;
        } else {
            pollListener_.disconnect();
            pollReplyEnabled_ = false;
        }
    }
    bool isPollReplyEnabled() const { return pollReplyEnabled_; }

private:
    ArtnetSender sender_;
    ArtnetReceiver receiver_;
    NodeIdentity identity_;
    tc::EventListener pollListener_;
    bool pollReplyEnabled_ = false;
};

} // namespace tcx
