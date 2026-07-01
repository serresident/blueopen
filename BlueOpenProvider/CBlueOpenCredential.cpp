#include "CBlueOpenCredential.h"
#include "CBlueOpenProvider.h"
#include "guid.h"
#include <ntsecapi.h>
#include <wincred.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>

#pragma comment(lib, "credui.lib")

// Bulletproof definitions if the SDK compiler doesn't load them
#ifndef CPGSR_RETURN_CREDENTIALS
#define CPGSR_RETURN_CREDENTIALS ((CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE)1)
#endif
#ifndef CPGSR_NO_CREDENTIALS
#define CPGSR_NO_CREDENTIALS ((CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE)0)
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
    _pProviderEvents(nullptr),
    _dwProviderEventsCookie(0),
    _upAdviseContext(0),
    _hasCredentials(FALSE),
    _hPipeThread(NULL),
    _hPipe(INVALID_HANDLE_VALUE),
    _bListening(FALSE)
{
    wcscpy_s(_szStatusText, L"Waiting for phone connection...");
    _szUsername[0] = L'\0';
    _szPassword[0] = L'\0';
}

CBlueOpenCredential::~CBlueOpenCredential()
{
    _bListening = FALSE;
    if (_hPipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_hPipe);
    }
    if (_hPipeThread != NULL)
    {
        WaitForSingleObject(_hPipeThread, 1000);
        CloseHandle(_hPipeThread);
    }
    if (_pEvents != nullptr)
    {
        _pEvents->Release();
    }
    if (_pProviderEvents != nullptr)
    {
        _pProviderEvents->Release();
    }
    if (_dwProviderEventsCookie != 0)
    {
        IGlobalInterfaceTable* pGIT = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, IID_IGlobalInterfaceTable, (void**)&pGIT);
        if (SUCCEEDED(hr))
        {
            pGIT->RevokeInterfaceFromGlobal(_dwProviderEventsCookie);
            pGIT->Release();
        }
    }
}

HRESULT CBlueOpenCredential::Initialize(_In_ CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus)
{
    _cpus = cpus;

    WriteLog("[BlueOpen] Initializing credential tile...\n");

    // Start Named Pipe Listener thread
    _bListening = TRUE;
    _hPipeThread = CreateThread(NULL, 0, PipeListenerThread, this, 0, NULL);
    if (_hPipeThread == NULL)
    {
        WriteLog("[BlueOpen] Failed to create PipeListenerThread, error = %d\n", GetLastError());
        return HRESULT_FROM_WIN32(GetLastError());
    }

    WriteLog("[BlueOpen] PipeListenerThread started successfully.\n");
    return S_OK;
}

void CBlueOpenCredential::SetProviderEvents(ICredentialProviderEvents* pEvents, UINT_PTR upAdviseContext)
{
    IGlobalInterfaceTable* pGIT = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, IID_IGlobalInterfaceTable, (void**)&pGIT);
    if (SUCCEEDED(hr))
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
                WriteLog("[BlueOpen] RegisterInterfaceInGlobal failed, hr = 0x%08X\n", hr);
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
    WriteLog("[BlueOpen] SetProviderEvents called. Events pointer = %p, context = %p, cookie = %u\n", pEvents, (void*)upAdviseContext, _dwProviderEventsCookie);
}

// IUnknown - Manual implementation to avoid QISearch macros
HRESULT CBlueOpenCredential::QueryInterface(_In_ REFIID riid, _Outptr_ void** ppv)
{
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_ICredentialProviderCredential)
    {
        *ppv = static_cast<ICredentialProviderCredential*>(this);
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

// ICredentialProviderCredential
HRESULT CBlueOpenCredential::GetFieldState(_In_ DWORD dwFieldID, _Out_ CREDENTIAL_PROVIDER_FIELD_STATE* pcpfs, _Out_ CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* pcpfis)
{
    *pcpfis = CPFIS_NONE;
    switch (dwFieldID)
    {
    case FID_LOGO:
        *pcpfs = CPFS_DISPLAY_IN_SELECTED_TILE;
        break;
    case FID_TITLE:
        *pcpfs = CPFS_DISPLAY_IN_BOTH;
        break;
    case FID_STATUS:
        *pcpfs = CPFS_DISPLAY_IN_BOTH;
        break;
    case FID_USERNAME:
    case FID_PASSWORD:
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
        src = L"BlueOpen Bluetooth Unlocker";
        break;
    case FID_STATUS:
        src = _szStatusText;
        break;
    case FID_USERNAME:
        if (_hasCredentials)
        {
            src = _szUsername;
        }
        break;
    case FID_PASSWORD:
        if (_hasCredentials)
        {
            src = _szPassword;
        }
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
    *phbmp = NULL;
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
    if (dwFieldID == FID_USERNAME)
    {
        wcscpy_s(_szUsername, pszVal);
        _hasCredentials = TRUE;
        return S_OK;
    }
    else if (dwFieldID == FID_PASSWORD)
    {
        wcscpy_s(_szPassword, pszVal);
        _hasCredentials = TRUE;
        return S_OK;
    }
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
            *pcpgsr = CPGSR_RETURN_CREDENTIALS;
            // Keep _hasCredentials as TRUE so SetSelected can detect it and set pbAutoSubmit = TRUE.
            // It will be reset to FALSE in ReportResult or SetDeselected.
            return S_OK;
        }
        else
        {
            *pcpgsr = CPGSR_NO_CREDENTIALS;
            return hr;
        }
    }

    *pcpgsr = CPGSR_NO_CREDENTIALS;
    return E_FAIL;
}

HRESULT CBlueOpenCredential::ReportResult(_In_ HRESULT hrSetup, _In_ HRESULT hrLogin, _Outptr_ PWSTR* ppszOptionalStatusText, _Outptr_ CREDENTIAL_PROVIDER_STATUS_ICON* pStatusIcon)
{
    *ppszOptionalStatusText = nullptr;
    *pStatusIcon = CPSI_NONE;
    WriteLog("[BlueOpen] ReportResult called. hrSetup = 0x%08X, hrLogin = 0x%08X\n", hrSetup, hrLogin);
    _hasCredentials = FALSE;
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
    WriteLog("[BlueOpen] Tile Advise called. pEvents = %p\n", pEvents);
    return S_OK;
}

HRESULT CBlueOpenCredential::UnAdvise()
{
    if (_pEvents != nullptr)
    {
        _pEvents->Release();
        _pEvents = nullptr;
    }
    WriteLog("[BlueOpen] Tile UnAdvise called.\n");
    return S_OK;
}

// Missing methods implementation
HRESULT CBlueOpenCredential::SetSelected(_Out_ BOOL* pbAutoSubmit)
{
    if (_hasCredentials)
    {
        *pbAutoSubmit = TRUE;
        WriteLog("[BlueOpen] SetSelected called: _hasCredentials is TRUE, setting pbAutoSubmit = TRUE.\n");
    }
    else
    {
        *pbAutoSubmit = FALSE;
        WriteLog("[BlueOpen] SetSelected called: _hasCredentials is FALSE, setting pbAutoSubmit = FALSE.\n");
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

    // Configure security descriptor with NULL DACL (allows anyone to write to pipe)
    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
    SECURITY_ATTRIBUTES sa = { sizeof(sa), &sd, FALSE };

    while (_bListening)
    {
        // Create the Named Pipe
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

        // Wait for client to connect
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

                // Parse: "username:password"
                // Convert buffer from UTF-8 to WCHAR
                WCHAR wBuffer[1024];
                int wLen = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wBuffer, 1024);
                if (wLen > 0)
                {
                    wBuffer[wLen] = L'\0';
                    
                    // Remove trailing newlines
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

                        WriteLog("[BlueOpen] Parsed credentials: Username = %S, Password length = %d\n", _szUsername, wcslen(_szPassword));
                        wcscpy_s(_szStatusText, L"Unlocking via Phone...");

                        // Notify Windows Logon UI that credentials are ready!
                        if (_dwProviderEventsCookie != 0)
                        {
                            IGlobalInterfaceTable* pGIT = nullptr;
                            HRESULT hr = CoCreateInstance(CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, IID_IGlobalInterfaceTable, (void**)&pGIT);
                            if (SUCCEEDED(hr))
                            {
                                ICredentialProviderEvents* pEvents = nullptr;
                                hr = pGIT->GetInterfaceFromGlobal(_dwProviderEventsCookie, IID_ICredentialProviderEvents, (void**)&pEvents);
                                if (SUCCEEDED(hr))
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
                                WriteLog("[BlueOpen] Failed to get GIT in pipe thread, hr = 0x%08X\n", hr);
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

                        // Send confirmation back to C# app
                        DWORD bytesWritten;
                        BOOL wSuccess = WriteFile(_hPipe, "OK\n", 3, &bytesWritten, NULL);
                        WriteLog("[BlueOpen] WriteFile to pipe returned %d, bytesWritten = %d\n", wSuccess, bytesWritten);
                    }
                    else
                    {
                        WriteLog("[BlueOpen] Error: colon separator not found in payload.\n");
                    }
                }
                else
                {
                    WriteLog("[BlueOpen] Error: MultiByteToWideChar failed, error = %d\n", GetLastError());
                }
            }
            else
            {
                WriteLog("[BlueOpen] Error: ReadFile failed, error = %d, success = %d\n", GetLastError(), success);
            }
        }

        DisconnectNamedPipe(_hPipe);
        CloseHandle(_hPipe);
        _hPipe = INVALID_HANDLE_VALUE;
        WriteLog("[BlueOpen] Pipe closed.\n");
    }

    if (SUCCEEDED(hrCo))
    {
        CoUninitialize();
    }
}

// ====================================================================
// Microsoft SDK Credential Provider helpers - exact port from:
// Windows-classic-samples/Samples/CredentialProvider/cpp/helpers.cpp
// ====================================================================

// Initialize a UNICODE_STRING with a PWSTR (shallow copy, no allocation)
static HRESULT _UnicodeStringInitWithString(_In_ PWSTR pwz, _Out_ UNICODE_STRING* pus)
{
    if (pwz)
    {
        size_t lenString = wcslen(pwz);
        USHORT usCharCount = (USHORT)lenString;
        USHORT usSize = (USHORT)sizeof(WCHAR);
        pus->Length = usCharCount * usSize; // byte count, NOT including NULL
        pus->MaximumLength = pus->Length;
        pus->Buffer = pwz;
        return S_OK;
    }
    return E_INVALIDARG;
}

// ====================================================================
// SerializeCredentials using CredPackAuthenticationBufferW
// ====================================================================
HRESULT CBlueOpenCredential::SerializeCredentials(
    _In_ PCWSTR pszUsername,
    _In_ PCWSTR pszPassword,
    _Out_ CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs
)
{
    ZeroMemory(pcpcs, sizeof(*pcpcs));

    // 1. Get package ID
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

    // Ensure we have a domain (CredPackAuthenticationBufferW requires it for correct packing)
    WCHAR szFullUser[256] = L"";
    if (wcschr(pszUsername, L'\\') == nullptr)
    {
        wcscpy_s(szFullUser, L".\\");
        wcscat_s(szFullUser, pszUsername);
    }
    else
    {
        wcscpy_s(szFullUser, pszUsername);
    }

    WriteLog("[BlueOpen] SDK Init: FullUser='%S', PasswordLen=%d, cpus=%d\n",
             szFullUser, (int)wcslen(pszPassword), (int)_cpus);

    DWORD cbTotal = 0;
    BOOL ok = CredPackAuthenticationBufferW(
        0, // Clear text, Negotiate will handle NTLM fallback
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

    // Optional: If it's CPUS_UNLOCK_WORKSTATION, we technically could override the MessageType, 
    // but Windows 10/11 Negotiate accepts KerbInteractiveLogon even for unlock if packed via CredPack.
    // If not, we uncomment this:
    /*
    if (_cpus == CPUS_UNLOCK_WORKSTATION) {
        KERB_INTERACTIVE_UNLOCK_LOGON* pkiul = (KERB_INTERACTIVE_UNLOCK_LOGON*)rgb;
        pkiul->Logon.MessageType = KerbWorkstationUnlockLogon;
    }
    */

    WriteLog("[BlueOpen] KerbPack OK. Buffer size = %d bytes\n", cbTotal);

    // Fill output
    pcpcs->ulAuthenticationPackage = pkgId;
    pcpcs->cbSerialization = cbTotal;
    pcpcs->rgbSerialization = rgb;
    pcpcs->clsidCredentialProvider = CLSID_BlueOpenProvider;

    WriteLog("[BlueOpen] SerializeCredentials complete. PackageId=%d, Size=%d\n", pkgId, cbTotal);

    return S_OK;
}



