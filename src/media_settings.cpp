#include "media_settings.h"
#include <commctrl.h>
#include <windowsx.h>
#include <future>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <uxtheme.h>
#include <vssym32.h>

namespace ttpbrowser {
using namespace media;
namespace {
enum {SourceList=5300,Url,AuthBox,User,Secret,Root,Password,Proxy,Save,Test,Status,Name,Type,
      Add=5320,Remove,Up,Down,Edit};
HINSTANCE Module(){HMODULE h{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Module),&h);return h;}
std::wstring Text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
const wchar_t* TypeName(Provider type){return type==Provider::OpenList?L"OpenList":L"OpenSubsonic";}
std::wstring Trim(std::wstring s){auto first=s.find_first_not_of(L" \t\r\n");if(first==s.npos)return {};return s.substr(first,s.find_last_not_of(L" \t\r\n")-first+1);}
void Center(HWND dialog,HWND owner){RECT r{},p{};GetWindowRect(dialog,&r);GetWindowRect(owner,&p);MONITORINFO m{sizeof(m)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&m);const LONG w=r.right-r.left,h=r.bottom-r.top;
    SetWindowPos(dialog,nullptr,std::clamp((p.left+p.right-w)/2,m.rcWork.left,std::max(m.rcWork.left,m.rcWork.right-w)),std::clamp((p.top+p.bottom-h)/2,m.rcWork.top,std::max(m.rcWork.top,m.rcWork.bottom-h)),0,0,SWP_NOSIZE|SWP_NOZORDER);}
void PaintButton(HWND h,HDC dc,HIMAGELIST images,const DRAWITEMSTRUCT* draw=nullptr){RECT r{};GetClientRect(h,&r);int w{},ht{};if(!ImageList_GetIconSize(images,&w,&ht))return;const int saved=SaveDC(dc);IntersectClipRect(dc,0,0,r.right,r.bottom);
    const bool enabled=draw?!(draw->itemState&ODS_DISABLED):IsWindowEnabled(h)!=FALSE,pressed=enabled&&(draw?(draw->itemState&ODS_SELECTED):(SendMessageW(h,BM_GETSTATE,0,0)&BST_PUSHED)),hot=enabled&&!pressed&&GetPropW(h,L"TTPBrowser.ButtonHot");const int state=!enabled?PBS_DISABLED:pressed?PBS_PRESSED:hot?PBS_HOT:PBS_NORMAL;
    auto theme=OpenThemeData(h,L"Button");FillRect(dc,&r,GetSysColorBrush(COLOR_BTNFACE));if(!theme||FAILED(DrawThemeBackground(theme,dc,BP_PUSHBUTTON,state,&r,nullptr)))DrawEdge(dc,&r,pressed?EDGE_SUNKEN:hot?EDGE_RAISED:BDR_RAISEDINNER,BF_RECT);
    const int x=(r.right-w+1)/2+pressed,y=(r.bottom-ht+1)/2+pressed;const RECT image_rect{x,y,x+w,y+ht};const bool themed=theme&&SUCCEEDED(DrawThemeIcon(theme,dc,BP_PUSHBUTTON,state,&image_rect,images,0));
    if(!themed&&!enabled){auto icon=ImageList_GetIcon(images,0,ILD_NORMAL);if(icon){DrawStateW(dc,nullptr,nullptr,reinterpret_cast<LPARAM>(icon),0,x,y,w,ht,DST_ICON|DSS_DISABLED);DestroyIcon(icon);}}else if(!themed)ImageList_Draw(images,0,dc,x,y,ILD_NORMAL);
    if(theme)CloseThemeData(theme);RestoreDC(dc,saved);}
void PaintButtonBuffered(HWND h,HDC dc,HIMAGELIST images,const DRAWITEMSTRUCT* draw=nullptr){
    RECT r{};GetClientRect(h,&r);if(r.right<=0||r.bottom<=0)return;
    HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);
    if(buffer&&bitmap){auto old=SelectObject(buffer,bitmap);PaintButton(h,buffer,images,draw);BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);}
    else PaintButton(h,dc,images,draw);
    if(bitmap)DeleteObject(bitmap);if(buffer)DeleteDC(buffer);
}
LRESULT CALLBACK ButtonProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
    if(m==WM_SETCURSOR&&reinterpret_cast<HWND>(w)==h&&LOWORD(l)==HTCLIENT&&IsWindowEnabled(h)){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}
    if(m==WM_MOUSEMOVE){RECT r{};GetClientRect(h,&r);POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};const bool hot=IsWindowEnabled(h)&&PtInRect(&r,p);
        if(hot!=(GetPropW(h,L"TTPBrowser.ButtonHot")!=nullptr)){if(hot){SetPropW(h,L"TTPBrowser.ButtonHot",reinterpret_cast<HANDLE>(1));TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);}else RemovePropW(h,L"TTPBrowser.ButtonHot");InvalidateRect(h,nullptr,FALSE);}}
    else if(m==WM_MOUSELEAVE||m==WM_CANCELMODE||(m==WM_ENABLE&&!w)){if(RemovePropW(h,L"TTPBrowser.ButtonHot"))InvalidateRect(h,nullptr,FALSE);}
    if(m==WM_ERASEBKGND)return TRUE;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){auto images=reinterpret_cast<HIMAGELIST>(data);if(m==WM_PRINTCLIENT)PaintButtonBuffered(h,reinterpret_cast<HDC>(w),images);else{PAINTSTRUCT p{};auto dc=BeginPaint(h,&p);PaintButtonBuffered(h,dc,images);EndPaint(h,&p);}return 0;}
    if(m==WM_NCDESTROY){RemoveWindowSubclass(h,ButtonProc,id);RemovePropW(h,L"TTPBrowser.ButtonHot");const auto result=DefSubclassProc(h,m,w,l);ImageList_Destroy(reinterpret_cast<HIMAGELIST>(data));return result;}return DefSubclassProc(h,m,w,l);}
void InstallButtonImage(HWND button,int index){const int ids[]={1027,1039,1042,1045,352};wchar_t path[32768]{};GetModuleFileNameW(Module(),path,32768);const auto resource_path=std::filesystem::path(path).parent_path()/L"ttpres.dll";auto resources=LoadLibraryExW(resource_path.c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE);
    HBITMAP bitmap=resources?static_cast<HBITMAP>(LoadImageW(resources,MAKEINTRESOURCEW(ids[index]),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION)):nullptr;if(resources)FreeLibrary(resources);
    if(!bitmap)bitmap=static_cast<HBITMAP>(LoadImageW(Module(),MAKEINTRESOURCEW(310+index),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION));if(!bitmap)return;BITMAP b{};GetObjectW(bitmap,sizeof(b),&b);auto images=ImageList_Create(b.bmWidth,b.bmHeight,ILC_COLOR32|ILC_MASK,1,0);if(images&&ImageList_AddMasked(images,bitmap,RGB(192,192,192))<0){ImageList_Destroy(images);images=nullptr;}DeleteObject(bitmap);
    if(images&&!SetWindowSubclass(button,ButtonProc,1,reinterpret_cast<DWORD_PTR>(images)))ImageList_Destroy(images);
}
// BUTTON can draw synchronously on BM_SETSTATE/WM_ENABLE, without WM_PAINT.
// Owner draw sends those frames through the same buffered icon renderer too.
bool DrawButton(const DRAWITEMSTRUCT& item){DWORD_PTR data{};
    if(item.CtlType!=ODT_BUTTON||!GetWindowSubclass(item.hwndItem,ButtonProc,1,&data)||!data)return false;
    PaintButtonBuffered(item.hwndItem,item.hDC,reinterpret_cast<HIMAGELIST>(data),&item);return true;
}

}
struct MediaSettings::Impl {
    Library& library;std::function<void()> saved;HWND window{},list{},status{},tooltip{};HFONT font{};
    std::vector<Profile> profiles;bool filling{};
    Impl(Library& l,std::function<void()> fn):library(l),saved(std::move(fn)){}
    ~Impl(){if(window)DestroyWindow(window);}
    void StatusText(const std::wstring& s){SetWindowTextW(status,s.c_str());}
    int Selected(){return ListView_GetNextItem(list,-1,LVNI_SELECTED);}
    void Buttons(){const int n=Selected();const bool valid=n>=0&&static_cast<size_t>(n)<profiles.size();
        EnableWindow(GetDlgItem(window,Add),profiles.size()<64);EnableWindow(GetDlgItem(window,Remove),valid);EnableWindow(GetDlgItem(window,Edit),valid);
        EnableWindow(GetDlgItem(window,Up),valid&&n>0);EnableWindow(GetDlgItem(window,Down),valid&&static_cast<size_t>(n+1)<profiles.size());}
    void Fill(int select=0){profiles=library.Profiles();filling=true;SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);
        for(size_t n=0;n<profiles.size();++n){LVITEMW row{};row.mask=LVIF_TEXT;row.iItem=static_cast<int>(n);row.pszText=profiles[n].name.data();ListView_InsertItem(list,&row);ListView_SetItemText(list,row.iItem,1,const_cast<wchar_t*>(TypeName(profiles[n].provider)));}
        if(!profiles.empty()){select=std::clamp(select,0,static_cast<int>(profiles.size()-1));ListView_SetItemState(list,select,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(list,select,FALSE);}
        SendMessageW(list,WM_SETREDRAW,TRUE,0);RedrawWindow(list,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN);filling=false;Buttons();}
    void Changed(int selected){Fill(selected);StatusText(profiles.empty()?L"尚无媒体来源，点击 + 添加。":L"双击来源或点击编辑图标进行编辑。");if(saved)saved();}
    struct Editor {
        Impl& owner;Profile original;bool adding{},loading{};HWND dialog{};
        std::shared_ptr<std::atomic<bool>> cancel;std::future<std::wstring> test;uint64_t generation{},test_generation{};
        ~Editor(){if(cancel)*cancel=true;if(test.valid())test.wait();}
        HWND Get(int id){return GetDlgItem(dialog,id);}void StatusText(const std::wstring& text){SetWindowTextW(Get(Status),text.c_str());}
        Provider ProviderValue(){return SendMessageW(Get(Type),CB_GETCURSEL,0,0)==1?Provider::OpenList:Provider::Subsonic;}
        void AuthControls(){const auto a=static_cast<Auth>(SendMessageW(Get(AuthBox),CB_GETITEMDATA,SendMessageW(Get(AuthBox),CB_GETCURSEL,0,0),0));
            EnableWindow(Get(User),a==Auth::Password);EnableWindow(Get(Secret),a!=Auth::Guest);for(int id:{Root,Password,Proxy})EnableWindow(Get(id),ProviderValue()==Provider::OpenList);}
        void AuthList(Auth selected){SendMessageW(Get(AuthBox),CB_RESETCONTENT,0,0);const wchar_t* names[]={L"密码登录",L"OpenSubsonic API 密钥",L"OpenList 登录令牌",L"OpenList 访客"};
            for(int a=0;a<4;++a){if(ProviderValue()==Provider::Subsonic?a>1:a==1)continue;const auto i=SendMessageW(Get(AuthBox),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(names[a]));SendMessageW(Get(AuthBox),CB_SETITEMDATA,i,a);if(a==static_cast<int>(selected))SendMessageW(Get(AuthBox),CB_SETCURSEL,i,0);}AuthControls();}
        Profile Edited(){auto p=original;p.name=Trim(Text(Get(Name)));p.provider=ProviderValue();p.url=NormalizeUrl(Text(Get(Url)));p.username=Text(Get(User));p.secret=Text(Get(Secret));p.root=Text(Get(Root));if(p.root.empty())p.root=L"/";
            if(p.name.empty()||p.name.size()>128)throw Error{E_INVALIDARG,L"请填写来源名称（最多 128 字）"};
            if(p.root.size()>4095||p.root.find_first_of(L"\r\n\\")!=p.root.npos)throw Error{E_INVALIDARG,L"目录路径无效"};
            p.directory_password=Text(Get(Password));p.auth=static_cast<Auth>(SendMessageW(Get(AuthBox),CB_GETITEMDATA,SendMessageW(Get(AuthBox),CB_GETCURSEL,0,0),0));p.proxy=Button_GetCheck(Get(Proxy))==BST_CHECKED;
            if(p.provider!=original.provider)p.id=NewProfile(p.provider).id;return p;}
        void Init(){loading=true;SetWindowTextW(dialog,adding?L"添加媒体来源":L"编辑媒体来源");SendMessageW(Get(Type),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"OpenSubsonic / Navidrome"));SendMessageW(Get(Type),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"OpenList"));SendMessageW(Get(Type),CB_SETCURSEL,static_cast<unsigned>(original.provider),0);
            SetWindowTextW(Get(Name),original.name.c_str());SetWindowTextW(Get(Url),original.url.c_str());SetWindowTextW(Get(User),original.username.c_str());SetWindowTextW(Get(Secret),original.secret.c_str());SetWindowTextW(Get(Root),original.root.c_str());SetWindowTextW(Get(Password),original.directory_password.c_str());Button_SetCheck(Get(Proxy),original.proxy?BST_CHECKED:BST_UNCHECKED);
            for(int id:{Name,Url,User,Secret,Root,Password})SendMessageW(Get(id),EM_SETLIMITTEXT,id==Name?128:id==Url||id==Root?4095:1024,0);
            AuthList(original.auth);loading=false;StatusText(L"凭据由 Windows 当前用户加密保存。");SetTimer(dialog,1,100,nullptr);Center(dialog,GetParent(dialog));}
        void Poll(){if(!test.valid()||test.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;auto error=test.get();EnableWindow(Get(Test),TRUE);if(test_generation==generation)StatusText(error.empty()?L"连接成功，账号和目录可用。":error);}
        void Command(int id,int code){if(loading)return;
            if(id==IDCANCEL){if(cancel)*cancel=true;EndDialog(dialog,IDCANCEL);return;}
            if(id==IDOK){auto p=Edited();auto next=owner.library.Profiles();int index{};
                if(adding){index=static_cast<int>(next.size());next.push_back(p);}else{auto it=std::find_if(next.begin(),next.end(),[&](const auto& v){return v.id==original.id;});if(it==next.end())throw Error{E_INVALIDARG,L"此来源已删除，请重新添加"};index=static_cast<int>(it-next.begin());*it=p;}
                const auto active=owner.library.ActiveProfile();const auto before=owner.library.Profiles();const bool changed_active=!adding&&active<before.size()&&before[active].id==original.id;
                owner.library.Save(next,changed_active?static_cast<unsigned>(index):UINT_MAX);owner.Changed(index);if(cancel)*cancel=true;EndDialog(dialog,IDOK);return;}
            if(id==Test){Poll();if(test.valid())return;auto p=Edited();auto* lib=&owner.library;cancel=std::make_shared<std::atomic<bool>>(false);auto c=cancel;test_generation=++generation;
                test=std::async(std::launch::async,[lib,p,c]{try{lib->Test(p,[c]{return c->load();});return std::wstring{};}catch(const Error& e){return e.message;}catch(...){return std::wstring(L"连接失败");}});EnableWindow(Get(Test),FALSE);StatusText(L"正在测试连接…");return;}
            if(code==EN_CHANGE||code==CBN_SELCHANGE||(id==Proxy&&code==BN_CLICKED)){++generation;if(cancel)*cancel=true;if(id==Type){AuthList(ProviderValue()==Provider::OpenList?Auth::Guest:Auth::Password);}else if(id==AuthBox)AuthControls();}
        }
        static INT_PTR CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){auto* e=reinterpret_cast<Editor*>(GetWindowLongPtrW(h,DWLP_USER));if(m==WM_INITDIALOG){e=reinterpret_cast<Editor*>(l);e->dialog=h;SetWindowLongPtrW(h,DWLP_USER,l);}if(!e)return FALSE;
            try{switch(m){case WM_INITDIALOG:e->Init();return TRUE;case WM_COMMAND:e->Command(LOWORD(w),HIWORD(w));return TRUE;case WM_TIMER:e->Poll();return TRUE;case WM_CLOSE:e->Command(IDCANCEL,0);return TRUE;case WM_DESTROY:KillTimer(h,1);if(e->cancel)*e->cancel=true;return TRUE;}}
            catch(const Error& error){e->StatusText(error.message);}catch(...){e->StatusText(L"操作失败，请检查配置");}return FALSE;}
    };
    void OpenEditor(bool adding){const int selected=Selected();if(!adding&&(selected<0||static_cast<size_t>(selected)>=profiles.size()))return;
        Editor editor{*this,adding?NewProfile(Provider::Subsonic):profiles[selected],adding};DialogBoxParamW(Module(),MAKEINTRESOURCEW(303),GetAncestor(window,GA_ROOT),Editor::Proc,reinterpret_cast<LPARAM>(&editor));}
    void Command(int id){const int n=Selected();if(id==Add){OpenEditor(true);return;}if(id==Edit){OpenEditor(false);return;}
        if(n<0||static_cast<size_t>(n)>=profiles.size())return;auto next=library.Profiles();auto it=std::find_if(next.begin(),next.end(),[&](const auto& p){return p.id==profiles[n].id;});if(it==next.end()){Fill();return;}int index=static_cast<int>(it-next.begin());
        if(id==Remove)next.erase(it);else if(id==Up&&index>0){std::swap(next[index],next[index-1]);--index;}else if(id==Down&&static_cast<size_t>(index+1)<next.size()){std::swap(next[index],next[index+1]);++index;}else return;
        library.Save(next);Changed(index);SetFocus(list);}
    void Layout(){if(!window)return;RECT r{};GetClientRect(window,&r);RECT unit{0,0,4,8};if(!MapDialogRect(window,&unit)){unit.right=6;unit.bottom=13;}
        const auto x=[&](int n){return MulDiv(n,unit.right,4);};const auto y=[&](int n){return MulDiv(n,unit.bottom,8);};
        const int right=r.right-x(8+15),bottom=r.bottom-y(24),list_width=std::max(30,right-x(8+4));
        struct Position{HWND h;int x,y,w,hgt;};std::vector<Position> changes;
        const auto move=[&](HWND h,int left,int top,int width,int height){RECT old{};GetWindowRect(h,&old);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&old),2);if(old.left==left&&old.top==top&&old.right-old.left==width&&old.bottom-old.top==height)return;
            changes.push_back({h,left,top,width,height});};
        move(list,x(8),y(8),list_width,std::max(30,bottom-y(8)));
        for(int n=0;n<4;++n)move(GetDlgItem(window,Add+n),right,y(8+n*14),x(15),y(14));
        move(GetDlgItem(window,Edit),right,std::max(y(64),bottom-y(14)),x(15),y(14));
        move(status,x(8),r.bottom-y(19),std::max(30L,r.right-x(16)),y(13));
        if(!changes.empty()){HDWP positions=BeginDeferWindowPos(static_cast<int>(changes.size()));for(const auto& p:changes){if(!positions)break;positions=DeferWindowPos(positions,p.h,nullptr,p.x,p.y,p.w,p.hgt,SWP_NOZORDER|SWP_NOACTIVATE);}
            if(!positions||!EndDeferWindowPos(positions))for(const auto& p:changes)SetWindowPos(p.h,nullptr,p.x,p.y,p.w,p.hgt,SWP_NOZORDER|SWP_NOACTIVATE);}
        RECT inner{};GetClientRect(list,&inner);const int width=std::max(0L,inner.right)-GetSystemMetrics(SM_CXVSCROLL);const int columns[]={std::max(40,width*3/5),std::max(40,width-width*3/5)};
        for(int n=0;n<2;++n)if(ListView_GetColumnWidth(list,n)!=columns[n])ListView_SetColumnWidth(list,n,columns[n]);}
    void Init(){font=reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0));
        list=GetDlgItem(window,SourceList);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);
        int col=0;for(auto title:{L"名称",L"类型"}){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<wchar_t*>(title);c.cx=120;ListView_InsertColumn(list,col++,&c);}
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,window,nullptr,Module(),nullptr);
        const wchar_t* labels[]={L"添加",L"删除",L"上移",L"下移",L"编辑"};for(int n=0;n<5;++n){const HWND button=GetDlgItem(window,Add+n);InstallButtonImage(button,n);TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window;info.uId=reinterpret_cast<UINT_PTR>(button);info.lpszText=const_cast<wchar_t*>(labels[n]);SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));}
        status=GetDlgItem(window,Status);Fill();Layout();}
    static INT_PTR CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){auto* s=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));if(m==WM_INITDIALOG){s=reinterpret_cast<Impl*>(l);s->window=h;SetWindowLongPtrW(h,DWLP_USER,l);}if(!s)return FALSE;
        try{switch(m){case WM_INITDIALOG:s->Init();return TRUE;case WM_SIZE:s->Layout();return TRUE;case WM_COMMAND:s->Command(LOWORD(w));return TRUE;
            case WM_DRAWITEM:return DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l))?TRUE:FALSE;
            case WM_NOTIFY:{auto* n=reinterpret_cast<NMHDR*>(l);if(n->idFrom==SourceList){if(n->code==LVN_ITEMCHANGED&&!s->filling)s->Buttons();else if(n->code==NM_DBLCLK)s->OpenEditor(false);else if(n->code==LVN_KEYDOWN){const auto key=reinterpret_cast<NMLVKEYDOWN*>(l)->wVKey;if(key==VK_RETURN||key==VK_F2)s->OpenEditor(false);else if(key==VK_DELETE)s->Command(Remove);else if(GetKeyState(VK_CONTROL)<0&&(key==VK_UP||key==VK_DOWN))s->Command(key==VK_UP?Up:Down);}}return 0;}
            case WM_DESTROY:if(s->tooltip)DestroyWindow(s->tooltip);s->tooltip=nullptr;break;case WM_NCDESTROY:s->window=nullptr;SetWindowLongPtrW(h,DWLP_USER,0);break;}}
        catch(const Error& e){s->StatusText(e.message);}catch(...){s->StatusText(L"操作失败");}return FALSE;}
};
MediaSettings::MediaSettings(Library& l,std::function<void()> fn):impl_(std::make_unique<Impl>(l,std::move(fn))){}
MediaSettings::~MediaSettings()=default;
HWND MediaSettings::Create(HWND parent){if(impl_->window)return impl_->window;INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&cc);
    const auto page=CreateDialogParamW(Module(),MAKEINTRESOURCEW(304),parent,Impl::Proc,reinterpret_cast<LPARAM>(impl_.get()));
    // Dialog creation starts behind existing sibling controls. The host's tab
    // occupies the whole page; bring the clipped child above it before layout.
    if(page)SetWindowPos(page,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    return page;}
HRESULT MediaSettings::Commit(){return S_OK;}
}
