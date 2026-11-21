#pragma once
// Layout adapted from WiizV app/android_ui.h (MIT; see licenses/WiizV.txt).
#include <SDL.h>
#include <algorithm>
#include <vector>
struct TouchButton { SDL_FRect rect; unsigned id; const char* label; bool circle=false; };
struct TouchLayout { std::vector<TouchButton> buttons; SDL_FRect gameArea; SDL_FRect toolbar; };
inline bool touchContains(const TouchButton& button,SDL_FPoint p) {
    if(!SDL_PointInFRect(&p,&button.rect)) return false;
    if(!button.circle) return true;
    float x=(p.x-button.rect.x)/button.rect.w-.5f,y=(p.y-button.rect.y)/button.rect.h-.5f;
    return x*x+y*y<=.25f;
}
inline TouchLayout makeTouchLayout(int w,int h,float density,bool visible,bool swapAB=false,bool swapXY=false,SDL_FRect safe={0,0,0,0}) {
    if(safe.w<=0 || safe.h<=0) safe={0,0,float(w),float(h)};
    bool portrait=safe.h>safe.w;
    float margin=(portrait?8:6)*density,gap=(portrait?8:4)*density;
    float left=safe.x+margin,rightEdge=safe.x+safe.w-margin;
    float width=rightEdge-left,bottom=safe.y+safe.h-margin;
    SDL_FRect toolbar{left,safe.y+margin,width,(portrait?48:28)*density};
    float top=toolbar.y+toolbar.h+gap,height=std::max(1.f,bottom-top);
    TouchLayout layout{{},{left,top,width,height},toolbar};
    if(!visible) return layout;
    float small=std::min(44*density,height*.16f);
    float key=std::min((portrait?60:54)*density,std::min(width/6.2f,std::max(1.f,(height-2*small-3*gap)/2.8f)));
    float pitch=key*.9f,group=key+2*pitch,right=rightEdge-group;
    float groupY=portrait?bottom-small-gap-group:top+small+gap+(height-2*small-2*gap-group)/2;
    float shoulderY=groupY-small-gap;
    auto button=[&](float x,float y,float bw,float bh,unsigned id,const char* label,bool circle=false) {layout.buttons.push_back({{x,y,bw,bh},id,label,circle});};
    button(left+pitch,groupY,key,key,4,"UP"); button(left,groupY+pitch,key,key,6,"LEFT");
    button(left+2*pitch,groupY+pitch,key,key,7,"RIGHT"); button(left+pitch,groupY+2*pitch,key,key,5,"DOWN");
    button(right+pitch,groupY,key,key,9,swapXY?"Y":"X",true);
    button(right,groupY+pitch,key,key,1,swapXY?"X":"Y",true);
    button(right+2*pitch,groupY+pitch,key,key,8,swapAB?"B":"A",true);
    button(right+pitch,groupY+2*pitch,key,key,0,swapAB?"A":"B",true);
    button(left,shoulderY,group*.55f,small,10,"L"); button(right+group*.45f,shoulderY,group*.55f,small,11,"R");
    float middle=safe.x+safe.w*.5f;
    button(portrait?middle-small*2-gap:left,bottom-small,small*2,small,2,"SELECT");
    button(portrait?middle+gap:rightEdge-small*2,bottom-small,small*2,small,3,"START");
    SDL_FRect available=portrait?SDL_FRect{left,top,width,std::max(1.f,shoulderY-gap-top)}:
        SDL_FRect{left+group+gap,top,std::max(1.f,width-2*(group+gap)),height};
    float gameW=std::min(available.w,available.h*4/3),gameH=gameW*3/4;
    layout.gameArea={available.x+(available.w-gameW)/2,available.y+(available.h-gameH)/2,gameW,gameH};
    return layout;
}
