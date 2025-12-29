/**
 * Magic Keyboard UI - v0.1
 * Non-focusable Qt6/QML keyboard with socket IPC
 */

#include <QDebug>
#include <QGuiApplication>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#include "protocol.h"
#include <QElapsedTimer>
#include <algorithm>

class KeyboardBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(State state READ state NOTIFY stateChanged)

public:
  enum State {
    Hidden,
    Passive, // Visible but low profile (e.g. from focus)
    Active   // Fully interactive (user clicked/typed)
  };
  Q_ENUM(State)

  explicit KeyboardBridge(QObject *parent = nullptr) : QObject(parent) {
    socket_ = new QLocalSocket(this);
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);
    lastToggleTimer_.start();
    toggleLogTimer_.start();

    connect(socket_, &QLocalSocket::connected, this, [this]() {
      qDebug() << "Connected to engine";
      reconnecting_ = false;
      // Reset backoff on successful connection
      reconnectDelayMs_ = kInitialReconnectDelayMs;

      // Identify as UI
      socket_->write("{\"type\":\"hello\",\"role\":\"ui\"}\n");
      socket_->flush();
    });

    connect(socket_, &QLocalSocket::disconnected, this, [this]() {
      qDebug() << "Disconnected from engine";
      scheduleReconnect();
    });

    connect(socket_, &QLocalSocket::readyRead, this,
            &KeyboardBridge::onReadyRead);

    connect(socket_, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError err) {
              Q_UNUSED(err);
              // Only log on first attempt to avoid spam
              if (reconnectDelayMs_ == kInitialReconnectDelayMs) {
                qDebug() << "Socket error:" << socket_->errorString();
              }
              scheduleReconnect();
            });

    connect(reconnectTimer_, &QTimer::timeout, this,
            &KeyboardBridge::tryConnect);
  }

  State state() const { return state_; }

  // Helper for QML to request state changes
  Q_INVOKABLE void requestState(State newState, const QString &reason) {
    setState(newState, reason);
  }

  void setState(State s, const QString &reason) {
    if (state_ != s) {
      qDebug() << "[UI] State transition:" << state_ << "->" << s
               << "reason=" << reason;
      state_ = s;
      emit stateChanged();
    } else {
      // Optional: log redundant transitions at debug level if needed
      // qDebug() << "[UI] Redundant transition to" << s << "reason=" << reason;
    }
  }

public slots:
  void connectToEngine() { tryConnect(); }

  // Helper to promote Passive -> Active with debounce
  // Returns true if promoted, false if already Active or ignored
  bool promoteIfPassive(const QString &reason) {
    if (state_ == Active)
      return false;
    if (state_ == Hidden)
      return false; // Should not happen if UI behaves, but safe guard

    // Debounce promotions to avoid spamming state changes/logs on rapid bursts
    if (lastPromotionTimer_.isValid() && lastPromotionTimer_.elapsed() < 150) {
      return false;
    }

    setState(Active, reason);
    lastPromotionTimer_.restart();
    return true;
  }

  void sendKey(const QString &key) {
    promoteIfPassive("intent_key");

    if (socket_->state() != QLocalSocket::ConnectedState)
      return;
    QString msg = QString("{\"type\":\"key\",\"text\":\"%1\"}\n").arg(key);
    socket_->write(msg.toUtf8());
    socket_->flush();
  }

  void sendAction(const QString &action) {
    promoteIfPassive("intent_action");

    if (socket_->state() != QLocalSocket::ConnectedState)
      return;
    QString msg =
        QString("{\"type\":\"action\",\"action\":\"%1\"}\n").arg(action);
    socket_->write(msg.toUtf8());
    socket_->flush();
  }

  void sendSwipePath(const QVariantList &path) {
    promoteIfPassive("intent_swipe");

    if (socket_->state() != QLocalSocket::ConnectedState)
      return;

    // Path is list of {x, y} already in layout space
    QString pointsJson = "[";
    for (int i = 0; i < path.size(); ++i) {
      QVariantMap pt = path[i].toMap();
      pointsJson += QString("{\"x\":%1,\"y\":%2}")
                        .arg(pt["x"].toReal())
                        .arg(pt["y"].toReal());
      if (i < path.size() - 1)
        pointsJson += ",";
    }
    pointsJson += "]";

    QString msg = QString("{\"type\":\"swipe_path\",\"layout\":\"qwerty\","
                          "\"space\":\"layout\",\"points\":%1}\n")
                      .arg(pointsJson);
    socket_->write(msg.toUtf8());
    socket_->flush();
    qDebug() << "Sent swipe_path layout=qwerty points=" << path.size();
  }

private slots:
  void tryConnect() {
    // Only connect if in unconnected state
    if (socket_->state() != QLocalSocket::UnconnectedState) {
      socket_->abort();
      // Wait for state to settle
      QTimer::singleShot(100, this, &KeyboardBridge::tryConnect);
      return;
    }

    reconnecting_ = false;
    QString path = QString::fromStdString(magickeyboard::ipc::getSocketPath());
    socket_->connectToServer(path);
  }

  void scheduleReconnect() {
    if (reconnecting_)
      return;
    reconnecting_ = true;

    // Exponential backoff with cap
    reconnectTimer_->setInterval(reconnectDelayMs_);
    reconnectTimer_->start();

    // Increase delay for next attempt (exponential backoff)
    reconnectDelayMs_ = qMin(reconnectDelayMs_ * 2, kMaxReconnectDelayMs);
  }

  void onReadyRead() {
    buffer_ += socket_->readAll();

    int idx;
    while ((idx = buffer_.indexOf('\n')) >= 0) {
      QByteArray line = buffer_.left(idx);
      buffer_.remove(0, idx + 1);

      QString msg = QString::fromUtf8(line).trimmed();

      if (msg.contains("\"type\":\"show\"") ||
          msg.contains("\"type\":\"ui_show\"")) {
        qDebug() << "Received: show -> Passive";
        setState(Passive, "ipc_show"); // Focus events default to Passive
      } else if (msg.contains("\"type\":\"hide\"") ||
                 msg.contains("\"type\":\"ui_hide\"")) {
        qDebug() << "Received: hide -> Hidden";
        setState(Hidden, "ipc_hide");
      } else if (msg.contains("\"type\":\"ui_toggle\"")) {
        // Debounce toggles (100ms)
        if (lastToggleTimer_.elapsed() < 100) {
          qDebug() << "Ignoring rapid toggle (<100ms)";
        } else {
          lastToggleTimer_.restart();
          // Log rate limiting logic inherited from Polish
          toggleCount_++;
          if (toggleLogTimer_.elapsed() >= 1000) {
            // New window - emit summary if we had multiple in last window
            if (toggleCount_ > 1) {
              qDebug() << "Accepted toggle x" << toggleCount_ << " in last 1s";
            } else {
              qDebug() << "Accepted toggle (fd" << socket_->socketDescriptor()
                       << ")";
            }
            toggleLogTimer_.restart();
            toggleCount_ = 0;
          } else if (toggleCount_ == 1) {
            // First toggle in new window - log immediately
            qDebug() << "Accepted toggle (fd" << socket_->socketDescriptor()
                     << ")";
          }
          // else: mid-window toggle, will be counted in summary

          // Toggle logic: Hidden -> Active (Manual intention), Visible ->
          // Hidden
          if (state_ == Hidden) {
            setState(Active,
                     "ipc_toggle"); // Manual toggle implies active usage
          } else {
            setState(Hidden, "ipc_toggle");
          }
        }
      } else if (msg.contains("\"type\":\"ui_intent\"")) {
        qDebug() << "Received ui_intent:" << msg;
        if (state_ == Hidden) {
          qDebug() << "Ignored ui_intent (Hidden state)";
        } else {
          // Minimal manual JSON parsing to avoid deps
          auto getString = [&](const QString &key) -> QString {
            QString pf = "\"" + key + "\":\"";
            int s = msg.indexOf(pf);
            if (s < 0)
              return QString();
            s += pf.length();
            int e = msg.indexOf("\"", s);
            if (e < 0)
              return QString();
            return msg.mid(s, e - s);
          };

          auto getDouble = [&](const QString &key,
                               double defVal = 1.0) -> double {
            QString pf = "\"" + key + "\":";
            int s = msg.indexOf(pf);
            if (s < 0)
              return defVal;
            s += pf.length();
            // Find end of number: , or }
            int e1 = msg.indexOf(",", s);
            int e2 = msg.indexOf("}", s);
            int e = (e1 == -1) ? e2 : (e2 == -1 ? e1 : std::min(e1, e2));
            if (e < 0)
              return defVal;
            QString num = msg.mid(s, e - s).trimmed();
            // Handle optional quotes around number: "0.5"
            if (num.startsWith("\"") && num.endsWith("\"") && num.size() >= 2) {
              num = num.mid(1, num.size() - 2).trimmed();
            }
            bool ok = false;
            double v = num.toDouble(&ok);
            return ok ? v : defVal;
          };

          QString intent = getString("intent");
          if (intent == "key") {
            QString val = getString("value");
            if (!val.isEmpty())
              sendKey(val);
          } else if (intent == "action") {
            QString val = getString("value");
            if (!val.isEmpty())
              sendAction(val);
          } else if (intent == "swipe") {
            QString dir = getString("dir");
            double mag = getDouble("mag");
            double len = 100.0 * mag;
            double dx = 0, dy = 0;
            if (dir == "left")
              dx = -len;
            else if (dir == "right")
              dx = len;
            else if (dir == "up")
              dy = -len;
            else if (dir == "down")
              dy = len;
            // Default to right if unknown/empty to ensure path has length
            if (dx == 0 && dy == 0)
              dx = 10.0;

            QVariantList path;
            path << QVariantMap{{"x", 0.0}, {"y", 0.0}};
            path << QVariantMap{{"x", dx}, {"y", dy}};
            sendSwipePath(path);
          }
        }
      } else if (msg.contains("\"type\":\"swipe_keys\"")) {
        QStringList keys;
        int arrKeyPos = msg.indexOf("\"keys\":[");
        if (arrKeyPos >= 0) {
          int arrStart = arrKeyPos + 8;
          int arrEnd = msg.indexOf("]", arrStart);
          if (arrEnd > arrStart) {
            QString content = msg.mid(arrStart, arrEnd - arrStart);
            QStringList items = content.split(",", Qt::SkipEmptyParts);
            for (auto &item : items) {
              keys << item.trimmed().remove("\"");
            }
          }
        }
        qDebug() << "Received swipe_keys count=" << keys.size();
        emit swipeKeysReceived(keys);
      } else if (msg.contains("\"type\":\"swipe_candidates\"")) {
        QStringList words;
        int arrStart = msg.indexOf("\"candidates\":[");
        if (arrStart >= 0) {
          arrStart += 14;
          int arrEnd = msg.indexOf("]", arrStart);
          if (arrEnd > arrStart) {
            QString content = msg.mid(arrStart, arrEnd - arrStart);
            // Content is list of {"w":"..."}
            int wPos = 0;
            while ((wPos = content.indexOf("\"w\":\"", wPos)) >= 0) {
              int wStart = wPos + 5;
              int wEnd = content.indexOf("\"", wStart);
              if (wEnd > wStart) {
                words << content.mid(wStart, wEnd - wStart);
              }
              wPos = wEnd;
            }
          }
        }
        qDebug() << "Received swipe_candidates count=" << words.size();
        emit swipeCandidatesReceived(words);
      }
    }
  }

public slots:
  void commitCandidate(const QString &word) {
    promoteIfPassive("intent_candidate");

    if (socket_->state() != QLocalSocket::ConnectedState)
      return;
    QString msg =
        QString("{\"type\":\"commit_candidate\",\"text\":\"%1\"}\n").arg(word);
    socket_->write(msg.toUtf8());
    socket_->flush();
  }

private:
  // Exponential backoff constants for reconnection
  static constexpr int kInitialReconnectDelayMs = 100;
  static constexpr int kMaxReconnectDelayMs = 5000;

  QLocalSocket *socket_;
  QTimer *reconnectTimer_;
  QByteArray buffer_;
  State state_ = Hidden; // Replaces 'bool visible_'
  bool reconnecting_ = false;
  int reconnectDelayMs_ = kInitialReconnectDelayMs;
  QElapsedTimer lastToggleTimer_;
  QElapsedTimer toggleLogTimer_;     // For rate-limiting toggle logs
  QElapsedTimer lastPromotionTimer_; // For debouncing promotions
  int toggleCount_ = 0;              // Toggles in current 1s window

signals:
  void stateChanged();
  void swipeKeysReceived(const QStringList &keys);
  void swipeCandidatesReceived(const QStringList &candidates);
};

int main(int argc, char *argv[]) {
  QGuiApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QGuiApplication app(argc, argv);
  app.setApplicationName("Magic Keyboard");

  qDebug() << "Magic Keyboard UI starting";
  qDebug() << "Socket:"
           << QString::fromStdString(magickeyboard::ipc::getSocketPath());

  KeyboardBridge bridge;

  qmlRegisterType<KeyboardBridge>("MagicKeyboard", 1, 0, "KeyboardBridge");

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("bridge", &bridge);

  const QUrl url(QStringLiteral("qrc:/MagicKeyboard/KeyboardWindow.qml"));

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreated, &app,
      [&bridge, url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
          qCritical() << "Failed to load QML";
          QCoreApplication::exit(-1);
          return;
        }

        auto *window = qobject_cast<QQuickWindow *>(obj);
        if (window) {
          // CRITICAL: Never steal focus
          window->setFlags(Qt::Tool | Qt::FramelessWindowHint |
                           Qt::WindowStaysOnTopHint |
                           Qt::WindowDoesNotAcceptFocus);

          // Position at bottom center
          QScreen *screen = QGuiApplication::primaryScreen();
          if (screen) {
            QRect r = screen->availableGeometry();
            int w = window->width();
            int h = window->height();
            window->setPosition((r.width() - w) / 2, r.height() - h - 20);
          }

          // State-driven visibility
          QObject::connect(&bridge, &KeyboardBridge::stateChanged, window,
                           [&bridge, window]() {
                             if (bridge.state() == KeyboardBridge::Hidden) {
                               window->hide();
                             } else {
                               window->show();
                             }
                           });

          // Allow QML to initialize correctly
          if (bridge.state() != KeyboardBridge::Hidden) {
            window->show();
          }

          // Connect to engine
          bridge.connectToEngine();

          qDebug() << "Window ready and managed by UiState";
        }
      },
      Qt::QueuedConnection);

  engine.load(url);

  return app.exec();
}

#include "main.moc"
