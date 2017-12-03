#include "remoteipc.h"
#include "helpers.h"
#include <QUrl>

namespace Etherwall {

    RemoteIPC::RemoteIPC(GethLog& gethLog) :
        EtherIPC(gethLog),
        fWebSocket("http://localhost"), fEndpoint(), fReceivedMessage()
    {
        QObject::connect(&fWebSocket, &QWebSocket::disconnected, this, &RemoteIPC::onDisconnectedWS);
        QObject::connect(&fWebSocket, &QWebSocket::connected, this, &RemoteIPC::onConnectedWS);
        QObject::connect(&fWebSocket, (void (QWebSocket::*)(QAbstractSocket::SocketError))&QWebSocket::error, this, &RemoteIPC::onErrorWS);
        QObject::connect(&fWebSocket, &QWebSocket::textMessageReceived, this, &RemoteIPC::onTextMessageReceivedWS);

        const QSettings settings;
        fIsThinClient = settings.value("geth/thinclient", true).toBool();
    }

    RemoteIPC::~RemoteIPC()
    {
        fWebSocket.close(); // in case we missed the app closing
    }

    void RemoteIPC::getLogs(const QStringList &addresses, const QJsonArray &topics, quint64 fromBlock, const QString& internalID)
    {
        if ( !fIsThinClient ) {
            return EtherIPC::getLogs(addresses, topics, fromBlock, internalID);
        }

        // do nothing on remote, getlogs is too expensive we just don't support it on a thin client
    }

    bool RemoteIPC::closeApp()
    {
        bool result = EtherIPC::closeApp();

        // wait for websocket if we're still not disconnected (only after all others are done tho!)
        if ( result && fWebSocket.state() != QAbstractSocket::UnconnectedState ) {
            fWebSocket.close();
            return false;
        }

        return result;
    }

    void RemoteIPC::setInterval(int interval)
    {
        Q_UNUSED(interval); // remote enforced to 10s
        fTimer.setInterval(10000);
    }

    void RemoteIPC::start(const QString &version, const QString &endpoint, const QString &warning)
    {
        Q_UNUSED(version); // TODO
        Q_UNUSED(warning);

        const QSettings settings; // reinit because of first time dialog
        fIsThinClient = settings.value("geth/thinclient", true).toBool();
        fEndpoint = endpoint;

        connectWebsocket();
        EtherIPC::start(version, endpoint, warning);
    }

    void RemoteIPC::finishInit()
    {
        // hold off until WS is done too if remote
        if ( !fIsThinClient || fWebSocket.state() == QAbstractSocket::ConnectedState ) {
            EtherIPC::finishInit();
        }
    }

    bool RemoteIPC::endpointWritable()
    {
        if ( isRemoteRequest() ) {
            return true;
        }

        return EtherIPC::endpointWritable();
    }

    qint64 RemoteIPC::endpointWrite(const QByteArray &data)
    {
        if ( isRemoteRequest() ) {
            qint64 sent = fWebSocket.sendBinaryMessage(data);
            return sent;
        }

        return EtherIPC::endpointWrite(data);
    }

    const QByteArray RemoteIPC::endpointRead()
    {
        if ( isRemoteRequest() ) {
            const QByteArray result = fReceivedMessage;
            fReceivedMessage.clear(); // ensure we get empties if this gets called out of order
            return result;
        }

        return EtherIPC::endpointRead();
    }

    const QStringList RemoteIPC::buildGethArgs()
    {
        QStringList args = EtherIPC::buildGethArgs();
        if ( fIsThinClient ) {
            args.append("--maxpeers=0");
            args.append("--nodiscover");
            args.append("--nat=none");
        }

        return args;
    }

    void RemoteIPC::onConnectedWS()
    {
        EtherLog::logMsg("Connected to WS endpoint", LS_Info);
        // if IPC is connected at this stage continue with init
        if ( fSocket.state() == QLocalSocket::ConnectedState ) {
            EtherIPC::finishInit();
        }
    }

    void RemoteIPC::onDisconnectedWS()
    {
        if ( !fClosingApp ) {
            setError("WS: Disconnected from websocket");
            bail();
        }
    }

    void RemoteIPC::onErrorWS(QAbstractSocket::SocketError error)
    {
        Q_UNUSED(error);
        setError("WS: " + fSocket.errorString());
        bail();
    }

    void RemoteIPC::onTextMessageReceivedWS(const QString &msg)
    {
        fReceivedMessage = msg.toUtf8();
        onSocketReadyRead();
    }

    bool RemoteIPC::isRemoteRequest() const
    {
        if ( !fIsThinClient ) {
            return false; // all are considered local in this case
        }

        switch ( fActiveRequest.getType() ) {
            // remote
            case GetBlockNumber: return true;
            case GetBalance: return true;
            case GetTransactionCount: return true;
            case SendRawTransaction: return true;
            case GetGasPrice: return true;
            case EstimateGas: return true;
            case NewBlockFilter: return true;
            case NewEventFilter: return true;
            case GetFilterChanges: return true;
            case UninstallFilter: return true;
            case GetTransactionByHash: return true;
            case GetBlock: return true;
            case GetTransactionReceipt: return true;
            case Call: return true;
            // local
            case NoRequest: return false;
            case NewAccount: return false;
            case UnlockAccount: return false;
            case SignTransaction: return false;
            case GetAccountRefs: return false;
            case SendTransaction: return false;
            case GetClientVersion: return false;
            case GetNetVersion: return false;
            case GetSyncing: return false;
            case GetPeerCount: return false; // only "eth" available
            case GetLogs: return false; // we could use remote but this is a very heavy call, better not allow it
        }

        return false; // better safe than sorry
    }

    void RemoteIPC::connectWebsocket()
    {
        if ( fIsThinClient && fWebSocket.state() == QAbstractSocket::UnconnectedState && !fEndpoint.isEmpty() ) {
            EtherLog::logMsg("Connecting to WS endpoint: " + fEndpoint, LS_Info);
            fWebSocket.open(QUrl(fEndpoint));
        }
    }

    bool RemoteIPC::isThinClient() const
    {
        return fIsThinClient;
    }

}