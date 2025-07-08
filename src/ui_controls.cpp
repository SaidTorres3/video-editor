#include "ui_controls.h"
#include "video_player.h"
#include "utils.h"
#include <commctrl.h>

// Forward declarations
void ApplyDarkTheme(HWND hwnd);

// Control IDs
#define ID_BUTTON_OPEN 1001
#define ID_BUTTON_PLAY 1002
#define ID_BUTTON_PAUSE 1003
#define ID_BUTTON_STOP 1004
#define ID_TIMELINE 1005
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

// Global variables
extern VideoPlayer *g_videoPlayer;
extern HWND g_hButtonOpen, g_hButtonPlay, g_hButtonPause, g_hButtonStop;
extern HWND g_hTimeline;
extern HWND g_hStatusText;
extern HWND g_hListBoxAudioTracks, g_hButtonMuteTrack;
extern HWND g_hSliderTrackVolume, g_hSliderMasterVolume;
extern HWND g_hLabelAudioTracks, g_hLabelTrackVolume, g_hLabelMasterVolume, g_hLabelEditing;
extern HWND g_hButtonSetStart, g_hButtonSetEnd, g_hButtonCut, g_hCheckboxMergeAudio;
extern HWND g_hRadioCopyCodec, g_hRadioH264, g_hEditBitrate, g_hEditTargetSize;
extern HWND g_hRadioUseBitrate, g_hRadioUseSize;
extern HWND g_hLabelBitrate, g_hLabelTargetSize;
extern HWND g_hEditStartTime, g_hEditEndTime;
extern HWND g_hLabelCutInfo;
extern HWND g_hButtonOptions;

void CreateControls(HWND hwnd)
{
    InitCommonControls();

    // Open button
    g_hButtonOpen = CreateWindow(
        L"BUTTON", L"Open Video",
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

    g_hButtonOptions = CreateWindow(
        L"BUTTON", L"Options",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        330, 10, 80, 30,
        hwnd, (HMENU)ID_BUTTON_OPTIONS,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonOptions);

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
    SendMessage(g_hSliderTrackVolume, TBM_SETRANGE, TRUE, MAKELONG(0, 200)); // 0-200% volume
    SendMessage(g_hSliderTrackVolume, TBM_SETPOS, TRUE, 100); // Default 100%
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
    SendMessage(g_hSliderMasterVolume, TBM_SETRANGE, TRUE, MAKELONG(0, 200)); // 0-200% volume
    SendMessage(g_hSliderMasterVolume, TBM_SETPOS, TRUE, 100); // Default 100%
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

    g_hRadioCopyCodec = CreateWindow(
        L"BUTTON", L"Copy Codec",
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
        340, 515, 100, 20,
        hwnd, (HMENU)ID_RADIO_COPY_CODEC,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hRadioCopyCodec);
    SendMessage(g_hRadioCopyCodec, BM_SETCHECK, BST_CHECKED, 0);

    g_hRadioH264 = CreateWindow(
        L"BUTTON", L"Convert to H264",
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
        445, 515, 160, 20, // ← Aumenta esto
        hwnd, (HMENU)ID_RADIO_H264,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hRadioH264);

    g_hRadioUseBitrate = CreateWindow(
        L"BUTTON", L"Specify Bitrate",
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
        340, 540, 100, 20,
        hwnd, (HMENU)ID_RADIO_USE_BITRATE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hRadioUseBitrate);
    SendMessage(g_hRadioUseBitrate, BM_SETCHECK, BST_CHECKED, 0);

    g_hRadioUseSize = CreateWindow(
        L"BUTTON", L"Target Size",
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
        445, 540, 100, 20,
        hwnd, (HMENU)ID_RADIO_USE_SIZE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hRadioUseSize);

    g_hLabelBitrate = CreateWindow(
        L"STATIC", L"Bitrate KBPS",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 565, 200, 20,
        hwnd, (HMENU)ID_LABEL_BITRATE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelBitrate);

    g_hEditBitrate = CreateWindow(
        L"EDIT", L"8000",
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
    EnableWindow(g_hTimeline, FALSE);
    EnableWindow(g_hListBoxAudioTracks, FALSE);
    EnableWindow(g_hButtonMuteTrack, FALSE);
    EnableWindow(g_hSliderTrackVolume, FALSE);
    EnableWindow(g_hSliderMasterVolume, FALSE);
    EnableWindow(g_hButtonSetStart, FALSE);
    EnableWindow(g_hButtonSetEnd, FALSE);
    EnableWindow(g_hEditStartTime, FALSE);
    EnableWindow(g_hEditEndTime, FALSE);
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
    MoveWindow(g_hButtonOptions, 330, mainControlsY, 80, mainControlsHeight, TRUE);

    // Audio controls (aligned to the right)
    int audioControlsWidth = 220;
    int audioControlsX = clientRect.right - audioControlsWidth - 10;
    int audioControlsY = 50;

    MoveWindow(g_hLabelAudioTracks, audioControlsX, audioControlsY, 200, 20, TRUE);
    MoveWindow(g_hListBoxAudioTracks, audioControlsX, audioControlsY + 25, 200, 100, TRUE);
    MoveWindow(g_hButtonMuteTrack, audioControlsX, audioControlsY + 135, 100, 25, TRUE);

    MoveWindow(g_hLabelTrackVolume, audioControlsX, audioControlsY + 170, 200, 20, TRUE);
    MoveWindow(g_hSliderTrackVolume, audioControlsX, audioControlsY + 195, 200, 30, TRUE);

    MoveWindow(g_hLabelMasterVolume, audioControlsX, audioControlsY + 235, 200, 20, TRUE);
    MoveWindow(g_hSliderMasterVolume, audioControlsX, audioControlsY + 260, 200, 30, TRUE);

    // Editing controls (below audio)
    int editingControlsY = audioControlsY + 260 + 40;
    MoveWindow(g_hLabelEditing, audioControlsX, editingControlsY, 200, 20, TRUE);
    MoveWindow(g_hButtonSetStart, audioControlsX, editingControlsY + 25, 95, 25, TRUE);
    MoveWindow(g_hButtonSetEnd, audioControlsX + 105, editingControlsY + 25, 95, 25, TRUE);
    MoveWindow(g_hEditStartTime, audioControlsX, editingControlsY + 55, 95, 20, TRUE);
    MoveWindow(g_hEditEndTime, audioControlsX + 105, editingControlsY + 55, 95, 20, TRUE);
    MoveWindow(g_hLabelCutInfo, audioControlsX, editingControlsY + 80, 200, 40, TRUE);
    MoveWindow(g_hButtonCut, audioControlsX, editingControlsY + 125, 200, 30, TRUE);
    MoveWindow(g_hCheckboxMergeAudio, audioControlsX, editingControlsY + 160, 200, 25, TRUE);
    MoveWindow(g_hRadioCopyCodec, audioControlsX, editingControlsY + 190, 100, 20, TRUE);
    MoveWindow(g_hRadioH264, audioControlsX + 105, editingControlsY + 190, 150, 20, TRUE);
    MoveWindow(g_hRadioUseBitrate, audioControlsX, editingControlsY + 215, 100, 20, TRUE);
    MoveWindow(g_hRadioUseSize, audioControlsX + 105, editingControlsY + 215, 100, 20, TRUE);
    MoveWindow(g_hLabelBitrate, audioControlsX, editingControlsY + 240, 200, 20, TRUE);
    MoveWindow(g_hEditBitrate, audioControlsX, editingControlsY + 260, 200, 20, TRUE);
    MoveWindow(g_hLabelTargetSize, audioControlsX, editingControlsY + 240, 200, 20, TRUE);
    MoveWindow(g_hEditTargetSize, audioControlsX, editingControlsY + 260, 200, 20, TRUE);

    // Video area (takes up remaining space)
    int videoSectionWidth = clientRect.right - audioControlsWidth - 30;
    int bottomControlsHeight = 100;

    g_videoPlayer->SetPosition(
        10,
        mainControlsY + mainControlsHeight + 10,
        videoSectionWidth,
        clientRect.bottom - (mainControlsY + mainControlsHeight + 10) - bottomControlsHeight
    );

    // Bottom controls
    int bottomControlsY = clientRect.bottom - 90;
    MoveWindow(g_hTimeline, 10, bottomControlsY, videoSectionWidth, 30, TRUE);
    MoveWindow(g_hStatusText, 10, bottomControlsY + 40, videoSectionWidth, 20, TRUE);

    // Redraw all controls
    InvalidateRect(hwnd, NULL, TRUE);
}
