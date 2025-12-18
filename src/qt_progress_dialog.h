#pragma once

#include <QDialog>

#include <atomic>

class QLabel;
class QProgressBar;
class QPushButton;

class ProgressDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ProgressDialog(std::atomic<bool>* cancelFlag, QWidget* parent = nullptr);

public slots:
    void setStatusText(const QString& text);
    void setProgressValue(int percent);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void requestCancel();

    std::atomic<bool>* m_cancelFlag = nullptr;
    QLabel* m_status = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_cancelButton = nullptr;
};

