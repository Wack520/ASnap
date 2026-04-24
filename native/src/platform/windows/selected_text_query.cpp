#include "platform/windows/selected_text_query.h"

#include <memory>

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ais::platform::windows {

namespace {

[[nodiscard]] std::unique_ptr<QMimeData> cloneMimeData(const QMimeData* source) {
    auto clone = std::make_unique<QMimeData>();
    if (source == nullptr) {
        return clone;
    }

    for (const QString& format : source->formats()) {
        clone->setData(format, source->data(format));
    }
    if (source->hasText()) {
        clone->setText(source->text());
    }
    if (source->hasHtml()) {
        clone->setHtml(source->html());
    }
    if (source->hasUrls()) {
        clone->setUrls(source->urls());
    }
    if (source->hasImage()) {
        clone->setImageData(source->imageData());
    }
    if (source->hasColor()) {
        clone->setColorData(source->colorData());
    }
    return clone;
}

void restoreClipboard(QClipboard* clipboard, std::unique_ptr<QMimeData> originalData) {
    if (clipboard == nullptr) {
        return;
    }

    if (originalData == nullptr || originalData->formats().isEmpty()) {
        clipboard->clear();
        return;
    }

    clipboard->setMimeData(originalData.release());
}

#if defined(_WIN32)
[[nodiscard]] bool isVirtualKeyDown(const int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

void waitForTriggerKeyRelease() {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 300) {
        if (!isVirtualKeyDown(VK_SHIFT) && !isVirtualKeyDown('A')) {
            return;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        Sleep(10);
    }
}

[[nodiscard]] bool sendCopyChord() {
    INPUT inputs[4]{};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'C';

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'C';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    return SendInput(static_cast<UINT>(std::size(inputs)), inputs, sizeof(INPUT)) ==
           static_cast<UINT>(std::size(inputs));
}
#endif

}  // namespace

QString querySelectedText(QString* errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QClipboard* const clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Clipboard is unavailable");
        }
        return {};
    }

    std::unique_ptr<QMimeData> originalData = cloneMimeData(clipboard->mimeData());

#if defined(_WIN32)
    const DWORD initialSequence = GetClipboardSequenceNumber();
    waitForTriggerKeyRelease();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    if (!sendCopyChord()) {
        restoreClipboard(clipboard, std::move(originalData));
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to send copy shortcut");
        }
        return {};
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 450) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
        if (GetClipboardSequenceNumber() != initialSequence) {
            const QString text = clipboard->text().trimmed();
            restoreClipboard(clipboard, std::move(originalData));
            if (!text.isEmpty()) {
                return text;
            }

            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Clipboard changed without text");
            }
            return {};
        }
        Sleep(15);
    }

    restoreClipboard(clipboard, std::move(originalData));
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Timed out waiting for selected text");
    }
    return {};
#else
    restoreClipboard(clipboard, std::move(originalData));
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Selected text query is only supported on Windows");
    }
    return {};
#endif
}

}  // namespace ais::platform::windows
