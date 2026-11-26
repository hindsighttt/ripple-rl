from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QColor, QFont, QPainter
from PySide6.QtWidgets import QWidget, QLabel, QVBoxLayout, QHBoxLayout


def _win(flags=True):
    w = QWidget()
    w.setWindowFlags(Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool |
                     (Qt.WindowTransparentForInput if flags else Qt.Widget))
    w.setAttribute(Qt.WA_TranslucentBackground)
    return w


class Label(QWidget):
    def __init__(self, size=14, color="#ffffff"):
        super().__init__()
        self.text = ""
        self.color = color
        self.size = size
        self.setFixedHeight(size + 10)

    def setText(self, t):
        self.text = t
        self.update()

    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        f = QFont("Segoe UI", self.size)
        f.setBold(True)
        p.setFont(f)
        p.setPen(QColor(self.color))
        p.drawText(self.rect(), Qt.AlignLeft | Qt.AlignVCenter, self.text)


class Overlays:
    def __init__(self):
        self.roster = _win()
        self.roster_layout = QVBoxLayout(self.roster)
        self.roster_layout.setContentsMargins(8, 8, 8, 8)
        self.roster_layout.setSpacing(2)
        self.roster_rows = []
        self.roster.resize(320, 200)
        self.roster.move(1900, 320)

        self.ball = _win()
        lay = QHBoxLayout(self.ball)
        lay.setContentsMargins(10, 6, 10, 6)
        self.ball_label = Label(20, "#7CFC00")
        lay.addWidget(self.ball_label)
        self.ball.resize(140, 40)
        self.ball.move(40, 40)

        self.fps = _win()
        flay = QHBoxLayout(self.fps)
        flay.setContentsMargins(10, 4, 10, 4)
        self.fps_label = Label(14, "#7CFC00")
        flay.addWidget(self.fps_label)
        self.fps.resize(140, 34)
        self.fps.move(40, 90)
        self._frames = 0
        self._timer = QTimer()
        self._timer.timeout.connect(self._tick)
        self._timer.start(1000)

    def _tick(self):
        if self.fps.isVisible():
            self.fps_label.setText("FPS  %d   MS  %.1f" % (self._frames, 1000.0 / max(self._frames, 1)))
        self._frames = 0

    def show_ball(self, on):
        self.ball.setVisible(on)

    def show_fps(self, on):
        self.fps.setVisible(on)

    def show_roster(self, on):
        self.roster.setVisible(on)

    def set_ball(self, speed_uu, unit="kmh", decimals=2):
        factor, label = {"mph": (0.0223693629, "mph"), "ms": (0.01, "m/s"),
                         "mps": (0.01, "m/s"), "m/s": (0.01, "m/s")}.get(unit, (0.036, "km/h"))
        self.ball_label.setText(("%.*f %s") % (max(0, min(3, int(decimals))), max(0.0, speed_uu) * factor, label))

    def set_roster(self, players):
        while len(self.roster_rows) < len(players):
            row = QWidget()
            h = QHBoxLayout(row)
            h.setContentsMargins(0, 0, 0, 0)
            name = Label(13)
            mmr = Label(13, "#8ab4ff")
            h.addWidget(name, 1)
            h.addWidget(mmr)
            self.roster_layout.addWidget(row)
            self.roster_rows.append((row, name, mmr))
        while len(self.roster_rows) > len(players):
            row, _, _ = self.roster_rows.pop()
            row.deleteLater()
        for i, pl in enumerate(players):
            _, name, mmr = self.roster_rows[i]
            name.setText(pl.get("Name", ""))
            mmr.setText(str(pl.get("MMR", "")))
            self.roster_rows[i][0].setVisible(True)
        for i in range(len(players), len(self.roster_rows)):
            self.roster_rows[i][0].setVisible(False)
