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

class KeyboardBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibleChanged)

public:
  explicit KeyboardBridge(QObject *parent = nullptr) : QObject(parent) {
    socket_ = new QLocalSocket(this);
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);
    reconnectTimer_->setInterval(1000);

    connect(socket_, &QLocalSocket::connected, this, [this]() {
      qDebug() << "Connected to engine";
      reconnecting_ = false;
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
              qDebug() << "Socket error:" << socket_->errorString();
              scheduleReconnect();
            });

    connect(reconnectTimer_, &QTimer::timeout, this,
            &KeyboardBridge::tryConnect);
  }

  bool isVisible() const { return visible_; }
  void setVisible(bool v) {
    if (visible_ != v) {
      visible_ = v;
      emit visibleChanged();
    }
  }

public slots:
  void connectToEngine() { tryConnect(); }

  void sendKey(const QString &key) {
    if (socket_->state() != QLocalSocket::ConnectedState)
      return;
    QString msg = QString("{\"type\":\"key\",\"text\":\"%1\"}\n").arg(key);
    socket_->write(msg.toUtf8());
    socket_->flush();
  }

signals:
  void visibleChanged();
  void showKeyboard();
  void hideKeyboard();

private slots:
  void tryConnect() {
    // Only connect if in unconnected state
    if (socket_->state() != QLocalSocket::UnconnectedState) {
      qDebug() << "Socket not unconnected, aborting first";
      socket_->abort();
      // Wait for state to settle
      QTimer::singleShot(100, this, &KeyboardBridge::tryConnect);
      return;
    }

    reconnecting_ = false;
    QString path = QString::fromStdString(magickeyboard::ipc::getSocketPath());
    qDebug() << "Connecting to:" << path;
    socket_->connectToServer(path);
  }

  void scheduleReconnect() {
    if (reconnecting_)
      return;
    reconnecting_ = true;
    reconnectTimer_->start();
  }

  void onReadyRead() {
    buffer_ += socket_->readAll();

    int idx;
    while ((idx = buffer_.indexOf('\n')) >= 0) {
      QByteArray line = buffer_.left(idx);
      buffer_.remove(0, idx + 1);

      QString msg = QString::fromUtf8(line);

      if (msg.contains("\"type\":\"show\"")) {
        qDebug() << "Received: show";
        setVisible(true);
        emit showKeyboard();
      } else if (msg.contains("\"type\":\"hide\"")) {
        qDebug() << "Received: hide";
        setVisible(false);
        emit hideKeyboard();
      }
    }
  }

private:
  QLocalSocket *socket_;
  QTimer *reconnectTimer_;
  QByteArray buffer_;
  bool visible_ = false;
  bool reconnecting_ = false;
};

int main(int argc, char *argv[]) {
  QGuiApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QGuiApplication app(argc, argv);
  app.setApplicationName("Magic Keyboard");

  qDebug() << "Magic Keyboard UI starting";
  qDebug() << "Socket:"
           << QString::fromStdString(magickeyboard::ipc::getSocketPath());

  KeyboardBridge bridge;

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

          QObject::connect(&bridge, &KeyboardBridge::showKeyboard, window,
                           [window]() { window->show(); });
          QObject::connect(&bridge, &KeyboardBridge::hideKeyboard, window,
                           [window]() { window->hide(); });

          // Start hidden
          window->hide();

          // Connect to engine
          bridge.connectToEngine();

          qDebug() << "Window ready, hidden until focus";
        }
      },
      Qt::QueuedConnection);

  engine.load(url);

  return app.exec();
}

#include "main.moc"
