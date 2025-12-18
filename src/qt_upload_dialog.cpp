#include "qt_upload_dialog.h"

#include "b2_upload.h"
#include "catbox_upload.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <thread>

static QString toQStringUtf8(const std::string& v)
{
    return QString::fromUtf8(v.c_str());
}

UploadDialog::UploadDialog(const std::wstring& exportPath, bool allowCatbox, bool allowB2, bool autoStart, QWidget* parent)
    : QDialog(parent), m_exportPath(exportPath)
{
    setWindowTitle("Upload");
    setModal(true);
    setMinimumWidth(560);

    auto* subtitle = new QLabel("Upload the exported file and copy the resulting URL:");
    subtitle->setWordWrap(true);

    auto makeProviderBox = [&](const QString& title, QPushButton*& btnUpload, QProgressBar*& bar, QLineEdit*& url,
                               QPushButton*& btnCopy, QLabel*& status) {
        auto* group = new QGroupBox(title);
        btnUpload = new QPushButton("Upload");
        bar = new QProgressBar();
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setVisible(false);

        url = new QLineEdit();
        url->setReadOnly(true);
        url->setVisible(false);

        btnCopy = new QPushButton("Copy");
        btnCopy->setVisible(false);

        status = new QLabel();
        status->setWordWrap(true);

        auto* topRow = new QHBoxLayout();
        topRow->addWidget(btnUpload);
        topRow->addWidget(bar, 1);

        auto* urlRow = new QHBoxLayout();
        urlRow->addWidget(url, 1);
        urlRow->addWidget(btnCopy);

        auto* layout = new QVBoxLayout();
        layout->addLayout(topRow);
        layout->addLayout(urlRow);
        layout->addWidget(status);
        group->setLayout(layout);

        return group;
    };

    auto* catbox = makeProviderBox("catbox.moe", m_catboxUpload, m_catboxProgress, m_catboxUrl, m_catboxCopy, m_catboxStatus);
    auto* b2 = makeProviderBox("Backblaze B2", m_b2Upload, m_b2Progress, m_b2Url, m_b2Copy, m_b2Status);

    m_openFolder = new QPushButton("Open Folder");
    m_close = new QPushButton("Close");

    connect(m_close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_openFolder, &QPushButton::clicked, this, [this]() {
        QFileInfo fi(QString::fromWCharArray(m_exportPath.c_str()));
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    });

    connect(m_catboxUpload, &QPushButton::clicked, this, &UploadDialog::startCatboxUpload);
    connect(m_b2Upload, &QPushButton::clicked, this, &UploadDialog::startB2Upload);

    connect(m_catboxCopy, &QPushButton::clicked, this, [this]() {
        if (auto* cb = QGuiApplication::clipboard())
            cb->setText(m_catboxUrl->text());
    });
    connect(m_b2Copy, &QPushButton::clicked, this, [this]() {
        if (auto* cb = QGuiApplication::clipboard())
            cb->setText(m_b2Url->text());
    });

    m_catboxUpload->setEnabled(allowCatbox);
    m_b2Upload->setEnabled(allowB2);

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(m_openFolder);
    buttons->addStretch(1);
    buttons->addWidget(m_close);

    auto* layout = new QVBoxLayout();
    layout->addWidget(subtitle);
    layout->addWidget(catbox);
    layout->addWidget(b2);
    layout->addLayout(buttons);
    setLayout(layout);

    if (autoStart)
    {
        if (allowCatbox)
            QMetaObject::invokeMethod(this, &UploadDialog::startCatboxUpload, Qt::QueuedConnection);
        if (allowB2)
            QMetaObject::invokeMethod(this, &UploadDialog::startB2Upload, Qt::QueuedConnection);
    }
}

void UploadDialog::startCatboxUpload()
{
    m_catboxUpload->setEnabled(false);
    m_catboxStatus->setText("Uploading…");
    m_catboxProgress->setValue(0);
    m_catboxProgress->setVisible(true);
    m_catboxUrl->setVisible(false);
    m_catboxCopy->setVisible(false);

    QPointer<UploadDialog> self(this);
    std::wstring path = m_exportPath;
    std::thread([self, path]() {
        std::string url;
        bool ok = UploadToCatbox(path, url, nullptr, [self](int pct) {
            if (!self)
                return;
            QMetaObject::invokeMethod(self, [self, pct]() {
                if (self)
                    self->m_catboxProgress->setValue(pct);
            });
        });
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, ok, url]() {
            if (!self)
                return;
            self->setCatboxResult(ok, toQStringUtf8(url));
        });
    }).detach();
}

void UploadDialog::startB2Upload()
{
    m_b2Upload->setEnabled(false);
    m_b2Status->setText("Uploading…");
    m_b2Progress->setValue(0);
    m_b2Progress->setVisible(true);
    m_b2Url->setVisible(false);
    m_b2Copy->setVisible(false);

    QPointer<UploadDialog> self(this);
    std::wstring path = m_exportPath;
    std::thread([self, path]() {
        std::string url;
        bool ok = UploadToB2(path, url, nullptr, [self](int pct) {
            if (!self)
                return;
            QMetaObject::invokeMethod(self, [self, pct]() {
                if (self)
                    self->m_b2Progress->setValue(pct);
            });
        });
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, ok, url]() {
            if (!self)
                return;
            self->setB2Result(ok, toQStringUtf8(url));
        });
    }).detach();
}

void UploadDialog::setCatboxResult(bool ok, const QString& url)
{
    m_catboxProgress->setVisible(false);
    if (ok) {
        m_catboxUrl->setText(url);
        m_catboxUrl->setVisible(true);
        m_catboxCopy->setVisible(true);
        m_catboxStatus->setText("Done.");
    } else {
        m_catboxUpload->setEnabled(true);
        m_catboxStatus->setText("Upload failed. Check your network/settings.");
    }
}

void UploadDialog::setB2Result(bool ok, const QString& url)
{
    m_b2Progress->setVisible(false);
    if (ok) {
        m_b2Url->setText(url);
        m_b2Url->setVisible(true);
        m_b2Copy->setVisible(true);
        m_b2Status->setText("Done.");
    } else {
        m_b2Upload->setEnabled(true);
        m_b2Status->setText("Upload failed. Check your network/settings.");
    }
}
