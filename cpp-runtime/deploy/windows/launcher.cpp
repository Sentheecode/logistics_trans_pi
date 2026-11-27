#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {
constexpr DWORD path_limit=32768;

fs::path package_directory() {
    std::wstring path(path_limit,L'\0');
    const auto length=GetModuleFileNameW(nullptr,path.data(),path.size());
    if (!length || length>=path.size()) throw std::runtime_error("Cannot locate the package");
    path.resize(length);
    return fs::path(path).parent_path();
}

fs::path data_directory(const fs::path& package) {
    std::wstring path(path_limit,L'\0');
    const auto length=GetEnvironmentVariableW(L"LTP_PREVIEW_DATA_DIR",path.data(),path.size());
    if (!length) return package/"simulation-data";
    if (length>=path.size()) throw std::runtime_error("Simulation path is too long");
    path.resize(length);
    return fs::absolute(fs::path(path));
}

// Windows paths cannot contain an embedded quote. The final slash is doubled
// so the closing quote remains a delimiter under Windows argv rules.
std::wstring argument(const fs::path& path) {
    auto text=path.wstring();
    const auto tail=text.find_last_not_of(L'\\');
    const auto slashes=tail==std::wstring::npos ? text.size() : text.size()-tail-1;
    return L"\""+text+std::wstring(slashes,L'\\')+L"\"";
}

DWORD start(const fs::path& executable, const fs::path& data, const fs::path& output) {
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    const auto log=CreateFileW(output.c_str(),GENERIC_WRITE,FILE_SHARE_READ,
                               &security,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (log==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open the process log");
    const auto input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                                 &security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (input==INVALID_HANDLE_VALUE) {
        CloseHandle(log);
        throw std::runtime_error("Cannot open NUL for process input");
    }
    STARTUPINFOW startup{};
    startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=input;
    startup.hStdOutput=log;
    startup.hStdError=log;
    PROCESS_INFORMATION child{};
    auto command=argument(executable)+L" "+argument(data);
    const auto ok=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
                                 CREATE_NO_WINDOW,nullptr,executable.parent_path().c_str(),&startup,&child);
    const auto error=GetLastError();
    CloseHandle(input);
    CloseHandle(log);
    if (!ok) throw std::runtime_error("Cannot start "+executable.filename().string()+" ("+std::to_string(error)+")");
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return child.dwProcessId;
}
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    try {
        const auto package=package_directory();
        const auto data=data_directory(package);
        const auto instance=std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
        const auto logs=package/"preview-logs"/instance;
        fs::create_directories(logs);
        std::ofstream processes(logs/"processes.txt");
        processes.exceptions(std::ios::failbit|std::ios::badbit);
        processes<<"collector "<<start(package/"collector.exe",data,logs/"collector.log")<<std::endl;
        processes<<"hmi "<<start(package/"hmi.exe",data,logs/"hmi.log")<<std::endl;
        return 0;
    } catch (const std::exception& error) {
        MessageBoxA(nullptr,error.what(),"Logistics Preview",MB_OK|MB_ICONERROR);
        return 1;
    }
}
