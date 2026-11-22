#pragma once
#include "dq5_rewards.h"
namespace dq5 {
inline bool inputProfileMatches(const uint8_t* rom,size_t size) {
    if(size!=0x180000) return false;
    const uint8_t wait[]{0x20,0x35,0x9d,0xa5,0x24,0x48,0x29,0xe0};
    for(size_t offset:{0x25247u,0x2525cu,0x25283u})
        if(!std::equal(std::begin(wait),std::end(wait),rom+offset)) return false;
    return std::all_of(rom+0x27f00,rom+0x27f40,[](uint8_t b){return b==0;});
}
inline void appendInputPatches(RewardPatch& patch,int speed) {
    // Reuse DQ5's frame wait and key-repeat logic. Only the cursor polling
    // waits are stretched; walking and the raw joypad state remain unchanged.
    // Keep the trampoline installed at 1x too, so states can resume inside it.
    const uint8_t code[]{
        0x08,0xe2,0x30,0x48,0xda,       // PHP; SEP #$30; PHA; PHX
        0xaf,0x3f,0xff,0x04,0xaa,       // LDA.l speed; TAX
        0x20,0x35,0x9d,0xca,0xd0,0xfa, // JSR $9d35; DEX; BNE loop
        0xfa,0x68,0x28,0x60             // PLX; PLA; PLP; RTS
    };
    for(uint32_t address:{0x04d247u,0x04d25cu,0x04d283u}) {
        patch.emplace_back(address+1,0x00); patch.emplace_back(address+2,0xff);
    }
    for(size_t i=0;i<sizeof(code);++i) patch.emplace_back(0x04ff00+uint32_t(i),code[i]);
    patch.emplace_back(0x04ff3f,uint8_t(speed));
}
}
