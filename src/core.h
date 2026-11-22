#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
struct Options {
    int speed = 1;
    int experience = 1;
    int gold = 1;
    bool swapAB = false, swapXY = false;
    bool recruit = false, drop = false, level = false, equipment = false, items = false;
};
class Core {
public:
    Core();
    ~Core();
    Core(const Core&)=delete;
    Core& operator=(const Core&)=delete;
    void load(const std::string& path);
    void run(bool render = true);
    void saveRam();
    void saveState(int slot);
    void loadState(int slot);
    void configure();
    bool loaded = false, supported = false, rewardsAvailable=false, gameOptionsAvailable=false;
    Options options;
    double fps = 60.0988, sampleRate = 32040;
    unsigned width = 256, height = 224;
    std::vector<uint32_t> pixels;
    std::string romPath, romName;
    uint16_t buttons = 0;
    bool renderVideo = true;
    bool inputTimingAvailable = false;
    std::function<void(const int16_t*,size_t)> audio;
    uint64_t audioFrames = 0, videoFrames = 0;
    int speedApplied = 1;
private:
    bool saveAllowed=false;
    int experienceApplied=-1,goldApplied=-1,gameOptionsApplied=-1;
    int inputSpeedApplied=-1;
    void applyCheats(bool force=false);
    uint32_t romCrc=0;
    std::vector<uint8_t> rom;
    std::string stateName(int slot) const;
};
