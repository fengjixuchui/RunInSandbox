#pragma once
#include "Sandboxing.hpp"


class StartupInfoWrap {
public:
    StartupInfoWrap() {
        si.StartupInfo.cb = sizeof(STARTUPINFOEX);

        const DWORD attr_count = 1; // only SECURITY_CAPABILITIES
        SIZE_T attr_size = 0;
        InitializeProcThreadAttributeList(NULL, attr_count, 0, /*out*/&attr_size);
        si.lpAttributeList = (PPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attr_size);
        WIN32_CHECK(InitializeProcThreadAttributeList(si.lpAttributeList, attr_count, 0, &attr_size));
    }

    void Update(SECURITY_CAPABILITIES & sc) {
        WIN32_CHECK(UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &sc, sizeof(sc), NULL, NULL));
    }

    ~StartupInfoWrap() {
        if (si.lpAttributeList) {
            DeleteProcThreadAttributeList(si.lpAttributeList);
            WIN32_CHECK(HeapFree(GetProcessHeap(), 0, si.lpAttributeList));
            si.lpAttributeList = nullptr;
        }
    }

    STARTUPINFOEX* operator& () {
        return &si;
    }

private:
    STARTUPINFOEX si = {};
};


/** Launch a new process within an AppContainer. */
static HandleWrap ProcCreate(const wchar_t * exe_path, IntegrityLevel mode, int argc, wchar_t *argv[]) {
    PROCESS_INFORMATION pi = {};
    StartupInfoWrap si;

    if (mode != IntegrityLevel::AppContainer) {
        std::wstring arguments = L"\"" + std::wstring(exe_path) + L"\"";
        if (argc == 0) {
            // mimic how svchost passes "-Embedding" argument
            arguments += L" -Embedding";
        } else {
            // append extra arguments
            for (int i = 0; i < argc; ++i) {
                arguments += L" ";
                arguments += argv[i];
            }
        }

        ImpersonateThread low_int(nullptr, nullptr, mode);
        std::wcout << L"Impersonation succeeded.\n";
        WIN32_CHECK(CreateProcessAsUser(low_int.m_token, exe_path, const_cast<wchar_t*>(arguments.data()), nullptr/*proc.attr*/, nullptr/*thread attr*/, FALSE, EXTENDED_STARTUPINFO_PRESENT, nullptr/*env*/, nullptr/*cur-dir*/, (STARTUPINFO*)&si, &pi));
    } else {
        AppContainerWrap ac;
        SECURITY_CAPABILITIES sec_cap = ac.SecCap();

        // create new AppContainer process, based on STARTUPINFO
        si.Update(sec_cap);

        // mimic how svchost passes "-Embedding" argument
        std::wstring cmdline = L"\"" + std::wstring(exe_path) + L"\" -Embedding";
        WIN32_CHECK(CreateProcess(nullptr, const_cast<wchar_t*>(cmdline.c_str()), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, (STARTUPINFO*)&si, &pi));
    }

    // wait for process to initialize
    // ignore failure if process is not a GUI app
    WaitForInputIdle(pi.hProcess, INFINITE);

    // wait a bit more (WaitForInputIdle doesn't seem to be sufficient)
    Sleep(200);

    // return process handle
    HandleWrap proc, thread;
    std::swap(*&proc, pi.hProcess);
    std::swap(*&thread, pi.hThread); // swap to avoid leak
    return proc;
}
