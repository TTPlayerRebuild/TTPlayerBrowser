#include "media_library.h"
#include "ttp_https.h"
#include "ttplayer/core/text.h"
#include <wininet.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <cwctype>
#include <set>
#include <optional>

namespace ttpbrowser::media {
namespace {
constexpr wchar_t prefix[]=L"https://ttpbrowser.invalid/v1/";
constexpr unsigned page_size=100;
void Require(bool ok,const wchar_t* message,HRESULT hr=E_INVALIDARG) { if(!ok)throw Error{hr,message}; }
void CheckCancel(const Cancel& cancel) { if(cancel && cancel())throw Error{HRESULT_FROM_WIN32(ERROR_CANCELLED),L"操作已取消"}; }
struct Internet { HINTERNET h{}; ~Internet(){if(h)InternetCloseHandle(h);} };
struct RequestGuard {
    const Cancel& cancel;DWORD start{GetTickCount()};bool timed_out{};
    static int __cdecl Canceled(void* context) noexcept {
        auto& self=*static_cast<RequestGuard*>(context);
        self.timed_out=GetTickCount()-self.start>=20000;
        try{return self.timed_out || (self.cancel && self.cancel());}catch(...){return 1;}
    }
    void Check(){if(Canceled(this))throw Error{HRESULT_FROM_WIN32(timed_out?ERROR_TIMEOUT:ERROR_CANCELLED),timed_out?L"服务器响应超时":L"操作已取消"};}
};
void CheckHttpStatus(unsigned status) {
    Require(status>=200&&status<300,status>=300&&status<400?L"服务返回了重定向，请在设置中填写最终服务地址":L"HTTP 请求失败，请检查权限和服务地址",HRESULT_FROM_WIN32(status==401||status==403?ERROR_ACCESS_DENIED:ERROR_BAD_NET_RESP));
}
std::optional<std::string> PortableRequest(const std::wstring& url,const std::string& body,
    const std::wstring& token,RequestGuard& guard) {
    wchar_t executable[32768]{};
    const auto size=GetModuleFileNameW(nullptr,executable,32768);
    if(!size || size>=32768)return std::nullopt;
    const auto path=std::filesystem::path(executable).parent_path()/L"AddIn"/L"ttp_https.dll";
    struct Module {HMODULE h;~Module(){if(h)FreeLibrary(h);}} module{LoadLibraryExW(path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH)};
    if(!module.h)return std::nullopt;
    const auto get=reinterpret_cast<ttp_https_get_api_fn>(GetProcAddress(module.h,"ttp_https_get_api"));
    const auto* base=get?get(TTP_HTTPS_REQUEST_ABI_VERSION):nullptr;
    if(!base || base->size<sizeof(ttp_https_api_v8) || base->abi_version!=TTP_HTTPS_REQUEST_ABI_VERSION)return std::nullopt;
    const auto* api=reinterpret_cast<const ttp_https_api_v8*>(base);
    if(!api->stream_open_ex || !api->base.stream_read || !api->base.stream_close)return std::nullopt;
    const auto auth=Utf8(token);
    ttp_https_stream_request_ex request{};request.size=sizeof(request);
    auto& stream=request.stream;stream.size=sizeof(stream);stream.timeout_ms=5000;stream.auto_detect=-1;
    auto& network=stream.request;network.size=sizeof(network);network.url=url.c_str();network.proxy_type=1;
    network.canceled=RequestGuard::Canceled;network.cancel_context=&guard;
    request.method=body.empty()?"GET":"POST";request.user_agent="TTPlayer-Media/1.0";
    request.accept="application/json, image/*";request.authorization=auth.c_str();
    request.content_type=body.empty()?nullptr:"application/json; charset=utf-8";
    request.body=reinterpret_cast<const unsigned char*>(body.data());request.body_size=static_cast<uint32_t>(body.size());
    struct Handle {const ttp_https_api_v7& api;ttp_https_stream* h{};~Handle(){if(h)api.stream_close(h);}} handle{api->base};
    unsigned status{};char error[512]{};
    const auto result=api->stream_open_ex(&request,&handle.h,&status,error,sizeof(error));
    guard.Check();
    if(result==TTP_HTTPS_USE_WINHTTP && !handle.h)return std::nullopt;
    const auto check=[&](int code){guard.Check();if(code!=TTP_HTTPS_OK)throw Error{
        HRESULT_FROM_WIN32(code==TTP_HTTPS_CANCELED?ERROR_CANCELLED:code==TTP_HTTPS_TIMEOUT?ERROR_TIMEOUT:code==TTP_HTTPS_TRUNCATED?ERROR_HANDLE_EOF:ERROR_BAD_NET_RESP),
        L"ttp_https 请求失败，请检查服务地址、网络和证书"};};
    check(result);Require(handle.h!=nullptr,L"ttp_https 返回了无效响应",E_FAIL);CheckHttpStatus(status);
    std::string content;std::array<char,16384> buffer{};
    for(;;){guard.Check();uint32_t got{};check(api->base.stream_read(handle.h,buffer.data(),static_cast<uint32_t>(buffer.size()),&got,error,sizeof(error)));
        Require(got<=buffer.size(),L"ttp_https 返回了无效响应",E_FAIL);if(!got)break;
        Require(content.size()+got<=8*1024*1024,L"服务响应过大，请缩小查询范围");content.append(buffer.data(),got);}
    return content;
}
struct HttpUrl {std::wstring host,path;INTERNET_SCHEME scheme;INTERNET_PORT port;};
HttpUrl ParseHttpUrl(const std::wstring& url) {
    // InternetCrackUrl's pointer-into-input mode requires Vista or later.
    // Supply real output buffers so XP also returns absent credentials as empty.
    const auto size=static_cast<DWORD>(url.size()+1);
    std::vector<wchar_t> storage(static_cast<size_t>(size)*5);
    URL_COMPONENTSW c{sizeof(c)};
    c.lpszHostName=storage.data();c.dwHostNameLength=size;
    c.lpszUserName=storage.data()+size;c.dwUserNameLength=size;
    c.lpszPassword=storage.data()+size*2;c.dwPasswordLength=size;
    c.lpszUrlPath=storage.data()+size*3;c.dwUrlPathLength=size;
    c.lpszExtraInfo=storage.data()+size*4;c.dwExtraInfoLength=size;
    Require(InternetCrackUrlW(url.c_str(),0,0,&c)&&c.dwHostNameLength&&
        (c.nScheme==INTERNET_SCHEME_HTTP||c.nScheme==INTERNET_SCHEME_HTTPS)&&
        !c.dwUserNameLength&&!c.dwPasswordLength,L"服务器地址无效");
    std::wstring path(c.lpszUrlPath,c.dwUrlPathLength);if(path.empty())path=L"/";
    path.append(c.lpszExtraInfo,c.dwExtraInfoLength);
    return {{c.lpszHostName,c.dwHostNameLength},std::move(path),c.nScheme,c.nPort};
}
std::wstring Field(const Json& j,const char* key){return Wide(j[key].String());}
std::wstring Hex(const BYTE* p,size_t size) {
    std::wstring out;for(size_t i=0;i<size;++i){out+=L"0123456789abcdef"[p[i]>>4];out+=L"0123456789abcdef"[p[i]&15];}return out;
}
std::wstring Salt() {
    HCRYPTPROV provider{};Require(CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT)!=FALSE,L"无法初始化认证");
    BYTE bytes[16];const auto ok=CryptGenRandom(provider,sizeof(bytes),bytes);CryptReleaseContext(provider,0);
    Require(ok!=FALSE,L"无法生成认证随机数");return Hex(bytes,sizeof(bytes));
}
std::wstring Md5(const std::wstring& value) {
    HCRYPTPROV provider{};HCRYPTHASH hash{};BYTE digest[16];DWORD count=sizeof(digest);
    Require(CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT)!=FALSE,L"无法初始化认证");
    bool ok=CryptCreateHash(provider,CALG_MD5,0,0,&hash)!=FALSE;
    const auto bytes=Utf8(value);
    if(ok)ok=CryptHashData(hash,reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),0) && CryptGetHashParam(hash,HP_HASHVAL,digest,&count,0);
    if(hash)CryptDestroyHash(hash);CryptReleaseContext(provider,0);
    Require(ok,L"无法计算认证令牌");return Hex(digest,sizeof(digest));
}
std::wstring ProfileKey(const Profile& p) {
    // Changing server/account must not reinterpret an existing playlist entry
    // or attach it to a live HTTP producer for the previous server/account.
    return p.id+L"-"+Md5(NormalizeUrl(p.url)+L"\n"+p.username);
}
std::wstring Join(std::wstring parent,const std::wstring& name) {
    Require(!name.empty() && name!=L"." && name!=L".." && name.find_first_of(L"/\\\r\n")==name.npos,L"服务器返回了无效文件名");
    if(parent.empty())parent=L"/";if(parent.back()!=L'/')parent+=L'/';return parent+name;
}
bool Audio(const std::wstring& name) {
    auto extension=std::filesystem::path(name).extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),towlower);
    static constexpr std::wstring_view types=L"|.mp3|.flac|.wav|.m4a|.mp4|.aac|.ogg|.opus|.ape|.wma|.aiff|.aif|.alac|.wv|.tta|.tak|.dsf|.dff|.ac3|.dts|.mka|.webm|.mpc|.mod|.s3m|.it|.xm|.mid|.ncm|";
    return !extension.empty() && types.find(L"|"+extension+L"|")!=types.npos;
}
Item Song(const Json& j) {
    Item i;i.id=Field(j,"id");i.title=DisplayMetadata(Field(j,"title"));i.artist=DisplayMetadata(Field(j,"artist"));i.album=DisplayMetadata(Field(j,"album"));
    i.suffix=Field(j,"suffix");i.cover=Field(j,"coverArt");i.duration=j["duration"].Integer();i.size=j["size"].Integer();return i;
}
void Append(Page& page,const Json& array,Kind kind) {
    for(const auto& j:array.items) {
        auto i=Song(j);i.kind=kind;if(kind!=Kind::Song)i.title=DisplayMetadata(Field(j,"name"));
        if(!i.id.empty())page.items.push_back(std::move(i));
    }
}
Json Parse(const std::string& body) {
    try{return ttplayer::core::JsonReader(body,8*1024*1024).Read();}
    catch(...){throw Error{HRESULT_FROM_WIN32(ERROR_INVALID_DATA),L"服务返回的数据格式不正确"};}
}
}
std::string Utf8(const std::wstring& value){return ttplayer::core::WideToUtf8(value);}
std::wstring Wide(const std::string& value){return ttplayer::core::Utf8ToWide(value);}
std::wstring DisplayMetadata(const std::wstring& value) {
    // ID3 encoding 0 is frequently GBK in old Chinese files. Navidrome
    // correctly transports its Latin-1 interpretation as UTF-8; changing the
    // JSON decoder would corrupt already correct Chinese/Japanese metadata.
    std::string bytes;unsigned high{},run{},longest{};
    for(wchar_t c:value){if(c>255)return value;bytes+=static_cast<char>(c);if(c>=128){++high;longest=std::max(longest,++run);}else run=0;}
    if(high<4||longest<4)return value;
    const int n=MultiByteToWideChar(936,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);if(n<=0)return value;
    std::wstring decoded(n,L'\0');if(!MultiByteToWideChar(936,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),decoded.data(),n))return value;
    const auto chinese=std::count_if(decoded.begin(),decoded.end(),[](wchar_t c){return c>=0x4e00&&c<=0x9fff;});if(chinese<2||chinese*2+2<high)return value;
    BOOL replaced{};const int size=WideCharToMultiByte(936,WC_NO_BEST_FIT_CHARS,decoded.data(),n,nullptr,0,nullptr,&replaced);if(size!=static_cast<int>(bytes.size())||replaced)return value;
    std::string back(size,'\0');WideCharToMultiByte(936,WC_NO_BEST_FIT_CHARS,decoded.data(),n,back.data(),size,nullptr,&replaced);
    return !replaced&&back==bytes?decoded:value;
}
std::string Quote(const std::wstring& value) {
    std::string result="\"";for(unsigned char c:Utf8(value)) {
        if(c=='"'||c=='\\'){result+='\\';result+=char(c);}
        else if(c<32){result+="\\u00";result+="0123456789abcdef"[c>>4];result+="0123456789abcdef"[c&15];}
        else result+=char(c);
    }return result+'"';
}
std::wstring Encode(const std::wstring& value,bool path) {
    std::wstring result;for(unsigned char c:Utf8(value)) {
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~'||(path&&c=='/'))result+=wchar_t(c);
        else {result+=L'%';result+=L"0123456789ABCDEF"[c>>4];result+=L"0123456789ABCDEF"[c&15];}
    }return result;
}
std::wstring Decode(const std::wstring& value) {
    std::string result;auto digit=[](wchar_t c){return c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:c>=L'A'&&c<=L'F'?c-L'A'+10:-1;};
    for(size_t n=0;n<value.size();++n) {
        const auto c=value[n];if(c!=L'%'){Require(c>0&&c<128,L"无效媒体标识");result+=char(c);continue;}
        Require(n+2<value.size(),L"无效媒体标识");int a=digit(value[++n]),b=digit(value[++n]);
        Require(a>=0&&b>=0&&(a||b),L"无效媒体标识");result+=char(a*16+b);
    }
    if(!result.empty())Require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,result.data(),static_cast<int>(result.size()),nullptr,0)>0,L"无效 UTF-8 媒体标识");
    return Wide(result);
}
std::wstring NormalizeUrl(std::wstring url) {
    while(!url.empty()&&iswspace(url.back()))url.pop_back();while(!url.empty()&&iswspace(url.front()))url.erase(0,1);
    while(!url.empty()&&url.back()==L'/')url.pop_back();
    Require(!url.empty()&&url.size()<4096&&url.find_first_of(L"\r\n?#")==url.npos,L"请填写服务的 HTTP(S) 地址，不包含查询参数");
    ParseHttpUrl(url);return url;
}
bool IsSource(const std::wstring& source){return source.starts_with(prefix);}
std::wstring Source(const Profile& p,const Item& item,bool download) {
    auto ext=item.suffix;
    if(ext.empty()&&p.provider==Provider::OpenList){ext=std::filesystem::path(item.title).extension().wstring();if(!ext.empty())ext.erase(0,1);}
    if(ext.empty()||ext.size()>12||!std::all_of(ext.begin(),ext.end(),[](wchar_t c){return iswalnum(c)!=0;}))ext=L"mp3";
    return prefix+Encode(ProfileKey(p))+L"/"+(download?L"download/":L"play/")+Encode(item.id)+L"/audio."+ext;
}
std::string Request(const std::wstring& url,const std::string& body,const std::wstring& token,const Cancel& cancel) {
    RequestGuard guard{cancel};guard.Check();
    Require(url.size()<=8192&&url.find_first_of(L"\r\n")==url.npos&&url.find(L'\0')==url.npos&&
        body.size()<=1024*1024&&token.size()<=16384&&std::all_of(token.begin(),token.end(),[](wchar_t c){return c>=32&&c<=126;}),L"请求参数无效");
    const auto parsed=ParseHttpUrl(url);
    if(auto result=PortableRequest(url,body,token,guard))return std::move(*result);
    guard.Check();
    Internet session{InternetOpenW(L"TTPlayer-Media/1.0",INTERNET_OPEN_TYPE_PRECONFIG,nullptr,nullptr,0)};
    Require(session.h!=nullptr,L"无法初始化网络",E_FAIL);
    DWORD timeout=5000;for(DWORD option:{INTERNET_OPTION_CONNECT_TIMEOUT,INTERNET_OPTION_RECEIVE_TIMEOUT,INTERNET_OPTION_SEND_TIMEOUT})InternetSetOptionW(session.h,option,&timeout,sizeof(timeout));
    Internet connection{InternetConnectW(session.h,parsed.host.c_str(),parsed.port,nullptr,nullptr,INTERNET_SERVICE_HTTP,0,0)};
    Require(connection.h!=nullptr,L"无法连接服务器",HRESULT_FROM_WIN32(GetLastError()));
    const wchar_t* accept[]={L"*/*",nullptr};
    DWORD flags=INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_NO_COOKIES|INTERNET_FLAG_NO_AUTH|INTERNET_FLAG_NO_AUTO_REDIRECT|INTERNET_FLAG_NO_UI;
    if(parsed.scheme==INTERNET_SCHEME_HTTPS)flags|=INTERNET_FLAG_SECURE;
    Internet request{HttpOpenRequestW(connection.h,body.empty()?L"GET":L"POST",parsed.path.c_str(),nullptr,nullptr,accept,flags,0)};
    Require(request.h!=nullptr,L"无法建立请求",HRESULT_FROM_WIN32(GetLastError()));
    std::wstring headers=L"Accept: application/json, image/*\r\n";
    if(!body.empty())headers+=L"Content-Type: application/json; charset=utf-8\r\n";
    if(!token.empty())headers+=L"Authorization: "+token+L"\r\n";
    guard.Check();
    Require(HttpSendRequestW(request.h,headers.c_str(),static_cast<DWORD>(headers.size()),body.empty()?nullptr:const_cast<char*>(body.data()),static_cast<DWORD>(body.size()))!=FALSE,L"连接失败，请检查服务地址、网络和证书",HRESULT_FROM_WIN32(GetLastError()));
    guard.Check();DWORD status{},count=sizeof(status);HttpQueryInfoW(request.h,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,&status,&count,nullptr);CheckHttpStatus(status);
    std::string result;std::array<char,16384> buffer{};
    for(;;){guard.Check();DWORD got{};
        Require(InternetReadFile(request.h,buffer.data(),static_cast<DWORD>(buffer.size()),&got)!=FALSE,L"读取响应失败",E_FAIL);
        if(!got)break;Require(result.size()+got<=8*1024*1024,L"服务响应过大，请缩小查询范围");result.append(buffer.data(),got);}
    return result;
}

Library::Library(std::filesystem::path state_file,Transport transport):file_(state_file.parent_path()/L"MediaLibrary.dat"),transport_(std::move(transport)) {
    profiles_={{Provider::Subsonic,Auth::Password,L"subsonic",L"OpenSubsonic",L"http://127.0.0.1:4533"},
               {Provider::OpenList,Auth::Guest,L"openlist",L"OpenList",L"http://127.0.0.1:5244"}};
    if(!std::filesystem::exists(file_))return;
    try {
        Require(std::filesystem::file_size(file_)<=1024*1024,L"配置文件过大");std::ifstream stream(file_,std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(stream)),{});DATA_BLOB in{static_cast<DWORD>(raw.size()),reinterpret_cast<BYTE*>(raw.data())},out{};
        Require(CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)!=FALSE,L"无法解密媒体库配置，请重新填写凭据");
        std::string plain(reinterpret_cast<char*>(out.pbData),out.cbData);SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);
        const auto j=Parse(plain);SecureZeroMemory(plain.data(),plain.size());
        Require(j["profiles"].kind==Json::array,L"媒体来源配置无效");
        std::vector<Profile> loaded;std::set<std::wstring> ids;
        for(const auto& node:j["profiles"].items) {
            Require(loaded.size()<64,L"媒体来源过多");Profile p;p.id=Field(node,"id");
            Require(!p.id.empty()&&p.id.size()<=80&&p.id.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==p.id.npos&&ids.insert(p.id).second,L"媒体来源标识无效");
            const auto provider=node["provider"].kind==Json::number?node["provider"].Integer():p.id==L"openlist"?1:0;Require(provider<=1,L"媒体来源类型无效");p.provider=static_cast<Provider>(provider);
            p.name=Field(node,"name");
            if(p.name.empty()||(p.provider==Provider::Subsonic&&
                (p.name==L"OpenSubsonic / Navidrome"||p.name==L"Navidrome / OpenSubsonic")))
                p.name=p.provider==Provider::OpenList?L"OpenList":L"OpenSubsonic";
            p.url=NormalizeUrl(Field(node,"url"));p.username=Field(node,"username");p.secret=Field(node,"secret");p.root=Field(node,"root");
            p.directory_password=Field(node,"directoryPassword");p.proxy=node["proxy"].True();
            const auto auth=node["auth"].Integer();Require(auth<=3,L"无效认证方式");p.auth=static_cast<Auth>(auth);loaded.push_back(std::move(p));
        }
        const auto active=Field(j,"activeId");unsigned index=0;
        if(!active.empty()){auto it=std::find_if(loaded.begin(),loaded.end(),[&](const auto& p){return p.id==active;});if(it!=loaded.end())index=static_cast<unsigned>(it-loaded.begin());}
        else if(j["active"].Integer()<loaded.size())index=static_cast<unsigned>(j["active"].Integer());
        profiles_=std::move(loaded);active_profile_=index;
    }catch(const Error& e){load_error_=e.message;}catch(...){load_error_=L"媒体库配置读取失败，请重新配置";}
}
std::vector<Profile> Library::Profiles() const {std::lock_guard lock(mutex_);return profiles_;}
unsigned Library::ActiveProfile() const {std::lock_guard lock(mutex_);return active_profile_;}
Profile Library::Find(const std::wstring& id) const {for(const auto& p:Profiles())if(ProfileKey(p)==id)return p;throw Error{E_INVALIDARG,L"媒体来源不存在或服务器账号已经更改，请重新添加歌曲"};}
void Library::Save(const std::vector<Profile>& profiles,unsigned active) {
    const auto previous=Profiles();const auto current=ActiveProfile();
    if(active==UINT_MAX){active=0;const auto id=current<previous.size()?previous[current].id:L"";auto it=std::find_if(profiles.begin(),profiles.end(),[&](const auto& p){return p.id==id;});if(it!=profiles.end())active=static_cast<unsigned>(it-profiles.begin());else if(!profiles.empty())active=std::min(current,static_cast<unsigned>(profiles.size()-1));}
    Require(profiles.size()<=64&&(profiles.empty()?active==0:active<profiles.size()),L"媒体来源最多 64 个");std::set<std::wstring> ids;
    std::string json="{\"version\":2,\"activeId\":"+Quote(profiles.empty()?L"":profiles[active].id)+",\"profiles\":[";bool first=true;
    for(const auto& p:profiles){NormalizeUrl(p.url);
        Require(!p.id.empty()&&p.id.size()<=80&&p.id.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==p.id.npos&&ids.insert(p.id).second,L"无效或重复的来源标识");
        Require(!p.name.empty()&&p.name.size()<=128&&p.name.find_first_of(L"\r\n\t")==p.name.npos,L"请填写有效的来源名称（最多 128 字）");
        Require(static_cast<unsigned>(p.provider)<=1,L"无效来源类型");const auto auth=static_cast<unsigned>(p.auth);
        Require(auth<=3&&(p.provider==Provider::Subsonic?auth<=1:auth!=1),L"认证方式不适用于此来源类型");
        for(const auto& old:previous)if(old.id==p.id)Require(old.provider==p.provider,L"更换类型时需生成新的来源标识");
        Require(p.root.size()<4096&&p.root.find_first_of(L"\r\n\\")==p.root.npos,L"目录路径无效");
        if(!first)json+=',';first=false;json+="{\"id\":"+Quote(p.id)+",\"name\":"+Quote(p.name)+",\"provider\":"+std::to_string(static_cast<unsigned>(p.provider))+",\"url\":"+Quote(p.url)+",\"username\":"+Quote(p.username)+",\"secret\":"+Quote(p.secret)+",\"root\":"+Quote(p.root)+",\"directoryPassword\":"+Quote(p.directory_password)+",\"auth\":"+std::to_string(auth)+",\"proxy\":"+(p.proxy?"true":"false")+"}";
    }json+="]}";DATA_BLOB in{static_cast<DWORD>(json.size()),reinterpret_cast<BYTE*>(json.data())},out{};
    Require(CryptProtectData(&in,L"TTPlayer personal media",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)!=FALSE,L"凭据加密失败",E_FAIL);
    SecureZeroMemory(json.data(),json.size());std::vector<BYTE> encrypted(out.pbData,out.pbData+out.cbData);LocalFree(out.pbData);
    std::filesystem::create_directories(file_.parent_path());const auto temp=file_.wstring()+L".tmp";
    {std::ofstream stream(temp,std::ios::binary|std::ios::trunc);stream.write(reinterpret_cast<const char*>(encrypted.data()),encrypted.size());stream.close();Require(!stream.fail(),L"配置保存失败",STG_E_WRITEFAULT);}
    Require(MoveFileExW(temp.c_str(),file_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,L"配置保存失败",STG_E_WRITEFAULT);
    std::lock_guard lock(mutex_);profiles_=profiles;active_profile_=active;sessions_.clear();load_error_.clear();
}
Profile NewProfile(Provider provider){Profile p;p.provider=provider;p.id=L"source-"+Salt();p.name=provider==Provider::OpenList?L"OpenList":L"OpenSubsonic";p.url=provider==Provider::OpenList?L"http://127.0.0.1:5244":L"http://127.0.0.1:4533";p.auth=provider==Provider::OpenList?Auth::Guest:Auth::Password;return p;}
void Library::SelectProfile(const std::wstring& id){auto profiles=Profiles();auto it=std::find_if(profiles.begin(),profiles.end(),[&](const auto& p){return p.id==id;});Require(it!=profiles.end(),L"媒体来源已不存在，请重新选择");Save(profiles,static_cast<unsigned>(it-profiles.begin()));}
std::wstring Library::SubsonicUrl(const Profile& p,const wchar_t* method,const std::wstring& args) {
    auto base=NormalizeUrl(p.url);if(!base.ends_with(L"/rest")&&!base.ends_with(L"/@rest"))base+=L"/rest";
    std::wstring auth;
    if(p.auth==Auth::ApiKey){Require(!p.secret.empty(),L"请填写 OpenSubsonic API 密钥");auth=L"&apiKey="+Encode(p.secret);}
    else {Require(!p.username.empty()&&!p.secret.empty(),L"请填写 OpenSubsonic 用户名和密码");const auto salt=Salt();auth=L"&u="+Encode(p.username)+L"&t="+Md5(p.secret+salt)+L"&s="+salt;}
    return base+L"/"+method+L".view?v=1.16.1&c=TTPlayerRebuild&f=json"+auth+args;
}
Json Library::Subsonic(const Profile& p,const wchar_t* method,const std::wstring& args,const Cancel& cancel) {
    CheckCancel(cancel);auto result=Parse(transport_(SubsonicUrl(p,method,args),{}, {},cancel));auto root=result["subsonic-response"];
    if(root["status"].String()!="ok")throw Error{HRESULT_FROM_WIN32(ERROR_BAD_NET_RESP),L"OpenSubsonic 请求失败（代码 "+std::to_wstring(root["error"]["code"].Integer())+L"），请检查账号、权限与接口支持"};
    return root;
}
Json Library::OpenList(const Profile& p,const wchar_t* method,const std::string& body,const Cancel& cancel) {
    auto base=NormalizeUrl(p.url);std::wstring token;
    const auto credentials=p.url+L"\n"+p.username+L"\n"+p.secret;
    auto login=[&]{
        Require(!p.username.empty()&&!p.secret.empty(),L"请填写 OpenList 用户名和密码");
        const auto j=Parse(transport_(base+L"/api/auth/login","{\"username\":"+Quote(p.username)+",\"password\":"+Quote(p.secret)+"}",{},cancel));
        Require(j["code"].Integer()==200,L"OpenList 登录失败；启用两步验证时请使用已登录令牌",E_ACCESSDENIED);
        token=Field(j["data"],"token");Require(!token.empty(),L"OpenList 未返回登录令牌",E_ACCESSDENIED);
        std::lock_guard lock(mutex_);sessions_[p.id]={credentials,token};
    };
    if(p.auth==Auth::Token)token=p.secret;
    else if(p.auth==Auth::Password){ {std::lock_guard lock(mutex_);const auto it=sessions_.find(p.id);if(it!=sessions_.end()&&it->second.credentials==credentials)token=it->second.token;}if(token.empty())login(); }
    CheckCancel(cancel);auto result=Parse(transport_(base+L"/api/"+method,body,token,cancel));
    if(result["code"].Integer()==401&&p.auth==Auth::Password){login();result=Parse(transport_(base+L"/api/"+method,body,token,cancel));}
    if(result["code"].Integer()!=200)throw Error{E_ACCESSDENIED,L"OpenList 请求失败（代码 "+std::to_wstring(result["code"].Integer())+L"）；请检查目录密码、访问权限或搜索索引"};
    return result["data"];
}
void Library::Test(const Profile& p,const Cancel& cancel) {
    if(p.provider==Provider::Subsonic)Subsonic(p,L"ping",{},cancel);
    else OpenList(p,L"fs/list","{\"path\":"+Quote(p.root.empty()?L"/":p.root)+",\"password\":"+Quote(p.directory_password)+",\"page\":1,\"per_page\":1,\"refresh\":false}",cancel);
}
Page Library::Browse(const Profile& p,const Query& q,const Cancel& cancel) {
    CheckCancel(cancel);Require(q.page<10000,L"页码超出限制");Page page;
    const auto offset=std::to_wstring(q.page*page_size), id=L"&id="+Encode(q.id);
    if(p.provider==Provider::OpenList) {
        const auto path=q.id.empty()?(p.root.empty()?L"/":p.root):q.id;
        const bool search=q.view==View::Search;
        auto body=search?"{\"parent\":"+Quote(path)+",\"keywords\":"+Quote(q.text)+",\"scope\":0":"{\"path\":"+Quote(path)+",\"refresh\":false";
        body+=",\"password\":"+Quote(p.directory_password)+",\"page\":"+std::to_string(q.page+1)+",\"per_page\":100}";
        auto j=OpenList(p,search?L"fs/search":L"fs/list",body,cancel);page.more=(q.page+1)*page_size<j["total"].Integer();page.description=path;
        for(const auto& node:j["content"].items) {
            Item i;i.title=Field(node,"name");i.kind=node["is_dir"].True()?Kind::Directory:Kind::Song;
            if(i.kind==Kind::Song&&!Audio(i.title))continue;
            i.id=Join(search?Field(node,"parent"):path,i.title);i.size=node["size"].Integer();page.items.push_back(std::move(i));
        }return page;
    }
    Json j;const Json* entries{};Kind kind=Kind::Song;
    switch(q.view) {
    case View::Albums:j=Subsonic(p,L"getAlbumList2",L"&type=newest&size=100&offset="+offset,cancel);entries=&j["albumList2"]["album"];kind=Kind::Album;break;
    case View::Artists:j=Subsonic(p,L"getArtists",{},cancel);for(const auto& index:j["artists"]["index"].items)Append(page,index["artist"],Kind::Artist);break;
    case View::Playlists:j=Subsonic(p,L"getPlaylists",{},cancel);entries=&j["playlists"]["playlist"];kind=Kind::Playlist;break;
    case View::Album:j=Subsonic(p,L"getAlbum",id,cancel);entries=&j["album"]["song"];page.description=DisplayMetadata(Field(j["album"],"name"));break;
    case View::Artist:j=Subsonic(p,L"getArtist",id,cancel);entries=&j["artist"]["album"];kind=Kind::Album;page.description=DisplayMetadata(Field(j["artist"],"name"));break;
    case View::Playlist:j=Subsonic(p,L"getPlaylist",id,cancel);entries=&j["playlist"]["entry"];page.description=DisplayMetadata(Field(j["playlist"],"name"));break;
    case View::Starred:j=Subsonic(p,L"getStarred2",{},cancel);entries=&j["starred2"]["song"];break;
    default:j=Subsonic(p,L"search3",L"&query="+Encode(q.text)+L"&artistCount=0&albumCount=0&songCount=100&songOffset="+offset,cancel);entries=&j["searchResult3"]["song"];break;
    }
    if(entries)Append(page,*entries,kind);
    if(q.view==View::Albums||q.view==View::Search||q.view==View::Songs)page.more=page.items.size()==page_size;
    else {const size_t start=std::min(page.items.size(),size_t(q.page)*page_size),end=std::min(page.items.size(),start+page_size);page.more=end<page.items.size();page.items=std::vector<Item>(page.items.begin()+start,page.items.begin()+end);}
    return page;
}
std::vector<Item> Library::Playable(const Profile& profile,const Item& container,const Cancel& cancel) {
    Require(profile.provider==Provider::Subsonic&&(container.kind==Kind::Album||container.kind==Kind::Playlist||container.kind==Kind::Artist),L"请选择专辑、歌手或歌单");
    std::vector<Item> songs;std::set<std::wstring> unique;unsigned requests{};const DWORD started=GetTickCount();
    const auto canceled=[&]{return (cancel&&cancel())||GetTickCount()-started>=30000;};
    const auto collect=[&](View view,const std::wstring& id,auto visit){
        for(unsigned page=0;;++page){CheckCancel(canceled);Require(++requests<=64,L"内容较多，请打开后分批播放");
            auto result=Browse(profile,{view,id,L"",page},canceled);CheckCancel(canceled);
            for(const auto& item:result.items)visit(item);if(!result.more)break;
        }
    };
    const auto append=[&](const Item& song){if(song.kind==Kind::Song&&unique.insert(song.id).second){Require(songs.size()<1000,L"歌曲超过 1000 首，请打开后分批播放");songs.push_back(song);}};
    if(container.kind==Kind::Artist)collect(View::Artist,container.id,[&](const Item& album){if(album.kind==Kind::Album)collect(View::Album,album.id,append);});
    else collect(container.kind==Kind::Album?View::Album:View::Playlist,container.id,append);
    Require(!songs.empty(),L"这里没有可播放的歌曲");return songs;
}
std::wstring Library::Resolve(const std::wstring& source,const Cancel& cancel) {
    Require(IsSource(source),L"无效媒体来源");const auto rest=source.substr(std::size(prefix)-1);const auto a=rest.find(L'/'),b=rest.find(L'/',a+1),c=rest.find(L'/',b+1);
    Require(a!=rest.npos&&b!=rest.npos&&c!=rest.npos&&c+1<rest.size(),L"无效媒体标识");
    const auto p=Find(Decode(rest.substr(0,a)));const auto item_id=Decode(rest.substr(b+1,c-b-1));const auto action=rest.substr(a+1,b-a-1);
    Require(!item_id.empty()&&(action==L"play"||action==L"download"),L"无效媒体操作");CheckCancel(cancel);
    if(p.provider==Provider::Subsonic)return SubsonicUrl(p,action==L"download"?L"download":L"stream",L"&id="+Encode(item_id)+(action==L"play"?L"&format=raw":L""));
    auto data=OpenList(p,L"fs/get","{\"path\":"+Quote(item_id)+",\"password\":"+Quote(p.directory_password)+"}",cancel);
    if(p.proxy) {
        // A user with a restricted base_path receives a root-relative path;
        // /p uses the global path, so query /me and prepend that base path.
        const auto user=OpenList(p,L"me",{},cancel);auto base_path=Field(user,"base_path");
        while(!base_path.empty()&&base_path.back()==L'/')base_path.pop_back();
        auto path=item_id;if(path.empty()||path.front()!=L'/')path=L"/"+path;
        return NormalizeUrl(p.url)+L"/p"+Encode(base_path+path,true)+L"?sign="+Encode(Field(data,"sign"));
    }
    Require(data["header"].fields.empty(),L"此存储要求专用请求头，请启用 OpenList 服务器代理");
    const auto raw=Field(data,"raw_url");Require(!HttpOrigin(raw).empty(),L"OpenList 未返回可用下载地址");return raw;
}
std::string Library::Cover(const Profile& p,const std::wstring& id,const Cancel& cancel) {
    if(p.provider!=Provider::Subsonic||id.empty())return {};return transport_(SubsonicUrl(p,L"getCoverArt",L"&id="+Encode(id)+L"&size=160"),{}, {},cancel);
}
std::wstring Library::Lyrics(const Profile& p,const Item& song,const Cancel& cancel) {
    if(p.provider!=Provider::Subsonic)return {};
    try {
        const auto capabilities=Subsonic(p,L"getOpenSubsonicExtensions",{},cancel);
        const auto& extensions=capabilities["openSubsonicExtensions"].items;
        const bool supported=std::any_of(extensions.begin(),extensions.end(),[](const Json& e){return e["name"].String()=="songLyrics";});
        if(supported){const auto j=Subsonic(p,L"getLyricsBySongId",L"&id="+Encode(song.id),cancel);
            for(const auto& lyric:j["lyricsList"]["structuredLyrics"].items){std::wstring text;
                for(const auto& line:lyric["line"].items){text+=Field(line,"value");text+=L"\r\n";}
                if(!text.empty())return text;}
        }
    }catch(const Error&){CheckCancel(cancel);}
    auto j=Subsonic(p,L"getLyrics",L"&artist="+Encode(song.artist)+L"&title="+Encode(song.title),cancel);return Field(j["lyrics"],"value");
}
}
