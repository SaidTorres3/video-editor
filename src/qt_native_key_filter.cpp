#include "qt_native_key_filter.h"

#include "qt_mainwindow.h"

#include <QApplication>
#include <QMetaObject>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

NativeKeyFilter::NativeKeyFilter(MainWindow* window) : m_window(window) {}

bool NativeKeyFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* /*result*/)
{
    if (!m_window)
        return false;

    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return false;

    MSG* msg = static_cast<MSG*>(message);
    if (!msg)
        return false;

    if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN)
        return false;

    const WPARAM vk = msg->wParam;

    auto invoke = [&](auto fn) {
        QMetaObject::invokeMethod(m_window, fn, Qt::QueuedConnection);
    };

    switch (vk)
    {
    case VK_SPACE:
        invoke([w = m_window]() { if (w) w->shortcutTogglePlayPause(); });
        return true;
    case 'K':
        invoke([w = m_window]() { if (w) w->shortcutTogglePlayPause(); });
        return true;
    case VK_LEFT:
        invoke([w = m_window]() { if (w) w->shortcutSeekBy(-5.0); });
        return true;
    case VK_RIGHT:
        invoke([w = m_window]() { if (w) w->shortcutSeekBy(5.0); });
        return true;
    case 'J':
        invoke([w = m_window]() { if (w) w->shortcutSeekBy(-10.0); });
        return true;
    case 'L':
        invoke([w = m_window]() { if (w) w->shortcutSeekBy(10.0); });
        return true;
    case VK_OEM_COMMA:
        invoke([w = m_window]() { if (w) w->shortcutStepFrame(-1); });
        return true;
    case VK_OEM_PERIOD:
        invoke([w = m_window]() { if (w) w->shortcutStepFrame(1); });
        return true;
    default:
        break;
    }

    return false;
}
