#include "tray.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

static const int S = 64; // icon canvas size (the panel downscales it)

Tray::Tray(QObject *parent) : QObject(parent) {
    m_menu.addAction(QIcon::fromTheme("document-edit"), "Last dictation…", this,
                     [this] { emit openDebug(); });
    m_menu.addAction(QIcon::fromTheme("configure"), "Settings…", this,
                     [this] { emit openSettings(); });
    m_menu.addSeparator();
    m_menu.addAction(QIcon::fromTheme("view-refresh"), "Restart daemon", this,
                     [this] { emit requestRestartDaemon(); });
    m_menu.addAction(QIcon::fromTheme("application-exit"), "Quit", this,
                     [this] { emit requestQuit(); });
    m_tray.setContextMenu(&m_menu);

    connect(&m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason r) {
                if (r == QSystemTrayIcon::Trigger) emit openDebug(); // left click
            });

    connect(&m_anim, &QTimer::timeout, this, [this] {
        if (m_state == "recording") { m_dotOn = !m_dotOn; render(); }
        else if (m_state == "processing") { m_frame = (m_frame + 1) % 8; render(); }
    });

    render();
    m_tray.show();
}

void Tray::setState(const QString &state) {
    m_state = state;
    m_anim.stop();
    if (state == "recording") m_anim.start(350);   // red pulse
    else if (state == "processing") m_anim.start(70); // spinner
    render();
}

static void paintMic(QPainter &p, const QColor &col) {
    p.save();
    p.translate(S / 2.0, S / 2.0);
    QPen pen(col, 4.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QRectF cap(-9, -22, 18, 25);            // capsule
    p.drawRoundedRect(cap, 8, 8);
    QPainterPath arc;                        // cradle
    arc.moveTo(-16, -7);
    arc.arcTo(QRectF(-16, -7, 32, 24), 180, 180);
    p.drawPath(arc);
    p.drawLine(QPointF(-7, 18), QPointF(7, 18)); // base
    p.restore();
}

void Tray::render() {
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r(3, 3, S - 6, S - 6);
    QPainterPath path;                       // dark badge so the icon reads on any panel
    path.addRoundedRect(r, 14, 14);
    p.fillPath(path, QColor(28, 28, 34, 215));
    p.setPen(QPen(QColor(255, 255, 255, 26), 1));
    p.drawPath(path);

    if (m_state == "recording") {
        paintMic(p, m_dotOn ? QColor(248, 113, 113) : QColor(220, 38, 38));
        p.setPen(QPen(m_dotOn ? QColor(248, 113, 113, 235) : QColor(239, 68, 68, 140), 2.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r.adjusted(2.5, 2.5, -2.5, -2.5)); // pulsing red ring
    } else if (m_state == "processing") {
        paintMic(p, QColor(167, 139, 250));  // wispr purple
        QPen arc(QColor(255, 255, 255, 220), 3.0, Qt::SolidLine, Qt::RoundCap);
        arc.setDashPattern({3.0, 4.0});
        arc.setDashOffset(-m_frame * 19.6);  // rotating dash around the badge
        p.setPen(arc);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r.adjusted(4, 4, -4, -4));
    } else if (m_state == "offline") {
        paintMic(p, QColor(148, 163, 184, 190));
        p.setPen(QPen(QColor(248, 113, 113), 4.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(16, 16), QPointF(S - 16, S - 16)); // red slash
    } else { // idle
        paintMic(p, QColor(255, 255, 255, 225));
    }
    p.end();

    QString tip = "WisprFlow";
    if (m_state == "recording") tip += " — recording… release Ctrl+Win to finish";
    else if (m_state == "processing") tip += " — transcribing…";
    else if (m_state == "offline") tip += " — daemon offline";
    else tip += " — hold Ctrl+Win to dictate";
    if (!m_backend.isEmpty()) tip += "\nbackend: " + m_backend;
    if (!m_lastText.isEmpty())
        tip += "\nlast: " + m_lastText.left(60) + (m_lastText.size() > 60 ? "…" : "");

    m_tray.setIcon(QIcon(pm));
    m_tray.setToolTip(tip);
}
