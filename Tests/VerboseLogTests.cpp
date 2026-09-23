#include "VerboseLog.h"
#include "ScanProcess.h"
#include <iostream>

using namespace lightHostModern;
static void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
int wmain(int argc,wchar_t** argv)
{
    try {
        if(argc==1) {
            wchar_t executable[32768]{},temp[32768]{};GetModuleFileNameW(nullptr,executable,32768);GetTempPathW(32768,temp);
            const auto profile=L"logs-"+std::to_wstring(GetCurrentProcessId());
            const auto result=scan::run(executable,L"--test-profile "+profile+L" --profile-root "+scan::quoteArgument(temp),[]{return false;},30000);
            require(result.outcome==scan::Exit::success,"logging child regression failed");return 0;
        }
        const auto base=verbose::root();require(RuntimeProfile::current().test,"isolated profile required");
        if(std::wstring(argv[argc-1])==L"--writer") {
            verbose::logger().attach(base,"scanner");verbose::log("fixture","child_event");
            while(verbose::logger().active())Sleep(10);verbose::logger().shutdown();return 0;
        }
        require(verbose::readState().phase=="off","initially off");
        verbose::arm(true);require(verbose::readState().phase=="armed"&&!verbose::logger().active(),"arming must not start capture");
        verbose::arm(false);require(verbose::readState().phase=="off","cancel restart scheduling");
        verbose::arm(true);verbose::startHost();const auto session=verbose::readState().session;
        require(verbose::logger().active()&&!session.empty(),"startup capture");
        verbose::log("fixture","parent_event "+verbose::utf8(RuntimeProfile::environment(L"USERPROFILE"))+"\\fixture");
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        auto command=scan::quoteArgument(executable)+RuntimeProfile::current().arguments()+L" --writer";
        STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
        require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"child writer");
        ipc::Handle child(process.hProcess),thread(process.hThread);
        struct ChildCleanup{HANDLE child;~ChildCleanup(){if(WaitForSingleObject(child,0)==WAIT_TIMEOUT){verbose::stop();WaitForSingleObject(child,3000);}}}childCleanup{child.get()};
        bool childReady=false;
        for(int i=0;i<200&&!childReady;++i){
            for(const auto& f:std::filesystem::directory_iterator(base/verbose::wide(session)))
                if(f.path().filename().wstring().find(L"-scanner-")!=std::wstring::npos){std::ifstream log(f.path());std::string line;while(std::getline(log,line))if(line.find("child_event")!=std::string::npos)childReady=true;}
            Sleep(10);
        }
        require(childReady,"child logs before stop");
        verbose::stop();require(WaitForSingleObject(child.get(),3000)==WAIT_OBJECT_0,"stop reaches existing child");
        require(!verbose::logger().active()&&verbose::readState().phase=="stopped","stop persisted");
        verbose::log("fixture","must_not_appear_after_stop");
        bool refused=false;try{verbose::exportText(base/L"invalid.txt",session);}catch(...){refused=true;}
        require(refused&&verbose::readState().phase=="stopped","export error preserves capture");
        verbose::startHost();require(!verbose::logger().active(),"stopped capture does not restart");
        const auto destination=RuntimeProfile::current().directory/L"diagnostics 日本.txt";
        verbose::exportText(destination,session);
        std::ifstream input(destination,std::ios::binary);std::string content((std::istreambuf_iterator<char>(input)),{});input.close();
        require(content.find("parent_event")!=std::string::npos&&content.find("child_event")!=std::string::npos,"merged processes");
        require(content.find("<USERPROFILE>")!=std::string::npos&&content.find("must_not_appear_after_stop")==std::string::npos,"privacy and stop boundary");
        verbose::complete(session);verbose::complete(session);require(verbose::readState().phase=="off","complete is persistent and idempotent");
        require(!std::filesystem::exists(base/verbose::wide(session)),"exported intermediates removed");
        verbose::arm(true);verbose::startHost();const auto limited=verbose::readState();
        HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(verbose::eventName(limited)+L"-budget").c_str());
        require(mapping!=nullptr,"shared capture budget");
        auto budget=(volatile LONG64*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(LONG64));require(budget!=nullptr,"budget view");
        InterlockedExchange64(budget,256LL*1024*1024);verbose::log("fixture","budget_limit");
        for(int i=0;i<200&&verbose::status().phase!="paused";++i)Sleep(10);
        require(verbose::status().phase=="paused","size cap pauses visibly");
        UnmapViewOfFile((void*)budget);CloseHandle(mapping);verbose::stop();
        verbose::exportText(destination,limited.session);verbose::complete(limited.session);
        std::filesystem::remove(destination);std::filesystem::remove(base/L"state.txt");
        std::filesystem::remove(base);std::filesystem::remove(base.parent_path());std::filesystem::remove(RuntimeProfile::current().directory);
        std::cout<<"Verbose capture lifecycle, child stop, export, privacy and limits passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
