#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2bth.h>
#include <windows.h>
#include <credentialprovider.h>
#include <string>

extern HINSTANCE g_hInst;

class CBlueOpenCredential : public ICredentialProviderCredential2
{
public:
    // IUnknown
    IFACEMETHODIMP QueryInterface(_In_ REFIID riid, _Outptr_ void** ppv);
    IFACEMETHODIMP_(ULONG) AddRef();
    IFACEMETHODIMP_(ULONG) Release();

    // ICredentialProviderCredential
    IFACEMETHODIMP GetFieldState(_In_ DWORD dwFieldID, _Out_ CREDENTIAL_PROVIDER_FIELD_STATE* pcpfs, _Out_ CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* pcpfis);
    IFACEMETHODIMP GetStringValue(_In_ DWORD dwFieldID, _Outptr_ WCHAR** ppszVal);
    IFACEMETHODIMP GetBitmapValue(_In_ DWORD dwFieldID, _Outptr_ HBITMAP* phbmp);
    IFACEMETHODIMP GetCheckboxValue(_In_ DWORD dwFieldID, _Out_ BOOL* pbChecked, _Outptr_ WCHAR** ppszLabel);
    IFACEMETHODIMP SetStringValue(_In_ DWORD dwFieldID, _In_ PCWSTR pszVal);
    IFACEMETHODIMP SetDwordValue(_In_ DWORD dwFieldID, _In_ DWORD dwVal);
    IFACEMETHODIMP CommandLinkClicked(_In_ DWORD dwFieldID);
    IFACEMETHODIMP GetSubmitButton(_Out_ DWORD* pdwFieldID);
    IFACEMETHODIMP GetSerialization(_Out_ CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr, _Out_ CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs, _Outptr_ PWSTR* ppszOptionalStatusText, _Outptr_ CREDENTIAL_PROVIDER_STATUS_ICON* pStatusIcon);
    IFACEMETHODIMP ReportResult(_In_ HRESULT hrSetup, _In_ HRESULT hrLogin, _Outptr_ PWSTR* ppszOptionalStatusText, _Outptr_ CREDENTIAL_PROVIDER_STATUS_ICON* pStatusIcon);
    IFACEMETHODIMP Advise(_In_ ICredentialProviderCredentialEvents* pEvents);
    IFACEMETHODIMP UnAdvise();

    IFACEMETHODIMP SetSelected(_Out_ BOOL* pbAutoSubmit);
    IFACEMETHODIMP SetDeselected();
    IFACEMETHODIMP GetSubmitButtonValue(_In_ DWORD dwFieldID, _Out_ DWORD* pdwAdjacentTo);
    IFACEMETHODIMP GetComboBoxValueCount(_In_ DWORD dwFieldID, _Out_ DWORD* pcItems, _Out_ DWORD* pdwDefault);
    IFACEMETHODIMP GetComboBoxValueAt(_In_ DWORD dwFieldID, _In_ DWORD dwIndex, _Outptr_ WCHAR** ppszValue);
    IFACEMETHODIMP SetCheckboxValue(_In_ DWORD dwFieldID, _In_ BOOL bChecked);
    IFACEMETHODIMP SetComboBoxSelectedValue(_In_ DWORD dwFieldID, _In_ DWORD dwIndex);

    // ICredentialProviderCredential2
    IFACEMETHODIMP GetUserSid(_Outptr_ wchar_t** sid);

    // Custom methods
    CBlueOpenCredential();
    HRESULT Initialize(_In_ CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus);
    void SetProviderEvents(ICredentialProviderEvents* pEvents, UINT_PTR upAdviseContext);
    void SetTargetUserSid(_In_ PCWSTR pszSid);
    BOOL HasCredentials() const { return _hasCredentials; }

protected:
    virtual ~CBlueOpenCredential();

private:
    // Named Pipe listener methods
    static DWORD WINAPI PipeListenerThread(LPVOID lpParam);
    void ListenToPipe();

    // Direct Bluetooth listener methods (for pre-logon / logoff state)
    static DWORD WINAPI BtListenerThread(LPVOID lpParam);
    void ListenToBluetooth();
    bool IsServerProcessRunning();
    bool LoadConfig(std::wstring& outUser, std::wstring& outDomain, std::wstring& outPassword, std::string& outAuthSecret);
    void TriggerLogon();

    // Credential Serialization helper
    HRESULT SerializeCredentials(
        _In_ PCWSTR pszUsername,
        _In_ PCWSTR pszPassword,
        _Out_ CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs
    );

private:
    LONG _cRef;
    CREDENTIAL_PROVIDER_USAGE_SCENARIO _cpus;
    ICredentialProviderCredentialEvents* _pEvents;
    DWORD _dwCredentialEventsCookie;
    
    // Logon events from the main provider
    ICredentialProviderEvents* _pProviderEvents;
    DWORD _dwProviderEventsCookie;
    UINT_PTR _upAdviseContext;

    // State
    WCHAR _szStatusText[256];
    WCHAR _szUsername[256];
    WCHAR _szPassword[256];
    WCHAR _szTargetUserSid[256];
    BOOL _hasCredentials;

    // Named Pipe thread handle
    HANDLE _hPipeThread;
    HANDLE _hPipe;
    BOOL _bListening;

    // Direct Bluetooth thread handle
    HANDLE _hBtThread;
    SOCKET _btListenSocket;
    BOOL _bBtListening;
};
