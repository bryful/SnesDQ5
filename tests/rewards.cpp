#define SDL_MAIN_HANDLED
#include "../src/core.h"
#include "../src/storage.h"
#include "../src/dq5_rewards.h"
#include "../src/dq5_options.h"
#include "snes9x.h"
#include "memmap.h"
#include "cpuexec.h"
#include "getset.h"
#include <SDL.h>
#include <cstdio>
#include <stdexcept>
#include <algorithm>
uint32_t addition(bool gold,unsigned reward,uint32_t initial) {
    unsigned total=gold?0x10ff:0x10fc;
    for(int i=0;i<3;++i) Memory.RAM[total+i]=uint8_t(initial>>(8*i));
    Registers.P.W=IndexFlag|IRQ; Registers.DB=0x7e;Registers.D.W=0;Registers.S.W=0x1fff;
    Registers.A.W=uint16_t(reward);Registers.X.W=0x36;Registers.Y.W=0x42;
    ICPU.ShiftedDB=0x7e0000;S9xUnpackStatus();S9xFixCycles();
    S9xSetPCBase(gold?0x28824f:0x288213);
    CPU.Cycles=0;CPU.NextEvent=100000000;CPU.Flags=0;
    unsigned steps=0,stop=gold?0x28826a:0x28822e;
    while(Registers.PBPC!=stop && ++steps<10000) {
        uint8_t op=S9xGetByte(Registers.PBPC); Registers.PCw++;
        ICPU.S9xOpcodes[op].S9xOpcode();
    }
    if(steps>=10000 || Registers.S.W!=0x1fff || Registers.X.W!=0x36 || Registers.Y.W!=0x42) { std::fprintf(stderr,"pc=%06x steps=%u S=%04x X=%04x Y=%04x P=%04x\n",Registers.PBPC,steps,Registers.S.W,Registers.X.W,Registers.Y.W,Registers.P.W); throw std::runtime_error("Reward routine did not preserve stack/registers"); }
    return Memory.RAM[total]|(uint32_t(Memory.RAM[total+1])<<8)|(uint32_t(Memory.RAM[total+2])<<16);
}
bool equipmentAllowed(unsigned actor,unsigned item) {
    Registers.P.W=IndexFlag|IRQ; Registers.DB=0x7e;Registers.D.W=0;Registers.S.W=0x1fff;
    Registers.A.W=uint16_t((actor<<8)|item);Registers.X.W=0x36;Registers.Y.W=0x42;
    ICPU.ShiftedDB=0x7e0000;S9xUnpackStatus();S9xFixCycles();S9xSetPCBase(0x21a53b);
    CPU.Cycles=0;CPU.NextEvent=100000000;CPU.Flags=0;
    unsigned steps=0;
    while(Registers.PBPC!=0x21a4f3 && Registers.PBPC!=0x21a4fa && ++steps<1000) {
        uint8_t op=S9xGetByte(Registers.PBPC);Registers.PCw++;ICPU.S9xOpcodes[op].S9xOpcode();
    }
    if(steps>=1000 || Registers.S.W!=0x1fff) throw std::runtime_error("Equipment routine stack mismatch");
    return ICPU._Carry!=0;
}
void itemUseDecision(bool battle,unsigned item,bool keep) {
    Registers.P.W=MemoryFlag|IndexFlag|IRQ;Registers.DB=0x7e;Registers.D.W=0;Registers.S.W=0x1fff;
    Registers.A.W=uint16_t(item);Registers.X.W=0x36;Registers.Y.W=0x42;
    ICPU.ShiftedDB=0x7e0000;S9xUnpackStatus();S9xFixCycles();
    uint32_t start=battle?0x20ba5e:0x1188d6,remove=battle?0x20ba63:0x1188db,skip=battle?0x20ba75:0x1188de;
    S9xSetPCBase(start);CPU.Cycles=0;CPU.NextEvent=100000000;CPU.Flags=0;
    unsigned steps=0;
    while(Registers.PBPC!=remove && Registers.PBPC!=skip && ++steps<1000) {
        uint8_t op=S9xGetByte(Registers.PBPC);Registers.PCw++;ICPU.S9xOpcodes[op].S9xOpcode();
    }
    bool consumable=(S9xGetByte(0x23b159+item)&0x20)!=0;
    if(steps>=1000 || Registers.PBPC!=((keep||!consumable)?skip:remove) || Registers.S.W!=0x1fff || Registers.A.W!=item || Registers.X.W!=0x36 || Registers.Y.W!=0x42)
        throw std::runtime_error("Field/battle consumable decision failed");
}
unsigned levelGain(unsigned species,unsigned level,unsigned cap,bool threshold=false) {
    std::fill(Memory.RAM+0x2040,Memory.RAM+0x206a,0);
    Memory.RAM[0x2040]=uint8_t(species);Memory.RAM[0x2052]=uint8_t(level);Memory.RAM[0x2053]=uint8_t(cap|0x80);
    Memory.RAM[0x204b]=Memory.RAM[0x204c]=Memory.RAM[0x204d]=threshold?0:0xff;
    Memory.RAM[0x2066]=1;Memory.RAM[0x2062]=0;
    Memory.RAM[0x2020]=0;Memory.RAM[0x201d]=Memory.RAM[0x201e]=1;
    Registers.P.W=MemoryFlag|IRQ;Registers.DB=0x7e;Registers.D.W=0;Registers.S.W=0x1ffc;
    Memory.RAM[0x1fff]=0;Memory.RAM[0x1ffe]=0x7f;Memory.RAM[0x1ffd]=0xff;
    Registers.A.W=0;Registers.X.W=0x36;Registers.Y.W=0x42;
    ICPU.ShiftedDB=0x7e0000;S9xUnpackStatus();S9xFixCycles();S9xSetPCBase(threshold?0x23ed58:0x23eb50);
    CPU.Cycles=0;CPU.NextEvent=100000000;CPU.Flags=0;
    unsigned steps=0;
    while(Registers.PBPC!=0x008000 && ++steps<200000) {
        uint8_t op=S9xGetByte(Registers.PBPC);Registers.PCw++;ICPU.S9xOpcodes[op].S9xOpcode();
    }
    if(steps>=200000 || Registers.S.W!=0x1fff || Memory.RAM[0x2053]!=(cap|0x80)) {
        std::fprintf(stderr,"level pc=%06x steps=%u stack=%04x cap=%02x\n",Registers.PBPC,steps,Registers.S.W,Memory.RAM[0x2053]);
        throw std::runtime_error("Native level routine failed or changed cached cap/hat flag");
    }
    if(threshold) return Memory.RAM[0]|(unsigned(Memory.RAM[1])<<8)|(unsigned(Memory.RAM[2])<<16);
    return ICPU._Carry?Memory.RAM[0x9a6]:level;
}
uint32_t branchTarget(uint32_t address,bool zero) {
    Registers.P.W=MemoryFlag|IndexFlag|IRQ|(zero?Zero:0);
    S9xUnpackStatus();S9xFixCycles();S9xSetPCBase(address);
    CPU.Cycles=0;CPU.NextEvent=100000000;CPU.Flags=0;
    uint8_t op=S9xGetByte(Registers.PBPC);Registers.PCw++;ICPU.S9xOpcodes[op].S9xOpcode();return Registers.PBPC;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    if(argc!=2) { std::fputs("Usage: reward-check <your ROM path>\n",stderr);return 2; }
    auto path=std::filesystem::path(argv[1]).u8string();
    SDL_SetMainReady();SDL_Init(0);
    storage::setTestRoot(std::filesystem::absolute("build/reward-verification/data"));
    try {
        Core core;core.load(path);
        if(!core.rewardsAvailable) throw std::runtime_error("ROM profile was not recognized");
        for(int n:{1,4,8,16,32,64,128}) {
            core.options.experience=n;core.options.gold=n;core.configure();
            struct Case {bool gold;unsigned reward;uint32_t initial;};
            for(auto test:{Case{false,513,65535},Case{true,65535,0x123456},Case{false,0,12345},Case{true,7,0xfffffe}}) {
                auto actual=addition(test.gold,test.reward,test.initial);
                auto expected=uint32_t(std::min(uint64_t(0xffffff),uint64_t(test.initial)+uint64_t(test.reward)*n));
                if(actual!=expected) { std::fprintf(stderr,"multiplier=%d gold=%d actual=%u expected=%u\n",n,test.gold,actual,expected);return 3; }
            }
            std::printf("%dx experience/gold: PASS (carry, zero, saturation)\n",n);
        }
        core.options.experience=core.options.gold=1;core.configure();
        if(S9xGetByte(0x288213)!=0x18 || S9xGetByte(0x28824f)!=0x18 || S9xGetByte(0x0bfc00)!=0) throw std::runtime_error("Cheat disable did not restore ROM");
        core.options.experience=128;core.options.gold=4;core.configure();
        core.saveState(1);core.options.experience=1;core.options.gold=1;core.configure();core.loadState(1);
        if(core.options.experience!=128 || core.options.gold!=4) throw std::runtime_error("State did not restore reward settings");
        std::puts("Cheat disable and state settings restore: PASS");
        if(!core.gameOptionsAvailable) throw std::runtime_error("Game option profile was not recognized");
        core.options.recruit=core.options.drop=core.options.equipment=core.options.items=core.options.level=true;core.configure();
        for(const auto& byte:dq5::optionBytes) if(S9xGetByte(byte.address)!=byte.value) throw std::runtime_error("Game option byte missing");
        for(bool zero:{false,true}) {
            if(branchTarget(0x20c2b7,zero)!=0x20c2b9) throw std::runtime_error("Recruit chance failed");
            if(branchTarget(0x2589a2,zero)!=0x2589a7) throw std::runtime_error("Drop chance failed");
        }
        for(unsigned actor:{0u,7u,0x80u,0x8eu}) for(unsigned item:{0u,0x10u,0x60u}) {
            if(!equipmentAllowed(actor,item)) throw std::runtime_error("Equipment permission not removed");
        }
        if(equipmentAllowed(0,0xff)) throw std::runtime_error("Empty inventory slot became equipment");
        for(bool battle:{false,true}) for(unsigned item:{0xa1u,0xa2u,0xadu,0xb1u,0u,0x2bu}) itemUseDecision(battle,item,true);
        for(unsigned species=0x80;species<=0xa9;++species) {
            unsigned cap=S9xGetByte(0x23aed2+(species-0x80)*9);
            for(unsigned level:{cap,98u,99u}) { auto actual=levelGain(species,level,cap); if(actual!=std::min(99u,level+1)) {std::fprintf(stderr,"species=%02x level=%u cap=%u actual=%u max=%u current=%u\n",species,level,cap,actual,Memory.RAM[0x0b],Memory.RAM[0x9a6]);throw std::runtime_error("Monster level cap was not raised safely to 99");}}
        }
        if(!levelGain(0x8f,8,8,true)) throw std::runtime_error("Next-level experience did not resume beyond original cap");
        if(levelGain(0,99,99)!=99 || levelGain(8,20,20)!=20) throw std::runtime_error("Human/NPC level cap changed");
        core.saveState(3);core.options.recruit=core.options.drop=core.options.equipment=core.options.items=core.options.level=false;core.configure();core.loadState(3);
        if(!core.options.recruit || !core.options.drop || !core.options.equipment || !core.options.items || !core.options.level) throw std::runtime_error("Game options were not restored");
        core.options.recruit=core.options.drop=core.options.equipment=core.options.items=core.options.level=false;core.configure();
        for(const auto& byte:dq5::optionBytes) if(S9xGetByte(byte.address)!=(byte.feature==dq5::Level?byte.value:byte.original)) throw std::runtime_error("Game options did not restore original ROM");
        for(bool battle:{false,true}) for(unsigned item:{0xa1u,0xa2u,0xadu,0xb1u,0u,0x2bu}) itemUseDecision(battle,item,false);
        std::puts("Native BRK field/battle item-use decisions, disable and state restore: PASS");
        if(levelGain(0x8f,8,8)!=8 || levelGain(0x93,7,7)!=7) throw std::runtime_error("Original monster cap was not restored");
        if(levelGain(0x8f,8,8,true)) throw std::runtime_error("Normal capped experience threshold was not restored");
        std::puts("Native monster level gain, 99 cap, humans, hat flag and disable: PASS");
        bool restricted=false;
        for(unsigned actor:{0u,7u,0x80u,0x8eu}) for(unsigned item:{0u,0x10u,0x60u}) restricted|=!equipmentAllowed(actor,item);
        if(!restricted) throw std::runtime_error("Equipment restriction disable did not take effect");
        std::puts("Recruit/drop branches, human/monster equipment, disable and state restore: PASS");
        core.options.recruit=core.options.drop=core.options.equipment=core.options.items=core.options.level=true;
        core.load(path); for(int frame=0;frame<600;++frame) core.run();
        if(core.pixels.empty() || !core.audioFrames) throw std::runtime_error("Patched ROM boot failed");
        core.options.experience=core.options.gold=1;core.configure();core.saveState(2);
        auto state=storage::read(core.romName+".slot2.state");
        std::vector<uint8_t> version2(state.begin(),state.begin()+24);version2[7]='2';version2.insert(version2.end(),state.begin()+28,state.end());
        storage::write(core.romName+".slot4.state",version2.data(),version2.size());
        core.options.recruit=core.options.drop=core.options.equipment=core.options.items=core.options.level=true;core.configure();core.loadState(4);
        if(core.options.experience!=1 || core.options.gold!=1 || core.options.recruit || core.options.drop || core.options.equipment || core.options.items || core.options.level) throw std::runtime_error("Version 2 state compatibility failed");
        std::vector<uint8_t> legacy(state.begin(),state.begin()+16);legacy[7]='1';legacy.insert(legacy.end(),state.begin()+28,state.end());
        storage::write(core.romName+".slot2.state",legacy.data(),legacy.size());
        core.options.experience=128;core.options.gold=128;core.configure();core.loadState(2);
        if(core.options.experience!=1 || core.options.gold!=1 || core.options.recruit || core.options.drop || core.options.equipment || core.options.items || core.options.level) throw std::runtime_error("Legacy state did not use original reward code");
        for(int frame=0;frame<30;++frame) core.run();
        std::puts("Patched ROM boot and legacy state resume: PASS");
    } catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
    return 0;
}
