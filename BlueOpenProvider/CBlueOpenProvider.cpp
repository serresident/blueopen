#include "CBlueOpenProvider.h"
#include "CBlueOpenCredential.h"
#include "guid.h"
#include <new>

CBlueOpenProvider::CBlueOpenProvider() : 
    _cRef(1), 
    _cpus(CPUS_INVALID), 
    _pCredential(nullptr),
    _pEvents(nullptr),
    _upAdviseContext(0)
{
    _szTargetUserSid[0] = L'\0';
}

CBlueOpenProvider::~CBlueOpenProvider()
{
    if (_pCredential != nullptr)
    {
        _pCredential->Release();
        _pCredential = nullptr;
    }
    if (_pEvents != nullptr)
    {
        _pEvents->Release();
        _pEvents = nullptr;
    }
}

// IUnknown - Manual implementation to avoid QISearch macros
HRESULT CBlueOpenProvider::QueryInterface(_In_ REFIID riid, _Outptr_ void** ppv)
{
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_ICredentialProvider)
    {
        *ppv = static_cast<ICredentialProvider*>(this);
        AddRef();
        return S_OK;
    }
    else if (riid == IID_ICredentialProviderSetUserArray)
    {
        *ppv = static_cast<ICredentialProviderSetUserArray*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG CBlueOpenProvider::AddRef()
{
    return InterlockedIncrement(&_cRef);
}

ULONG CBlueOpenProvider::Release()
{
    ULONG cRef = InterlockedDecrement(&_cRef);
    if (cRef == 0)
    {
        delete this;
    }
    return cRef;
}

// ICredentialProviderSetUserArray
HRESULT CBlueOpenProvider::SetUserArray(_In_ ICredentialProviderUserArray* users)
{
    if (users == nullptr) return E_POINTER;

    DWORD userCount = 0;
    HRESULT hr = users->GetCount(&userCount);
    if (FAILED(hr)) return hr;

    for (DWORD i = 0; i < userCount; ++i)
    {
        ICredentialProviderUser* pUser = nullptr;
        hr = users->GetAt(i, &pUser);
        if (SUCCEEDED(hr) && pUser != nullptr)
        {
            PWSTR pszSid = nullptr;
            hr = pUser->GetSid(&pszSid);
            if (SUCCEEDED(hr) && pszSid != nullptr)
            {
                wcscpy_s(_szTargetUserSid, pszSid);
                CoTaskMemFree(pszSid);

                if (_pCredential != nullptr)
                {
                    static_cast<CBlueOpenCredential*>(_pCredential)->SetTargetUserSid(_szTargetUserSid);
                }
                pUser->Release();
                break;
            }
            pUser->Release();
        }
    }

    return S_OK;
}

// ICredentialProvider
HRESULT CBlueOpenProvider::SetUsageScenario(_In_ CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, _In_ DWORD dwFlags)
{
    switch (cpus)
    {
    case CPUS_LOGON:
    case CPUS_UNLOCK_WORKSTATION:
    case CPUS_CREDUI:
        _cpus = cpus;
        return S_OK;
    default:
        return E_NOTIMPL;
    }
}

HRESULT CBlueOpenProvider::SetSerialization(_In_ const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs)
{
    return S_OK;
}

HRESULT CBlueOpenProvider::GetCredentialCount(_Out_ DWORD* pdwCount, _Out_ DWORD* pdwDefault, _Out_ BOOL* pbAutoLogonWithDefault)
{
    *pdwCount = 1;
    *pdwDefault = 0;

    // Only auto-logon if credentials have already been received via phone / pipe!
    if (_pCredential != nullptr && static_cast<CBlueOpenCredential*>(_pCredential)->HasCredentials())
    {
        *pbAutoLogonWithDefault = TRUE;
    }
    else
    {
        *pbAutoLogonWithDefault = FALSE;
    }

    return S_OK;
}

HRESULT CBlueOpenProvider::GetCredentialAt(_In_ DWORD dwIndex, _Outptr_ ICredentialProviderCredential** ppcpc)
{
    if (dwIndex != 0) return E_INVALIDARG;

    if (_pCredential == nullptr)
    {
        CBlueOpenCredential* pCred = new (std::nothrow) CBlueOpenCredential();
        if (pCred == nullptr) return E_OUTOFMEMORY;

        if (_szTargetUserSid[0] != L'\0')
        {
            pCred->SetTargetUserSid(_szTargetUserSid);
        }

        HRESULT hr = pCred->Initialize(_cpus);
        if (FAILED(hr))
        {
            pCred->Release();
            return hr;
        }

        _pCredential = pCred;

        // Forward saved events to the newly created credential tile
        if (_pEvents != nullptr)
        {
            pCred->SetProviderEvents(_pEvents, _upAdviseContext);
        }
    }

    _pCredential->AddRef();
    *ppcpc = _pCredential;
    return S_OK;
}

HRESULT CBlueOpenProvider::Advise(_In_ ICredentialProviderEvents* pEvents, _In_ UINT_PTR upAdviseContext)
{
    if (_pEvents != nullptr)
    {
        _pEvents->Release();
    }
    _pEvents = pEvents;
    _upAdviseContext = upAdviseContext;
    if (_pEvents != nullptr)
    {
        _pEvents->AddRef();
    }

    // If credential tile is already created, forward events to it
    if (_pCredential != nullptr)
    {
        CBlueOpenCredential* pCred = static_cast<CBlueOpenCredential*>(_pCredential);
        pCred->SetProviderEvents(pEvents, upAdviseContext);
    }
    return S_OK;
}

HRESULT CBlueOpenProvider::UnAdvise()
{
    if (_pCredential != nullptr)
    {
        CBlueOpenCredential* pCred = static_cast<CBlueOpenCredential*>(_pCredential);
        pCred->SetProviderEvents(nullptr, 0);
    }
    if (_pEvents != nullptr)
    {
        _pEvents->Release();
        _pEvents = nullptr;
    }
    return S_OK;
}

static const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR s_rgFieldDescriptors[] = {
    { FID_LOGO, CPFT_TILE_IMAGE, (LPWSTR)L"Logo", GUID_NULL },
    { FID_TITLE, CPFT_LARGE_TEXT, (LPWSTR)L"BlueOpen Bluetooth & Net", GUID_NULL },
    { FID_STATUS, CPFT_SMALL_TEXT, (LPWSTR)L"Status", GUID_NULL },
    { FID_NET_REQUEST_BUTTON, CPFT_COMMAND_LINK, (LPWSTR)L"🌐 Запросить вход через интернет", GUID_NULL },
};

HRESULT CBlueOpenProvider::GetFieldDescriptorCount(_Out_ DWORD* pdwCount)
{
    *pdwCount = FID_NUM_FIELDS;
    return S_OK;
}

HRESULT CBlueOpenProvider::GetFieldDescriptorAt(_In_ DWORD dwIndex, _Outptr_ CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** ppcpfd)
{
    if (dwIndex >= FID_NUM_FIELDS) return E_INVALIDARG;

    *ppcpfd = (CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*)CoTaskMemAlloc(sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR));
    if (*ppcpfd == nullptr) return E_OUTOFMEMORY;

    (*ppcpfd)->dwFieldID = s_rgFieldDescriptors[dwIndex].dwFieldID;
    (*ppcpfd)->cpft = s_rgFieldDescriptors[dwIndex].cpft;
    (*ppcpfd)->guidFieldType = s_rgFieldDescriptors[dwIndex].guidFieldType;

    if (s_rgFieldDescriptors[dwIndex].pszLabel != nullptr)
    {
        size_t len = wcslen(s_rgFieldDescriptors[dwIndex].pszLabel) + 1;
        (*ppcpfd)->pszLabel = (LPWSTR)CoTaskMemAlloc(len * sizeof(WCHAR));
        if ((*ppcpfd)->pszLabel == nullptr)
        {
            CoTaskMemFree(*ppcpfd);
            return E_OUTOFMEMORY;
        }
        wcscpy_s((*ppcpfd)->pszLabel, len, s_rgFieldDescriptors[dwIndex].pszLabel);
    }
    else
    {
        (*ppcpfd)->pszLabel = nullptr;
    }

    return S_OK;
}

HRESULT CBlueOpenProvider_CreateInstance(_In_ REFIID riid, _Outptr_ void** ppv)
{
    CBlueOpenProvider* pProvider = new (std::nothrow) CBlueOpenProvider();
    if (pProvider == nullptr) return E_OUTOFMEMORY;

    HRESULT hr = pProvider->QueryInterface(riid, ppv);
    pProvider->Release();
    return hr;
}
