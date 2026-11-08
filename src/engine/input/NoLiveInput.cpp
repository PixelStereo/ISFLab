#include "LiveInput.h"

namespace {
class NoLiveInput final : public LiveInput {
public:
    QString error() const override { return "This build does not include the native video-sharing SDK."; }
    QImage frame(unsigned int) override { return {}; }
};
}
std::unique_ptr<LiveInput> createLiveInput(const QString &, QSize) {
    return std::make_unique<NoLiveInput>();
}
