#pragma once
#include <stdint.h>
namespace RobotControlPolicy {
constexpr uint32_t leaseMs=350;
inline bool valid(int left,int right,int arm,int bucket,int aux){return left>=-255&&left<=255&&right>=-255&&right<=255&&arm>=-255&&arm<=255&&bucket>=0&&bucket<=180&&aux>=0&&aux<=180;}
inline bool expired(uint32_t now,uint32_t received){return uint32_t(now-received)>=leaseMs;}
inline bool ownedByOther(uint32_t owner,uint32_t requester,uint32_t now,uint32_t received){return owner&&owner!=requester&&!expired(now,received);}
inline bool reservedPin(int pin){return pin==16||pin==17||pin==18||pin==19||pin==43||pin==44||pin==0||pin==20||pin==21||pin==1;}
}
