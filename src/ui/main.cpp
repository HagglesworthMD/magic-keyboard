/**
 * Magic Keyboard UI - Main Entry Point
 * 
 * Creates a non-focusable keyboard window that:
 * - Never steals focus from target application
 * - Stays on top of other windows
 * - Communicates with engine via Unix socket
 */

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QLocalSocket>

#include "protocol.h"

#include <QDebug>

/**
 * Apply window flags to prevent focus stealing.
 * 
 * CRITICAL: These flags ensure the keyboard never becomes the active window
 * and focus stays with the application the user is typing into.
 */
void applyWindowFlags(QQuickWindow* window) {
    if (!window) return;
    
    window->setFlags(
        Qt::Tool |                          // Utility window (not in taskbar)
        Qt::FramelessWindowHint |           // No window decorations
        Qt::WindowStaysOnTopHint |          // Always on top
        Qt::WindowDoesNotAcceptFocus        // NEVER take focus
    );
    
    // Additional hint for window managers
    window->setProperty("_q_showWithoutActivating", true);
    
    qDebug() << "Applied non-focusable window flags";
}

/**
 * Position keyboard at bottom center of primary screen.
 */
void positionKeyboard(QQuickWindow* window) {
    if (!window) return;
    
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    
    QRect available = screen->availableGeometry();
    
    // Keyboard dimensions (will be set by QML, use defaults here)
    int kbWidth = 800;
    int kbHeight = 280;
    
    int x = (available.width() - kbWidth) / 2;
    int y = available.height() - kbHeight - 20;  // 20px margin from bottom
    
    window->setGeometry(x, y, kbWidth, kbHeight);
    
    qDebug() << "Positioned keyboard at:" << x << y;
}

int main(int argc, char* argv[]) {
    // Set application attributes before creating QGuiApplication
    QGuiApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    
    QGuiApplication app(argc, argv);
    app.setApplicationName("Magic Keyboard");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("MagicKeyboard");
    
    qDebug() << "Magic Keyboard UI starting...";
    qDebug() << "Socket path:" << QString::fromStdString(magickeyboard::ipc::getSocketPath());
    
    // Create QML engine
    QQmlApplicationEngine engine;
    
    // Load main QML file
    const QUrl url(QStringLiteral("qrc:/MagicKeyboard/KeyboardWindow.qml"));
    
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject* obj, const QUrl& objUrl) {
        if (!obj && url == objUrl) {
            qCritical() << "Failed to load KeyboardWindow.qml";
            QCoreApplication::exit(-1);
            return;
        }
        
        // Get the window and apply our flags
        QQuickWindow* window = qobject_cast<QQuickWindow*>(obj);
        if (window) {
            applyWindowFlags(window);
            positionKeyboard(window);
            
            qDebug() << "Keyboard window created successfully";
        }
    }, Qt::QueuedConnection);
    
    engine.load(url);
    
    // TODO: Connect to engine socket
    // QLocalSocket socket;
    // socket.connectToServer(QString::fromStdString(magickeyboard::ipc::getSocketPath()));
    
    return app.exec();
}
