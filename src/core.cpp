#include "core.h"
#include "dq5_rewards.h"
#include "dq5_options.h"
#include "dq5_input.h"
#include "storage.h"
#include "libretro.h"
#include "snes9x.h"
#include "65c816.h"
#include "apu/apu.h"
#include "cheats.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include <cstdarg>
namespace {
Core* instance = nullptr;
retro_pixel_format pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
std::string directory;
void logger(retro_log_level, const char* format, ...) { va_list args; va_start(args,format); vfprintf(stderr,format,args); va_end(args); }
bool environment(unsigned command,void* data) {
    switch(command) {
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *static_cast<int*>(data) = instance->renderVideo ? 3 : 2; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: static_cast<retro_log_callback*>(data)->log = logger; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *static_cast<const char**>(data) = directory.c_str(); return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixelFormat = *static_cast<retro_pixel_format*>(data); return pixelFormat != RETRO_PIXEL_FORMAT_UNKNOWN;
    case RETRO_ENVIRONMENT_GET_VARIABLE: static_cast<retro_variable*>(data)->value = nullptr; return false;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *static_cast<bool*>(data) = false; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *static_cast<bool*>(data) = true; return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: return true;
    default: return false;
    }
}
void video(const void* data,unsigned w,unsigned h,size_t pitch) {
    if (!data || !instance->renderVideo) return;
    auto& c = *instance; c.width=w; c.height=h; c.pixels.resize(size_t(w)*h); ++c.videoFrames;
    for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
        const auto* row = static_cast<const uint8_t*>(data)+y*pitch;
        uint32_t value;
        if (pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) { std::memcpy(&value,row+x*4,4); value |= 0xff000000; }
        else { uint16_t p; std::memcpy(&p,row+x*2,2);
            auto r = pixelFormat==RETRO_PIXEL_FORMAT_RGB565 ? (p>>11)&31 : (p>>10)&31;
            auto g = pixelFormat==RETRO_PIXEL_FORMAT_RGB565 ? (p>>5)&63 : (p>>5)&31;
            auto b=p&31; value=0xff000000 | ((r*255/31)<<16) | ((g*255/(pixelFormat==RETRO_PIXEL_FORMAT_RGB565?63:31))<<8) | (b*255/31); }
        c.pixels[y*w+x]=value;
    }
}
size_t audioBatch(const int16_t* data,size_t frames) { instance->audioFrames+=frames; if(instance->audio) instance->audio(data,frames); return frames; }
void audioSample(int16_t left,int16_t right) { int16_t samples[]{left,right}; audioBatch(samples,1); }
void poll() {}
int16_t input(unsigned port,unsigned device,unsigned,unsigned id) {
    if(port || device!=RETRO_DEVICE_JOYPAD || id>15) return 0;
    return (instance->buttons>>id)&1;
}
uint32_t crc32(const uint8_t* data,size_t size) {
    uint32_t crc=~0u; for(size_t i=0;i<size;++i) { crc^=data[i]; for(int b=0;b<8;++b) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1))); } return ~crc;
}
}
Core::Core() {
    if(instance) throw std::runtime_error("Only one core may be used per process");
    instance=this; directory=storage::root().u8string();
    retro_set_environment(environment); retro_set_video_refresh(video); retro_set_audio_sample(audioSample);
    retro_set_audio_sample_batch(audioBatch); retro_set_input_poll(poll); retro_set_input_state(input); retro_init();
}
Core::~Core() { if(loaded) { try { saveRam(); } catch(...) {} retro_cheat_reset(); retro_unload_game(); } retro_deinit(); instance=nullptr; }
void Core::load(const std::string& path) {
    auto next=storage::readRom(path);
    if(loaded) { saveRam(); retro_cheat_reset(); retro_unload_game(); loaded=false; }
    saveAllowed=false;
    rom=std::move(next); romPath=path; romName=std::filesystem::u8path(path).stem().u8string();
    size_t header=rom.size()%0x8000==512 ? 512:0;
    romCrc=crc32(rom.data()+header,rom.size()-header);
    supported=romCrc==0xbc955f3b;
    rewardsAvailable=supported && dq5::rewardProfileMatches(rom.data()+header,rom.size()-header);
    gameOptionsAvailable=supported && dq5::optionProfileMatches(rom.data()+header,rom.size()-header);
    inputTimingAvailable=supported && dq5::inputProfileMatches(rom.data()+header,rom.size()-header);
    inputSpeedApplied=-1;
    experienceApplied=goldApplied=gameOptionsApplied=-1;
    retro_game_info info{romPath.c_str(),rom.data(),rom.size(),nullptr};
    if(!retro_load_game(&info)) throw std::runtime_error("Snes9x rejected this ROM");
    loaded=true; retro_set_controller_port_device(0,RETRO_DEVICE_JOYPAD);
    retro_system_av_info av{}; retro_get_system_av_info(&av); fps=av.timing.fps; sampleRate=av.timing.sample_rate;
    auto ram=storage::read(romName+".srm"); auto size=retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    auto identity=storage::read(romName+".srm.meta");
    uint8_t expected[4]; for(int i=0;i<4;++i) expected[i]=uint8_t(romCrc>>(8*i));
    if(!identity.empty() && (identity.size()!=4 || std::memcmp(identity.data(),expected,4))) throw std::runtime_error("This save belongs to a different ROM with the same name");
    if(!ram.empty()) {
        if(ram.size()!=size) throw std::runtime_error("Save RAM size mismatch; existing save was not loaded");
        std::memcpy(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM),ram.data(),size);
    }
    saveAllowed=true;
    configure();
}
void Core::configure() {
    if(!loaded) return;
    // Existing Snes9x timing control; compensate its resampler so the SPC runs in wall time.
    int speed=options.speed;
    if(speed!=1 && speed!=2 && speed!=3 && speed!=4 && speed!=8) speed=1;
    S9xAPUTimingSetSpeedup(256-256*speed);
    Settings.SoundInputRate=32040*speed;
    S9xUpdateDynamicRate(1,2);
    speedApplied=speed;
    applyCheats();
}
void Core::applyCheats(bool force) {
    int experience=rewardsAvailable && dq5::validMultiplier(options.experience)?options.experience:1;
    int gold=rewardsAvailable && dq5::validMultiplier(options.gold)?options.gold:1;
    unsigned flags=gameOptionsAvailable?((options.recruit?dq5::Recruit:0u)|(options.drop?dq5::Drop:0u)|(options.equipment?dq5::Equipment:0u)|(options.items?dq5::Items:0u)|(options.level?dq5::Level:0u)):0u;
    if(experience==experienceApplied && gold==goldApplied && int(flags)==gameOptionsApplied && inputSpeedApplied==speedApplied) return;
    // A snapshot can stop between any two instructions. Wait until the patched routine has returned.
    if(!force && dq5::insideRewardCode(Registers.PBPC)) return;
    retro_cheat_reset();
    S9xCheatsEnable();
    unsigned index=0;
    auto patches=dq5::rewardPatches(experience,gold);
    if(gameOptionsAvailable) dq5::appendOptionPatches(patches,flags);
    if(inputTimingAvailable) dq5::appendInputPatches(patches,speedApplied);
    for(auto [address,value]:patches) {
        char code[9]; std::snprintf(code,sizeof(code),"%06X%02X",unsigned(address),unsigned(value));
        retro_cheat_set(index++,true,code);
    }
    experienceApplied=experience;goldApplied=gold;gameOptionsApplied=int(flags);
    inputSpeedApplied=speedApplied;
}
void Core::run(bool render) { if(loaded) { renderVideo=render; applyCheats(); retro_run(); } }
void Core::saveRam() {
    if(!loaded || !saveAllowed) return;
    auto size=retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if(size) {
        storage::write(romName+".srm",retro_get_memory_data(RETRO_MEMORY_SAVE_RAM),size);
        uint8_t identity[4]; for(int i=0;i<4;++i) identity[i]=uint8_t(romCrc>>(8*i));
        storage::write(romName+".srm.meta",identity,4);
    }
}
std::string Core::stateName(int slot) const {
    if(!loaded || slot<1 || slot>5) throw std::runtime_error("Invalid state slot");
    return romName+".slot"+std::to_string(slot)+".state";
}
void Core::saveState(int slot) {
    auto name=stateName(slot); std::vector<uint8_t> data(retro_serialize_size());
    if(data.empty() || !retro_serialize(data.data(),data.size())) throw std::runtime_error("State save failed");
    std::vector<uint8_t> wrapped{'S','D','Q','5','S','T','0','3'};
    auto append=[&](uint32_t n) { for(int i=0;i<4;++i) wrapped.push_back(uint8_t(n>>(8*i))); };
    append(romCrc); append(crc32(data.data(),data.size())); append(experienceApplied<1?1:experienceApplied); append(goldApplied<1?1:goldApplied); append(gameOptionsApplied<0?0:gameOptionsApplied); wrapped.insert(wrapped.end(),data.begin(),data.end());
    storage::write(name,wrapped.data(),wrapped.size()); saveRam();
}
void Core::loadState(int slot) {
    auto data=storage::read(stateName(slot));
    bool legacy=data.size()==retro_serialize_size()+16 && !std::memcmp(data.data(),"SDQ5ST01",8);
    bool version2=data.size()==retro_serialize_size()+24 && !std::memcmp(data.data(),"SDQ5ST02",8);
    bool current=data.size()==retro_serialize_size()+28 && !std::memcmp(data.data(),"SDQ5ST03",8);
    if(!legacy && !version2 && !current) throw std::runtime_error("State not found or incompatible");
    size_t header=legacy?16:version2?24:28;
    auto number=[&](size_t offset) { uint32_t n=0; for(int i=0;i<4;++i) n|=uint32_t(data[offset+i])<<(8*i); return n; };
    if(number(8)!=romCrc || number(12)!=crc32(data.data()+header,data.size()-header)) throw std::runtime_error("State belongs to another ROM or is damaged");
    int experience=legacy?1:int(number(16)),gold=legacy?1:int(number(20));
    if(!dq5::validMultiplier(experience) || !dq5::validMultiplier(gold)) throw std::runtime_error("Invalid reward settings in state");
    unsigned flags=current?number(24):0;
    if(flags&~unsigned(dq5::AllOptions)) throw std::runtime_error("Invalid game options in state");
    data.erase(data.begin(),data.begin()+header);
    // Restore the current state if an invalid snapshot partially changes core memory.
    std::vector<uint8_t> before(retro_serialize_size());
    if(!retro_serialize(before.data(),before.size())) throw std::runtime_error("Could not back up current state");
    if(!retro_unserialize(data.data(),data.size())) { retro_unserialize(before.data(),before.size()); throw std::runtime_error("State load failed"); }
    options.experience=experience;options.gold=gold;
    options.recruit=(flags&dq5::Recruit)!=0; options.drop=(flags&dq5::Drop)!=0;options.equipment=(flags&dq5::Equipment)!=0;
    options.items=(flags&dq5::Items)!=0;
    options.level=(flags&dq5::Level)!=0;
    applyCheats(true);
    configure();
}
