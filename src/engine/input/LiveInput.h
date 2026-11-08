#pragma once
#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <memory>

struct LiveSender { QString id, label; };
class LiveInput {
public:
    virtual ~LiveInput() = default;
    virtual QList<LiveSender> senders() { return {}; }
    virtual QString error() const { return {}; }
    // Called from the renderer's current GL context.
    virtual QImage frame(unsigned int framebuffer) = 0;
};

std::unique_ptr<LiveInput> createLiveInput(const QString &sender, QSize dimensions = {});
