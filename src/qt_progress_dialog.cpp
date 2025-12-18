#include "qt_progress_dialog.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

ProgressDialog::ProgressDialog(std::atomic<bool>* cancelFlag, QWidget* parent)
    : QDialog(parent), m_cancelFlag(cancelFlag)
{
    setWindowTitle("Working…");
    setModal(true);
    setMinimumWidth(420);

    m_status = new QLabel("Starting…");
    m_status->setWordWrap(true);

    m_progress = new QProgressBar();
    m_progress->setRange(0, 100);
    m_progress->setValue(0);

    m_cancelButton = new QPushButton("Cancel");
    connect(m_cancelButton, &QPushButton::clicked, this, &ProgressDialog::requestCancel);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_cancelButton);

    auto* layout = new QVBoxLayout();
    layout->addWidget(m_status);
    layout->addWidget(m_progress);
    layout->addLayout(buttonRow);
    setLayout(layout);
}

void ProgressDialog::setStatusText(const QString& text)
{
    m_status->setText(text);
}

void ProgressDialog::setProgressValue(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    m_progress->setValue(percent);
}

void ProgressDialog::closeEvent(QCloseEvent* event)
{
    requestCancel();
    event->ignore();
}

void ProgressDialog::requestCancel()
{
    if (m_cancelFlag)
        m_cancelFlag->store(true);
    if (m_cancelButton)
        m_cancelButton->setEnabled(false);
    if (m_status)
        m_status->setText("Canceling…");
}

