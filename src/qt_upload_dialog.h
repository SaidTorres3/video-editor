#pragma once

#include <QDialog>

#include <string>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

class UploadDialog final : public QDialog
{
    Q_OBJECT

public:
    UploadDialog(const std::wstring& exportPath, bool allowCatbox, bool allowB2, bool autoStart, QWidget* parent = nullptr);

private:
    void startCatboxUpload();
    void startB2Upload();

    void setCatboxResult(bool ok, const QString& url);
    void setB2Result(bool ok, const QString& url);

    std::wstring m_exportPath;

    QPushButton* m_catboxUpload = nullptr;
    QProgressBar* m_catboxProgress = nullptr;
    QLineEdit* m_catboxUrl = nullptr;
    QPushButton* m_catboxCopy = nullptr;
    QLabel* m_catboxStatus = nullptr;

    QPushButton* m_b2Upload = nullptr;
    QProgressBar* m_b2Progress = nullptr;
    QLineEdit* m_b2Url = nullptr;
    QPushButton* m_b2Copy = nullptr;
    QLabel* m_b2Status = nullptr;

    QPushButton* m_openFolder = nullptr;
    QPushButton* m_close = nullptr;
};
