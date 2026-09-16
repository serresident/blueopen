#include "CBlueOpenCredential.h"
#include "CBlueOpenProvider.h"
#include "guid.h"
#include "resource.h"
#include <ntsecapi.h>
#include <wincred.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <sddl.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <cctype>

#pragma comment(lib, "credui.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

// 4a982c5e-0c12-40f4-8a48-4a5f4a7c1b52
static const GUID BluetoothServiceUuid = 
    { 0x4a982c5e, 0x0c12, 0x40f4, { 0x8a, 0x48, 0x4a, 0x5f, 0x4a, 0x7c, 0x1b, 0x52 } };

#ifndef CPGSR_NO_CREDENTIAL_NOT_FINISHED
#define CPGSR_NO_CREDENTIAL_NOT_FINISHED ((CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE)0)
#endif
#ifndef CPGSR_NO_CREDENTIAL_FINISHED
#define CPGSR_NO_CREDENTIAL_FINISHED ((CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE)1)
#endif
#ifndef CPGSR_RETURN_CREDENTIAL_FINISHED
#define CPGSR_RETURN_CREDENTIAL_FINISHED ((CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE)2)
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef CRED_PACK_PROTECTED_CREDENTIALS
#define CRED_PACK_PROTECTED_CREDENTIALS 0x1
#endif

// Log helper to write to a temp file accessible by SYSTEM
void WriteLog(const char* format, ...)
{
    FILE* f = nullptr;
    fopen_s(&f, "C:\\Users\\Public\\BlueOpenLog.txt", "a");
    if (f != nullptr)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        va_list args;
        va_start(args, format);
        vfprintf(f, format, args);
        va_end(args);
        fclose(f);
    }
}

CBlueOpenCredential::CBlueOpenCredential() :
    _cRef(1),
    _cpus(CPUS_INVALID),
    _pEvents(nullptr),
    _dwCredentialEventsCookie(0),
    _pProviderEvents(nullptr),
    _dwProviderEventsCookie(0),
    _upAdviseContext(0),
    _hasCredentials(FALSE),
    _hPipeThread(NULL),
    _hPipe(INVALID_HANDLE_VALUE),
    _bListening(FALSE),
    _hBtThread(NULL),
    _btListenSocket(INVALID_SOCKET),
    _bBtListening(FALSE)
{
    wcscpy_s(_szStatusText, L"Waiting for phone connection...");
    _szUsername[0] = L'\0';
    _szPassword[0] = L'\0';
    _szTargetUserSid[0] = L'\0';
}

CBlueOpenCredential::~CBlueOpenCredential()
{
    _bListening = FALSE;
    if (_hPipe != INVALID_HANDLE_VALUE)
    {
        if (_hPipeThread != NULL)
        {
            CancelSynchronousIo(_hPipeThread);
        }
        CloseHandle(_hPipe);
        _hPipe = INVALID_HANDLE_VALUE;
    }
    if (_hPipeThread != NULL)
    {
        HANDLE hDummy = CreateFileW(L"\\\\.\\pipe\\BlueOpenUnlockPipe", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hDummy != INVALID_HANDLE_VALUE) CloseHandle(hDummy);

        WaitForSingleObject(_hPipeThread, 1000);
        CloseHandle(_hPipeThread);
        _hPipeThread = NULL;
    }

    _bBtListening = FALSE;
    if (_btListenSocket != INVALID_SOCKET)
    {
        closesocket(_btListenSocket);
        _btListenSocket = INVALID_SOCKET;
    }
    if (_hBtThread != NULL)
    {
        CancelSynchronousIo(_hBtThread);
        WaitForSingleObject(_hBtThread, 1000);
        CloseHandle(_hBtThread);
        _hBtThread = NULL;
    }

    if (_pEvents != nullptr)
    {
        _pEvents->Release();
        _pEvents = nullptr;
    }
    if (_pProviderEvents != nullptr)
    {
        _pProviderEvents->Release();
        _pProviderEvents = nullptr;
    }

    IGlobalInterfaceTable* pGIT = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, IID_IGlobalInterfaceTable, (void**)&pGIT);
    if (SUCCEEDED(hr) && pGIT != nullptr)
    {
        if (_dwCredentialEventsCookie != 0)
        {
            pGIT->RevokeInterfaceFromGlobal(_dwCredentialEventsCookie);
            _dwCredentialEventsCookie = 0;
        }
        if (_dwProviderEventsCookie != 0)
        {
            pGIT->RevokeInterfaceFromGlobal(_dwProviderEventsCookie);
            _dwProviderEventsCookie = 0;
        }
        pGIT->Release();
    }
}

HRESULT CBlueOpenCredential::Initialize(_In_ CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus)
{
    _cpus = cpus;

    WriteLog("[BlueOpen] Initializing credential tile, scenario = %d...\n", (int)cpus);

    // 1. Always start Named Pipe Listener thread (for communication with BlueOpenServer)
    _bListening = TRUE;
    _hPipeThread = CreateThread(NULL, 0, PipeListenerThread, this, 0, NULL);
    if (_hPipeThread == NULL)
    {
        WriteLog("[BlueOpen] Failed to create PipeListenerThread, error = %d\n", GetLastError());
    }
    else
    {
        WriteLog("[BlueOpen] PipeListenerThread started successfully.\n");
    }

    // 2. Check if BlueOpenServer.exe is running.
    // If NOT running (pre-logon boot or after sign-out), start direct Bluetooth listener!
    if (!IsServerProcessRunning())
    {
        WriteLog("[BlueOpen] BlueOpenServer.exe is NOT running. Starting direct Bluetooth Winsock listener...\n");
        _bBtListening = TRUE;
        _hBtThread = CreateThread(NULL, 0, BtListenerThread, this, 0, NULL);
        if (_hBtThread == NULL)
        {
            WriteLog("[BlueOpen] Failed to create BtListenerThread, error = %d\n", GetLastError());
        }
        else
        {
            WriteLog("[BlueOpen] BtListenerThread started successfully.\n");
        }
    }
    else
    {
        WriteLog("[BlueOpen] BlueOpenServer.exe is already running in user session. Deferring Bluetooth to server.\n");
    }

    return S_OK;
}

void CBlueOpenCredential::SetTargetUserSid(_In_ PCWSTR pszSid)
{
    if (pszSid != nullptr)
    {
        wcscpy_s(_szTargetUserSid, pszSid);
        WriteLog("[BlueOpen] SetTargetUserSid: %S\n", _szTargetUserSid);
    }
}

void CBlueOpenCredential::SetProviderEvents(ICredentialProviderEvents* pEvents, UINT_PTR upAdviseContext)
{
    IGlobalInterfaceTable* pGIT = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, IID_IGlobalInterfaceTable, (void**)&pGIT);
    if (SUCCEEDED(hr) && pGIT != nullptr)
    {
        if (_dwProviderEventsCookie != 0)
        {
            pGIT->RevokeInterfaceFromGlobal(_dwProviderEventsCookie);
            _dwProviderEventsCookie = 0;
        }

        if (pEvents != nullptr)
        {
            hr = pGIT->RegisterInterfaceInGlobal(pEvents, IID_ICredentialProviderEvents, &_dwProviderEventsCookie);
            if (FAILED(hr))
            {
                WriteLog("[BlueOpen] RegisterInterfaceInGlobal (ProviderEvents) failed, hr = 0x%08X\n", hr);
            }
        }
        pGIT->Release();
    }
    else
    {
        WriteLog("[BlueOpen] Failed to get GIT, hr = 0x%08X\n", hr);
    }

    if (_pProviderEvents != nullptr)
    {
        _pProviderEvents->Release();
    }
    _pProviderEvents = pEvents;
    _upAdviseContext = upAdviseContext;
    if (_pProviderEvents != nullptr)
    {
        _pProviderEvents->AddRef();
    }
    WriteLog("[BlueOpen] SetProviderEvents called. Events = %p, context = %p, cookie = %u\n", pEvents, (void*)upAdviseContext, _dwProviderEventsCookie);
}

// IUnknown
HRESULT CBlueOpenCredential::QueryInterface(_In_ REFIID riid, _Outptr_ void** ppv)
{
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || 
        riid == IID_ICredentialProviderCredential || 
        riid == IID_ICredentialProviderCredential2)
    {
        *ppv = static_cast<ICredentialProviderCredential2*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG CBlueOpenCredential::AddRef()
{
    return InterlockedIncrement(&_cRef);
}

ULONG CBlueOpenCredential::Release()
{
    ULONG cRef = InterlockedDecrement(&_cRef);
    if (cRef == 0)
    {
        delete this;
    }
    return cRef;
}

// ICredentialProviderCredential2
HRESULT CBlueOpenCredential::GetUserSid(_Outptr_ wchar_t** sid)
{
    if (sid == nullptr) return E_POINTER;
    *sid = nullptr;

    // 1. If we have a target SID from SetUserArray, return it!
    if (_szTargetUserSid[0] != L'\0')
    {
        WriteLog("[BlueOpen] GetUserSid: returning target SID '%S'\n", _szTargetUserSid);
        return SHStrDupW(_szTargetUserSid, sid);
    }

    // 2. Fallback: try reading config.json and lookup SID by username
    std::wstring winUser, winDomain, winPass;
    std::string authSecret;
    if (LoadConfig(winUser, winDomain, winPass, authSecret) && !winUser.empty())
    {
        WCHAR szDomain[256] = { 0 };
        DWORD cbDomain = ARRAYSIZE(szDomain);
        SID_NAME_USE peUse;
        BYTE sidBuffer[SECURITY_MAX_SID_SIZE] = { 0 };
        DWORD cbSid = sizeof(sidBuffer);

        if (LookupAccountNameW(NULL, winUser.c_str(), (PSID)sidBuffer, &cbSid, szDomain, &cbDomain, &peUse))
        {
            LPWSTR pStrSid = nullptr;
            if (ConvertSidToStringSidW((PSID)sidBuffer, &pStrSid))
            {
                wcscpy_s(_szTargetUserSid, pStrSid);
                LocalFree(pStrSid);
                WriteLog("[BlueOpen] GetUserSid: resolved SID from config '%S' -> '%S'\n", winUser.c_str(), _szTargetUserSid);
                return SHStrDupW(_szTargetUserSid, sid);
            }
        }
    }

    WriteLog("[BlueOpen] GetUserSid: no SID available\n");
    return E_NOTIMPL;
}

// ICredentialProviderCredential
HRESULT CBlueOpenCredential::GetFieldState(_In_ DWORD dwFieldID, _Out_ CREDENTIAL_PROVIDER_FIELD_STATE* pcpfs, _Out_ CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* pcpfis)
{
    *pcpfis = CPFIS_NONE;
    switch (dwFieldID)
    {
    case FID_LOGO:
        *pcpfs = CPFS_DISPLAY_IN_BOTH;
        break;
    case FID_TITLE:
        *pcpfs = CPFS_DISPLAY_IN_BOTH;
        break;
    case FID_STATUS:
        *pcpfs = CPFS_DISPLAY_IN_BOTH;
        break;
    default:
        *pcpfs = CPFS_HIDDEN;
        break;
    }
    return S_OK;
}

HRESULT CBlueOpenCredential::GetStringValue(_In_ DWORD dwFieldID, _Outptr_ WCHAR** ppszVal)
{
    *ppszVal = nullptr;
    LPCWSTR src = nullptr;

    switch (dwFieldID)
    {
    case FID_TITLE:
        src = L"BlueOpen";
        break;
    case FID_STATUS:
        src = _szStatusText;
        break;
    }

    if (src == nullptr)
    {
        src = L"";
    }

    size_t len = wcslen(src) + 1;
    *ppszVal = (WCHAR*)CoTaskMemAlloc(len * sizeof(WCHAR));
    if (*ppszVal == nullptr) return E_OUTOFMEMORY;
    wcscpy_s(*ppszVal, len, src);
    return S_OK;
}

HRESULT CBlueOpenCredential::GetBitmapValue(_In_ DWORD dwFieldID, _Outptr_ HBITMAP* phbmp)
{
    if (phbmp == nullptr) return E_POINTER;
    *phbmp = NULL;

    if (dwFieldID == FID_LOGO)
    {
        *phbmp = (HBITMAP)LoadImageW(
            g_hInst,
            MAKEINTRESOURCEW(IDB_LOGO),
            IMAGE_BITMAP,
            0, 0,
            LR_CREATEDIBSECTION
        );
        if (*phbmp != NULL)
        {
            return S_OK;
        }
        WriteLog("[BlueOpen] LoadImageW for IDB_LOGO failed, error = %d\n", GetLastError());
    }

    return E_NOTIMPL;
}

HRESULT CBlueOpenCredential::GetCheckboxValue(_In_ DWORD dwFieldID, _Out_ BOOL* pbChecked, _Outptr_ WCHAR** ppszLabel)
{
    *pbChecked = FALSE;
    *ppszLabel = nullptr;
    return E_NOTIMPL;
}

HRESULT CBlueOpenCredential::SetStringValue(_In_ DWORD dwFieldID, _In_ PCWSTR pszVal)
{
    return E_NOTIMPL;
}

HRESULT CBlueOpenCredential::SetDwordValue(_In_ DWORD dwFieldID, _In_ DWORD dwVal)
{
    return E_NOTIMPL;
}

HRESULT CBlueOpenCredential::CommandLinkClicked(_In_ DWORD dwFieldID)
{
    return E_NOTIMPL;
}

HRESULT CBlueOpenCredential::GetSubmitButton(_Out_ DWORD* pdwFieldID)
{
    *pdwFieldID = 0;
    return E_NOTIMPL;
}

HRESULT CBlueOpenCredential::GetSerialization(
    _Out_ CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
    _Out_ CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs,
    _Outptr_ PWSTR* ppszOptionalStatusText,
    _Outptr_ CREDENTIAL_PROVIDER_STATUS_ICON* pStatusIcon
)
{
    *ppszOptionalStatusText = nullptr;
    *pStatusIcon = CPSI_NONE;

    WriteLog("[BlueOpen] GetSerialization called. _hasCredentials = %d\n", _hasCredentials);

    if (_hasCredentials)
    {
        HRESULT hr = SerializeCredentials(_szUsername, _szPassword, pcpcs);
        WriteLog("[BlueOpen] SerializeCredentials returned HRESULT = 0x%08X\n", hr);
        if (SUCCEEDED(hr))
        {
            *pcpgsr = CPGSR_RETURN_CREDENTIAL_FINISHED;
            return S_OK;
        }
        else
        {
            *pcpgsr = CPGSR_NO_CREDENTIAL_FINISHED;
            return hr;
        }
    }

    *pcpgsr = CPGSR_NO_CREDENTIAL_NOT_FINISHED;
    return E_FAIL;
}

HRESULT CBlueOpenCredential::ReportResult(_In_ HRESULT hrSetup, _In_ HRESULT hrLogin, _Outptr_ PWSTR* ppszOptionalStatusText, _Outptr_ CREDENTIAL_PROVIDER_STATUS_ICON* pStatusIcon)
{
    *ppszOptionalStatusText = nullptr;
    *pStatusIcon = CPSI_NONE;
    WriteLog("[BlueOpen] ReportResult called. hrSetup = 0x%08X, hrLogin = 0x%08X\n", hrSetup, hrLogin);
    _hasCredentials = FALSE;
    wcscpy_s(_szStatusText, L"Waiting for phone connection...");
    return S_OK;
}

HRESULT CBlueOpenCredential::Advise(_In_ ICredentialProviderCredentialEvents* pEvents)
{
    if (_pEvents != nullptr)
    {
        _pEvents->Release();
    }
    _pEvents = pEvents;
    if (_pEvents != nullptr)
    {
        _pEvents->AddRef();
    }

    IGlobalInterfaceTable* pGIT = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, IID_IGlobalInterfaceTable, (void**)&pGIT);
    if (SUCCEEDED(hr) && pGIT != nullptr)
    {
        if (_dwCredentialEventsCookie != 0)
        {
            pGIT->RevokeInterfaceFromGlobal(_dwCredentialEventsCookie);
            _dwCredentialEventsCookie = 0;
        }

        if (pEvents != nullptr)
        {
            hr = pGIT->RegisterInterfaceInGlobal(pEvents, IID_ICredentialProviderCredentialEvents, &_dwCredentialEventsCookie);
            if (FAILED(hr))
            {
                WriteLog("[BlueOpen] RegisterInterfaceInGlobal (CredentialEvents) failed, hr = 0x%08X\n", hr);
            }
        }
        pGIT->Release();
    }

    WriteLog("[BlueOpen] Tile Advise called. pEvents = %p, cookie = %u\n", pEvents, _dwCredentialEventsCookie);
    return S_OK;
}

HRESULT CBlueOpenCredential::UnAdvise()
{
    if (_dwCredentialEventsCookie != 0)
    {
        IGlobalInterfaceTable* pGIT = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, IID_IGlobalInterfaceTable, (void**)&pGIT);
        if (SUCCEEDED(hr) && pGIT != nullptr)
        {
            pGIT->RevokeInterfaceFromGlobal(_dwCredentialEventsCookie);
            _dwCredentialEventsCookie = 0;
            pGIT->Release();
        }
    }

    if (_pEvents != nullptr)
    {
        _pEvents->Release();
        _pEvents = nullptr;
    }
    WriteLog("[BlueOpen] Tile UnAdvise called.\n");
    return S_OK;
}

HRESULT CBlueOpenCredential::SetSelected(_Out_ BOOL* pbAutoSubmit)
{
    if (_hasCredentials)
    {
        *pbAutoSubmit = TRUE;
        WriteLog("[BlueOpen] SetSelected called: _hasCredentials is TRUE, pbAutoSubmit = TRUE.\n");
    }
    else
    {
        *pbAutoSubmit = FALSE;
        WriteLog("[BlueOpen] SetSelected called: _hasCredentials is FALSE, pbAutoSubmit = FALSE.\n");
    }
    return S_OK;
}

HRESULT CBlueOpenCredential::SetDeselected()
{
    WriteLog("[BlueOpen] SetDeselected called.\n");
    _hasCredentials = FALSE;
    return S_OK;
}

HRESULT CBlueOpenCredential::GetSubmitButtonValue(_In_ DWORD dwFieldID, _Out_ DWORD* pdwAdjacentTo)
{
    return E_NOTIMPL;
}

HRESULT CBlueOpenCredential::GetComboBoxValueCount(_In_ DWORD dwFieldID, _Out_ DWORD* pcItems, _Out_ DWORD* pdwDefault)
{
    return E_NOTIMPL;
}

HRESULT CBlueOpenCredential::GetComboBoxValueAt(_In_ DWORD dwFieldID, _In_ DWORD dwIndex, _Outptr_ WCHAR** ppszValue)
{
    return E_NOTIMPL;
}

HRESULT CBlueOpenCredential::SetCheckboxValue(_In_ DWORD dwFieldID, _In_ BOOL bChecked)
{
    return E_NOTIMPL;
}

HRESULT CBlueOpenCredential::SetComboBoxSelectedValue(_In_ DWORD dwFieldID, _In_ DWORD dwIndex)
{
    return E_NOTIMPL;
}

// Named Pipe Listener Thread
DWORD WINAPI CBlueOpenCredential::PipeListenerThread(LPVOID lpParam)
{
    CBlueOpenCredential* pThis = (CBlueOpenCredential*)lpParam;
    pThis->ListenToPipe();
    return 0;
}

void CBlueOpenCredential::ListenToPipe()
{
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    WriteLog("[BlueOpen] Named Pipe thread running. CoInitializeEx = 0x%08X\n", hrCo);

    // Configure security descriptor with NULL DACL (allows user process to connect from default desktop)
    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
    SECURITY_ATTRIBUTES sa = { sizeof(sa), &sd, FALSE };

    while (_bListening)
    {
        _hPipe = CreateNamedPipeW(
            L"\\\\.\\pipe\\BlueOpenUnlockPipe",
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            1024,
            1024,
            0,
            &sa
        );

        if (_hPipe == INVALID_HANDLE_VALUE)
        {
            WriteLog("[BlueOpen] CreateNamedPipeW failed, error = %d\n", GetLastError());
            Sleep(2000);
            continue;
        }

        BOOL connected = ConnectNamedPipe(_hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected && _bListening)
        {
            WriteLog("[BlueOpen] Pipe client connected.\n");
            char buffer[1024];
            DWORD bytesRead = 0;
            BOOL success = ReadFile(_hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL);

            if (success && bytesRead > 0)
            {
                buffer[bytesRead] = '\0';
                WriteLog("[BlueOpen] Read %d bytes from pipe: %s\n", bytesRead, buffer);

                WCHAR wBuffer[1024];
                int wLen = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wBuffer, 1024);
                if (wLen > 0)
                {
                    wBuffer[wLen] = L'\0';
                    
                    for (int i = 0; i < wLen; i++)
                    {
                        if (wBuffer[i] == L'\r' || wBuffer[i] == L'\n')
                        {
                            wBuffer[i] = L'\0';
                            break;
                        }
                    }

                    WCHAR* colon = wcschr(wBuffer, L':');
                    if (colon != nullptr)
                    {
                        *colon = L'\0';
                        wcscpy_s(_szUsername, wBuffer);
                        wcscpy_s(_szPassword, colon + 1);
                        _hasCredentials = TRUE;

                        WriteLog("[BlueOpen] Parsed credentials: Username = %S, Password length = %d\n", _szUsername, (int)wcslen(_szPassword));

                        // Notify Windows Logon UI that credentials are ready!
                        TriggerLogon();

                        DWORD bytesWritten;
                        WriteFile(_hPipe, "OK\n", 3, &bytesWritten, NULL);
                    }
                    else
                    {
                        WriteLog("[BlueOpen] Error: colon separator not found in payload.\n");
                    }
                }
            }
        }

        if (_hPipe != INVALID_HANDLE_VALUE)
        {
            DisconnectNamedPipe(_hPipe);
            CloseHandle(_hPipe);
            _hPipe = INVALID_HANDLE_VALUE;
        }
    }

    if (SUCCEEDED(hrCo))
    {
        CoUninitialize();
    }
}

void CBlueOpenCredential::TriggerLogon()
{
    WriteLog("[BlueOpen] TriggerLogon called. Username = %S, Password length = %d\n", _szUsername, (int)wcslen(_szPassword));

    wcscpy_s(_szStatusText, L"Unlocking via Phone...");

    // 1. Update tile status text & credentials changed if tile is active/selected
    if (_dwCredentialEventsCookie != 0)
    {
        IGlobalInterfaceTable* pGIT = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, IID_IGlobalInterfaceTable, (void**)&pGIT);
        if (SUCCEEDED(hr) && pGIT != nullptr)
        {
            ICredentialProviderCredentialEvents* pCredEvents = nullptr;
            hr = pGIT->GetInterfaceFromGlobal(_dwCredentialEventsCookie, IID_ICredentialProviderCredentialEvents, (void**)&pCredEvents);
            if (SUCCEEDED(hr) && pCredEvents != nullptr)
            {
                pCredEvents->SetFieldString(this, FID_STATUS, _szStatusText);
                pCredEvents->Release();
                WriteLog("[BlueOpen] Updated tile status via SetFieldString.\n");
            }
            pGIT->Release();
        }
    }

    // 2. Notify provider events via GIT (LogonUI re-evaluates GetCredentialCount and executes auto-logon)
    if (_dwProviderEventsCookie != 0)
    {
        IGlobalInterfaceTable* pGIT = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, IID_IGlobalInterfaceTable, (void**)&pGIT);
        if (SUCCEEDED(hr) && pGIT != nullptr)
        {
            ICredentialProviderEvents* pEvents = nullptr;
            hr = pGIT->GetInterfaceFromGlobal(_dwProviderEventsCookie, IID_ICredentialProviderEvents, (void**)&pEvents);
            if (SUCCEEDED(hr) && pEvents != nullptr)
            {
                WriteLog("[BlueOpen] Calling CredentialsChanged via GIT...\n");
                hr = pEvents->CredentialsChanged(_upAdviseContext);
                WriteLog("[BlueOpen] CredentialsChanged returned HRESULT = 0x%08X\n", hr);
                pEvents->Release();
            }
            else
            {
                WriteLog("[BlueOpen] GetInterfaceFromGlobal failed, hr = 0x%08X\n", hr);
            }
            pGIT->Release();
        }
        else
        {
            WriteLog("[BlueOpen] Failed to get GIT in TriggerLogon, hr = 0x%08X\n", hr);
        }
    }
    else if (_pProviderEvents != nullptr)
    {
        WriteLog("[BlueOpen] Fallback: calling CredentialsChanged directly...\n");
        HRESULT hr = _pProviderEvents->CredentialsChanged(_upAdviseContext);
        WriteLog("[BlueOpen] CredentialsChanged returned HRESULT = 0x%08X\n", hr);
    }
    else
    {
        WriteLog("[BlueOpen] WARNING: _pProviderEvents is NULL and no GIT cookie exists!\n");
    }
}

bool CBlueOpenCredential::IsServerProcessRunning()
{
    bool isRunning = false;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnapshot, &pe))
        {
            do
            {
                if (_wcsicmp(pe.szExeFile, L"BlueOpenServer.exe") == 0)
                {
                    isRunning = true;
                    break;
                }
            } while (Process32NextW(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }
    WriteLog("[BlueOpen] IsServerProcessRunning returned: %s\n", isRunning ? "TRUE" : "FALSE");
    return isRunning;
}

static std::wstring ExtractJsonValue(const std::string& json, const std::string& key)
{
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return L"";

    size_t colonPos = json.find(':', keyPos + searchKey.length());
    if (colonPos == std::string::npos) return L"";

    size_t quoteStart = json.find('\"', colonPos + 1);
    if (quoteStart == std::string::npos) return L"";

    std::string val;
    bool escaped = false;
    for (size_t i = quoteStart + 1; i < json.length(); ++i)
    {
        char c = json[i];
        if (escaped)
        {
            if (c == 'n') val += '\n';
            else if (c == 'r') val += '\r';
            else if (c == 't') val += '\t';
            else if (c == '\"') val += '\"';
            else if (c == '\\') val += '\\';
            else val += c;
            escaped = false;
        }
        else if (c == '\\')
        {
            escaped = true;
        }
        else if (c == '\"')
        {
            break;
        }
        else
        {
            val += c;
        }
    }

    if (val.empty()) return L"";
    int wLen = MultiByteToWideChar(CP_UTF8, 0, val.c_str(), (int)val.length(), NULL, 0);
    if (wLen <= 0) return L"";
    std::wstring wVal(wLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, val.c_str(), (int)val.length(), &wVal[0], wLen);
    return wVal;
}

bool CBlueOpenCredential::LoadConfig(std::wstring& outUser, std::wstring& outDomain, std::wstring& outPassword, std::string& outAuthSecret)
{
    // 1. Try ProgramData first
    WCHAR commonPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, commonPath)))
    {
        wcscpy_s(commonPath, L"C:\\ProgramData");
    }
    std::wstring configPath = std::wstring(commonPath) + L"\\BlueOpen\\config.json";

    FILE* f = nullptr;
    _wfopen_s(&f, configPath.c_str(), L"rb");

    // 2. Fallback: try User AppData Roaming if ProgramData not present
    if (!f)
    {
        WCHAR appDataPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath)))
        {
            std::wstring userConfigPath = std::wstring(appDataPath) + L"\\BlueOpen\\config.json";
            _wfopen_s(&f, userConfigPath.c_str(), L"rb");
            if (f) configPath = userConfigPath;
        }
    }

    if (!f)
    {
        WriteLog("[BlueOpen] Config file not found at %S\n", configPath.c_str());
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 1024 * 1024)
    {
        fclose(f);
        return false;
    }

    std::string content(fsize, '\0');
    fread(&content[0], 1, fsize, f);
    fclose(f);

    outUser = ExtractJsonValue(content, "WinUsername");
    outDomain = ExtractJsonValue(content, "WinDomain");
    outPassword = ExtractJsonValue(content, "WinPassword");
    std::wstring wAuthSecret = ExtractJsonValue(content, "Password");

    int sLen = WideCharToMultiByte(CP_UTF8, 0, wAuthSecret.c_str(), (int)wAuthSecret.length(), NULL, 0, NULL, NULL);
    if (sLen > 0)
    {
        outAuthSecret.resize(sLen);
        WideCharToMultiByte(CP_UTF8, 0, wAuthSecret.c_str(), (int)wAuthSecret.length(), &outAuthSecret[0], sLen, NULL, NULL);
    }
    else
    {
        outAuthSecret = "123456";
    }

    WriteLog("[BlueOpen] Config loaded from %S: User = %S, Domain = %S, PwdLen = %d, SecretLen = %d\n",
        configPath.c_str(), outUser.c_str(), outDomain.c_str(), (int)outPassword.length(), (int)outAuthSecret.length());
    return true;
}

static std::string ComputeSha256(const std::string& input)
{
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (status != 0) return "";

    DWORD cbHash = 0, cbData = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&cbHash, sizeof(DWORD), &cbData, 0);

    status = BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0);
    if (status != 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    BCryptHashData(hHash, (PBYTE)input.c_str(), (ULONG)input.length(), 0);

    std::string hashBytes(cbHash, '\0');
    BCryptFinishHash(hHash, (PBYTE)&hashBytes[0], cbHash, 0);

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    char hexBuffer[65] = { 0 };
    for (DWORD i = 0; i < cbHash; ++i)
    {
        sprintf_s(&hexBuffer[i * 2], 3, "%02x", (unsigned char)hashBytes[i]);
    }
    return std::string(hexBuffer);
}

DWORD WINAPI CBlueOpenCredential::BtListenerThread(LPVOID lpParam)
{
    CBlueOpenCredential* pThis = (CBlueOpenCredential*)lpParam;
    pThis->ListenToBluetooth();
    return 0;
}

void CBlueOpenCredential::ListenToBluetooth()
{
    WriteLog("[BlueOpen BT] Starting direct Bluetooth Winsock RFCOMM listener...\n");

    WSADATA wsaData;
    int wsaErr = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaErr != 0)
    {
        WriteLog("[BlueOpen BT] WSAStartup failed: %d\n", wsaErr);
        return;
    }

    _btListenSocket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (_btListenSocket == INVALID_SOCKET)
    {
        WriteLog("[BlueOpen BT] socket(AF_BTH) failed: %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }

    SOCKADDR_BTH sab = { 0 };
    sab.addressFamily = AF_BTH;
    sab.btAddr = 0;
    sab.port = BT_PORT_ANY;

    if (bind(_btListenSocket, (SOCKADDR*)&sab, sizeof(sab)) == SOCKET_ERROR)
    {
        WriteLog("[BlueOpen BT] bind failed: %d\n", WSAGetLastError());
        closesocket(_btListenSocket);
        _btListenSocket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    if (listen(_btListenSocket, 2) == SOCKET_ERROR)
    {
        WriteLog("[BlueOpen BT] listen failed: %d\n", WSAGetLastError());
        closesocket(_btListenSocket);
        _btListenSocket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    int sabLen = sizeof(sab);
    getsockname(_btListenSocket, (SOCKADDR*)&sab, &sabLen);
    WriteLog("[BlueOpen BT] Listening on RFCOMM channel %d\n", sab.port);

    WSAQUERYSETW qs = { 0 };
    qs.dwSize = sizeof(qs);
    qs.lpszServiceInstanceName = (LPWSTR)L"BlueOpen Direct Unlock Service";
    GUID serviceClassId = BluetoothServiceUuid;
    qs.lpServiceClassId = &serviceClassId;
    qs.dwNameSpace = NS_BTH;
    qs.dwNumberOfCsAddrs = 1;

    CSADDR_INFO csAddr = { 0 };
    csAddr.LocalAddr.lpSockaddr = (LPSOCKADDR)&sab;
    csAddr.LocalAddr.iSockaddrLength = sizeof(sab);
    csAddr.iSocketType = SOCK_STREAM;
    csAddr.iProtocol = BTHPROTO_RFCOMM;
    qs.lpcsaBuffer = &csAddr;

    if (WSASetServiceW(&qs, RNRSERVICE_REGISTER, 0) == SOCKET_ERROR)
    {
        WriteLog("[BlueOpen BT] WSASetServiceW REGISTER failed: %d\n", WSAGetLastError());
    }
    else
    {
        WriteLog("[BlueOpen BT] SDP service registered successfully!\n");
    }

    while (_bBtListening)
    {
        SOCKADDR_BTH clientAddr = { 0 };
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSock = accept(_btListenSocket, (SOCKADDR*)&clientAddr, &clientAddrLen);
        if (clientSock == INVALID_SOCKET)
        {
            if (!_bBtListening) break;
            WriteLog("[BlueOpen BT] accept returned INVALID_SOCKET, error = %d\n", WSAGetLastError());
            Sleep(500);
            continue;
        }

        WriteLog("[BlueOpen BT] Device connected! Address: %012llX\n", clientAddr.btAddr);

        GUID nonceGuid;
        CoCreateGuid(&nonceGuid);
        char nonceStr[64];
        sprintf_s(nonceStr, "%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
            nonceGuid.Data1, nonceGuid.Data2, nonceGuid.Data3,
            nonceGuid.Data4[0], nonceGuid.Data4[1], nonceGuid.Data4[2], nonceGuid.Data4[3],
            nonceGuid.Data4[4], nonceGuid.Data4[5], nonceGuid.Data4[6], nonceGuid.Data4[7]);

        std::string nonceMsg = std::string(nonceStr) + "\n";
        send(clientSock, nonceMsg.c_str(), (int)nonceMsg.length(), 0);
        WriteLog("[BlueOpen BT] Sent challenge nonce: %s\n", nonceStr);

        std::string response;
        char ch;
        while (recv(clientSock, &ch, 1, 0) > 0)
        {
            if (ch == '\n') break;
            if (ch != '\r') response += ch;
        }
        WriteLog("[BlueOpen BT] Received client response: %s\n", response.c_str());

        size_t colonPos = response.find(':');
        if (colonPos == std::string::npos)
        {
            WriteLog("[BlueOpen BT] Invalid response format.\n");
            send(clientSock, "FAIL\n", 5, 0);
            closesocket(clientSock);
            continue;
        }

        std::string clientHash = response.substr(0, colonPos);
        std::string command = response.substr(colonPos + 1);

        while (!clientHash.empty() && isspace(clientHash.back())) clientHash.pop_back();
        while (!command.empty() && isspace(command.back())) command.pop_back();

        std::wstring winUser, winDomain, winPass;
        std::string authSecret;
        if (!LoadConfig(winUser, winDomain, winPass, authSecret))
        {
            WriteLog("[BlueOpen BT] Failed to load config.\n");
            send(clientSock, "FAIL\n", 5, 0);
            closesocket(clientSock);
            continue;
        }

        std::string expectedHash = ComputeSha256(std::string(nonceStr) + authSecret);
        WriteLog("[BlueOpen BT] Client hash:   %s\n", clientHash.c_str());
        WriteLog("[BlueOpen BT] Expected hash: %s\n", expectedHash.c_str());

        if (_stricmp(clientHash.c_str(), expectedHash.c_str()) == 0)
        {
            WriteLog("[BlueOpen BT] Authentication SUCCESS! Command: %s\n", command.c_str());
            send(clientSock, "OK\n", 3, 0);

            if (_stricmp(command.c_str(), "UNLOCK") == 0)
            {
                if (!winDomain.empty() && _wcsicmp(winDomain.c_str(), L".") != 0 && winDomain != winUser)
                {
                    std::wstring qualified = winDomain + L"\\" + winUser;
                    wcscpy_s(_szUsername, qualified.c_str());
                }
                else
                {
                    wcscpy_s(_szUsername, winUser.c_str());
                }
                wcscpy_s(_szPassword, winPass.c_str());
                _hasCredentials = TRUE;

                TriggerLogon();
            }
        }
        else
        {
            WriteLog("[BlueOpen BT] Authentication FAILED (hash mismatch).\n");
            send(clientSock, "FAIL\n", 5, 0);
        }

        closesocket(clientSock);
    }

    WSASetServiceW(&qs, RNRSERVICE_DELETE, 0);
    if (_btListenSocket != INVALID_SOCKET)
    {
        closesocket(_btListenSocket);
        _btListenSocket = INVALID_SOCKET;
    }
    WSACleanup();
    WriteLog("[BlueOpen BT] Bluetooth listener stopped.\n");
}

HRESULT CBlueOpenCredential::SerializeCredentials(
    _In_ PCWSTR pszUsername,
    _In_ PCWSTR pszPassword,
    _Out_ CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs
)
{
    ZeroMemory(pcpcs, sizeof(*pcpcs));

    HANDLE hLsa;
    NTSTATUS status = LsaConnectUntrusted(&hLsa);
    if (status != STATUS_SUCCESS)
    {
        WriteLog("[BlueOpen] LsaConnectUntrusted failed, status = 0x%08X\n", status);
        return HRESULT_FROM_NT(status);
    }

    LSA_STRING pkgName;
    pkgName.Buffer = (char*)"Negotiate";
    pkgName.Length = (USHORT)strlen(pkgName.Buffer);
    pkgName.MaximumLength = pkgName.Length;

    ULONG pkgId = 0;
    status = LsaLookupAuthenticationPackage(hLsa, &pkgName, &pkgId);
    LsaDeregisterLogonProcess(hLsa);

    if (status != STATUS_SUCCESS)
    {
        WriteLog("[BlueOpen] LsaLookupAuthenticationPackage failed, status = 0x%08X\n", status);
        return HRESULT_FROM_NT(status);
    }

    WriteLog("[BlueOpen] Auth package ID = %d\n", pkgId);

    // If username has no domain and no '@' (not Microsoft Account / UPN), prepend '.\'
    WCHAR szFullUser[256] = L"";
    if (wcschr(pszUsername, L'\\') == nullptr && wcschr(pszUsername, L'@') == nullptr)
    {
        wcscpy_s(szFullUser, L".\\");
        wcscat_s(szFullUser, pszUsername);
    }
    else
    {
        wcscpy_s(szFullUser, pszUsername);
    }

    WriteLog("[BlueOpen] Packing: FullUser='%S', PasswordLen=%d, cpus=%d\n",
             szFullUser, (int)wcslen(pszPassword), (int)_cpus);

    DWORD cbTotal = 0;
    BOOL ok = CredPackAuthenticationBufferW(
        0,
        szFullUser,
        (LPWSTR)pszPassword,
        nullptr,
        &cbTotal);

    if (!ok && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        DWORD err = GetLastError();
        WriteLog("[BlueOpen] CredPack (size) failed, err = %d\n", err);
        return HRESULT_FROM_WIN32(err);
    }

    BYTE* rgb = (BYTE*)CoTaskMemAlloc(cbTotal);
    if (!rgb) return E_OUTOFMEMORY;
    ZeroMemory(rgb, cbTotal);

    ok = CredPackAuthenticationBufferW(
        0,
        szFullUser,
        (LPWSTR)pszPassword,
        rgb,
        &cbTotal);

    if (!ok)
    {
        DWORD err = GetLastError();
        WriteLog("[BlueOpen] CredPack (pack) failed, err = %d\n", err);
        CoTaskMemFree(rgb);
        return HRESULT_FROM_WIN32(err);
    }

    WriteLog("[BlueOpen] KerbPack OK. Buffer size = %d bytes\n", cbTotal);

    pcpcs->ulAuthenticationPackage = pkgId;
    pcpcs->cbSerialization = cbTotal;
    pcpcs->rgbSerialization = rgb;
    pcpcs->clsidCredentialProvider = CLSID_BlueOpenProvider;

    WriteLog("[BlueOpen] SerializeCredentials complete. PackageId=%d, Size=%d\n", pkgId, cbTotal);
    return S_OK;
}
