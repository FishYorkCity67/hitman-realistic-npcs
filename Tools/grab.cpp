// Bildschirmfotos ueber DXGI Desktop Duplication (funktioniert auch mit Vollbild-Spielen).
// Aufruf: grab.exe <Zielordner> <Anzahl> <Abstand_ms>
// Speichert halbierte PNGs (shot_000.png, ...).

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>

#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

template <typename T>
static void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

static bool SavePng(IWICImagingFactory* p_Factory, const std::wstring& p_Path, const std::vector<uint8_t>& p_Rgb, UINT p_W, UINT p_H) {
    IWICStream* s_Stream = nullptr;
    IWICBitmapEncoder* s_Encoder = nullptr;
    IWICBitmapFrameEncode* s_Frame = nullptr;
    bool s_Ok = false;
    const UINT s_Stride = p_W * 3;

    if (SUCCEEDED(p_Factory->CreateStream(&s_Stream)) && SUCCEEDED(s_Stream->InitializeFromFilename(p_Path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(p_Factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &s_Encoder)) &&
        SUCCEEDED(s_Encoder->Initialize(s_Stream, WICBitmapEncoderNoCache)) && SUCCEEDED(s_Encoder->CreateNewFrame(&s_Frame, nullptr)) &&
        SUCCEEDED(s_Frame->Initialize(nullptr)) && SUCCEEDED(s_Frame->SetSize(p_W, p_H))) {
        WICPixelFormatGUID s_Format = GUID_WICPixelFormat24bppBGR;

        if (SUCCEEDED(s_Frame->SetPixelFormat(&s_Format)) && s_Format == GUID_WICPixelFormat24bppBGR &&
            SUCCEEDED(s_Frame->WritePixels(p_H, s_Stride, s_Stride * p_H, const_cast<BYTE*>(p_Rgb.data()))) && SUCCEEDED(s_Frame->Commit()) &&
            SUCCEEDED(s_Encoder->Commit())) {
            s_Ok = true;
        }
    }

    SafeRelease(s_Frame);
    SafeRelease(s_Encoder);
    SafeRelease(s_Stream);
    return s_Ok;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        std::fwprintf(stderr, L"grab.exe <ordner> <anzahl> <abstand_ms>\n");
        return 2;
    }

    const std::wstring s_Dir = argv[1];
    const int s_Count = _wtoi(argv[2]);
    const int s_Interval = _wtoi(argv[3]);
    const UINT s_Div = (argc >= 5 && std::wstring(argv[4]) == L"full") ? 1 : 2; // volle oder halbe Aufloesung

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IWICImagingFactory* s_Wic = nullptr;

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_Wic)))) {
        std::fwprintf(stderr, L"WIC fehlt\n");
        return 3;
    }

    // Den Grafikadapter suchen, an dem der Hauptbildschirm haengt.
    IDXGIFactory1* s_Factory = nullptr;
    CreateDXGIFactory1(IID_PPV_ARGS(&s_Factory));
    IDXGIAdapter1* s_Adapter = nullptr;
    IDXGIOutput* s_Output = nullptr;

    for (UINT a = 0; s_Factory && s_Factory->EnumAdapters1(a, &s_Adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        if (s_Adapter->EnumOutputs(0, &s_Output) == S_OK) {
            break;
        }

        SafeRelease(s_Adapter);
    }

    if (!s_Adapter || !s_Output) {
        std::fwprintf(stderr, L"Kein Bildschirm gefunden\n");
        return 4;
    }

    ID3D11Device* s_Device = nullptr;
    ID3D11DeviceContext* s_Context = nullptr;

    if (FAILED(D3D11CreateDevice(s_Adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &s_Device, nullptr, &s_Context))) {
        std::fwprintf(stderr, L"D3D11 fehlgeschlagen\n");
        return 5;
    }

    IDXGIOutput1* s_Output1 = nullptr;
    s_Output->QueryInterface(IID_PPV_ARGS(&s_Output1));
    IDXGIOutputDuplication* s_Dup = nullptr;
    HRESULT s_Hr = s_Output1 ? s_Output1->DuplicateOutput(s_Device, &s_Dup) : E_FAIL;

    if (FAILED(s_Hr)) {
        std::fwprintf(stderr, L"DuplicateOutput fehlgeschlagen: 0x%08X\n", static_cast<unsigned>(s_Hr));
        return 6;
    }

    ID3D11Texture2D* s_Staging = nullptr;
    int s_Saved = 0;
    const DWORD s_T0 = GetTickCount();
    FILE* s_Times = nullptr;
    _wfopen_s(&s_Times, (s_Dir + L"\\times.txt").c_str(), L"w");

    for (int i = 0; i < s_Count; ++i) {
        const DWORD s_Start = GetTickCount();
        DXGI_OUTDUPL_FRAME_INFO s_Info {};
        IDXGIResource* s_Resource = nullptr;
        s_Hr = s_Dup->AcquireNextFrame(1000, &s_Info, &s_Resource);

        if (s_Hr == DXGI_ERROR_ACCESS_LOST) {
            // Modus gewechselt (z. B. Vollbild): neu verbinden.
            SafeRelease(s_Dup);
            SafeRelease(s_Staging);
            Sleep(200);

            if (FAILED(s_Output1->DuplicateOutput(s_Device, &s_Dup))) {
                std::fwprintf(stderr, L"Neu verbinden fehlgeschlagen\n");
                break;
            }

            --i;
            continue;
        }

        if (FAILED(s_Hr)) {
            std::fwprintf(stderr, L"Bild %d: kein neues Bild (0x%08X)\n", i, static_cast<unsigned>(s_Hr));
            continue;
        }

        // Nur Mauszeiger bewegt, kein neues Bild: auf ein echtes Bild warten (begrenzt).
        static int s_Retries = 0;

        if (s_Info.LastPresentTime.QuadPart == 0 && s_Retries < 20) {
            ++s_Retries;
            SafeRelease(s_Resource);
            s_Dup->ReleaseFrame();
            --i;
            continue;
        }

        s_Retries = 0;

        ID3D11Texture2D* s_Texture = nullptr;
        s_Resource->QueryInterface(IID_PPV_ARGS(&s_Texture));
        D3D11_TEXTURE2D_DESC s_Desc {};
        s_Texture->GetDesc(&s_Desc);

        if (!s_Staging) {
            D3D11_TEXTURE2D_DESC s_StagingDesc = s_Desc;
            s_StagingDesc.Usage = D3D11_USAGE_STAGING;
            s_StagingDesc.BindFlags = 0;
            s_StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            s_StagingDesc.MiscFlags = 0;
            s_StagingDesc.MipLevels = 1;
            s_StagingDesc.ArraySize = 1;
            s_StagingDesc.SampleDesc = { 1, 0 };
            s_Device->CreateTexture2D(&s_StagingDesc, nullptr, &s_Staging);
        }

        s_Context->CopyResource(s_Staging, s_Texture);

        if (s_Times) {
            std::fprintf(s_Times, "%d %lu\n", i, GetTickCount() - s_T0);
            std::fflush(s_Times);
        }

        D3D11_MAPPED_SUBRESOURCE s_Map {};

        if (SUCCEEDED(s_Context->Map(s_Staging, 0, D3D11_MAP_READ, 0, &s_Map))) {
            // Optional halbieren (2x2-Mittel), BGRA -> BGR.
            const UINT s_W = s_Desc.Width / s_Div;
            const UINT s_H = s_Desc.Height / s_Div;
            std::vector<uint8_t> s_Rgb(static_cast<size_t>(s_W) * s_H * 3);
            const auto* s_Src = static_cast<const uint8_t*>(s_Map.pData);

            for (UINT y = 0; y < s_H; ++y) {
                for (UINT x = 0; x < s_W; ++x) {
                    for (int c = 0; c < 3; ++c) {
                        uint8_t s_Value;

                        if (s_Div == 1) {
                            s_Value = s_Src[y * s_Map.RowPitch + x * 4 + c];
                        }
                        else {
                            const uint8_t* p0 = s_Src + (y * 2) * s_Map.RowPitch + (x * 2) * 4 + c;
                            const uint8_t* p1 = s_Src + (y * 2 + 1) * s_Map.RowPitch + (x * 2) * 4 + c;
                            s_Value = static_cast<uint8_t>((p0[0] + p0[4] + p1[0] + p1[4]) / 4);
                        }

                        s_Rgb[(static_cast<size_t>(y) * s_W + x) * 3 + c] = s_Value;
                    }
                }
            }

            s_Context->Unmap(s_Staging, 0);
            wchar_t s_Name[64];
            swprintf_s(s_Name, L"\\shot_%03d.png", i);

            if (SavePng(s_Wic, s_Dir + s_Name, s_Rgb, s_W, s_H)) {
                ++s_Saved;
            }
        }

        SafeRelease(s_Texture);
        SafeRelease(s_Resource);
        s_Dup->ReleaseFrame();

        const DWORD s_Elapsed = GetTickCount() - s_Start;

        if (static_cast<int>(s_Elapsed) < s_Interval) {
            Sleep(s_Interval - s_Elapsed);
        }
    }

    std::wprintf(L"%d Bilder gespeichert\n", s_Saved);
    return s_Saved > 0 ? 0 : 1;
}
