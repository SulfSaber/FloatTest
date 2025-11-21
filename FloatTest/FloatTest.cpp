// FloatTest_Optimized.cpp
// Optimized version of FloatNumberLineDirect2D.cpp
// Compile with Visual Studio, link d2d1.lib, dwrite.lib

#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <memory>
#include <unordered_map>
#undef DrawText

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")

// ---------------------------- Utilities -----------------------------------

inline uint32_t float_to_u32(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}
inline float u32_to_float(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

inline uint64_t double_to_u64(double d) {
    uint64_t u;
    std::memcpy(&u, &d, sizeof(u));
    return u;
}
inline double u64_to_double(uint64_t u) {
    double d;
    std::memcpy(&d, &u, sizeof(d));
    return d;
}

inline uint32_t float_to_ordered(uint32_t bits) {
    if (bits & 0x80000000u) return ~bits;
    else return bits ^ 0x80000000u;
}
inline uint32_t ordered_to_floatbits(uint32_t ord) {
    if (ord & 0x80000000u) return ord ^ 0x80000000u;
    else return ~ord;
}

inline uint64_t double_to_ordered(uint64_t bits) {
    if (bits & 0x8000000000000000ull) return ~bits;
    else return bits ^ 0x8000000000000000ull;
}
inline uint64_t ordered_to_doublebits(uint64_t ord) {
    if (ord & 0x8000000000000000ull) return ord ^ 0x8000000000000000ull;
    else return ~ord;
}

// ------------------------ Globals & Camera --------------------------------

static ID2D1Factory* g_pFactory = nullptr;
static ID2D1HwndRenderTarget* g_pRenderTarget = nullptr;
static ID2D1SolidColorBrush* g_pBrush = nullptr;
static IDWriteFactory* g_pDWriteFactory = nullptr;
static IDWriteTextFormat* g_pTextFormat = nullptr;

static HWND g_hWnd = nullptr;

int g_windowWidth = 1200;
int g_windowHeight = 240;

// Camera
double viewCenter = 0.0;
//double viewHalfWidth = 10.0;
double viewHalfWidth = FLT_MAX;
double pixelsPerWorld = 0.0;

// Interaction
bool dragging = false;
POINT dragStart;
double viewCenterStart = 0.0;
bool showDoubleMode = false;

// LOD control: reduce default to be more interactive
uint64_t maxPointsToDraw = 100000;

// Brushes cache

// ----------------------- Color utility ------------------------------------

D2D1::ColorF bandColorFromExponent(int exp, bool /*isDouble*/) {
    int base = std::abs(exp);
    float hue = (base * 37) % 360;
    float c = 1.0f;
    float x = 1.0f - fabs(fmod(hue / 60.0f, 2.0f) - 1.0f);
    float r = 0, g = 0, b = 0;
    int region = int(hue / 60.0f) % 6;
    switch (region) {
    case 0: r = c; g = x; b = 0; break;
    case 1: r = x; g = c; b = 0; break;
    case 2: r = 0; g = c; b = x; break;
    case 3: r = 0; g = x; b = c; break;
    case 4: r = x; g = 0; b = c; break;
    case 5: r = c; g = 0; b = x; break;
    }
    return D2D1::ColorF(r * 0.82f, g * 0.82f, b * 0.82f, 1.0f);
}

// -------------------- Coordinate transforms -------------------------------

inline float worldToScreenX(double worldX) {
    double left = viewCenter - viewHalfWidth;
    double s = (worldX - left) / (2.0 * viewHalfWidth) * g_windowWidth;
    return (float)s;
}
inline double screenToWorldX(int sx) {
    double left = viewCenter - viewHalfWidth;
    return left + (double(sx) / g_windowWidth) * (2.0 * viewHalfWidth);
}

// -------------------- Drawing helpers ------------------------------------

void CreateDeviceResources() {
    if (!g_pRenderTarget) {
        RECT rc;
        GetClientRect(g_hWnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

        HRESULT hr = g_pFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(g_hWnd, size),
            &g_pRenderTarget);
        if (FAILED(hr)) return;

        // one-time brushes (kept until target recreated)
        g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &g_pBrush);

        if (!g_pDWriteFactory) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_pDWriteFactory));
        if (g_pDWriteFactory && !g_pTextFormat) {
            g_pDWriteFactory->CreateTextFormat(
                L"Consolas",
                nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                12.0f,
                L"en-US",
                &g_pTextFormat);
        }
    }
}

void ReleaseBrushes() {

}

void DiscardDeviceResources() {
    ReleaseBrushes();
    if (g_pRenderTarget) { g_pRenderTarget->Release(); g_pRenderTarget = nullptr; }
    if (g_pTextFormat) { g_pTextFormat->Release(); g_pTextFormat = nullptr; }
    if (g_pDWriteFactory) { g_pDWriteFactory->Release(); g_pDWriteFactory = nullptr; }
}

// draw small dot centered at (sx, sy)
inline void DrawDotFast(ID2D1HwndRenderTarget* rt, float sx, float sy, float radius, ID2D1SolidColorBrush* brush) {
    // If radius <= 1 use FillRectangle (cheaper than ellipse)
    //if (radius <= 1.2f) {
        D2D1_RECT_F r = D2D1::RectF(sx - radius, sy + 10.f, sx + radius, sy - 10.f);
        rt->FillRectangle(r, brush);
    //}
    //else {
    //    D2D1_ELLIPSE ell = D2D1::Ellipse(D2D1::Point2F(sx, sy), radius, radius);
    //    rt->FillEllipse(&ell, brush);
    //}
}

// -------------------- Main render function --------------------------------

void Render() {

    // Slowly animate zooming in
    if (false)
    {
        double notchCount = 0.01f; // normally +/-1 per notch
        double baseFactor = 0.85; // smaller baseFactor = faster zoom
        double zoomFactor = pow(baseFactor, notchCount); // multiply viewHalfWidth by this

        // Clamp extremes
        double newHalf = viewHalfWidth * zoomFactor;
        if (newHalf < 1e-308) newHalf = 1e-308;
        if (newHalf > 1e308) newHalf = 1e308;

        viewHalfWidth = newHalf;
    }

    CreateDeviceResources();
    if (!g_pRenderTarget) return;

    g_pRenderTarget->BeginDraw();
    g_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));

    pixelsPerWorld = (g_windowWidth) / (2.0 * viewHalfWidth);
    float centerY = g_windowHeight / 2.0f;

    // draw baseline
    g_pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));
    g_pRenderTarget->DrawLine(D2D1::Point2F(0, centerY), D2D1::Point2F((float)g_windowWidth, centerY), g_pBrush, 1.0f);

    double left = viewCenter - viewHalfWidth;
    double right = viewCenter + viewHalfWidth;

    if (!showDoubleMode) {
        float leftF = static_cast<float>(std::max(left, (double)-std::numeric_limits<float>::max()));
        float rightF = static_cast<float>(std::min(right, (double)std::numeric_limits<float>::max()));
        if (!(leftF <= rightF)) goto REND_END;

        uint32_t leftBits = float_to_u32(leftF);
        uint32_t rightBits = float_to_u32(rightF);
        uint32_t leftOrd = float_to_ordered(leftBits);
        uint32_t rightOrd = float_to_ordered(rightBits);
        if (leftOrd > rightOrd) std::swap(leftOrd, rightOrd);

        uint64_t totalPoints = (uint64_t)rightOrd - (uint64_t)leftOrd + 1ull;
        uint64_t step = 1;
        if (totalPoints > maxPointsToDraw) {
            step = (totalPoints + maxPointsToDraw - 1) / maxPointsToDraw;
        }

        // dot radius scales with zoom but clamp

        // iterate ordered integers
        for (uint64_t ord = leftOrd; ord <= rightOrd; ord += step) {
            uint64_t pickedOrd = ord;

            uint32_t bits = ordered_to_floatbits((uint32_t)pickedOrd);
            float value = u32_to_float(bits);

            if (std::isnan(value) || std::isinf(value)) continue;

            float sx = worldToScreenX((double)value);
            if (sx < -400.0f || sx > g_windowWidth + 4.0f) continue;

            double nextValue = std::nextafter(value, INFINITY);
            double ulp = nextValue - value;
            float nextSx = worldToScreenX(nextValue);
            float pixelSpacing = fabs(nextSx - sx);

            float dotRadius = pixelSpacing >= 6.f ? 3.f : 1.f;

            bool detailMode = (pixelSpacing >= 20.0f && step == 1);

            if (detailMode) {
                std::ostringstream ss;
                ss << std::setprecision(8) << value;
                std::string txt = ss.str();

                D2D1_RECT_F r = D2D1::RectF(sx + 4.0f, centerY - 40.0f,
                    sx + 200.0f, centerY - 20.0f);

                std::wstring wtxt(txt.begin(), txt.end());   // convert to UTF-16

                g_pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));
                g_pRenderTarget->DrawTextW(
                    wtxt.c_str(),
                    (UINT32)wtxt.size(),
                    g_pTextFormat,
                    r,
                    g_pBrush
                );

                float midY = centerY + 30.0f;
                float leftX = sx;
                float rightX = nextSx;

                g_pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::LightGray));
                g_pRenderTarget->DrawLine(
                    D2D1::Point2F(leftX, midY),
                    D2D1::Point2F(rightX, midY),
                    g_pBrush, 1.5f);

                g_pRenderTarget->DrawLine(
                    D2D1::Point2F(leftX, midY - 5.0f),
                    D2D1::Point2F(leftX, midY + 5.0f),
                    g_pBrush, 1.5f);

                g_pRenderTarget->DrawLine(
                    D2D1::Point2F(rightX, midY - 5.0f),
                    D2D1::Point2F(rightX, midY + 5.0f),
                    g_pBrush, 1.5f);

                std::ostringstream ss2;
                ss2 << std::setprecision(10) << ulp;
                std::string ulpText = ss2.str();

                D2D1_RECT_F r2 = D2D1::RectF(
                    (leftX + rightX) * 0.5f - 80.0f,
                    midY + 4.0f,
                    (leftX + rightX) * 0.5f + 80.0f,
                    midY + 24.0f
                );

                std::wstring wUlp(ulpText.begin(), ulpText.end());

                g_pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));
                g_pRenderTarget->DrawTextW(
                    wUlp.c_str(),
                    (UINT32)wUlp.size(),
                    g_pTextFormat,
                    r2,
                    g_pBrush
                );
            }

            uint32_t biased = (bits >> 23) & 0xFFu;

            {
                int unbiased = (biased == 0) ? -127 : (int)biased - 127;
                D2D1::ColorF c = bandColorFromExponent(unbiased, false);
                g_pBrush->SetColor(c);
            }

            DrawDotFast(g_pRenderTarget, sx, centerY, dotRadius, g_pBrush);
        }
    }

REND_END:
    // Draw UI text
    if (g_pTextFormat) {
        std::wostringstream ws;
        ws << L"Mode: " << (showDoubleMode ? L"double (64-bit)" : L"float (32-bit)");
        ws << L"    Center: " << std::fixed << std::setprecision(6) << viewCenter;
        ws << L"    HalfWidth: " << std::scientific << std::setprecision(3) << viewHalfWidth;
        std::wstring s = ws.str();
        D2D1_RECT_F layout = D2D1::RectF(4, 4, (float)g_windowWidth - 4, 40);
        g_pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));
        g_pRenderTarget->DrawTextW(s.c_str(), (UINT32)s.size(), g_pTextFormat, layout, g_pBrush);
    }

    HRESULT hr = g_pRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

// -------------------- Mouse / interaction ---------------------------------

void OnWheel(WPARAM wParam, LPARAM lParam) {
    short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
    POINT pt; pt.x = (short)LOWORD(lParam); pt.y = (short)HIWORD(lParam);
    ScreenToClient(g_hWnd, &pt);
    double mouseWorld = screenToWorldX(pt.x);

    // Improved zoom: exponential based on zDelta. Use Ctrl for fine control.
    bool fine = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    double notchCount = zDelta / 120.0; // normally +/-1 per notch
    double baseFactor = fine ? 0.85 : 0.6; // smaller baseFactor = faster zoom
    double zoomFactor = pow(baseFactor, notchCount); // multiply viewHalfWidth by this

    // Clamp extremes
    double newHalf = viewHalfWidth * zoomFactor;
    if (newHalf < 1e-308) newHalf = 1e-308;
    if (newHalf > 1e308) newHalf = 1e308;

    // Keep zoom centered on mouseWorld:
    double leftBefore = viewCenter - viewHalfWidth;
    double rel = (mouseWorld - leftBefore) / (2.0 * viewHalfWidth);
    double newLeft = mouseWorld - rel * 2.0 * newHalf;
    viewHalfWidth = newHalf;
    //viewCenter = newLeft + viewHalfWidth;

    InvalidateRect(g_hWnd, nullptr, FALSE);
}

void OnLButtonDown(int x, int y) {
    dragging = true;
    dragStart.x = x;
    dragStart.y = y;
    viewCenterStart = viewCenter;
    SetCapture(g_hWnd);
}

void OnLButtonUp() {
    dragging = false;
    ReleaseCapture();
}

void OnMouseMove(int x, int y) {
    if (dragging) {
        int dx = x - dragStart.x;
        double worldDx = -(double)dx / (double)g_windowWidth * 2.0 * viewHalfWidth;
        viewCenter = viewCenterStart + worldDx;
        InvalidateRect(g_hWnd, nullptr, FALSE);
    }
    else {
        // no-op (no heavy work)
    }
}

// -------------------- Win32 stuff -----------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateDeviceResources();
        return 0;
    case WM_SIZE:
        if (g_pRenderTarget) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            g_windowWidth = rc.right - rc.left;
            g_windowHeight = rc.bottom - rc.top;
            D2D1_SIZE_U size = D2D1::SizeU(g_windowWidth, g_windowHeight);
            g_pRenderTarget->Resize(size);
        }
        return 0;
    case WM_PAINT:
        ValidateRect(hwnd, nullptr);
        return 0;
    case WM_MOUSEWHEEL:
        OnWheel(wParam, lParam);
        return 0;
    case WM_LBUTTONDOWN:
        OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        OnLButtonUp();
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_KEYDOWN:
        if (wParam == 'F') {
            showDoubleMode = !showDoubleMode;
            InvalidateRect(g_hWnd, nullptr, FALSE);
        }
        else if (wParam == 'R') {
            viewCenter = 0.0;
            viewHalfWidth = 10.0;
            InvalidateRect(g_hWnd, nullptr, FALSE);
        }
        else if (wParam == 'D') {
            // optional: change budget
            maxPointsToDraw = std::min<uint64_t>(maxPointsToDraw * 2, 1000000ull);
            InvalidateRect(g_hWnd, nullptr, FALSE);
        }
        else if (wParam == 'S') {
            maxPointsToDraw = std::max<uint64_t>(1000ull, maxPointsToDraw / 2);
            InvalidateRect(g_hWnd, nullptr, FALSE);
        }
        return 0;
    case WM_DESTROY:
        DiscardDeviceResources();
        if (g_pFactory) { g_pFactory->Release(); g_pFactory = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

// -------------------- Entry point ----------------------------------------

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pFactory);

    WNDCLASS wc = { };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FloatNumberLineClassOpt";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    g_hWnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        L"Float/Double Number-Line Visualization - Optimized (Wheel=zoom, Ctrl+Wheel=fine, Drag=pan, F=toggle, R=reset)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, g_windowWidth, g_windowHeight,
        NULL, NULL, hInstance, NULL);

    ShowWindow(g_hWnd, nCmdShow);

    MSG msg = {};
    while (true)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return 0;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        Render();
    }

    return 0;
}
