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

    connect(socket_, &QLocalSocket::connected, this,
            [this]() { qDebug() << "Connected to engine"; });

    connect(socket_, &QLocalSocket::disconnected, this, [this]() {
      qDebug() << "Disconnected, retrying...";
      QTimer::singleShot(1000, this, &KeyboardBridge::connectToEngine);
    });

    connect(socket_, &QLocalSocket::readyRead, this,
            &KeyboardBridge::onReadyRead);

    connect(socket_, &QLocalSocket::errorOccurred, this, [this]() {
      qDebug() << "Socket error:" << socket_->errorString();
      QTimer::singleShot(1000, this, &KeyboardBridge::connectToEngine);
    });
  }

  bool isVisible() const { return visible_; }
  void setVisible(bool v) {
    if (visible_ != v) {
      visible_ = v;
      emit visibleChanged();
    }
  }

public slots:
  void connectToEngine() {
    QString path = QString::fromStdString(magickeyboard::ipc::getSocketPath());
    qDebug() << "Connecting to:" << path;
    socket_->connectToServer(path);
  }

  void sendKey(const QString &key) {
    QString msg = QString("{\"type\":\"key\",\"text\":\"%1\"}\n").arg(key);
    socket_->write(msg.toUtf8());
    socket_->flush();
    qDebug() << "Sent:" << key;
  }

signals:
  void visibleChanged();
  void showKeyboard();
  void hideKeyboard();

private slots:
  void onReadyRead() {
    buffer_ += socket_->readAll();

    int idx;
    while ((idx = buffer_.indexOf('\n')) >= 0) {
      QByteArray line = buffer_.left(idx);
      buffer_.remove(0, idx + 1);

      QString msg = QString::fromUtf8(line);
      qDebug() << "Received:" << msg;

      if (msg.contains("\"type\":\"show\"")) {
        setVisible(true);
        emit showKeyboard();
      } else if (msg.contains("\"type\":\"hide\"")) {
        setVisible(false);
        emit hideKeyboard();
      }
    }
  }

private:
  QLocalSocket *socket_;
  QByteArray buffer_;
  bool visible_ = false;
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

          // Connect visibility
          QObject::connect(&bridge, &KeyboardBridge::showKeyboard, window,
                           [window]() { window->show(); });
          QObject::connect(&bridge, &KeyboardBridge::hideKeyboard, window,
                           [window]() { window->hide(); });

          // Start hidden, connect to engine
          window->hide();
          bridge.connectToEngine();

          qDebug() << "Window ready, hidden until activation";
        }
      },
      Qt::QueuedConnection);

  engine.load(url);

  return app.exec();
}

#include "main.moc"
