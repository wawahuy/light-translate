#pragma once

// Application icon
#define IDI_APP_ICON          101

// === Settings window control IDs ===
#define IDC_STATIC_API        1000
#define IDC_API_URL_EDIT      1001
#define IDC_API_KEY_EDIT      1002

#define IDC_STATIC_CAPTURE    1010
#define IDC_MONITOR_COMBO     1011
#define IDC_SELECT_REGION     1012
#define IDC_REGION_INFO       1013
#define IDC_FPM_COMBO         1014
#define IDC_CAPTURE_MODE_COMBO 1015  ///< Auto / Hotkey capture mode
#define IDC_INTERVAL_EDIT     1016   ///< Interval in ms (Auto mode)
#define IDC_HOTKEY_EDIT       1017   ///< Hotkey display (Hotkey mode)
#define IDC_PAUSE_HOTKEY_EDIT 1018   ///< Pause hotkey (Auto mode)
#define IDC_TOGGLE_WND_HOTKEY_EDIT 1019   ///< Toggle Settings window hotkey

#define IDC_STATIC_OVERLAY    1020
#define IDC_OVERLAY_POS_LABEL 1021   ///< Read-only "X: …  Y: …" label
#define IDC_FONT_NAME_EDIT    1023
#define IDC_FONT_SIZE_EDIT    1024
#define IDC_TEXT_COLOR_BTN    1025
#define IDC_SHADOW_CHECK      1026
#define IDC_SHADOW_COLOR_BTN  1027
#define IDC_STROKE_CHECK      1028
#define IDC_STROKE_COLOR_BTN  1029

#define IDC_START_BTN         1030
#define IDC_STOP_BTN          1031
#define IDC_TEST_API_BTN      1032
#define IDC_TOGGLE_DRAG_BTN   1033
#define IDC_SAVE_BTN          1034
#define IDC_STATUS_EDIT       1035
#define IDC_PROVIDER_COMBO    1036   ///< Translate Provider selection
#define IDC_API_MODEL_EDIT    1037   ///< Translate Model edit
#define IDC_TAB_CTRL          1038   ///< Main tab control
#define IDC_LANGUAGE_COMBO    1039   ///< Target language combo box

// === Tray icon ===
#define WM_TRAYICON           (WM_APP + 100)
#define ID_TRAY_ICON          1
#define ID_TRAY_SHOW          2001
#define ID_TRAY_TOGGLE_DRAG   2002
#define ID_TRAY_EXIT          2003

// === Custom window messages ===
#define WM_OVERLAY_SETTEXT    (WM_APP + 1)   // LPARAM = wchar_t* (heap, caller frees)
#define WM_UPDATE_STATUS      (WM_APP + 2)   // LPARAM = wchar_t* (heap, caller frees)
