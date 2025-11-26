// renderer/dxgi_support.h
inline bool CheckTearingSupport() {
    BOOL allowTearing = FALSE;
    ComPtr<IDXGIFactory5> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        if (FAILED(factory->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)))) {
            allowTearing = FALSE;
        }
    }
    return allowTearing == TRUE;
}
