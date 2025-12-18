#include "qt_options_dialog.h"

#include "app_settings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QRadioButton>
#include <QVBoxLayout>

static QString toQString(const std::wstring& v)
{
    return QString::fromWCharArray(v.c_str());
}

static std::wstring toWString(const QString& v)
{
    auto w = v.toStdWString();
    return w;
}

OptionsDialog::OptionsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Options");
    setModal(true);
    setMinimumWidth(520);

    auto* encoderBox = new QGroupBox("Encoder");
    m_encoderLibx264 = new QRadioButton("CPU (libx264)");
    m_encoderNvenc = new QRadioButton("NVIDIA (NVENC)");
    m_encoderAmf = new QRadioButton("AMD (AMF)");

    auto* encoderLayout = new QVBoxLayout();
    encoderLayout->addWidget(m_encoderLibx264);
    encoderLayout->addWidget(m_encoderNvenc);
    encoderLayout->addWidget(m_encoderAmf);
    encoderBox->setLayout(encoderLayout);

    m_logToFile = new QCheckBox("Enable log file");
    m_autoPlay = new QCheckBox("Auto-play after opening");

    auto* generalBox = new QGroupBox("General");
    auto* generalLayout = new QVBoxLayout();
    generalLayout->addWidget(m_logToFile);
    generalLayout->addWidget(m_autoPlay);
    generalBox->setLayout(generalLayout);

    m_autoUpload = new QCheckBox("Auto-upload after export");
    m_useCatbox = new QCheckBox("Use catbox.moe");
    m_useB2 = new QCheckBox("Use Backblaze B2");

    m_catboxHash = new QLineEdit();
    m_catboxHash->setPlaceholderText("Optional userhash for catbox");

    m_b2KeyId = new QLineEdit();
    m_b2AppKey = new QLineEdit();
    m_b2AppKey->setEchoMode(QLineEdit::Password);
    m_b2BucketId = new QLineEdit();
    m_b2BucketName = new QLineEdit();
    m_b2CustomUrl = new QLineEdit();
    m_b2CustomUrl->setPlaceholderText("Optional CDN/custom base URL");

    auto* uploadBox = new QGroupBox("Upload");
    auto* uploadLayout = new QVBoxLayout();
    uploadLayout->addWidget(m_autoUpload);
    uploadLayout->addWidget(m_useCatbox);
    uploadLayout->addWidget(m_useB2);

    auto* uploadForm = new QFormLayout();
    uploadForm->addRow("Catbox userhash:", m_catboxHash);
    uploadForm->addRow("B2 key ID:", m_b2KeyId);
    uploadForm->addRow("B2 app key:", m_b2AppKey);
    uploadForm->addRow("B2 bucket ID:", m_b2BucketId);
    uploadForm->addRow("B2 bucket name:", m_b2BucketName);
    uploadForm->addRow("B2 custom URL:", m_b2CustomUrl);
    uploadLayout->addLayout(uploadForm);
    uploadBox->setLayout(uploadLayout);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &OptionsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &OptionsDialog::reject);

    auto* layout = new QVBoxLayout();
    layout->addWidget(generalBox);
    layout->addWidget(encoderBox);
    layout->addWidget(uploadBox);
    layout->addWidget(buttons);
    setLayout(layout);

    m_logToFile->setChecked(g_logToFile);
    m_autoPlay->setChecked(g_autoPlay);
    m_autoUpload->setChecked(g_autoUpload);
    m_useCatbox->setChecked(g_useCatbox);
    m_useB2->setChecked(g_useB2);

    m_catboxHash->setText(toQString(g_catboxUserHash));
    m_b2KeyId->setText(toQString(g_b2KeyId));
    m_b2AppKey->setText(toQString(g_b2AppKey));
    m_b2BucketId->setText(toQString(g_b2BucketId));
    m_b2BucketName->setText(toQString(g_b2BucketName));
    m_b2CustomUrl->setText(toQString(g_b2CustomUrl));

    m_encoderLibx264->setChecked(g_encoderSelection == EncoderSelection::Libx264);
    m_encoderNvenc->setChecked(g_encoderSelection == EncoderSelection::Nvenc);
    m_encoderAmf->setChecked(g_encoderSelection == EncoderSelection::Amf);
}

void OptionsDialog::accept()
{
    if (m_encoderNvenc->isChecked())
        g_encoderSelection = EncoderSelection::Nvenc;
    else if (m_encoderAmf->isChecked())
        g_encoderSelection = EncoderSelection::Amf;
    else
        g_encoderSelection = EncoderSelection::Libx264;

    g_logToFile = m_logToFile->isChecked();
    g_autoPlay = m_autoPlay->isChecked();

    g_autoUpload = m_autoUpload->isChecked();
    g_useCatbox = m_useCatbox->isChecked();
    g_useB2 = m_useB2->isChecked();

    g_catboxUserHash = toWString(m_catboxHash->text());
    g_b2KeyId = toWString(m_b2KeyId->text());
    g_b2AppKey = toWString(m_b2AppKey->text());
    g_b2BucketId = toWString(m_b2BucketId->text());
    g_b2BucketName = toWString(m_b2BucketName->text());
    g_b2CustomUrl = toWString(m_b2CustomUrl->text());

    SaveSettings();
    QDialog::accept();
}

