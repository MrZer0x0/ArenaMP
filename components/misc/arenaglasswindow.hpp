#ifndef OPENMW_COMPONENTS_MISC_ARENAGLASSWINDOW_H
#define OPENMW_COMPONENTS_MISC_ARENAGLASSWINDOW_H

#include <QApplication>
#include <QLabel>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSizeGrip>
#include <QToolButton>
#include <QWindow>
#include <QWizard>
#include <QLayout>
#include <QPointer>
#include <QLibrary>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <qt_windows.h>
#endif

namespace ArenaUi
{
    // No private Windows composition APIs. Unsupported systems retain the
    // opaque painted material. No new link dependency on dwmapi is required.
    inline bool requestSystemGlass(QWidget* window)
    {
#ifdef Q_OS_WIN
        static QLibrary dwm(QStringLiteral("dwmapi"));
        using SetAttribute = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        const auto setAttribute = reinterpret_cast<SetAttribute>(dwm.resolve("DwmSetWindowAttribute"));
        if (setAttribute)
        {
            const HWND hwnd = reinterpret_cast<HWND>(window->winId());
            const int dark = 1, rounded = 2, acrylic = 3;
            setAttribute(hwnd, 20, &dark, sizeof(dark));
            setAttribute(hwnd, 33, &rounded, sizeof(rounded));
            // DWMWA_SYSTEMBACKDROP_TYPE / DWMSBT_TRANSIENTWINDOW (Win11 22H2+).
            return SUCCEEDED(setAttribute(hwnd, 38, &acrylic, sizeof(acrylic)));
        }
#else
        Q_UNUSED(window)
#endif
        return false;
    }

    class GlassTitleBar : public QWidget
    {
        QWidget* mWindow;
        QPoint mOffset;
        bool mDragging = false;
    public:
        explicit GlassTitleBar(QWidget* window) : QWidget(window), mWindow(window)
        {
            setObjectName(QStringLiteral("arenaGlassTitleBar"));
            auto* row = new QHBoxLayout(this);
            row->setContentsMargins(14, 3, 4, 3);
            row->setSpacing(2);

            // U021: Windows-style window controls on the right. The launcher is
            // a fixed-size window, so only Minimize and Close are offered and
            // the former green maximize control is gone.
            auto* title = new QLabel(window->windowTitle(), this);
            title->setObjectName(QStringLiteral("arenaWindowTitle"));
            title->setAttribute(Qt::WA_TransparentForMouseEvents);
            title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            row->addWidget(title, 1);

            auto button = [this, row](const QString& name, const QString& label, const QString& glyph) {
                auto* b = new QToolButton(this);
                b->setObjectName(name);
                b->setText(glyph);
                b->setFixedSize(42, 26);
                b->setToolTip(label);
                b->setAccessibleName(label);
                b->setFocusPolicy(Qt::NoFocus);
                b->setCursor(Qt::ArrowCursor);
                row->addWidget(b);
                return b;
            };
            auto* minimize = button(QStringLiteral("arenaMinimize"), tr("Minimize"), QStringLiteral("\u2500"));
            auto* close = button(QStringLiteral("arenaClose"), tr("Close"), QStringLiteral("\u2715"));
            minimize->setVisible(window->windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
            connect(close, &QToolButton::clicked, window, [window]() { window->close(); });
            connect(minimize, &QToolButton::clicked, window, &QWidget::showMinimized);
            connect(window, &QWidget::windowTitleChanged, title, &QLabel::setText);
        }
    protected:
        void mousePressEvent(QMouseEvent* event) override
        {
            if (event->button() != Qt::LeftButton) return;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            if (mWindow->windowHandle() && mWindow->windowHandle()->startSystemMove()) return;
#endif
            if (!mWindow->isMaximized())
            {
                mDragging = true;
                mOffset = event->globalPos() - mWindow->frameGeometry().topLeft();
            }
        }
        void mouseMoveEvent(QMouseEvent* event) override
        {
            if (mDragging && (event->buttons() & Qt::LeftButton))
                mWindow->move(event->globalPos() - mOffset);
        }
        void mouseReleaseEvent(QMouseEvent*) override { mDragging = false; }
        void mouseDoubleClickEvent(QMouseEvent* event) override
        {
            mDragging = false;
            if (event->button() == Qt::LeftButton && mWindow->minimumSize() != mWindow->maximumSize()
                && mWindow->windowFlags().testFlag(Qt::WindowMaximizeButtonHint))
            {
                if (mWindow->isMaximized()) mWindow->showNormal(); else mWindow->showMaximized();
            }
        }
    };

    class GlassWindowMaterial : public QWidget
    {
        QWidget* mWindow;
        GlassTitleBar* mTitle;
        QSizeGrip* mGrip;
        bool mSystemGlass = false;
        QPointer<QWidget> mWizardBody;
        void arrange()
        {
            // QWizard sizes a private body directly against rect(), bypassing
            // QWidget contentsMargins. Keep that body's existing layout inside
            // the reserved content area; its own children remain untouched.
            if (mWizardBody && mWizardBody->geometry() != mWindow->contentsRect())
                mWizardBody->setGeometry(mWindow->contentsRect());
            setGeometry(mWindow->rect());
            lower();
            mTitle->setGeometry(7, 7, qMax(0, width() - 14), 34);
            mTitle->raise();
            mGrip->move(width() - 22, height() - 22);
            mGrip->setVisible(!mWindow->isMaximized() && mWindow->minimumSize() != mWindow->maximumSize());
            mGrip->raise();
            update();
        }
    public:
        explicit GlassWindowMaterial(QWidget* window)
            : QWidget(window), mWindow(window), mTitle(new GlassTitleBar(window)), mGrip(new QSizeGrip(window))
        {
            setAttribute(Qt::WA_TransparentForMouseEvents);
            setAttribute(Qt::WA_NoSystemBackground);
            mGrip->setFixedSize(16, 16);
            if (qobject_cast<QWizard*>(window))
            {
                for (auto* child : window->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
                    if (child != this && child != mTitle && child != mGrip)
                    {
                        mWizardBody = child;
                        child->installEventFilter(this);
                        break;
                    }
            }
            window->installEventFilter(this);
            arrange();
        }
    protected:
        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (mWizardBody && watched == mWizardBody
                && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
                arrange();
            if (watched == mWindow)
            {
                if (event->type() == QEvent::Resize || event->type() == QEvent::WindowStateChange
                    || event->type() == QEvent::LayoutRequest)
                    arrange();
                if (event->type() == QEvent::Show)
                {
                    mSystemGlass = requestSystemGlass(mWindow);
                    show();
                    mTitle->show();
                    arrange();
                }
            }
            return QWidget::eventFilter(watched, event);
        }
        void paintEvent(QPaintEvent*) override
        {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            const QRectF panel = QRectF(rect()).adjusted(6, 6, -6, -6);
            const qreal radius = mWindow->isMaximized() ? 0 : 17;

            // Static inexpensive shadow. Real compositor blur is requested on
            // supported Windows versions but is never required for readability.
            for (int i = 4; i > 0; --i)
            {
                p.setPen(QPen(QColor(0, 0, 0, 13), 2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(panel.adjusted(-i, -i, i, i), radius + i, radius + i);
            }

            QLinearGradient material(panel.topLeft(), panel.bottomRight());
            material.setColorAt(0, QColor(48, 48, 48, mSystemGlass ? 232 : 255));
            material.setColorAt(.50, QColor(27, 28, 31, mSystemGlass ? 238 : 255));
            material.setColorAt(1, QColor(24, 24, 26, 255));
            p.setPen(QPen(QColor(239, 225, 197, 48), 1));
            p.setBrush(material);
            p.drawRoundedRect(panel, radius, radius);

            QPainterPath clip;
            clip.addRoundedRect(panel, radius, radius);
            p.setClipPath(clip);
            QRadialGradient warm(QPointF(width() * .10, 0), width() * .72);
            warm.setColorAt(0, QColor(197, 151, 77, 31));
            warm.setColorAt(1, Qt::transparent);
            p.fillRect(panel, warm);
            QRadialGradient cool(QPointF(width(), height() * .45), width() * .72);
            cool.setColorAt(0, QColor(107, 133, 151, 17));
            cool.setColorAt(1, Qt::transparent);
            p.fillRect(panel, cool);
            p.setPen(QColor(255, 255, 255, 19));
            p.drawLine(20, 42, width() - 20, 42);
        }
    };

    // Call once after constructing a top-level window, before its first show.
    // Keeping the original widget preserves modality, signals, closeEvent and
    // updater cancellation/transaction guards. No wrapper window is introduced.
    inline void installGlassWindow(QWidget& window)
    {
        if (window.property("arenaGlassWindow").toBool()) return;
        window.setProperty("arenaGlassWindow", true);
        window.setWindowFlag(Qt::FramelessWindowHint);
        window.setAttribute(Qt::WA_TranslucentBackground);
        const QMargins old = window.contentsMargins();
        window.setContentsMargins(old.left() + 10, old.top() + 44, old.right() + 10, old.bottom() + 10);
        new GlassWindowMaterial(&window);
    }
}
#endif
