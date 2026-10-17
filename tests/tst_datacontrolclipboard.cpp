// SPDX-License-Identifier: GPL-3.0-or-later
//
// The clipboard through ext-data-control-v1 against a small in-process
// compositor (libwayland-server, its own thread) that implements wl_seat and
// the data-control globals the way KWin's seat does: one selection, offered
// to every data-control device, with its source cancelled when replaced.
// Two clients stand for "another application" and the keyboard.

#include <QtTest/QTest>

#include <QSignalSpy>
#include <QSocketNotifier>
#include <QTimer>

#include "fakecompositor.h"
#include "platform/kwin/datacontrolclipboard.h"
#include "platform/kwin/fakeinputkeychords.h"

#include <wayland-client.h>

#include "ext-data-control-v1-client-protocol.h"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using TestWayland::FakeCompositor;

namespace
{

// A client connection whose events the Qt event loop dispatches, as Qt's
// Wayland plugin does for the keyboard.
class Connection
{
public:
    explicit Connection(const char *socket)
        : m_display(wl_display_connect(socket))
    {
        if (!m_display) return;
        m_notifier = std::make_unique<QSocketNotifier>(wl_display_get_fd(m_display), QSocketNotifier::Read);
        // Never block: read what is there, then dispatch (as Qt does).
        QObject::connect(m_notifier.get(), &QSocketNotifier::activated, [this] {
            if (wl_display_prepare_read(m_display) == 0) wl_display_read_events(m_display);
            wl_display_dispatch_pending(m_display);
        });
        m_timer.setInterval(5);
        QObject::connect(&m_timer, &QTimer::timeout, [this] {
            wl_display_dispatch_pending(m_display);
            wl_display_flush(m_display);
        });
        m_timer.start();
    }
    ~Connection()
    {
        m_timer.stop();
        m_notifier.reset();
        if (m_display) wl_display_disconnect(m_display);
    }
    wl_display *display() const { return m_display; }

private:
    wl_display *m_display = nullptr;
    std::unique_ptr<QSocketNotifier> m_notifier;
    QTimer m_timer;
};

// Another application's clipboard, written by hand: offers the given types
// and answers a read with `answer` (or never, when `answers` is false).
class RawSource
{
public:
    RawSource(wl_display *display, const std::vector<std::string> &mimes, const QByteArray &answer, bool answers = true)
        : m_display(display), m_answer(answer), m_answers(answers)
    {
        m_registry = wl_display_get_registry(display);
        static const wl_registry_listener listener = {
            [](void *data, wl_registry *r, uint32_t name, const char *interface, uint32_t) {
                auto *self = static_cast<RawSource *>(data);
                if (std::string(interface) == ext_data_control_manager_v1_interface.name)
                    self->m_manager = static_cast<ext_data_control_manager_v1 *>(wl_registry_bind(r, name, &ext_data_control_manager_v1_interface, 1));
                if (std::string(interface) == wl_seat_interface.name)
                    self->m_seat = static_cast<wl_seat *>(wl_registry_bind(r, name, &wl_seat_interface, 1));
            },
            [](void *, wl_registry *, uint32_t) {}};
        wl_registry_add_listener(m_registry, &listener, this);
        wl_display_roundtrip(display);
        m_device = ext_data_control_manager_v1_get_data_device(m_manager, m_seat);
        m_source = ext_data_control_manager_v1_create_data_source(m_manager);
        static const ext_data_control_source_v1_listener sourceListener = {
            [](void *data, ext_data_control_source_v1 *, const char *, int32_t fd) {
                auto *self = static_cast<RawSource *>(data);
                if (self->m_answers) {
                    if (::write(fd, self->m_answer.constData(), size_t(self->m_answer.size())) < 0) {}
                    ::close(fd);
                } else {
                    self->m_heldFds.push_back(fd);     // a reader that never gets an answer
                }
            },
            [](void *, ext_data_control_source_v1 *) {}};
        ext_data_control_source_v1_add_listener(m_source, &sourceListener, this);
        for (const std::string &mime : mimes) ext_data_control_source_v1_offer(m_source, mime.c_str());
    }
    ~RawSource()
    {
        for (int fd : m_heldFds) ::close(fd);
        ext_data_control_source_v1_destroy(m_source);
        ext_data_control_device_v1_destroy(m_device);
        ext_data_control_manager_v1_destroy(m_manager);
        wl_seat_destroy(m_seat);
        wl_registry_destroy(m_registry);
        wl_display_flush(m_display);
    }
    void copy()
    {
        ext_data_control_device_v1_set_selection(m_device, m_source);
        wl_display_flush(m_display);
    }

private:
    wl_display *m_display;
    QByteArray m_answer;
    bool m_answers;
    std::vector<int> m_heldFds;
    wl_registry *m_registry = nullptr;
    ext_data_control_manager_v1 *m_manager = nullptr;
    wl_seat *m_seat = nullptr;
    ext_data_control_device_v1 *m_device = nullptr;
    ext_data_control_source_v1 *m_source = nullptr;
};

}

class DataControlClipboardTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void anotherApplicationsTextIsRead()
    {
        FakeCompositor compositor;
        Connection appConnection(compositor.socketName()), keyboardConnection(compositor.socketName());
        auto app = Tastra::KWin::DataControlClipboard::create(appConnection.display());
        auto keyboard = Tastra::KWin::DataControlClipboard::create(keyboardConnection.display());
        QVERIFY(app && keyboard);
        QSignalSpy changed(keyboard.get(), &Tastra::SystemClipboard::changed);

        app->setText(QStringLiteral("Привіт 👋 from another app"));
        QTRY_COMPARE(keyboard->text(), QStringLiteral("Привіт 👋 from another app"));
        QVERIFY(changed.count() >= 1);

        // Longer text arrives whole, in several reads. (Both clients share
        // one thread here, so it stays below a pipe's 64 KiB buffer; in the
        // keyboard the writer is another process.)
        const QString big = QString(QStringLiteral("0123456789")).repeated(6000);
        app->setText(big);
        QTRY_COMPARE_WITH_TIMEOUT(keyboard->text().size(), big.size(), 10000);
        QCOMPARE(keyboard->text(), big);
    }

    void theKeyboardsCopyReachesOtherApplications()
    {
        // Copy / Cut in the text-editing panel: the keyboard owns the
        // clipboard without having keyboard focus; its own offer is not read
        // back through a pipe (no waiting on itself).
        FakeCompositor compositor;
        Connection appConnection(compositor.socketName()), keyboardConnection(compositor.socketName());
        auto app = Tastra::KWin::DataControlClipboard::create(appConnection.display());
        auto keyboard = Tastra::KWin::DataControlClipboard::create(keyboardConnection.display());
        QVERIFY(app && keyboard);
        keyboard->setText(QStringLiteral("copied on the keyboard"));
        QTRY_COMPARE(app->text(), QStringLiteral("copied on the keyboard"));
        QCOMPARE(keyboard->text(), QStringLiteral("copied on the keyboard"));

        // The other application copies: the keyboard's source is cancelled
        // and the new text arrives.
        app->setText(QStringLiteral("newer"));
        QTRY_COMPARE(keyboard->text(), QStringLiteral("newer"));
        keyboard->setText(QStringLiteral("again"));
        QTRY_COMPARE(app->text(), QStringLiteral("again"));

        // Clearing empties it for everyone.
        QSignalSpy changed(app.get(), &Tastra::SystemClipboard::changed);
        keyboard->clear();
        QTRY_VERIFY(changed.count() >= 1);
        QTRY_VERIFY(app->text().isEmpty());
    }

    void imagesAreNotText()
    {
        FakeCompositor compositor;
        Connection appConnection(compositor.socketName()), keyboardConnection(compositor.socketName());
        auto keyboard = Tastra::KWin::DataControlClipboard::create(keyboardConnection.display());
        QVERIFY(keyboard);
        auto text = Tastra::KWin::DataControlClipboard::create(appConnection.display());
        text->setText(QStringLiteral("text first"));
        QTRY_COMPARE(keyboard->text(), QStringLiteral("text first"));

        // A raw client that only offers an image.
        wl_display *display = appConnection.display();
        wl_registry *registry = wl_display_get_registry(display);
        struct Globals { ext_data_control_manager_v1 *manager = nullptr; wl_seat *seat = nullptr; } globals;
        static const wl_registry_listener listener = {
            [](void *data, wl_registry *r, uint32_t name, const char *interface, uint32_t) {
                auto *g = static_cast<Globals *>(data);
                if (std::string(interface) == ext_data_control_manager_v1_interface.name)
                    g->manager = static_cast<ext_data_control_manager_v1 *>(wl_registry_bind(r, name, &ext_data_control_manager_v1_interface, 1));
                if (std::string(interface) == wl_seat_interface.name)
                    g->seat = static_cast<wl_seat *>(wl_registry_bind(r, name, &wl_seat_interface, 1));
            },
            [](void *, wl_registry *, uint32_t) {}};
        wl_registry_add_listener(registry, &listener, &globals);
        wl_display_roundtrip(display);
        QVERIFY(globals.manager && globals.seat);
        ext_data_control_device_v1 *device = ext_data_control_manager_v1_get_data_device(globals.manager, globals.seat);
        ext_data_control_source_v1 *source = ext_data_control_manager_v1_create_data_source(globals.manager);
        ext_data_control_source_v1_offer(source, "image/png");
        QSignalSpy changed(keyboard.get(), &Tastra::SystemClipboard::changed);
        ext_data_control_device_v1_set_selection(device, source);
        wl_display_flush(display);
        QTRY_VERIFY(changed.count() >= 1);
        QVERIFY(keyboard->text().isEmpty());
        ext_data_control_source_v1_destroy(source);
        ext_data_control_device_v1_destroy(device);
        ext_data_control_manager_v1_destroy(globals.manager);
        wl_seat_destroy(globals.seat);
        wl_registry_destroy(registry);
        wl_display_flush(display);
    }

    void imagesAreReadAndOffered()
    {
        // Gboard keeps copied images and screenshots: with no text offered,
        // the image is read (PNG first); the keyboard can own the clipboard
        // with an image too, written as the reader takes it (larger than a
        // pipe's buffer here, with both clients on one thread).
        FakeCompositor compositor;
        Connection appConnection(compositor.socketName()), keyboardConnection(compositor.socketName());
        auto keyboard = Tastra::KWin::DataControlClipboard::create(keyboardConnection.display());
        auto app = Tastra::KWin::DataControlClipboard::create(appConnection.display());
        QVERIFY(keyboard && app);
        const QByteArray png = QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray(2000, 'p');
        {
            RawSource screenshot(appConnection.display(), {"image/jpeg", "image/png"}, png);
            screenshot.copy();
            QTRY_COMPARE(keyboard->image(), png);
            QCOMPARE(keyboard->imageMime(), QStringLiteral("image/png"));
            QVERIFY(keyboard->text().isEmpty());
            // Text wins over an image, and replaces it.
            app->setText(QStringLiteral("words"));
            QTRY_COMPARE(keyboard->text(), QStringLiteral("words"));
            QVERIFY(keyboard->image().isEmpty());
        }
        QByteArray big(300 * 1024, '\0');
        for (int i = 0; i < big.size(); ++i) big[i] = char(i * 7);
        keyboard->setImage(big, QStringLiteral("image/png"));
        QCOMPARE(keyboard->image(), big);
        QTRY_COMPARE_WITH_TIMEOUT(app->image().size(), big.size(), 10000);
        QCOMPARE(app->image(), big);
        QCOMPARE(app->imageMime(), QStringLiteral("image/png"));
        QVERIFY(app->text().isEmpty());
        // The keyboard's own image comes back without a read.
        QCOMPARE(keyboard->image(), big);
    }

    void copyingAgainNeverEmptiesTheClipboard()
    {
        // The new source replaces the old one in one step (destroying the old
        // one first emptied the clipboard for everyone in between).
        FakeCompositor compositor;
        Connection appConnection(compositor.socketName()), keyboardConnection(compositor.socketName());
        auto app = Tastra::KWin::DataControlClipboard::create(appConnection.display());
        auto keyboard = Tastra::KWin::DataControlClipboard::create(keyboardConnection.display());
        QVERIFY(app && keyboard);
        keyboard->setText(QStringLiteral("A"));
        QTRY_COMPARE(app->text(), QStringLiteral("A"));
        const int emptyBefore = compositor.emptySelectionsSent();
        keyboard->setText(QStringLiteral("B"));
        QTRY_COMPARE(app->text(), QStringLiteral("B"));
        QCOMPARE(compositor.emptySelectionsSent(), emptyBefore);
    }

    void aPasswordIsMarkedSensitive()
    {
        // KeePassXC marks secrets with x-kde-passwordManagerHint (Klipper
        // keeps them out of its history): the keyboard reads the text for
        // Paste but flags it.
        FakeCompositor compositor;
        Connection appConnection(compositor.socketName()), keyboardConnection(compositor.socketName());
        auto keyboard = Tastra::KWin::DataControlClipboard::create(keyboardConnection.display());
        QVERIFY(keyboard);
        RawSource secret(appConnection.display(), {"text/plain;charset=utf-8", "x-kde-passwordManagerHint"}, "s3cret");
        secret.copy();
        QTRY_COMPARE(keyboard->text(), QStringLiteral("s3cret"));
        QVERIFY(keyboard->isSensitive());
        RawSource plain(appConnection.display(), {"text/plain;charset=utf-8"}, "hello");
        plain.copy();
        QTRY_COMPARE(keyboard->text(), QStringLiteral("hello"));
        QVERIFY(!keyboard->isSensitive());
    }

    void theOldTextIsGoneWhileTheNewOneIsRead()
    {
        // Paste must not insert what was copied before when the new text is
        // slow to come or never comes.
        FakeCompositor compositor;
        Connection appConnection(compositor.socketName()), keyboardConnection(compositor.socketName());
        auto app = Tastra::KWin::DataControlClipboard::create(appConnection.display());
        auto keyboard = Tastra::KWin::DataControlClipboard::create(keyboardConnection.display());
        QVERIFY(app && keyboard);
        app->setText(QStringLiteral("old text"));
        QTRY_COMPARE(keyboard->text(), QStringLiteral("old text"));
        RawSource stuck(appConnection.display(), {"text/plain;charset=utf-8"}, QByteArray(), false);
        stuck.copy();
        QTRY_VERIFY_WITH_TIMEOUT(keyboard->text().isEmpty(), 1000);    // well before the 3 s read timeout
    }

    void editingShortcutsGoThroughFakeInputAsKeysyms()
    {
        // KWin 6.5+: keysyms, which KWin looks up in the user's layout, so
        // Ctrl+Z stays Undo with a German layout (Z is evdev KEY_Y there).
        FakeCompositor compositor;
        Connection connection(compositor.socketName());
        auto chords = Tastra::KWin::FakeInputKeyChords::create(connection.display());
        QVERIFY(chords);
        chords->send({Tastra::EvdevKey::LeftCtrl}, Tastra::EvdevKey::Z);
        chords->send({Tastra::EvdevKey::LeftCtrl, Tastra::EvdevKey::LeftShift}, Tastra::EvdevKey::Z);
        chords->send({Tastra::EvdevKey::LeftShift}, Tastra::EvdevKey::Left);
        using Key = std::pair<uint32_t, bool>;
        const std::vector<Key> expected = {{0xffe3, true}, {0x7a, true}, {0x7a, false}, {0xffe3, false},
                                           {0xffe3, true}, {0xffe1, true}, {0x7a, true}, {0x7a, false}, {0xffe1, false}, {0xffe3, false},
                                           {0xffe1, true}, {0xff51, true}, {0xff51, false}, {0xffe1, false}};
        QTRY_VERIFY(compositor.fakeKeysyms() == expected);
        QVERIFY(compositor.fakeKeys().empty());
        QVERIFY(compositor.fakeInputAuthenticated());
    }

    void editingShortcutsGoThroughFakeInput()
    {
        // Older KWin (version 5): key codes, as KDE Connect sends keys:
        // modifier down, key down and up, modifier up, after authenticating.
        FakeCompositor compositor(true, 5);
        Connection connection(compositor.socketName());
        auto chords = Tastra::KWin::FakeInputKeyChords::create(connection.display());
        QVERIFY(chords);
        chords->send({Tastra::EvdevKey::LeftCtrl}, Tastra::EvdevKey::A);
        chords->send({Tastra::EvdevKey::LeftShift}, Tastra::EvdevKey::Left);
        using Key = std::pair<uint32_t, bool>;
        const std::vector<Key> expected = {{29, true}, {30, true}, {30, false}, {29, false},
                                           {42, true}, {105, true}, {105, false}, {42, false}};
        QTRY_VERIFY(compositor.fakeKeys() == expected);
        QVERIFY(compositor.fakeInputAuthenticated());
    }

    void withoutDataControlThereIsNoClipboardObject()
    {
        // KWin before Plasma 6.4: the keyboard keeps QClipboard.
        FakeCompositor compositor(false);
        Connection connection(compositor.socketName());
        QVERIFY(connection.display());
        QVERIFY(!Tastra::KWin::DataControlClipboard::create(connection.display()));
    }
};

QTEST_GUILESS_MAIN(DataControlClipboardTest)
#include "tst_datacontrolclipboard.moc"
