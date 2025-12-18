#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QRadioButton;

class OptionsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget* parent = nullptr);

private slots:
    void accept() override;

private:
    QRadioButton* m_encoderLibx264 = nullptr;
    QRadioButton* m_encoderNvenc = nullptr;
    QRadioButton* m_encoderAmf = nullptr;

    QCheckBox* m_logToFile = nullptr;
    QCheckBox* m_autoPlay = nullptr;

    QCheckBox* m_autoUpload = nullptr;
    QCheckBox* m_useCatbox = nullptr;
    QCheckBox* m_useB2 = nullptr;

    QLineEdit* m_catboxHash = nullptr;
    QLineEdit* m_b2KeyId = nullptr;
    QLineEdit* m_b2AppKey = nullptr;
    QLineEdit* m_b2BucketId = nullptr;
    QLineEdit* m_b2BucketName = nullptr;
    QLineEdit* m_b2CustomUrl = nullptr;
};

