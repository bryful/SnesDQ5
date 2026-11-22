#include "core.h"
#include "dq5_rewards.h"
#include "storage.h"
#include "licenses.h"
#include "touch.h"
#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <libretro.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
namespace {
struct Settings {
    Options options;
    bool touch=false;
    std::string lastRom;
    void load() {
        auto bytes=storage::read("settings.ini");
        std::istringstream in(std::string(bytes.begin(),bytes.end())); std::string key;
        while(in>>key) {
            if(key=="rom") in>>std::quoted(lastRom);
            else if(key=="speed") in>>options.speed;
            else if(key=="experience") in>>options.experience;
            else if(key=="gold") in>>options.gold;
            else if(key=="recruit") in>>options.recruit;
            else if(key=="items") in>>options.items;
            else if(key=="level") in>>options.level;
            else if(key=="drop") in>>options.drop;
            else if(key=="equipment") in>>options.equipment;
            else if(key=="swapAB") in>>options.swapAB;
            else if(key=="swapXY") in>>options.swapXY;
            else if(key=="touch") in>>touch;
            else { std::string ignored; std::getline(in,ignored); }
        }
        if(options.speed!=1 && options.speed!=2 && options.speed!=3 && options.speed!=4 && options.speed!=8) options.speed=1;
        if(!dq5::validMultiplier(options.experience)) options.experience=1;
        if(!dq5::validMultiplier(options.gold)) options.gold=1;
    }
    void save() {
        std::ostringstream out; out<<"rom "<<std::quoted(lastRom)<<"\nspeed "<<options.speed<<"\nswapAB "<<options.swapAB<<"\nswapXY "<<options.swapXY<<"\n";
        out<<"touch "<<touch<<"\nexperience "<<options.experience<<"\ngold "<<options.gold<<"\n";
        out<<"recruit "<<options.recruit<<"\ndrop "<<options.drop<<"\nequipment "<<options.equipment<<"\n";
        out<<"items "<<options.items<<"\nlevel "<<options.level<<"\n";
        auto text=out.str(); storage::write("settings.ini",text.data(),text.size());
    }
};
std::vector<TouchButton> touchButtons;
struct Finger { SDL_FPoint point; bool sampled=false; };
std::map<SDL_FingerID,Finger> fingers;
uint16_t touchPulse=0;
void scrollTouchWindow(float scale) {
    auto& io=ImGui::GetIO();
    if(io.MouseSource==ImGuiMouseSource_TouchScreen &&
       ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
       ImGui::IsMouseDragging(ImGuiMouseButton_Left,8*scale) &&
       ImGui::GetScrollMaxY()>0 &&
       io.MousePos.x<ImGui::GetWindowPos().x+ImGui::GetWindowWidth()-ImGui::GetStyle().ScrollbarSize) {
        ImGui::ClearActiveID();
        ImGui::SetScrollY(std::clamp(ImGui::GetScrollY()-io.MouseDelta.y,0.f,ImGui::GetScrollMaxY()));
    }
}
uint16_t readButtons(SDL_GameController* pad, const Options& options, int w, int h) {
    const auto* key=SDL_GetKeyboardState(nullptr); uint16_t bits=0;
    auto set=[&](unsigned id,bool on) { if(on) bits|=uint16_t(1u<<id); };
    set(0,key[SDL_SCANCODE_Z]); set(1,key[SDL_SCANCODE_A]); set(2,key[SDL_SCANCODE_RSHIFT]); set(3,key[SDL_SCANCODE_RETURN]);
    set(4,key[SDL_SCANCODE_UP]); set(5,key[SDL_SCANCODE_DOWN]); set(6,key[SDL_SCANCODE_LEFT]); set(7,key[SDL_SCANCODE_RIGHT]);
    set(8,key[SDL_SCANCODE_X]); set(9,key[SDL_SCANCODE_S]); set(10,key[SDL_SCANCODE_Q]); set(11,key[SDL_SCANCODE_W]);
    const std::array<SDL_GameControllerButton,12> map={SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_X,SDL_CONTROLLER_BUTTON_BACK,SDL_CONTROLLER_BUTTON_START,SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_CONTROLLER_BUTTON_DPAD_DOWN,SDL_CONTROLLER_BUTTON_DPAD_LEFT,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,SDL_CONTROLLER_BUTTON_B,SDL_CONTROLLER_BUTTON_Y,SDL_CONTROLLER_BUTTON_LEFTSHOULDER,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
    if(pad) {
        for(unsigned id=0;id<map.size();++id) set(id,SDL_GameControllerGetButton(pad,map[id])!=0);
        auto x=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX), y=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY);
        set(4,y < -12000); set(5,y>12000); set(6,x < -12000); set(7,x>12000);
    }
    bits |= touchPulse; touchPulse=0;
    for(auto& finger:fingers) {
        finger.second.sampled=true;
        for(auto& button:touchButtons) {
            SDL_FPoint point{finger.second.point.x*w,finger.second.point.y*h}; if(touchContains(button,point)) set(button.id,true);
        }
    }
    auto swap=[&](unsigned a,unsigned b) { bool aa=(bits>>a)&1,bb=(bits>>b)&1; bits &= ~(uint16_t((1u<<a)|(1u<<b))); set(a,bb); set(b,aa); };
    if(options.swapAB) swap(0,8); if(options.swapXY) swap(1,9); return bits;
}
void drawTouch(const TouchLayout& layout,int w,int h) {
    touchButtons=layout.buttons;
    auto* draw=ImGui::GetForegroundDrawList();
    for(const auto& button:touchButtons) {
        auto r=button.rect; bool pressed=(touchPulse&(1u<<button.id))!=0;
        for(const auto& finger:fingers) if(touchContains(button,{finger.second.point.x*w,finger.second.point.y*h})) pressed=true;
        ImU32 color=pressed?IM_COL32(100,150,215,245):IM_COL32(42,56,80,210);
        ImVec2 center{r.x+r.w/2,r.y+r.h/2};
        if(button.circle) { draw->AddCircleFilled(center,r.w*.48f,color); draw->AddCircle(center,r.w*.48f,IM_COL32(130,150,180,255),0,2); }
        else { draw->AddRectFilled({r.x+2,r.y+2},{r.x+r.w-2,r.y+r.h-2},color,8); draw->AddRect({r.x+2,r.y+2},{r.x+r.w-2,r.y+r.h-2},IM_COL32(100,120,150,255),8); }
        if(button.id>=4 && button.id<=7) {
            ImVec2 direction=button.id==4?ImVec2{0,-1}:button.id==5?ImVec2{0,1}:button.id==6?ImVec2{-1,0}:ImVec2{1,0};
            float a=r.w*.22f; draw->AddTriangleFilled({center.x+direction.x*a,center.y+direction.y*a},
                {center.x-direction.x*a-direction.y*a,center.y-direction.y*a+direction.x*a},
                {center.x-direction.x*a+direction.y*a,center.y-direction.y*a-direction.x*a},IM_COL32_WHITE);
        } else { auto size=ImGui::CalcTextSize(button.label); draw->AddText({center.x-size.x/2,center.y-size.y/2},IM_COL32_WHITE,button.label); }
    }
}
}

int main(int argc,char** argv) {
    bool smoke=false; int smokeSpeed=1; int smokeFrames=600; std::string romArgument;
    for(int i=1;i<argc;++i) { std::string arg=argv[i]; if(arg=="--smoke") smoke=true; else if(arg=="--speed" && i+1<argc) smokeSpeed=std::stoi(argv[++i]); else if(arg=="--frames" && i+1<argc) smokeFrames=std::stoi(argv[++i]); else romArgument=arg; }
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH,"1");
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON,"101");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL,"101");
#endif
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS,"0");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"nearest");
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)!=0) return 1;
    int result=0;
    try {
        if(smoke) storage::setTestRoot(fs::absolute("build/verification/data"));
        Settings settings; if(!smoke) settings.load();

        if(smoke) { if(smokeSpeed!=1 && smokeSpeed!=2 && smokeSpeed!=3 && smokeSpeed!=4 && smokeSpeed!=8) throw std::runtime_error("Invalid speed"); settings.options.speed=smokeSpeed; }
        Uint32 platformFlags=0;
#ifdef __ANDROID__
        platformFlags=0;
#endif
        SDL_Window* window=SDL_CreateWindow("SnesDQ5",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1100,780,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|platformFlags|(smoke?SDL_WINDOW_HIDDEN:0));
        if(!window) throw std::runtime_error(SDL_GetError());
        SDL_Renderer* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
        if(!renderer) renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        if(!renderer) throw std::runtime_error(SDL_GetError());
        ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigNavCursorVisibleAlways=true;
        io.ConfigWindowsMoveFromTitleBarOnly=true;
        float uiScale=1.f;
#ifdef __ANDROID__
        float dpi=0; if(SDL_GetDisplayDPI(0,&dpi,nullptr,nullptr)==0 && dpi>0) uiScale=std::clamp(dpi/160.f,1.f,3.f); else uiScale=2.f;
        ImFontConfig config; config.SizePixels=18.f*uiScale; io.Fonts->AddFontDefault(&config);
#endif
        const char* font=nullptr;
#ifndef __ANDROID__
        if(fs::exists("C:/Windows/Fonts/meiryo.ttc")) font="C:/Windows/Fonts/meiryo.ttc";
#endif
        if(font) io.Fonts->AddFontFromFileTTF(font,18.f,nullptr,io.Fonts->GetGlyphRangesJapanese());
        ImGui::StyleColorsDark(); ImGui::GetStyle().FrameRounding=6; ImGui::GetStyle().FramePadding={12,10}; ImGui::GetStyle().ScaleAllSizes(uiScale);
        ImGui::GetStyle().ScrollbarSize=28*uiScale;
        ImGui::GetStyle().GrabMinSize=32*uiScale;
        ImGui_ImplSDL2_InitForSDLRenderer(window,renderer); ImGui_ImplSDLRenderer2_Init(renderer);
        Core core; core.options=settings.options;
        SDL_AudioDeviceID audio=0;
        bool audioPriming=true;
        auto resetAudio=[&]() {
            if(audio) { SDL_PauseAudioDevice(audio,1); SDL_ClearQueuedAudio(audio); }
            audioPriming=true;
        };
        auto openAudio=[&]() {
            if(audio) SDL_CloseAudioDevice(audio);
            SDL_AudioSpec wanted{}; wanted.freq=int(core.sampleRate); wanted.format=AUDIO_S16SYS; wanted.channels=2; wanted.samples=1024;
            audio=SDL_OpenAudioDevice(nullptr,0,&wanted,nullptr,0);
            if(!audio) throw std::runtime_error(SDL_GetError()); audioPriming=true;
        };
        core.audio=[&](const int16_t* samples,size_t frames) {
            if(!audio || !frames) return;
            auto queued=SDL_GetQueuedAudioSize(audio);
            if(!queued && !audioPriming) { SDL_PauseAudioDevice(audio,1); audioPriming=true; }
            if(queued<unsigned(core.sampleRate*.2*4)) SDL_QueueAudio(audio,samples,Uint32(frames*4));
            // Refill after an underrun before restarting the device, rather than
            // alternating a short batch with silence on every display frame.
            if(audioPriming && SDL_GetQueuedAudioSize(audio)>=unsigned(core.sampleRate*.06*4)) {
                audioPriming=false; SDL_PauseAudioDevice(audio,0);
            }
        };
        std::string message; bool menu=true, running=true, pause=false, browser=false, touch=settings.touch, focused=true;
        bool requestMenuFocus=true, blockGameInput=true;
        int menuPage=3;
        auto showMenu=[&](bool open) {
            menu=open || !core.loaded;
            requestMenuFocus=menu; blockGameInput=true;
            core.buttons=0; fingers.clear(); touchPulse=0;
            resetAudio();
        };
        fs::path directory=storage::root()/"rom";
        if(fs::exists(".rom")) directory=fs::absolute(".rom");
        auto load=[&](const std::string& path) {
            core.load(path); openAudio(); settings.lastRom=path; settings.options=core.options; menuPage=0;
            if(!smoke) settings.save(); showMenu(false); browser=false; message="ROM loaded";
        };
        if(!romArgument.empty()) load(romArgument);
        else if(!settings.lastRom.empty()) { try { load(settings.lastRom); } catch(const std::exception& e) { message=e.what(); } }
        SDL_GameController* controller=nullptr;
        auto connect=[&]() { if(controller) return; for(int i=0;i<SDL_NumJoysticks();++i) if(SDL_IsGameController(i)) { controller=SDL_GameControllerOpen(i); if(controller) break; } };
        connect(); SDL_Texture* texture=nullptr; unsigned texW=0,texH=0;
        bool firstPresented=false;
        int frame=0; auto next=std::chrono::steady_clock::now(); uint64_t lastSave=SDL_GetTicks64(); bool checkpointVerified=false; bool firstPresent=true;
        auto attempt=[&](auto action) { try { action(); } catch(const std::exception& e) { message=e.what(); SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"%s",e.what()); } };
        std::string stateError;
        Uint64 stateNoticeUntil=0;
        auto stateAction=[&](int slot,bool restore) {
            try {
                if(restore) core.loadState(slot); else core.saveState(slot);
                stateError.clear(); message=restore?"Loaded":"Saved";
                pause=false; showMenu(false); stateNoticeUntil=SDL_GetTicks64()+1800;
                SDL_Log("State slot %d %s; returned to game",slot,restore?"loaded":"saved");
            } catch(const std::exception& e) {
                stateError=e.what(); message=stateError; showMenu(true);
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"State action failed: %s",e.what());
            }
        };
        while(running) {
            SDL_Event event;
            while(SDL_PollEvent(&event)) {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if(event.type==SDL_QUIT) running=false;
                if(event.type==SDL_KEYDOWN && !event.key.repeat && (event.key.keysym.sym==SDLK_ESCAPE || event.key.keysym.sym==SDLK_AC_BACK)) {
                    if(menu && browser) { browser=false; requestMenuFocus=true; } else showMenu(!menu);
                }
                if(event.type==SDL_DROPFILE) { std::string path=event.drop.file; SDL_free(event.drop.file); attempt([&]{load(path);}); }
                if(event.type==SDL_CONTROLLERDEVICEADDED) connect();
                if(event.type==SDL_CONTROLLERDEVICEREMOVED && controller && !SDL_GameControllerGetAttached(controller)) { SDL_GameControllerClose(controller); controller=nullptr; connect(); }
                if(event.type==SDL_CONTROLLERBUTTONDOWN && controller &&
                   event.cbutton.which==SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) &&
                   (event.cbutton.button==SDL_CONTROLLER_BUTTON_GUIDE || event.cbutton.button==SDL_CONTROLLER_BUTTON_LEFTSTICK)) showMenu(!menu);
                if(event.type==SDL_FINGERDOWN && !menu && core.loaded && focused) {
                    int fw,fh; SDL_GetWindowSize(window,&fw,&fh);
                    for(const auto& button:touchButtons) if(touchContains(button,{event.tfinger.x*fw,event.tfinger.y*fh})) {
                        fingers[event.tfinger.fingerId]={{event.tfinger.x,event.tfinger.y},false}; break;
                    }
                }
                if(event.type==SDL_FINGERMOTION) { auto it=fingers.find(event.tfinger.fingerId); if(it!=fingers.end()) it->second.point={event.tfinger.x,event.tfinger.y}; }
                if(event.type==SDL_FINGERUP) {
                    auto it=fingers.find(event.tfinger.fingerId);
                    if(it!=fingers.end() && !it->second.sampled) {
                        int fw,fh; SDL_GetWindowSize(window,&fw,&fh);
                        for(const auto& button:touchButtons) if(touchContains(button,{event.tfinger.x*fw,event.tfinger.y*fh})) touchPulse|=uint16_t(1u<<button.id);
                    }
                    fingers.erase(event.tfinger.fingerId);
                }
                if(event.type==SDL_WINDOWEVENT && (event.window.event==SDL_WINDOWEVENT_FOCUS_LOST || event.window.event==SDL_WINDOWEVENT_MINIMIZED)) { focused=false; fingers.clear(); touchPulse=0; resetAudio(); attempt([&]{core.saveRam();}); }
                if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED) focused=true;
                if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED) { fingers.clear(); touchPulse=0; blockGameInput=true; }
                if(event.type==SDL_APP_WILLENTERBACKGROUND) { focused=false; fingers.clear(); touchPulse=0; blockGameInput=true; attempt([&]{core.saveRam();}); }
                if(event.type==SDL_APP_DIDENTERFOREGROUND) focused=true;
            }
            attempt([&]{auto selected=storage::pickedRom(); if(!selected.empty()) load(selected);});
            int w,h; SDL_GetWindowSize(window,&w,&h);
            auto inset=storage::safeInsets();
            SDL_FRect safe{float(inset[0]),float(inset[1]),std::max(1.f,float(w-inset[0]-inset[2])),std::max(1.f,float(h-inset[1]-inset[3]))};
            bool touchVisible=touch;
#ifdef __ANDROID__
            touchVisible=touch || !controller;
#endif
            static bool previousTouchVisible=false;
            if(touchVisible!=previousTouchVisible) { fingers.clear(); touchPulse=0; blockGameInput=true; previousTouchVisible=touchVisible; }
            auto touchLayout=makeTouchLayout(w,h,uiScale,touchVisible && !menu && core.loaded,core.options.swapAB,core.options.swapXY,safe);
            const int navFlags=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
            if(menu || !core.loaded) io.ConfigFlags|=navFlags; else io.ConfigFlags&=~navFlags;
            ImGui_ImplSDLRenderer2_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
            if(menu && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight,false)) {
                if(browser) { browser=false; requestMenuFocus=true; }
                else if(core.loaded) showMenu(false);
                else requestMenuFocus=true;
            }
            if(menu && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)) {
                int direction=int(ImGui::IsKeyPressed(ImGuiKey_GamepadR1,false))-int(ImGui::IsKeyPressed(ImGuiKey_GamepadL1,false));
                if(direction) { menuPage=(menuPage+direction+4)%4; browser=false; requestMenuFocus=true; }
            }
            if(menu || !core.loaded) {
                float top=safe.y+8*uiScale;
                ImGuiCond placement=ImGuiCond_Always;
                ImGui::SetNextWindowPos({safe.x+8*uiScale,top},placement); ImGui::SetNextWindowSize({std::min(620.f*uiScale,safe.w-16*uiScale),std::min(720.f*uiScale,safe.h-16*uiScale)},placement);
                bool focusMenu=requestMenuFocus;
                if(focusMenu) ImGui::SetNextWindowFocus();
                bool menuOpen=true;
                ImGui::Begin("SnesDQ5",core.loaded?&menuOpen:nullptr,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
                if(!menuOpen) { pause=false; showMenu(false); }
                if(focusMenu) ImGui::SetKeyboardFocusHere();
                ImGui::BeginDisabled(!core.loaded);
                if(ImGui::Button("Back to game",{ImGui::GetContentRegionAvail().x,40*uiScale})) { pause=false; showMenu(false); }
                ImGui::EndDisabled();
                const char* pages[]{"Play","Saves","Options","Files"};
                float pageWidth=(ImGui::GetContentRegionAvail().x-3*ImGui::GetStyle().ItemSpacing.x)/4;
                for(int page=0;page<4;++page) {
                    if(page) ImGui::SameLine();
                    if(page==menuPage) ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    bool selected=ImGui::Button(pages[page],{pageWidth,40*uiScale});
                    if(page==menuPage) ImGui::PopStyleColor();
                    if(selected) { menuPage=page; browser=false; }
                }
                ImGui::Separator();
                ImGui::PushID(menuPage);
                ImGui::BeginChild("menu-content",{0,0},ImGuiChildFlags_NavFlattened,ImGuiWindowFlags_NoMove);
                // Dear ImGui provides scrolling and drag detection; cancel the
                // pressed widget once a touch becomes a scrolling gesture.
                scrollTouchWindow(uiScale);
                if(!stateError.empty()) ImGui::TextWrapped("%s",stateError.c_str());
                if(menuPage==3) {
                ImGui::TextWrapped("%s",core.loaded ? fs::u8path(core.romPath).filename().u8string().c_str():"Open your own ROM to start");
                if(ImGui::Button("Open ROM")) {
#ifdef __ANDROID__
                    storage::pickRom();
#else
                    browser=!browser;
#endif
                }
#ifdef __ANDROID__
                if(ImGui::Button("Documents / SnesDQ5")) storage::pickDirectory();
                ImGui::TextWrapped("Choose Documents once to create SnesDQ5/rom and store settings and saves there.");

#endif
                if(browser) {
                    ImGui::TextWrapped("%s",directory.u8string().c_str());
                    if(ImGui::Button("Parent directory") && directory.has_parent_path()) directory=directory.parent_path();
                    ImGui::BeginChild("files",{0,180*uiScale},true);
                    scrollTouchWindow(uiScale);
                    std::error_code ec; std::vector<fs::directory_entry> entries;
                    for(fs::directory_iterator it(directory,ec),end;!ec && it!=end;it.increment(ec)) entries.push_back(*it);
                    std::sort(entries.begin(),entries.end(),[](auto& a,auto& b) {return a.path()<b.path();});
                    for(auto& entry:entries) {
                        auto ext=entry.path().extension().u8string(); std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
                        bool dir=entry.is_directory(ec);
                        if(!dir && ext!=".sfc" && ext!=".smc") continue;
                        std::string name=(dir?"[DIR] ":"")+entry.path().filename().u8string();
                        if(ImGui::Selectable(name.c_str(),false,0,{0,40*uiScale})) { if(dir) { directory=entry.path(); break; } else attempt([&]{load(entry.path().u8string());}); }
                    }
                    ImGui::EndChild();
                }
                }
                if(menuPage==0) {
                ImGui::SeparatorText("Speed");
                bool changed=false;
                for(int speed:{1,2,3,4,8}) { if(speed!=1) ImGui::SameLine(); changed |= ImGui::RadioButton((std::to_string(speed)+"x").c_str(),&core.options.speed,speed); }
                ImGui::SeparatorText("Controls");
                changed |= ImGui::Checkbox("Swap A / B",&core.options.swapAB);
                changed |= ImGui::Checkbox("Swap X / Y",&core.options.swapXY);
                changed |= ImGui::Checkbox("Show touch with pad",&touch);
                if(changed) attempt([&]{core.configure(); settings.options=core.options; settings.touch=touch; settings.save(); resetAudio();});
                }
                if(menuPage==1) {
                ImGui::SeparatorText("Save states");
                ImGui::BeginDisabled(!core.loaded);
                for(int slot=1;slot<=5;++slot) {
                    ImGui::PushID(slot); ImGui::Text("Slot %d",slot); ImGui::SameLine();
                    if(ImGui::Button("Save",{90*uiScale,40*uiScale})) stateAction(slot,false);
                    ImGui::SameLine(); if(ImGui::Button("Load",{90*uiScale,40*uiScale})) stateAction(slot,true);
                    ImGui::PopID();
                }
                ImGui::EndDisabled();
                }
                if(menuPage==2) {
                ImGui::SeparatorText("DQ5 options");
                bool optionColumns=ImGui::GetContentRegionAvail().x>=480*uiScale;
                if(optionColumns) ImGui::BeginTable("option-columns",2,ImGuiTableFlags_SizingStretchSame);
                ImGui::BeginDisabled(!core.rewardsAvailable);
                const int multipliers[]{1,4,8,16,32,64,128};
                const char* labels[]{"1x","4x","8x","16x","32x","64x","128x"};
                auto multiplier=[&](const char* label,int& value) {
                    if(optionColumns) ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(std::max(65*uiScale,ImGui::GetContentRegionAvail().x-ImGui::CalcTextSize(label).x-8*uiScale));
                    int selected=0; for(int n=0;n<7;++n) if(multipliers[n]==value) selected=n;
                    bool edited=ImGui::Combo(label,&selected,labels,7); if(edited) value=multipliers[selected]; return edited;
                };
                bool rewardChanged=multiplier("Experience",core.options.experience);
                rewardChanged |= multiplier("Gold",core.options.gold);
                ImGui::EndDisabled();
                if(rewardChanged) attempt([&]{core.configure(); settings.options=core.options; settings.save();});
                if(!core.rewardsAvailable) ImGui::TextWrapped("Reward multipliers require the verified Japanese v1.0 ROM.");
                ImGui::BeginDisabled(!core.gameOptionsAvailable);
                if(optionColumns) ImGui::TableNextColumn();
                bool optionChanged=ImGui::Checkbox("Recruit monsters",&core.options.recruit);
                if(optionColumns) ImGui::TableNextColumn();
                optionChanged |= ImGui::Checkbox("Guaranteed drops",&core.options.drop);
                if(optionColumns) ImGui::TableNextColumn();
                optionChanged |= ImGui::Checkbox("Any equipment",&core.options.equipment);
                if(optionColumns) ImGui::TableNextColumn();
                optionChanged |= ImGui::Checkbox("Keep used items",&core.options.items);
                if(optionColumns) ImGui::TableNextColumn();
                optionChanged |= ImGui::Checkbox("Monster level: 99",&core.options.level);
                ImGui::EndDisabled();
                if(optionColumns) ImGui::EndTable();
                if(optionChanged) attempt([&]{core.configure();settings.options=core.options;settings.save();});
                }
                if(ImGui::CollapsingHeader("Help")) {
                ImGui::TextWrapped("D-pad / stick: move | Bottom button: OK | Right button: Back | L / R: page | L3: menu");
                ImGui::TextWrapped("Android Back / title-bar close: return to game. Swipe content to scroll.");
                ImGui::TextWrapped("SPC audio at 1x is experimental.");
                ImGui::TextWrapped("Recruitment: normal story/party/level eligibility still applies. Drops: mixed-species battles remain excluded by the game.");
                ImGui::TextWrapped("Keep items: ordinary field/battle use only. Breakage, automatic activation and story events are not covered.");
                ImGui::TextWrapped("Monster cap: existing allies can grow to 99. Turning OFF keeps earned levels/stats and restores the normal cap.");
                ImGui::Separator(); ImGui::TextWrapped("Keyboard: arrows, Z=B X=A A=Y S=X, Q=L W=R, Enter=Start, right Shift=Select, Esc=menu. Drop a ROM file to open it.");
                }
                if(!message.empty()) ImGui::TextWrapped("%s",message.c_str());
                if(ImGui::CollapsingHeader("Licenses")) {
                    ImGui::TextWrapped("Snes9x: non-commercial only. SDL2: zlib. Dear ImGui: MIT. ROMs are not included.");
                    ImGui::BeginChild("notices",{0,240},true,ImGuiWindowFlags_HorizontalScrollbar);
                    scrollTouchWindow(uiScale);
                    ImGui::TextUnformatted(ThirdPartyNotices); ImGui::EndChild();
                }
                ImGui::EndChild();
                ImGui::PopID();
                // Tapping the visible game outside the panel also resumes play.
                auto panelPos=ImGui::GetWindowPos(),panelSize=ImGui::GetWindowSize();
                bool outside=io.MousePos.x<panelPos.x || io.MousePos.y<panelPos.y ||
                    io.MousePos.x>=panelPos.x+panelSize.x || io.MousePos.y>=panelPos.y+panelSize.y;
                if(core.loaded && !focusMenu && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                   outside &&
                   !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)) { pause=false; showMenu(false); }
                requestMenuFocus=false;
                ImGui::End();
            } else {
                const auto& toolbar=touchLayout.toolbar;
                bool compactToolbar=safe.w>safe.h;
                if(compactToolbar) ImGui::PushFont(nullptr,14*uiScale);
                ImGui::SetNextWindowPos({toolbar.x,toolbar.y}); ImGui::SetNextWindowSize({toolbar.w,toolbar.h});
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
                ImGui::Begin("HUD",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings);
                float buttonWidth=std::min((compactToolbar?72:100)*uiScale,(toolbar.w-60*uiScale-3*ImGui::GetStyle().ItemSpacing.x)/3);
                if(ImGui::Button("Menu",{buttonWidth,toolbar.h})) showMenu(true);
                ImGui::SameLine(); if(ImGui::Button("Save1",{buttonWidth,toolbar.h})) stateAction(1,false);
                ImGui::SameLine(); if(ImGui::Button("Load1",{buttonWidth,toolbar.h})) stateAction(1,true);
                ImGui::SameLine();
                if(SDL_GetTicks64()<stateNoticeUntil) ImGui::TextUnformatted(message.c_str());
                else ImGui::Text("%dx",core.options.speed);
                ImGui::End(); ImGui::PopStyleVar();
                if(compactToolbar) ImGui::PopFont();
            }
            drawTouch(touchLayout,w,h);
            auto gameButtons=readButtons(controller,core.options,w,h);
            if(blockGameInput && gameButtons==0 && !menu) blockGameInput=false;
            core.buttons=(!menu && focused && !blockGameInput && !io.WantTextInput)?gameButtons:0;
            if(core.loaded && (!menu || smoke) && !pause && (focused || smoke)) {
                for(int i=0;i<core.options.speed;++i) { core.run(i==core.options.speed-1); ++frame; }
                if(smoke && frame>=120 && !checkpointVerified) {
                    auto* ram=static_cast<const uint8_t*>(retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
                    size_t size=retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM); std::vector<uint8_t> before(ram,ram+size);
                    core.saveState(1); core.run(); core.loadState(1);
                    if(std::memcmp(before.data(),retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),size)) throw std::runtime_error("State RAM roundtrip mismatch");
                    checkpointVerified=true;
                }
            }
            if(smoke && (!core.loaded || frame>=smokeFrames)) running=false;
            if(!smoke && core.loaded && SDL_GetTicks64()-lastSave>10000) { attempt([&]{core.saveRam();}); lastSave=SDL_GetTicks64(); }
            SDL_SetRenderDrawColor(renderer,8,12,20,255); SDL_RenderClear(renderer);
            if(!core.pixels.empty()) {
                if(texW!=core.width || texH!=core.height) { if(texture) SDL_DestroyTexture(texture); texW=core.width;texH=core.height;texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,int(texW),int(texH)); }
                if(!texture) throw std::runtime_error(SDL_GetError());
                SDL_UpdateTexture(texture,nullptr,core.pixels.data(),int(texW*4));
                int rw,rh; SDL_GetRendererOutputSize(renderer,&rw,&rh);
                auto area=touchLayout.gameArea;
                float sx=float(rw)/w,sy=float(rh)/h;
                float dw=std::min(area.w,area.h*4/3),dh=dw*3/4;
                SDL_FRect target{(area.x+(area.w-dw)/2)*sx,(area.y+(area.h-dh)/2)*sy,dw*sx,dh*sy};
                SDL_RenderCopyF(renderer,texture,nullptr,&target);
            }
            ImGui::Render(); ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),renderer); SDL_RenderPresent(renderer);
            if(firstPresent) { SDL_Log("SnesDQ5: first frame rendered (%dx%d)",w,h); firstPresent=false; }
            if(!firstPresented) { SDL_Log("SnesDQ5: first frame presented (%dx%d)",w,h); firstPresented=true; }
            if(smoke) continue;
            next+=std::chrono::microseconds(core.loaded?int64_t(1000000/core.fps):16667);
            auto now=std::chrono::steady_clock::now(); if(next<now-std::chrono::milliseconds(100)) next=now;
            std::this_thread::sleep_until(next);
        }
        if(smoke) {
            if(core.pixels.empty() || !checkpointVerified || core.audioFrames==0) throw std::runtime_error("Smoke check failed");
            SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormatFrom(core.pixels.data(),int(core.width),int(core.height),32,int(core.width*4),SDL_PIXELFORMAT_ARGB8888);
            fs::create_directories("build/verification"); SDL_SaveBMP(surface,"build/verification/smoke.bmp"); SDL_FreeSurface(surface);
            std::ofstream report("build/verification/result.txt"); report<<"frames="<<frame<<"\naudio_frames="<<core.audioFrames<<"\nvideo="<<core.width<<"x"<<core.height<<"\nstate_roundtrip="<<checkpointVerified<<"\ndq5_profile="<<core.supported<<"\n";
        } else { core.saveRam(); settings.options=core.options; settings.touch=touch; settings.save(); }
        core.audio={}; if(audio) SDL_CloseAudioDevice(audio); if(controller) SDL_GameControllerClose(controller); if(texture) SDL_DestroyTexture(texture);
        ImGui_ImplSDLRenderer2_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext(); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    } catch(const std::exception& e) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"%s",e.what()); if(!smoke) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"SnesDQ5",e.what(),nullptr); result=1; }
    SDL_Quit(); return result;
}
