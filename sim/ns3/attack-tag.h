#ifndef ATTACK_TAG_H
#define ATTACK_TAG_H

#include "ns3/tag.h"
#include "ns3/log.h"

namespace ns3 {

enum class AttackType : uint8_t {
    NONE = 0,
    UDP_FLOOD = 1,
    TCP_SYN_FLOOD = 2,
    ICMP_FLOOD = 3
};

class AttackTag : public Tag
{
public:
    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("AttackTag")
            .SetParent<Tag>()
            .AddConstructor<AttackTag>();
        return tid;
    }

    TypeId GetInstanceTypeId() const override { return GetTypeId(); }
    uint32_t GetSerializedSize() const override { return sizeof(bool) + sizeof(uint8_t); }

    void Serialize(TagBuffer buf) const override {
        buf.WriteU8(static_cast<uint8_t>(m_isAttack));
        buf.WriteU8(m_type);
    }

    void Deserialize(TagBuffer buf) override {
        m_isAttack = buf.ReadU8();
        m_type = buf.ReadU8();
    }

    void Print(std::ostream &os) const override {
        os << "isAttack=" << m_isAttack << ", type=" << static_cast<int>(m_type);
    }

    void SetAttack(bool isAttack, AttackType type = AttackType::NONE) {
        m_isAttack = isAttack;
        m_type = static_cast<uint8_t>(type);
    }

    bool IsAttack() const { return m_isAttack; }
    AttackType GetAttackType() const { return static_cast<AttackType>(m_type); }

private:
    bool m_isAttack = false;
    uint8_t m_type = 0;
};

} // namespace ns3
#endif