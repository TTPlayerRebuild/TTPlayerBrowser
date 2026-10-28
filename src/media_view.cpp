#include "media_view.h"
#include "downloads.h"
#include <commctrl.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <atomic>
#include <chrono>
#include <future>
#include <algorithm>

namespace ttpbrowser {
using namespace media;
namespace {
constexpr wchar_t class_name[]=L"TTPBrowser.MediaLibrary.v1";
enum {SourceBox=5100,Settings,Category,SearchText,Search,BackButton,NextPage,PreviousPage,
      List,Play,Add,Download,Status,Location,CoverImage,Detail,
      Url=5200,User,Secret,AuthBox,Root,FolderPassword,Proxy,Save,CancelSettings,Test};
HINSTANCE Module(){HMODULE h{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Module),&h);return h;}
std::wstring Text(HWND h){int size=GetWindowTextLengthW(h);std::wstring s(size+1,L'\0');GetWindowTextW(h,s.data(),size+1);s.resize(size);return s;}
const wchar_t* Type(Kind k){switch(k){case Kind::Album:return L"专辑";case Kind::Artist:return L"歌手";case Kind::Playlist:return L"歌单";case Kind::Directory:return L"文件夹";default:return L"歌曲";}}
std::wstring Duration(uint64_t n){if(!n)return {};const auto secs=n%60;return std::to_wstring(n/60)+L":"+(secs<10?L"0":L"")+std::to_wstring(secs);}
HBITMAP Bitmap(const std::string& bytes) {
    if(bytes.empty())return nullptr;IStream* stream{};if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)))return nullptr;
    ULONG written{};LARGE_INTEGER zero{};
    if(FAILED(stream->Write(bytes.data(),static_cast<ULONG>(bytes.size()),&written))||written!=bytes.size()||FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr))){stream->Release();return nullptr;}
    HBITMAP result{};
    {Gdiplus::Bitmap original(stream);if(original.GetLastStatus()==Gdiplus::Ok){Gdiplus::Bitmap canvas(96,96,PixelFormat32bppARGB);Gdiplus::Graphics g(&canvas);g.Clear(Gdiplus::Color(255,245,245,245));
        const auto w=original.GetWidth(),h=original.GetHeight();if(w&&h){const double scale=96.0/std::max(w,h);int dw=static_cast<int>(w*scale),dh=static_cast<int>(h*scale);g.DrawImage(&original,(96-dw)/2,(96-dh)/2,dw,dh);canvas.GetHBITMAP(Gdiplus::Color(255,245,245,245),&result);}}}
    stream->Release();return result;
}
}
struct MediaView::Impl {
    Library& library;ttp_browser_host host;Downloads& downloads;
    HWND window{};std::map<int,HWND> controls;std::vector<HWND> form_labels;
    std::vector<Profile> profiles;unsigned selected{};Query query;std::vector<Query> history;
    Page page;bool busy{};uint64_t generation{},detail_generation{};
    HBITMAP cover{};ULONG_PTR graphics{};
    struct Result {Page page;std::wstring error,detail;std::string cover;HRESULT code{S_OK};int kind{};uint64_t generation{},detail_generation{};};
    struct Job {std::shared_ptr<std::atomic<bool>> cancel;std::future<Result> future;};
    std::vector<Job> jobs;
    Impl(Library& l,const ttp_browser_host& h,Downloads& d):library(l),host(h),downloads(d),profiles(l.Profiles()),selected(l.ActiveProfile()) {Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&graphics,&input,nullptr);}
    ~Impl(){for(auto& j:jobs)*j.cancel=true;for(auto& j:jobs)if(j.future.valid())j.future.wait();if(window)DestroyWindow(window);if(cover)DeleteObject(cover);if(graphics)Gdiplus::GdiplusShutdown(graphics);UnregisterClassW(class_name,Module());}
    HWND Control(int id,const wchar_t* type,const wchar_t* text,DWORD style=0,DWORD ex=0) {
        auto h=CreateWindowExW(ex,type,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),Module(),nullptr);
        controls[id]=h;SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return h;
    }
    void StatusText(const std::wstring& value){SetWindowTextW(controls[Status],value.c_str());}
    void CancelJobs(){for(auto& j:jobs)*j.cancel=true;++generation;++detail_generation;busy=false;}
    template<class F> void Run(int kind,F fn) {
        Poll();if(jobs.size()>=4){StatusText(L"正在取消上一次请求，请稍后重试");return;}
        auto cancel=std::make_shared<std::atomic<bool>>(false);const auto gen=generation,detail_gen=detail_generation;
        jobs.push_back({cancel,std::async(std::launch::async,[=]() mutable {Result r;r.kind=kind;r.generation=gen;r.detail_generation=detail_gen;
            try{fn(r,Cancel([cancel]{return cancel->load();}));}catch(const Error& e){r.error=e.message;r.code=e.code;}catch(...){r.error=L"请求失败，请检查服务器配置";r.code=E_FAIL;}return r;})});
        if(kind!=2){busy=true;StatusText(kind==1?L"正在测试连接…":L"正在读取媒体库…");}
    }
    void Poll() {
        for(size_t i=0;i<jobs.size();) {
            if(jobs[i].future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){++i;continue;}
            auto result=jobs[i].future.get();jobs.erase(jobs.begin()+i);
            if(result.generation!=generation)continue;
            if(result.kind==2){if(result.detail_generation!=detail_generation)continue;SetWindowTextW(controls[Detail],result.detail.c_str());
                auto next=Bitmap(result.cover);SendMessageW(controls[CoverImage],STM_SETIMAGE,IMAGE_BITMAP,reinterpret_cast<LPARAM>(next));if(cover)DeleteObject(cover);cover=next;continue;}
            busy=false;if(FAILED(result.code)){StatusText(result.error);continue;}
            if(result.kind==1){StatusText(L"连接成功，账号和目录可用");continue;}
            page=std::move(result.page);Fill();
        }
    }
    void Fill() {
        ListView_DeleteAllItems(controls[List]);int row=0;
        for(const auto& item:page.items) {
            LVITEMW value{};value.mask=LVIF_TEXT;value.iItem=row;value.pszText=const_cast<wchar_t*>(item.title.c_str());ListView_InsertItem(controls[List],&value);
            auto put=[&](int col,const std::wstring& s){ListView_SetItemText(controls[List],row,col,const_cast<wchar_t*>(s.c_str()));};
            put(1,item.artist);put(2,item.album);put(3,Type(item.kind));put(4,Duration(item.duration));++row;
        }
        EnableWindow(controls[PreviousPage],query.page>0);EnableWindow(controls[NextPage],page.more);EnableWindow(controls[BackButton],!history.empty());
        for(int id:{Play,Add,Download})EnableWindow(controls[id],TRUE);
        SetWindowTextW(controls[Location],page.description.c_str());
        SetWindowTextW(controls[5140],std::to_wstring(query.page+1).c_str());StatusText(L"");
    }
    void Browse(bool remember=false) {
        CancelJobs();if(remember)history.push_back(query);
        page={};ListView_DeleteAllItems(controls[List]);
        for(int id:{Play,Add,Download,NextPage,PreviousPage})EnableWindow(controls[id],FALSE);
        SetWindowTextW(controls[Detail],L"");SendMessageW(controls[CoverImage],STM_SETIMAGE,IMAGE_BITMAP,0);if(cover){DeleteObject(cover);cover=nullptr;}
        if(profiles.empty()){StatusText(L"暂无媒体来源，请从右下角菜单进入设置添加。");SetWindowTextW(controls[Location],L"");EnableWindow(controls[BackButton],FALSE);return;}
        const auto p=profiles[selected];const auto q=query;auto* l=&library;Run(0,[l,p,q](Result& r,const Cancel& c){r.page=l->Browse(p,q,c);});
    }
    void RootQuery(){history.clear();query={};if(profiles.empty())return;query.view=profiles[selected].provider==Provider::OpenList?View::Directory:View::Albums;query.id=profiles[selected].root;SendMessageW(controls[Category],CB_SETCURSEL,0,0);}
    void Open(int index) {
        if(index<0||static_cast<size_t>(index)>=page.items.size())return;
        const auto item=page.items[index];if(item.kind==Kind::Song){Submit(Play);return;}
        history.push_back(query);query={};query.id=item.id;
        query.view=item.kind==Kind::Directory?View::Directory:item.kind==Kind::Album?View::Album:item.kind==Kind::Artist?View::Artist:View::Playlist;Browse();
    }
    void Submit(int action) {
        if(busy)return;
        if(!host.media_source_version){StatusText(L"请同时更新 TTPlayerRebuild.exe，以支持个人媒体库播放和下载");return;}
        std::vector<BrowserTrack> tracks;
        for(int i=-1;(i=ListView_GetNextItem(controls[List],i,LVNI_SELECTED))>=0;)if(static_cast<size_t>(i)<page.items.size()&&page.items[i].kind==Kind::Song){
            const auto& item=page.items[i];tracks.push_back({Source(profiles[selected],item,action==Download),item.artist,item.title,profiles[selected].id+L":"+item.id});}
        if(tracks.empty()){StatusText(L"请先打开专辑、歌单或目录，再选择歌曲");return;}
        HRESULT hr=S_OK;
        if(action==Download){for(const auto& t:tracks){uint64_t task{};auto track=t.Borrow();const auto result=downloads.Enqueue(track,&task);if(FAILED(result))hr=result;}}
        else {std::vector<ttp_browser_track> borrowed;for(const auto& t:tracks)borrowed.push_back(t.Borrow());hr=host.submit_tracks?host.submit_tracks(host.context,borrowed.data(),static_cast<uint32_t>(borrowed.size()),0,1,action==Play?1:0):E_NOTIMPL;}
        if(action==Download&&hr==HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
            StatusText(L"请先在下载设置中选择保存目录，然后再次点击下载");
            if(host.owner)PostMessageW(host.owner,WM_COMMAND,0x903,0);
        }
        else if(FAILED(hr)){wchar_t code[24]{};swprintf_s(code,L"0x%08lX",hr);StatusText(std::wstring(L"操作失败：")+code+(action==Download?L"；请检查下载目录设置":L""));}
        else StatusText(action==Download?L"已加入下载管理":action==Play?L"已交给千千播放":L"已加入播放列表");
    }
    void Details() {
        const int index=ListView_GetNextItem(controls[List],-1,LVNI_SELECTED);++detail_generation;
        if(index<0||static_cast<size_t>(index)>=page.items.size())return;
        auto item=page.items[index];auto p=profiles[selected];auto* l=&library;
        Run(2,[l,p,item](Result& r,const Cancel& c){r.detail=item.title+L"\r\n"+item.artist+(item.album.empty()?L"":L"\r\n"+item.album);
            if(!item.cover.empty())try{r.cover=l->Cover(p,item.cover,c);}catch(...){}
            if(item.kind==Kind::Song)try{auto lyric=l->Lyrics(p,item,c);if(!lyric.empty())r.detail+=L"\r\n\r\n"+lyric;}catch(...){}
        });
    }
    void Categories() {
        SendMessageW(controls[Category],CB_RESETCONTENT,0,0);
        for(int id:{Category,Search,SearchText})EnableWindow(controls[id],!profiles.empty());if(profiles.empty())return;
        if(profiles[selected].provider==Provider::OpenList)SendMessageW(controls[Category],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"文件夹"));
        else for(const auto s:{L"最新专辑",L"歌手",L"歌单",L"全部歌曲",L"收藏歌曲"})SendMessageW(controls[Category],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));
        SendMessageW(controls[Category],CB_SETCURSEL,0,0);
    }
    void Command(int id,int notification) {
        if(id==Settings){auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,6000,L"设置");const auto sources=library.Profiles();const auto active=library.ActiveProfile();if(!sources.empty())AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            for(size_t n=0;n<sources.size();++n){std::wstring title;for(auto c:sources[n].name){title+=c;if(c==L'&')title+=c;}AppendMenuW(menu,MF_STRING|(n==active?MF_CHECKED:0),6100+n,title.c_str());}
            RECT r{};GetWindowRect(controls[Settings],&r);auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN|TPM_BOTTOMALIGN|TPM_RIGHTBUTTON,r.right,r.top,0,window,nullptr);DestroyMenu(menu);if(choice)Command(choice,0);return;}
        if(id==6000){if(host.open_media_settings)host.open_media_settings(host.context);else StatusText(L"请更新主程序，并在选项 → 网络连接 → 音乐窗中设置媒体来源");return;}
        if(id>=6100&&id<6164){const auto sources=library.Profiles();if(static_cast<size_t>(id-6100)<sources.size()){library.SelectProfile(sources[id-6100].id);profiles=library.Profiles();selected=library.ActiveProfile();Categories();RootQuery();Browse();}return;}
        if(profiles.empty())return;
        if(id==Play||id==Add||id==Download)Submit(id);
        else if(id==Category&&notification==CBN_SELCHANGE){history.clear();query={};const View views[]={View::Albums,View::Artists,View::Playlists,View::Songs,View::Starred};const auto index=SendMessageW(controls[Category],CB_GETCURSEL,0,0);query.view=profiles[selected].provider==Provider::OpenList?View::Directory:views[std::clamp<INT_PTR>(index,0,4)];Browse();}
        else if(id==Search){history.push_back(query);query={View::Search,profiles[selected].provider==Provider::OpenList?profiles[selected].root:L"",Text(controls[SearchText]),0};Browse();}
        else if(id==BackButton){if(!history.empty()){query=history.back();history.pop_back();Browse();}}
        else if(id==NextPage&&page.more){++query.page;Browse();}
        else if(id==PreviousPage&&query.page){--query.page;Browse();}
    }
    void Layout() {
        if(!window)return;RECT r{};GetClientRect(window,&r);const int width=std::max(300L,r.right),height=std::max(180L,r.bottom),pad=8;
        auto move=[&](int id,int x,int y,int w,int h){MoveWindow(controls[id],x,y,std::max(1,w),std::max(1,h),TRUE);};
        move(Settings,width-30,height-28,22,22);move(Status,pad,height-51,width-16,20);
        move(Category,8,8,100,210);move(SearchText,116,8,width-186,24);move(Search,width-62,8,54,24);
        move(BackButton,8,38,54,23);move(Location,70,40,width-78,22);move(PreviousPage,8,height-27,22,20);move(5140,32,height-24,18,18);move(NextPage,52,height-27,22,20);
        const bool detail=height>=400;const int bottom=height-60-(detail?104:0);move(List,8,66,width-16,bottom-66);
        for(int id:{CoverImage,Detail})ShowWindow(controls[id],detail?SW_SHOW:SW_HIDE);
        if(detail){move(CoverImage,8,bottom+4,96,96);move(Detail,112,bottom+4,width-120,96);}
        move(Play,width-189,height-27,49,20);move(Add,width-137,height-27,49,20);move(Download,width-85,height-27,49,20);
        ListView_SetColumnWidth(controls[List],0,std::max(135,width*2/5));
    }
    HWND Create(HWND parent) {
        if(window)return window;INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&cc);
        WNDCLASSW wc{};wc.hInstance=Module();wc.lpszClassName=class_name;wc.lpfnWndProc=Proc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&wc);
        window=CreateWindowExW(WS_EX_CONTROLPARENT,class_name,L"个人媒体库",WS_CHILD|WS_CLIPCHILDREN,0,0,0,0,parent,nullptr,Module(),this);return window;
    }
    void Init() {
        Control(Settings,L"BUTTON",L"⚙");
        Control(Category,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL);Control(SearchText,L"EDIT",L"",ES_AUTOHSCROLL,WS_EX_CLIENTEDGE);SendMessageW(controls[SearchText],EM_SETLIMITTEXT,256,0);
        Control(Search,L"BUTTON",L"搜索");Control(BackButton,L"BUTTON",L"返回");Control(PreviousPage,L"BUTTON",L"‹");Control(NextPage,L"BUTTON",L"›");Control(5140,L"STATIC",L"1",SS_CENTER);
        Control(List,WC_LISTVIEWW,L"",LVS_REPORT|LVS_SHOWSELALWAYS,WS_EX_CLIENTEDGE);ListView_SetExtendedListViewStyle(controls[List],LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
        const wchar_t* names[]={L"名称",L"歌手",L"专辑",L"类型",L"时长"};for(int i=0;i<5;++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.cx=i==0?180:i<3?110:60;c.pszText=const_cast<wchar_t*>(names[i]);ListView_InsertColumn(controls[List],i,&c);}
        Control(Play,L"BUTTON",L"▶播放");Control(Add,L"BUTTON",L"＋添加");Control(Download,L"BUTTON",L"↓下载");Control(Status,L"STATIC",L"",SS_NOPREFIX);Control(Location,L"STATIC",L"",SS_PATHELLIPSIS|SS_NOPREFIX);
        Control(CoverImage,L"STATIC",L"",SS_BITMAP);Control(Detail,L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL);
        Categories();RootQuery();SetTimer(window,1,100,nullptr);Layout();
        if(!library.LoadError().empty())StatusText(library.LoadError());else Browse();
    }
    static LRESULT CALLBACK Proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,message,wp,lp);
        try{switch(message){case WM_CREATE:self->Init();return 0;case WM_SIZE:self->Layout();return 0;case WM_TIMER:self->Poll();return 0;
        case WM_COMMAND:self->Command(LOWORD(wp),HIWORD(wp));return 0;
        case WM_NOTIFY:{auto* n=reinterpret_cast<NMHDR*>(lp);if(n->idFrom==List){if(n->code==NM_DBLCLK)self->Open(reinterpret_cast<NMITEMACTIVATE*>(lp)->iItem);
            else if(n->code==LVN_ITEMCHANGED){auto* item=reinterpret_cast<NMLISTVIEW*>(lp);if((item->uChanged&LVIF_STATE)&&(item->uNewState&LVIS_SELECTED)&&!(item->uOldState&LVIS_SELECTED))self->Details();}
            else if(n->code==LVN_KEYDOWN){auto key=reinterpret_cast<NMLVKEYDOWN*>(lp)->wVKey;if(key==VK_RETURN)self->Open(ListView_GetNextItem(self->controls[List],-1,LVNI_SELECTED));}}return 0;}
        case WM_DESTROY:self->CancelJobs();KillTimer(hwnd,1);return 0;
        case WM_NCDESTROY:self->window=nullptr;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);break;}}
        catch(const Error& e){self->StatusText(e.message);}catch(...){self->StatusText(L"操作失败");}return DefWindowProcW(hwnd,message,wp,lp);
    }
};
MediaView::MediaView(Library& l,const ttp_browser_host& h,Downloads& d):impl_(std::make_unique<Impl>(l,h,d)){}
MediaView::~MediaView()=default;
HWND MediaView::Create(HWND p){return impl_->Create(p);}
HWND MediaView::Window() const{return impl_->window;}
void MediaView::Resize(RECT r){if(impl_->window)MoveWindow(impl_->window,r.left,r.top,r.right-r.left,r.bottom-r.top,TRUE);}
void MediaView::ReloadProfiles(){impl_->profiles=impl_->library.Profiles();impl_->selected=impl_->library.ActiveProfile();if(impl_->window){impl_->Categories();impl_->RootQuery();impl_->Browse();}}
void MediaView::Refresh(){if(impl_->window)impl_->Browse();}
void MediaView::Back(){impl_->Command(BackButton,0);}
bool MediaView::CanBack() const{return !impl_->history.empty();}
bool MediaView::Translate(MSG* m){if(!m||!impl_->window||!IsWindowVisible(impl_->window))return false;if(m->message==WM_KEYDOWN&&m->wParam==VK_RETURN&&m->hwnd==impl_->controls[SearchText]){impl_->Command(Search,0);return true;}return IsDialogMessageW(impl_->window,m)!=FALSE;}
}
