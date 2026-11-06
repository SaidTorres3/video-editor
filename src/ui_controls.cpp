#include "ui_controls.h"
#include "video_player.h"
#include "utils.h"
#include "options_window.h"
#include "timeline.h"
#include "ui_updates.h"
#include "theme.h"
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <algorithm>
#include <vector>

// Forward declarations
void ApplyDarkTheme(HWND hwnd);

// Control IDs
#define ID_BUTTON_OPEN 1001
#define ID_BUTTON_PLAY 1002
#define ID_BUTTON_PAUSE 1003
#define ID_BUTTON_STOP 1004
#define ID_TIMELINE 1005
#define ID_TIMELINE_RESIZE_BAR 1036
#define ID_LISTBOX_AUDIO_TRACKS 1007
#define ID_BUTTON_MUTE_TRACK 1008
#define ID_SLIDER_TRACK_VOLUME 1009
#define ID_SLIDER_MASTER_VOLUME 1010
#define ID_BUTTON_SET_START 1011
#define ID_BUTTON_SET_END 1012
#define ID_BUTTON_CUT 1013
#define ID_CHECKBOX_MERGE_AUDIO 1014
#define ID_RADIO_COPY_CODEC 1015
#define ID_RADIO_H264 1016
#define ID_EDIT_BITRATE 1017
#define ID_EDIT_START_TIME 1018
#define ID_EDIT_END_TIME 1019
#define ID_BUTTON_OPTIONS 1020
#define ID_LABEL_BITRATE 1021
#define ID_EDIT_TARGETSIZE 1022
#define ID_LABEL_TARGETSIZE 1023
#define ID_RADIO_USE_BITRATE 1024
#define ID_RADIO_USE_SIZE 1025
#define ID_CONTEXT_VOICE_ISOLATION 1026
#define ID_BUTTON_PLAY_CLIP 1027
#define ID_BUTTON_PLAY_END 1028
#define ID_BUTTON_TOGGLE_PANEL 1029
#define ID_BUTTON_ADD_CLIP 1030
#define ID_BUTTON_CLEAR_CLIPS 1031
#define ID_LISTBOX_CUT_SEGMENTS 1032
#define ID_BUTTON_UPDATE_CLIP 1033
#define ID_BUTTON_REMOVE_CLIP 1034
#define ID_BUTTON_PLAY_ALL_CLIPS 1035
#define ID_BUTTON_SPEED_DOWN 1040
#define ID_BUTTON_SPEED_UP 1041
#define ID_EDIT_PLAYBACK_SPEED 1042

// Global variables
extern VideoPlayer *g_videoPlayer;
extern HWND g_hButtonOpen, g_hButtonPlay, g_hButtonPause, g_hButtonStop;
extern HWND g_hTimeline;
extern HWND g_hTimelineResizeBar;
extern HWND g_hStatusText;
extern HWND g_hListBoxAudioTracks, g_hButtonMuteTrack;
extern HWND g_hSliderTrackVolume, g_hSliderMasterVolume;
extern HWND g_hLabelAudioTracks, g_hLabelTrackVolume, g_hLabelMasterVolume, g_hLabelEditing;
extern HWND g_hButtonSetStart, g_hButtonSetEnd, g_hButtonCut, g_hCheckboxMergeAudio;
extern HWND g_hRadioCopyCodec, g_hRadioH264, g_hEditBitrate, g_hEditTargetSize;
extern HWND g_hRadioUseBitrate, g_hRadioUseSize;
extern HWND g_hLabelBitrate, g_hLabelTargetSize;
extern HWND g_hEditStartTime, g_hEditEndTime, g_hButtonPlayClip, g_hButtonPlayEnd;
extern HWND g_hLabelCutInfo;
extern HWND g_hButtonOptions;
extern HWND g_hButtonTogglePanel;
extern HWND g_hButtonAddClip, g_hButtonClearClips;
extern HWND g_hListBoxCutSegments, g_hButtonUpdateClip, g_hButtonRemoveClip, g_hButtonPlayAllClips;
extern HWND g_hButtonSpeedDown, g_hButtonSpeedUp;
extern HWND g_hEditPlaybackSpeed;
extern bool g_isPanelVisible;

static LRESULT CALLBACK VolumeSliderSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                                  LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    const auto notifyVolumeChanged = [hwnd](bool snapToZero)
    {
        if (hwnd == g_hSliderTrackVolume)
            OnTrackVolumeChanged(snapToZero);
        else if (hwnd == g_hSliderMasterVolume)
            OnMasterVolumeChanged(snapToZero);
    };

    if (message == WM_MOUSEWHEEL)
    {
        const int wheelSteps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        if (wheelSteps != 0)
        {
            const int minimum = static_cast<int>(SendMessage(hwnd, TBM_GETRANGEMIN, 0, 0));
            const int maximum = static_cast<int>(SendMessage(hwnd, TBM_GETRANGEMAX, 0, 0));
            const int current = static_cast<int>(SendMessage(hwnd, TBM_GETPOS, 0, 0));
            const int newPosition = std::clamp(current + wheelSteps, minimum, maximum);

            if (newPosition != current)
            {
                SendMessage(hwnd, TBM_SETPOS, TRUE, newPosition);
                // Wheel input uses exact 0.1 dB steps, including when leaving 0.0 dB.
                notifyVolumeChanged(false);
            }
        }
        return 0;
    }

    if (message == WM_LBUTTONDOWN)
    {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT thumbRect{};
        SendMessage(hwnd, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumbRect));

        // Let the native control handle dragging when the thumb itself is clicked.
        if (!PtInRect(&thumbRect, point))
        {
            RECT channelRect{};
            SendMessage(hwnd, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channelRect));

            const int minimum = static_cast<int>(SendMessage(hwnd, TBM_GETRANGEMIN, 0, 0));
            const int maximum = static_cast<int>(SendMessage(hwnd, TBM_GETRANGEMAX, 0, 0));
            const int channelWidth = channelRect.right - channelRect.left;
            if (channelWidth > 0)
            {
                const int clickedPosition = minimum +
                    MulDiv(point.x - channelRect.left, maximum - minimum, channelWidth);
                const int newPosition = std::clamp(clickedPosition, minimum, maximum);

                SetFocus(hwnd);
                SendMessage(hwnd, TBM_SETPOS, TRUE, newPosition);
                notifyVolumeChanged(true);
                return 0;
            }
        }
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}

namespace
{
constexpr int kToolbarHeight = 56;
constexpr int kStatusHeight = 30;
constexpr int kSidebarWidth = 300;
constexpr int kPadding = 12;

struct MainLayout
{
    RECT toolbar{};
    RECT sidebar{};
    bool panelVisible = false;
    int sidebarX = 0;
    int sidebarInnerWidth = 0;
    std::vector<int> dividers;
    int footerTop = 0;
    RECT masterIcon{};
};

MainLayout g_layout;
HWND g_hLabelExport = nullptr;

HINSTANCE InstanceOf(HWND hwnd)
{
    return reinterpret_cast<HINSTANCE>(GetWindowLongPtr(hwnd, GWLP_HINSTANCE));
}

HWND MakeButton(HWND parent, const wchar_t* text, int id, DWORD extraStyle,
                ui::ButtonStyle style, wchar_t glyph, COLORREF background, bool iconOnly = false)
{
    HWND button = CreateWindowW(L"BUTTON", text, WS_VISIBLE | WS_CHILD | extraStyle,
                                0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                InstanceOf(parent), nullptr);
    SetWindowTheme(button, L"DarkMode_Explorer", nullptr);
    ui::StyleButton(button, style, glyph, background, iconOnly);
    return button;
}

HWND MakeEdit(HWND parent, const wchar_t* text, int id, DWORD extraStyle)
{
    HWND edit = CreateWindowW(L"EDIT", text, WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | extraStyle,
                              0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                              InstanceOf(parent), nullptr);
    SetWindowTheme(edit, L"DarkMode_Explorer", nullptr);
    ui::SetFont(edit, ui::FontRole::Body);
    SendMessage(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    return edit;
}

HWND MakeListBox(HWND parent, int id, ui::ListKind kind, const wchar_t* emptyText)
{
    HWND list = CreateWindowW(L"LISTBOX", nullptr,
                              WS_VISIBLE | WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED |
                                  LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                              0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                              InstanceOf(parent), nullptr);
    SetWindowTheme(list, L"DarkMode_Explorer", nullptr);
    ui::StyleListBox(list, kind, emptyText);
    return list;
}

HWND MakeVolumeSlider(HWND parent, int id, const wchar_t* name)
{
    HWND slider = CreateWindowW(TRACKBAR_CLASS, name,
                                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | TBS_FIXEDLENGTH,
                                0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                InstanceOf(parent), nullptr);
    SendMessage(slider, TBM_SETRANGE, TRUE, MAKELONG(0, 1001)); // 0=mute, 1..1001 => -50dB..+50dB
    SendMessage(slider, TBM_SETPOS, TRUE, 501);                 // Default 0dB
    SetWindowSubclass(slider, VolumeSliderSubclassProc, 1, 0);
    ui::StyleSlider(slider, 501);
    return slider;
}

void AddTooltip(HWND tooltip, HWND owner, HWND control, const wchar_t* text)
{
    TOOLINFOW info{};
    info.cbSize = sizeof(info);
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = owner;
    info.uId = reinterpret_cast<UINT_PTR>(control);
    info.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}

void Place(HWND hwnd, int x, int y, int w, int h)
{
    // Skip the bit copy: moved controls would otherwise keep stale pixels from
    // what was drawn at their previous position.
    SetWindowPos(hwnd, nullptr, x, y, (std::max)(0, w), (std::max)(0, h),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}
} // namespace

void CreateControls(HWND hwnd)
{
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    using ui::ButtonStyle;
    namespace glyph = ui::glyph;
    const COLORREF bar = ui::kSurface;
    const COLORREF field = ui::kField;

    // Toolbar
    g_hButtonOpen = MakeButton(hwnd, L"Open", ID_BUTTON_OPEN, BS_PUSHBUTTON, ButtonStyle::Default, glyph::Open, bar);
    g_hButtonStop = MakeButton(hwnd, L"Stop", ID_BUTTON_STOP, BS_PUSHBUTTON, ButtonStyle::Default, glyph::Stop, bar, true);
    g_hButtonPlay = MakeButton(hwnd, L"Play", ID_BUTTON_PLAY, BS_PUSHBUTTON, ButtonStyle::Primary, glyph::Play, bar, true);
    g_hButtonPause = MakeButton(hwnd, L"Pause", ID_BUTTON_PAUSE, BS_PUSHBUTTON, ButtonStyle::Primary, glyph::Pause, bar, true);
    ShowWindow(g_hButtonPause, SW_HIDE);
    g_hButtonSpeedDown = MakeButton(hwnd, L"-", ID_BUTTON_SPEED_DOWN, BS_PUSHBUTTON, ButtonStyle::Ghost, glyph::Minus, field, true);
    g_hButtonSpeedUp = MakeButton(hwnd, L"+", ID_BUTTON_SPEED_UP, BS_PUSHBUTTON, ButtonStyle::Ghost, glyph::Plus, field, true);
    g_hEditPlaybackSpeed = MakeEdit(hwnd, L"1x", ID_EDIT_PLAYBACK_SPEED, ES_CENTER);
    ui::SetFont(g_hEditPlaybackSpeed, ui::FontRole::Semibold);
    g_hButtonOptions = MakeButton(hwnd, L"Options", ID_BUTTON_OPTIONS, BS_PUSHBUTTON, ButtonStyle::Ghost, glyph::Settings, bar);
    g_hButtonTogglePanel = MakeButton(hwnd, L"Panel", ID_BUTTON_TOGGLE_PANEL, BS_PUSHBUTTON, ButtonStyle::Ghost, glyph::ClosePane, bar, true);

    // Dedicated resize bar above the timeline. Keeping this separate prevents
    // resize gestures from colliding with waveform and marker interactions.
    g_hTimelineResizeBar = CreateWindow(
        L"TimelineResizeBarClass", nullptr, WS_CHILD | WS_VISIBLE,
        10, 360, 600, 10, hwnd, (HMENU)ID_TIMELINE_RESIZE_BAR, InstanceOf(hwnd), nullptr);

    g_hTimeline = CreateWindow(
        L"TimelineClass", nullptr, WS_CHILD | WS_VISIBLE,
        10, 370, 600, 30, hwnd, (HMENU)ID_TIMELINE, InstanceOf(hwnd), nullptr);
    ApplyDarkTheme(g_hTimeline);

    g_hStatusText = ui::CreateStatusBar(hwnd);

    // Audio section
    g_hLabelAudioTracks = ui::CreateLabel(hwnd, L"Audio", ui::LabelKind::Header, bar);
    g_hListBoxAudioTracks = MakeListBox(hwnd, ID_LISTBOX_AUDIO_TRACKS, ui::ListKind::AudioTracks, L"No audio tracks");
    g_hLabelTrackVolume = ui::CreateLabel(hwnd, L"Track Volume: 0.0 dB", ui::LabelKind::ValueSplit, bar);
    g_hButtonMuteTrack = MakeButton(hwnd, L"Mute", ID_BUTTON_MUTE_TRACK, BS_PUSHBUTTON, ButtonStyle::Default, glyph::Volume, bar, true);
    ui::SetButtonAlternate(g_hButtonMuteTrack, L"Unmute", glyph::Mute, true);
    g_hSliderTrackVolume = MakeVolumeSlider(hwnd, ID_SLIDER_TRACK_VOLUME, L"Track Volume");
    g_hLabelMasterVolume = ui::CreateLabel(hwnd, L"Master Volume: 0.0 dB", ui::LabelKind::ValueSplit, bar);
    g_hSliderMasterVolume = MakeVolumeSlider(hwnd, ID_SLIDER_MASTER_VOLUME, L"Master Volume");

    // Trim section
    g_hLabelEditing = ui::CreateLabel(hwnd, L"Trim", ui::LabelKind::Header, bar);
    g_hLabelCutInfo = ui::CreateLabel(hwnd, L"Cut points not set.", ui::LabelKind::Meta, bar);
    g_hButtonSetStart = MakeButton(hwnd, L"Set start", ID_BUTTON_SET_START, BS_PUSHBUTTON, ButtonStyle::Default, 0, bar);
    g_hButtonSetEnd = MakeButton(hwnd, L"Set end", ID_BUTTON_SET_END, BS_PUSHBUTTON, ButtonStyle::Default, 0, bar);
    g_hEditStartTime = MakeEdit(hwnd, L"", ID_EDIT_START_TIME, 0);
    g_hEditEndTime = MakeEdit(hwnd, L"", ID_EDIT_END_TIME, 0);
    g_hButtonPlayClip = MakeButton(hwnd, L"Play from start", ID_BUTTON_PLAY_CLIP, BS_PUSHBUTTON, ButtonStyle::Ghost, glyph::Play, field, true);
    g_hButtonPlayEnd = MakeButton(hwnd, L"Play end", ID_BUTTON_PLAY_END, BS_PUSHBUTTON, ButtonStyle::Ghost, glyph::Play, field, true);
    g_hListBoxCutSegments = MakeListBox(hwnd, ID_LISTBOX_CUT_SEGMENTS, ui::ListKind::Clips, L"Set start and end, then Add");
    g_hButtonAddClip = MakeButton(hwnd, L"Add", ID_BUTTON_ADD_CLIP, BS_PUSHBUTTON, ButtonStyle::Default, 0, bar);
    g_hButtonUpdateClip = MakeButton(hwnd, L"Update", ID_BUTTON_UPDATE_CLIP, BS_PUSHBUTTON, ButtonStyle::Default, 0, bar);
    g_hButtonRemoveClip = MakeButton(hwnd, L"Remove", ID_BUTTON_REMOVE_CLIP, BS_PUSHBUTTON, ButtonStyle::Default, 0, bar);
    g_hButtonClearClips = MakeButton(hwnd, L"Clear", ID_BUTTON_CLEAR_CLIPS, BS_PUSHBUTTON, ButtonStyle::Default, 0, bar);
    g_hButtonPlayAllClips = MakeButton(hwnd, L"Play all", ID_BUTTON_PLAY_ALL_CLIPS, BS_PUSHBUTTON, ButtonStyle::Default, glyph::Play, bar);

    // Export section
    g_hLabelExport = ui::CreateLabel(hwnd, L"Export", ui::LabelKind::Header, bar);
    g_hCheckboxMergeAudio = MakeButton(hwnd, L"Merge audio tracks", ID_CHECKBOX_MERGE_AUDIO, BS_AUTOCHECKBOX, ButtonStyle::Check, 0, bar);
    SendMessage(g_hCheckboxMergeAudio, BM_SETCHECK, g_mergeAudioByDefault ? BST_CHECKED : BST_UNCHECKED, 0);
    g_hRadioH264 = MakeButton(hwnd, L"Encode H.264", ID_RADIO_H264, BS_AUTORADIOBUTTON | WS_GROUP, ButtonStyle::Segment, 0, field);
    SendMessage(g_hRadioH264, BM_SETCHECK, BST_CHECKED, 0);
    g_hRadioCopyCodec = MakeButton(hwnd, L"Copy codec", ID_RADIO_COPY_CODEC, BS_AUTORADIOBUTTON, ButtonStyle::Segment, 0, field);
    g_hRadioUseBitrate = MakeButton(hwnd, L"Bitrate", ID_RADIO_USE_BITRATE, BS_AUTORADIOBUTTON | WS_GROUP, ButtonStyle::Segment, 0, field);
    SendMessage(g_hRadioUseBitrate, BM_SETCHECK, BST_CHECKED, 0);
    g_hRadioUseSize = MakeButton(hwnd, L"Size", ID_RADIO_USE_SIZE, BS_AUTORADIOBUTTON, ButtonStyle::Segment, 0, field);
    g_hEditBitrate = MakeEdit(hwnd, L"0", ID_EDIT_BITRATE, ES_NUMBER);
    g_hLabelBitrate = ui::CreateLabel(hwnd, L"kbps", ui::LabelKind::Unit, field);
    g_hEditTargetSize = MakeEdit(hwnd, L"0", ID_EDIT_TARGETSIZE, ES_NUMBER);
    g_hLabelTargetSize = ui::CreateLabel(hwnd, L"MB", ui::LabelKind::Unit, field);
    g_hButtonCut = MakeButton(hwnd, L"Export Video", ID_BUTTON_CUT, BS_PUSHBUTTON, ButtonStyle::Primary, glyph::Save, bar);
    ui::SetButtonAlternate(g_hButtonCut, L"Cut Video", glyph::Cut, false);

    // Tooltips for icon-only buttons
    HWND tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                   WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                   hwnd, nullptr, InstanceOf(hwnd), nullptr);
    if (tooltip)
    {
        SetWindowTheme(tooltip, L"DarkMode_Explorer", nullptr);
        AddTooltip(tooltip, hwnd, g_hButtonOpen, L"Open a video file");
        AddTooltip(tooltip, hwnd, g_hButtonPlay, L"Play");
        AddTooltip(tooltip, hwnd, g_hButtonPause, L"Pause");
        AddTooltip(tooltip, hwnd, g_hButtonStop, L"Stop");
        AddTooltip(tooltip, hwnd, g_hButtonSpeedDown, L"Slower");
        AddTooltip(tooltip, hwnd, g_hButtonSpeedUp, L"Faster");
        AddTooltip(tooltip, hwnd, g_hEditPlaybackSpeed, L"Playback speed");
        AddTooltip(tooltip, hwnd, g_hButtonTogglePanel, L"Show or hide the side panel");
        AddTooltip(tooltip, hwnd, g_hButtonMuteTrack, L"Mute or unmute the selected track");
        AddTooltip(tooltip, hwnd, g_hButtonPlayClip, L"Play from start point");
        AddTooltip(tooltip, hwnd, g_hButtonPlayEnd, L"Play the last seconds before the end point");
    }

    // Disable controls until video is loaded
    EnableWindow(g_hButtonPlay, FALSE);
    EnableWindow(g_hButtonPause, FALSE);
    EnableWindow(g_hButtonStop, FALSE);
    EnableWindow(g_hButtonSpeedDown, FALSE);
    EnableWindow(g_hButtonSpeedUp, FALSE);
    EnableWindow(g_hEditPlaybackSpeed, FALSE);
    EnableWindow(g_hTimeline, FALSE);
    EnableWindow(g_hListBoxAudioTracks, FALSE);
    EnableWindow(g_hButtonMuteTrack, FALSE);
    EnableWindow(g_hSliderTrackVolume, FALSE);
    EnableWindow(g_hSliderMasterVolume, FALSE);
    EnableWindow(g_hButtonSetStart, FALSE);
    EnableWindow(g_hButtonSetEnd, FALSE);
    EnableWindow(g_hEditStartTime, FALSE);
    EnableWindow(g_hEditEndTime, FALSE);
    EnableWindow(g_hButtonPlayClip, FALSE);
    EnableWindow(g_hButtonPlayEnd, FALSE);
    EnableWindow(g_hButtonAddClip, FALSE);
    EnableWindow(g_hButtonClearClips, FALSE);
    EnableWindow(g_hListBoxCutSegments, FALSE);
    EnableWindow(g_hButtonUpdateClip, FALSE);
    EnableWindow(g_hButtonRemoveClip, FALSE);
    EnableWindow(g_hButtonPlayAllClips, FALSE);
    EnableWindow(g_hButtonCut, FALSE);
    EnableWindow(g_hCheckboxMergeAudio, FALSE);
    EnableWindow(g_hRadioCopyCodec, FALSE);
    EnableWindow(g_hRadioH264, FALSE);
    EnableWindow(g_hRadioUseBitrate, FALSE);
    EnableWindow(g_hRadioUseSize, FALSE);
    EnableWindow(g_hEditBitrate, FALSE);
    EnableWindow(g_hLabelTargetSize, FALSE);
    EnableWindow(g_hEditTargetSize, FALSE);
    ShowWindow(g_hRadioUseBitrate, SW_HIDE);
    ShowWindow(g_hRadioUseSize, SW_HIDE);
    ShowWindow(g_hLabelBitrate, SW_HIDE);
    ShowWindow(g_hEditBitrate, SW_HIDE);
    ShowWindow(g_hLabelTargetSize, SW_HIDE);
    ShowWindow(g_hEditTargetSize, SW_HIDE);
}

void RepositionControls(HWND hwnd)
{
    if (!g_videoPlayer) return;

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    const int width = clientRect.right;
    const int height = clientRect.bottom;
    const int statusTop = height - kStatusHeight;
    const int sidebarLeft = g_isPanelVisible ? width - kSidebarWidth : width;

    g_layout.toolbar = { 0, 0, width, kToolbarHeight };
    g_layout.sidebar = { sidebarLeft, kToolbarHeight, width, statusTop };
    g_layout.panelVisible = g_isPanelVisible;
    g_layout.dividers.clear();

    // ---- Toolbar ----
    const int buttonY = 10;
    const int buttonHeight = 36;
    const int openWidth = 92;
    Place(g_hButtonOpen, kPadding, buttonY, openWidth, buttonHeight);

    const int optionsWidth = 108;
    const int toggleWidth = 36;
    const int optionsX = width - kPadding - optionsWidth;
    const int toggleX = optionsX - 4 - toggleWidth;
    Place(g_hButtonOptions, optionsX, buttonY, optionsWidth, buttonHeight);
    Place(g_hButtonTogglePanel, toggleX, buttonY, toggleWidth, buttonHeight);
    ui::SetButtonGlyph(g_hButtonTogglePanel,
                       g_isPanelVisible ? ui::glyph::ClosePane : ui::glyph::OpenPane);

    // Transport centered over the video, followed by the speed control.
    const int stopWidth = 36;
    const int playWidth = 44;
    const int speedWidth = 124;
    const int transportGap = 6;
    const int groupWidth = stopWidth + transportGap + playWidth + 16 + speedWidth;
    const int minimumGroupX = kPadding + openWidth + 16;
    const int maximumGroupX = toggleX - 16 - groupWidth;
    int groupX = sidebarLeft / 2 - (stopWidth + transportGap + playWidth / 2);
    groupX = (std::min)(groupX, maximumGroupX);
    groupX = (std::max)(groupX, minimumGroupX);
    Place(g_hButtonStop, groupX, buttonY, stopWidth, buttonHeight);
    Place(g_hButtonPlay, groupX + stopWidth + transportGap, buttonY, playWidth, buttonHeight);
    Place(g_hButtonPause, groupX + stopWidth + transportGap, buttonY, playWidth, buttonHeight);

    const int speedX = groupX + stopWidth + transportGap + playWidth + 16;
    ui::SetFieldRect(g_hEditPlaybackSpeed, RECT{ speedX, buttonY, speedX + speedWidth, buttonY + buttonHeight });
    Place(g_hButtonSpeedDown, speedX + 4, buttonY + 4, 30, buttonHeight - 8);
    Place(g_hEditPlaybackSpeed, speedX + 38, buttonY + (buttonHeight - 20) / 2, speedWidth - 76, 20);
    Place(g_hButtonSpeedUp, speedX + speedWidth - 34, buttonY + 4, 30, buttonHeight - 8);

    // ---- Side panel ----
    const int panelCmdShow = g_isPanelVisible ? SW_SHOW : SW_HIDE;
    const int multiClipCmdShow = (g_isPanelVisible && g_enableMultiClipEditing) ? SW_SHOW : SW_HIDE;
    for (HWND control : { g_hLabelAudioTracks, g_hListBoxAudioTracks, g_hButtonMuteTrack,
                          g_hLabelTrackVolume, g_hSliderTrackVolume, g_hLabelMasterVolume,
                          g_hSliderMasterVolume, g_hLabelEditing, g_hButtonSetStart,
                          g_hButtonSetEnd, g_hEditStartTime, g_hEditEndTime, g_hButtonPlayClip,
                          g_hButtonPlayEnd, g_hLabelCutInfo, g_hButtonCut, g_hCheckboxMergeAudio,
                          g_hRadioH264, g_hRadioCopyCodec, g_hLabelExport })
        ShowWindow(control, panelCmdShow);
    for (HWND control : { g_hButtonAddClip, g_hButtonClearClips, g_hListBoxCutSegments,
                          g_hButtonUpdateClip, g_hButtonRemoveClip, g_hButtonPlayAllClips })
        ShowWindow(control, multiClipCmdShow);

    if (g_isPanelVisible)
    {
        const int x0 = sidebarLeft + 18;
        const int innerWidth = kSidebarWidth - 36;
        g_layout.sidebarX = x0;
        g_layout.sidebarInnerWidth = innerWidth;

        const int exportHeight = 38;
        const int pinnedExportY = statusTop - 12 - exportHeight;

        // Lays out the panel top to bottom. The clip list absorbs spare height,
        // so this runs once to measure and once to place.
        auto layoutPanel = [&](int clipListHeight, bool apply) -> int {
            auto place = [apply](HWND control, int x, int y, int w, int h) {
                if (apply)
                    Place(control, x, y, w, h);
            };
            auto setField = [apply](HWND key, RECT box) {
                if (apply)
                    ui::SetFieldRect(key, box);
            };
            auto divider = [&](int& y) {
                y += 10;
                if (apply)
                    g_layout.dividers.push_back(y);
                y += 11;
            };

            int y = kToolbarHeight + 16;

            // Audio
            place(g_hLabelAudioTracks, x0, y, innerWidth, 16);
            y += 24;
            setField(g_hListBoxAudioTracks, RECT{ x0, y, x0 + innerWidth, y + 64 });
            place(g_hListBoxAudioTracks, x0 + 2, y + 2, innerWidth - 4, 60);
            y += 64 + 12;
            place(g_hLabelTrackVolume, x0, y, innerWidth, 18);
            y += 22;
            place(g_hButtonMuteTrack, x0, y, 32, 28);
            place(g_hSliderTrackVolume, x0 + 38, y, innerWidth - 38, 28);
            y += 28 + 8;
            place(g_hLabelMasterVolume, x0, y, innerWidth, 18);
            y += 22;
            if (apply)
                g_layout.masterIcon = RECT{ x0, y, x0 + 32, y + 28 };
            place(g_hSliderMasterVolume, x0 + 38, y, innerWidth - 38, 28);
            y += 28;
            divider(y);

            // Trim
            place(g_hLabelEditing, x0, y, 80, 16);
            if (apply)
                ui::SetLabelKind(g_hLabelCutInfo, g_enableMultiClipEditing ? ui::LabelKind::Caption
                                                                           : ui::LabelKind::Meta);
            if (!g_enableMultiClipEditing)
                place(g_hLabelCutInfo, x0 + 60, y, innerWidth - 60, 16);
            y += 24;
            const int half = (innerWidth - 8) / 2;
            const int rightX = x0 + innerWidth - half;
            place(g_hButtonSetStart, x0, y, half, 30);
            place(g_hButtonSetEnd, rightX, y, half, 30);
            y += 38;
            setField(g_hEditStartTime, RECT{ x0, y, x0 + half, y + 30 });
            place(g_hEditStartTime, x0 + 10, y + 5, half - 40, 20);
            place(g_hButtonPlayClip, x0 + half - 29, y + 3, 26, 24);
            setField(g_hEditEndTime, RECT{ rightX, y, rightX + half, y + 30 });
            place(g_hEditEndTime, rightX + 10, y + 5, half - 40, 20);
            place(g_hButtonPlayEnd, rightX + half - 29, y + 3, 26, 24);
            y += 30;

            if (g_enableMultiClipEditing)
            {
                y += 8;
                const int gap = 5;
                const int each = (innerWidth - gap * 3) / 4;
                int x = x0;
                HWND clipButtons[] = { g_hButtonAddClip, g_hButtonUpdateClip,
                                       g_hButtonRemoveClip, g_hButtonClearClips };
                for (HWND button : clipButtons)
                {
                    const int w = (button == g_hButtonClearClips) ? x0 + innerWidth - x : each;
                    place(button, x, y, w, 30);
                    x += each + gap;
                }
                y += 38;
                setField(g_hListBoxCutSegments, RECT{ x0, y, x0 + innerWidth, y + clipListHeight });
                place(g_hListBoxCutSegments, x0 + 2, y + 2, innerWidth - 4, clipListHeight - 4);
                y += clipListHeight + 8;
                const int playAllWidth = 100;
                place(g_hLabelCutInfo, x0, y, innerWidth - playAllWidth - 8, 28);
                place(g_hButtonPlayAllClips, x0 + innerWidth - playAllWidth, y, playAllWidth, 28);
                y += 28;
            }
            divider(y);

            // Export
            place(g_hLabelExport, x0, y, innerWidth, 16);
            y += 24;
            place(g_hCheckboxMergeAudio, x0, y, innerWidth, 22);
            y += 30;
            const int segmentInner = innerWidth - 6;
            setField(g_hRadioH264, RECT{ x0, y, x0 + innerWidth, y + 34 });
            place(g_hRadioH264, x0 + 3, y + 3, segmentInner / 2, 28);
            place(g_hRadioCopyCodec, x0 + 3 + segmentInner / 2, y + 3, segmentInner - segmentInner / 2, 28);
            y += 34;

            // Rate controls only take space when re-encoding.
            if (SendMessage(g_hRadioH264, BM_GETCHECK, 0, 0) != BST_CHECKED)
                return y;
            y += 8;
            const int rateWidth = 140;
            setField(g_hRadioUseBitrate, RECT{ x0, y, x0 + rateWidth, y + 34 });
            place(g_hRadioUseBitrate, x0 + 3, y + 3, (rateWidth - 6) / 2, 28);
            place(g_hRadioUseSize, x0 + 3 + (rateWidth - 6) / 2, y + 3, (rateWidth - 6) - (rateWidth - 6) / 2, 28);
            const int valueX = x0 + rateWidth + 8;
            const int valueWidth = innerWidth - rateWidth - 8;
            const RECT valueBox{ valueX, y, valueX + valueWidth, y + 34 };
            setField(g_hEditBitrate, valueBox);
            setField(g_hEditTargetSize, valueBox);
            place(g_hEditBitrate, valueX + 10, y + 7, valueWidth - 56, 20);
            place(g_hLabelBitrate, valueX + valueWidth - 46, y + 1, 36, 32);
            place(g_hEditTargetSize, valueX + 10, y + 7, valueWidth - 56, 20);
            place(g_hLabelTargetSize, valueX + valueWidth - 46, y + 1, 36, 32);
            y += 34;
            return y;
        };

        const int preferredClipList = 84;
        const int measuredEnd = layoutPanel(preferredClipList, false);
        const int spare = pinnedExportY - 24 - measuredEnd;
        const int clipListHeight = std::clamp(preferredClipList + spare, 40, 200);
        const int flowEnd = layoutPanel(clipListHeight, true);

        const int exportY = (std::max)(pinnedExportY, flowEnd + 24);
        g_layout.footerTop = exportY - 12;
        Place(g_hButtonCut, x0, exportY, innerWidth, exportHeight);
    }

    RepositionTimelineArea(hwnd);

    // Redraw all controls after a regular window/panel layout. Timeline-only
    // drag updates use RepositionTimelineArea directly and avoid this full erase.
    InvalidateRect(hwnd, NULL, TRUE);
}

void RepositionTimelineArea(HWND hwnd)
{
    if (!g_videoPlayer) return;

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    const int contentRight = g_isPanelVisible
        ? static_cast<int>(clientRect.right) - kSidebarWidth
        : static_cast<int>(clientRect.right);
    const int videoSectionWidth = (std::max)(0, contentRight - kPadding * 2);
    const int videoTop = kToolbarHeight + kPadding;
    const int statusY = clientRect.bottom - kStatusHeight;
    const int timelineBottom = statusY - kPadding;
    const int resizeBarHeight = 12;
    const int minimumVideoHeight = 100;
    const int maximumTimelineHeight = (std::max)(
        30, timelineBottom - videoTop - resizeBarHeight - minimumVideoHeight);
    const int timelineHeight = (std::min)(
        GetPreferredTimelineHeight(), maximumTimelineHeight);
    const int timelineY = timelineBottom - timelineHeight;
    const int resizeBarY = timelineY - resizeBarHeight;

    g_videoPlayer->SetPosition(
        kPadding,
        videoTop,
        videoSectionWidth,
        (std::max)(0, resizeBarY - videoTop)
    );

    MoveWindow(g_hTimelineResizeBar, kPadding, resizeBarY, videoSectionWidth, resizeBarHeight, TRUE);
    MoveWindow(g_hTimeline, kPadding, timelineY, videoSectionWidth, timelineHeight, TRUE);
    MoveWindow(g_hStatusText, 0, statusY, clientRect.right, kStatusHeight, TRUE);
    UpdateWindow(g_hTimelineResizeBar);
    UpdateWindow(g_hTimeline);
}

void PaintMainWindow(HWND hwnd, HDC hdc)
{
    RECT client;
    GetClientRect(hwnd, &client);
    ui::FillSolid(hdc, client, ui::kBgApp);

    // Toolbar
    ui::FillSolid(hdc, g_layout.toolbar, ui::kSurface);
    RECT toolbarLine{ 0, g_layout.toolbar.bottom - 1, client.right, g_layout.toolbar.bottom };
    ui::FillSolid(hdc, toolbarLine, ui::kBorder);

    if (g_layout.panelVisible)
    {
        ui::FillSolid(hdc, g_layout.sidebar, ui::kSurface);
        RECT edge{ g_layout.sidebar.left, g_layout.sidebar.top, g_layout.sidebar.left + 1, g_layout.sidebar.bottom };
        ui::FillSolid(hdc, edge, ui::kBorder);
        for (int y : g_layout.dividers)
        {
            RECT line{ g_layout.sidebarX, y, g_layout.sidebarX + g_layout.sidebarInnerWidth, y + 1 };
            ui::FillSolid(hdc, line, ui::kBorder);
        }
        RECT footer{ g_layout.sidebar.left + 1, g_layout.footerTop, g_layout.sidebar.right, g_layout.footerTop + 1 };
        ui::FillSolid(hdc, footer, ui::kBorder);
        ui::DrawGlyph(hdc, ui::glyph::Volume, g_layout.masterIcon,
                      IsWindowEnabled(g_hSliderMasterVolume) ? ui::kTextMuted : ui::kTextDisabled);
    }

    ui::PaintFields(hdc);
}
