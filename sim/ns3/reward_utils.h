#ifndef REWARD_UTILS_H
#define REWARD_UTILS_H

#include "ns3/packet.h"
#include "attack-tag.h"

namespace ns3 {

/**
 * ONLY include this header from reward-computation code.
 * NEVER include it in telemetry/observation code.
 */
inline bool IsAttackPacket(Ptr<const Packet> pkt)
{
    AttackTag tag;
    pkt->PeekPacketTag(tag);
    return tag.IsAttack();
}

} // namespace ns3

#endif