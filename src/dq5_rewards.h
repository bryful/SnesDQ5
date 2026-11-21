#pragma once
#include <cstdint>
#include <vector>
#include <utility>
#include <algorithm>
namespace dq5 {
inline bool validMultiplier(int n) { return n==1 || n==4 || n==8 || n==16 || n==32 || n==64 || n==128; }
inline bool rewardProfileMatches(const uint8_t* rom,size_t size) {
    if(size!=0x180000) return false;
    const uint8_t exp[]{0x18,0x6d,0xfc,0x10,0x8d,0xfc};
    const uint8_t gold[]{0x18,0x6d,0xff,0x10,0x8d,0xff};
    return std::equal(exp,exp+6,rom+0x140213) && std::equal(gold,gold+6,rom+0x14024f) &&
        rom[0x14022e]==0xc2 && rom[0x14022f]==0x30 && rom[0x14026a]==0xc2 && rom[0x14026b]==0x30 &&
        std::all_of(rom+0x5fc00,rom+0x5fd00,[](uint8_t b){return b==0;});
}
using RewardPatch=std::vector<std::pair<uint32_t,uint8_t>>;
inline void addRewardRoutine(RewardPatch& patch,uint32_t entry,uint32_t stub,uint16_t total,int multiplier) {
    if(multiplier==1) return;
    // The original entry has M=0, X=1. Preserve flags, X and the incoming 16-bit reward.
    // Repeated full 24-bit additions avoid truncation when 128x exceeds 65535.
    std::vector<uint8_t> code{0x08,0xc2,0x30,0xda,0x48,0xa2,uint8_t(multiplier),0};
    const size_t loop=code.size();
    auto abs=[&](uint8_t op,uint16_t address){code.push_back(op);code.push_back(uint8_t(address));code.push_back(uint8_t(address>>8));};
    code.insert(code.end(),{0xa3,0x01,0x18}); // LDA 1,S; CLC
    abs(0x6d,total); abs(0x8d,total); // ADC total; STA total
    code.insert(code.end(),{0xe2,0x20,0x90,0x10}); // SEP #$20; BCC no carry
    abs(0xee,total+2); code.insert(code.end(),{0xd0,0x0b,0xa9,0xff}); // INC high; BNE no overflow
    abs(0x8d,total); abs(0x8d,total+1); abs(0x8d,total+2); // Saturate to $ffffff like the original.
    code.insert(code.end(),{0xc2,0x20,0xca,0xd0}); // REP #$20; DEX; BNE loop
    code.push_back(uint8_t(int(loop)-int(code.size()+1)));
    code.insert(code.end(),{0x68,0xfa,0x28,0x6b}); // PLA; PLX; PLP; RTL
    for(size_t i=0;i<code.size();++i) patch.emplace_back(stub+uint32_t(i),code[i]);
    const uint8_t jump[]{0x22,uint8_t(stub),uint8_t(stub>>8),uint8_t(stub>>16),0x80,0x15};
    for(unsigned i=0;i<6;++i) patch.emplace_back(entry+i,jump[i]);
}
inline RewardPatch rewardPatches(int experience,int gold) {
    RewardPatch patch;
    addRewardRoutine(patch,0x288213,0x0bfc00,0x10fc,experience);
    addRewardRoutine(patch,0x28824f,0x0bfc80,0x10ff,gold);
    return patch;
}
inline bool insideRewardCode(uint32_t pc) {
    pc &= 0x7fffff;
    return (pc>=0x288213 && pc<0x28822e) || (pc>=0x28824f && pc<0x28826a) || (pc>=0x0bfc00 && pc<0x0bfd00);
}
}
