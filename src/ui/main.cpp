/**
 * Magic Keyboard UI - v0.1
 * Non-focusable Qt6/QML keyboard with socket IPC
 */

#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#include "protocol.h"
#include <QElapsedTimer>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>

class KeyboardBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(State state READ state NOTIFY stateChanged)
  Q_PROPERTY(double windowOpacity READ windowOpacity NOTIFY settingsChanged)
  Q_PROPERTY(double windowScale READ windowScale NOTIFY settingsChanged)
  Q_PROPERTY(double swipeThreshold READ swipeThreshold NOTIFY settingsChanged)
  Q_PROPERTY(double pathSmoothing READ pathSmoothing NOTIFY settingsChanged)
  Q_PROPERTY(QString activeTheme READ activeTheme NOTIFY settingsChanged)
  Q_PROPERTY(bool settingsVisible READ settingsVisible WRITE setSettingsVisible NOTIFY settingsVisibleChanged)

  // Theme colors (applied from active theme)
  Q_PROPERTY(QString themeBackground READ themeBackground NOTIFY themeChanged)
  Q_PROPERTY(QString themeKeyBackground READ themeKeyBackground NOTIFY themeChanged)
  Q_PROPERTY(QString themeKeyHover READ themeKeyHover NOTIFY themeChanged)
  Q_PROPERTY(QString themeKeyPressed READ themeKeyPressed NOTIFY themeChanged)
  Q_PROPERTY(QString themeKeyBorder READ themeKeyBorder NOTIFY themeChanged)
  Q_PROPERTY(QString themeKeyBorderHover READ themeKeyBorderHover NOTIFY themeChanged)
  Q_PROPERTY(QString themeKeyText READ themeKeyText NOTIFY themeChanged)
  Q_PROPERTY(QString themeSpecialKeyText READ themeSpecialKeyText NOTIFY themeChanged)
  Q_PROPERTY(QString themeCandidateBar READ themeCandidateBar NOTIFY themeChanged)
  Q_PROPERTY(QString themeSwipeTrail READ themeSwipeTrail NOTIFY themeChanged)
  Q_PROPERTY(QStringList availableThemes READ availableThemes CONSTANT)
  Q_PROPERTY(int caretX READ caretX NOTIFY caretPositionChanged)
  Q_PROPERTY(int caretY READ caretY NOTIFY caretPositionChanged)
  Q_PROPERTY(bool hasCaretPosition READ hasCaretPosition NOTIFY caretPositionChanged)
  Q_PROPERTY(int snapToCaretMode READ snapToCaretMode NOTIFY settingsChanged)

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
    swipeSeq_ = 1;

    // Backspace repeat
    backspaceRepeatTimer_.setSingleShot(false);
    backspaceRepeatTimer_.setInterval(
        45); // fast enough to feel native; low overhead
    connect(&backspaceRepeatTimer_, &QTimer::timeout, this, [this]() {
      if (!backspaceHeld_) {
        backspaceRepeatTimer_.stop();
        return;
      }
      sendAction(QStringLiteral("backspace"));
    });

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

    // Load default theme
    loadTheme("default");
  }

  State state() const { return state_; }

  // Settings getters
  double windowOpacity() const { return windowOpacity_; }
  double windowScale() const { return windowScale_; }
  double swipeThreshold() const { return swipeThreshold_; }
  double pathSmoothing() const { return pathSmoothing_; }
  QString activeTheme() const { return activeTheme_; }
  bool settingsVisible() const { return settingsVisible_; }

  // Theme color getters
  QString themeBackground() const { return themeColors_.value("background", "#1a1a2e"); }
  QString themeKeyBackground() const { return themeColors_.value("keyBackground", "#2a2a4a"); }
  QString themeKeyHover() const { return themeColors_.value("keyHover", "#3a3a6a"); }
  QString themeKeyPressed() const { return themeColors_.value("keyPressed", "#5a5a9a"); }
  QString themeKeyBorder() const { return themeColors_.value("keyBorder", "#4a4a6a"); }
  QString themeKeyBorderHover() const { return themeColors_.value("keyBorderHover", "#88c0d0"); }
  QString themeKeyText() const { return themeColors_.value("keyText", "#eceff4"); }
  QString themeSpecialKeyText() const { return themeColors_.value("specialKeyText", "#88c0d0"); }
  QString themeCandidateBar() const { return themeColors_.value("candidateBar", "#0f0f1a"); }
  QString themeSwipeTrail() const { return themeColors_.value("swipeTrail", "#88c0d0"); }
  QStringList availableThemes() const { return QStringList{"default", "dark-blue", "steam-deck", "light"}; }

  // Caret position getters
  int caretX() const { return caretX_; }
  int caretY() const { return caretY_; }
  bool hasCaretPosition() const { return hasCaretPosition_; }
  int snapToCaretMode() const { return snapToCaretMode_; }

  void setSettingsVisible(bool visible) {
    if (settingsVisible_ != visible) {
      settingsVisible_ = visible;
      emit settingsVisibleChanged();
    }
  }

  // Helper for QML to request state changes
  Q_INVOKABLE void requestState(State newState, const QString &reason) {
    setState(newState, reason);
  }

  // Update a setting and send to engine
  Q_INVOKABLE void updateSetting(const QString &key, double value) {
    if (socket_->state() != QLocalSocket::ConnectedState)
      return;
    QString msg = QString("{\"type\":\"setting_update\",\"key\":\"%1\",\"value\":%2}\n")
                      .arg(key)
                      .arg(value);
    socket_->write(msg.toUtf8());
    socket_->flush();
    qDebug() << "Sent setting update:" << key << "=" << value;
  }

  Q_INVOKABLE void updateStringSetting(const QString &key, const QString &value) {
    if (socket_->state() != QLocalSocket::ConnectedState)
      return;
    QString msg = QString("{\"type\":\"setting_update\",\"key\":\"%1\",\"value\":\"%2\"}\n")
                      .arg(key)
                      .arg(value);
    socket_->write(msg.toUtf8());
    socket_->flush();
    qDebug() << "Sent setting update:" << key << "=" << value;
  }

  Q_INVOKABLE void requestSettings() {
    if (socket_->state() != QLocalSocket::ConnectedState)
      return;
    socket_->write("{\"type\":\"settings_request\"}\n");
    socket_->flush();
  }

  Q_INVOKABLE void setActiveTheme(const QString &theme) {
    updateStringSetting("active_theme", theme);
  }

  Q_INVOKABLE void toggleVisibility() {
    if (state_ == Hidden) {
      setState(Passive, "ui_button_toggle");
      if (socket_->state() == QLocalSocket::ConnectedState) {
        socket_->write("{\"type\":\"ui_show\"}\n");
        socket_->flush();
      }
    } else {
      setState(Hidden, "ui_button_toggle");
      if (socket_->state() == QLocalSocket::ConnectedState) {
        socket_->write("{\"type\":\"ui_hide\"}\n");
        socket_->flush();
      }
    }
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
    if (socket_->write(msg.toUtf8()) > 0) {
      socket_->flush();
      qDebug() << "Sent key text=" << key;
    }
  }

  Q_INVOKABLE void backspaceHoldBegin() {
    if (backspaceHeld_)
      return;
    backspaceHeld_ = true;
    backspaceHoldElapsed_.restart();

    // Immediate delete on press
    sendAction(QStringLiteral("backspace"));

    // Start repeating after an initial delay (phone-like)
    QTimer::singleShot(250, this, [this]() {
      if (!backspaceHeld_)
        return;
      backspaceRepeatTimer_.start();
    });
  }

  Q_INVOKABLE void backspaceHoldEnd() {
    backspaceHeld_ = false;
    backspaceRepeatTimer_.stop();
  }

  void sendAction(const QString &action) {
    promoteIfPassive("intent_action");

    if (socket_->state() != QLocalSocket::ConnectedState)
      return;
    QString msg =
        QString("{\"type\":\"action\",\"action\":\"%1\"}\n").arg(action);
    if (socket_->write(msg.toUtf8()) > 0) {
      socket_->flush();
      qDebug() << "Sent action type=" << action;
    }
  }

  // Paste from clipboard: reads clipboard text and sends commit_text to engine
  // This bypasses system Ctrl+V which is unreliable when fcitx5 is running
  void pasteFromClipboard() {
    promoteIfPassive("intent_paste");

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
      qWarning() << "Paste: no clipboard available";
      return;
    }

    QString text = clipboard->text();
    if (text.isEmpty()) {
      qDebug() << "Paste: clipboard empty";
      return;
    }

    if (socket_->state() != QLocalSocket::ConnectedState) {
      qWarning() << "Paste: not connected to engine";
      return;
    }

    // Escape text for JSON: handle quotes, backslashes, newlines
    QString escaped = text;
    escaped.replace("\\", "\\\\");
    escaped.replace("\"", "\\\"");
    escaped.replace("\n", "\\n");
    escaped.replace("\r", "\\r");
    escaped.replace("\t", "\\t");

    QString msg =
        QString("{\"type\":\"commit_text\",\"text\":\"%1\"}\n").arg(escaped);
    if (socket_->write(msg.toUtf8()) > 0) {
      socket_->flush();
      qDebug() << "Sent commit_text len=" << text.length();
    }
  }

  void sendSwipePath(const QVariantList &path) {
    promoteIfPassive("intent_swipe");

    if (socket_->state() != QLocalSocket::ConnectedState)
      return;

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

    lastSwipeSeqSent_ = swipeSeq_++;
    QString msg = QString("{\"type\":\"swipe_path\",\"seq\":%1,\"layout\":\""
                          "qwerty\",\"space\":\"layout\",\"points\":%2}\n")
                      .arg(lastSwipeSeqSent_)
                      .arg(pointsJson);
    socket_->write(msg.toUtf8());
    socket_->flush();
    lastSwipeSentTimer_.restart();
    qDebug() << "Sent swipe_path seq=" << lastSwipeSeqSent_
             << "layout=qwerty points=" << path.size();
  }

  Q_INVOKABLE void sendSwipeWithKeys(const QVariantList &path, const QVariantList &keys) {
    promoteIfPassive("intent_swipe");

    if (socket_->state() != QLocalSocket::ConnectedState)
      return;

    // Build keys JSON array
    QString keysJson = "[";
    for (int i = 0; i < keys.size(); ++i) {
      keysJson += QString("\"%1\"").arg(keys[i].toString());
      if (i < keys.size() - 1)
        keysJson += ",";
    }
    keysJson += "]";

    // Build points JSON (minimal, since we have keys)
    QString pointsJson = "[]";

    lastSwipeSeqSent_ = swipeSeq_++;
    QString msg = QString("{\"type\":\"swipe_path\",\"seq\":%1,\"layout\":\""
                          "qwerty\",\"ui_keys\":%2,\"points\":%3}\n")
                      .arg(lastSwipeSeqSent_)
                      .arg(keysJson)
                      .arg(pointsJson);
    socket_->write(msg.toUtf8());
    socket_->flush();
    lastSwipeSentTimer_.restart();
    qDebug() << "Sent swipe_path seq=" << lastSwipeSeqSent_
             << "ui_keys=" << keys.size();
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
    QString socketPath =
        QString::fromStdString(magickeyboard::ipc::getSocketPath()).trimmed();

    if (socketPath.isEmpty()) {
      qWarning() << "Socket path is empty, cannot connect";
      return;
    }

    // AF_UNIX path limit is usually 108 chars
    if (socketPath.length() > 107) {
      qWarning() << "Socket path is too long:" << socketPath.length()
                 << "chars (max 107)";
      return;
    }

    qDebug() << "Connecting to socket:" << socketPath;
    socket_->connectToServer(socketPath);
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
      QJsonParseError err;
      QJsonDocument doc = QJsonDocument::fromJson(line, &err);
      QJsonObject obj;
      if (doc.isObject()) {
        obj = doc.object();
      } else if (msg.startsWith('{')) {
        qWarning() << "IPC JSON parse error:" << err.errorString()
                   << "at offset" << err.offset
                   << "Line snippet:" << msg.left(64);
      }

      if (obj.value("type").toString() == "ui_intent") {
        if (state_ == Hidden) {
          qDebug() << "Ignored ui_intent (Hidden state)";
        } else {
          QString intent = obj.value("intent").toString();
          if (intent == "key") {
            QString val = obj.value("value").toString();
            if (!val.isEmpty()) {
              qDebug() << "ui_intent intent=key value=" << val;
              sendKey(val);
            }
          } else if (intent == "action") {
            QString val = obj.value("value").toString();
            if (!val.isEmpty()) {
              qDebug() << "ui_intent intent=action value=" << val;
              sendAction(val);
            }
          } else if (intent == "swipe") {
            QString dir = obj.value("dir").toString().toLower();

            // Robust mag parsing: handle numeric or string-quoted numeric
            double mag = 1.0;
            QJsonValue vmag = obj.value("mag");
            if (vmag.isDouble()) {
              mag = vmag.toDouble();
            } else if (vmag.isString()) {
              bool ok = false;
              double v = vmag.toString().toDouble(&ok);
              if (ok)
                mag = v;
            }

            // Harden mag: handle NaN/Inf and clamp to safe range
            if (!std::isfinite(mag))
              mag = 1.0;
            mag = std::clamp(mag, 0.1, 3.0);

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
            else {
              qWarning() << "Ignored ui_intent swipe: unknown dir =" << dir;
              continue;
            }

            QVariantList path;
            path << QVariantMap{{"x", 0.0}, {"y", 0.0}};
            path << QVariantMap{{"x", dx}, {"y", dy}};
            sendSwipePath(path);
          } else if (intent == "swipe_path") {
            QString layout = obj.value("layout").toString();
            if (layout.isEmpty())
              layout = "qwerty";
            QJsonValue vpoints = obj.value("points");
            if (vpoints.isArray()) {
              QVariantList path;
              for (const auto &v : vpoints.toArray()) {
                if (v.isObject()) {
                  QJsonObject po = v.toObject();
                  path << QVariantMap{{"x", po.value("x").toDouble()},
                                      {"y", po.value("y").toDouble()}};
                }
              }
              if (!path.isEmpty()) {
                qDebug() << "ui_intent intent=swipe_path layout=" << layout
                         << "points=" << path.size();
                sendSwipePath(path);
              }
            }
          }
        }
      } else {
        // Handle other message types via JSON or substring fallback
        QString type = obj.value("type").toString();
        if (type == "ui_show" || type == "show" ||
            msg.contains("\"type\":\"show\"") ||
            msg.contains("\"type\":\"ui_show\"")) {
          qDebug() << "Received: show -> Passive";
          setState(Passive, "ipc_show");
        } else if (type == "ui_hide" || type == "hide" ||
                   msg.contains("\"type\":\"hide\"") ||
                   msg.contains("\"type\":\"ui_hide\"")) {
          qDebug() << "Received: hide -> Hidden";
          setState(Hidden, "ipc_hide");
        } else if (type == "ui_toggle" ||
                   msg.contains("\"type\":\"ui_toggle\"")) {
          if (lastToggleTimer_.elapsed() < 100) {
            qDebug() << "Ignoring rapid toggle (<100ms)";
          } else {
            lastToggleTimer_.restart();
            toggleCount_++;
            if (toggleLogTimer_.elapsed() >= 1000) {
              if (toggleCount_ > 1)
                qDebug() << "Accepted toggle x" << toggleCount_
                         << " in last 1s";
              else
                qDebug() << "Accepted toggle (fd" << socket_->socketDescriptor()
                         << ")";
              toggleLogTimer_.restart();
              toggleCount_ = 0;
            } else if (toggleCount_ == 1) {
              qDebug() << "Accepted toggle (fd" << socket_->socketDescriptor()
                       << ")";
            }
            if (state_ == Hidden)
              setState(Passive, "ipc_toggle");
            else if (state_ == Active)
              setState(Passive, "ipc_toggle");
            else // Passive
              setState(Hidden, "ipc_toggle");
          }
        } else if (type == "swipe_keys" ||
                   msg.contains("\"type\":\"swipe_keys\"")) {
          QStringList keys;
          QJsonValue vkeys = obj.value("keys");
          if (vkeys.isArray()) {
            for (const auto &v : vkeys.toArray())
              keys << v.toString();
          } else {
            // Legacy/Robust fallback logic
            int arrKeyPos = msg.indexOf("\"keys\":[");
            if (arrKeyPos >= 0) {
              int arrStart = arrKeyPos + 8;
              int arrEnd = msg.indexOf("]", arrStart);
              if (arrEnd > arrStart) {
                QString content = msg.mid(arrStart, arrEnd - arrStart);
                for (auto &item : content.split(",", Qt::SkipEmptyParts))
                  keys << item.trimmed().remove("\"");
              }
            }
          }
          uint64_t seq = 0;
          bool hasSeq = obj.contains("seq");
          if (hasSeq) {
            seq = obj.value("seq").toVariant().toULongLong();
          }

          if (lastSwipeSentTimer_.isValid() && hasSeq &&
              seq == lastSwipeSeqSent_) {
            qDebug() << "Received swipe_keys seq=" << seq
                     << "count=" << keys.size()
                     << "latency_ms=" << lastSwipeSentTimer_.elapsed()
                     << "keys=" << keys.join("-");
            lastSwipeSentTimer_.invalidate();
          } else {
            qDebug() << "Received swipe_keys count=" << keys.size()
                     << "seq=" << (hasSeq ? QString::number(seq) : "missing")
                     << (hasSeq ? "(mismatch or stale)" : "(dropping)");
          }
          emit swipeKeysReceived(keys);
        } else if (type == "swipe_candidates" ||
                   msg.contains("\"type\":\"swipe_candidates\"")) {
          QStringList words;
          QJsonValue vcands = obj.value("candidates");
          if (vcands.isArray()) {
            for (const auto &v : vcands.toArray()) {
              if (v.isObject())
                words << v.toObject().value("w").toString();
              else if (v.isString())
                words << v.toString();
            }
          } else {
            // Legacy fallback
            int arrStart = msg.indexOf("\"candidates\":[");
            if (arrStart >= 0) {
              arrStart += 14;
              int arrEnd = msg.indexOf("]", arrStart);
              if (arrEnd > arrStart) {
                QString content = msg.mid(arrStart, arrEnd - arrStart);
                int wPos = 0;
                while ((wPos = content.indexOf("\"w\":\"", wPos)) >= 0) {
                  int wStart = wPos + 5;
                  int wEnd = content.indexOf("\"", wStart);
                  if (wEnd > wStart)
                    words << content.mid(wStart, wEnd - wStart);
                  wPos = wEnd;
                }
              }
            }
          }
          uint64_t seq = 0;
          bool hasSeq = obj.contains("seq");
          if (hasSeq) {
            seq = obj.value("seq").toVariant().toULongLong();
          }

          if (hasSeq && seq != lastSwipeSeqSent_) {
            qDebug() << "Received swipe_candidates count=" << words.size()
                     << "seq=" << seq << "(stale, expected" << lastSwipeSeqSent_
                     << ")";
          } else {
            qDebug() << "Received swipe_candidates count=" << words.size()
                     << "seq=" << (hasSeq ? QString::number(seq) : "missing");
            emit swipeCandidatesReceived(words);
          }
        } else if (type == "settings" ||
                   msg.contains("\"type\":\"settings\"")) {
          // Parse settings from engine
          bool changed = false;

          if (obj.contains("window_opacity")) {
            double v = obj.value("window_opacity").toDouble();
            if (v != windowOpacity_) {
              windowOpacity_ = v;
              changed = true;
            }
          }
          if (obj.contains("window_scale")) {
            double v = obj.value("window_scale").toDouble();
            if (v != windowScale_) {
              windowScale_ = v;
              changed = true;
            }
          }
          if (obj.contains("swipe_threshold_px")) {
            double v = obj.value("swipe_threshold_px").toDouble();
            if (v != swipeThreshold_) {
              swipeThreshold_ = v;
              changed = true;
            }
          }
          if (obj.contains("path_smoothing")) {
            double v = obj.value("path_smoothing").toDouble();
            if (v != pathSmoothing_) {
              pathSmoothing_ = v;
              changed = true;
            }
          }
          if (obj.contains("active_theme")) {
            QString v = obj.value("active_theme").toString();
            if (v != activeTheme_) {
              activeTheme_ = v;
              changed = true;
              // Load the new theme
              loadTheme(v.isEmpty() ? "default" : v);
            }
          }

          if (obj.contains("snap_to_caret_mode")) {
            int v = obj.value("snap_to_caret_mode").toInt();
            if (v != snapToCaretMode_) {
              snapToCaretMode_ = v;
              changed = true;
            }
          }

          if (changed) {
            qDebug() << "Settings updated: opacity=" << windowOpacity_
                     << "scale=" << windowScale_
                     << "swipeThreshold=" << swipeThreshold_
                     << "theme=" << activeTheme_;
            emit settingsChanged();
          }
        } else if (type == "caret_position" ||
                   msg.contains("\"type\":\"caret_position\"")) {
          // Parse caret position
          bool available = obj.value("available").toBool(true);
          if (available && obj.contains("x") && obj.contains("y")) {
            caretX_ = obj.value("x").toInt();
            caretY_ = obj.value("y").toInt();
            hasCaretPosition_ = true;
            qDebug() << "Caret position:" << caretX_ << caretY_;
          } else {
            hasCaretPosition_ = false;
          }
          emit caretPositionChanged();
        }
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
  QElapsedTimer lastSwipeSentTimer_; // For latency tracking
  uint64_t swipeSeq_ = 1;
  uint64_t lastSwipeSeqSent_ = 0;
  int toggleCount_ = 0; // Toggles in current 1s window

  // Backspace repeat state
  QTimer backspaceRepeatTimer_;
  QElapsedTimer backspaceHoldElapsed_;
  bool backspaceHeld_ = false;

  // Settings (synced from engine)
  double windowOpacity_ = 1.0;
  double windowScale_ = 1.0;
  double swipeThreshold_ = 12.0;
  double pathSmoothing_ = 0.35;
  QString activeTheme_ = "";
  bool settingsVisible_ = false;

  // Theme colors
  QMap<QString, QString> themeColors_;

  // Caret position for snap-to-caret
  int caretX_ = 0;
  int caretY_ = 0;
  bool hasCaretPosition_ = false;
  int snapToCaretMode_ = 0;

  void loadTheme(const QString &themeName) {
    // Find theme file
    QStringList searchPaths = {
        QDir::homePath() + "/.local/share/magic-keyboard/themes/" + themeName +
            ".json",
        "/usr/local/share/magic-keyboard/themes/" + themeName + ".json",
        "/usr/share/magic-keyboard/themes/" + themeName + ".json"};

    for (const QString &path : searchPaths) {
      QFile file(path);
      if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
        if (doc.isObject()) {
          QJsonObject colors = doc.object().value("colors").toObject();
          themeColors_.clear();
          for (auto it = colors.begin(); it != colors.end(); ++it) {
            themeColors_[it.key()] = it.value().toString();
          }
          qDebug() << "Loaded theme:" << themeName << "from" << path;
          emit themeChanged();
          return;
        }
      }
    }

    // Use default colors if theme not found
    themeColors_.clear();
    themeColors_["background"] = "#1a1a2e";
    themeColors_["keyBackground"] = "#2a2a4a";
    themeColors_["keyHover"] = "#3a3a6a";
    themeColors_["keyPressed"] = "#5a5a9a";
    themeColors_["keyBorder"] = "#4a4a6a";
    themeColors_["keyBorderHover"] = "#88c0d0";
    themeColors_["keyText"] = "#eceff4";
    themeColors_["specialKeyText"] = "#88c0d0";
    themeColors_["candidateBar"] = "#0f0f1a";
    themeColors_["swipeTrail"] = "#88c0d0";
    emit themeChanged();
  }

signals:
  void stateChanged();
  void swipeKeysReceived(const QStringList &keys);
  void swipeCandidatesReceived(const QStringList &candidates);
  void settingsChanged();
  void settingsVisibleChanged();
  void themeChanged();
  void caretPositionChanged();
};

// Emergency kill handler - restores focus instantly by hard-exiting UI process
// This is the MANDATORY escape hatch when UI steals focus and breaks typing
static void emergencyKillHandler(int signum) {
  if (signum == SIGUSR1) {
    // NO Qt cleanup - immediate hard exit to restore focus
    // fcitx5 engine process remains running
    _exit(0);
  }
}

int main(int argc, char *argv[]) {
  // Register emergency kill signal BEFORE Qt init (CRITICAL for focus recovery)
  std::signal(SIGUSR1, emergencyKillHandler);

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

          // Caret-based positioning (snap-to-caret feature)
          QObject::connect(
              &bridge, &KeyboardBridge::caretPositionChanged, window,
              [&bridge, window]() {
                if (bridge.snapToCaretMode() == 0)
                  return; // Snap disabled

                QScreen *screen = QGuiApplication::primaryScreen();
                if (!screen)
                  return;

                QRect r = screen->availableGeometry();
                int w = window->width();
                int h = window->height();

                if (bridge.hasCaretPosition()) {
                  int caretX = bridge.caretX();
                  int caretY = bridge.caretY();

                  // Mode 1: Below caret, Mode 2: Above caret, Mode 3: Smart
                  int newX = caretX - w / 2;
                  int newY;

                  if (bridge.snapToCaretMode() == 2) {
                    // Above caret
                    newY = caretY - h - 20;
                  } else if (bridge.snapToCaretMode() == 3) {
                    // Smart: above if caret is in bottom half
                    if (caretY > r.height() / 2) {
                      newY = caretY - h - 20;
                    } else {
                      newY = caretY + 40;
                    }
                  } else {
                    // Below caret (mode 1 or default)
                    newY = caretY + 40;
                  }

                  // Clamp to screen bounds
                  newX = qBound(r.left(), newX, r.right() - w);
                  newY = qBound(r.top(), newY, r.bottom() - h);

                  window->setPosition(newX, newY);
                  qDebug() << "Snapped to caret:" << newX << newY;
                } else {
                  // Fallback: bottom center
                  window->setPosition((r.width() - w) / 2, r.height() - h - 20);
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
