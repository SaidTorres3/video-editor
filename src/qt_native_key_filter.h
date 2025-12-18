#pragma once

#include <QAbstractNativeEventFilter>
#include <QPointer>

class MainWindow;

class NativeKeyFilter final : public QAbstractNativeEventFilter
{
public:
    explicit NativeKeyFilter(MainWindow* window);

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    QPointer<MainWindow> m_window;
};

