// SPDX-License-Identifier: GPL-3.0-or-later

#include "datacontrolclipboard.h"
#include "waylanddisplay.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QSocketNotifier>
#include <QTimer>

#include <wayland-client.h>

#include "ext-data-control-v1-client-protocol.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcDataControl, "tastra.clipboard")

namespace Tastra::KWin
{
namespace
{

// In order of preference (what Qt and GTK offer for text).
const char *const TextMimes[] = {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "TEXT", "STRING"};
const char *const PasswordManagerHint = "x-kde-passwordManagerHint";
constexpr qsizetype MaxClipboardBytes = 4 * 1024 * 1024;
constexpr qsizetype MaxImageBytes = 24 * 1024 * 1024;        // a screenshot of a large screen
constexpr int ReadTimeoutMs = 3000;
constexpr int ImageTimeoutMs = 8000;

void seatCapabilities(void *, wl_seat *, uint32_t) {}
void seatName(void *, wl_seat *, const char *) {}
const wl_seat_listener seatListener = {seatCapabilities, seatName};

// Writes a large answer (an image) to the reader's pipe as the reader takes
// it, without holding the keyboard; gives up after ImageTimeoutMs.
class PipeWriter final : public QObject
{
public:
    PipeWriter(int fd, const QByteArray &data, QObject *parent)
        : QObject(parent), m_fd(fd), m_data(data)
    {
        fcntl(m_fd, F_SETFL, fcntl(m_fd, F_GETFL) | O_NONBLOCK);
        m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Write, this);
        connect(m_notifier, &QSocketNotifier::activated, this, [this] { writeSome(); });
        QTimer::singleShot(ImageTimeoutMs, this, [this] { finish(); });
    }
    ~PipeWriter() override { if (m_fd >= 0) ::close(m_fd); }

private:
    void writeSome()
    {
        while (m_done < m_data.size()) {
            const ssize_t n = ::write(m_fd, m_data.constData() + m_done, size_t(m_data.size() - m_done));
            if (n > 0) { m_done += n; continue; }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;      // later
            break;                                                                  // reader gone
        }
        finish();
    }
    void finish()
    {
        if (m_notifier) m_notifier->setEnabled(false);
        if (m_fd >= 0) ::close(m_fd);
        m_fd = -1;
        deleteLater();
    }

    int m_fd;
    QByteArray m_data;
    qsizetype m_done = 0;
    QSocketNotifier *m_notifier = nullptr;
};

// Writes all of `data` to the reader's pipe; a stuck reader cannot hold the
// keyboard for more than half a second in all (the pipe is made
// non-blocking, so a write never waits for the reader to drain it).
void writeAll(int fd, const QByteArray &data)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    QElapsedTimer clock;
    clock.start();
    qsizetype done = 0;
    while (done < data.size()) {
        const qint64 left = 500 - clock.elapsed();
        if (left <= 0) break;
        pollfd pfd{fd, POLLOUT, 0};
        if (poll(&pfd, 1, int(left)) <= 0) break;
        const ssize_t n = ::write(fd, data.constData() + done, size_t(data.size() - done));
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        done += n;
    }
}

}

const wl_registry_listener registryListener = {DataControlClipboard::registryGlobal, DataControlClipboard::registryGlobalRemove};
const ext_data_control_device_v1_listener deviceListener = {
    DataControlClipboard::deviceDataOffer, DataControlClipboard::deviceSelection,
    DataControlClipboard::deviceFinished, DataControlClipboard::devicePrimarySelection};
const ext_data_control_offer_v1_listener offerListener = {DataControlClipboard::offerMimeType};
const ext_data_control_source_v1_listener sourceListener = {DataControlClipboard::sourceSend, DataControlClipboard::sourceCancelled};

DataControlClipboard::DataControlClipboard(wl_display *display)
    : m_display(display)
{
    // Marks the text this object put on the clipboard: its own offer is not
    // read back through a pipe. Unique per process and object.
    static int counter = 0;
    m_ownMime = QByteArrayLiteral("application/x-tastra-clipboard;pid=") + QByteArray::number(getpid())
        + QByteArrayLiteral(";n=") + QByteArray::number(++counter);
}

std::unique_ptr<DataControlClipboard> DataControlClipboard::create(wl_display *display)
{
    if (!display) return nullptr;
    // A reader that goes away mid-write must not kill the keyboard.
    std::signal(SIGPIPE, SIG_IGN);
    std::unique_ptr<DataControlClipboard> clipboard(new DataControlClipboard(display));
    if (!clipboard->bind()) return nullptr;
    return clipboard;
}

std::unique_ptr<DataControlClipboard> DataControlClipboard::createForApplication()
{
    return create(applicationWaylandDisplay());
}

bool DataControlClipboard::bind()
{
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &registryListener, this);
    wl_display_roundtrip(m_display);                     // the globals
    if (!m_manager || !m_seat) {
        qCInfo(lcDataControl) << "no ext-data-control or seat: clipboard through QClipboard";
        return false;
    }
    m_device = ext_data_control_manager_v1_get_data_device(m_manager, m_seat);
    ext_data_control_device_v1_add_listener(m_device, &deviceListener, this);
    wl_display_roundtrip(m_display);                     // the current selection
    qCInfo(lcDataControl) << "clipboard through ext-data-control-v1";
    return true;
}

DataControlClipboard::~DataControlClipboard()
{
    stopReading();
    for (auto it = m_offers.constBegin(); it != m_offers.constEnd(); ++it) ext_data_control_offer_v1_destroy(it.key());
    m_offers.clear();
    if (m_source) ext_data_control_source_v1_destroy(m_source);
    if (m_device) ext_data_control_device_v1_destroy(m_device);
    if (m_manager) ext_data_control_manager_v1_destroy(m_manager);
    if (m_seat) {
        if (wl_seat_get_version(m_seat) >= WL_SEAT_RELEASE_SINCE_VERSION) wl_seat_release(m_seat);
        else wl_seat_destroy(m_seat);
    }
    if (m_registry) wl_registry_destroy(m_registry);
    wl_display_flush(m_display);
}

void DataControlClipboard::registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    auto *self = static_cast<DataControlClipboard *>(data);
    if (std::strcmp(interface, ext_data_control_manager_v1_interface.name) == 0 && !self->m_manager) {
        self->m_manager = static_cast<ext_data_control_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0 && !self->m_seat) {
        self->m_seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, qMin(version, 5u)));
        wl_seat_add_listener(self->m_seat, &seatListener, self);
    }
}

void DataControlClipboard::registryGlobalRemove(void *, wl_registry *, uint32_t) {}

void DataControlClipboard::deviceDataOffer(void *data, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer)
{
    auto *self = static_cast<DataControlClipboard *>(data);
    self->m_offers.insert(offer, {});
    ext_data_control_offer_v1_add_listener(offer, &offerListener, self);
}

void DataControlClipboard::offerMimeType(void *data, ext_data_control_offer_v1 *offer, const char *mimeType)
{
    auto *self = static_cast<DataControlClipboard *>(data);
    const auto it = self->m_offers.find(offer);
    if (it != self->m_offers.end()) it->append(QString::fromUtf8(mimeType));
}

void DataControlClipboard::deviceSelection(void *data, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer)
{
    static_cast<DataControlClipboard *>(data)->takeSelection(offer);
}

void DataControlClipboard::devicePrimarySelection(void *data, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer)
{
    // The middle-click selection is not the clipboard: let it go.
    auto *self = static_cast<DataControlClipboard *>(data);
    if (offer && offer != self->m_selection && self->m_offers.remove(offer)) ext_data_control_offer_v1_destroy(offer);
}

void DataControlClipboard::deviceFinished(void *data, ext_data_control_device_v1 *device)
{
    auto *self = static_cast<DataControlClipboard *>(data);
    qCInfo(lcDataControl) << "data-control device finished";
    if (device == self->m_device) {
        ext_data_control_device_v1_destroy(device);
        self->m_device = nullptr;
    }
}

void DataControlClipboard::takeSelection(ext_data_control_offer_v1 *offer)
{
    stopReading();
    if (m_selection && m_selection != offer && m_offers.remove(m_selection)) ext_data_control_offer_v1_destroy(m_selection);
    m_selection = offer;
    m_sensitive = false;
    if (!offer) {
        setTextFromOffer(QString());
        return;
    }
    const QStringList mimes = m_offers.value(offer);
    if (mimes.contains(QString::fromLatin1(m_ownMime))) {
        if (m_sourceImageMime.isEmpty()) setTextFromOffer(QString::fromUtf8(m_sourceData));
        else setImageFromOffer(m_sourceData, m_sourceImageMime);
        return;
    }
    m_sensitive = mimes.contains(QLatin1String(PasswordManagerHint));
    const char *chosen = nullptr;
    for (const char *mime : TextMimes) {
        if (mimes.contains(QLatin1String(mime))) {
            chosen = mime;
            break;
        }
    }
    // No text: an image or screenshot (Gboard keeps those too); files and
    // anything else are not read.
    QByteArray chosenImage;
    m_readingImageMime.clear();
    if (!chosen) {
        for (const QString &mime : imageMimes()) {
            if (mimes.contains(mime)) {
                m_readingImageMime = mime;
                chosenImage = mime.toLatin1();
                chosen = chosenImage.constData();
                break;
            }
        }
    }
    if (!chosen) {
        setTextFromOffer(QString());
        return;
    }
    // Until the new text has arrived the old one is gone: Paste must not
    // insert what was copied before (nor after a failed read).
    if (!m_text.isEmpty() || !m_image.isEmpty()) setTextFromOffer(QString());
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return;
    // Only our end is non-blocking: the writer gets its own end as it is.
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    ext_data_control_offer_v1_receive(offer, chosen, fds[1]);
    ::close(fds[1]);
    wl_display_flush(m_display);
    m_readFd = fds[0];
    m_readBuffer.clear();
    m_reader = new QSocketNotifier(m_readFd, QSocketNotifier::Read, this);
    connect(m_reader, &QSocketNotifier::activated, this, [this]() {
        char buffer[16384];
        for (;;) {
            const ssize_t n = ::read(m_readFd, buffer, sizeof buffer);
            if (n > 0) {
                m_readBuffer.append(buffer, n);
                if (m_readBuffer.size() > (m_readingImageMime.isEmpty() ? MaxClipboardBytes : MaxImageBytes))
                    return finishReading(false);
                continue;
            }
            if (n == 0) return finishReading(true);
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            return finishReading(false);
        }
    });
    m_readTimeout = new QTimer(this);
    m_readTimeout->setSingleShot(true);
    connect(m_readTimeout, &QTimer::timeout, this, [this]() { finishReading(false); });
    m_readTimeout->start(m_readingImageMime.isEmpty() ? ReadTimeoutMs : ImageTimeoutMs);
}

void DataControlClipboard::stopReading()
{
    if (m_reader) {
        m_reader->setEnabled(false);
        m_reader->deleteLater();
        m_reader = nullptr;
    }
    if (m_readTimeout) {
        m_readTimeout->stop();
        m_readTimeout->deleteLater();
        m_readTimeout = nullptr;
    }
    if (m_readFd >= 0) {
        ::close(m_readFd);
        m_readFd = -1;
    }
}

void DataControlClipboard::finishReading(bool complete)
{
    const QByteArray data = m_readBuffer;
    m_readBuffer.clear();
    stopReading();
    if (!complete) {
        qCInfo(lcDataControl) << "clipboard read failed or timed out";
        return;
    }
    if (!m_readingImageMime.isEmpty()) setImageFromOffer(data, m_readingImageMime);
    else setTextFromOffer(QString::fromUtf8(data));
}

void DataControlClipboard::setTextFromOffer(const QString &text)
{
    m_text = text;
    m_image.clear();
    m_imageMime.clear();
    Q_EMIT changed();
}

void DataControlClipboard::setImageFromOffer(const QByteArray &data, const QString &mime)
{
    m_text.clear();
    m_image = data;
    m_imageMime = data.isEmpty() ? QString() : mime;
    Q_EMIT changed();
}

void DataControlClipboard::offerSource(const QByteArray &data, const QStringList &mimes)
{
    // The new source replaces the old one in one step: destroying the old
    // one first would empty the clipboard for everyone in between.
    ext_data_control_source_v1 *previous = m_source;
    m_sourceData = data;
    m_source = ext_data_control_manager_v1_create_data_source(m_manager);
    ext_data_control_source_v1_add_listener(m_source, &sourceListener, this);
    ext_data_control_source_v1_offer(m_source, m_ownMime.constData());
    for (const QString &mime : mimes) ext_data_control_source_v1_offer(m_source, mime.toLatin1().constData());
    ext_data_control_device_v1_set_selection(m_device, m_source);
    if (previous) ext_data_control_source_v1_destroy(previous);
    wl_display_flush(m_display);
    m_sensitive = false;
}

void DataControlClipboard::setText(const QString &text)
{
    if (!m_manager || !m_device) return;
    QStringList mimes;
    for (const char *mime : TextMimes) mimes.append(QString::fromLatin1(mime));
    m_sourceImageMime.clear();
    offerSource(text.toUtf8(), mimes);
    m_text = text;                         // the selection event confirms it
    m_image.clear();
    m_imageMime.clear();
}

void DataControlClipboard::setImage(const QByteArray &data, const QString &mime)
{
    if (!m_manager || !m_device || data.isEmpty() || mime.isEmpty()) return;
    m_sourceImageMime = mime;
    offerSource(data, {mime});
    m_text.clear();
    m_image = data;
    m_imageMime = mime;
}

void DataControlClipboard::clear()
{
    if (!m_device) return;
    ext_data_control_device_v1_set_selection(m_device, nullptr);
    wl_display_flush(m_display);
}

void DataControlClipboard::sourceSend(void *data, ext_data_control_source_v1 *source, const char *, int32_t fd)
{
    auto *self = static_cast<DataControlClipboard *>(data);
    if (source != self->m_source) {
        ::close(fd);
    } else if (!self->m_sourceImageMime.isEmpty()) {
        new PipeWriter(fd, self->m_sourceData, self);         // closes fd when done
    } else {
        writeAll(fd, self->m_sourceData);
        ::close(fd);
    }
}

void DataControlClipboard::sourceCancelled(void *data, ext_data_control_source_v1 *source)
{
    // Another application owns the clipboard now.
    auto *self = static_cast<DataControlClipboard *>(data);
    if (source == self->m_source) self->m_source = nullptr;
    ext_data_control_source_v1_destroy(source);
}

}
