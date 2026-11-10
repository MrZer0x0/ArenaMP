#ifndef OPENMW_COMPONENTS_MISC_ARENAHEROBUTTON_H
#define OPENMW_COMPONENTS_MISC_ARENAHEROBUTTON_H

#include <QEvent>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVariantAnimation>

#include <cmath>

namespace ArenaUi
{
    // QFontMetrics::horizontalAdvance() only exists from Qt 5.11 on.
    inline int textAdvance(const QFontMetrics& metrics, const QString& text)
    {
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
        return metrics.horizontalAdvance(text);
#else
        return metrics.width(text);
#endif
    }

    // U021: self-painted primary action ("Start game" / "Play" / "Update").
    //
    // A regular QPushButton cannot be used for the hero action: the shared QSS
    // min-height rule overrides the .ui minimum height and child labels do not
    // contribute to QPushButton::sizeHint(), so the button collapsed to ~22 px
    // inside the launch card. This widget computes its own size, paints a gold
    // glass body plus a soft animated glow, and keeps the full QPushButton API
    // (text(), icon(), clicked(), setEnabled()) so existing code is unchanged.
    //
    // The pulse is a cheap repaint of one small widget and stops automatically
    // while the button is hidden or disabled.
    class HeroButton : public QPushButton
    {
    public:
        explicit HeroButton(QWidget* parent = nullptr)
            : QPushButton(parent)
            , mPulse(new QVariantAnimation(this))
        {
            setProperty("arenaHero", true);
            setCursor(Qt::PointingHandCursor);
            setFocusPolicy(Qt::TabFocus);
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            setIconSize(QSize(18, 18));
            mPulse->setStartValue(0.0);
            mPulse->setEndValue(1.0);
            mPulse->setDuration(2600);
            mPulse->setLoopCount(-1);
            QObject::connect(mPulse, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
                // 0 -> 1 -> 0 per period, smooth at both ends.
                mPhase = 0.5 - 0.5 * std::cos(value.toDouble() * 6.283185307179586);
                update();
            });
        }

        void setSubtitle(const QString& subtitle)
        {
            if (mSubtitle == subtitle)
                return;
            mSubtitle = subtitle;
            updateGeometry();
            update();
        }
        QString subtitle() const { return mSubtitle; }

        // Compact variant for the footer: one line, smaller glow margin.
        void setCompact(bool compact)
        {
            mCompact = compact;
            setIconSize(compact ? QSize(16, 16) : QSize(18, 18));
            updateGeometry();
            update();
        }

        // Soft breathing glow. "Strong" is used when an update is waiting.
        void setPulse(bool enabled, bool strong = false)
        {
            mPulseEnabled = enabled;
            mStrongPulse = strong;
            syncAnimation();
            update();
        }

        QSize sizeHint() const override
        {
            const int margin = glowMargin();
            const QFontMetrics titleMetrics(titleFont());
            int width = textAdvance(titleMetrics, text()) + 2 * margin + (mCompact ? 34 : 48);
            if (!icon().isNull())
                width += iconSize().width() + 8;
            if (!mCompact && !mSubtitle.isEmpty())
                width = qMax(width, textAdvance(QFontMetrics(subtitleFont()), mSubtitle) + 2 * margin + 40);
            return QSize(width, bodyHeight() + 2 * margin);
        }
        QSize minimumSizeHint() const override
        {
            return QSize(mCompact ? 120 : 160, bodyHeight() + 2 * glowMargin());
        }

    protected:
        int glowMargin() const { return mCompact ? 3 : 6; }
        int bodyHeight() const { return mCompact ? 36 : 60; }

        QFont titleFont() const
        {
            QFont font = this->font();
            font.setPixelSize(mCompact ? 13 : 16);
            font.setBold(true);
            return font;
        }
        QFont subtitleFont() const
        {
            QFont font = this->font();
            font.setPixelSize(11);
            font.setBold(false);
            return font;
        }

        void syncAnimation()
        {
            const bool run = mPulseEnabled && isEnabled() && isVisible();
            if (run && mPulse->state() != QAbstractAnimation::Running)
                mPulse->start();
            else if (!run && mPulse->state() != QAbstractAnimation::Stopped)
            {
                mPulse->stop();
                mPhase = 0.0;
            }
        }

        void showEvent(QShowEvent* event) override
        {
            QPushButton::showEvent(event);
            syncAnimation();
        }
        void hideEvent(QHideEvent* event) override
        {
            QPushButton::hideEvent(event);
            syncAnimation();
        }
        void changeEvent(QEvent* event) override
        {
            QPushButton::changeEvent(event);
            if (event->type() == QEvent::EnabledChange)
                syncAnimation();
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        void enterEvent(QEnterEvent* event) override
#else
        void enterEvent(QEvent* event) override
#endif
        {
            mHover = true;
            update();
            QPushButton::enterEvent(event);
        }
        void leaveEvent(QEvent* event) override
        {
            mHover = false;
            update();
            QPushButton::leaveEvent(event);
        }

        void paintEvent(QPaintEvent*) override
        {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::TextAntialiasing);

            const int margin = glowMargin();
            const QRectF body = QRectF(rect()).adjusted(margin + 0.5, margin + 0.5, -margin - 0.5, -margin - 0.5);
            const qreal radius = mCompact ? 11.0 : 13.0;
            const bool enabled = isEnabled();

            // Soft outer glow: a few widening, fading rounded outlines.
            if (enabled)
            {
                const qreal base = mHover ? 0.55 : 0.30;
                const qreal amplitude = mPulseEnabled ? (mStrongPulse ? 0.70 : 0.45) : 0.0;
                const qreal intensity = qMin(1.0, base + amplitude * mPhase);
                for (int i = 1; i <= margin; ++i)
                {
                    const qreal fade = 1.0 - qreal(i) / (margin + 1);
                    QColor glow(236, 190, 104);
                    glow.setAlphaF(qBound(0.0, 0.22 * intensity * fade * fade, 1.0));
                    p.setPen(QPen(glow, 1.6));
                    p.setBrush(Qt::NoBrush);
                    p.drawRoundedRect(body.adjusted(-i, -i, i, i), radius + i, radius + i);
                }
            }

            // Gold glass body.
            QLinearGradient fill(body.topLeft(), body.bottomLeft());
            if (!enabled)
            {
                fill.setColorAt(0.0, QColor(66, 62, 55));
                fill.setColorAt(1.0, QColor(52, 49, 44));
            }
            else if (isDown())
            {
                fill.setColorAt(0.0, QColor(196, 150, 84));
                fill.setColorAt(1.0, QColor(160, 116, 58));
            }
            else
            {
                const qreal lift = (mHover ? 0.10 : 0.0) + 0.06 * mPhase * (mPulseEnabled ? 1.0 : 0.0);
                auto lighten = [lift](const QColor& color) {
                    return QColor(qMin(255, int(color.red() + (255 - color.red()) * lift)),
                        qMin(255, int(color.green() + (255 - color.green()) * lift)),
                        qMin(255, int(color.blue() + (255 - color.blue()) * lift)));
                };
                fill.setColorAt(0.0, lighten(QColor(243, 216, 156)));
                fill.setColorAt(0.52, lighten(QColor(216, 174, 103)));
                fill.setColorAt(1.0, lighten(QColor(176, 128, 61)));
            }
            p.setPen(QPen(enabled ? QColor(248, 225, 172) : QColor(88, 82, 72), 1.0));
            p.setBrush(fill);
            p.drawRoundedRect(body, radius, radius);

            // Glass highlight on the upper half.
            if (enabled)
            {
                QPainterPath clip;
                clip.addRoundedRect(body, radius, radius);
                p.save();
                p.setClipPath(clip);
                QLinearGradient shine(body.topLeft(), QPointF(body.left(), body.center().y()));
                shine.setColorAt(0.0, QColor(255, 255, 255, 70));
                shine.setColorAt(1.0, QColor(255, 255, 255, 0));
                p.fillRect(QRectF(body.left(), body.top(), body.width(), body.height() / 2), shine);
                p.restore();
            }

            // Content: [icon] Title  /  subtitle.
            const QColor titleColor = enabled ? QColor(33, 25, 16) : QColor(141, 136, 126);
            QColor subtitleColor = titleColor;
            subtitleColor.setAlpha(enabled ? 190 : 170);
            const QFont tFont = titleFont();
            const QFontMetrics tMetrics(tFont);
            const bool twoLines = !mCompact && !mSubtitle.isEmpty();
            const int titleWidth = textAdvance(tMetrics, text());
            const QSize iSize = icon().isNull() ? QSize() : iconSize();
            const int gap = iSize.isValid() ? 8 : 0;
            const int rowWidth = titleWidth + (iSize.isValid() ? iSize.width() : 0) + gap;
            const qreal titleCenterY = twoLines ? body.top() + body.height() * 0.40 : body.center().y();
            qreal x = body.center().x() - rowWidth / 2.0;
            if (iSize.isValid())
            {
                const QPixmap pix = icon().pixmap(iSize, enabled ? QIcon::Normal : QIcon::Disabled);
                p.drawPixmap(QPointF(x, titleCenterY - iSize.height() / 2.0), pix);
                x += iSize.width() + gap;
            }
            p.setFont(tFont);
            p.setPen(titleColor);
            p.drawText(QRectF(x, titleCenterY - tMetrics.height() / 2.0, titleWidth + 2, tMetrics.height()),
                Qt::AlignLeft | Qt::AlignVCenter, text());
            if (twoLines)
            {
                const QFont sFont = subtitleFont();
                const QFontMetrics sMetrics(sFont);
                p.setFont(sFont);
                p.setPen(subtitleColor);
                const QRectF subRect(body.left() + 10, body.top() + body.height() * 0.66 - sMetrics.height() / 2.0,
                    body.width() - 20, sMetrics.height());
                p.drawText(subRect, Qt::AlignCenter, sMetrics.elidedText(mSubtitle, Qt::ElideRight, int(subRect.width())));
            }

            if (hasFocus() && enabled)
            {
                p.setPen(QPen(QColor(255, 244, 214, 150), 1.0, Qt::DotLine));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(body.adjusted(3, 3, -3, -3), radius - 3, radius - 3);
            }
        }

    private:
        QVariantAnimation* mPulse;
        QString mSubtitle;
        qreal mPhase = 0.0;
        bool mCompact = false;
        bool mPulseEnabled = false;
        bool mStrongPulse = false;
        bool mHover = false;
    };
}

#endif
