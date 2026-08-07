#include "ui_controls.h"
#include "video_player.h"
#include "utils.h"
#include "options_window.h"
#include "timeline.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <algorithm>

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

void CreateControls(HWND hwnd)
{
    InitCommonControls();

    // Open button
    g_hButtonOpen = CreateWindow(
        L"BUTTON", L"\U0001F4C2",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        10, 10, 100, 30,
        hwnd, (HMENU)ID_BUTTON_OPEN,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonOpen);

    // Play button
    g_hButtonPlay = CreateWindow(
        L"BUTTON", L"\x25B6",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        120, 10, 60, 30,
        hwnd, (HMENU)ID_BUTTON_PLAY,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonPlay);

    // Pause button
    g_hButtonPause = CreateWindow(
        L"BUTTON", L"\x23F8",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        190, 10, 60, 30,
        hwnd, (HMENU)ID_BUTTON_PAUSE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonPause);

    // Stop button
    g_hButtonStop = CreateWindow(
        L"BUTTON", L"\x23F9",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        260, 10, 60, 30,
        hwnd, (HMENU)ID_BUTTON_STOP,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonStop);

    g_hButtonSpeedDown = CreateWindow(
        L"BUTTON", L"-",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        330, 10, 40, 30,
        hwnd, (HMENU)ID_BUTTON_SPEED_DOWN,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonSpeedDown);

    g_hButtonSpeedUp = CreateWindow(
        L"BUTTON", L"+",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        375, 10, 40, 30,
        hwnd, (HMENU)ID_BUTTON_SPEED_UP,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonSpeedUp);

    g_hEditPlaybackSpeed = CreateWindow(
        L"EDIT", L"1x",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_CENTER | ES_AUTOHSCROLL,
        375, 10, 64, 30,
        hwnd, (HMENU)ID_EDIT_PLAYBACK_SPEED,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hEditPlaybackSpeed);

    g_hButtonOptions = CreateWindow(
        L"BUTTON", L"\u2699 Options",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        330, 10, 80, 30,
        hwnd, (HMENU)ID_BUTTON_OPTIONS,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonOptions);

    g_hButtonTogglePanel = CreateWindow(
        L"BUTTON", L"\u25C4 Panel",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        420, 10, 80, 30,
        hwnd, (HMENU)ID_BUTTON_TOGGLE_PANEL,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonTogglePanel);

    // Dedicated resize bar above the timeline. Keeping this separate prevents
    // resize gestures from colliding with waveform and marker interactions.
    g_hTimelineResizeBar = CreateWindow(
        L"TimelineResizeBarClass", nullptr,
        WS_CHILD | WS_VISIBLE,
        10, 360, 600, 10, // Placeholder position
        hwnd, (HMENU)ID_TIMELINE_RESIZE_BAR,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    // Timeline
    g_hTimeline = CreateWindow(
        L"TimelineClass", nullptr,
        WS_CHILD | WS_VISIBLE,
        10, 370, 600, 30, // Placeholder position
        hwnd, (HMENU)ID_TIMELINE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hTimeline);

    // Status text
    g_hStatusText = CreateWindow(
        L"STATIC", L"No video loaded",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, 410, 600, 20, // Placeholder position
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hStatusText);

    // Audio controls section
    // Audio tracks label
    g_hLabelAudioTracks = CreateWindow(
        L"STATIC", L"Audio Tracks:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 50, 100, 20, // Placeholder position
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelAudioTracks);

    // Audio tracks listbox
    g_hListBoxAudioTracks = CreateWindow(
        L"LISTBOX", nullptr,
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        340, 75, 200, 100, // Placeholder position
        hwnd, (HMENU)ID_LISTBOX_AUDIO_TRACKS,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hListBoxAudioTracks);

    // Mute track button
    g_hButtonMuteTrack = CreateWindow(
        L"BUTTON", L"Mute/Unmute",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        340, 185, 100, 25, // Placeholder position
        hwnd, (HMENU)ID_BUTTON_MUTE_TRACK,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonMuteTrack);

    // Track volume label
    g_hLabelTrackVolume = CreateWindow(
        L"STATIC", L"Track Volume:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 220, 100, 20, // Placeholder position
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelTrackVolume);

    // Track volume slider
    g_hSliderTrackVolume = CreateWindow(
        TRACKBAR_CLASS, L"Track Volume",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_BOTH,
        340, 245, 200, 30, // Placeholder position
        hwnd, (HMENU)ID_SLIDER_TRACK_VOLUME,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    SendMessage(g_hSliderTrackVolume, TBM_SETRANGE, TRUE, MAKELONG(0, 601)); // 0=mute, 1..601 => -30dB..+30dB
    SendMessage(g_hSliderTrackVolume, TBM_SETPOS, TRUE, 301); // Default 0dB
    ApplyDarkTheme(g_hSliderTrackVolume);

    // Master volume label
    g_hLabelMasterVolume = CreateWindow(
        L"STATIC", L"Master Volume:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 285, 100, 20, // Placeholder position
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelMasterVolume);

    // Master volume slider
    g_hSliderMasterVolume = CreateWindow(
        TRACKBAR_CLASS, L"Master Volume",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_BOTH,
        340, 310, 200, 30, // Placeholder position
        hwnd, (HMENU)ID_SLIDER_MASTER_VOLUME,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    SendMessage(g_hSliderMasterVolume, TBM_SETRANGE, TRUE, MAKELONG(0, 601)); // 0=mute, 1..601 => -30dB..+30dB
    SendMessage(g_hSliderMasterVolume, TBM_SETPOS, TRUE, 301); // Default 0dB
    ApplyDarkTheme(g_hSliderMasterVolume);

    // Editing controls section
    g_hLabelEditing = CreateWindow(
        L"STATIC", L"Editing:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 350, 100, 20, // Placeholder
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelEditing);

    g_hButtonSetStart = CreateWindow(
        L"BUTTON", L"Set Start",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        340, 375, 95, 25, // Placeholder
        hwnd, (HMENU)ID_BUTTON_SET_START,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonSetStart);

    g_hButtonSetEnd = CreateWindow(
        L"BUTTON", L"Set End",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        445, 375, 95, 25, // Placeholder
        hwnd, (HMENU)ID_BUTTON_SET_END,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonSetEnd);

    g_hEditStartTime = CreateWindow(
        L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        340, 405, 95, 20,
        hwnd, (HMENU)ID_EDIT_START_TIME,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hEditStartTime);

    g_hEditEndTime = CreateWindow(
        L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        445, 405, 95, 20,
        hwnd, (HMENU)ID_EDIT_END_TIME,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hEditEndTime);

    g_hButtonPlayClip = CreateWindow(
        L"BUTTON", L"\x25B6",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        550, 405, 30, 20,
        hwnd, (HMENU)ID_BUTTON_PLAY_CLIP,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonPlayClip);

    g_hButtonPlayEnd = CreateWindow(
        L"BUTTON", L"\x25B6",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        585, 405, 30, 20,
        hwnd, (HMENU)ID_BUTTON_PLAY_END,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonPlayEnd);

    g_hListBoxCutSegments = CreateWindow(
        L"LISTBOX", nullptr,
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        340, 430, 200, 55,
        hwnd, (HMENU)ID_LISTBOX_CUT_SEGMENTS,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hListBoxCutSegments);

    g_hButtonPlayAllClips = CreateWindow(
        L"BUTTON", L"Play\nAll",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
        545, 430, 45, 55,
        hwnd, (HMENU)ID_BUTTON_PLAY_ALL_CLIPS,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonPlayAllClips);

    g_hButtonAddClip = CreateWindow(
        L"BUTTON", L"Add",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        340, 430, 95, 25,
        hwnd, (HMENU)ID_BUTTON_ADD_CLIP,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonAddClip);

    g_hButtonUpdateClip = CreateWindow(
        L"BUTTON", L"Update",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        400, 490, 60, 25,
        hwnd, (HMENU)ID_BUTTON_UPDATE_CLIP,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonUpdateClip);

    g_hButtonRemoveClip = CreateWindow(
        L"BUTTON", L"Remove",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        465, 490, 60, 25,
        hwnd, (HMENU)ID_BUTTON_REMOVE_CLIP,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonRemoveClip);

    g_hButtonClearClips = CreateWindow(
        L"BUTTON", L"Clear",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        445, 430, 95, 25,
        hwnd, (HMENU)ID_BUTTON_CLEAR_CLIPS,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonClearClips);

    g_hLabelCutInfo = CreateWindow(
        L"STATIC", L"Cut points not set.",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 430, 200, 40, // Placeholder
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelCutInfo);

    g_hButtonCut = CreateWindow(
        L"BUTTON", L"Export Video",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        340, 450, 200, 30, // Placeholder
        hwnd, (HMENU)ID_BUTTON_CUT,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonCut);

   g_hCheckboxMergeAudio = CreateWindow(
       L"BUTTON", L"Merge Audios",
       WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
       340, 485, 200, 25, // Placeholder
       hwnd, (HMENU)ID_CHECKBOX_MERGE_AUDIO,
       (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
   SendMessage(g_hCheckboxMergeAudio, BM_SETCHECK, BST_UNCHECKED, 0);
   ApplyDarkTheme(g_hCheckboxMergeAudio);

    g_hRadioH264 = CreateWindow(
        L"BUTTON", L"Convert to H264",
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
        340, 515, 100, 20,
        hwnd, (HMENU)ID_RADIO_H264,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hRadioH264);
    SetWindowTheme(g_hRadioH264, L"", L"");
    SendMessage(g_hRadioH264, BM_SETCHECK, BST_CHECKED, 0);

    g_hRadioCopyCodec = CreateWindow(
        L"BUTTON", L"Copy Codec",
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
        445, 515, 100, 20,
        hwnd, (HMENU)ID_RADIO_COPY_CODEC,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hRadioCopyCodec);
    SetWindowTheme(g_hRadioCopyCodec, L"", L"");

    g_hRadioUseBitrate = CreateWindow(
        L"BUTTON", L"Specify Bitrate",
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
        340, 540, 100, 20,
        hwnd, (HMENU)ID_RADIO_USE_BITRATE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hRadioUseBitrate);
    SetWindowTheme(g_hRadioUseBitrate, L"", L"");
    SendMessage(g_hRadioUseBitrate, BM_SETCHECK, BST_CHECKED, 0);

    g_hRadioUseSize = CreateWindow(
        L"BUTTON", L"Target Size",
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
        445, 540, 100, 20,
        hwnd, (HMENU)ID_RADIO_USE_SIZE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hRadioUseSize);
    SetWindowTheme(g_hRadioUseSize, L"", L"");

    g_hLabelBitrate = CreateWindow(
        L"STATIC", L"Bitrate KBPS",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 565, 200, 20,
        hwnd, (HMENU)ID_LABEL_BITRATE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelBitrate);

    g_hEditBitrate = CreateWindow(
        L"EDIT", L"0",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
        340, 585, 200, 20,
        hwnd, (HMENU)ID_EDIT_BITRATE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hEditBitrate);

    g_hLabelTargetSize = CreateWindow(
        L"STATIC", L"Target Size MB",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 565, 200, 20,
        hwnd, (HMENU)ID_LABEL_TARGETSIZE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelTargetSize);

    g_hEditTargetSize = CreateWindow(
        L"EDIT", L"0",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
        340, 585, 200, 20,
        hwnd, (HMENU)ID_EDIT_TARGETSIZE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hEditTargetSize);


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

    // Main controls
    int mainControlsY = 10;
    int mainControlsHeight = 30;
    MoveWindow(g_hButtonOpen, 10, mainControlsY, 100, mainControlsHeight, TRUE);
    MoveWindow(g_hButtonPlay, 120, mainControlsY, 60, mainControlsHeight, TRUE);
    MoveWindow(g_hButtonPause, 190, mainControlsY, 60, mainControlsHeight, TRUE);
    MoveWindow(g_hButtonStop, 260, mainControlsY, 60, mainControlsHeight, TRUE);
    int optionsWidth = 80;
    int toggleWidth = 80;
    int optionsX = clientRect.right - optionsWidth - 10;
    int toggleX = optionsX - toggleWidth - 5;
    int speedButtonWidth = 36;
    int speedEditWidth = 64;
    int speedGap = 4;
    int speedGroupWidth = speedButtonWidth * 2 + speedEditWidth + speedGap * 2;
    int speedDownX = toggleX - speedGroupWidth - 8;
    MoveWindow(g_hButtonSpeedDown, speedDownX, mainControlsY, speedButtonWidth, mainControlsHeight, TRUE);
    MoveWindow(g_hEditPlaybackSpeed, speedDownX + speedButtonWidth + speedGap,
               mainControlsY, speedEditWidth, mainControlsHeight, TRUE);
    MoveWindow(g_hButtonSpeedUp, speedDownX + speedButtonWidth + speedGap + speedEditWidth + speedGap,
               mainControlsY, speedButtonWidth, mainControlsHeight, TRUE);
    MoveWindow(g_hButtonOptions, optionsX, mainControlsY, optionsWidth, mainControlsHeight, TRUE);
    MoveWindow(g_hButtonTogglePanel, toggleX, mainControlsY, toggleWidth, mainControlsHeight, TRUE);
    SetWindowTextW(g_hButtonTogglePanel, g_isPanelVisible ? L"\u25BA Panel" : L"\u25C4 Panel");

    // Audio controls (aligned to the right)
    int audioControlsWidth = 250;
    int audioControlsX = clientRect.right - audioControlsWidth - 10;
    int audioControlsY = 50;

    int nCmdShow = g_isPanelVisible ? SW_SHOW : SW_HIDE;
    int multiClipCmdShow = (g_isPanelVisible && g_enableMultiClipEditing) ? SW_SHOW : SW_HIDE;
    ShowWindow(g_hLabelAudioTracks, nCmdShow);
    ShowWindow(g_hListBoxAudioTracks, nCmdShow);
    ShowWindow(g_hButtonMuteTrack, nCmdShow);
    ShowWindow(g_hLabelTrackVolume, nCmdShow);
    ShowWindow(g_hSliderTrackVolume, nCmdShow);
    ShowWindow(g_hLabelMasterVolume, nCmdShow);
    ShowWindow(g_hSliderMasterVolume, nCmdShow);
    ShowWindow(g_hLabelEditing, nCmdShow);
    ShowWindow(g_hButtonSetStart, nCmdShow);
    ShowWindow(g_hButtonSetEnd, nCmdShow);
    ShowWindow(g_hEditStartTime, nCmdShow);
    ShowWindow(g_hEditEndTime, nCmdShow);
    ShowWindow(g_hButtonPlayClip, nCmdShow);
    ShowWindow(g_hButtonPlayEnd, nCmdShow);
    ShowWindow(g_hButtonAddClip, multiClipCmdShow);
    ShowWindow(g_hButtonClearClips, multiClipCmdShow);
    ShowWindow(g_hListBoxCutSegments, multiClipCmdShow);
    ShowWindow(g_hButtonUpdateClip, multiClipCmdShow);
    ShowWindow(g_hButtonRemoveClip, multiClipCmdShow);
    ShowWindow(g_hButtonPlayAllClips, multiClipCmdShow);
    ShowWindow(g_hLabelCutInfo, nCmdShow);
    ShowWindow(g_hButtonCut, nCmdShow);
    ShowWindow(g_hCheckboxMergeAudio, nCmdShow);
    ShowWindow(g_hRadioH264, nCmdShow);
    ShowWindow(g_hRadioCopyCodec, nCmdShow);

    if (g_isPanelVisible)
    {
        MoveWindow(g_hLabelAudioTracks, audioControlsX, audioControlsY, 200, 20, TRUE);
        MoveWindow(g_hListBoxAudioTracks, audioControlsX, audioControlsY + 25, 200, 70, TRUE);
        MoveWindow(g_hButtonMuteTrack, audioControlsX, audioControlsY + 100, 100, 25, TRUE);

        MoveWindow(g_hLabelTrackVolume, audioControlsX, audioControlsY + 135, 200, 20, TRUE);
        MoveWindow(g_hSliderTrackVolume, audioControlsX, audioControlsY + 155, 200, 30, TRUE);

        MoveWindow(g_hLabelMasterVolume, audioControlsX, audioControlsY + 190, 200, 20, TRUE);
        MoveWindow(g_hSliderMasterVolume, audioControlsX, audioControlsY + 210, 200, 30, TRUE);

        // Editing controls (below audio)
        int editingControlsY = audioControlsY + 245;
        MoveWindow(g_hLabelEditing, audioControlsX, editingControlsY, 200, 20, TRUE);
        MoveWindow(g_hButtonSetStart, audioControlsX, editingControlsY + 25, 95, 25, TRUE);
        MoveWindow(g_hButtonSetEnd, audioControlsX + 105, editingControlsY + 25, 95, 25, TRUE);
        MoveWindow(g_hEditStartTime, audioControlsX, editingControlsY + 55, 95, 20, TRUE);
        MoveWindow(g_hEditEndTime, audioControlsX + 105, editingControlsY + 55, 95, 20, TRUE);
        MoveWindow(g_hButtonPlayClip, audioControlsX + 200, editingControlsY + 55, 20, 20, TRUE);
        MoveWindow(g_hButtonPlayEnd, audioControlsX + 225, editingControlsY + 55, 20, 20, TRUE);
        int exportControlsY;
        if (g_enableMultiClipEditing) {
            MoveWindow(g_hListBoxCutSegments, audioControlsX, editingControlsY + 80, 200, 55, TRUE);
            MoveWindow(g_hButtonPlayAllClips, audioControlsX + 205, editingControlsY + 80, 40, 55, TRUE);
            MoveWindow(g_hButtonAddClip, audioControlsX, editingControlsY + 140, 55, 25, TRUE);
            MoveWindow(g_hButtonUpdateClip, audioControlsX + 60, editingControlsY + 140, 60, 25, TRUE);
            MoveWindow(g_hButtonRemoveClip, audioControlsX + 125, editingControlsY + 140, 60, 25, TRUE);
            MoveWindow(g_hButtonClearClips, audioControlsX + 190, editingControlsY + 140, 55, 25, TRUE);
            MoveWindow(g_hLabelCutInfo, audioControlsX, editingControlsY + 170, 240, 25, TRUE);
            exportControlsY = editingControlsY + 195;
        } else {
            MoveWindow(g_hLabelCutInfo, audioControlsX, editingControlsY + 80, 240, 40, TRUE);
            exportControlsY = editingControlsY + 125;
        }
        MoveWindow(g_hButtonCut, audioControlsX, exportControlsY, 200, 30, TRUE);
        MoveWindow(g_hCheckboxMergeAudio, audioControlsX, exportControlsY + 35, 200, 25, TRUE);
        MoveWindow(g_hRadioH264, audioControlsX, exportControlsY + 65, 120, 20, TRUE);
        MoveWindow(g_hRadioCopyCodec, audioControlsX + 120, exportControlsY + 65, 100, 20, TRUE);
        MoveWindow(g_hRadioUseBitrate, audioControlsX, exportControlsY + 90, 100, 20, TRUE);
        MoveWindow(g_hRadioUseSize, audioControlsX + 105, exportControlsY + 90, 100, 20, TRUE);
        MoveWindow(g_hLabelBitrate, audioControlsX, exportControlsY + 115, 200, 20, TRUE);
        MoveWindow(g_hEditBitrate, audioControlsX, exportControlsY + 135, 200, 20, TRUE);
        MoveWindow(g_hLabelTargetSize, audioControlsX, exportControlsY + 115, 200, 20, TRUE);
        MoveWindow(g_hEditTargetSize, audioControlsX, exportControlsY + 135, 200, 20, TRUE);
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

    // Video area (takes up remaining space)
    const int mainControlsY = 10;
    const int mainControlsHeight = 30;
    const int audioControlsWidth = 250;
    int panelReserved = g_isPanelVisible ? (audioControlsWidth + 30) : 10;
    int videoSectionWidth = (std::max)(
        0, static_cast<int>(clientRect.right) - panelReserved - 10);
    const int videoTop = mainControlsY + mainControlsHeight + 10;
    const int statusY = clientRect.bottom - 50;
    const int timelineBottom = statusY - 10;
    const int resizeBarHeight = 10;
    const int minimumVideoHeight = 100;
    const int maximumTimelineHeight = (std::max)(
        30, timelineBottom - videoTop - resizeBarHeight - minimumVideoHeight);
    const int timelineHeight = (std::min)(
        GetPreferredTimelineHeight(), maximumTimelineHeight);
    const int timelineY = timelineBottom - timelineHeight;
    const int resizeBarY = timelineY - resizeBarHeight;

    g_videoPlayer->SetPosition(
        10,
        videoTop,
        videoSectionWidth,
        (std::max)(0, resizeBarY - videoTop)
    );

    MoveWindow(
        g_hTimelineResizeBar,
        10, resizeBarY, videoSectionWidth, resizeBarHeight, TRUE);
    MoveWindow(g_hTimeline, 10, timelineY, videoSectionWidth, timelineHeight, TRUE);
    MoveWindow(g_hStatusText, 10, statusY, videoSectionWidth, 20, TRUE);
    UpdateWindow(g_hTimelineResizeBar);
    UpdateWindow(g_hTimeline);
}
