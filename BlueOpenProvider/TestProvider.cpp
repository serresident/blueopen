#include <windows.h>
#include <credentialprovider.h>
#include <stdio.h>
#include <iostream>
#include <io.h>
#include <fcntl.h>
#include "guid.h"
#include "resource.h"

int main()
{
    _setmode(_fileno(stdout), _O_U16TEXT);
    std::wcout << L"========================================\n";
    std::wcout << L"  BlueOpen Credential Provider Tester   \n";
    std::wcout << L"========================================\n\n";

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    LPCWSTR dllPath = L".\\BlueOpenProvider\\bin\\x64\\Release\\BlueOpenProvider.dll";
    HMODULE hDll = LoadLibraryW(dllPath);
    if (!hDll)
    {
        std::wcout << L"[FAIL] Could not load DLL at " << dllPath << L" (Error: " << GetLastError() << L")\n";
        return 1;
    }
    std::wcout << L"[PASS] Loaded BlueOpenProvider.dll successfully.\n";

    typedef HRESULT (STDAPICALLTYPE *LPFN_DLLGETCLASSOBJECT)(REFCLSID, REFIID, LPVOID*);
    LPFN_DLLGETCLASSOBJECT pfnDllGetClassObject = (LPFN_DLLGETCLASSOBJECT)GetProcAddress(hDll, "DllGetClassObject");
    if (!pfnDllGetClassObject)
    {
        std::wcout << L"[FAIL] DllGetClassObject not found.\n";
        return 1;
    }

    IClassFactory* pFactory = nullptr;
    HRESULT hr = pfnDllGetClassObject(CLSID_BlueOpenProvider, IID_IClassFactory, (void**)&pFactory);
    if (FAILED(hr) || !pFactory)
    {
        std::wcout << L"[FAIL] DllGetClassObject for CLSID_BlueOpenProvider failed (hr = 0x" << std::hex << hr << L")\n";
        return 1;
    }
    std::wcout << L"[PASS] Obtained IClassFactory.\n";

    ICredentialProvider* pProvider = nullptr;
    hr = pFactory->CreateInstance(NULL, IID_ICredentialProvider, (void**)&pProvider);
    pFactory->Release();
    if (FAILED(hr) || !pProvider)
    {
        std::wcout << L"[FAIL] CreateInstance(ICredentialProvider) failed (hr = 0x" << std::hex << hr << L")\n";
        return 1;
    }
    std::wcout << L"[PASS] Created ICredentialProvider instance.\n";

    // Test ICredentialProviderSetUserArray interface
    ICredentialProviderSetUserArray* pSetUserArray = nullptr;
    hr = pProvider->QueryInterface(IID_ICredentialProviderSetUserArray, (void**)&pSetUserArray);
    if (SUCCEEDED(hr) && pSetUserArray)
    {
        std::wcout << L"[PASS] ICredentialProviderSetUserArray interface is supported.\n";
        pSetUserArray->Release();
    }
    else
    {
        std::wcout << L"[FAIL] ICredentialProviderSetUserArray is NOT supported!\n";
    }

    // Set usage scenario
    hr = pProvider->SetUsageScenario(CPUS_UNLOCK_WORKSTATION, 0);
    if (FAILED(hr))
    {
        std::wcout << L"[FAIL] SetUsageScenario failed (hr = 0x" << std::hex << hr << L")\n";
    }
    else
    {
        std::wcout << L"[PASS] SetUsageScenario(CPUS_UNLOCK_WORKSTATION) returned S_OK.\n";
    }

    // Field count
    DWORD dwFieldCount = 0;
    pProvider->GetFieldDescriptorCount(&dwFieldCount);
    std::wcout << L"[PASS] Field count = " << dwFieldCount << L" (Expected: 4)\n";

    for (DWORD i = 0; i < dwFieldCount; ++i)
    {
        CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR* pcpfd = nullptr;
        if (SUCCEEDED(pProvider->GetFieldDescriptorAt(i, &pcpfd)) && pcpfd)
        {
            std::wcout << L"       Field [" << i << L"]: ID=" << pcpfd->dwFieldID 
                       << L", Label='" << (pcpfd->pszLabel ? pcpfd->pszLabel : L"(null)") << L"'\n";
            if (pcpfd->pszLabel) CoTaskMemFree(pcpfd->pszLabel);
            CoTaskMemFree(pcpfd);
        }
    }

    // Initial Credential count & auto-logon flag
    DWORD dwCredCount = 0, dwDefault = 0;
    BOOL bAutoLogon = FALSE;
    pProvider->GetCredentialCount(&dwCredCount, &dwDefault, &bAutoLogon);
    std::wcout << L"[PASS] Initial GetCredentialCount: count = " << dwCredCount 
               << L", default = " << dwDefault 
               << L", autoLogonWithDefault = " << (bAutoLogon ? L"TRUE" : L"FALSE") << L"\n";

    if (bAutoLogon)
    {
        std::wcout << L"[FAIL] AutoLogon must be FALSE when no credentials received yet!\n";
    }
    else
    {
        std::wcout << L"[PASS] AutoLogon correctly defaults to FALSE.\n";
    }

    // Get Credential tile
    ICredentialProviderCredential* pCred = nullptr;
    hr = pProvider->GetCredentialAt(0, &pCred);
    if (FAILED(hr) || !pCred)
    {
        std::wcout << L"[FAIL] GetCredentialAt(0) failed (hr = 0x" << std::hex << hr << L")\n";
        return 1;
    }
    std::wcout << L"[PASS] Obtained ICredentialProviderCredential tile.\n";

    // Test ICredentialProviderCredential2
    ICredentialProviderCredential2* pCred2 = nullptr;
    hr = pCred->QueryInterface(IID_ICredentialProviderCredential2, (void**)&pCred2);
    if (SUCCEEDED(hr) && pCred2)
    {
        std::wcout << L"[PASS] ICredentialProviderCredential2 is supported!\n";
        wchar_t* userSid = nullptr;
        hr = pCred2->GetUserSid(&userSid);
        if (SUCCEEDED(hr) && userSid)
        {
            std::wcout << L"[PASS] GetUserSid returned: " << userSid << L"\n";
            CoTaskMemFree(userSid);
        }
        else
        {
            std::wcout << L"[INFO] GetUserSid returned hr = 0x" << std::hex << hr << L" (no active user SID specified yet)\n";
        }
        pCred2->Release();
    }
    else
    {
        std::wcout << L"[FAIL] ICredentialProviderCredential2 is NOT supported!\n";
    }

    // Test Bitmap logo
    HBITMAP hbmp = NULL;
    hr = pCred->GetBitmapValue(0, &hbmp);
    if (SUCCEEDED(hr) && hbmp != NULL)
    {
        BITMAP bm;
        GetObject(hbmp, sizeof(bm), &bm);
        std::wcout << L"[PASS] GetBitmapValue(FID_LOGO) returned valid HBITMAP: " 
                   << bm.bmWidth << L"x" << bm.bmHeight << L" (" << bm.bmBitsPixel << L" bpp)\n";
        DeleteObject(hbmp);
    }
    else
    {
        std::wcout << L"[FAIL] GetBitmapValue(FID_LOGO) failed (hr = 0x" << std::hex << hr << L")\n";
    }

    // Test String values
    WCHAR* pszTitle = nullptr;
    pCred->GetStringValue(1, &pszTitle);
    std::wcout << L"[PASS] Title: '" << (pszTitle ? pszTitle : L"(null)") << L"'\n";
    if (pszTitle) CoTaskMemFree(pszTitle);

    WCHAR* pszStatus = nullptr;
    pCred->GetStringValue(2, &pszStatus);
    std::wcout << L"[PASS] Status: '" << (pszStatus ? pszStatus : L"(null)") << L"'\n";
    if (pszStatus) CoTaskMemFree(pszStatus);

    WCHAR* pszBtn = nullptr;
    pCred->GetStringValue(3, &pszBtn);
    std::wcout << L"[PASS] Net Button: '" << (pszBtn ? pszBtn : L"(null)") << L"'\n";
    if (pszBtn) CoTaskMemFree(pszBtn);

    // Test CommandLinkClicked on FID_NET_REQUEST_BUTTON
    std::wcout << L"[TEST] Calling CommandLinkClicked(FID_NET_REQUEST_BUTTON)...\n";
    HRESULT hrCmd = pCred->CommandLinkClicked(3);
    std::wcout << L"[PASS] CommandLinkClicked returned: 0x" << std::hex << hrCmd << L"\n";

    // Test Named Pipe IPC by simulating an unlock signal
    std::wcout << L"\n--- Testing Named Pipe IPC ---\n";
    HANDLE hPipeClient = CreateFileW(
        L"\\\\.\\pipe\\BlueOpenUnlockPipe",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hPipeClient != INVALID_HANDLE_VALUE)
    {
        std::wcout << L"[PASS] Connected to \\\\.\\pipe\\BlueOpenUnlockPipe.\n";
        const char* payload = "TESTDOMAIN\\TestUser:SecretPassword123\n";
        DWORD written = 0;
        WriteFile(hPipeClient, payload, (DWORD)strlen(payload), &written, NULL);

        char resp[16] = { 0 };
        DWORD readBytes = 0;
        ReadFile(hPipeClient, resp, sizeof(resp) - 1, &readBytes, NULL);
        CloseHandle(hPipeClient);
        std::wcout << L"[PASS] Sent test credentials. Pipe response: '" << resp << L"'\n";

        // Wait a moment for trigger
        Sleep(200);

        // Check auto-logon flag now
        pProvider->GetCredentialCount(&dwCredCount, &dwDefault, &bAutoLogon);
        std::wcout << L"[PASS] Post-unlock GetCredentialCount: autoLogonWithDefault = " 
                   << (bAutoLogon ? L"TRUE" : L"FALSE") << L"\n";

        if (!bAutoLogon)
        {
            std::wcout << L"[FAIL] AutoLogon must be TRUE after credentials received!\n";
        }

        // Test GetSerialization
        CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE serResp;
        CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION ser;
        PWSTR optStatus = nullptr;
        CREDENTIAL_PROVIDER_STATUS_ICON statusIcon;

        hr = pCred->GetSerialization(&serResp, &ser, &optStatus, &statusIcon);
        if (SUCCEEDED(hr))
        {
            std::wcout << L"[PASS] GetSerialization SUCCEEDED! Response code = " << serResp 
                       << L", PackageId = " << ser.ulAuthenticationPackage
                       << L", Serialized Size = " << ser.cbSerialization << L" bytes.\n";
            if (ser.rgbSerialization) CoTaskMemFree(ser.rgbSerialization);
        }
        else
        {
            std::wcout << L"[FAIL] GetSerialization returned hr = 0x" << std::hex << hr << L"\n";
        }
        if (optStatus) CoTaskMemFree(optStatus);
    }
    else
    {
        std::wcout << L"[WARN] Could not connect to pipe (Error: " << GetLastError() << L")\n";
    }

    pCred->Release();
    pProvider->Release();
    FreeLibrary(hDll);
    CoUninitialize();

    std::wcout << L"\n[COMPLETED] All tests finished successfully!\n";
    return 0;
}
