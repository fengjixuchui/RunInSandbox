#include <conio.h>
#include <iostream>
#include <Shlobj.h>
#include <atlbase.h>
#include <atlcom.h>
#include <wincred.h>
#pragma comment(lib, "Credui.lib")
#include "ComCreate.hpp"
#include "ProcCreate.hpp"
#include "../TestControl/TestControl_h.h"


int wmain (int argc, wchar_t *argv[]) {
    if (argc < 2) {
        std::wcerr << L"Too few arguments\n.";
        std::wcerr << L"Usage: RunInSandbox.exe [ac|li|mi|hi] ProgID [username] [password]\n";
        std::wcerr << L"Usage: RunInSandbox.exe [ac|li|mi|hi] ExePath|URL\n";
        return -1;
    }

    int arg_idx = 1;
    IntegrityLevel mode = FromString(argv[arg_idx]);
    if (mode != IntegrityLevel::Default)
        arg_idx++;

    // check if 1st argument is a COM class ProgID
    CLSID clsid = {};
    std::wstring progid = argv[arg_idx];
    bool progid_provided = SUCCEEDED(CLSIDFromProgID(progid.c_str(), &clsid));
    bool url_provided = std::wstring(argv[arg_idx]).substr(0, 4) == L"http";

    if (progid_provided) {
        // initialize multi-threaded COM apartment
        if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED)))
            abort();

    #if 0
        // attempt to disable COM security and enable cloaking
        HRESULT hr = CoInitializeSecurity(nullptr, -1/*auto*/, nullptr, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE /*EOAC_STATIC_CLOAKING*/, NULL);
        if (FAILED(hr))
            abort();
    #endif

        std::wcout << L"Creating COM object " << progid << L" in " << ToString(mode).c_str() << L"...\n";

        CComPtr<IUnknown> obj;
        if ((mode == IntegrityLevel::High) && !IsUserAnAdmin()) {
            // launch "COM Elevation Moniker"-compatible COM class in elevated process
            // example COM class for testing: HNetCfg.FwOpenPort
            CHECK(CoCreateInstanceElevated<IUnknown>(0, clsid, &obj));
            std::wcout << L"COM server sucessfully created in elevated process.\n";
        } else {
            arg_idx++;
            wchar_t* user = (argc > arg_idx) ? argv[arg_idx++] : nullptr;
            wchar_t* pw = (argc > arg_idx) ? argv[arg_idx++] : nullptr;
            obj = CoCreateAsUser_impersonate(clsid, mode, user, pw);
            //CComPtr<IUnknown> obj = CoCreateAsUser_dcom(clsid, user, pw);
        }

        // try to add two numbers
        CComPtr<ISimpleCalculator> calc;
        obj.QueryInterface(&calc);
        if (calc) {
            int sum = 0;
            CHECK(calc->Add(2, 3, &sum));

            std::wcout << L"Add(2, 3) returned " << sum << L".\n";
            assert(sum == 2 + 3);

            BOOL is_elevated = false, high_integrity = false;
            CHECK(calc->IsElevated(&is_elevated, &high_integrity));
            std::wcout << L"IsElevated: " << (is_elevated ? L"true" : L"false") << L"\n";
            std::wcout << L"HighIntegrity: " << (high_integrity ? L"true" : L"false") << L"\n";

#if 0
            BOOL has_network = false;
            CComBSTR host = L"1.1.1.1"; // cloudflare
            calc->TestNetworkConnection(host, 80, &has_network);
            std::wcout << L"HasNetwork: " << (has_network ? L"true" : L"false") << L"\n";
#endif
#if 0
            // try to create child object in elevated process
            // WARNING: Doesn't trigger UAC elevation if launched from a medium-integrity process that was launched from an elevated process
            std::wcout << L"Creating child COM object " << progid << L" in " << ToString(IntegrityLevel::High).c_str() << L"...\n";
            CComPtr<IUnknown> child;
            CHECK(calc->CreateInstance(true, clsid, &child));
            CComPtr<ISimpleCalculator> child_calc;
            child_calc = child;
            is_elevated = false, high_integrity = false;
            CHECK(child_calc->IsElevated(&is_elevated, &high_integrity));
            std::wcout << L"Child IsElevated: " << (is_elevated ? L"true" : L"false") << L"\n";
            std::wcout << L"Child HighIntegrity: " << (high_integrity ? L"true" : L"false") << L"\n";
#endif
        }

        // try to make window visible
        SetComAttribute(obj, L"Visible", true);

        Sleep(2000); // wait 2sec to keep the child process alive a bit
    } else if (url_provided) {
        std::wcout << L"Opening URL " << progid << " in default browser\n";
        if (ImpersonateThread::GetProcessLevel() == IntegrityLevel::Low)
            std::wcout << L"WARNING: Does not seem to work in low-integrity!\n";

        int ret = (int)reinterpret_cast<INT_PTR>(ShellExecuteW(NULL, NULL, progid.c_str(), NULL, NULL, SW_SHOWNORMAL));
        if (ret <= 32) {
            std::wcout << L"ShellExecute failed with code " << ret << std::endl;
            return ret;
        }
    } else {
        std::wcout << L"Starting executable " << progid << L" in " << ToString(mode).c_str() << L"...\n";

        int extra_args = argc - arg_idx - 1;
        ProcCreate(progid.c_str(), mode, false, extra_args, extra_args > 0 ? &argv[arg_idx+1] : nullptr);
    }

    std::wcout << L"[done]" << std::endl;
}
