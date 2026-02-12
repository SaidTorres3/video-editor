// imgui_ui.cpp - Complete ImGui UI for the Video Editor
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "imgui_ui.h"
#include "video_player.h"
#include "video_decoder.h"
#include "video_renderer.h"
#include "options_window.h"
#include "progress_window.h"
#include "file_handling.h"
#include "editing.h"
#include "b2_upload.h"
#include "catbox_upload.h"
#include "utils.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <d3d11.h>
#include <commdlg.h>
#include <shellapi.h>
#include <cmath>
#include <algorithm>
#include <string>
#include <thread>
#include <filesystem>
#include <cstdio>

// ---- Externs ----
extern VideoPlayer* g_videoPlayer;
extern double g_cutStartTime;
extern double g_cutEndTime;
extern double g_previewSeekTime;
extern EncoderSelection g_encoderSelection;
extern bool g_autoUpload;
extern bool g_useCatbox;
extern bool g_useB2;
extern bool g_autoPlay;
extern bool g_logToFile;
extern std::wstring g_qualityPreset;
extern std::wstring g_b2KeyId;
extern std::wstring g_b2AppKey;
extern std::wstring g_b2BucketId;
extern std::wstring g_b2BucketName;
extern std::wstring g_b2CustomUrl;
extern std::wstring g_catboxUserHash;
extern bool g_lastOperationWasExport;
extern bool g_uploadSuccess;
extern std::wstring g_catboxUploadedUrl;
extern std::wstring g_b2UploadedUrl;
extern bool g_catboxUploadSuccess;
extern bool g_b2UploadSuccess;
extern std::wstring g_lastOutputFile;
extern std::atomic<bool> g_cancelExport;

// ---- Video Texture ----
ID3D11ShaderResourceView* g_pVideoSRV = nullptr;
ID3D11Texture2D*          g_pVideoTexture = nullptr;
int                       g_videoTexWidth = 0;
int                       g_videoTexHeight = 0;

// ---- UI State ----
static bool g_showOptionsWindow = false;
static bool g_showProgressPopup = false;
static bool g_showResultPopup = false;
static bool g_showUploadPopup = false;
static std::string g_resultMessage;
static int g_selectedAudioTrack = -1;
static float g_masterVolumeDb = 0.0f;
static float g_trackVolumeDb = 0.0f;
static bool g_mergeAudio = false;
static int g_codecMode = 0; // 0 = H264, 1 = Copy
static int g_bitrateMode = 0; // 0 = Bitrate, 1 = Target Size
static int g_bitrateKbps = 0;
static int g_targetSizeMB = 0;
static double g_timelineZoom = 1.0;
static double g_timelineScroll = 0.0;
static bool g_timelineDragging = false;
static char g_startTimeStr[64] = "";
static char g_endTimeStr[64] = "";
static bool g_exportRunning = false;
static float g_exportProgress = 0.0f;
static std::string g_exportStatus = "";

// Upload popup state
static bool g_manualUploadMode = false;
static bool g_catboxUploading = false;
static bool g_b2Uploading = false;
static std::string g_catboxResultUrl;
static std::string g_b2ResultUrl;
static bool g_catboxDone = false;
static bool g_b2Done = false;

// Crop selection state
static bool g_cropSelecting = false;
static ImVec2 g_cropSelStart = ImVec2(0, 0); // screen-space start of drag
static ImVec2 g_cropSelCurrent = ImVec2(0, 0); // screen-space current drag pos

// Keyframe context menu state
static bool g_keyframeContextMenuOpen = false;
static double g_keyframeContextMenuTime = -1.0; // which keyframe was clicked
static bool g_keyframeMoveMode = false; // active keyframe move mode
static double g_keyframeMovingTime = -1.0; // keyframe being moved
static bool g_blockTimelineUntilMouseRelease = false;

// ---- Color Palette ----
namespace Colors {
    static const ImVec4 BgDark       = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    static const ImVec4 BgPanel      = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    static const ImVec4 BgHeader     = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    static const ImVec4 Accent       = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    static const ImVec4 AccentHover  = ImVec4(0.36f, 0.66f, 1.00f, 1.00f);
    static const ImVec4 AccentActive = ImVec4(0.20f, 0.50f, 0.85f, 1.00f);
    static const ImVec4 TextPrimary  = ImVec4(0.92f, 0.93f, 0.94f, 1.00f);
    static const ImVec4 TextSecondary= ImVec4(0.60f, 0.62f, 0.65f, 1.00f);
    static const ImVec4 Green        = ImVec4(0.30f, 0.75f, 0.40f, 1.00f);
    static const ImVec4 Red          = ImVec4(0.85f, 0.30f, 0.30f, 1.00f);
    static const ImVec4 Orange       = ImVec4(0.90f, 0.60f, 0.20f, 1.00f);
    static const ImVec4 Yellow       = ImVec4(0.95f, 0.85f, 0.20f, 1.00f);
    static const ImVec4 TimelineBg   = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    static const ImVec4 TimelineBar  = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    static const ImVec4 Separator    = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
}

// ---- Helper: Wide/Narrow conversions ----
static std::string WtoA(const std::wstring& w) {
    if (w.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(sz - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), sz, nullptr, nullptr);
    return s;
}

static std::wstring AtoW(const std::string& a) {
    if (a.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, nullptr, 0);
    std::wstring w(sz - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, w.data(), sz);
    return w;
}

static std::string FormatTimeA(double totalSeconds, bool showMs = false) {
    std::wstring w = FormatTime(totalSeconds, showMs);
    return WtoA(w);
}

// ---- ImGui Style Setup ----
void InitImGuiStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Rounding
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;

    // Sizing
    style.WindowPadding     = ImVec2(12, 12);
    style.FramePadding      = ImVec2(8, 5);
    style.ItemSpacing       = ImVec2(8, 6);
    style.ItemInnerSpacing  = ImVec2(6, 4);
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 8.0f;
    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 0.0f;
    style.SeparatorTextBorderSize = 2.0f;

    // Colors - Modern dark theme
    colors[ImGuiCol_Text]                   = Colors::TextPrimary;
    colors[ImGuiCol_TextDisabled]           = Colors::TextSecondary;
    colors[ImGuiCol_WindowBg]               = Colors::BgDark;
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.12f, 0.12f, 0.14f, 0.97f);
    colors[ImGuiCol_Border]                 = Colors::Separator;
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    colors[ImGuiCol_TitleBg]                = Colors::BgPanel;
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.10f, 0.10f, 0.12f, 0.75f);
    colors[ImGuiCol_MenuBarBg]              = Colors::BgPanel;
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.10f, 0.10f, 0.12f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.36f, 0.36f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.44f, 0.44f, 0.48f, 1.00f);
    colors[ImGuiCol_CheckMark]              = Colors::Accent;
    colors[ImGuiCol_SliderGrab]             = Colors::Accent;
    colors[ImGuiCol_SliderGrabActive]       = Colors::AccentHover;
    colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = Colors::AccentActive;
    colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = Colors::AccentActive;
    colors[ImGuiCol_Separator]              = Colors::Separator;
    colors[ImGuiCol_SeparatorHovered]       = Colors::Accent;
    colors[ImGuiCol_SeparatorActive]        = Colors::AccentActive;
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);
    colors[ImGuiCol_TabHovered]             = Colors::AccentHover;
    colors[ImGuiCol_TabSelected]            = Colors::Accent;
    colors[ImGuiCol_TabSelectedOverline]    = Colors::Accent;
    colors[ImGuiCol_TabDimmed]              = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]      = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_PlotLines]              = Colors::Accent;
    colors[ImGuiCol_PlotLinesHovered]       = Colors::Red;
    colors[ImGuiCol_PlotHistogram]          = Colors::Accent;
    colors[ImGuiCol_PlotHistogramHovered]   = Colors::Orange;
    colors[ImGuiCol_TableHeaderBg]          = Colors::BgHeader;
    colors[ImGuiCol_TableBorderStrong]      = Colors::Separator;
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = Colors::Yellow;
    colors[ImGuiCol_NavCursor]              = Colors::Accent;
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);

    // Font
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);
}

// ---- Video Texture Management ----
void CleanupVideoTexture()
{
    if (g_pVideoSRV) { g_pVideoSRV->Release(); g_pVideoSRV = nullptr; }
    if (g_pVideoTexture) { g_pVideoTexture->Release(); g_pVideoTexture = nullptr; }
    g_videoTexWidth = 0;
    g_videoTexHeight = 0;
}

void UpdateVideoTexture()
{
    if (!g_videoPlayer || !g_videoPlayer->IsLoaded() || !g_pd3dDevice)
        return;

    int fw = g_videoPlayer->frameWidth;
    int fh = g_videoPlayer->frameHeight;
    if (fw <= 0 || fh <= 0)
        return;

    // Get frame data
    uint8_t* frameData = nullptr;
    int linesize = 0;
    {
        std::lock_guard<std::mutex> lock(g_videoPlayer->decodeMutex);
        if (g_videoPlayer->frameRGB && g_videoPlayer->frameRGB->data[0])
        {
            frameData = g_videoPlayer->frameRGB->data[0];
            linesize = g_videoPlayer->frameRGB->linesize[0];
        }
    }
    if (!frameData)
        return;

    // Recreate texture if size changed
    if (fw != g_videoTexWidth || fh != g_videoTexHeight)
    {
        CleanupVideoTexture();
        g_videoTexWidth = fw;
        g_videoTexHeight = fh;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = fw;
        desc.Height = fh;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = g_pd3dDevice->CreateTexture2D(&desc, nullptr, &g_pVideoTexture);
        if (FAILED(hr)) return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        hr = g_pd3dDevice->CreateShaderResourceView(g_pVideoTexture, &srvDesc, &g_pVideoSRV);
        if (FAILED(hr)) { CleanupVideoTexture(); return; }
    }

    // Update texture data
    if (g_pVideoTexture && g_pd3dDeviceContext)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = g_pd3dDeviceContext->Map(g_pVideoTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr))
        {
            std::lock_guard<std::mutex> lock(g_videoPlayer->decodeMutex);
            if (g_videoPlayer->frameRGB && g_videoPlayer->frameRGB->data[0])
            {
                uint8_t* src = g_videoPlayer->frameRGB->data[0];
                int srcPitch = g_videoPlayer->frameRGB->linesize[0];
                uint8_t* dst = (uint8_t*)mapped.pData;
                int copyBytes = std::min(srcPitch, (int)mapped.RowPitch);
                for (int y = 0; y < fh; y++)
                    memcpy(dst + y * mapped.RowPitch, src + y * srcPitch, copyBytes);
            }
            g_pd3dDeviceContext->Unmap(g_pVideoTexture, 0);
        }
    }
}

// ---- Styled button helpers ----
static bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Button, Colors::Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colors::AccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Colors::AccentActive);
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

static bool DangerButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Button, Colors::Red);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.40f, 0.40f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.20f, 0.20f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

static bool GreenButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Button, Colors::Green);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.85f, 0.50f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.65f, 0.35f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

// ---- Section header helper ----
static void SectionHeader(const char* label)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::Accent);
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

// ---- Timeline Widget ----
static void DrawTimeline(float width, float height)
{
    if (!g_videoPlayer || !g_videoPlayer->IsLoaded())
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::TimelineBg);
        ImGui::BeginChild("TimelineEmpty", ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImVec2 center = ImVec2(
            ImGui::GetWindowPos().x + width * 0.5f,
            ImGui::GetWindowPos().y + height * 0.5f
        );
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(center.x - 60, center.y - 8),
            IM_COL32(120, 120, 130, 255), "No video loaded");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    double duration = g_videoPlayer->GetDuration();
    double currentTime = (g_previewSeekTime >= 0.0) ? g_previewSeekTime : g_videoPlayer->GetCurrentTime();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::TimelineBg);
    ImGui::BeginChild("Timeline", ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float barY = pos.y + 20;
    float barH = size.y - 30;
    float barW = size.x;

    // Visible time range based on zoom
    double visibleDuration = duration / g_timelineZoom;
    double viewStart = g_timelineScroll;
    double viewEnd = viewStart + visibleDuration;
    if (viewEnd > duration) { viewEnd = duration; viewStart = duration - visibleDuration; if (viewStart < 0) viewStart = 0; }

    auto timeToX = [&](double t) -> float {
        return pos.x + (float)((t - viewStart) / visibleDuration) * barW;
    };
    auto xToTime = [&](float x) -> double {
        return viewStart + ((x - pos.x) / barW) * visibleDuration;
    };

    // Background bar
    dl->AddRectFilled(ImVec2(pos.x, barY), ImVec2(pos.x + barW, barY + barH),
        IM_COL32(30, 30, 34, 255), 3.0f);

    // Cut region highlight
    if (g_cutStartTime >= 0 && g_cutEndTime > g_cutStartTime)
    {
        float sx = timeToX(g_cutStartTime);
        float ex = timeToX(g_cutEndTime);
        dl->AddRectFilled(ImVec2(sx, barY), ImVec2(ex, barY + barH),
            IM_COL32(66, 150, 250, 40));
    }

    // Time ruler ticks
    {
        double tickInterval = 1.0;
        if (visibleDuration > 600) tickInterval = 60.0;
        else if (visibleDuration > 120) tickInterval = 10.0;
        else if (visibleDuration > 30) tickInterval = 5.0;
        else if (visibleDuration > 10) tickInterval = 2.0;
        else if (visibleDuration > 5) tickInterval = 1.0;
        else tickInterval = 0.5;

        double startTick = std::floor(viewStart / tickInterval) * tickInterval;
        for (double t = startTick; t <= viewEnd; t += tickInterval)
        {
            if (t < viewStart) continue;
            float x = timeToX(t);
            bool major = (std::fmod(t, tickInterval * 5.0) < 0.001);
            float tickH = major ? 12.0f : 6.0f;
            dl->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + tickH),
                IM_COL32(80, 80, 90, 255), 1.0f);

            if (major || visibleDuration < 30)
            {
                std::string label = FormatTimeA(t);
                dl->AddText(ImVec2(x + 2, pos.y + 2), IM_COL32(140, 140, 150, 255), label.c_str());
            }
        }
    }

    // Crop keyframes
    auto keyframes = g_videoPlayer->GetCropKeyframeTimes();
    for (double kt : keyframes)
    {
        float kx = timeToX(kt);
        dl->AddTriangleFilled(
            ImVec2(kx, barY), ImVec2(kx - 5, barY - 8), ImVec2(kx + 5, barY - 8),
            IM_COL32(240, 200, 50, 255));
    }

    bool blockTimelineInputThisFrame = false;

    // Keyframe move mode - track mouse and move keyframe without seeking playhead
    // Handle this BEFORE the invisible button and block normal timeline interactions
    if (g_keyframeMoveMode)
    {
        blockTimelineInputThisFrame = true;

        // Show instruction
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        dl->AddText(ImVec2(mousePos.x + 10, mousePos.y - 20), IM_COL32(255, 255, 100, 255),
            "Click timeline to place keyframe");

        // Draw the moving keyframe at mouse position
        float kx = mousePos.x;
        if (kx >= pos.x && kx <= pos.x + barW)
        {
            dl->AddTriangleFilled(
                ImVec2(kx, barY), ImVec2(kx - 5, barY - 8), ImVec2(kx + 5, barY - 8),
                IM_COL32(255, 220, 100, 200)); // Semi-transparent preview
        }

        // Check for mouse position over timeline area
        ImVec2 mousePos2 = ImGui::GetIO().MousePos;
        bool mouseOverTimeline = (mousePos2.x >= pos.x && mousePos2.x <= pos.x + barW &&
                                  mousePos2.y >= pos.y && mousePos2.y <= pos.y + height);

        // Left-click to place keyframe at new position
        if (mouseOverTimeline && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            float mouseX = ImGui::GetIO().MousePos.x;
            double targetTime = xToTime(mouseX);
            targetTime = std::clamp(targetTime, 0.0, duration);
            
            // Clamp to last frame
            double frameTime = g_videoPlayer->frameRate > 0 ? (1.0 / g_videoPlayer->frameRate) : 0.033;
            if (targetTime >= duration - frameTime)
                targetTime = duration - frameTime;
            if (targetTime < 0.0)
                targetTime = 0.0;

            if (g_videoPlayer->MoveCropKeyframe(g_keyframeMovingTime, targetTime))
            {
                g_videoPlayer->UpdateCropForTime(g_videoPlayer->GetCurrentTime());
            }

            g_keyframeMoveMode = false;
            g_keyframeMovingTime = -1.0;
            g_timelineDragging = false;
            g_blockTimelineUntilMouseRelease = true;
        }

        // Escape or right-click to cancel
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            g_keyframeMoveMode = false;
            g_keyframeMovingTime = -1.0;
        }
    }

    // Interaction - invisible button over timeline
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("timeline_area", ImVec2(barW, height));

    // Right-click on keyframe markers for context menu
    if (!g_keyframeMoveMode && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        float mouseX = ImGui::GetIO().MousePos.x;
        for (double kt : keyframes)
        {
            float kx = timeToX(kt);
            if (std::fabs(mouseX - kx) <= 8.0f) // 8px hit area
            {
                g_keyframeContextMenuTime = kt;
                g_keyframeContextMenuOpen = true;
                ImGui::OpenPopup("KeyframeContextMenu");
                break;
            }
        }
    }

    // Keyframe context menu popup
    if (ImGui::BeginPopup("KeyframeContextMenu"))
    {
        if (ImGui::MenuItem("Edit Keyframe"))
        {
            g_videoPlayer->SeekToTimeExact(g_keyframeContextMenuTime);
            g_keyframeContextMenuOpen = false;
        }
        if (ImGui::MenuItem("Delete Keyframe"))
        {
            if (g_videoPlayer->RemoveCropKeyframe(g_keyframeContextMenuTime))
                g_videoPlayer->UpdateCropForTime(g_videoPlayer->GetCurrentTime());
            g_keyframeContextMenuOpen = false;
        }
        if (ImGui::MenuItem("Move Keyframe"))
        {
            g_keyframeMoveMode = true;
            g_keyframeMovingTime = g_keyframeContextMenuTime;
            g_timelineDragging = false;
            g_keyframeContextMenuOpen = false;
        }
        ImGui::EndPopup();
    }

    // Start marker
    if (g_cutStartTime >= 0)
    {
        float sx = timeToX(g_cutStartTime);
        dl->AddRectFilled(ImVec2(sx - 2, barY), ImVec2(sx + 2, barY + barH),
            IM_COL32(80, 200, 80, 255));
        dl->AddTriangleFilled(
            ImVec2(sx, barY + barH), ImVec2(sx - 6, barY + barH + 8), ImVec2(sx + 6, barY + barH + 8),
            IM_COL32(80, 200, 80, 255));
    }

    // End marker
    if (g_cutEndTime >= 0)
    {
        float ex = timeToX(g_cutEndTime);
        dl->AddRectFilled(ImVec2(ex - 2, barY), ImVec2(ex + 2, barY + barH),
            IM_COL32(200, 80, 80, 255));
        dl->AddTriangleFilled(
            ImVec2(ex, barY + barH), ImVec2(ex - 6, barY + barH + 8), ImVec2(ex + 6, barY + barH + 8),
            IM_COL32(200, 80, 80, 255));
    }

    // Playhead cursor
    float cx = timeToX(currentTime);
    dl->AddLine(ImVec2(cx, pos.y), ImVec2(cx, barY + barH), IM_COL32(255, 255, 255, 220), 2.0f);
    dl->AddTriangleFilled(
        ImVec2(cx, pos.y + 14), ImVec2(cx - 6, pos.y + 6), ImVec2(cx + 6, pos.y + 6),
        IM_COL32(255, 255, 255, 240));

    // Current time display
    {
        std::string timeLabel = FormatTimeA(currentTime, true) + " / " + FormatTimeA(duration);
        ImVec2 textSize = ImGui::CalcTextSize(timeLabel.c_str());
        dl->AddText(ImVec2(pos.x + barW - textSize.x - 8, pos.y + 2),
            IM_COL32(200, 200, 210, 255), timeLabel.c_str());
    }

    // Timeline scrubbing interaction
    if (g_blockTimelineUntilMouseRelease && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        g_blockTimelineUntilMouseRelease = false;

    if (!blockTimelineInputThisFrame && !g_blockTimelineUntilMouseRelease && ImGui::IsItemHovered())
    {
        // Zoom with scroll wheel
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            double oldZoom = g_timelineZoom;
            float mouseX = ImGui::GetIO().MousePos.x;
            double mouseTime = xToTime(mouseX);

            g_timelineZoom *= (wheel > 0) ? 1.2 : (1.0 / 1.2);
            if (g_timelineZoom < 1.0) g_timelineZoom = 1.0;
            if (g_timelineZoom > 500.0) g_timelineZoom = 500.0;

            // Keep mouse position at same time after zoom
            double newVisibleDuration = duration / g_timelineZoom;
            g_timelineScroll = mouseTime - (mouseX - pos.x) / barW * newVisibleDuration;
            if (g_timelineScroll < 0) g_timelineScroll = 0;
            if (g_timelineScroll + newVisibleDuration > duration)
                g_timelineScroll = duration - newVisibleDuration;
        }
    }

    if (!blockTimelineInputThisFrame && !g_blockTimelineUntilMouseRelease && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        float mouseX = ImGui::GetIO().MousePos.x;
        double seekTime = xToTime(mouseX);
        seekTime = std::clamp(seekTime, 0.0, duration);
        
        if (!g_timelineDragging)
        {
            g_timelineDragging = true;
            if (g_videoPlayer->IsPlaying())
                g_videoPlayer->Pause();
        }
        
        // Immediate UI feedback
        g_previewSeekTime = seekTime;

        // Asynchronous seek to keep UI perfectly smooth
        g_videoPlayer->RequestSeek(seekTime);
    }
    else if (g_timelineDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        g_timelineDragging = false;
        g_previewSeekTime = -1.0;
        
        // Final seek ensures we land exactly where we released
        double mouseX = ImGui::GetIO().MousePos.x;
        double seekTime = xToTime(mouseX);
        g_videoPlayer->SeekToTime(std::clamp(seekTime, 0.0, duration));
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---- Options Window ----
static void DrawOptionsWindow()
{
    if (!g_showOptionsWindow) return;

    ImGui::SetNextWindowSize(ImVec2(550, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Options", &g_showOptionsWindow, ImGuiWindowFlags_NoCollapse))
    {
        if (ImGui::BeginTabBar("OptionsTabs"))
        {
            // ---- General Tab ----
            if (ImGui::BeginTabItem("General"))
            {
                ImGui::Spacing();
                ImGui::Checkbox("Auto-play on load", &g_autoPlay);
                ImGui::Checkbox("Log to file", &g_logToFile);
                ImGui::Spacing();

                if (AccentButton("Save Settings"))
                    SaveSettings();
                ImGui::EndTabItem();
            }

            // ---- Encoding Tab ----
            if (ImGui::BeginTabItem("Encoding"))
            {
                ImGui::Spacing();

                int encoder = static_cast<int>(g_encoderSelection);
                const char* encoderNames[] = { "libx264 (CPU)", "NVENC (NVIDIA GPU)", "AMF (AMD GPU)" };
                if (ImGui::Combo("Encoder", &encoder, encoderNames, IM_ARRAYSIZE(encoderNames)))
                    g_encoderSelection = static_cast<EncoderSelection>(encoder);

                std::string qp = WtoA(g_qualityPreset);
                char qpBuf[64];
                strncpy_s(qpBuf, qp.c_str(), sizeof(qpBuf) - 1);
                const char* presets[] = { "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow" };
                int presetIdx = 5; // medium default
                for (int i = 0; i < IM_ARRAYSIZE(presets); i++)
                    if (qp == presets[i]) presetIdx = i;
                if (ImGui::Combo("Quality Preset", &presetIdx, presets, IM_ARRAYSIZE(presets)))
                    g_qualityPreset = AtoW(presets[presetIdx]);

                ImGui::Spacing();
                if (AccentButton("Save Settings"))
                    SaveSettings();
                ImGui::EndTabItem();
            }

            // ---- Upload Tab ----
            if (ImGui::BeginTabItem("Upload"))
            {
                ImGui::Spacing();
                ImGui::Checkbox("Auto-upload after export", &g_autoUpload);
                ImGui::Spacing();
                ImGui::Separator();

                // Catbox
                SectionHeader("Catbox.moe");
                ImGui::Checkbox("Enable Catbox upload", &g_useCatbox);
                {
                    std::string hash = WtoA(g_catboxUserHash);
                    char buf[256];
                    strncpy_s(buf, hash.c_str(), sizeof(buf) - 1);
                    if (ImGui::InputText("User Hash", buf, sizeof(buf)))
                        g_catboxUserHash = AtoW(buf);
                }

                // B2
                SectionHeader("Backblaze B2");
                ImGui::Checkbox("Enable B2 upload", &g_useB2);
                {
                    auto inputW = [](const char* label, std::wstring& val) {
                        std::string s = WtoA(val);
                        char buf[256];
                        strncpy_s(buf, s.c_str(), sizeof(buf) - 1);
                        if (ImGui::InputText(label, buf, sizeof(buf)))
                            val = AtoW(buf);
                    };
                    inputW("Key ID", g_b2KeyId);
                    inputW("App Key", g_b2AppKey);
                    inputW("Bucket ID", g_b2BucketId);
                    inputW("Bucket Name", g_b2BucketName);
                    inputW("Custom URL", g_b2CustomUrl);
                }

                ImGui::Spacing();
                if (AccentButton("Save Settings"))
                    SaveSettings();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// ---- Progress Popup ----
static void DrawProgressPopup()
{
    if (!g_showProgressPopup) return;

    ImGui::SetNextWindowSize(ImVec2(420, 160), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    if (ImGui::Begin("Processing Video", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::TextWrapped("%s", g_exportStatus.c_str());
        ImGui::Spacing();
        ImGui::ProgressBar(g_exportProgress, ImVec2(-1, 0));

        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", g_exportProgress * 100.0f);
        ImVec2 textSize = ImGui::CalcTextSize(pctBuf);
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - textSize.x) * 0.5f + ImGui::GetCursorPosX());
        ImGui::TextUnformatted(pctBuf);

        ImGui::Spacing();
        float buttonWidth = 120.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);
        if (DangerButton("Cancel", ImVec2(buttonWidth, 0)))
            g_cancelExport = true;

        // Check if export finished
        if (!g_exportRunning && g_showProgressPopup)
        {
            g_showProgressPopup = false;
            g_showResultPopup = true;
        }
    }
    ImGui::End();
}

// ---- Result Popup ----
static void DrawResultPopup()
{
    if (!g_showResultPopup) return;

    ImGui::SetNextWindowSize(ImVec2(450, 200), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    if (ImGui::Begin("Result", &g_showResultPopup,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::TextWrapped("%s", g_resultMessage.c_str());

        // Show upload URLs if available
        if (g_catboxUploadSuccess && !g_catboxUploadedUrl.empty())
        {
            ImGui::Spacing();
            std::string url = WtoA(g_catboxUploadedUrl);
            ImGui::Text("Catbox: %s", url.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##catbox"))
            {
                if (OpenClipboard(nullptr))
                {
                    EmptyClipboard();
                    size_t sz = (url.size() + 1) * sizeof(char);
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                    if (hMem) { memcpy(GlobalLock(hMem), url.c_str(), sz); GlobalUnlock(hMem); SetClipboardData(CF_TEXT, hMem); }
                    CloseClipboard();
                }
            }
        }
        if (g_b2UploadSuccess && !g_b2UploadedUrl.empty())
        {
            std::string url = WtoA(g_b2UploadedUrl);
            ImGui::Text("B2: %s", url.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##b2"))
            {
                if (OpenClipboard(nullptr))
                {
                    EmptyClipboard();
                    size_t sz = (url.size() + 1) * sizeof(char);
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                    if (hMem) { memcpy(GlobalLock(hMem), url.c_str(), sz); GlobalUnlock(hMem); SetClipboardData(CF_TEXT, hMem); }
                    CloseClipboard();
                }
            }
        }

        // Manual upload buttons
        if (!g_autoUpload && !g_lastOutputFile.empty() && (g_useCatbox || g_useB2))
        {
            ImGui::Separator();
            ImGui::Spacing();
            if (g_useCatbox && !g_catboxDone)
            {
                if (g_catboxUploading)
                    ImGui::Text("Uploading to Catbox...");
                else if (GreenButton("Upload to Catbox"))
                {
                    g_catboxUploading = true;
                    std::wstring path = g_lastOutputFile;
                    std::thread([path]() {
                        std::string url;
                        if (UploadToCatbox(path, url, nullptr))
                        {
                            g_catboxResultUrl = url;
                            g_catboxUploadSuccess = true;
                        }
                        g_catboxUploading = false;
                        g_catboxDone = true;
                    }).detach();
                }
            }
            if (g_useB2 && !g_b2Done)
            {
                if (g_b2Uploading)
                    ImGui::Text("Uploading to B2...");
                else if (GreenButton("Upload to B2"))
                {
                    g_b2Uploading = true;
                    std::wstring path = g_lastOutputFile;
                    std::thread([path]() {
                        std::string url;
                        if (UploadToB2(path, url, nullptr))
                        {
                            g_b2ResultUrl = url;
                            g_b2UploadSuccess = true;
                        }
                        g_b2Uploading = false;
                        g_b2Done = true;
                    }).detach();
                }
            }
            if (!g_lastOutputFile.empty())
            {
                if (ImGui::Button("Open Containing Folder"))
                {
                    std::wstring dir = std::filesystem::path(g_lastOutputFile).parent_path().wstring();
                    ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOW);
                }
            }
        }

        ImGui::Spacing();
        float buttonWidth = 80.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);
        if (AccentButton("OK", ImVec2(buttonWidth, 0)))
        {
            g_showResultPopup = false;
            g_catboxDone = false;
            g_b2Done = false;
        }
    }
    ImGui::End();
}

// ---- Export logic ----
static void StartExport(HWND hwnd, bool isCut)
{
    if (!g_videoPlayer) return;

    OPENFILENAMEW ofn = {};
    wchar_t szFile[260] = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"MP4 Video\0*.mp4\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"mp4";
    extern std::wstring g_lastSaveDir;
    if (!g_lastSaveDir.empty())
        ofn.lpstrInitialDir = g_lastSaveDir.c_str();

    if (!GetSaveFileNameW(&ofn))
        return;

    g_lastSaveDir = std::filesystem::path(szFile).parent_path().wstring();
    g_lastOperationWasExport = !isCut;

    bool mergeAudio = g_mergeAudio;
    bool convertH264 = (g_codecMode == 0);
    int bitrate = g_bitrateKbps;
    int targetSize = g_targetSizeMB;
    bool useSize = (g_bitrateMode == 1);

    double startTime = isCut ? g_cutStartTime : 0.0;
    double endTime = isCut ? g_cutEndTime : g_videoPlayer->GetDuration();

    if (convertH264 && useSize && targetSize > 0) {
        double dur = endTime - startTime;
        int audioKbps = 0;
        if (mergeAudio) {
            audioKbps = 128;
        } else {
            for (const auto& track : g_videoPlayer->audioTracks) {
                if (track->isMuted) continue;
                AVCodecParameters* par = g_videoPlayer->formatContext->streams[track->streamIndex]->codecpar;
                int64_t br = par->bit_rate > 0 ? par->bit_rate : 128000;
                audioKbps += static_cast<int>(br / 1000);
            }
        }
        int totalKbps = static_cast<int>((targetSize * 8192) / dur);
        bitrate = totalKbps > audioKbps ? (totalKbps - audioKbps) : totalKbps / 2;
    }

    g_exportRunning = true;
    g_exportProgress = 0.0f;
    g_exportStatus = isCut ? "Cutting video..." : "Exporting video...";
    g_showProgressPopup = true;
    g_cancelExport = false;
    g_uploadSuccess = false;
    g_catboxUploadSuccess = false;
    g_b2UploadSuccess = false;
    g_catboxUploadedUrl.clear();
    g_b2UploadedUrl.clear();
    g_catboxDone = false;
    g_b2Done = false;

    std::wstring outFile = szFile;
    std::thread([outFile, mergeAudio, convertH264, bitrate, startTime, endTime]() {
        // Use a dummy HWND progress bar - we'll poll from the ImGui side
        bool ok = g_videoPlayer->CutVideo(outFile, startTime, endTime,
            mergeAudio, convertH264, g_encoderSelection, g_qualityPreset,
            bitrate, &g_exportProgress, &g_cancelExport);

        g_lastOutputFile = ok ? outFile : L"";

        if (ok && g_autoUpload && (g_useCatbox || g_useB2))
        {
            g_exportStatus = "Uploading...";
            if (g_useCatbox)
            {
                std::string url;
                if (UploadToCatbox(outFile, url, &g_exportProgress))
                {
                    int sz = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
                    g_catboxUploadedUrl.assign(sz - 1, 0);
                    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, g_catboxUploadedUrl.data(), sz);
                    g_catboxUploadSuccess = true;
                }
            }
            if (g_useB2)
            {
                std::string url;
                if (UploadToB2(outFile, url, &g_exportProgress))
                {
                    int sz = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
                    g_b2UploadedUrl.assign(sz - 1, 0);
                    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, g_b2UploadedUrl.data(), sz);
                    g_b2UploadSuccess = true;
                }
            }
            g_uploadSuccess = (!g_useCatbox || g_catboxUploadSuccess) && (!g_useB2 || g_b2UploadSuccess);
        }

        // Build result message
        if (ok)
        {
            g_resultMessage = g_lastOperationWasExport ? "Video exported successfully!" : "Video cut successfully!";
            if (g_autoUpload)
            {
                if (g_catboxUploadSuccess)
                    g_resultMessage += "\nUploaded to Catbox.";
                if (g_b2UploadSuccess)
                    g_resultMessage += "\nUploaded to B2.";
            }
        }
        else if (g_cancelExport)
        {
            g_resultMessage = "Export canceled.";
        }
        else
        {
            g_resultMessage = g_lastOperationWasExport ? "Failed to export video." : "Failed to cut video.";
        }

        g_exportProgress = 1.0f;
        g_exportRunning = false;
    }).detach();
}

// ---- Main UI Rendering ----
void RenderUI(HWND hwnd)
{
    bool isLoaded = g_videoPlayer && g_videoPlayer->IsLoaded();
    bool isPlaying = g_videoPlayer && g_videoPlayer->IsPlaying();

    // Handle keyboard shortcuts (only when no ImGui text input is active)
    if (isLoaded && !ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Space))
        {
            if (isPlaying) g_videoPlayer->Pause();
            else g_videoPlayer->Play();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        {
            double t = std::max(0.0, g_videoPlayer->GetCurrentTime() - 5.0);
            bool wp = g_videoPlayer->IsPlaying();
            if (wp) g_videoPlayer->Pause();
            g_videoPlayer->SeekToTime(t);
            if (wp) g_videoPlayer->Play();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        {
            double t = std::min(g_videoPlayer->GetDuration(), g_videoPlayer->GetCurrentTime() + 5.0);
            bool wp = g_videoPlayer->IsPlaying();
            if (wp) g_videoPlayer->Pause();
            g_videoPlayer->SeekToTime(t);
            if (wp) g_videoPlayer->Play();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Comma))
        {
            if (g_videoPlayer->IsPlaying()) g_videoPlayer->Pause();
            int64_t frame = g_videoPlayer->GetCurrentFrame() - 1;
            if (frame < 0) frame = 0;
            g_videoPlayer->SeekToFrame(frame);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Period))
        {
            if (g_videoPlayer->IsPlaying()) g_videoPlayer->Pause();
            int64_t frame = g_videoPlayer->GetCurrentFrame() + 1;
            int64_t total = g_videoPlayer->GetTotalFrames();
            if (total > 0 && frame >= total) frame = total - 1;
            g_videoPlayer->SeekToFrame(frame);
        }
    }

    // ---- Full-window layout ----
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    ImGui::Begin("##MainWindow", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    // ---- Menu Bar ----
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open Video...", "Ctrl+O"))
                OpenVideoFile(hwnd);
            ImGui::Separator();
            if (ImGui::MenuItem("Export Video...", nullptr, false, isLoaded))
                StartExport(hwnd, false);
            if (ImGui::MenuItem("Cut Video...", nullptr, false, isLoaded && g_cutStartTime >= 0 && g_cutEndTime > g_cutStartTime))
                StartExport(hwnd, true);
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
                PostQuitMessage(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Options"))
                g_showOptionsWindow = true;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // ---- Layout: Main content area ----
    float menuBarHeight = ImGui::GetFrameHeight();
    float timelineHeight = 80.0f;
    float toolbarHeight = 50.0f;
    float statusBarHeight = 28.0f;
    float rightPanelWidth = 300.0f;
    float spacingY = ImGui::GetStyle().ItemSpacing.y;
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    
    // Account for spacing between items (Toolbar, VideoArea, Timeline, StatusBar)
    float videoAreaHeight = contentSize.y - timelineHeight - toolbarHeight - statusBarHeight - (spacingY * 3);

    // ---- Toolbar ----
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::BgPanel);
        ImGui::BeginChild("Toolbar", ImVec2(0, toolbarHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // Open button
        if (ImGui::Button("  Open  ", ImVec2(80, 32)))
            OpenVideoFile(hwnd);

        ImGui::SameLine(0, 16);

        // Playback controls
        ImGui::BeginDisabled(!isLoaded);
        {
            if (isPlaying)
            {
                if (ImGui::Button("  Pause  ", ImVec2(80, 32)))
                    g_videoPlayer->Pause();
            }
            else
            {
                if (GreenButton("  Play  ", ImVec2(80, 32)))
                    g_videoPlayer->Play();
            }
            ImGui::SameLine();
            if (ImGui::Button("  Stop  ", ImVec2(70, 32)))
            {
                g_videoPlayer->Stop();
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine(0, 24);

        // Frame stepping
        ImGui::BeginDisabled(!isLoaded);
        {
            if (ImGui::Button(" < ", ImVec2(32, 32)))
            {
                if (g_videoPlayer->IsPlaying()) g_videoPlayer->Pause();
                int64_t f = g_videoPlayer->GetCurrentFrame() - 1;
                if (f < 0) f = 0;
                g_videoPlayer->SeekToFrame(f);
            }
            ImGui::SameLine();
            if (ImGui::Button(" > ", ImVec2(32, 32)))
            {
                if (g_videoPlayer->IsPlaying()) g_videoPlayer->Pause();
                int64_t f = g_videoPlayer->GetCurrentFrame() + 1;
                int64_t total = g_videoPlayer->GetTotalFrames();
                if (total > 0 && f >= total) f = total - 1;
                g_videoPlayer->SeekToFrame(f);
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 16);

        // Status
        if (isLoaded)
        {
            double ct = g_videoPlayer->GetCurrentTime();
            double dur = g_videoPlayer->GetDuration();
            int64_t cf = g_videoPlayer->GetCurrentFrame();
            int64_t tf = g_videoPlayer->GetTotalFrames();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::TextSecondary);
            ImGui::Text("%s / %s  |  Frame %lld / %lld  |  %s",
                FormatTimeA(ct, true).c_str(), FormatTimeA(dur).c_str(),
                cf, tf, isPlaying ? "Playing" : "Paused");
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::TextSecondary);
            ImGui::Text("No video loaded - Open a file or drag & drop");
            ImGui::PopStyleColor();
        }

        // Options button on the right
        ImGui::SameLine(ImGui::GetWindowWidth() - 100);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6);
        if (ImGui::Button("Options", ImVec2(80, 32)))
            g_showOptionsWindow = true;

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ---- Video + Right Panel ----
    {
        float videoWidth = contentSize.x - rightPanelWidth - 4;

        // Video Preview
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("VideoArea", ImVec2(videoWidth, videoAreaHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();

            if (isLoaded && g_pVideoSRV)
            {
                int srcW = g_videoPlayer->frameWidth;
                int srcH = g_videoPlayer->frameHeight;

                // Handle crop for preview
                float cropL = 0, cropT = 0;
                float cropW = (float)srcW, cropH = (float)srcH;
                if (g_videoPlayer->hasCrop)
                {
                    cropL = (float)g_videoPlayer->cropRect.left;
                    cropT = (float)g_videoPlayer->cropRect.top;
                    cropW = (float)(g_videoPlayer->cropRect.right - g_videoPlayer->cropRect.left);
                    cropH = (float)(g_videoPlayer->cropRect.bottom - g_videoPlayer->cropRect.top);
                }

                float vidAspect = cropW / cropH;
                float dispW, dispH;
                float panelAspect = avail.x / avail.y;
                if (panelAspect > vidAspect) {
                    dispH = avail.y;
                    dispW = dispH * vidAspect;
                } else {
                    dispW = avail.x;
                    dispH = dispW / vidAspect;
                }

                float offX = (avail.x - dispW) * 0.5f;
                float offY = (avail.y - dispH) * 0.5f;
                ImGui::SetCursorPos(ImVec2(offX, offY));

                // UV coords for crop
                ImVec2 uv0(cropL / srcW, cropT / srcH);
                ImVec2 uv1((cropL + cropW) / srcW, (cropT + cropH) / srcH);

                ImGui::Image((ImTextureID)g_pVideoSRV, ImVec2(dispW, dispH), uv0, uv1);

                // Remember image rect in screen space for crop interaction
                ImVec2 imgMin = ImGui::GetItemRectMin();
                ImVec2 imgMax = ImGui::GetItemRectMax();
                bool imgHovered = ImGui::IsItemHovered();

                // ---- Left-click drag: crop selection ----
                if (imgHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    g_cropSelecting = true;
                    g_cropSelStart = ImGui::GetMousePos();
                    g_cropSelCurrent = g_cropSelStart;
                }
                if (g_cropSelecting)
                {
                    g_cropSelCurrent = ImGui::GetMousePos();

                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    {
                        g_cropSelecting = false;

                        // Compute selection rect clamped to image bounds
                        float sx1 = std::clamp(std::min(g_cropSelStart.x, g_cropSelCurrent.x), imgMin.x, imgMax.x);
                        float sy1 = std::clamp(std::min(g_cropSelStart.y, g_cropSelCurrent.y), imgMin.y, imgMax.y);
                        float sx2 = std::clamp(std::max(g_cropSelStart.x, g_cropSelCurrent.x), imgMin.x, imgMax.x);
                        float sy2 = std::clamp(std::max(g_cropSelStart.y, g_cropSelCurrent.y), imgMin.y, imgMax.y);

                        float selW = sx2 - sx1;
                        float selH = sy2 - sy1;

                        // Only create keyframe if the selection is large enough (> 4px each axis)
                        if (selW > 4.0f && selH > 4.0f)
                        {
                            // Convert screen selection to coordinates relative to the displayed image
                            float relX1 = (sx1 - imgMin.x) / dispW;
                            float relY1 = (sy1 - imgMin.y) / dispH;
                            float relX2 = (sx2 - imgMin.x) / dispW;
                            float relY2 = (sy2 - imgMin.y) / dispH;

                            // Map to the current crop region (base) in video pixel coords
                            RECT base;
                            if (g_videoPlayer->hasCrop)
                                base = g_videoPlayer->cropRect;
                            else
                            {
                                base.left = 0;
                                base.top = 0;
                                base.right = g_videoPlayer->frameWidth;
                                base.bottom = g_videoPlayer->frameHeight;
                            }
                            float baseW = (float)(base.right - base.left);
                            float baseH = (float)(base.bottom - base.top);

                            if (baseW > 0.0f && baseH > 0.0f)
                            {
                                RECT newRect;
                                newRect.left   = base.left + (LONG)(relX1 * baseW);
                                newRect.top    = base.top  + (LONG)(relY1 * baseH);
                                newRect.right  = base.left + (LONG)(relX2 * baseW);
                                newRect.bottom = base.top  + (LONG)(relY2 * baseH);

                                if (newRect.right > newRect.left && newRect.bottom > newRect.top)
                                {
                                    double selectionTime = g_videoPlayer->GetCurrentTime();
                                    if (g_videoPlayer->duration > 0.0)
                                        selectionTime = std::clamp(selectionTime, 0.0, g_videoPlayer->duration);
                                    double appliedTime = selectionTime;
                                    bool inserted = g_videoPlayer->AddCropKeyframe(selectionTime, newRect, &appliedTime);
                                    g_videoPlayer->UpdateCropForTime(inserted ? appliedTime : selectionTime);
                                }
                            }
                        }
                    }
                }

                // Draw selection rectangle overlay while dragging
                if (g_cropSelecting)
                {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 rMin(
                        std::clamp(std::min(g_cropSelStart.x, g_cropSelCurrent.x), imgMin.x, imgMax.x),
                        std::clamp(std::min(g_cropSelStart.y, g_cropSelCurrent.y), imgMin.y, imgMax.y));
                    ImVec2 rMax(
                        std::clamp(std::max(g_cropSelStart.x, g_cropSelCurrent.x), imgMin.x, imgMax.x),
                        std::clamp(std::max(g_cropSelStart.y, g_cropSelCurrent.y), imgMin.y, imgMax.y));

                    // Dim the area outside the selection
                    ImU32 dimColor = IM_COL32(0, 0, 0, 120);
                    // Top strip
                    dl->AddRectFilled(imgMin, ImVec2(imgMax.x, rMin.y), dimColor);
                    // Bottom strip
                    dl->AddRectFilled(ImVec2(imgMin.x, rMax.y), imgMax, dimColor);
                    // Left strip  
                    dl->AddRectFilled(ImVec2(imgMin.x, rMin.y), ImVec2(rMin.x, rMax.y), dimColor);
                    // Right strip
                    dl->AddRectFilled(ImVec2(rMax.x, rMin.y), ImVec2(imgMax.x, rMax.y), dimColor);

                    // Selection border
                    dl->AddRect(rMin, rMax, IM_COL32(255, 220, 50, 255), 0.0f, 0, 2.0f);
                    // Corner handles
                    float hs = 4.0f;
                    ImU32 handleCol = IM_COL32(255, 220, 50, 255);
                    dl->AddRectFilled(ImVec2(rMin.x - hs, rMin.y - hs), ImVec2(rMin.x + hs, rMin.y + hs), handleCol);
                    dl->AddRectFilled(ImVec2(rMax.x - hs, rMin.y - hs), ImVec2(rMax.x + hs, rMin.y + hs), handleCol);
                    dl->AddRectFilled(ImVec2(rMin.x - hs, rMax.y - hs), ImVec2(rMin.x + hs, rMax.y + hs), handleCol);
                    dl->AddRectFilled(ImVec2(rMax.x - hs, rMax.y - hs), ImVec2(rMax.x + hs, rMax.y + hs), handleCol);
                }

                // ---- Right-click: hierarchical crop undo ----
                if (imgHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    double currentTime = g_videoPlayer->GetCurrentTime();
                    double clampedTime = currentTime;
                    if (g_videoPlayer->duration > 0.0)
                        clampedTime = std::clamp(clampedTime, 0.0, g_videoPlayer->duration);

                    auto keyframes = g_videoPlayer->GetCropKeyframes();
                    bool hasCurrent = false;
                    int currentKeyframeIndex = -1;

                    for (int i = (int)keyframes.size() - 1; i >= 0; --i)
                    {
                        if (keyframes[i].time <= clampedTime)
                        {
                            hasCurrent = true;
                            currentKeyframeIndex = i;
                            break;
                        }
                    }

                    if (hasCurrent)
                    {
                        bool foundPreviousEnabled = false;
                        if (currentKeyframeIndex >= 0 &&
                            std::fabs(keyframes[currentKeyframeIndex].time - clampedTime) < 0.001)
                        {
                            if (currentKeyframeIndex > 0)
                            {
                                for (int i = currentKeyframeIndex - 1; i >= 0; --i)
                                {
                                    if (keyframes[i].enabled)
                                    {
                                        g_videoPlayer->RemoveCropKeyframe(keyframes[currentKeyframeIndex].time);
                                        foundPreviousEnabled = true;
                                        break;
                                    }
                                }
                            }
                            if (!foundPreviousEnabled)
                            {
                                g_videoPlayer->RemoveCropKeyframe(keyframes[currentKeyframeIndex].time);
                                foundPreviousEnabled = true;
                            }
                        }
                        if (!foundPreviousEnabled)
                        {
                            double appliedTime = clampedTime;
                            g_videoPlayer->AddCropDisabledKeyframe(clampedTime, &appliedTime);
                        }
                    }
                    g_videoPlayer->UpdateCropForTime(clampedTime);
                }
            }
            else
            {
                // Empty state
                ImVec2 center = ImVec2(avail.x * 0.5f, avail.y * 0.5f);
                ImGui::SetCursorPos(ImVec2(center.x - 80, center.y - 20));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::TextSecondary);
                ImGui::Text("Drop a video file here\nor click Open");
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine();

        // ---- Right Panel ----
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::BgPanel);
        ImGui::BeginChild("RightPanel", ImVec2(rightPanelWidth, videoAreaHeight),
            ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
        {
            // ---- Audio Section ----
            SectionHeader("Audio");
            ImGui::BeginDisabled(!isLoaded);
            {
                int trackCount = isLoaded ? g_videoPlayer->GetAudioTrackCount() : 0;
                if (trackCount > 0)
                {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
                    if (ImGui::BeginListBox("##AudioTracks", ImVec2(-1, 70)))
                    {
                        for (int i = 0; i < trackCount; i++)
                        {
                            std::string name = g_videoPlayer->GetAudioTrackName(i);
                            if (g_videoPlayer->IsAudioTrackMuted(i)) name += " (MUTED)";
                            if (g_videoPlayer->IsVoiceIsolationEnabled(i)) name += " (VOICE)";
                            
                            bool selected = (g_selectedAudioTrack == i);
                            if (ImGui::Selectable(name.c_str(), selected))
                            {
                                g_selectedAudioTrack = i;
                                float vol = g_videoPlayer->GetAudioTrackVolume(i);
                                g_trackVolumeDb = (vol > 0.0f) ? 20.0f * log10f(vol) : -30.0f;
                            }
                        }
                        ImGui::EndListBox();
                    }
                    ImGui::PopStyleColor();

                    if (g_selectedAudioTrack >= 0 && g_selectedAudioTrack < trackCount)
                    {
                        ImGui::Spacing();
                        bool muted = g_videoPlayer->IsAudioTrackMuted(g_selectedAudioTrack);
                        if (ImGui::Button(muted ? "Unmute" : "Mute", ImVec2(80, 0)))
                            g_videoPlayer->SetAudioTrackMuted(g_selectedAudioTrack, !muted);

                        ImGui::SameLine();
                        bool vi = g_videoPlayer->IsVoiceIsolationEnabled(g_selectedAudioTrack);
                        if (ImGui::Checkbox("Voice Isolation", &vi))
                            g_videoPlayer->SetVoiceIsolationEnabled(g_selectedAudioTrack, vi);

                        // Track volume
                        ImGui::Text("Track Volume: %.1f dB", g_trackVolumeDb);
                        if (ImGui::SliderFloat("##TrackVol", &g_trackVolumeDb, -30.0f, 30.0f, "%.1f dB"))
                        {
                            float vol = powf(10.0f, g_trackVolumeDb / 20.0f);
                            g_videoPlayer->SetAudioTrackVolume(g_selectedAudioTrack, vol);
                        }
                    }
                }
                else
                {
                    ImGui::TextColored(Colors::TextSecondary, "No audio tracks");
                }

                // Master volume
                ImGui::Spacing();
                ImGui::Text("Master Volume: %.1f dB", g_masterVolumeDb);
                if (ImGui::SliderFloat("##MasterVol", &g_masterVolumeDb, -30.0f, 30.0f, "%.1f dB"))
                {
                    float vol = powf(10.0f, g_masterVolumeDb / 20.0f);
                    if (g_videoPlayer) g_videoPlayer->SetMasterVolume(vol);
                }
            }
            ImGui::EndDisabled();

            // ---- Editing Section ----
            SectionHeader("Editing");
            ImGui::BeginDisabled(!isLoaded);
            {
                float halfW = (ImGui::GetContentRegionAvail().x - 8) * 0.5f;

                // Set Start / End buttons
                if (AccentButton("Set Start", ImVec2(halfW, 0)))
                {
                    g_cutStartTime = g_videoPlayer->GetCurrentTime();
                    if (g_cutEndTime >= 0 && g_cutStartTime >= g_cutEndTime)
                        g_cutEndTime = -1.0;
                    snprintf(g_startTimeStr, sizeof(g_startTimeStr), "%s", FormatTimeA(g_cutStartTime, true).c_str());
                }
                ImGui::SameLine();
                if (AccentButton("Set End", ImVec2(halfW, 0)))
                {
                    double ct = g_videoPlayer->GetCurrentTime();
                    if (g_cutStartTime < 0 || ct > g_cutStartTime)
                    {
                        g_cutEndTime = ct;
                        snprintf(g_endTimeStr, sizeof(g_endTimeStr), "%s", FormatTimeA(g_cutEndTime, true).c_str());
                    }
                }

                // Time inputs
                ImGui::SetNextItemWidth(halfW);
                if (ImGui::InputText("##StartTime", g_startTimeStr, sizeof(g_startTimeStr),
                    ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    double t = ParseTimeString(AtoW(g_startTimeStr));
                    if (t >= 0) { g_cutStartTime = t; if (g_cutEndTime >= 0 && g_cutStartTime >= g_cutEndTime) g_cutEndTime = -1.0; }
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(halfW);
                if (ImGui::InputText("##EndTime", g_endTimeStr, sizeof(g_endTimeStr),
                    ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    double t = ParseTimeString(AtoW(g_endTimeStr));
                    if (t >= 0 && (g_cutStartTime < 0 || t > g_cutStartTime)) g_cutEndTime = t;
                }

                // Preview buttons
                bool canPreview = g_cutStartTime >= 0 && g_cutEndTime > g_cutStartTime;
                ImGui::BeginDisabled(!canPreview);
                if (ImGui::Button("Preview Clip", ImVec2(halfW, 0)))
                    g_videoPlayer->PlayClip(g_cutStartTime, g_cutEndTime);
                ImGui::SameLine();
                if (ImGui::Button("Preview End", ImVec2(halfW, 0)))
                {
                    double ps = std::max(g_cutStartTime, g_cutEndTime - 3.0);
                    g_videoPlayer->PlayClip(ps, g_cutEndTime);
                }
                ImGui::EndDisabled();

                // Cut info
                ImGui::Spacing();
                if (g_cutStartTime >= 0 && g_cutEndTime > g_cutStartTime)
                {
                    double cutDur = g_cutEndTime - g_cutStartTime;
                    ImGui::TextColored(Colors::Green, "Cut: %s - %s (%.2fs)",
                        FormatTimeA(g_cutStartTime, true).c_str(),
                        FormatTimeA(g_cutEndTime, true).c_str(), cutDur);
                }
                else if (g_cutStartTime >= 0)
                {
                    ImGui::TextColored(Colors::Orange, "Start: %s | End: not set",
                        FormatTimeA(g_cutStartTime, true).c_str());
                }
                else
                {
                    ImGui::TextColored(Colors::TextSecondary, "No cut points set");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Export settings
                bool anyCrop = g_videoPlayer && g_videoPlayer->HasAnyCrop();
                ImGui::Checkbox("Merge Audio Tracks", &g_mergeAudio);

                if (anyCrop) g_codecMode = 0; // Force H264 when crop is set
                ImGui::BeginDisabled(anyCrop);
                ImGui::RadioButton("Convert H264", &g_codecMode, 0);
                ImGui::SameLine();
                ImGui::RadioButton("Copy Codec", &g_codecMode, 1);
                ImGui::EndDisabled();

                if (g_codecMode == 0) // H264
                {
                    ImGui::RadioButton("Bitrate", &g_bitrateMode, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("Target Size", &g_bitrateMode, 1);

                    if (g_bitrateMode == 0)
                    {
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputInt("##Bitrate", &g_bitrateKbps);
                        ImGui::TextColored(Colors::TextSecondary, "Bitrate (kbps)");
                    }
                    else
                    {
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputInt("##TargetSize", &g_targetSizeMB);
                        ImGui::TextColored(Colors::TextSecondary, "Target size (MB)");
                    }
                }

                ImGui::Spacing();

                // Export / Cut button
                float fullW = ImGui::GetContentRegionAvail().x;
                bool hasCut = g_cutStartTime >= 0 && g_cutEndTime > g_cutStartTime;
                if (hasCut)
                {
                    if (GreenButton("Cut Video", ImVec2(fullW, 34)))
                        StartExport(hwnd, true);
                }
                else
                {
                    if (GreenButton("Export Video", ImVec2(fullW, 34)))
                        StartExport(hwnd, false);
                }

                // Clear cut points
                if (hasCut)
                {
                    if (ImGui::SmallButton("Clear cut points"))
                    {
                        g_cutStartTime = -1.0;
                        g_cutEndTime = -1.0;
                        g_startTimeStr[0] = 0;
                        g_endTimeStr[0] = 0;
                    }
                }
            }
            ImGui::EndDisabled();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ---- Timeline ----
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
        float timelineWidth = contentSize.x;
        DrawTimeline(timelineWidth, timelineHeight);
        ImGui::PopStyleVar();
    }

    // ---- Status Bar ----
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::BgPanel);
        ImGui::BeginChild("StatusBar", ImVec2(0, statusBarHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetCursorPosY(4);
        ImGui::SetCursorPosX(8);

        if (isLoaded)
        {
            std::wstring fn = g_videoPlayer->loadedFilename;
            std::string filename = WtoA(std::filesystem::path(fn).filename().wstring());
            ImGui::TextColored(Colors::TextSecondary, "%s  |  %.1f fps  |  %dx%d",
                filename.c_str(),
                g_videoPlayer->frameRate,
                g_videoPlayer->frameWidth, g_videoPlayer->frameHeight);
        }
        else
        {
            ImGui::TextColored(Colors::TextSecondary, "Ready");
        }

        // Zoom indicator
        if (isLoaded && g_timelineZoom > 1.0)
        {
            ImGui::SameLine(ImGui::GetWindowWidth() - 120);
            ImGui::TextColored(Colors::TextSecondary, "Zoom: %.1fx", g_timelineZoom);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::End(); // MainWindow

    // ---- Floating windows ----
    DrawOptionsWindow();
    DrawProgressPopup();
    DrawResultPopup();
}
