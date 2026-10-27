// Message-level checks for the custom list's state transitions. The separate
// mouse_contract_tests target exercises the public DLL ABI with a real queue.
#include <memory>
#define private public
#include "downloads.h"
#undef private
#include "../src/downloads.cpp"
#include <iostream>

namespace {
void Require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
HRESULT WINAPI NoNetwork(void*,const wchar_t*,HANDLE,IStream** out,BOOL* persist){*out=nullptr;*persist=FALSE;return E_NOTIMPL;}
struct Submitted {unsigned calls{};};
HRESULT WINAPI Submit(void* context,const ttp_browser_track*,uint32_t,int,int,int){++static_cast<Submitted*>(context)->calls;return S_OK;}
void Down(HWND window,int x,int y){SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));}
void Up(HWND window,int x,int y){SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(x,y));}
void Move(HWND window,int x,int y){SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x,y));}
void Click(HWND window,int x,int y){Down(window,x,y);Up(window,x,y);}
}
int wmain(){
    OleInitialize(nullptr);
    try{
        wchar_t temporary[MAX_PATH]{};Require(GetTempPathW(MAX_PATH,temporary)!=0,"temp path");
        const auto root=std::filesystem::path(temporary)/(L"ttpbrowser-mouse-state-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount()));
        std::filesystem::create_directories(root);
        const auto state=(root/L"queue.xml").wstring(),folder=root.wstring();
        Submitted submitted;ttp_browser_host host{};host.size=sizeof(host);host.version=1;host.context=&submitted;host.open_stream=NoNetwork;host.submit_tracks=Submit;
        ttp_browser_config config{};config.size=sizeof(config);config.runtime_directory=folder.c_str();config.download_directory=folder.c_str();config.state_file=state.c_str();config.maximum_downloads=1;
        const HWND parent=CreateWindowExW(0,L"STATIC",L"Isolated mouse state fixture",WS_OVERLAPPED,0,0,600,500,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Require(parent!=nullptr,"fixture parent");
        {
            ttpbrowser::Downloads view(host,config);const auto frame=view.CreateView(parent);Require(frame!=nullptr,"downloads frame");view.Resize({0,0,500,400});
            auto& d=*view.impl_;for(unsigned i=0;i<40;++i){ttplayer::audio::HttpDownload row;row.id=i+1;row.state=ttplayer::audio::DownloadState::paused;
                row.display_title=i%2?L"Alpha":L"Zulu";row.entry.title=L"Artist - "+row.display_title;row.entry.sourceurl=L"http://fixture.invalid/unused.mp3";row.destination=root/(std::to_wstring(i)+L".mp3");d.rows.push_back(row);}
            d.Filter();const HWND list=d.list;const int title_x=d.Widths()[0]+20;
            Click(list,title_x,8);Require(d.sort_column==-1,"active header cannot sort");
            for(auto& row:d.rows)row.state=ttplayer::audio::DownloadState::complete;d.ChooseCategory(1);
            Down(list,title_x,8);Require(d.sort_column==-1,"header waits for button release");
            Up(list,200,8);Require(d.sort_column==-1,"release on different column cancels sort");
            Click(list,d.Widths()[0]+d.Widths()[1]-4,8);Require(d.sort_column==-1,"column boundary cannot sort");
            Click(list,title_x,8);Require(d.sort_column==1 && !d.descending,"completed header sorts on matching release");
            Down(list,title_x,8);SendMessageW(list,WM_CANCELMODE,0,0);Up(list,title_x,8);Require(!d.descending,"cancel does not sort");
            Click(list,title_x,8);Require(d.descending,"repeat header click reverses sort");
            Down(list,5,d.HeaderHeight()+5);Require(submitted.calls==0,"play icon waits for release");Up(list,title_x,d.HeaderHeight()+5);
            Require(submitted.calls==0,"dragging off play icon cancels playback");
            Down(list,5,d.HeaderHeight()+5);SendMessageW(list,WM_CANCELMODE,0,0);Up(list,5,d.HeaderHeight()+5);
            Require(submitted.calls==0,"canceling play icon does not play");
            Down(list,5,d.HeaderHeight()+5);SetCapture(parent);ReleaseCapture();Up(list,5,d.HeaderHeight()+5);
            Require(submitted.calls==0,"losing play icon capture does not play");
            Click(list,title_x,d.HeaderHeight()+5);Require(d.selected.size()==1,"row selects");
            d.Scroll(72);d.ChooseCategory(1);Require(d.scroll==72 && d.selected.size()==1,"same category preserves selection and scroll");
            const int middle=(d.BodyTop()+400)/2;
            Click(frame,82,middle);Require(d.category_width==0,"splitter center collapses");
            Click(frame,2,middle);Require(d.category_width==80,"splitter center restores width");
            Down(frame,82,100);Move(frame,132,100);Require(d.category_width==130,"splitter drag tracks delta");
            SendMessageW(frame,WM_CANCELMODE,0,0);Require(d.category_width==80,"splitter cancel restores width");
            Down(frame,82,100);Move(frame,112,100);SendMessageW(frame,WM_KEYDOWN,VK_ESCAPE,0);Require(d.category_width==80,"splitter escape restores width");
            Down(frame,82,100);Move(frame,112,100);SetCapture(parent);Require(d.category_width==80,"splitter capture loss restores width");ReleaseCapture();
            d.Scroll(0);auto thumb=d.Thumb();const int scrollbar_x=d.ListWidth()+4,thumb_y=(thumb.top+thumb.bottom)/2;
            Down(list,scrollbar_x,thumb_y);Move(list,scrollbar_x,thumb_y+60);Require(d.scroll>0,"thumb drag scrolls");
            SendMessageW(list,WM_KEYDOWN,VK_ESCAPE,0);Require(d.scroll==0,"thumb escape restores scroll");
            Down(list,scrollbar_x,thumb_y);Move(list,scrollbar_x,thumb_y+60);SetCapture(parent);Require(d.scroll==0,"thumb capture loss restores scroll");ReleaseCapture();
            RECT bounds{};GetClientRect(list,&bounds);Down(list,scrollbar_x,bounds.bottom-5);const int first_scroll=d.scroll;
            SendMessageW(list,WM_TIMER,ttpbrowser::kScrollRepeatTimer,0);Require(d.scroll>first_scroll,"held scroll arrow repeats");Up(list,scrollbar_x,bounds.bottom-5);
            const int stopped=d.scroll;SendMessageW(list,WM_TIMER,ttpbrowser::kScrollRepeatTimer,0);Require(d.scroll==stopped,"button release stops repeat");
            UINT wheel_lines=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&wheel_lines,0);d.Scroll(0);
            SendMessageW(list,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-60)),0);Require(d.scroll==0,"partial wheel delta is accumulated");
            SendMessageW(list,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-60)),0);
            const int64_t requested=wheel_lines==WHEEL_PAGESCROLL?d.BodyHeight():static_cast<int64_t>(wheel_lines)*d.skin.row_height;
            Require(d.scroll==std::min<int64_t>(requested,d.MaxScroll()),"wheel obeys system line/page setting");
            d.Scroll(0);Move(list,5,d.HeaderHeight()+5);Require(d.tooltip_text==L"播放歌曲","play cell tooltip");
            Move(list,title_x,d.HeaderHeight()+5);Require(d.tooltip_text.find(L"歌曲名：")!=std::wstring::npos && d.tooltip_text.find(L"文件：")!=std::wstring::npos,"title cell metadata tooltip");
            SendMessageW(list,WM_MOUSELEAVE,0,0);Require(d.tooltip_text.empty(),"mouse leave clears tooltip");
            d.ChooseCategory(0);for(auto& row:d.rows)row.state=ttplayer::audio::DownloadState::paused;d.Filter();
            Move(list,5,d.HeaderHeight()+5);Require(d.tooltip_text==L"暂停下载","paused task tooltip");
            Require(submitted.calls==0,"state probes do not play or access network");
            std::cout<<"download mouse state checks passed\n";
        }
        DestroyWindow(parent);OleUninitialize();return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';OleUninitialize();return 1;}
}
