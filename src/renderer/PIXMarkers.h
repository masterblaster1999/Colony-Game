#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
class PixAnnot {
public:
    explicit PixAnnot(ID3D11DeviceContext* ctx) { ctx->QueryInterface(__uuidof(ID3DUserDefinedAnnotation), (void**)&m_annot); }
    void Begin(const wchar_t* name) { if (m_annot) m_annot->BeginEvent(name); }
    void End()                      { if (m_annot) m_annot->EndEvent(); }
private:
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> m_annot;
};
