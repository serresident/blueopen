#pragma once
#include <winsock2.h>
#include <windows.h>
#include <credentialprovider.h>

enum BLUEOPEN_FIELD_ID
{
    FID_LOGO = 0,
    FID_TITLE = 1,
    FID_STATUS = 2,
    FID_NET_REQUEST_BUTTON = 3,
    FID_NUM_FIELDS = 4,
};

class CBlueOpenProvider : public ICredentialProvider, public ICredentialProviderSetUserArray
{
public:
    // IUnknown
    IFACEMETHODIMP QueryInterface(_In_ REFIID riid, _Outptr_ void** ppv);
    IFACEMETHODIMP_(ULONG) AddRef();
    IFACEMETHODIMP_(ULONG) Release();

    // ICredentialProvider
    IFACEMETHODIMP SetUsageScenario(_In_ CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, _In_ DWORD dwFlags);
    IFACEMETHODIMP SetSerialization(_In_ const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs);
    IFACEMETHODIMP GetCredentialCount(_Out_ DWORD* pdwCount, _Out_ DWORD* pdwDefault, _Out_ BOOL* pbAutoLogonWithDefault);
    IFACEMETHODIMP GetCredentialAt(_In_ DWORD dwIndex, _Outptr_ ICredentialProviderCredential** ppcpc);
    IFACEMETHODIMP GetFieldDescriptorCount(_Out_ DWORD* pdwCount);
    IFACEMETHODIMP GetFieldDescriptorAt(_In_ DWORD dwIndex, _Outptr_ CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** ppcpfd);
    IFACEMETHODIMP Advise(_In_ ICredentialProviderEvents* pEvents, _In_ UINT_PTR upAdviseContext);
    IFACEMETHODIMP UnAdvise();

    // ICredentialProviderSetUserArray
    IFACEMETHODIMP SetUserArray(_In_ ICredentialProviderUserArray* users);

    CBlueOpenProvider();

protected:
    virtual ~CBlueOpenProvider();

private:
    LONG _cRef;
    CREDENTIAL_PROVIDER_USAGE_SCENARIO _cpus;
    ICredentialProviderCredential* _pCredential;
    ICredentialProviderEvents* _pEvents;
    UINT_PTR _upAdviseContext;
    WCHAR _szTargetUserSid[256];
};

HRESULT CBlueOpenProvider_CreateInstance(_In_ REFIID riid, _Outptr_ void** ppv);
