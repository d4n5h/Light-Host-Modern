#pragma once
// Shared native backend. No JUCE/WinUI dependency, no audio-thread file access.
#include "RuntimeProfile.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <map>
#include <objbase.h>
#include <cstdio>
#include <psapi.h>
#pragma comment(lib, "ole32.lib")

namespace lightHostModern::verbose
{
inline constexpr uint64_t segmentLimit=16ULL*1024*1024, captureLimit=256ULL*1024*1024, queueLimit=2ULL*1024*1024;
inline std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    std::string r(WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr),0);
    WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),r.data(),(int)r.size(),nullptr,nullptr); return r;
}
inline std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    std::wstring r(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0),0);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),r.data(),(int)r.size()); return r;
}
inline std::string timestamp() {
    SYSTEMTIME t{}; GetSystemTime(&t); char b[40]{};
    snprintf(b,sizeof b,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds); return b;
}
inline std::filesystem::path root() {
    const auto& p=RuntimeProfile::current();
    return (p.test?p.directory:std::filesystem::path(RuntimeProfile::environment(L"LOCALAPPDATA"))/L"LightHostModern")/L"Logs"/L"Captures";
}
struct State { std::string phase="off", session, started, error; };
inline State readState(const std::filesystem::path& base=root()) {
    State s; std::ifstream in(base/L"state.txt");
    std::string phase,id,start; if (std::getline(in,phase)&&std::getline(in,id)&&std::getline(in,start)) {
        if (id.size()>80 || id.find_first_not_of("0123456789abcdef-")!=std::string::npos) return s;
        if (phase!="off"&&phase!="armed"&&phase!="collecting"&&phase!="stopped") return s;
        s.phase=phase;s.session=id;s.started=start;std::getline(in,s.error);
    } return s;
}
inline void writeState(const State& s,const std::filesystem::path& base=root()) {
    std::filesystem::create_directories(base);
    const auto tmp=base/L"state.pending"; {std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
        out<<s.phase<<'\n'<<s.session<<'\n'<<s.started<<'\n'<<s.error<<'\n';out.flush();if(!out)throw std::runtime_error("log_state_write_failed");}
    if(!MoveFileExW(tmp.c_str(),(base/L"state.txt").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("log_state_write_failed");
}
inline uint64_t bytes(const std::filesystem::path& folder) {
    uint64_t n=0;std::error_code ec;
    for(std::filesystem::directory_iterator i(folder,ec),end;i!=end&&!ec;i.increment(ec))
        if(i->path().extension()==L".log") {
            HANDLE file=CreateFileW(i->path().c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE){LARGE_INTEGER size{};if(GetFileSizeEx(file,&size))n+=(uint64_t)size.QuadPart;CloseHandle(file);}
        }
    return n;
}
inline std::wstring eventName(const State& s) {return L"Local\\LightHostModernCapture-"+wide(s.session);}
inline void reportFailure(const std::filesystem::path& base,const char* message) noexcept {
    OutputDebugStringA(message);
    try {
        const auto state=readState(base);if(state.phase!="collecting"||state.session.empty())return;
        std::ofstream error(base/wide(state.session)/L"capture-error.txt");error<<message;
        HANDLE event=CreateEventW(nullptr,TRUE,TRUE,eventName(state).c_str());if(event){SetEvent(event);CloseHandle(event);}
    }catch(...){}
}
inline std::string sanitize(std::string line) {
    static const auto home=utf8(RuntimeProfile::environment(L"USERPROFILE"));
    const auto replace=[&](const std::string& prefix){if(!prefix.empty())for(size_t p=0;p+prefix.size()<=line.size();) {
        if(_strnicmp(line.c_str()+p,prefix.c_str(),prefix.size())==0){line.replace(p,prefix.size(),"<USERPROFILE>");p+=13;}else ++p;
    }};
    std::string escaped,forward=home;for(char c:home){escaped+=c;if(c=='\\')escaped+=c;}
    std::replace(forward.begin(),forward.end(),'\\','/');replace(escaped);replace(home);replace(forward);
    for(auto& c:line)if(c=='\r'||c=='\n'||c=='\0')c=' ';
    if(line.size()>8192)line.resize(8192);return line;
}
inline uint64_t processBirth(HANDLE process) {
    FILETIME c{},e{},k{},u{};if(!GetProcessTimes(process,&c,&e,&k,&u))return 0;
    return (uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
}
inline std::atomic<HANDLE> crashFile{INVALID_HANDLE_VALUE};
inline LONG WINAPI fatalException(EXCEPTION_POINTERS* info) {
    const auto file=crashFile.load();
    if(file!=INVALID_HANDLE_VALUE) {
        char line[96]{};const auto code=info&&info->ExceptionRecord?info->ExceptionRecord->ExceptionCode:0;
        const int n=snprintf(line,sizeof line,"[fatal] pid=%lu native_exception=0x%08lx\n",GetCurrentProcessId(),code);
        DWORD written=0;WriteFile(file,line,(DWORD)n,&written,nullptr);FlushFileBuffers(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
class Logger {
public:
    ~Logger(){shutdown();}
    void attach(const std::filesystem::path& base,const std::string& component) {
        shutdown();state=readState(base);if(state.phase!="collecting"||state.session.empty())return;
        folder=base/wide(state.session); std::filesystem::create_directories(folder);name=component;
        if(std::filesystem::exists(folder/L"capture-error.txt"))return;
        stopEvent=CreateEventW(nullptr,TRUE,FALSE,eventName(state).c_str());if(!stopEvent)throw std::runtime_error("log_event_failed");
        // Budget is shared by all writers. The host seeds it from persisted segments on startup.
        budgetMap=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(LONG64),(eventName(state)+L"-budget").c_str());
        const bool fresh=GetLastError()!=ERROR_ALREADY_EXISTS;
        budget=budgetMap?(volatile LONG64*)MapViewOfFile(budgetMap,FILE_MAP_ALL_ACCESS,0,0,sizeof(LONG64)):nullptr;
        if(!budget){shutdown();throw std::runtime_error("log_budget_failed");}
        // Only the first writer initializes; the host attaches before launching children.
        if(fresh)InterlockedExchange64(budget,(LONG64)bytes(folder));
        prefix=std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+component;
        live=folder/wide(prefix+".active"); {std::ofstream marker(live);marker<<GetCurrentProcessId()<<' '<<processBirth(GetCurrentProcess());}
        crashFile.store(CreateFileW((folder/wide(prefix+"-fatal.log")).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        SetUnhandledExceptionFilter(fatalException);
        ending=false;enabled=WaitForSingleObject(stopEvent,0)!=WAIT_OBJECT_0;
        worker=std::thread([this]{writeLoop();});
        log("lifecycle","process_start session="+state.session+" component="+name);
    }
    bool active() const noexcept{return enabled.load(std::memory_order_relaxed);}
    void log(const std::string& component,const std::string& message) {
        if(!active())return;
        const bool important=component.find("failure")!=std::string::npos||component=="lifecycle";
        const auto line=timestamp()+" level="+(component.find("failure")!=std::string::npos?"error":"info")+" tick="+std::to_string(GetTickCount64())+" pid="+std::to_string(GetCurrentProcessId())
            +" tid="+std::to_string(GetCurrentThreadId())+" ["+component+"] "+sanitize(message)+"\n";
        std::lock_guard<std::mutex> lock(mutex);if(!enabled||ending)return;
        while(important&&queuedBytes+line.size()>queueLimit&&!queue.empty()){queuedBytes-=queue.front().size();queue.pop_front();++dropped;}
        if(queuedBytes+line.size()>queueLimit){++dropped;return;}queuedBytes+=line.size();queue.push_back(line);wake.notify_one();
    }
    void shutdown() {
        enabled=false;{std::lock_guard<std::mutex> lock(mutex);ending=true;}wake.notify_all();
        if(worker.joinable())worker.join();
        if(budget){UnmapViewOfFile((void*)budget);budget=nullptr;}if(budgetMap){CloseHandle(budgetMap);budgetMap=nullptr;}
        if(stopEvent){CloseHandle(stopEvent);stopEvent=nullptr;}
        const auto fatal=crashFile.exchange(INVALID_HANDLE_VALUE);if(fatal!=INVALID_HANDLE_VALUE)CloseHandle(fatal);
    }
    bool stoppedExternally()const {return stopEvent&&WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0;}
    std::filesystem::path directory()const{return folder;}
private:
    void writeLoop() noexcept {
        try {
            std::ofstream output;uint64_t size=0,lastResources=0;int segment=0;
            const auto append=[&](const std::string& line) {
                if(InterlockedAdd64(budget,(LONG64)line.size())>(LONG64)captureLimit){
                    InterlockedAdd64(budget,-(LONG64)line.size());throw std::runtime_error("capture_size_limit");}
                if(!output.is_open()||size+line.size()>segmentLimit){
                    if(output.is_open()){output.flush();if(!output)throw std::runtime_error("log_write_failed");output.close();}
                    output.open(folder/wide(prefix+"-"+std::to_string(segment++)+".log"),std::ios::binary);size=0;}
                output.write(line.data(),(std::streamsize)line.size());if(!output)throw std::runtime_error("log_write_failed");size+=line.size();
            };
            for(;;){
                std::deque<std::string> batch;uint64_t lost=0;bool done=false;
                {std::unique_lock<std::mutex> lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return ending||!queue.empty();});
                    done=ending||stoppedExternally();if(done)enabled=false;batch.swap(queue);queuedBytes=0;lost=dropped.exchange(0);}
                if(lost)append(timestamp()+" [capture] dropped_events="+std::to_string(lost)+"\n");
                for(const auto& line:batch)append(line);
                if(!done&&GetTickCount64()-lastResources>=30000) {
                    lastResources=GetTickCount64();PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
                    FILETIME created{},exited{},kernel{},user{};
                    if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)&&GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&memory,sizeof(memory))) {
                        const auto ticks=[](FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
                        append(timestamp()+" level=info pid="+std::to_string(GetCurrentProcessId())+" [resources] cpu100ns="+std::to_string(ticks(kernel)+ticks(user))+" workingSetBytes="+std::to_string(memory.WorkingSetSize)+" privateBytes="+std::to_string(memory.PrivateUsage)+"\n");
                    }
                }
                if(done){append(timestamp()+" [capture] segment_closed\n");break;}
                output.flush();if(output.is_open()&&!output)throw std::runtime_error("log_flush_failed");
            }
            output.flush();if(output.is_open()&&!output)throw std::runtime_error("log_flush_failed");
        } catch(const std::exception& e) {
            enabled=false;SetEvent(stopEvent);std::ofstream error(folder/L"capture-error.txt");error<<e.what();
        } catch(...) {enabled=false;SetEvent(stopEvent);}
        const auto fatal=crashFile.exchange(INVALID_HANDLE_VALUE);if(fatal!=INVALID_HANDLE_VALUE)CloseHandle(fatal);
        std::error_code ec;std::filesystem::remove(live,ec);
    }
    State state;std::string name,prefix;std::filesystem::path folder,live;
    HANDLE stopEvent=nullptr,budgetMap=nullptr;volatile LONG64* budget=nullptr;
    std::atomic<bool> enabled{false};bool ending=true;std::atomic<uint64_t>dropped{0};
    std::mutex mutex;std::condition_variable wake;std::thread worker;std::deque<std::string>queue;size_t queuedBytes=0;
};
inline Logger& logger(){static Logger instance;return instance;}
inline void log(const std::string& component,const std::string& message){logger().log(component,message);}
inline void startHost() {
    auto s=readState();
    if(s.phase=="collecting"&&!s.session.empty()) {
        std::error_code ec;
        for(const auto& f:std::filesystem::directory_iterator(root()/wide(s.session),ec))if(f.path().extension()==L".active") {
            DWORD pid=0;uint64_t birth=0;std::ifstream in(f.path());in>>pid>>birth;
            HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
            const bool alive=process&&processBirth(process)==birth&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;
            if(process)CloseHandle(process);
            if(!alive){s.error="capture_resumed_after_interruption";writeState(s);std::filesystem::remove(f.path(),ec);}
        }
    }
    if(s.phase=="armed"){
        GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("capture_id_failed");wchar_t b[40]{};StringFromGUID2(id,b,40);
        s.session=utf8(std::wstring(b+1,36));std::transform(s.session.begin(),s.session.end(),s.session.begin(),[](char c){return (char)tolower(c);});
        s.started=timestamp();s.phase="collecting";s.error.clear();writeState(s);
    }
    logger().attach(root(),"host");
}
inline State status() {
    auto s=readState();if(s.phase=="collecting"){
        std::ifstream in(root()/wide(s.session)/L"capture-error.txt");std::string error;
        if(std::getline(in,error)&&!error.empty()){s.phase="paused";s.error=error;}
    }return s;
}
inline void arm(bool enabled) {
    auto s=readState();if(s.phase=="collecting"||s.phase=="stopped")throw std::runtime_error("capture_pending");
    s.phase=enabled?"armed":"off";writeState(s);
}
inline void stop() {
    auto s=readState();if(s.phase=="stopped")return;if(s.phase!="collecting")throw std::runtime_error("capture_not_active");
    // Persist before signaling so late children cannot join a closing capture.
    s.phase="stopped";writeState(s);
    HANDLE event=CreateEventW(nullptr,TRUE,TRUE,eventName(s).c_str());if(event){SetEvent(event);CloseHandle(event);}
    logger().shutdown();
}
inline bool writersClosed(const std::filesystem::path& folder) {
    for(const auto& file:std::filesystem::directory_iterator(folder))if(file.path().extension()==L".active"){
        DWORD pid=0;uint64_t birth=0;std::ifstream in(file.path());in>>pid>>birth;if(!pid)continue;
        HANDLE p=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(p){bool alive=WaitForSingleObject(p,0)==WAIT_TIMEOUT && processBirth(p)==birth;CloseHandle(p);if(alive)return false;}
    }return true;
}
inline void exportText(const std::filesystem::path& destination,const std::string& session,const std::filesystem::path& base=root()) {
    const auto s=readState(base);if(s.phase!="stopped"||s.session!=session)throw std::runtime_error("capture_changed");
    const auto folder=base/wide(session);
    for(int i=0;i<100&&!writersClosed(folder);++i)Sleep(50);
    if(!writersClosed(folder))throw std::runtime_error("capture_writers_busy");
    if(!destination.is_absolute()||_wcsicmp(destination.extension().c_str(),L".txt")!=0)throw std::runtime_error("invalid_log_destination");
    // Never overwrite the staging area or its state through the save dialog.
    const auto canonical=std::filesystem::weakly_canonical(destination),storage=std::filesystem::weakly_canonical(base);
    auto dest=canonical.wstring(), staging=storage.wstring()+L"\\";
    if(_wcsnicmp(dest.c_str(),staging.c_str(),staging.size())==0)throw std::runtime_error("invalid_log_destination");
    auto temporary=destination;temporary+=L".LightHostModern-"+wide(session)+L".pending";
    try {
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("log_export_write_failed");
        out<<"LightHostModern diagnostic capture\nSession: "<<session<<"\nStarted UTC: "<<s.started
           <<"\nExported UTC: "<<timestamp()<<"\nEvents are grouped by process segment; use UTC/tick and operation IDs to correlate.\n\n";
        std::vector<std::filesystem::path> files;
        for(const auto& f:std::filesystem::directory_iterator(folder))if(f.path().extension()==L".log"||f.path().filename()==L"capture-error.txt")files.push_back(f.path());
        std::sort(files.begin(),files.end());
        for(const auto& f:files){std::ifstream in(f,std::ios::binary);if(!in)throw std::runtime_error("log_export_read_failed");
            if(std::filesystem::file_size(f)==0)continue;
            out<<"\n--- "<<utf8(f.filename().wstring())<<" ---\n";out<<in.rdbuf();if(in.bad()||!out)throw std::runtime_error("log_export_write_failed");}
        out.flush();if(!out)throw std::runtime_error("log_export_write_failed");out.close();
        if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("log_export_write_failed");
        // Receipt is committed only after the destination is complete. Host owns state transitions.
        std::ofstream receipt(folder/L"exported.txt");receipt<<timestamp();receipt.flush();if(!receipt)throw std::runtime_error("log_receipt_failed");
    } catch(...) {std::error_code ec;std::filesystem::remove(temporary,ec);throw;}
}
inline void complete(const std::string& session) {
    auto s=readState();if(s.phase=="off"&&s.session==session)return;
    if(s.phase!="stopped"||s.session!=session||!std::filesystem::exists(root()/wide(session)/L"exported.txt"))throw std::runtime_error("capture_not_exported");
    s.phase="off";writeState(s); // Persist export before removing only this capture's owned flat files.
    const auto folder=root()/wide(session);
    if(!session.empty()&&std::filesystem::weakly_canonical(folder).parent_path()==std::filesystem::weakly_canonical(root())) {
        std::error_code ec;
        for(const auto& f:std::filesystem::directory_iterator(folder,ec))if(f.is_regular_file(ec))std::filesystem::remove(f.path(),ec);
        std::filesystem::remove(folder,ec);
    }
}
}
