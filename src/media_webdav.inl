// Included in media_library.cpp: shares URL validation and audio filtering.
namespace {
const wchar_t* ProviderName(Provider p){return p==Provider::WebDav?L"WebDAV":p==Provider::OpenList?L"OpenList":L"OpenSubsonic";}
std::wstring DavPath(std::wstring path){
    if(path.empty())path=L"/";
    Require(path.front()==L'/'&&path.size()<4096&&path.find_first_of(L"\\\r\n\t")==path.npos&&path.find(L'\0')==path.npos,L"WebDAV 目录路径无效");
    while(path.size()>1&&path.back()==L'/')path.pop_back();
    for(size_t start=1;start<path.size();){auto end=path.find(L'/',start);auto part=path.substr(start,end-start);
        Require(!part.empty()&&part!=L"."&&part!=L"..",L"WebDAV 路径不能包含上级目录或空路径段");if(end==path.npos)break;start=end+1;}
    return path;
}
bool DavWithin(const std::wstring& path,const std::wstring& root){return root==L"/"||path==root||(path.starts_with(root)&&path.size()>root.size()&&path[root.size()]==L'/');}
std::wstring DavAuthorization(const Profile& p){
    if(p.auth==Auth::Guest)return {};
    Require(p.auth==Auth::Password&&!p.username.empty()&&p.username.find(L':')==p.username.npos,L"请填写 WebDAV 用户名和密码（Basic）");
    auto bytes=Utf8(p.username+L":"+p.secret);DWORD size{};
    Require(CryptBinaryToStringW(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&size)!=FALSE,L"无法编码 WebDAV 认证");
    std::wstring result(size,L'\0');const bool ok=CryptBinaryToStringW(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,result.data(),&size)!=FALSE;
    SecureZeroMemory(bytes.data(),bytes.size());Require(ok,L"无法编码 WebDAV 认证");result.resize(size);while(!result.empty()&&!result.back())result.pop_back();return L"Basic "+result;
}
std::wstring DavUrl(const Profile& p,const std::wstring& path,bool directory){
    const auto normalized=DavPath(path),root=DavPath(p.root);Require(DavWithin(normalized,root),L"WebDAV 路径超出起始目录");
    auto url=NormalizeUrl(p.url)+(normalized==L"/"?L"":Encode(normalized,true));if(directory)url+=L'/';return url;
}
using Microsoft::WRL::ComPtr;
ComPtr<IXMLDOMNode> DavNode(IXMLDOMNode* node,const wchar_t* xpath){ComPtr<IXMLDOMNode> result;node->selectSingleNode(_bstr_t(xpath),&result);return result;}
std::wstring DavText(IXMLDOMNode* node,const wchar_t* xpath){auto selected=DavNode(node,xpath);if(!selected)return {};BSTR text{};selected->get_text(&text);std::wstring result=text?text:L"";SysFreeString(text);return result;}
std::wstring DavDecode(std::wstring path){
    std::wstring ascii;for(size_t n=0;n<path.size();){if(path[n]<128)ascii+=path[n++];else {const auto start=n;while(n<path.size()&&path[n]>=128)++n;ascii+=Encode(path.substr(start,n-start));}}
    return DavPath(Decode(ascii));
}
}
Page Library::WebDav(const Profile& p,const Query& q,const Cancel& cancel){
    Require(q.view==View::Directory||q.view==View::Search,L"WebDAV 支持目录浏览与当前目录筛选");
    const auto directory=DavPath(q.id.empty()?p.root:q.id),url=DavUrl(p,directory,true);
    const auto xml=dav_(url,DavAuthorization(p),cancel);CheckCancel(cancel);
    Require(xml.size()<=8*1024*1024&&xml.find("<!DOCTYPE")==xml.npos&&xml.find("<!ENTITY")==xml.npos,L"WebDAV XML 不允许 DTD 或外部实体");
    struct ComScope{HRESULT hr{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};~ComScope(){if(SUCCEEDED(hr))CoUninitialize();}} com;
    ComPtr<IXMLDOMDocument2> doc;
    if(FAILED(CoCreateInstance(__uuidof(DOMDocument60),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&doc)))){
        CLSID cls{};Require(SUCCEEDED(CLSIDFromProgID(L"Msxml2.DOMDocument.3.0",&cls))&&SUCCEEDED(CoCreateInstance(cls,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&doc))),L"无法初始化 WebDAV XML 解析器");}
    doc->put_async(VARIANT_FALSE);doc->put_resolveExternals(VARIANT_FALSE);doc->put_validateOnParse(VARIANT_FALSE);
    doc->setProperty(_bstr_t(L"ProhibitDTD"),_variant_t(true));doc->setProperty(_bstr_t(L"SelectionLanguage"),_variant_t(L"XPath"));
    Require(SUCCEEDED(doc->setProperty(_bstr_t(L"SelectionNamespaces"),_variant_t(L"xmlns:d='DAV:'"))),L"无法设置 WebDAV 命名空间");
    VARIANT_BOOL loaded{};const auto wide=Wide(xml);Require(wide.find(L"<!DOCTYPE")==wide.npos&&wide.find(L"<!ENTITY")==wide.npos,L"WebDAV XML 不允许外部实体");
    Require(SUCCEEDED(doc->loadXML(_bstr_t(wide.c_str()),&loaded))&&loaded==VARIANT_TRUE,L"WebDAV 返回了无效 XML");
    Require(DavNode(doc.Get(),L"/d:multistatus")!=nullptr,L"服务器没有返回 WebDAV 目录，请检查地址");
    ComPtr<IXMLDOMNodeList> rows;doc->selectNodes(_bstr_t(L"/d:multistatus/d:response"),&rows);long count{};if(rows)rows->get_length(&count);Require(count<=10000,L"WebDAV 目录超过 10000 项，请缩小起始目录");
    const auto base=NormalizeUrl(p.url),origin=HttpOrigin(base),basePath=DavDecode(ParseHttpUrl(base).path);
    const auto expected=basePath==L"/"?directory:DavPath(basePath+(directory==L"/"?L"":directory));
    std::vector<Item> items;std::set<std::wstring> seen;
    for(long n=0;n<count;++n){CheckCancel(cancel);ComPtr<IXMLDOMNode> row;rows->get_item(n,&row);if(!row)continue;
        auto href=DavText(row.Get(),L"d:href");if(href.empty()||href.find_first_of(L"?#\\")!=href.npos)continue;
        std::wstring raw;
        if(href.starts_with(L"http://")||href.starts_with(L"https://")){if(HttpOrigin(href)!=origin)continue;raw=ParseHttpUrl(href).path;}
        else if(href.front()==L'/'){if(href.starts_with(L"//"))continue;raw=href;}
        else raw=Encode(expected,true)+L"/"+href;
        std::wstring path;try{path=DavDecode(raw);}catch(const Error&){continue;}
        if(path==expected||!path.starts_with(expected==L"/"?L"/":expected+L"/"))continue;
        const auto name=path.substr(expected==L"/"?1:expected.size()+1);if(name.empty()||name.find(L'/')!=name.npos)continue;
        auto prop=DavNode(row.Get(),L"d:propstat[contains(d:status,' 200 ')]/d:prop");if(!prop)continue;
        Item item;item.kind=DavNode(prop.Get(),L"d:resourcetype/d:collection")?Kind::Directory:Kind::Song;
        if(item.kind==Kind::Song&&!Audio(name))continue;
        item.id=Join(directory,name);if(!seen.insert(item.id).second)continue;item.title=name;
        const auto length=DavText(prop.Get(),L"d:getcontentlength");if(!length.empty()){wchar_t* end{};item.size=wcstoull(length.c_str(),&end,10);if(!end||*end)item.size=0;}
        if(q.view==View::Search&&!q.text.empty()){auto a=name,b=q.text;std::transform(a.begin(),a.end(),a.begin(),towlower);std::transform(b.begin(),b.end(),b.begin(),towlower);if(a.find(b)==a.npos)continue;}
        items.push_back(std::move(item));
    }
    std::stable_sort(items.begin(),items.end(),[](const Item& a,const Item& b){if(a.kind!=b.kind)return a.kind==Kind::Directory;return _wcsicmp(a.title.c_str(),b.title.c_str())<0;});
    Page result;result.description=directory;const auto first=std::min<size_t>(items.size(),static_cast<size_t>(q.page)*100),last=std::min(items.size(),first+100);result.more=last<items.size();
    result.items.assign(std::make_move_iterator(items.begin()+first),std::make_move_iterator(items.begin()+last));return result;
}
