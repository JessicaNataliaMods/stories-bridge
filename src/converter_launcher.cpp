// Standalone offline launcher. No game files are linked or embedded.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>

std::wstring quote(const std::wstring& value) {
    std::wstring result=L"\"";size_t slashes=0;
    for(wchar_t c:value) {
        if(c==L'\\'){++slashes;continue;}
        if(c==L'\"'){result.append(2*slashes+1,L'\\');result+=c;}
        else{result.append(slashes,L'\\');result+=c;}
        slashes=0;
    }
    result.append(2*slashes,L'\\');return result+L"\"";
}

int wmain(int argc,wchar_t** argv) {
    wchar_t module[32768];auto length=GetModuleFileNameW(nullptr,module,32768);
    if(!length || length==32768){std::cerr<<"Cannot locate converter.\n";return 1;}
    const auto root=std::filesystem::path(std::wstring(module,length)).parent_path()/L"resources";
    const auto python=root/L"python"/L"python.exe";
    const auto script=root/L"tools"/L"convert-ps2-map.py";
    if(!std::filesystem::is_regular_file(python)||!std::filesystem::is_regular_file(script)) {
        std::cerr<<"Keep StoriesConverter.exe and its resources directory together.\n";return 1;
    }
    std::wstring command=quote(python.wstring())+L" -E -s -B "+quote(script.wstring());
    for(int i=1;i<argc;++i)command+=L" "+quote(argv[i]);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(python.c_str(),command.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&startup,&process)) {
        std::cerr<<"Cannot start offline converter (Windows error "<<GetLastError()<<").\n";return 1;
    }
    WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return int(code);
}
