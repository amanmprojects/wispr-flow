#include "pill.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QWindow>
#include <QScreen>
#include <QFontMetrics>
#include <QAccessible>
#include <QtMath>

Pill::Pill() {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("WisprFlow status pill");
    setAccessibleDescription("Shows dictation state, press Enter or Space to open menu, Shift+F10 for context menu");

    m_menu.addAction(QIcon(), "Last dictation…", this, [this] { emit openDebug(); });
    m_menu.addAction(QIcon(), "Settings…", this, [this] { emit openSettings(); });
    m_menu.addSeparator();
    m_menu.addAction(QIcon(), "Restart daemon", this, [this] { emit requestRestartDaemon(); });
    m_menu.addAction(QIcon(), "Quit", this, [this] { emit requestQuit(); });
    resize(sizeHint());

    connect(&m_anim, &QTimer::timeout, this, [this] {
        m_spinAngle = (m_spinAngle + 12) % 360;
        m_dotOn = !m_dotOn;
        m_peak = m_peak * 0.86 + 0.14 * (m_levels.empty() ? 0 : m_levels.back());
        update();
    });
    // timer starts only when recording/processing (controlled by setState)
}

QSize Pill::sizeHint() const {
    QFontMetrics fm(font());
    // widest state text
    int w1 = fm.horizontalAdvance("Hold Ctrl+Win");
    int w2 = fm.horizontalAdvance("Transcribing…");
    int w3 = fm.horizontalAdvance("Listening…");
    int w4 = fm.horizontalAdvance("daemon offline");
    int textW = qMax(qMax(w1, w2), qMax(w3, w4));
    qreal dpr = devicePixelRatioF();
    if (qFuzzyCompare(dpr, 1.0)) {
        if (auto *s = QGuiApplication::primaryScreen()) dpr = s->devicePixelRatio();
        if (dpr < 1.0) dpr = 1.0;
    }
    // icon area ~ 32px + spacing 8 + padding 20
    int w = textW + 64 + 24;
    int h = qMax(42, fm.height() + 24);
    // ensure minimum width for waveform etc.
    w = qMax(w, 184);
    Q_UNUSED(dpr);
    return QSize(w, h);
}

void Pill::setState(const QString &state) {
    m_state = state;
    bool reduced = qEnvironmentVariableIsSet("WISPR_REDUCED_MOTION");
    if (reduced) { if (m_anim.isActive()) m_anim.stop(); }
    else if (state == "recording" || state == "processing") {
        if (!m_anim.isActive()) m_anim.start(30);
    } else {
        if (m_anim.isActive()) m_anim.stop();
    }
    update();
}

void Pill::addLevel(int v) {
    m_levels.push_back(v);
    while ((int)m_levels.size() > 32) m_levels.pop_front();
}

static QColor mix(const QColor &a, const QColor &b, double t) {
    return QColor(int(a.red() + (b.red() - a.red()) * t),
                  int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue() + (b.blue() - a.blue()) * t));
}

void Pill::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = rect().adjusted(1, 1, -1, -1);
    const double radius = r.height() / 2.0;

    // background
    const QColor accent(139, 92, 246);   // wispr purple
    QColor bg(24, 24, 30, 235);
    if (m_state == "recording") bg = mix(bg, QColor(220, 38, 38), 0.15);
    else if (m_state == "processing") bg = mix(bg, accent, 0.12);
    else if (m_state == "offline") bg = QColor(40, 40, 46, 220);

    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    p.fillPath(path, bg);
    p.setPen(QPen(QColor(255, 255, 255, 26), 1));
    p.drawPath(path);

    const double cx = r.left() + r.height() / 2.0;
    const double cy = r.center().y();

    auto drawMic = [&](const QColor &col, double scale) {
        p.save();
        p.translate(cx, cy);
        p.scale(scale, scale);
        QPen pen(col, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        QRectF cap(-4, -9, 8, 11);          // capsule
        p.drawRoundedRect(cap, 3.6, 3.6);
        QPainterPath arc;                    // cradle
        arc.moveTo(-7, -3);
        arc.arcTo(QRectF(-7, -3, 14, 10), 180, 180);
        p.drawPath(arc);
        QLineF base(-3, 7.5, 3, 7.5);        // base line
        p.drawLine(base);
        p.restore();
    };

    auto drawSpinner = [&](const QColor &col) {
        p.save();
        p.translate(cx, cy);
        QPen pen(col, 2.4, Qt::SolidLine, Qt::RoundCap);
        pen.setDashPattern({2.4, 3.0});
        pen.setDashOffset(-m_spinAngle / 360.0 * 5.4);
        p.setPen(pen);
        p.drawEllipse(QRectF(-7, -7, 14, 14));
        p.restore();
    };

    const QFontMetrics fm(p.font());
    const int textX = int(cx + r.height() / 2.0 + 8);

    if (m_state == "recording") {
        // pulsing red dot + waveform bars
        p.setPen(Qt::NoPen);
        p.setBrush(m_dotOn ? QColor(248, 113, 113) : QColor(239, 68, 68));
        p.drawEllipse(QPointF(cx, cy), 4.5, 4.5);

        // bars
        const int n = (int)m_levels.size();
        const int maxBars = 26;
        const int bars = qMin(n, maxBars);
        const double bw = 2.4, gap = 2.0;
        const double totalW = bars * (bw + gap);
        double x0 = textX + fm.horizontalAdvance(" ") + 2;
        if (bars > 0 && x0 + totalW < width() - 10) {
            double norm = qMax(m_peak, 100.0);
            for (int i = 0; i < bars; i++) {
                double v = m_levels[n - bars + i] / norm;
                if (v > 1) v = 1;
                double h = 4 + v * (r.height() - 16);
                QColor c = mix(QColor(248, 113, 113), QColor(255, 255, 255), v);
                p.setBrush(c);
                p.drawRoundedRect(QRectF(x0 + i * (bw + gap), cy - h / 2, bw, h), 1.2, 1.2);
            }
        } else {
            p.setPen(QColor(255, 255, 255, 180));
            p.drawText(QRectF(textX, 0, width() - textX - 8, height()),
                       Qt::AlignVCenter | Qt::AlignLeft, "Listening…");
        }
    } else if (m_state == "processing") {
        drawSpinner(accent);
        p.setPen(QColor(255, 255, 255, 200));
        p.drawText(QRectF(textX, 0, width() - textX - 8, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, "Transcribing…");
    } else if (m_state == "offline") {
        // improved contrast for offline text (was 90 alpha)
        p.setPen(QColor(255, 255, 255, 165));
        p.drawText(QRectF(textX, 0, width() - textX - 8, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, "daemon offline");
    } else { // idle
        drawMic(QColor(255, 255, 255, 210), 1.0);
        p.setPen(QColor(255, 255, 255, 235));
        QFont f = p.font();
        f.setBold(true);
        f.setPointSizeF(8.6);
        p.setFont(f);
        p.drawText(QRectF(textX, 0, width() - textX - 6, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, "Hold Ctrl+Win");
    }
}

void Pill::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && windowHandle()) {
        windowHandle()->startSystemMove(); // Wayland-safe drag
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void Pill::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) emit openDebug();
    QWidget::mouseDoubleClickEvent(e);
}

void Pill::contextMenuEvent(QContextMenuEvent *e) {
    m_menu.exec(e->globalPos());
}

void Pill::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        hide();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
        // Enter/Space opens debug/dashboard (primary action)
        emit openDebug();
        e->accept();
        return;
    }
    if ((e->key() == Qt::Key_F10 && e->modifiers() & Qt::ShiftModifier) || e->key() == Qt::Key_Menu) {
        // Shift+F10 / Menu key opens context menu
        m_menu.exec(mapToGlobal(rect().center()));
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}
