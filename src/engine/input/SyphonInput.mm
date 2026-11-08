#define GL_SILENCE_DEPRECATION
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <Syphon/SyphonOpenGLClient.h>
#import <Syphon/SyphonOpenGLImage.h>
#import <Syphon/SyphonServerDirectory.h>
#include "LiveInput.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>

namespace {
NSArray<NSDictionary *> *servers() {
    return [[SyphonServerDirectory sharedDirectory] servers];
}

class SyphonInput final : public LiveInput {
public:
    explicit SyphonInput(QString sender) : sender(std::move(sender)) {}
    ~SyphonInput() override { [client stop]; }

    QList<LiveSender> senders() override {
        QList<LiveSender> result;
        @autoreleasepool {
            for (NSDictionary *info in servers()) {
                NSString *uuid = info[SyphonServerDescriptionUUIDKey];
                if (!uuid) continue;
                const auto app = QString::fromNSString(info[SyphonServerDescriptionAppNameKey] ?: @"");
                const auto name = QString::fromNSString(info[SyphonServerDescriptionNameKey] ?: @"");
                result << LiveSender{QString::fromNSString(uuid), app.isEmpty() ? name :
                    (name.isEmpty() ? app : app + " — " + name)};
            }
        }
        return result;
    }

    QImage frame(unsigned int) override {
        @autoreleasepool {
            auto current = CGLGetCurrentContext();
            if (!current) return {};
            if (!client || ![client isValid] || current != context) {
                [client stop];
                client = nil;
                context = current;
                for (NSDictionary *info in servers()) {
                    if (QString::fromNSString(info[SyphonServerDescriptionUUIDKey] ?: @"") != sender) continue;
                    client = [[SyphonOpenGLClient alloc] initWithServerDescription:info
                              context:current options:nil newFrameHandler:nil];
                    break;
                }
            }
            if (![client hasNewFrame]) return {};
            SyphonOpenGLImage *image = [client newFrameImage];
            const NSSize size = image.textureSize;
            if (!image || size.width < 1 || size.height < 1 || size.width > 8192 || size.height > 8192) return {};
            QImage pixels(int(size.width), int(size.height), QImage::Format_RGBA8888);
            if (pixels.isNull()) return {};
            auto *gl = QOpenGLContext::currentContext()->functions();
            GLint previous = 0;
            GLuint framebuffer = 0;
            gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
            gl->glGenFramebuffers(1, &framebuffer);
            gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE_ARB, image.textureName, 0);
            const bool complete = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            if (complete)
                gl->glReadPixels(0, 0, pixels.width(), pixels.height(), GL_RGBA, GL_UNSIGNED_BYTE, pixels.bits());
            gl->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previous));
            gl->glDeleteFramebuffers(1, &framebuffer);
            if (!complete) return {};
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            return pixels.flipped(Qt::Vertical);
#else
            return pixels.mirrored();
#endif
        }
    }
private:
    QString sender;
    SyphonOpenGLClient *client = nil;
    CGLContextObj context = nullptr;
};
}

std::unique_ptr<LiveInput> createLiveInput(const QString &sender, QSize) {
    return std::make_unique<SyphonInput>(sender);
}
