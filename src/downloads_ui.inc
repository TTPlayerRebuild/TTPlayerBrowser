    int HeaderHeight() const{return skin.Image(DownloadSkin::Header).size.cy?skin.Image(DownloadSkin::Header).size.cy:25;}
    int BodyTop() const{return skin.banner.bottom;}
    RECT ListRect() const{RECT r{};if(list)GetClientRect(list,&r);return r;}
    int ListWidth() const{return std::max(0L,ListRect().right-13);}
    int BodyHeight() const{return std::max(0L,ListRect().bottom-HeaderHeight());}
    int MaxScroll() const{return std::max(0,static_cast<int>(visible.size())*skin.row_height-BodyHeight());}
    void Scroll(int value){scroll=std::clamp(value,0,MaxScroll());ClearTooltip();if(list)InvalidateRect(list,nullptr,FALSE);}
    std::array<int,7> Widths() const{
        // 00420E99 weights; 004232E6 assigns the final column the remainder.
        const int active[]={5,28,18,9,12,15,14},complete[]={5,33,20,13,13,0,16};
        const auto* weights=category?complete:active;std::array<int,7> widths{};int used{};
        for(int i=0;i<6;++i){widths[i]=ListWidth()*weights[i]/100;used+=widths[i];}widths[6]=ListWidth()-used;return widths;
    }
    std::wstring Cell(const HttpDownload& row,int column) const{
        wchar_t text[64]{};
        switch(column){case 1:return row.display_title.empty()?row.entry.title:row.display_title;case 2:return Artist(row);
        case 3:{auto ext=row.destination.extension().wstring();if(!ext.empty())ext.erase(0,1);std::transform(ext.begin(),ext.end(),ext.begin(),towupper);return ext;}
        case 4:if(!row.total)return L"未知";swprintf_s(text,L"%.2fM",row.total/(1024.0*1024));return text;
        case 5:if(category)return L"未知";return std::to_wstring(row.total?std::min<uint64_t>(100,row.received*100/row.total):0)+L"%";
        case 6:if(category){WIN32_FILE_ATTRIBUTE_DATA data{};FILETIME local{};SYSTEMTIME date{};
            if(GetFileAttributesExW(row.destination.c_str(),GetFileExInfoStandard,&data) && FileTimeToLocalFileTime(&data.ftLastWriteTime,&local) && FileTimeToSystemTime(&local,&date)){
                swprintf_s(text,L"%02u-%02u",date.wMonth,date.wDay);return text;}return L"未知";
            }else{const auto rate=rates.find(row.id);if(row.state==DownloadState::running && rate!=rates.end())swprintf_s(text,L"%u K/S",static_cast<unsigned>(rate->second.speed/1024));return text;}
        default:return {};}
    }
    void Filter(){visible.clear();for(size_t i=0;i<rows.size();++i)if((rows[i].state==DownloadState::complete)==(category!=0))visible.push_back(i);
        if(category && sort_column>=0)std::stable_sort(visible.begin(),visible.end(),[&](size_t a,size_t b){int cmp{};
            if(sort_column==4)cmp=rows[a].total<rows[b].total?-1:rows[a].total>rows[b].total?1:0;
            else if(sort_column==0)cmp=static_cast<int>(rows[a].state)-static_cast<int>(rows[b].state);
            else cmp=_wcsicmp(Cell(rows[a],sort_column).c_str(),Cell(rows[b],sort_column).c_str());return descending?cmp>0:cmp<0;});
        scroll=std::clamp(scroll,0,MaxScroll());const int last=static_cast<int>(visible.size())-1;focus_row=std::clamp(focus_row,-1,last);anchor=std::clamp(anchor,-1,last);}
    void Refresh(){auto snapshot=queue->Snapshot();snapshot.erase(std::remove_if(snapshot.begin(),snapshot.end(),[](const auto& row){return row.prefetch;}),snapshot.end());
        const auto now=GetTickCount();std::set<uint64_t> ids;
        for(const auto& row:snapshot){ids.insert(row.id);auto& rate=rates[row.id];const DWORD elapsed=now-rate.tick;
            if(rate.tick && elapsed>=250){rate.speed=row.received>=rate.bytes?(row.received-rate.bytes)*1000.0/elapsed:0;rate.bytes=row.received;rate.tick=now;}
            else if(!rate.tick){rate.bytes=row.received;rate.tick=now;}}
        for(auto i=selected.begin();i!=selected.end();)if(!ids.contains(*i))i=selected.erase(i);else ++i;
        for(auto i=rates.begin();i!=rates.end();)if(!ids.contains(i->first))i=rates.erase(i);else ++i;
        rows=std::move(snapshot);Filter();if(!tooltip_text.empty())UpdateTooltip(last_list_point);if(window)InvalidateRect(window,nullptr,FALSE);if(list)InvalidateRect(list,nullptr,FALSE);
        const auto storage=queue->StorageResult();if(FAILED(storage) && storage!=last_storage && window && IsWindowVisible(window)){
            auto message=std::wstring(L"下载列表无法保存，或正由另一个播放器使用。");const auto backup=queue->RecoveryBackupPath();
            if(!backup.empty())message+=L"\n原列表备份："+backup.wstring();MessageBoxW(window,message.c_str(),L"下载管理",MB_OK|MB_ICONERROR);}last_storage=storage;
    }
    void Layout(){if(!window)return;RECT r{};GetClientRect(window,&r);if(r.right<=0 || r.bottom<=0)return;
        if(category_width)category_width=std::clamp(category_width,40,std::max(40,static_cast<int>(r.right)-100));
        const int left=category_width+5;MoveWindow(list,left,BodyTop(),std::max(0L,r.right-left),std::max(0L,r.bottom-BodyTop()),TRUE);
        MoveWindow(link,skin.link.left,skin.link.top,skin.link.right-skin.link.left,skin.link.bottom-skin.link.top,TRUE);
        auto button=skin.setting;OffsetRect(&button,r.right-skin.banner.right,0);MoveWindow(settings,button.left,button.top,button.right-button.left,button.bottom-button.top,TRUE);
        Scroll(scroll);InvalidateRect(window,nullptr,FALSE);}
    void DrawHeader(HDC dc,RECT rect,const std::wstring& caption,bool hot=false){const int frame=hot?1:0;
        skin.Draw(dc,DownloadSkin::Header,rect,{frame,0,frame+1,HeaderHeight()});
        skin.Draw(dc,DownloadSkin::HeaderLine,{rect.right-2,rect.top+1,rect.right,rect.top+23});rect.left+=5;rect.right-=3;Text(dc,rect,caption,skin.header_text);}
    void PaintFrame(HDC dc){RECT r{};GetClientRect(window,&r);Fill(dc,r,skin.category_background);skin.Draw(dc,DownloadSkin::Banner,{0,0,r.right,BodyTop()});
        const auto old=SelectObject(dc,skin.header_font);DrawHeader(dc,{0,BodyTop(),category_width,BodyTop()+HeaderHeight()},L"下载管理");SelectObject(dc,skin.category_font);
        const auto finished=std::count_if(rows.begin(),rows.end(),[](const auto& row){return row.state==DownloadState::complete;});
        for(int i=0;i<2;++i){RECT row{0,BodyTop()+HeaderHeight()+i*skin.category_height,category_width,BodyTop()+HeaderHeight()+(i+1)*skin.category_height};
            if(i==category)Fill(dc,row,skin.category_selection);const auto asset=i?DownloadSkin::Finish:DownloadSkin::Downloading;const auto size=skin.Image(asset).size;
            const int y=row.top+(skin.category_height-size.cy)/2;skin.Draw(dc,asset,{3,y,3+size.cx,y+size.cy},{},true);row.left=13;
            Text(dc,row,(i?L"已下载(":L"下载中(")+std::to_wstring(i?finished:rows.size()-finished)+L")",skin.category_text);}
        SelectObject(dc,old);Fill(dc,{category_width,BodyTop(),category_width+5,r.bottom},skin.splitter_background);Fill(dc,{category_width+2,BodyTop(),category_width+3,r.bottom},skin.splitter_front);
        // The stock splitter has no bitmap: its central five-pixel button
        // collapses/restores the category pane (0041C1EB / 0041C941).
        const auto button=SplitterButton();const int center=(button.top+button.bottom)/2;
        POINT arrow[3]{{category_width+(category_width?1:3),center},{category_width+(category_width?3:1),center-2},{category_width+(category_width?3:1),center+2}};
        const auto brush=CreateSolidBrush(skin.header_text);const auto pen=CreatePen(PS_SOLID,1,skin.header_text);
        const auto old_brush=SelectObject(dc,brush),old_pen=SelectObject(dc,pen);Polygon(dc,arrow,3);
        SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(brush);DeleteObject(pen);}
    RECT Thumb() const{const auto r=ListRect();const int top=HeaderHeight()+12,bottom=std::max(top,static_cast<int>(r.bottom)-12),track=bottom-top;
        const int total=static_cast<int>(visible.size())*skin.row_height;const int height=total?std::clamp(track*BodyHeight()/std::max(1,total),std::min(18,track),track):track;
        const int y=top+(MaxScroll()?scroll*(track-height)/MaxScroll():0);return {r.right-12,y,r.right,y+height};}
    void PaintScrollbar(HDC dc){const auto r=ListRect();const int x=r.right-12,top=HeaderHeight();if(r.bottom<=top || x<0)return;
        skin.Draw(dc,DownloadSkin::ScrollBar,{x,top,r.right,r.bottom});const int up=hot_scroll==1?1:0,down=hot_scroll==2?1:0;
        skin.Draw(dc,DownloadSkin::ScrollButtons,{x,top,r.right,top+12},{up*12,0,up*12+12,12});
        skin.Draw(dc,DownloadSkin::ScrollButtons,{x,std::max(top+12,static_cast<int>(r.bottom)-12),r.right,r.bottom},{down*12,12,down*12+12,24});
        if(MaxScroll()){const int frame=thumb_drag?2:hot_scroll==3?1:0;skin.Draw(dc,DownloadSkin::ScrollThumb,Thumb(),{frame*12,0,frame*12+12,skin.Image(DownloadSkin::ScrollThumb).size.cy});}}
    void PaintList(HDC dc){const auto r=ListRect();Fill(dc,r,skin.list_odd);const auto widths=Widths();const int h=HeaderHeight();
        const wchar_t* captions[]={L"",L"歌曲名",L"歌手名",L"格式",L"大小",category?L"码率Kbps":L"进度",category?L"下载时间":L"速度"};
        const auto old=SelectObject(dc,skin.header_font);int x{};
        for(int i=0;i<7;++i){RECT cell{x,0,x+widths[i],h};if(widths[i]){DrawHeader(dc,cell,captions[i],i==hover_header);
            if(category && i==sort_column){const auto asset=descending?DownloadSkin::SortDown:DownloadSkin::SortUp;const auto size=skin.Image(asset).size;
                skin.Draw(dc,asset,{cell.right-size.cx-3,(h-size.cy)/2,cell.right-3,(h+size.cy)/2},{},true);}}x+=widths[i];}
        skin.Draw(dc,DownloadSkin::Header,{x,0,r.right,h},{0,0,1,h});const int saved=SaveDC(dc);IntersectClipRect(dc,0,h,ListWidth(),r.bottom);SelectObject(dc,skin.list_font);
        for(size_t index=static_cast<size_t>(scroll/skin.row_height);index<visible.size();++index){const int y=h+static_cast<int>(index)*skin.row_height-scroll;if(y>=r.bottom)break;
            const auto& row=rows[visible[index]];RECT bounds{0,y,ListWidth(),y+skin.row_height};const auto background=selected.contains(row.id)?skin.list_selection:index%2?skin.list_odd:skin.list_even;
            Fill(dc,bounds,background);int left{};
            for(int column=0;column<7;++column){RECT cell{left,y,left+widths[column],y+skin.row_height};left+=widths[column];if(!widths[column])continue;
                const int clip=SaveDC(dc);IntersectClipRect(dc,cell.left,cell.top,cell.right,cell.bottom);
                if(column==0){const auto asset=category?DownloadSkin::Play:row.state==DownloadState::running?DownloadSkin::Downloading:row.state==DownloadState::paused?DownloadSkin::Pause:
                        row.state==DownloadState::failed || row.state==DownloadState::canceled?DownloadSkin::Failed:DownloadSkin::Waiting;
                    const auto size=skin.Image(asset).size;const int cx=cell.left+(widths[0]-size.cx)/2,cy=y+(skin.row_height-size.cy)/2;skin.Draw(dc,asset,{cx,cy,cx+size.cx,cy+size.cy},{},true);
                }else if(column==5 && !category){RECT frame=cell;InflateRect(&frame,-2,-2);Fill(dc,frame,skin.progress_border);RECT inside=frame;InflateRect(&inside,-1,-1);Fill(dc,inside,background);
                    RECT progress=cell;InflateRect(&progress,-4,-4);progress.right=progress.left+static_cast<LONG>((progress.right-progress.left)*(row.total?std::min<uint64_t>(100,row.received*100/row.total):0)/100);
                    Fill(dc,progress,row.state==DownloadState::paused?skin.disabled:skin.progress);Text(dc,cell,Cell(row,column),skin.progress_text,DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
                }else{cell.left+=4;Text(dc,cell,Cell(row,column),skin.list_text);}RestoreDC(dc,clip);}
            if(GetFocus()==list && static_cast<int>(index)==focus_row){RECT focus=bounds;InflateRect(&focus,-1,-1);DrawFocusRect(dc,&focus);}}
        RestoreDC(dc,saved);SelectObject(dc,old);PaintScrollbar(dc);}
    void Paint(HWND hwnd,HDC target,bool contents){RECT r{};GetClientRect(hwnd,&r);if(r.right<=0 || r.bottom<=0)return;
        const auto dc=CreateCompatibleDC(target);const auto bitmap=CreateCompatibleBitmap(target,r.right,r.bottom);const auto old=SelectObject(dc,bitmap);
        if(contents)PaintList(dc);else PaintFrame(dc);BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);}
    void ChooseCategory(int value){if(value==category || value<0 || value>1)return;EndListInteraction(false);ClearTooltip();category=value;selected.clear();focus_row=anchor=hover_header=-1;scroll=wheel_remainder=0;Filter();InvalidateRect(window,nullptr,FALSE);InvalidateRect(list,nullptr,FALSE);}
    void ChooseRow(int index,bool control,bool shift){if(index<0 || index>=static_cast<int>(visible.size()))return;if(!control)selected.clear();
        if(shift && anchor>=0){for(int i=std::min(anchor,index);i<=std::max(anchor,index);++i)selected.insert(rows[visible[i]].id);}
        else{const auto id=rows[visible[index]].id;if(control && selected.contains(id))selected.erase(id);else selected.insert(id);anchor=index;}focus_row=index;InvalidateRect(list,nullptr,FALSE);}
    void EnsureVisible(){if(focus_row<0)return;const int y=focus_row*skin.row_height;if(y<scroll)Scroll(y);else if(y+skin.row_height>scroll+BodyHeight())Scroll(y+skin.row_height-BodyHeight());}
    std::vector<HttpDownload> Selection() const{std::vector<HttpDownload> result;for(auto i:visible)if(selected.contains(rows[i].id))result.push_back(rows[i]);return result;}
    int RowAt(POINT point) const{const auto rect=ListRect();if(point.x<0 || point.x>=ListWidth() || point.y<HeaderHeight() || point.y>=rect.bottom)return -1;
        const int index=(point.y-HeaderHeight()+scroll)/skin.row_height;return index<static_cast<int>(visible.size())?index:-1;}
    int ColumnAt(int x) const{if(x<0 || x>=ListWidth())return -1;int right{};const auto widths=Widths();for(int i=0;i<7;++i){right+=widths[i];if(x<right)return i;}return -1;}
    int HeaderAt(POINT point) const{if(!category || visible.size()<2 || point.y<0 || point.y>=HeaderHeight())return -1;
        const int column=ColumnAt(point.x);if(column<0)return -1;int right{};for(const auto width:Widths()){right+=width;if(width && std::abs(point.x-right)<=10)return -1;}return column;}
    void ClearTooltip(){if(tooltip){SendMessageW(tooltip,TTM_POP,0,0);SendMessageW(tooltip,TTM_ACTIVATE,FALSE,0);}tooltip_text.clear();}
    void CreateTooltip(){INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,0,0,0,0,list,nullptr,ThisModule(),nullptr);
        if(!tooltip)return;TOOLINFOW tool{sizeof(tool)};tool.uFlags=TTF_SUBCLASS|TTF_IDISHWND;tool.hwnd=list;tool.uId=reinterpret_cast<UINT_PTR>(list);tool.lpszText=LPSTR_TEXTCALLBACKW;
        SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,420);SendMessageW(tooltip,TTM_ACTIVATE,FALSE,0);}
    void UpdateTooltip(POINT point){last_list_point=point;const int index=RowAt(point),column=ColumnAt(point.x);std::wstring text;
        if(index>=0){const auto& row=rows[visible[index]];if(column==0){if(category)text=L"播放歌曲";
            else switch(row.state){case DownloadState::running:text=L"正在下载";break;case DownloadState::paused:text=L"暂停下载";break;
                case DownloadState::waiting:text=L"等待下载";break;default:text=L"下载失败或存储目录不存在";break;}}
            else if(column==1){text=L"歌曲名："+Cell(row,1)+L"\n歌手名："+Cell(row,2)+L"\n格式："+Cell(row,3)+L"\n大小："+Cell(row,4);
                text+=category?L"\n文件："+row.destination.wstring():L"\n下载地址："+row.entry.sourceurl;}}
        if(text==tooltip_text)return;ClearTooltip();tooltip_text=std::move(text);if(tooltip && !tooltip_text.empty())SendMessageW(tooltip,TTM_ACTIVATE,TRUE,0);}
    void DoubleClickRow(int index){if(index<0 || index>=static_cast<int>(visible.size()))return;
        if(!selected.contains(rows[visible[index]].id))ChooseRow(index,false,false);
        if(category){Command(kPlay);return;}const auto chosen=Selection();if(chosen.empty())return;
        // 004244B1 operates on the first selected task after the list hit test.
        const auto& task=chosen.front();if(task.state==DownloadState::running)queue->Pause(task.id);else queue->Retry(task.id);Refresh();}
    void EndListInteraction(bool cancel){const bool restore=cancel && thumb_drag;thumb_drag=false;header_pressed=-1;pressed_play_id=0;scroll_repeat=0;KillTimer(list,kScrollRepeatTimer);
        if(restore)Scroll(drag_scroll);if(GetCapture()==list)ReleaseCapture();if(list)InvalidateRect(list,nullptr,FALSE);}
    int ScrollPart(POINT point) const{const auto r=ListRect();if(point.x<ListWidth() || point.x>=r.right || point.y<HeaderHeight() || point.y>=r.bottom)return 0;
        if(point.y<HeaderHeight()+12)return 1;if(point.y>=r.bottom-12)return 2;const auto thumb=Thumb();
        if(point.y<thumb.top)return 4;if(point.y>=thumb.bottom)return 5;return MaxScroll()?3:0;}
    void RepeatScroll(){if(!scroll_repeat)return;const auto thumb=Thumb();
        // A held track click stops once the thumb reaches the pointer.
        if((scroll_repeat==4 && repeat_y>=thumb.top) || (scroll_repeat==5 && repeat_y<thumb.bottom))return;
        const int amount=scroll_repeat<=2?skin.row_height:BodyHeight();Scroll(scroll+((scroll_repeat==1 || scroll_repeat==4)?-amount:amount));}
    void Wheel(short delta){UINT lines=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);if(!lines){wheel_remainder=0;return;}
        wheel_remainder+=delta;const int ticks=wheel_remainder/WHEEL_DELTA;wheel_remainder%=WHEEL_DELTA;if(!ticks)return;
        const int64_t amount=lines==WHEEL_PAGESCROLL?BodyHeight():static_cast<int64_t>(skin.row_height)*lines;
        Scroll(static_cast<int>(std::clamp<int64_t>(scroll-ticks*amount,0,MaxScroll())));}
    RECT SplitterButton() const{RECT r{};GetClientRect(window,&r);const int middle=(BodyTop()+r.bottom)/2;return {category_width,middle-2,category_width+5,middle+3};}
    void EndSplit(bool cancel,POINT point={-1,-1}){if(!split_drag)return;const auto button=SplitterButton();const bool toggle=!cancel && category_width==split_start && PtInRect(&button,point);
        split_drag=false;if(cancel)category_width=split_start;else if(toggle){if(category_width){saved_category_width=category_width;category_width=0;}else category_width=saved_category_width;}
        else if(category_width)saved_category_width=category_width;if(GetCapture()==window)ReleaseCapture();Layout();}
    void Error(const wchar_t* message){MessageBoxW(window,message,L"下载管理",MB_OK|MB_ICONERROR);}
    void Command(int command){auto chosen=Selection();
        if(command==kSelectTasks || command==kSelectFiles){for(auto index:visible)selected.insert(rows[index].id);InvalidateRect(list,nullptr,FALSE);return;}
        if(command==kSettings){if(host.owner)PostMessageW(host.owner,WM_COMMAND,0x903,0);return;}
        if(command==kLink){ShellExecuteW(window,L"open",L"http://ttplayer.qianqian.com/help.html?f=client",nullptr,nullptr,SW_SHOWNORMAL);return;}
        if(command==kPlay || command==kAdd){if(chosen.empty() || !host.submit_tracks)return;
            struct Song{std::wstring source,artist,title,tid;};std::vector<Song> songs;songs.reserve(chosen.size());
            for(const auto& row:chosen)songs.push_back({row.state==DownloadState::complete?row.destination.wstring():row.entry.sourceurl,Artist(row),row.display_title,row.tid});
            std::vector<ttp_browser_track> tracks;tracks.reserve(songs.size());for(const auto& song:songs)tracks.push_back({song.source.c_str(),song.artist.c_str(),song.title.c_str(),song.tid.c_str()});
            if(FAILED(host.submit_tracks(host.context,tracks.data(),static_cast<uint32_t>(tracks.size()),command==kAdd?1:0,command==kAdd?0:1,command==kAdd?0:1)))Error(L"无法添加歌曲。");return;
        }
        if(command==kSendFolder){if(chosen.empty())return;BROWSEINFOW browse{};browse.hwndOwner=window;browse.lpszTitle=L"请选择文件夹";browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
            auto* item=SHBrowseForFolderW(&browse);if(!item)return;wchar_t path[MAX_PATH]{};const bool resolved=SHGetPathFromIDListW(item,path)!=FALSE;CoTaskMemFree(item);if(!resolved)return;
            for(const auto& row:chosen){const auto target=std::filesystem::path(path)/row.destination.filename();if(!CopyFileW(row.destination.c_str(),target.c_str(),TRUE))Error(L"无法复制歌曲文件，目标文件可能已经存在。");}return;
        }
        if(command==kImport){std::array<wchar_t,32768> path{};OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=window;ofn.lpstrFile=path.data();ofn.nMaxFile=static_cast<DWORD>(path.size());
            ofn.lpstrFilter=L"XML 列表\0*.xml\0所有文件\0*.*\0\0";ofn.lpstrTitle=L"导入原版缓存索引或下载队列";ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER|OFN_NOCHANGEDIR;
            if(GetOpenFileNameW(&ofn)){const auto count=queue->ImportLegacyList(path.data(),folder,artist_folders,replace);if(!count)Error(L"没有可导入的任务。");}Refresh();return;}
        if(command==kClear){queue->ClearHistory();Refresh();return;}
        if(command==kDeleteFile){std::vector<std::filesystem::path> paths;for(const auto& row:chosen)paths.push_back(row.destination);
            const auto result=DeleteDownloadedFiles(window,paths,host);for(const auto& row:chosen){const auto path=std::filesystem::absolute(row.destination).lexically_normal().wstring();
                if(std::any_of(result.removed.begin(),result.removed.end(),[&](const auto& removed){return _wcsicmp(path.c_str(),removed.c_str())==0;}))queue->Remove(row.id);}
            if(FAILED(result.status))Error(L"部分歌曲文件无法删除，未删除文件的下载记录已保留。");Refresh();return;}
        for(const auto& row:chosen){switch(command){case kStart:case kRetry:queue->Retry(row.id);break;case kPause:queue->Pause(row.id);break;
            case kRemoveTask:case kRemove:queue->Remove(row.id);break;
            case kBrowse:{const auto args=L"/select,\""+row.destination.wstring()+L"\"";ShellExecuteW(window,L"open",L"explorer.exe",args.c_str(),nullptr,SW_SHOWNORMAL);break;}
            default:break;}}
        Refresh();
    }
    void ContextMenu(POINT point){EndListInteraction(false);ClearTooltip();if(point.x==-1 && point.y==-1){if(Selection().empty())return;
            if(focus_row<0 || !selected.contains(rows[visible[focus_row]].id)){for(size_t i=0;i<visible.size();++i)if(selected.contains(rows[visible[i]].id)){focus_row=static_cast<int>(i);break;}}
            EnsureVisible();point={10,HeaderHeight()+focus_row*skin.row_height-scroll+skin.row_height/2};ClientToScreen(list,&point);}else{
            auto local=point;ScreenToClient(list,&local);const int row=RowAt(local);if(row<0)return;
            if(!selected.contains(rows[visible[row]].id))ChooseRow(row,false,false);else focus_row=row;}
        SetFocus(list);const auto chosen=Selection();const bool any=!chosen.empty();const auto menu=CreatePopupMenu();if(!menu)return;
        auto item=[&](int id,const wchar_t* label,bool enabled=true){AppendMenuW(menu,MF_STRING|(enabled?0:MF_GRAYED),id,label);};auto separator=[&]{AppendMenuW(menu,MF_SEPARATOR,0,nullptr);};
        if(!category){const bool multiple=chosen.size()>1;
            item(kStart,L"开始下载(&S)",multiple || (any && chosen[0].state!=DownloadState::running && chosen[0].state!=DownloadState::waiting));
            item(kPause,L"暂停下载(&P)",multiple || (any && chosen[0].state!=DownloadState::failed && chosen[0].state!=DownloadState::paused && chosen[0].state!=DownloadState::canceled));
            item(kRemoveTask,L"删除任务(&E)",any);if(chosen.size()==1)item(kSelectTasks,L"全部选中(&A)",!visible.empty());}
        else{item(kPlay,L"播放歌曲(&P)",any && host.submit_tracks);item(kAdd,L"添加到列表(&D)...",any && host.submit_tracks);separator();
            item(kRemove,L"删除记录(&E)",any);item(kDeleteFile,L"删除文件(&L)",any);item(kRetry,L"重新下载(&R)",any);separator();item(kSelectFiles,L"全部选中(&A)",!visible.empty());item(kBrowse,L"浏览文件(&F)...",chosen.size()==1);
            const auto send=CreatePopupMenu();if(send){AppendMenuW(send,MF_STRING,kSendFolder,L"文件夹(&F)...");AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(send),L"发送到");}}
        const auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);if(choice)Command(choice);
    }
    static LRESULT CALLBACK ListProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) noexcept{
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,message,wp,lp);try{switch(message){
        case WM_PAINT:{PAINTSTRUCT ps{};const auto dc=BeginPaint(hwnd,&ps);self->Paint(hwnd,dc,true);EndPaint(hwnd,&ps);return 0;}
        case WM_PRINTCLIENT:self->Paint(hwnd,reinterpret_cast<HDC>(wp),true);return 0;case WM_ERASEBKGND:return 1;case WM_GETDLGCODE:return DLGC_WANTARROWS|DLGC_WANTCHARS;
        case WM_SETFOCUS:case WM_KILLFOCUS:InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_MOUSEWHEEL:self->Wheel(GET_WHEEL_DELTA_WPARAM(wp));return 0;
        case WM_LBUTTONDOWN:{SetFocus(hwnd);self->EndListInteraction(false);self->ClearTooltip();const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};self->last_list_point=point;
            if(point.y<self->HeaderHeight()){self->header_pressed=self->HeaderAt(point);if(self->header_pressed>=0)SetCapture(hwnd);return 0;}
            const int part=self->ScrollPart(point);if(part){self->drag_scroll=self->scroll;
                if(part==3){self->thumb_drag=true;self->drag_y=point.y;SetCapture(hwnd);}
                else{self->scroll_repeat=part;self->repeat_y=point.y;SetCapture(hwnd);self->RepeatScroll();SetTimer(hwnd,kScrollRepeatTimer,GetDoubleClickTime(),nullptr);}return 0;}
            const int row=self->RowAt(point);if(row<0){self->selected.clear();self->focus_row=self->anchor=-1;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            self->ChooseRow(row,(GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_SHIFT)&0x8000)!=0);
            if(self->category && self->ColumnAt(point.x)==0){self->pressed_play_id=self->rows[self->visible[row]].id;SetCapture(hwnd);}return 0;}
        case WM_LBUTTONDBLCLK:{const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};const int row=self->RowAt(point);
            // A double-click already includes the first icon single-click.
            if(row>=0 && !(self->category && self->ColumnAt(point.x)==0))self->DoubleClickRow(row);return 0;}
        case WM_MOUSEMOVE:{const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};self->last_list_point=point;
            if(self->thumb_drag){const auto rect=self->ListRect(),thumb=self->Thumb();const int track=rect.bottom-self->HeaderHeight()-24-(thumb.bottom-thumb.top);
                if(track>0)self->Scroll(self->drag_scroll+(point.y-self->drag_y)*self->MaxScroll()/track);return 0;}
            if(self->scroll_repeat){self->repeat_y=point.y;self->hot_scroll=self->ScrollPart(point);return 0;}
            if(self->pressed_play_id)return 0;
            self->hover_header=self->HeaderAt(point);const int part=self->ScrollPart(point);self->hot_scroll=part<=3?part:0;
            self->UpdateTooltip(point);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        case WM_MOUSELEAVE:self->hover_header=-1;self->hot_scroll=0;self->ClearTooltip();InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_LBUTTONUP:{const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};const int column=self->HeaderAt(point),pressed=self->header_pressed,row=self->RowAt(point);
            const bool play=self->pressed_play_id && row>=0 && self->ColumnAt(point.x)==0 && self->rows[self->visible[row]].id==self->pressed_play_id;
            self->EndListInteraction(false);if(play)self->Command(kPlay);
            else if(pressed>=0 && column==pressed){self->descending=self->sort_column==column?!self->descending:false;self->sort_column=column;self->Filter();InvalidateRect(hwnd,nullptr,FALSE);}return 0;}
        case WM_CAPTURECHANGED:case WM_CANCELMODE:self->EndListInteraction(true);return 0;
        case WM_RBUTTONDOWN:if(self->thumb_drag || self->scroll_repeat || self->header_pressed>=0 || self->pressed_play_id)self->EndListInteraction(true);break;
        case WM_SETCURSOR:if(reinterpret_cast<HWND>(wp)==hwnd && LOWORD(lp)==HTCLIENT){POINT point{};GetCursorPos(&point);ScreenToClient(hwnd,&point);
            SetCursor(LoadCursorW(nullptr,self->category && self->RowAt(point)>=0 && self->ColumnAt(point.x)==0?IDC_HAND:IDC_ARROW));return TRUE;}break;
        case WM_NOTIFY:{const auto* note=reinterpret_cast<NMHDR*>(lp);if(note && note->hwndFrom==self->tooltip && note->code==TTN_GETDISPINFOW){
            reinterpret_cast<NMTTDISPINFOW*>(lp)->lpszText=self->tooltip_text.data();return 0;}break;}
        case WM_TIMER:if(wp==kScrollRepeatTimer && self->scroll_repeat){if(GetCapture()!=hwnd){self->EndListInteraction(true);return 0;}
            if(self->ScrollPart(self->last_list_point)==self->scroll_repeat)self->RepeatScroll();SetTimer(hwnd,kScrollRepeatTimer,60,nullptr);}return 0;
        case WM_CONTEXTMENU:self->ContextMenu({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
        case WM_KEYDOWN:if(wp==VK_ESCAPE && (self->thumb_drag || self->scroll_repeat || self->header_pressed>=0 || self->pressed_play_id)){self->EndListInteraction(true);return 0;}
            if(wp==VK_DELETE){self->Command(self->category?((GetKeyState(VK_SHIFT)&0x8000)?kDeleteFile:kRemove):kRemoveTask);return 0;}
            if(wp=='A' && (GetKeyState(VK_CONTROL)&0x8000)){self->Command(self->category?kSelectFiles:kSelectTasks);return 0;}if(wp==VK_RETURN){self->Command(kPlay);return 0;}
            if(wp==VK_UP || wp==VK_DOWN || wp==VK_HOME || wp==VK_END){int row=self->focus_row;if(wp==VK_HOME)row=0;else if(wp==VK_END)row=static_cast<int>(self->visible.size())-1;else row+=wp==VK_UP?-1:1;
                row=std::clamp(row,0,std::max(0,static_cast<int>(self->visible.size())-1));self->ChooseRow(row,false,(GetKeyState(VK_SHIFT)&0x8000)!=0);self->EnsureVisible();return 0;}break;
        case WM_SHOWWINDOW:if(!wp){self->EndListInteraction(true);self->ClearTooltip();}break;
        case WM_DESTROY:self->EndListInteraction(false);if(self->tooltip){DestroyWindow(self->tooltip);self->tooltip=nullptr;}return 0;
        case WM_NCDESTROY:SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);break;
        }}catch(...){}return DefWindowProcW(hwnd,message,wp,lp);
    }
    static LRESULT CALLBACK Proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) noexcept{
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,message,wp,lp);try{switch(message){
        case WM_CREATE:{const auto module=ThisModule();self->list=CreateWindowExW(0,kListClass,L"下载歌曲列表",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,hwnd,reinterpret_cast<HMENU>(kList),module,self);
            self->link=CreateWindowExW(0,L"BUTTON",L"千千静听使用技巧",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,reinterpret_cast<HMENU>(kLink),module,nullptr);
            self->settings=CreateWindowExW(0,L"BUTTON",L"下载设置",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,reinterpret_cast<HMENU>(kSettings),module,nullptr);
            if(!self->list || !self->link || !self->settings)return -1;self->CreateTooltip();SetTimer(hwnd,kRefreshTimer,500,nullptr);self->Refresh();return 0;}
        case WM_SIZE:self->Layout();return 0;
        case WM_PAINT:{PAINTSTRUCT ps{};const auto dc=BeginPaint(hwnd,&ps);self->Paint(hwnd,dc,false);EndPaint(hwnd,&ps);return 0;}
        case WM_PRINTCLIENT:self->Paint(hwnd,reinterpret_cast<HDC>(wp),false);return 0;case WM_ERASEBKGND:return 1;
        case WM_DRAWITEM:{const auto* draw=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(!draw)return FALSE;
            if(draw->CtlID==kSettings){Fill(draw->hDC,draw->rcItem,self->skin.link_background);const auto image=self->skin.Image(DownloadSkin::Setting);
                const int frame=draw->itemState&ODS_DISABLED?3:draw->itemState&ODS_SELECTED?2:draw->itemState&ODS_HOTLIGHT?1:0;
                const int width=image.size.cx/4,height=image.size.cy,x=(draw->rcItem.right-width)/2,y=(draw->rcItem.bottom-height)/2;
                self->skin.Draw(draw->hDC,DownloadSkin::Setting,{x,y,x+width,y+height},{frame*width,0,(frame+1)*width,height});
            }else if(draw->CtlID==kLink){Fill(draw->hDC,draw->rcItem,self->skin.link_background);const auto old=SelectObject(draw->hDC,self->skin.link_font);Text(draw->hDC,draw->rcItem,L"千千静听使用技巧",self->skin.link_color);SelectObject(draw->hDC,old);}
            else return FALSE;if(draw->itemState&ODS_FOCUS){RECT focus=draw->rcItem;InflateRect(&focus,-2,-2);DrawFocusRect(draw->hDC,&focus);}return TRUE;}
        case WM_LBUTTONDOWN:{const int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);if(x>=self->category_width && x<self->category_width+5 && y>=self->BodyTop()){
                self->EndListInteraction(false);self->split_start=self->category_width;self->split_anchor=x;self->split_moved=false;self->split_drag=true;SetCapture(hwnd);return 0;}
            if(x>=0 && x<self->category_width && y>=self->BodyTop()+self->HeaderHeight()){const int row=(y-self->BodyTop()-self->HeaderHeight())/self->skin.category_height;if(row<2)self->ChooseCategory(row);}return 0;}
        case WM_MOUSEMOVE:if(self->split_drag){const int offset=GET_X_LPARAM(lp)-self->split_anchor;
            if(offset || self->split_moved){self->split_moved=true;self->category_width=std::max(0,self->split_start+offset);self->Layout();}}return 0;
        case WM_LBUTTONUP:self->EndSplit(false,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
        case WM_CAPTURECHANGED:case WM_CANCELMODE:self->EndSplit(true);return 0;
        case WM_RBUTTONDOWN:if(self->split_drag)self->EndSplit(true);return 0;
        case WM_CONTEXTMENU:return 0;
        case WM_KEYDOWN:if(wp==VK_ESCAPE && self->split_drag){self->EndSplit(true);return 0;}break;
        case WM_SETCURSOR:if(reinterpret_cast<HWND>(wp)==hwnd && LOWORD(lp)==HTCLIENT){POINT point{};GetCursorPos(&point);ScreenToClient(hwnd,&point);
            if(point.x>=self->category_width && point.x<self->category_width+5 && point.y>=self->BodyTop()){const auto button=self->SplitterButton();SetCursor(LoadCursorW(nullptr,PtInRect(&button,point)?IDC_HAND:IDC_SIZEWE));return TRUE;}}break;
        case WM_SHOWWINDOW:if(wp)self->Refresh();else{self->EndSplit(true);self->EndListInteraction(true);self->ClearTooltip();}break;
        case WM_TIMER:if(wp==kRefreshTimer && IsWindowVisible(hwnd))self->Refresh();return 0;
        case WM_COMMAND:self->Command(LOWORD(wp));return 0;case WM_DESTROY:self->EndSplit(false);KillTimer(hwnd,kRefreshTimer);return 0;
        case WM_NCDESTROY:self->window=self->list=self->link=self->settings=nullptr;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);break;
        }}catch(...){if(message==WM_CREATE)return -1;}return DefWindowProcW(hwnd,message,wp,lp);
    }
    HWND CreateView(HWND parent){if(window)return window;if(!IsWindow(parent))return nullptr;
        WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=Proc;cls.hInstance=ThisModule();cls.lpszClassName=kClass;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassExW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
        cls.lpfnWndProc=ListProc;cls.lpszClassName=kListClass;cls.style=CS_DBLCLKS;
        if(!RegisterClassExW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
        return CreateWindowExW(WS_EX_CONTROLPARENT,kClass,L"下载管理",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,0,0,parent,nullptr,ThisModule(),this);
    }
