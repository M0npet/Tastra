// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads the real Main.qml against the real KeyboardUiBridge (with a fake
// input-method backend) in an offscreen QQuickView. Guards UI hot-path
// behaviour that unit tests of the bridge alone cannot observe.

#include <QtTest/QTest>
#include <QMimeData>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <QColor>
#include <QClipboard>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QStandardPaths>

#include "app/keyboarduibridge.h"
#include "core/emojicatalog.h"
#include "app/keyboardhider.h"
#include "core/inputmethodbackend.h"
#include "core/keyboardcontroller.h"
#include "core/keyboardmodel.h"
#include "core/systemclipboard.h"
#include "core/keychords.h"
#include "app/voicecontroller.h"
#include "core/locallexicon.h"

#include <QPointingDevice>
#include <QStyleHints>
#include <QRegularExpression>
#include <QSettings>

namespace
{

class FakeClipboard final : public Tastra::SystemClipboard
{
public:
    QString text() const override { return m_text; }
    void setText(const QString &text) override { m_text = text; Q_EMIT changed(); }
    void clear() override { m_text.clear(); Q_EMIT changed(); }

private:
    QString m_text;
};

class FakeChords final : public Tastra::KeyChordSender
{
public:
    void send(const QList<int> &modifiers, int key) override { sent.append({modifiers, key}); }
    QList<QPair<QList<int>, int>> sent;
};

class FakeBackend final : public Tastra::InputMethodBackend
{
public:
    void commitText(const QString &text) override { commits.append(text); }
    void backspace() override { ++backspaces; }
    void deleteForward() override {}
    void moveLeft() override {}
    void moveRight() override {}
    void moveHome() override {}
    void moveEnd() override {}
    void enter() override {}

    QStringList commits;
    int backspaces = 0;
};

void collectLetterKeys(QQuickItem *item, QList<QPointer<QQuickItem>> &result)
{
    // Letter keys are the only items exposing a non-empty glideValue.
    const QVariant glide = item->property("glideValue");
    if (glide.isValid() && !glide.toString().isEmpty()) result.append(item);
    const auto children = item->childItems();
    for (QQuickItem *child : children) collectLetterKeys(child, result);
}

QQuickItem *findText(QQuickItem *item, const QString &text)
{
    if (item->isVisible() && item->property("text").toString() == text) return item;
    const auto children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findText(child, text)) return found;
    }
    return nullptr;
}

QQuickItem *findNamed(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name && item->isVisible()) return item;
    const auto children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findNamed(child, name)) return found;
    }
    return nullptr;
}

QList<QPointer<QQuickItem>> letterKeys(QQuickItem *root)
{
    QList<QPointer<QQuickItem>> result;
    collectLetterKeys(root, result);
    return result;
}

}

class QmlKeyboardTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("TastraTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_qmlkeyboard"));
        Tastra::LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setBlocklistSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/empty/none.txt"));
    }

    void init()
    {
        QSettings().clear();
        // The labels below are English; not the system language's.
        QSettings().setValue(QStringLiteral("uiLanguage"), QStringLiteral("en"));
        // Script errors in handlers are silent on device; make them fatal here.
        QTest::failOnWarning(QRegularExpression(QStringLiteral("ReferenceError|TypeError|is not declared")));
    }

    void tappingSuggestionReplacesTheTypedWord()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);

        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        QCOMPARE(view.status(), QQuickView::Ready);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        bridge.tapLetter(QStringLiteral("h"));
        bridge.tapLetter(QStringLiteral("e"));
        bridge.tapLetter(QStringLiteral("l"));
        // The strip follows the typed case ("Hel" at sentence start).
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("Hello")));

        QQuickItem *label = findText(view.rootObject(), QStringLiteral("Hello"));
        const QPointF centre = label->mapToScene(QPointF(label->width() / 2, label->height() / 2));
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre.toPoint());

        QTRY_COMPARE(backend.commits.size(), 5);
        QCOMPARE(backend.backspaces, 3);
        QCOMPARE(backend.commits.mid(3), QStringList({QStringLiteral("Hello"), QStringLiteral(" ")}));
    }

    void tappingARealKeyCommitsTheLetter()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);

        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        // Press/move/release exercise every Key handler (glide, drag, popup).
        QQuickItem *key = findText(view.rootObject(), QStringLiteral("Q"));
        QVERIFY(key);
        const QPoint centre = key->mapToScene(QPointF(key->width() / 2, key->height() / 2)).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, {}, centre);
        QTest::mouseMove(&view, centre + QPoint(1, 0));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, centre + QPoint(1, 0));
        QTRY_COMPARE(backend.commits, QStringList({QStringLiteral("Q")}));
    }

    void overlappingTwoThumbTapsAreBothCommitted()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);

        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto centreOf = [&](const QString &label) {
            QQuickItem *key = findText(view.rootObject(), label);
            return key ? key->mapToScene(QPointF(key->width() / 2, key->height() / 2)).toPoint() : QPoint(-1, -1);
        };
        const QPoint left = centreOf(QStringLiteral("a"));
        const QPoint right = centreOf(QStringLiteral("l"));
        QVERIFY(left.x() >= 0 && right.x() >= 0);

        static QPointingDevice *touch = QTest::createTouchDevice();
        // Fast two-thumb typing: the right thumb lands before the left lifts.
        // One touch sequence (a new QTest::touchEvent per step would move the
        // stationary point to 0,0).
        auto seq = QTest::touchEvent(&view, touch, false);
        seq.press(0, left).commit();
        seq.stationary(0).press(1, right).commit();
        seq.release(0, left).stationary(1).commit();
        seq.release(1, right).commit();
        QTRY_COMPARE(backend.commits, QStringList({QStringLiteral("a"), QStringLiteral("l")}));

        // Rolling the other way (the right thumb lifts first): still the order
        // of the presses, as LatinIME releases the older press on a new one.
        backend.commits.clear();
        seq.press(0, left).commit();
        seq.stationary(0).press(1, right).commit();
        seq.stationary(0).release(1, right).commit();
        seq.release(0, left).commit();
        QTRY_COMPARE(backend.commits, QStringList({QStringLiteral("a"), QStringLiteral("l")}));

        // Two thumbs on the same key: two letters, and the key is free again.
        backend.commits.clear();
        const QPoint e = centreOf(QStringLiteral("e"));
        seq.press(0, e - QPoint(10, 0)).commit();
        seq.stationary(0).press(1, e + QPoint(10, 0)).commit();
        seq.release(0, e - QPoint(10, 0)).stationary(1).commit();
        seq.release(1, e + QPoint(10, 0)).commit();
        QTRY_COMPARE(backend.commits, QStringList({QStringLiteral("e"), QStringLiteral("e")}));
        QTest::qWait(bridge.longPressDelay() + 150);
        QCOMPARE(backend.commits.size(), 2);                     // no long press afterwards

        // A tap that rolls a little on its key is a tap, not a glide.
        backend.commits.clear();
        seq.press(0, left).commit();
        seq.move(0, left + QPoint(22, 3)).commit();
        seq.release(0, left + QPoint(22, 3)).commit();
        QTRY_COMPARE(backend.commits, QStringList({QStringLiteral("a")}));

        // A glide that starts slowly (the finger drifts off the press point
        // and stays before going on) is not a long press on its first key.
        backend.commits.clear();
        const QPoint h = centreOf(QStringLiteral("h"));
        seq.press(0, h).commit();
        for (int i = 1; i <= 5; ++i) { seq.move(0, h + QPoint(i * 6, 0)).commit(); QTest::qWait(10); }
        QTest::qWait(bridge.longPressDelay() + 100);
        QVERIFY2(backend.commits.isEmpty(), qPrintable(backend.commits.join(',')));
        const QList<QPoint> rest = {h + QPoint(30, 0), centreOf(QStringLiteral("e")), centreOf(QStringLiteral("l")), centreOf(QStringLiteral("o"))};
        for (int i = 1; i < rest.size(); ++i)
            for (int step = 1; step <= 6; ++step) seq.move(0, rest[i - 1] + (rest[i] - rest[i - 1]) * step / 6).commit();
        seq.release(0, rest.last()).commit();
        QTRY_VERIFY(!backend.commits.isEmpty());
        QVERIFY2(backend.commits.first() != QStringLiteral("-") && backend.commits.first() != QStringLiteral("h"),
                 qPrintable(backend.commits.join(',')));

        // A tap a few pixels past the key's edge still types it.
        backend.commits.clear();
        QQuickItem *lKey = findText(view.rootObject(), QStringLiteral("l"))->parentItem();
        const QPoint edge = lKey->mapToScene(QPointF(lKey->width() + 4, lKey->height() / 2)).toPoint();
        seq.press(0, lKey->mapToScene(QPointF(lKey->width() - 3, lKey->height() / 2)).toPoint()).commit();
        seq.release(0, edge).commit();
        QTRY_COMPARE(backend.commits, QStringList({QStringLiteral("l")}));
    }

    void singleFingerGlideAndLongPressStillWork()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);

        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto centreOf = [&](const QString &label) {
            QQuickItem *key = findText(view.rootObject(), label);
            return key ? key->mapToScene(QPointF(key->width() / 2, key->height() / 2)).toPoint() : QPoint(-1, -1);
        };

        static QPointingDevice *touch = QTest::createTouchDevice();
        // Glide h-e-l-o with one finger.
        const QList<QPoint> path = {centreOf(QStringLiteral("h")), centreOf(QStringLiteral("e")),
                                    centreOf(QStringLiteral("l")), centreOf(QStringLiteral("o"))};
        QTest::touchEvent(&view, touch).press(0, path.first());
        for (int i = 1; i < path.size(); ++i) {
            const QPoint from = path.at(i - 1);
            const QPoint to = path.at(i);
            for (int step = 1; step <= 6; ++step) {
                QTest::touchEvent(&view, touch).move(0, from + (to - from) * step / 6);
            }
        }
        QQuickItem *trail = findNamed(view.rootObject(), QStringLiteral("glideTrail"));
        QVERIFY2(trail && trail->isVisible(), "gesture trail is drawn while gliding");
        QTest::touchEvent(&view, touch).release(0, path.last());
        QTRY_COMPARE(backend.commits.value(0), QStringLiteral("hello"));
        QTRY_VERIFY(!findNamed(view.rootObject(), QStringLiteral("glideTrail")));   // hidden after

        // Settings: Gesture trail off -> the same glide works, without a trail.
        bridge.setGlideTrail(false);
        const int commitsBefore = backend.commits.size();
        QTest::touchEvent(&view, touch).press(0, path.first());
        for (int i = 1; i < path.size(); ++i) {
            const QPoint from = path.at(i - 1);
            const QPoint to = path.at(i);
            for (int step = 1; step <= 6; ++step) {
                QTest::touchEvent(&view, touch).move(0, from + (to - from) * step / 6);
            }
        }
        QVERIFY(!findNamed(view.rootObject(), QStringLiteral("glideTrail")));
        QTest::touchEvent(&view, touch).release(0, path.last());
        QTRY_VERIFY2(backend.commits.size() > commitsBefore, "the second glide really happened");
        bridge.setGlideTrail(true);

        // Long press on a key with an alternate commits the alternate (DE s -> ß).
        bridge.setLanguage(QStringLiteral("de"));
        // Let the old EN key delegates finish deleting so lookups hit DE keys.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("ä")));
        const QString alternate = bridge.alternateForKey(QStringLiteral("s"));
        QCOMPARE(alternate, QStringLiteral("ß"));
        backend.commits.clear();
        const QPoint q = centreOf(QStringLiteral("s"));
        QTest::touchEvent(&view, touch).press(0, q);
        QTest::qWait(qApp->styleHints()->mousePressAndHoldInterval() + 200);
        QTest::touchEvent(&view, touch).release(0, q);
        QTRY_COMPARE(backend.commits, QStringList({alternate}));
    }

    void compactModeDocksTheKeysLeftOrRight()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);          // lowercase labels

        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.resize(1600, 520);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto keyX = [&](const QString &label) {
            QQuickItem *key = findText(view.rootObject(), label);
            return key ? key->mapToScene(QPointF(key->width() / 2, 0)).x() : -1.0;
        };
        QTRY_VERIFY(keyX(QStringLiteral("q")) > 0 && keyX(QStringLiteral("p")) > 0);
        const qreal fullSpan = keyX(QStringLiteral("p")) - keyX(QStringLiteral("q"));
        QVERIFY(fullSpan > 300);

        QCOMPARE(bridge.layoutMode(), QStringLiteral("full"));
        bridge.setLayoutMode(QStringLiteral("left"));
        QTRY_VERIFY(keyX(QStringLiteral("p")) < view.width() / 2 + 200);
        QVERIFY(keyX(QStringLiteral("p")) - keyX(QStringLiteral("q")) < fullSpan * 0.8);
        bridge.setLayoutMode(QStringLiteral("right"));
        QTRY_VERIFY(keyX(QStringLiteral("q")) > view.width() / 2 - 200);
        bridge.setLayoutMode(QStringLiteral("bogus"));        // ignored
        QCOMPARE(bridge.layoutMode(), QStringLiteral("right"));

        // Persisted like the other preferences.
        Tastra::KeyboardUiBridge reloaded(controller, model);
        QCOMPARE(reloaded.layoutMode(), QStringLiteral("right"));
    }

    void micButtonAppearsOnlyWithVoiceAndStartsRecording()
    {
        struct Recorder final : Tastra::AudioRecorder {
            bool start(QString *) override { started = true; return true; }
            std::vector<float> stop() override { return {}; }
            bool started = false;
        } recorder;
        struct Recognizer final : Tastra::SpeechRecognizer {
            QString unavailableReason() const override { return {}; }
            void recognize(std::vector<float>, const QString &, std::function<void(QString, QString)>) override {}
        } recognizer;

        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(!findText(view.rootObject(), QStringLiteral("🎤")));      // not built in

        Tastra::VoiceController voice(&recorder, &recognizer);
        bridge.setVoiceController(&voice);
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("🎤")));
        QTest::qWait(100);   // let the toolbar Row re-position its children (polish)
        QQuickItem *mic = findText(view.rootObject(), QStringLiteral("🎤"));
        QTest::mouseClick(&view, Qt::LeftButton, {}, mic->mapToScene(QPointF(mic->width() / 2, mic->height() / 2)).toPoint());
        QTRY_VERIFY(recorder.started);
        QCOMPARE(bridge.voiceState(), QStringLiteral("recording"));
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("Listening… tap 🎤 to finish")));
    }

    void gboardLongPressPickerNumberRowHintsAndForget()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto centre = [&](QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };
        static QPointingDevice *touch = QTest::createTouchDevice();

        // 1. Long-press "e", slide one cell right, release -> second choice.
        QQuickItem *eLabel = findText(view.rootObject(), QStringLiteral("e"));
        QVERIFY(eLabel);
        const QPoint e = centre(eLabel);
        QTest::touchEvent(&view, touch).press(0, e);
        QTest::qWait(qApp->styleHints()->mousePressAndHoldInterval() + 150);
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("è")));        // picker open
        const int cell = int(eLabel->parentItem()->width() * 0.86) + 2;
        QTest::touchEvent(&view, touch).move(0, e + QPoint(cell, 0));
        QTest::touchEvent(&view, touch).release(0, e + QPoint(cell, 0));
        QTRY_COMPARE(backend.commits, QStringList({QStringLiteral("è")}));

        // 2. Symbol hints in key corners can be switched off.
        QVERIFY(findText(view.rootObject(), QStringLiteral("1")));            // hint on "q"
        bridge.setSymbolHints(false);
        QTRY_VERIFY(!findText(view.rootObject(), QStringLiteral("1")));

        // 3. Number row grows the panel and types digits.
        const qreal before = view.rootObject()->height();
        bridge.setNumberRow(true);
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("numberKey_7")));
        QVERIFY(view.rootObject()->height() > before);
        QTest::qWait(100);                                                    // Row polish
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(findNamed(view.rootObject(), QStringLiteral("numberKey_7"))));
        QTRY_COMPARE(backend.commits.last(), QStringLiteral("7"));
        // ?123 keeps the height (the panel edge must not move under a
        // finger): the symbol rows take the number row's space.
        const qreal withNumbers = view.rootObject()->height();
        const qreal letterKey = findNamed(view.rootObject(), QStringLiteral("symbolsKey"))->height();
        bridge.toggleSymbols();
        QTRY_VERIFY(!findNamed(view.rootObject(), QStringLiteral("numberKey_7")) || !findNamed(view.rootObject(), QStringLiteral("numberKey_7"))->isVisible());
        QCOMPARE(view.rootObject()->height(), withNumbers);
        QVERIFY(findNamed(view.rootObject(), QStringLiteral("symbolsKey"))->height() > letterKey);
        bridge.toggleSymbols();
        QTRY_COMPARE(findNamed(view.rootObject(), QStringLiteral("symbolsKey"))->height(), letterKey);

        // 4. Long-press a suggestion removes it (and does not insert it).
        bridge.setNumberRow(false);
        bridge.space();
        for (const QChar ch : QStringLiteral("hel")) bridge.tapLetter(QString(ch));
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("hello")));
        QTest::qWait(100);
        const int commitsBefore = backend.commits.size();
        const QPoint h = centre(findText(view.rootObject(), QStringLiteral("hello")));
        QTest::mousePress(&view, Qt::LeftButton, {}, h);
        QTest::qWait(qApp->styleHints()->mousePressAndHoldInterval() + 200);
        QTest::mouseRelease(&view, Qt::LeftButton, {}, h);
        QTRY_VERIFY(!bridge.suggestions().contains(QStringLiteral("hello")));
        QCOMPARE(backend.commits.size(), commitsBefore);
    }

    void stripQuotesTypedWordAndBoldsTheCorrection()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        for (const QChar ch : QStringLiteral("teh")) bridge.tapLetter(QString(ch));
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("\u201Cteh\u201D")));
        QQuickItem *correction = findText(view.rootObject(), QStringLiteral("the"));
        QVERIFY(correction);
        QCOMPARE(correction->property("font").value<QFont>().weight(), QFont::Bold);
    }

    void numberFieldsShowANumpadEmailFieldsAnAt()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto centre = [&](QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };

        bridge.setContentType(0, 4);                                  // phone
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("numpadKey_5")));
        QVERIFY(findNamed(view.rootObject(), QStringLiteral("numpadKey_+")));
        QVERIFY(!findText(view.rootObject(), QStringLiteral("Q")));    // letters hidden
        QTest::qWait(100);
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(findNamed(view.rootObject(), QStringLiteral("numpadKey_5"))));
        QTRY_COMPARE(backend.commits, QStringList({QStringLiteral("5")}));

        bridge.setContentType(0, 6);                                  // e-mail
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("@")));
        QVERIFY(!findNamed(view.rootObject(), QStringLiteral("numpadKey_5")));
        QVERIFY(findText(view.rootObject(), QStringLiteral("q")));     // letters back (no auto-cap)
    }

    void gboardSlideGesturesAndEmojiRow()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto centre = [&](QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };
        static QPointingDevice *touch = QTest::createTouchDevice();
        auto slide = [&](const QPoint &from, const QPoint &to) {
            QTest::touchEvent(&view, touch).press(0, from);
            for (int i = 1; i <= 8; ++i) {
                QTest::touchEvent(&view, touch).move(0, from + (to - from) * i / 8);
                QTest::qWait(10);
            }
            QTest::touchEvent(&view, touch).release(0, to);
        };

        // 1. Shift slide onto "d" -> one capital, then lowercase again.
        const QPoint d = centre(findText(view.rootObject(), QStringLiteral("d")));
        slide(centre(findNamed(view.rootObject(), QStringLiteral("shiftKey"))), d);
        QTRY_COMPARE(backend.commits, QStringList({QStringLiteral("D")}));
        QTRY_VERIFY(!bridge.uppercase());

        // 2. ?123 slide onto the key where "5" appears (same place as "t").
        const QPoint t = centre(findText(view.rootObject(), QStringLiteral("t")));
        slide(centre(findNamed(view.rootObject(), QStringLiteral("symbolsKey"))), t);
        QTRY_COMPARE(backend.commits.last(), QStringLiteral("5"));
        QTRY_VERIFY(!bridge.symbolsActive());

        // 3. Emoji fast-access row: frequent emoji until some were used.
        bridge.setEmojiRow(true);
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("emojiRowKey_👍")));
        QCOMPARE(bridge.emojiRowEmojis().size(), 10);
        bridge.insertEmoji(QStringLiteral("😀"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("emojiRowKey_😀")));
        QTest::qWait(100);
        backend.commits.clear();                 // insertEmoji above committed one already
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(findNamed(view.rootObject(), QStringLiteral("emojiRowKey_😀"))));
        QTRY_COMPARE(backend.commits, QStringList({QStringLiteral("😀")}));
    }

    void lightDarkAndAmoledPalettes()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        QCOMPARE(view.status(), QQuickView::Ready);
        auto colour = [&](const char *name) { return view.rootObject()->property(name).value<QColor>(); };
        bridge.setTheme(QStringLiteral("light"));
        QCOMPARE(colour("backgroundColor"), QColor(QStringLiteral("#e8eaed")));
        QCOMPARE(colour("keyColor"), QColor(QStringLiteral("#ffffff")));
        QCOMPARE(colour("textColor"), QColor(QStringLiteral("#202124")));
        bridge.setTheme(QStringLiteral("dark"));
        QCOMPARE(colour("backgroundColor"), QColor(QStringLiteral("#202124")));
        bridge.setTheme(QStringLiteral("amoled"));
        QCOMPARE(colour("backgroundColor"), QColor(QStringLiteral("#000000")));
        QCOMPARE(colour("textColor"), QColor(QStringLiteral("#f1f3f4")));

        // Settings show version and data attributions.
        bridge.activateToolbarAction(QStringLiteral("settings"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("aboutText")));
        const QString about = findNamed(view.rootObject(), QStringLiteral("aboutText"))->property("text").toString();
        QVERIFY(about.contains(bridge.version()));
        QVERIFY(about.contains(QStringLiteral("CC BY-SA 4.0")));
    }

    void savingAndRemovingPersonalWordsThroughTheUi()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto centre = [&](QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };

        for (const QChar ch : QStringLiteral("zorgle")) bridge.tapLetter(QString(ch));
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("\u201Czorgle\u201D")));
        QTest::qWait(100);
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(findText(view.rootObject(), QStringLiteral("\u201Czorgle\u201D"))));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("saveWordChip")));
        QTest::qWait(50);
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(findNamed(view.rootObject(), QStringLiteral("saveWordChip"))));
        QTRY_COMPARE(bridge.userWords(), QStringList({QStringLiteral("zorgle")}));

        bridge.activateToolbarAction(QStringLiteral("settings"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("userWord_zorgle")));
        QTest::qWait(100);
        QQuickItem *chip = findNamed(view.rootObject(), QStringLiteral("userWord_zorgle"));
        // The section sits low in the settings: scroll there like a user would.
        QQuickItem *flick = chip->parentItem();
        while (flick && !flick->property("contentY").isValid()) flick = flick->parentItem();
        QVERIFY(flick);
        const QPointF inContent = chip->mapToItem(flick->property("contentItem").value<QQuickItem *>(), QPointF(0, 0));
        flick->setProperty("contentY", qMax(0.0, inContent.y() - 20));
        QTest::qWait(100);
        const QPoint at = centre(chip);
        QVERIFY2(at.y() > 0 && at.y() < view.height(), "chip scrolled into view");
        QTest::mouseClick(&view, Qt::LeftButton, {}, at);
        QTRY_VERIFY(bridge.userWords().isEmpty());
    }

    void aPhraseChipIsDimmedAfterItsFirstWordAndTypesTheWholePhrase()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.clearLearnedWords();
        bridge.setAutoCapitalizationEnabled(false);
        bridge.setAutocorrectEnabled(false);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        for (const QChar ch : QStringLiteral("see you soon. see you soon. see you soon. se")) {
            if (ch == QLatin1Char(' ')) bridge.space();
            else if (ch.isLetter()) bridge.tapLetter(QString(ch));
            else bridge.tapText(QString(ch));
        }
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("phraseChip")));
        QQuickItem *chip = findNamed(view.rootObject(), QStringLiteral("phraseChip"));
        const QString markup = chip->property("text").toString();
        QVERIFY2(markup.startsWith(QStringLiteral("see<font color=")) && markup.endsWith(QStringLiteral(" you soon</font>")),
                 qPrintable(markup));
        QVERIFY(chip->isVisible());
        QTest::qWait(100);
        if (qEnvironmentVariableIsSet("TASTRA_SCREENSHOT")) view.grabWindow().save(qEnvironmentVariable("TASTRA_SCREENSHOT"));
        backend.commits.clear();
        QTest::mouseClick(&view, Qt::LeftButton, {}, chip->mapToScene(QPointF(chip->width() / 2, chip->height() / 2)).toPoint());
        QTRY_COMPARE(backend.commits.join(QString()), QStringLiteral("see you soon "));
        QTRY_VERIFY(!findNamed(view.rootObject(), QStringLiteral("phraseChip")));
    }

    void morseLayoutKeysSignsAndShowsWhatTheyCanBecome()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setLanguage(QStringLiteral("en"));
        bridge.setAutoCapitalizationEnabled(false);
        bridge.setAutocorrectEnabled(false);
        while (!bridge.morseLayout()) bridge.cycleLetterLayout();
        bridge.setMorseTimeouts(250, 500);
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.resize(1280, 400);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        static QPointingDevice *touch = QTest::createTouchDevice();
        auto tap = [&](QQuickItem *item) {
            QVERIFY(item);
            const QPoint at = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
            QTest::touchEvent(&view, touch).press(0, at);
            QTest::qWait(40);
            QTest::touchEvent(&view, touch).release(0, at);
            QTest::qWait(40);
        };
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("morseDotKey")));
        QQuickItem *dot = findNamed(view.rootObject(), QStringLiteral("morseDotKey"));
        QQuickItem *dash = findNamed(view.rootObject(), QStringLiteral("morseDashKey"));
        QVERIFY(dot->isVisible() && dash->isVisible());
        QVERIFY(dot->height() > 2.5 * findNamed(view.rootObject(), QStringLiteral("symbolsKey"))->height());
        QQuickItem *q = findText(view.rootObject(), QStringLiteral("q"));
        QVERIFY(!q || !q->isVisible());                                          // no letter keys

        tap(dot);
        tap(dash);
        QCOMPARE(bridge.morseCode(), QStringLiteral(".-"));
        QQuickItem *strip = findNamed(view.rootObject(), QStringLiteral("morseStrip"));
        QTRY_VERIFY(strip && strip->isVisible());
        QVERIFY(findNamed(view.rootObject(), QStringLiteral("morseHint_r")));
        if (qEnvironmentVariableIsSet("TASTRA_SCREENSHOT")) view.grabWindow().save(qEnvironmentVariable("TASTRA_SCREENSHOT"));
        // The pause types the sign; a longer one ends the word.
        QTRY_COMPARE_WITH_TIMEOUT(backend.commits.join(QString()), QStringLiteral("a"), 2000);
        QVERIFY(!strip->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(backend.commits.join(QString()), QStringLiteral("a "), 2000);
        // A sign from the strip instead.
        tap(dot);
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("morseHint_i")));
        QTest::mouseClick(&view, Qt::LeftButton, {}, findNamed(view.rootObject(), QStringLiteral("morseHint_i"))
                          ->mapToScene(QPointF(10, 10)).toPoint());
        QTRY_COMPARE(backend.commits.join(QString()), QStringLiteral("a i"));
        // Backspace takes back a dot first.
        bridge.setMorseTimeouts(0, 0);
        tap(dash);
        tap(findNamed(view.rootObject(), QStringLiteral("morseBackspaceKey")));
        QVERIFY(bridge.morseCode().isEmpty());
        QCOMPARE(backend.commits.join(QString()), QStringLiteral("a i"));
        // Symbols still have their page; the Morse keys come back after it.
        bridge.toggleSymbols();
        QTRY_VERIFY(!dot->isVisible());
        bridge.toggleSymbols();
        QTRY_VERIFY(dot->isVisible());
    }

    void topRowKeyPreviewStaysInsideThePanel()
    {
        // Live report: the preview of top-row keys was cut off — Wayland cannot
        // draw above the panel surface, so the whole bubble (incl. its rounded
        // top) must stay inside, for any panel size.
        for (const QSize size : {QSize(1600, 560), QSize(1700, 420), QSize(1280, 360)}) {
            FakeBackend backend;
            Tastra::KeyboardController controller(backend);
            Tastra::KeyboardModel model;
            Tastra::KeyboardUiBridge bridge(controller, model);
            bridge.setAutoCapitalizationEnabled(false);
            QQuickView view;
            view.setResizeMode(QQuickView::SizeRootObjectToView);
            view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
            view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
            view.resize(size);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            QTest::qWait(50);
            QQuickItem *label = findText(view.rootObject(), QStringLiteral("q"));
            QVERIFY(label);
            const QPoint at = label->mapToScene(QPointF(label->width() / 2, label->height() / 2)).toPoint();
            static QPointingDevice *touch = QTest::createTouchDevice();
            QTest::touchEvent(&view, touch).press(0, at);
            QTest::qWait(80);
            QQuickItem *preview = findNamed(view.rootObject(), QStringLiteral("keyPreview"));
            QVERIFY2(preview, "a key preview is shown while pressing");
            const QRectF box = preview->mapRectToScene(QRectF(0, 0, preview->width(), preview->height()));
            QTest::touchEvent(&view, touch).release(0, at);
            QVERIFY2(box.top() >= 2.0, qPrintable(QStringLiteral("%1x%2: preview top %3 touches/leaves the panel edge")
                                                    .arg(size.width()).arg(size.height()).arg(box.top())));
        }
    }

    void holdingBackspaceKeepsDeletingUntilRelease()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QQuickItem *key = findNamed(view.rootObject(), QStringLiteral("backspaceKey"));
        QVERIFY(key);
        const QPoint at = key->mapToScene(QPointF(key->width() / 2, key->height() / 2)).toPoint();
        static QPointingDevice *touch = QTest::createTouchDevice();

        QTest::touchEvent(&view, touch).press(0, at);
        QTest::qWait(qApp->styleHints()->mousePressAndHoldInterval() + 800);
        QTest::touchEvent(&view, touch).release(0, at);
        const int whileHeld = backend.backspaces;
        QVERIFY2(whileHeld >= 6, qPrintable(QStringLiteral("only %1 deletions while held").arg(whileHeld)));
        QTest::qWait(300);
        QCOMPARE(backend.backspaces, whileHeld);                 // stops on release
    }

    void slidingLeftFromBackspaceSelectsThenDeletesOnRelease()
    {
        // Gboard gesture delete with KWin's fake input: the slide selects
        // words (Ctrl+Shift+Left per step), sliding back deselects, release
        // deletes the selection with one Backspace; nothing is deleted while
        // sliding. Without fake input it still deletes word by word.
        using Chord = QPair<QList<int>, int>;
        using namespace Tastra::EvdevKey;
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        FakeChords chords;
        bridge.setKeyChordSender(&chords);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QQuickItem *key = findNamed(view.rootObject(), QStringLiteral("backspaceKey"));
        QVERIFY(key);
        const QPoint at = key->mapToScene(QPointF(key->width() * 0.8, key->height() / 2)).toPoint();
        const int step = int(qMax(18.0, key->width() * 0.20)) + 2;
        static QPointingDevice *touch = QTest::createTouchDevice();
        auto seq = QTest::touchEvent(&view, touch, false);
        seq.press(0, at).commit();
        // Qt Quick merges touch updates within a frame: one move per frame.
        for (int i = 1; i <= 3; ++i) { seq.move(0, at - QPoint(i * step, 0)).commit(); QTest::qWait(40); }
        seq.move(0, at - QPoint(2 * step, 0)).commit();
        QTest::qWait(40);
        const Chord left{{LeftCtrl, LeftShift}, Left}, right{{LeftCtrl, LeftShift}, Right};
        QCOMPARE(chords.sent, (QList<Chord>{left, left, left, right}));
        QCOMPARE(backend.backspaces, 0);
        seq.release(0, at - QPoint(2 * step, 0)).commit();
        QTRY_COMPARE(chords.sent.last(), (Chord{{}, BackSpace}));
        QCOMPARE(backend.backspaces, 0);

        // Back to the start before lifting: nothing deleted.
        chords.sent.clear();
        seq.press(0, at).commit();
        QTest::qWait(40);
        seq.move(0, at - QPoint(step, 0)).commit();
        QTest::qWait(40);
        seq.move(0, at).commit();
        QTest::qWait(40);
        seq.release(0, at).commit();
        QTest::qWait(30);
        QCOMPARE(chords.sent, (QList<Chord>{left, right}));
        QCOMPARE(backend.backspaces, 0);

        // No fake input: word by word while sliding, as before.
        bridge.setKeyChordSender(nullptr);
        chords.sent.clear();
        seq.press(0, at).commit();
        for (int i = 1; i <= 2; ++i) { seq.move(0, at - QPoint(i * step, 0)).commit(); QTest::qWait(40); }
        seq.release(0, at - QPoint(2 * step, 0)).commit();
        QTRY_VERIFY(backend.backspaces > 0);
        QVERIFY(chords.sent.isEmpty());
    }

    void emojiAndLanguageSwitchKeysAndTheCommaMenu()
    {
        // Gboard Preferences: "Show emoji switch key" replaces the globe key
        // (and greys out "Show language switch key"); with neither, the space
        // bar takes the room. Touch and hold the comma: emoji, settings,
        // one-handed keyboard.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(findNamed(view.rootObject(), QStringLiteral("globeKey")));
        QVERIFY(!findNamed(view.rootObject(), QStringLiteral("emojiKey")));
        QQuickItem *space = findNamed(view.rootObject(), QStringLiteral("spaceKey"));
        const qreal spaceWidth = space->width();

        bridge.setShowEmojiKey(true);
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("emojiKey")));
        QVERIFY(!findNamed(view.rootObject(), QStringLiteral("globeKey")));
        QVERIFY(!bridge.showLanguageKey());
        bridge.setShowLanguageKey(true);                                   // greyed out: no effect
        QVERIFY(!findNamed(view.rootObject(), QStringLiteral("globeKey")));
        QQuickItem *emoji = findNamed(view.rootObject(), QStringLiteral("emojiKey"));
        QTest::qWait(30);
        QTest::mouseClick(&view, Qt::LeftButton, {}, emoji->mapToScene(QPointF(emoji->width() / 2, emoji->height() / 2)).toPoint());
        QTRY_COMPARE(bridge.activePanel(), QStringLiteral("emoji"));
        bridge.closePanel();

        bridge.setShowEmojiKey(false);
        bridge.setShowLanguageKey(false);
        QTRY_VERIFY(!findNamed(view.rootObject(), QStringLiteral("globeKey")));
        QTRY_VERIFY(space->width() > spaceWidth + space->height() * 0.5);
        bridge.setShowLanguageKey(true);
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("globeKey")));
        QTRY_COMPARE(space->width(), spaceWidth);
        QCOMPARE(QSettings().value(QStringLiteral("showLanguageKey")).toBool(), true);

        // The comma menu: hold, slide onto a choice, release.
        QQuickItem *comma = findNamed(view.rootObject(), QStringLiteral("commaKey"));
        QVERIFY(comma);
        static QPointingDevice *touch = QTest::createTouchDevice();
        auto choose = [&](int index) {
            const QPoint at = comma->mapToScene(QPointF(comma->width() / 2, comma->height() / 2)).toPoint();
            QTest::touchEvent(&view, touch).press(0, at);
            QTest::qWait(bridge.longPressDelay() + 120);
            QVariant origin, cell;
            QMetaObject::invokeMethod(comma, "choiceOriginX", Q_RETURN_ARG(QVariant, origin));
            QMetaObject::invokeMethod(comma, "choiceCellWidth", Q_RETURN_ARG(QVariant, cell));
            const QPoint to = comma->mapToScene(QPointF(origin.toReal() + cell.toReal() * (index + 0.5), comma->height() / 2)).toPoint();
            QTest::touchEvent(&view, touch).move(0, to);
            QTest::qWait(40);
            QTest::touchEvent(&view, touch).release(0, to);
            QTest::qWait(40);
        };
        choose(1);
        QTRY_COMPARE(bridge.activePanel(), QStringLiteral("settings"));
        bridge.closePanel();
        choose(2);
        QTRY_COMPARE(bridge.layoutMode(), QStringLiteral("right"));
        choose(2);
        QTRY_COMPARE(bridge.layoutMode(), QStringLiteral("full"));
        choose(0);
        QTRY_COMPARE(bridge.activePanel(), QStringLiteral("emoji"));
        QVERIFY(backend.commits.isEmpty());                                // nothing typed
        bridge.closePanel();
    }

    void anotherLetterLayoutIsChosenInSettingsAndKept()
    {
        // Settings -> Letter layout: QWERTY -> QWERTZ -> AZERTY -> Dvorak; the
        // keys follow, Dvorak's punctuation keys type their mark, the choice
        // is kept for the language.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setLanguage(QStringLiteral("en"));
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QCOMPARE(bridge.letterLayout(), QStringLiteral("QWERTY"));
        bridge.cycleLetterLayout();
        bridge.cycleLetterLayout();
        QCOMPARE(bridge.letterLayout(), QStringLiteral("AZERTY"));
        // Labels are capitals at the start of the field.
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("M")) || findText(view.rootObject(), QStringLiteral("m")));
        QCOMPARE(bridge.row2().size(), 10);
        bridge.cycleLetterLayout();
        QCOMPARE(bridge.letterLayout(), QStringLiteral("Dvorak"));
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("'")));
        bridge.tapLetter(QStringLiteral("p"));
        bridge.tapLetter(QStringLiteral(","));
        QCOMPARE(backend.commits.last(), QStringLiteral(","));                       // a mark, not a letter
        QCOMPARE(QSettings().value(QStringLiteral("layoutVariant/en")).toString(), QStringLiteral("dvorak"));
        Tastra::KeyboardModel again;
        Tastra::KeyboardUiBridge restarted(controller, again);
        QCOMPARE(restarted.letterLayout(), QStringLiteral("Dvorak"));
        bridge.setLanguage(QStringLiteral("ru"));
        QVERIFY(bridge.letterLayoutChoice());                                        // ЙЦУКЕН and Morse code
        QCOMPARE(bridge.letterLayout(), QString::fromUtf8("ЙЦУКЕН"));
        bridge.setLanguage(QStringLiteral("en"));
    }

    void colourThemesAndAnOwnPhoto()
    {
        // Gboard theme gallery: a colour tints the palette; an own photo
        // from the Pictures folder sits behind translucent keys, dimmed on
        // request; a photo that is gone falls back to the plain theme.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setTheme(QStringLiteral("dark"));
        QTemporaryDir pictures;
        QImage photo(320, 200, QImage::Format_RGB32);
        photo.fill(QColor(QStringLiteral("#ff8800")));
        QVERIFY(photo.save(pictures.filePath(QStringLiteral("beach.png"))));
        QFile(pictures.filePath(QStringLiteral("notes.txt"))).open(QIODevice::WriteOnly);
        bridge.setPicturesFolderForTesting(pictures.path());
        QCOMPARE(bridge.pictureFiles().size(), 1);

        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QQuickItem *root = view.rootObject();
        const QColor plain = root->property("backgroundColor").value<QColor>();
        bridge.setThemeColor(QStringLiteral("#123456"));                    // not one of the colours
        QCOMPARE(bridge.themeColor(), QString());
        bridge.setThemeColor(QStringLiteral("#e53935"));
        QTRY_VERIFY(root->property("backgroundColor").value<QColor>() != plain);
        const QColor red = root->property("backgroundColor").value<QColor>();
        QVERIFY2(red.red() > red.blue(), qPrintable(red.name()));
        QCOMPARE(root->property("accentColor").value<QColor>(), QColor(QStringLiteral("#e53935")).lighter(135));

        bridge.setThemeImage(pictures.filePath(QStringLiteral("notes.txt")));  // not a picture
        QCOMPARE(bridge.themeImage(), QString());
        bridge.setThemeImage(bridge.pictureFiles().first());
        QTRY_VERIFY(findNamed(root, QStringLiteral("themePhoto")));
        QVERIFY(root->property("keyColor").value<QColor>().alphaF() < 0.9);
        view.grabWindow();                                                    // renders
        bridge.cycleThemeImageDim();
        QCOMPARE(bridge.themeImageDim(), 20);
        QCOMPARE(QSettings().value(QStringLiteral("themeImageDim")).toInt(), 20);
        {
            Tastra::KeyboardModel again;
            Tastra::KeyboardUiBridge restarted(controller, again);
            QCOMPARE(restarted.themeImage(), bridge.themeImage());
            QCOMPARE(restarted.themeColor(), QStringLiteral("#e53935"));
        }
        QFile::remove(pictures.filePath(QStringLiteral("beach.png")));
        {
            Tastra::KeyboardModel again;
            Tastra::KeyboardUiBridge restarted(controller, again);
            QCOMPARE(restarted.themeImage(), QString());
        }
        bridge.setThemeImage(QString());
        QTRY_VERIFY(!findNamed(root, QStringLiteral("themePhoto")));
        QCOMPARE(root->property("keyColor").value<QColor>().alphaF(), 1.0);
        bridge.setThemeColor(QString());
        QTRY_COMPARE(root->property("backgroundColor").value<QColor>(), plain);

        // Gboard "Gradients": behind translucent keys; a gradient and a photo
        // exclude each other.
        bridge.setThemeGradient(QStringLiteral("rainbow"));                    // not one of them
        QCOMPARE(bridge.themeGradient(), QString());
        bridge.setThemeGradient(QStringLiteral("ocean"));
        QTRY_VERIFY(findNamed(root, QStringLiteral("themeGradient")));
        QVERIFY(root->property("keyColor").value<QColor>().alphaF() < 0.9);
        QImage again(64, 64, QImage::Format_RGB32);
        again.fill(Qt::darkGreen);
        QVERIFY(again.save(pictures.filePath(QStringLiteral("hill.png"))));
        bridge.setThemeImage(pictures.filePath(QStringLiteral("hill.png")));
        QCOMPARE(bridge.themeGradient(), QString());
        QTRY_VERIFY(!findNamed(root, QStringLiteral("themeGradient")));
        bridge.setThemeGradient(QStringLiteral("dusk"));
        QCOMPARE(bridge.themeImage(), QString());
        QCOMPARE(QSettings().value(QStringLiteral("themeGradient")).toString(), QStringLiteral("dusk"));
        bridge.setThemeGradient(QString());
        QTRY_VERIFY(!findNamed(root, QStringLiteral("themeGradient")));
    }

    void resizingByDraggingTheTopBar()
    {
        // Gboard "Resize" (toolbar): drag the bar at the top; the keyboard
        // grows by what the finger moved, measured from the fixed bottom
        // edge; Reset goes back, ✓ ends; the keys do not type meanwhile.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setKeyScale(1.0);
        QVERIFY(bridge.toolbarActionIds().contains(QStringLiteral("resize")));
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QQuickItem *root = view.rootObject();
        const qreal keyHeight = root->property("keyHeight").toReal();
        root->setProperty("resizing", true);
        QTRY_VERIFY(findNamed(root, QStringLiteral("resizeFrame")));
        QQuickItem *drag = findNamed(root, QStringLiteral("resizeDrag"));
        QVERIFY(drag);
        const QPoint start = drag->mapToScene(QPointF(drag->width() / 2, drag->height() / 2)).toPoint();
        // KWin keeps the panel's bottom on the screen's bottom, so as the
        // keyboard grows its top moves up under the finger: the finger's
        // window position is its distance from the bottom, from the top of
        // the current height. The finger goes 40 px up the screen.
        const int fromBottom = int(root->height()) - start.y();
        auto at = [&](int up) { return QPoint(start.x(), int(root->height()) - fromBottom - up); };
        QTest::mousePress(&view, Qt::LeftButton, {}, at(0));
        for (int i = 1; i <= 4; ++i) { QTest::mouseMove(&view, at(10 * i)); QTest::qWait(20); }
        QTest::mouseRelease(&view, Qt::LeftButton, {}, at(40));
        QVERIFY2(qAbs(bridge.keyScale() - (keyHeight + 10.0) / keyHeight) < 0.01, qPrintable(QString::number(bridge.keyScale())));
        QTRY_VERIFY(root->property("keyHeight").toReal() > keyHeight + 9);

        // A tap on a letter under the frame types nothing.
        QQuickItem *letter = findText(root, QStringLiteral("Q"));
        if (!letter) letter = findText(root, QStringLiteral("q"));
        QVERIFY(letter);
        QTest::mouseClick(&view, Qt::LeftButton, {}, letter->mapToScene(QPointF(letter->width() / 2, letter->height() / 2)).toPoint());
        QVERIFY(backend.commits.isEmpty());

        QQuickItem *reset = findNamed(root, QStringLiteral("resizeReset"));
        QTest::mouseClick(&view, Qt::LeftButton, {}, reset->mapToScene(QPointF(reset->width() / 2, reset->height() / 2)).toPoint());
        QTRY_COMPARE(bridge.keyScale(), 1.0);
        QQuickItem *done = findNamed(root, QStringLiteral("resizeDone"));
        QTest::mouseClick(&view, Qt::LeftButton, {}, done->mapToScene(QPointF(done->width() / 2, done->height() / 2)).toPoint());
        QTRY_VERIFY(!findNamed(root, QStringLiteral("resizeFrame")));
        bridge.setKeyScale(3.0);
        QCOMPARE(bridge.keyScale(), 1.4);                                       // bounded
        bridge.setKeyScale(1.0);
    }

    void toolbarFeaturesAreChosenInSettings()
    {
        // Settings -> Toolbar: switch "One-handed mode" on, move it up; the
        // toolbar shows ✋ first, and it switches the one-handed keyboard.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto centre = [&](QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };
        auto scrollTo = [&](QQuickItem *item) {
            QQuickItem *flick = item->parentItem();
            while (flick && !flick->property("contentY").isValid()) flick = flick->parentItem();
            QVERIFY(flick);
            const QPointF inContent = item->mapToItem(flick->property("contentItem").value<QQuickItem *>(), QPointF(0, 0));
            flick->setProperty("contentY", qMax(0.0, inContent.y() - 20));
            QTest::qWait(80);
        };
        bridge.activateToolbarAction(QStringLiteral("settings"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("toolbarShown_one-handed")));
        QQuickItem *shown = findNamed(view.rootObject(), QStringLiteral("toolbarShown_one-handed"));
        scrollTo(shown);
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(shown));
        QTRY_VERIFY(bridge.isToolbarItemShown(QStringLiteral("one-handed")));
        for (int i = 0; i < 5; ++i) {
            QQuickItem *up = findNamed(view.rootObject(), QStringLiteral("toolbarUp_one-handed"));
            scrollTo(up);
            if (!up->isEnabled()) break;
            QTest::mouseClick(&view, Qt::LeftButton, {}, centre(up));
            QTest::qWait(30);
        }
        QCOMPARE(bridge.toolbarActionIds().first(), QStringLiteral("one-handed"));
        bridge.closePanel();
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("✋")));
        QQuickItem *hand = findText(view.rootObject(), QStringLiteral("✋"))->parentItem();
        QTest::qWait(50);
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(hand));
        QTRY_COMPARE(bridge.layoutMode(), QStringLiteral("right"));
    }

    void touchPointInsideTheKeyReachesCorrection()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        bridge.addWordToDictionary(QStringLiteral("bat"));
        bridge.addWordToDictionary(QStringLiteral("bet"));
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(100);
        auto keyOf = [&](const QString &label) { return findText(view.rootObject(), label)->parentItem(); };
        auto tapAt = [&](QQuickItem *key, qreal fx, qreal fy) {
            QTest::mouseClick(&view, Qt::LeftButton, {}, key->mapToScene(QPointF(key->width() * fx, key->height() * fy)).toPoint());
            QTest::qWait(30);
        };
        auto typeBst = [&](qreal sx, qreal sy) {
            tapAt(keyOf(QStringLiteral("b")), 0.5, 0.5);
            tapAt(keyOf(QStringLiteral("s")), sx, sy);
            tapAt(keyOf(QStringLiteral("t")), 0.5, 0.5);
        };
        // The literal "bst" stays first (kept/saveable); corrections follow.
        auto before = [&](const QString &a, const QString &b) {
            const QStringList s = bridge.suggestions();
            return s.value(0) == QStringLiteral("bst") && s.contains(a) && (!s.contains(b) || s.indexOf(a) < s.indexOf(b));
        };
        typeBst(0.92, 0.10);                               // top-right edge of s -> e side
        QTRY_VERIFY2(before(QStringLiteral("bet"), QStringLiteral("bat")), qPrintable(bridge.suggestions().join(',')));
        for (int i = 0; i < 3; ++i) bridge.backspace();
        typeBst(0.08, 0.55);                               // left edge of s -> a side
        QTRY_VERIFY2(before(QStringLiteral("bat"), QStringLiteral("bet")), qPrintable(bridge.suggestions().join(',')));
    }

    void hideButtonHidesTheKeyboardLikeGboard()
    {
        struct FakeHider final : Tastra::KeyboardHider {
            int calls = 0;
            void hideKeyboard() override { ++calls; }
        } hider;
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setKeyboardHider(&hider);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QQuickItem *button = findNamed(view.rootObject(), QStringLiteral("hideKeyboardButton"));
        QVERIFY(button);
        QTest::qWait(50);
        QTest::mouseClick(&view, Qt::LeftButton, {}, button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTRY_COMPARE(hider.calls, 1);

        // Without a hider (not on Plasma) there is no button.
        Tastra::KeyboardUiBridge plain(controller, model);
        QQuickView other;
        other.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &plain);
        other.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        QVERIFY(!findNamed(other.rootObject(), QStringLiteral("hideKeyboardButton")));
    }

    void copiedImagesShowInTheStripAndThePanel()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        FakeChords chords;
        bridge.setKeyChordSender(&chords);
        bridge.clearClipboardHistory();
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        bridge.setQmlEngine(view.engine());                       // the image provider
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.resize(1280, 420);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        // A screenshot is copied in another application.
        QImage shot(160, 90, QImage::Format_RGB32);
        shot.fill(QColor(30, 120, 220));
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        shot.save(&buffer, "PNG");
        auto *mime = new QMimeData;
        mime->setData(QStringLiteral("image/png"), png);
        QGuiApplication::clipboard()->setMimeData(mime);
        QTRY_VERIFY(!bridge.clipboardImageSuggestion().isEmpty());
        QQuickItem *chip = findNamed(view.rootObject(), QStringLiteral("clipboardImageChip"));
        QTRY_VERIFY(chip && chip->isVisible());
        QQuickItem *thumb = nullptr;
        for (QQuickItem *child : chip->childItems()) {
            if (child->property("status").isValid() && child->property("source").toString().startsWith(QStringLiteral("image://")))
                thumb = child;
        }
        QVERIFY(thumb);
        QTRY_COMPARE(thumb->property("status").toInt(), 1);         // Image.Ready: decoded by the provider
        QTest::qWait(100);
        if (qEnvironmentVariableIsSet("TASTRA_SCREENSHOT")) view.grabWindow().save(qEnvironmentVariable("TASTRA_SCREENSHOT"));
        QTest::mouseClick(&view, Qt::LeftButton, {}, chip->mapToScene(QPointF(chip->width() / 2, chip->height() / 2)).toPoint());
        QTRY_COMPARE(chords.sent.size(), 1);                        // the application pastes it
        QTRY_VERIFY(!chip->isVisible());

        // In the panel: the picture, tap pastes it again.
        QGuiApplication::clipboard()->setText(QStringLiteral("later text"));
        bridge.activateToolbarAction(QStringLiteral("clipboard"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("clipboardImageItem")));
        QQuickItem *item = nullptr;
        QTRY_VERIFY((item = [&]() -> QQuickItem * {
            QList<QQuickItem *> stack{view.rootObject()};
            while (!stack.isEmpty()) {
                QQuickItem *it = stack.takeLast();
                if (it->objectName() == QLatin1String("clipboardImageItem") && it->isVisible()) return it;
                stack += it->childItems();
            }
            return nullptr;
        }()));
        QTRY_COMPARE(item->property("status").toInt(), 1);
        if (qEnvironmentVariableIsSet("TASTRA_SCREENSHOT2")) view.grabWindow().save(qEnvironmentVariable("TASTRA_SCREENSHOT2"));
        QTest::mouseClick(&view, Qt::LeftButton, {}, item->mapToScene(QPointF(item->width() / 4, 6)).toPoint());   // its visible top
        QTRY_COMPARE(chords.sent.size(), 2);
        QCOMPARE(QGuiApplication::clipboard()->mimeData()->data(QStringLiteral("image/png")), png);   // back on the clipboard
        QVERIFY(backend.commits.isEmpty());
        bridge.setKeyChordSender(nullptr);
        bridge.clearClipboardHistory();
    }

    void clipboardItemsArePinnedWithALongPress()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.clearClipboardHistory();
        QGuiApplication::clipboard()->setText(QStringLiteral("keep me"));   // bridge listens to dataChanged
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        bridge.activateToolbarAction(QStringLiteral("clipboard"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("clipboardItem")));
        QTest::qWait(100);
        QQuickItem *item = findNamed(view.rootObject(), QStringLiteral("clipboardItem"));   // the list entry
        const QPoint at = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        QSignalSpy changed(&bridge, &Tastra::KeyboardUiBridge::clipboardChanged);
        QTest::mousePress(&view, Qt::LeftButton, {}, at);
        QTest::qWait(qApp->styleHints()->mousePressAndHoldInterval() + 200);
        QTest::mouseRelease(&view, Qt::LeftButton, {}, at);
        QTRY_VERIFY(bridge.isClipboardPinned(QStringLiteral("keep me")));
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("\U0001F4CC keep me")));
        QVERIFY(backend.commits.isEmpty());                    // a long press does not paste

        // A tap on a non-interactive part of the open panel (the header that
        // shows the current clipboard; a key lies underneath it) must not
        // reach that key — first live symptom: "&" typed through the panel.
        QQuickItem *header = findText(view.rootObject(), QStringLiteral("keep me"));   // list shows "📌 keep me" now
        QVERIFY(header);
        QTest::mouseClick(&view, Qt::LeftButton, {}, header->mapToScene(QPointF(header->width() / 2, header->height() / 2)).toPoint());
        QTest::qWait(50);
        QVERIFY2(backend.commits.isEmpty(), qPrintable(backend.commits.join(',')));
    }

    void settingsAreGroupedLikeGboard()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        bridge.activateToolbarAction(QStringLiteral("settings"));
        const QStringList sections = {QStringLiteral("Languages"), QStringLiteral("Preferences"), QStringLiteral("Theme"), QStringLiteral("Text correction"),
                                      QStringLiteral("Glide typing"), QStringLiteral("Clipboard"), QStringLiteral("Dictionary"),
                                      QStringLiteral("Advanced")};
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("settingsSection_Languages")));
        auto yOf = [&](QQuickItem *item) { return item->mapToScene(QPointF(0, 0)).y(); };
        QList<qreal> sectionY;
        for (const QString &name : sections) {
            QQuickItem *header = findNamed(view.rootObject(), QStringLiteral("settingsSection_") + name);
            QVERIFY2(header, qPrintable(name));
            sectionY.append(yOf(header));
            if (sectionY.size() > 1) QVERIFY2(sectionY.last() > sectionY.at(sectionY.size() - 2), qPrintable(name));
        }
        auto under = [&](const QString &label, const QString &section) {
            QQuickItem *item = findText(view.rootObject(), label);
            if (!item) return false;
            const qreal y = yOf(item);
            const int s = sections.indexOf(section);
            return y > sectionY.at(s) && (s + 1 >= sectionY.size() || y < sectionY.at(s + 1));
        };
        QVERIFY(under(QStringLiteral("Deutsch"), QStringLiteral("Languages")));
        QVERIFY(under(QStringLiteral("Number row"), QStringLiteral("Preferences")));
        // Toggling a language in settings takes it out of the globe rotation.
        QQuickItem *toggle = findNamed(view.rootObject(), QStringLiteral("languageToggle_de"));
        QVERIFY(toggle);
        QTest::qWait(100);
        QTest::mouseClick(&view, Qt::LeftButton, {}, toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint());
        QTRY_VERIFY(!bridge.isLanguageEnabled(QStringLiteral("de")));
        QVERIFY(!bridge.languageCodes().contains(QStringLiteral("de")));
        bridge.setLanguageEnabled(QStringLiteral("de"), true);
        QVERIFY(under(QStringLiteral("Key borders"), QStringLiteral("Theme")));
        QVERIFY(under(QStringLiteral("Autocorrect"), QStringLiteral("Text correction")));
        QVERIFY(under(QStringLiteral("Double-space period"), QStringLiteral("Text correction")));
        QVERIFY(under(QStringLiteral("Gesture trail"), QStringLiteral("Glide typing")));
        QVERIFY(under(QStringLiteral("Clipboard history"), QStringLiteral("Clipboard")));
        QVERIFY(under(QStringLiteral("Local learning"), QStringLiteral("Dictionary")));
        QVERIFY(under(QStringLiteral("Underline word while typing"), QStringLiteral("Advanced")));
    }

    void glideAcrossTheSplitGapFindsTheWord()
    {
        // The decoder works on the real key centres, so a glide over both
        // halves of the split keyboard ("heart": h right, e/a/r/t left)
        // decodes like on the full layout. The bundled word list is needed:
        // "heart" is not one of the built-in core words.
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(":/tastra/frequency")});
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        QVERIFY(bridge.waitForDictionaryForTesting(10000));
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.resize(1600, 560);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        bridge.setLayoutMode(QStringLiteral("split"));
        QTest::qWait(150);
        auto centreOf = [&](const QString &label) {
            QQuickItem *key = findText(view.rootObject(), label)->parentItem();
            return key->mapToScene(QPointF(key->width() / 2, key->height() / 2));
        };
        static QPointingDevice *touch = QTest::createTouchDevice();
        const QList<QPointF> corners = {centreOf(QStringLiteral("h")), centreOf(QStringLiteral("e")),
                                        centreOf(QStringLiteral("a")), centreOf(QStringLiteral("r")),
                                        centreOf(QStringLiteral("t"))};
        QTest::touchEvent(&view, touch).press(0, corners.first().toPoint());
        for (int i = 1; i < corners.size(); ++i) {
            for (int step = 1; step <= 8; ++step) {
                const QPointF at = corners.at(i - 1) + (corners.at(i) - corners.at(i - 1)) * (step / 8.0);
                QTest::touchEvent(&view, touch).move(0, at.toPoint());
                QTest::qWait(8);                                   // a finger moves at ~120 Hz, not all at once
            }
        }
        QTest::touchEvent(&view, touch).release(0, corners.last().toPoint());
        QTRY_COMPARE(backend.commits.value(0), QStringLiteral("heart"));
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void splitLayoutForThumbTypingLikeGboardOnTablets()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.resize(1600, 560);                                   // landscape tablet
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        bridge.setLayoutMode(QStringLiteral("split"));
        QTest::qWait(150);
        auto box = [&](const QString &label) {
            QQuickItem *key = findText(view.rootObject(), label)->parentItem();
            return key->mapRectToScene(QRectF(0, 0, key->width(), key->height()));
        };
        const qreal mid = view.width() / 2.0;
        const QRectF t = box(QStringLiteral("t"));
        const QRectF y = box(QStringLiteral("y"));
        QVERIFY2(t.right() < mid && y.left() > mid, "t on the left half, y on the right half");
        QVERIFY2(y.left() - t.right() > view.width() * 0.15, qPrintable(QStringLiteral("gap %1").arg(y.left() - t.right())));
        const QRectF g = box(QStringLiteral("g"));
        const QRectF h = box(QStringLiteral("h"));
        QVERIFY(g.right() < mid && h.left() > mid);                // asdfg | hjkl
        QQuickItem *space = findText(view.rootObject(), QStringLiteral("English"));
        QVERIFY(space);
        const QRectF s = space->parentItem()->mapRectToScene(QRectF(0, 0, space->parentItem()->width(), space->parentItem()->height()));
        QVERIFY2(s.left() < mid && s.right() > mid, "the space bar stays reachable from both halves");
        // ...and spans the gap: its ends meet the inner edges of both halves.
        const qreal slack = view.width() * 0.03;
        QVERIFY2(s.left() <= t.right() + slack && s.right() >= y.left() - slack,
                 qPrintable(QStringLiteral("space %1..%2, halves end %3 / start %4").arg(s.left()).arg(s.right()).arg(t.right()).arg(y.left())));
        // Touch follows the moved keys.
        QTest::mouseClick(&view, Qt::LeftButton, {}, y.center().toPoint());
        QTRY_COMPARE(backend.commits.value(0), QStringLiteral("y"));
        // Number fields keep the plain number pad.
        bridge.setContentType(0, 4);
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("numpadKey_5")));
    }

    void longPressOnAnEmojiOffersSkinTones()
    {
        Tastra::EmojiCatalog::setSystemDataPathForTesting(QStringLiteral(TASTRA_TEST_DATA "/emoji-system/emoji-test.txt"));
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        Tastra::EmojiCatalog::setSystemDataPathForTesting(QString());
        QCOMPARE(bridge.emojiSkinTones(QStringLiteral("👍")).size(), 5);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        bridge.activateToolbarAction(QStringLiteral("emoji"));
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("👍")));
        QTest::qWait(100);
        QQuickItem *tile = findText(view.rootObject(), QStringLiteral("👍"));
        const QPoint at = tile->mapToScene(QPointF(tile->width() / 2, tile->height() / 2)).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, {}, at);
        QTest::qWait(qApp->styleHints()->mousePressAndHoldInterval() + 200);
        QTest::mouseRelease(&view, Qt::LeftButton, {}, at);
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("emojiTonePicker")));
        QVERIFY(backend.commits.isEmpty());                         // the long press itself inserts nothing
        QQuickItem *dark = findText(view.rootObject(), QStringLiteral("👍🏿"));
        QVERIFY(dark);
        QTest::qWait(50);
        QTest::mouseClick(&view, Qt::LeftButton, {}, dark->mapToScene(QPointF(dark->width() / 2, dark->height() / 2)).toPoint());
        QTRY_COMPARE(backend.commits, QStringList({QStringLiteral("👍🏿")}));
        QTRY_VERIFY(!findNamed(view.rootObject(), QStringLiteral("emojiTonePicker")));
    }

    void emojiSearchTypesOnTheLettersAndShowsResults()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto click = [&](QQuickItem *item) {
            QTest::mouseClick(&view, Qt::LeftButton, {}, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            QTest::qWait(30);
        };
        bridge.activateToolbarAction(QStringLiteral("emoji"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("emojiSearchField")));
        QTest::qWait(100);
        click(findNamed(view.rootObject(), QStringLiteral("emojiSearchField")));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("emojiSearchBar")));
        for (const QString &letter : {QStringLiteral("p"), QStringLiteral("i"), QStringLiteral("z"), QStringLiteral("z"), QStringLiteral("a")}) {
            QQuickItem *label = findText(view.rootObject(), letter);
            QVERIFY2(label, qPrintable(letter));
            click(label->parentItem());
        }
        QTRY_COMPARE(findNamed(view.rootObject(), QStringLiteral("emojiSearchText"))->property("text").toString(), QStringLiteral("pizza"));
        QVERIFY(backend.commits.isEmpty());
        QTRY_VERIFY(findText(findNamed(view.rootObject(), QStringLiteral("emojiSearchBar")), QStringLiteral("🍕")));
        click(findText(findNamed(view.rootObject(), QStringLiteral("emojiSearchBar")), QStringLiteral("🍕")));
        QTRY_COMPARE(backend.commits, QStringList{QStringLiteral("🍕")});
        QTRY_VERIFY(!findNamed(view.rootObject(), QStringLiteral("emojiSearchBar")));
    }

    void textEditingPanelCopiesCutsAndPastesTheSelection()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        FakeClipboard clipboard;
        bridge.setSystemClipboard(&clipboard);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto click = [&](QQuickItem *item) {
            QTest::mouseClick(&view, Qt::LeftButton, {}, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            QTest::qWait(30);
        };
        bridge.activateToolbarAction(QStringLiteral("text-editing"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("copyButton")));
        QQuickItem *copy = findNamed(view.rootObject(), QStringLiteral("copyButton"));
        QQuickItem *cut = findNamed(view.rootObject(), QStringLiteral("cutButton"));
        QQuickItem *paste = findNamed(view.rootObject(), QStringLiteral("pasteButton"));
        QVERIFY(!copy->isEnabled() && !cut->isEnabled() && !paste->isEnabled());

        bridge.setSurroundingText(QStringLiteral("Hello world"), 11, 6);    // "world" selected in the app
        QTRY_VERIFY(copy->isEnabled());
        click(copy);
        QCOMPARE(clipboard.text(), QStringLiteral("world"));
        QTRY_VERIFY(paste->isEnabled());
        click(cut);
        QCOMPARE(backend.backspaces, 1);
        QTRY_VERIFY(!copy->isEnabled());
        click(paste);
        QVERIFY(backend.commits.join(QString()).endsWith(QStringLiteral("world")));
        QVERIFY(!findNamed(view.rootObject(), QStringLiteral("selectButton")));      // no shortcuts: hidden

        // With KWin's fake input: Select and Select all appear; Select lights
        // up and the arrows then extend the selection.
        FakeChords chords;
        bridge.setKeyChordSender(&chords);
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("selectButton")));
        QQuickItem *select = findNamed(view.rootObject(), QStringLiteral("selectButton"));
        QQuickItem *selectAll = findNamed(view.rootObject(), QStringLiteral("selectAllButton"));
        QVERIFY(!copy->isEnabled());                                         // the app reports nothing selected
        bridge.setSurroundingText(QStringLiteral("Hello "), 5, 0);           // "Hello" selected
        QTRY_VERIFY(copy->isEnabled());                                      // the app copies its own selection
        QTest::qWait(50);                                                    // the grid lays the new buttons out
        const QPointF selectAt = select->mapToScene(QPointF(select->width() / 2, select->height() / 2));
        QVERIFY2(selectAt.y() < view.height() && selectAt.y() > 0, qPrintable(QString::number(selectAt.y()) + QLatin1Char('/') + QString::number(view.height())));
        click(select);
        QVERIFY(bridge.selectMode());
        QVERIFY(select->property("accent").toBool());
        click(selectAll);
        QCOMPARE(chords.sent.last(), (QPair<QList<int>, int>({Tastra::EvdevKey::LeftCtrl}, Tastra::EvdevKey::A)));
        click(copy);
        QCOMPARE(chords.sent.last(), (QPair<QList<int>, int>({Tastra::EvdevKey::LeftCtrl}, Tastra::EvdevKey::C)));
        bridge.setKeyChordSender(nullptr);
        bridge.setSystemClipboard(nullptr);
    }

    void interfaceTextsFollowTheChosenLanguage()
    {
        // Settings -> Interface language: the panel's texts change at once
        // (qsTr through the installed table, re-evaluated by the engine).
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        bridge.setQmlEngine(view.engine());
        bridge.setUiLanguage(QStringLiteral("en"));
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        bridge.activateToolbarAction(QStringLiteral("settings"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("settingsSection_Languages")));
        QQuickItem *header = findNamed(view.rootObject(), QStringLiteral("settingsSection_Languages"));
        QQuickItem *button = findNamed(view.rootObject(), QStringLiteral("uiLanguageButton"));
        QVERIFY(button);
        QCOMPARE(header->property("text").toString(), QStringLiteral("Languages"));
        QCOMPARE(button->property("label").toString(), QStringLiteral("English"));

        bridge.setUiLanguage(QStringLiteral("de"));
        QTRY_COMPARE(header->property("text").toString(), QStringLiteral("Sprachen"));
        QCOMPARE(button->property("label").toString(), QStringLiteral("Deutsch"));
        QVERIFY(findText(view.rootObject(), QStringLiteral("Zahlenreihe")));
        QVERIFY(findText(view.rootObject(), QStringLiteral("Sprache der Oberfläche")));

        bridge.setUiLanguage(QStringLiteral("uk"));
        QTRY_COMPARE(header->property("text").toString(), QStringLiteral("Мови"));
        QVERIFY(findText(view.rootObject(), QStringLiteral("Ряд цифр")));
        bridge.setUiLanguage(QStringLiteral("en"));
        QTRY_COMPARE(header->property("text").toString(), QStringLiteral("Languages"));
    }

    void addWordFromSettingsTypedOnTheKeys()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto click = [&](QQuickItem *item) {
            QTest::mouseClick(&view, Qt::LeftButton, {}, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            QTest::qWait(30);
        };
        bridge.activateToolbarAction(QStringLiteral("settings"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("addWordButton")));
        QQuickItem *add = findNamed(view.rootObject(), QStringLiteral("addWordButton"));
        // Scroll the settings so the button is on screen, then tap it.
        QQuickItem *flick = findNamed(view.rootObject(), QStringLiteral("settingsFlick"));
        const qreal y = add->mapToItem(flick->property("contentItem").value<QQuickItem *>(), QPointF(0, 0)).y();
        flick->setProperty("contentY", qMax(0.0, y - 20));
        QTest::qWait(50);
        click(add);
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("wordEntryBar")));
        for (const QString &letter : {QStringLiteral("t"), QStringLiteral("a"), QStringLiteral("s"), QStringLiteral("t"),
                                      QStringLiteral("r"), QStringLiteral("a")}) {
            click(findText(view.rootObject(), letter)->parentItem());
        }
        QTRY_COMPARE(findNamed(view.rootObject(), QStringLiteral("wordEntryText"))->property("text").toString(), QStringLiteral("tastra"));
        click(findNamed(view.rootObject(), QStringLiteral("wordEntryConfirm")));    // the word
        QTRY_COMPARE(bridge.wordEntryStep(), QStringLiteral("shortcut"));
        click(findNamed(view.rootObject(), QStringLiteral("wordEntryConfirm")));    // no shortcut
        QTRY_VERIFY(!findNamed(view.rootObject(), QStringLiteral("wordEntryBar")));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("userWord_tastra")));   // back in settings
        QVERIFY(backend.commits.isEmpty());
        bridge.removeWordFromDictionary(QStringLiteral("tastra"));
    }

    void emoticonTabShowsWideTilesAndInsertsTheFace()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        bridge.activateToolbarAction(QStringLiteral("emoji"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("emojiCategory_:-)")));
        QTest::qWait(100);
        QQuickItem *chip = findNamed(view.rootObject(), QStringLiteral("emojiCategory_:-)"));
        QTest::mouseClick(&view, Qt::LeftButton, {}, chip->mapToScene(QPointF(chip->width() / 2, chip->height() / 2)).toPoint());
        // ";-)" sits in the first row (":-)" would also match the chip label).
        const QString wink = QStringLiteral(";-)");
        QTRY_VERIFY(findText(view.rootObject(), wink));
        QTest::qWait(100);
        QQuickItem *face = findText(view.rootObject(), wink);
        QQuickItem *tile = face->parentItem();
        // Faces get wide tiles (an emoji tile is 58 px): "¯\\_(ツ)_/¯" fits.
        QVERIFY2(tile->width() >= 100, qPrintable(QString::number(tile->width())));
        QVERIFY(face->width() <= tile->width());
        QTest::mouseClick(&view, Qt::LeftButton, {}, tile->mapToScene(QPointF(tile->width() / 2, tile->height() / 2)).toPoint());
        QTRY_COMPARE(backend.commits, QStringList{wink});
        QVERIFY(!bridge.recentEmojis().contains(wink));
    }

    void justCopiedTextIsOfferedForPasting()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QGuiApplication::clipboard()->setText(QStringLiteral("https://example.org/a"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("clipboardChip")));
        QTest::qWait(50);
        QQuickItem *chip = findNamed(view.rootObject(), QStringLiteral("clipboardChip"));
        QTest::mouseClick(&view, Qt::LeftButton, {}, chip->mapToScene(QPointF(chip->width() / 2, chip->height() / 2)).toPoint());
        QTRY_COMPARE(backend.commits.join(QString()), QStringLiteral("https://example.org/a"));
        QTRY_VERIFY(!findNamed(view.rootObject(), QStringLiteral("clipboardChip")));    // used once

        // Gboard "paste sections": the address alone, next to the whole text.
        backend.commits.clear();
        QGuiApplication::clipboard()->setText(QStringLiteral("Write to anna@example.org today"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("clipboardPart0")));
        QTest::qWait(50);
        QQuickItem *part = findNamed(view.rootObject(), QStringLiteral("clipboardPart0"));
        QQuickItem *whole = findNamed(view.rootObject(), QStringLiteral("clipboardChip"));
        QVERIFY(whole);
        QVERIFY(whole->mapToScene(QPointF(whole->width(), 0)).x() <= part->mapToScene(QPointF(0, 0)).x());
        QVERIFY(part->mapToScene(QPointF(part->width(), 0)).x() <= view.width());
        QTest::mouseClick(&view, Qt::LeftButton, {}, part->mapToScene(QPointF(part->width() / 2, part->height() / 2)).toPoint());
        QTRY_COMPARE(backend.commits.join(QString()), QStringLiteral("anna@example.org"));
        QTRY_VERIFY(!findNamed(view.rootObject(), QStringLiteral("clipboardPart0")));

        // Typing dismisses a fresh offer.
        QGuiApplication::clipboard()->setText(QStringLiteral("second"));
        QTRY_VERIFY(findNamed(view.rootObject(), QStringLiteral("clipboardChip")));
        bridge.tapLetter(QStringLiteral("a"));
        bridge.space();
        QTRY_VERIFY(!findNamed(view.rootObject(), QStringLiteral("clipboardChip")));
    }

    void secondSymbolPageAndSymbolExtras()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto centre = [&](QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };
        bridge.setLanguage(QStringLiteral("ru"));
        bridge.toggleSymbols();
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("₽")));          // the language's currency
        QQuickItem *pageKey = findNamed(view.rootObject(), QStringLiteral("symbolPageKey"));
        QVERIFY(pageKey);
        QTest::qWait(50);
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(pageKey));
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("<")));
        QTest::qWait(50);
        // The third row (page key + symbols + Backspace) is as wide as the others.
        {
            auto rowOf = [&](const QString &label) { return findText(view.rootObject(), label)->parentItem()->parentItem(); };
            const QRectF r1 = rowOf(QStringLiteral("~"))->mapRectToScene(QRectF(0, 0, rowOf(QStringLiteral("~"))->width(), 1));
            const QRectF r3 = rowOf(QStringLiteral("<"))->mapRectToScene(QRectF(0, 0, rowOf(QStringLiteral("<"))->width(), 1));
            QVERIFY2(qAbs(r3.width() - r1.width()) < 4.0, qPrintable(QStringLiteral("row1 %1 vs row3 %2").arg(r1.width()).arg(r3.width())));
        }
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(findText(view.rootObject(), QStringLiteral("<"))));
        QTRY_COMPARE(backend.commits.value(0), QStringLiteral("<"));
        // Back to page 1, long-press the quote for guillemets.
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(findNamed(view.rootObject(), QStringLiteral("symbolPageKey"))));
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("\"")));
        QTest::qWait(50);
        static QPointingDevice *touch = QTest::createTouchDevice();
        const QPoint q = centre(findText(view.rootObject(), QStringLiteral("\"")));
        QTest::touchEvent(&view, touch).press(0, q);
        QTest::qWait(bridge.longPressDelay() + 150);
        QTRY_VERIFY(findText(view.rootObject(), QStringLiteral("«")));          // picker open
        QTest::touchEvent(&view, touch).release(0, q);                          // first choice
        QTRY_COMPARE(backend.commits.value(1), QStringLiteral("«"));
    }

    void caseChangesDoNotRecreateLetterKeys()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);

        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("keyboardBridge"), &bridge);
        view.setSource(QUrl::fromLocalFile(QStringLiteral(TASTRA_MAIN_QML)));
        QCOMPARE(view.status(), QQuickView::Ready);

        const auto before = letterKeys(view.rootObject());
        QVERIFY2(before.size() >= 26, "expected the EN letter rows to be instantiated");

        // Sentence start -> first letter consumes auto-uppercase (case flip),
        // Shift toggles case again. None of this changes the layout.
        bridge.tapLetter(QStringLiteral("h"));
        bridge.shift();
        bridge.tapLetter(QStringLiteral("i"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();

        for (const auto &key : before) {
            QVERIFY2(!key.isNull(), "a letter key was destroyed by a case-only change");
        }
        QCOMPARE(letterKeys(view.rootObject()).size(), before.size());
        QCOMPARE(backend.commits, QStringList({QStringLiteral("H"), QStringLiteral("I")}));

        // Sanity check of the probe itself: a real layout change must rebuild.
        bridge.setLanguage(QStringLiteral("de"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        bool anyDestroyed = false;
        for (const auto &key : before) anyDestroyed = anyDestroyed || key.isNull();
        QVERIFY2(anyDestroyed, "probe could not observe delegate recreation");
    }
};

QTEST_MAIN(QmlKeyboardTest)
#include "tst_qmlkeyboard.moc"
