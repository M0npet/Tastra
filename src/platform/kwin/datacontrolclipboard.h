// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/systemclipboard.h"

#include <QByteArray>
#include <QHash>
#include <QStringList>

#include <memory>

class QSocketNotifier;
class QTimer;
struct wl_display;
struct wl_registry;
struct wl_seat;
struct ext_data_control_manager_v1;
struct ext_data_control_device_v1;
struct ext_data_control_offer_v1;
struct ext_data_control_source_v1;

namespace Tastra::KWin
{

// The clipboard through ext-data-control-v1, as Klipper uses it. KWin offers
// the regular Wayland clipboard (wl_data_device) only to the client with
// keyboard focus, which an input panel never gets, and it refuses
// set_selection without focus; data control works without focus, and KWin
// gives the input method every global (KWin wayland_server.cpp:
// allowInterface returns true for the input method connection).
//
// Plain libwayland objects on the given display's default queue: in the
// keyboard that is Qt's display, whose event loop dispatches them; tests use
// their own display. Reading another application's text is asynchronous
// (a pipe watched by the event loop), so the keyboard never waits for it.
class DataControlClipboard final : public SystemClipboard
{
    Q_OBJECT

public:
    // Null when the compositor has no ext_data_control_manager_v1 (KWin
    // before Plasma 6.4) or no seat. Binds its own wl_seat proxy.
    static std::unique_ptr<DataControlClipboard> create(wl_display *display);
    // For the keyboard: Qt's own Wayland display, if running on Wayland.
    static std::unique_ptr<DataControlClipboard> createForApplication();
    ~DataControlClipboard() override;

    QString text() const override { return m_text; }
    bool isSensitive() const override { return m_sensitive; }
    void setText(const QString &text) override;
    void clear() override;
    QByteArray image() const override { return m_image; }
    QString imageMime() const override { return m_imageMime; }
    void setImage(const QByteArray &data, const QString &mime) override;

    // libwayland callbacks (C listeners); not for callers.
    static void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
    static void registryGlobalRemove(void *data, wl_registry *registry, uint32_t name);
    static void deviceDataOffer(void *data, ext_data_control_device_v1 *device, ext_data_control_offer_v1 *offer);
    static void deviceSelection(void *data, ext_data_control_device_v1 *device, ext_data_control_offer_v1 *offer);
    static void deviceFinished(void *data, ext_data_control_device_v1 *device);
    static void devicePrimarySelection(void *data, ext_data_control_device_v1 *device, ext_data_control_offer_v1 *offer);
    static void offerMimeType(void *data, ext_data_control_offer_v1 *offer, const char *mimeType);
    static void sourceSend(void *data, ext_data_control_source_v1 *source, const char *mimeType, int32_t fd);
    static void sourceCancelled(void *data, ext_data_control_source_v1 *source);

private:
    explicit DataControlClipboard(wl_display *display);
    bool bind();

    void takeSelection(ext_data_control_offer_v1 *offer);
    void stopReading();
    void finishReading(bool complete);
    void setTextFromOffer(const QString &text);
    void setImageFromOffer(const QByteArray &data, const QString &mime);
    void offerSource(const QByteArray &data, const QStringList &mimes);

    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_seat *m_seat = nullptr;
    ext_data_control_manager_v1 *m_manager = nullptr;
    ext_data_control_device_v1 *m_device = nullptr;
    QHash<ext_data_control_offer_v1 *, QStringList> m_offers;   // offered, not yet destroyed
    ext_data_control_offer_v1 *m_selection = nullptr;
    ext_data_control_source_v1 *m_source = nullptr;             // what we put on the clipboard
    QByteArray m_sourceData;
    QString m_sourceImageMime;                                  // set when the source is an image
    QByteArray m_ownMime;
    QString m_readingImageMime;                                 // the read in progress is an image
    int m_readFd = -1;
    QSocketNotifier *m_reader = nullptr;
    QTimer *m_readTimeout = nullptr;
    QByteArray m_readBuffer;
    QString m_text;
    QByteArray m_image;
    QString m_imageMime;
    bool m_sensitive = false;
};

}
