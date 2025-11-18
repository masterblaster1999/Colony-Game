// tools/map_viewer/MapViewerWin.cpp
//
// Standalone Windows-only map viewer for Colony-Game.
//
// Features:
//  - Load/save a simple .cgmv binary map format.
//  - View map as a colored tile grid.
//  - Toggle overlays:
//      * Regions (colors per region id)
//      * Resources (small colored marker)
//      * Nav mesh (walkable vs blocked)
//
// This is intentionally independent from the main game engine,
// so you can drop it into the repo and build it as its own tool.
//
// To integrate with the real game map format:
//  - Replace LoadMapFromFile / SaveMapToFile with calls into your
//    existing map serialization code.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commdlg.h>   // GetOpenFileNameW / GetSaveFileNameW
#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>

// ----------------- Map data structures -----------------------

#pragma pack(push, 1)
struct MapFileHeader
{
    char     magic[4];    // "CGMV"
    uint32_t version;     // 1
    uint32_t width;
    uint32_t height;
};

struct TileOnDisk
{
    std::uint8_t terrain;    // arbitrary terrain type
    std::uint8_t regionId;   // used for region overlay
    std::uint8_t resourceId; // used for resource overlay
    std::uint8_t navFlags;   // bit 0: walkable (1) / blocked (0)
};
#pragma pack(pop)

struct Tile
{
    std::uint8_t terrain = 0;
    std::uint8_t regionId = 0;
    std::uint8_t resourceId = 0;
    std::uint8_t navFlags = 0;  // bit 0: walkable
};

struct Map
{
    std::uint32_t width  = 0;
    std::uint32_t height = 0;
    std::vector<Tile> tiles;

    bool valid() const noexcept { return width > 0 && height > 0 && tiles.size() == static_cast<std::size_t>(width) * height; }

    Tile& at(std::uint32_t x, std::uint32_t y)
    {
        return tiles[static_cast<std::size_t>(y) * width + x];
    }
    const Tile& at(std::uint32_t x, std::uint32_t y) const
    {
        return tiles[static_cast<std::size_t>(y) * width + x];
    }
};

// ----------------- Global viewer state -----------------------

static HINSTANCE g_hInst = nullptr;
static Map       g_Map;
static bool      g_HasMap        = false;
static bool      g_ShowRegions   = true;
static bool      g_ShowResources = true;
static bool      g_ShowNavMesh   = true;

enum MenuId : UINT
{
    ID_FILE_OPEN             = 1,
    ID_FILE_SAVE             = 2,
    ID_FILE_EXIT             = 3,
    ID_VIEW_TOGGLE_REGIONS   = 10,
    ID_VIEW_TOGGLE_RESOURCES = 11,
    ID_VIEW_TOGGLE_NAVMESH   = 12,
};

// ----------------- Utility: Message box helper ----------------

static void ShowErrorBox(const wchar_t* text, const wchar_t* caption = L"Map Viewer Error")
{
    MessageBoxW(nullptr, text, caption, MB_ICONERROR | MB_OK);
}

// ----------------- File dialogs (Open / Save) ----------------
//
// Uses the classic GetOpenFileNameW / GetSaveFileNameW APIs to show
// a file dialog for .cgmv map files. :contentReference[oaicite:2]{index=2}

static bool ShowOpenFileDialog(std::wstring& outPath)
{
    wchar_t buffer[MAX_PATH] = {};

    OPENFILENAMEW ofn{};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = nullptr;
    ofn.lpstrFile       = buffer;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrFilter     = L"Colony Map Viewer (*.cgmv)\0*.cgmv\0All Files\0*.*\0";
    ofn.nFilterIndex    = 1;
    ofn.Flags           = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt     = L"cgmv";

    if (GetOpenFileNameW(&ofn))
    {
        outPath.assign(buffer);
        return true;
    }
    return false;
}

static bool ShowSaveFileDialog(std::wstring& outPath)
{
    wchar_t buffer[MAX_PATH] = {};

    OPENFILENAMEW ofn{};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = nullptr;
    ofn.lpstrFile       = buffer;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrFilter     = L"Colony Map Viewer (*.cgmv)\0*.cgmv\0All Files\0*.*\0";
    ofn.nFilterIndex    = 1;
    ofn.Flags           = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    ofn.lpstrDefExt     = L"cgmv";

    if (GetSaveFileNameW(&ofn))
    {
        outPath.assign(buffer);
        return true;
    }
    return false;
}

// ----------------- Map loading / saving ----------------------

static bool LoadMapFromFile(const std::wstring& path, Map& outMap)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return false;
    }

    MapFileHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in)
        return false;

    if (header.magic[0] != 'C' || header.magic[1] != 'G' ||
        header.magic[2] != 'M' || header.magic[3] != 'V')
    {
        return false;
    }
    if (header.version != 1)
    {
        return false;
    }

    if (header.width == 0 || header.height == 0)
    {
        return false;
    }

    std::size_t count = static_cast<std::size_t>(header.width) * header.height;
    std::vector<TileOnDisk> diskTiles(count);
    in.read(reinterpret_cast<char*>(diskTiles.data()), count * sizeof(TileOnDisk));
    if (!in)
        return false;

    Map tmp;
    tmp.width  = header.width;
    tmp.height = header.height;
    tmp.tiles.resize(count);

    for (std::size_t i = 0; i < count; ++i)
    {
        tmp.tiles[i].terrain    = diskTiles[i].terrain;
        tmp.tiles[i].regionId   = diskTiles[i].regionId;
        tmp.tiles[i].resourceId = diskTiles[i].resourceId;
        tmp.tiles[i].navFlags   = diskTiles[i].navFlags;
    }

    outMap = std::move(tmp);
    return true;
}

static bool SaveMapToFile(const std::wstring& path, const Map& map)
{
    if (!map.valid())
        return false;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    MapFileHeader header{};
    header.magic[0] = 'C';
    header.magic[1] = 'G';
    header.magic[2] = 'M';
    header.magic[3] = 'V';
    header.version  = 1;
    header.width    = map.width;
    header.height   = map.height;

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!out)
        return false;

    std::size_t count = static_cast<std::size_t>(map.width) * map.height;
    std::vector<TileOnDisk> diskTiles(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        diskTiles[i].terrain    = map.tiles[i].terrain;
        diskTiles[i].regionId   = map.tiles[i].regionId;
        diskTiles[i].resourceId = map.tiles[i].resourceId;
        diskTiles[i].navFlags   = map.tiles[i].navFlags;
    }

    out.write(reinterpret_cast<const char*>(diskTiles.data()), count * sizeof(TileOnDisk));
    return static_cast<bool>(out);
}

// ----------------- Map rendering helpers ---------------------

// Simple hash to color regions consistently.
static COLORREF ColorForRegion(std::uint8_t regionId)
{
    if (regionId == 0)
        return RGB(64, 64, 64);

    std::uint32_t r = (regionId * 97u) & 0xFFu;
    std::uint32_t g = (regionId * 57u) & 0xFFu;
    std::uint32_t b = (regionId * 193u) & 0xFFu;

    // Avoid very dark colors
    r = 64 + (r / 2);
    g = 64 + (g / 2);
    b = 64 + (b / 2);

    return RGB(r, g, b);
}

static COLORREF BaseColorForTile(const Tile& t)
{
    // You can change these to match your terrain semantics.
    switch (t.terrain)
    {
    case 0:  return RGB(20, 20, 20);      // unknown / empty
    case 1:  return RGB(30, 60, 160);     // water
    case 2:  return RGB(70, 120, 40);     // grassland
    case 3:  return RGB(120, 90, 50);     // dirt / hill
    case 4:  return RGB(160, 160, 160);   // rock / mountain
    default: return RGB(80, 80, 80);
    }
}

static COLORREF ColorForResource(std::uint8_t resourceId)
{
    switch (resourceId)
    {
    case 1: return RGB(200, 200, 50);  // food
    case 2: return RGB(200, 50, 50);   // iron/metal
    case 3: return RGB(50, 200, 50);   // wood/plants
    default: return RGB(0, 0, 0);      // none / unknown
    }
}

static void PaintMap(HDC hdc, const RECT& client)
{
    if (!g_HasMap || !g_Map.valid())
    {
        const wchar_t* msg = L"No map loaded. Use File -> Open to load a .cgmv map.";
        TextOutW(hdc, 10, 10, msg, static_cast<int>(wcslen(msg)));
        return;
    }

    const int clientW = client.right - client.left;
    const int clientH = client.bottom - client.top;
    if (clientW <= 0 || clientH <= 0) return;

    if (g_Map.width == 0 || g_Map.height == 0) return;

    int tileW = clientW / static_cast<int>(g_Map.width);
    int tileH = clientH / static_cast<int>(g_Map.height);
    if (tileW <= 0) tileW = 1;
    if (tileH <= 0) tileH = 1;

    // Use DC brush & pen for fast color changes.
    HBRUSH  hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(DC_BRUSH));
    HPEN    hOldPen   = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));

    for (std::uint32_t y = 0; y < g_Map.height; ++y)
    {
        for (std::uint32_t x = 0; x < g_Map.width; ++x)
        {
            const Tile& t = g_Map.at(x, y);
            int left   = client.left + static_cast<int>(x) * tileW;
            int top    = client.top  + static_cast<int>(y) * tileH;
            int right  = left + tileW;
            int bottom = top  + tileH;

            // Base terrain color
            COLORREF color = BaseColorForTile(t);

            // Region overlay: tint color by region if enabled
            if (g_ShowRegions)
            {
                COLORREF rc = ColorForRegion(t.regionId);
                // Simple blend: average the two colors
                int r = (GetRValue(color) + GetRValue(rc)) / 2;
                int g = (GetGValue(color) + GetGValue(rc)) / 2;
                int b = (GetBValue(color) + GetBValue(rc)) / 2;
                color = RGB(r, g, b);
            }

            // Nav mesh overlay: darken if non-walkable
            if (g_ShowNavMesh)
            {
                bool walkable = (t.navFlags & 0x1) != 0;
                if (!walkable)
                {
                    int r = GetRValue(color) / 3;
                    int g = GetGValue(color) / 3;
                    int b = GetBValue(color) / 3;
                    color = RGB(r, g, b);
                }
            }

            SetDCBrushColor(hdc, color);
            Rectangle(hdc, left, top, right, bottom);

            // Resource overlay: small inner rectangle
            if (g_ShowResources && t.resourceId != 0)
            {
                COLORREF rc = ColorForResource(t.resourceId);
                SetDCBrushColor(hdc, rc);

                int inset = (tileW < tileH ? tileW : tileH) / 4;
                if (inset < 1) inset = 1;
                int il = left   + inset;
                int it = top    + inset;
                int ir = right  - inset;
                int ib = bottom - inset;

                Rectangle(hdc, il, it, ir, ib);
            }
        }
    }

    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
}

// ----------------- Window / menu creation --------------------

static HMENU CreateMainMenu()
{
    HMENU hMenuBar = CreateMenu();
    HMENU hFile    = CreatePopupMenu();
    HMENU hView    = CreatePopupMenu();

    AppendMenuW(hFile, MF_STRING, ID_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(hFile, MF_STRING, ID_FILE_SAVE, L"&Save As...\tCtrl+S");
    AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hFile, MF_STRING, ID_FILE_EXIT, L"E&xit");

    AppendMenuW(hView, MF_STRING | (g_ShowRegions   ? MF_CHECKED : 0), ID_VIEW_TOGGLE_REGIONS,   L"Show &Regions");
    AppendMenuW(hView, MF_STRING | (g_ShowResources ? MF_CHECKED : 0), ID_VIEW_TOGGLE_RESOURCES, L"Show &Resources");
    AppendMenuW(hView, MF_STRING | (g_ShowNavMesh   ? MF_CHECKED : 0), ID_VIEW_TOGGLE_NAVMESH,   L"Show &Nav Mesh");

    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFile, L"&File");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hView, L"&View");

    return hMenuBar;
}

// ----------------- Window procedure --------------------------

static void UpdateViewMenuChecks(HMENU hMenuBar)
{
    HMENU hView = GetSubMenu(hMenuBar, 1); // "View" is second menu
    if (!hView) return;

    CheckMenuItem(hView, ID_VIEW_TOGGLE_REGIONS,
                  MF_BYCOMMAND | (g_ShowRegions ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hView, ID_VIEW_TOGGLE_RESOURCES,
                  MF_BYCOMMAND | (g_ShowResources ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hView, ID_VIEW_TOGGLE_NAVMESH,
                  MF_BYCOMMAND | (g_ShowNavMesh ? MF_CHECKED : MF_UNCHECKED));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HMENU hMenuBar = CreateMainMenu();
        SetMenu(hwnd, hMenuBar);
        return 0;
    }

    case WM_COMMAND:
    {
        const UINT id  = LOWORD(wParam);
        const UINT evt = HIWORD(wParam);

        switch (id)
        {
        case ID_FILE_OPEN:
            if (evt == 0)
            {
                std::wstring path;
                if (ShowOpenFileDialog(path))
                {
                    Map m;
                    if (!LoadMapFromFile(path, m))
                    {
                        ShowErrorBox(L"Failed to load map file.");
                    }
                    else
                    {
                        g_Map    = std::move(m);
                        g_HasMap = true;
                        InvalidateRect(hwnd, nullptr, TRUE);
                    }
                }
            }
            return 0;

        case ID_FILE_SAVE:
            if (evt == 0)
            {
                if (!g_HasMap || !g_Map.valid())
                {
                    ShowErrorBox(L"No map to save.");
                    return 0;
                }

                std::wstring path;
                if (ShowSaveFileDialog(path))
                {
                    if (!SaveMapToFile(path, g_Map))
                    {
                        ShowErrorBox(L"Failed to save map file.");
                    }
                }
            }
            return 0;

        case ID_FILE_EXIT:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;

        case ID_VIEW_TOGGLE_REGIONS:
            g_ShowRegions = !g_ShowRegions;
            UpdateViewMenuChecks(GetMenu(hwnd));
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;

        case ID_VIEW_TOGGLE_RESOURCES:
            g_ShowResources = !g_ShowResources;
            UpdateViewMenuChecks(GetMenu(hwnd));
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;

        case ID_VIEW_TOGGLE_NAVMESH:
            g_ShowNavMesh = !g_ShowNavMesh;
            UpdateViewMenuChecks(GetMenu(hwnd));
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;

        default:
            break;
        }
        break;
    }

    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintMap(hdc, ps.rcPaint);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ----------------- WinMain entry point -----------------------

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    g_hInst = hInstance;

    const wchar_t CLASS_NAME[] = L"ColonyMapViewerWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc))
    {
        ShowErrorBox(L"RegisterClassW failed.");
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Colony Map Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1024, 768,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd)
    {
        ShowErrorBox(L"CreateWindowExW failed.");
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
