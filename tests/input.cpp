#define SDL_MAIN_HANDLED
#include "../src/core.h"
#include "../src/storage.h"
#include "snes9x.h"
#include "memmap.h"
#include "cpuexec.h"
#include "getset.h"
#include <SDL.h>
#include <cstdio>
#include <stdexcept>

// Run the ROM's cursor-repeat routine with its existing one-frame wait
// intercepted, so no UI, user saves, or wall-clock timing is involved.
unsigned cursorWait(unsigned phase,unsigned mode,unsigned menuType,bool change) {
    Memory.RAM[0xada]=uint8_t(phase); Memory.RAM[0xadb]=1;
    Memory.RAM[0xaea]=uint8_t(mode); Memory.RAM[0xa50]=uint8_t(menuType);
    Memory.RAM[0x24]=change?2:1; Memory.RAM[0xb4c]=0;
    Registers.P.W=MemoryFlag|IndexFlag|IRQ; Registers.DB=4;
    Registers.D.W=0; Registers.S.W=0x1fff;
    Registers.A.W=0x1234;Registers.X.W=0x56;Registers.Y.W=0x78;
    ICPU.ShiftedDB=0x040000;S9xUnpackStatus();S9xFixCycles();
    S9xSetPCBase(0x04d222);CPU.Cycles=0;CPU.NextEvent=100000000;CPU.Flags=0;
    unsigned waits=0,steps=0;
    while(Registers.PBPC!=0x04cebf && ++steps<100000) {
        if(Registers.PBPC==0x049d35) {
            ++waits;
            // RTS from the existing wait; the routine preserves P and X/Y.
            auto low=S9xGetByte(++Registers.S.W);
            auto high=S9xGetByte(++Registers.S.W);
            S9xSetPCBase(0x040000+uint16_t((low|(high<<8))+1));
        } else {
            uint8_t op=S9xGetByte(Registers.PBPC);Registers.PCw++;
            ICPU.S9xOpcodes[op].S9xOpcode();
        }
    }
    if(steps>=100000 || Registers.S.W!=0x1ff7)
        throw std::runtime_error("Cursor routine result/stack mismatch");
    return waits;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    SDL_SetMainReady();SDL_Init(0);
    storage::setTestRoot(std::filesystem::absolute("build/input-verification/data"));
    try {
        Core core;core.load(std::filesystem::path(argv[1]).u8string());
        if(!core.inputTimingAvailable) throw std::runtime_error("Input profile mismatch");
        for(unsigned mode:{0u,0x40u,0x80u}) for(unsigned type:{3u,5u,8u})
            for(unsigned phase:{0u,0xffu}) for(bool change:{false,true}) {
                core.options.speed=1;core.configure();
                auto baseline=cursorWait(phase,mode,type,change);
                auto result=Memory.RAM[0xb4c],nextPhase=Memory.RAM[0xada],previous=Memory.RAM[0xadb];
                for(int speed:{2,3,4,8}) {
                    core.options.speed=speed;core.configure();
                    if(cursorWait(phase,mode,type,change)!=baseline*speed ||
                       Memory.RAM[0xb4c]!=result || Memory.RAM[0xada]!=nextPhase || Memory.RAM[0xadb]!=previous)
                        throw std::runtime_error("Cursor repeat is not scaled to real time");
                }
            }
        std::puts("PASS: cursor initial/repeat waits and direction changes at 1/2/3/4/8x (3 menu modes/types)");
    } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
    SDL_Quit();
}
