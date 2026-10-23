#pragma once
#include <ConfigurationRpcProtocol.h>
#include <QHostAddress>
#include <QList>
#include <QNetworkInterface>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUdpSocket>
#include <functional>
#include <utility>

// Finding VP on the network, exactly as the Config GUI does. Everything inside
// the namespace is the developer's code, copied verbatim from his Config GUI's
// AppMain.cpp (FoundTarget, LanTargetWatcher, parseTargetAddress); only the
// namespace is ours. Each PC searches for itself.
namespace LevelsScopeRigSearch
{
struct FoundTarget
{
	QString instanceId;
	QString label;
	QString address;
	quint16 port = 41686;
	bool local = false;
	QString vpVersion;
};

class LanTargetWatcher final : public QObject
{
	public:
	explicit LanTargetWatcher(QObject* parent = nullptr) : QObject(parent)
	{
		settle_.setSingleShot(true);
		connect(&socket_, &QUdpSocket::readyRead, this,
			[this] { readReplies(); });
		connect(&settle_, &QTimer::timeout, this, [this]
		{
			scanning_ = false;
			const bool stillLooking = results_ && results_(found_);
			if (rescanRequested_)
			{
				rescanRequested_ = false;
				QTimer::singleShot(0, this, [this] { scan(); });
			}
			else periodic_.start(stillLooking ? 1500 : 15000);
		});
		periodic_.setSingleShot(true);
		connect(&periodic_, &QTimer::timeout, this, [this] { scan(); });
	}
	void setResultsHandler(std::function<bool(const QList<FoundTarget>&)> handler)
	{
		results_ = std::move(handler);
	}
	void scan()
	{
		using namespace ConfigurationRpcProtocol;
		if (scanning_)
		{
			rescanRequested_ = true;
			return;
		}
		periodic_.stop();
		if (socket_.state() != QAbstractSocket::BoundState &&
			!socket_.bind(QHostAddress::AnyIPv4, 0))
		{
			const bool stillLooking = results_ && results_({});
			periodic_.start(stillLooking ? 1500 : 15000);
			return;
		}
		Frame query;
		query.operation = static_cast<uint16_t>(Operation::DiscoveryQuery);
		std::vector<uint8_t> bytes;
		if (!Encode(query, bytes)) return;
		found_.clear();
		scanning_ = true;
		QSet<QHostAddress> broadcasts;
		broadcasts.insert(QHostAddress::Broadcast);
		broadcasts.insert(QHostAddress::LocalHost);
		for (const auto& adapter : QNetworkInterface::allInterfaces())
		{
			if (!(adapter.flags() & QNetworkInterface::IsUp) ||
				!(adapter.flags() & QNetworkInterface::CanBroadcast)) continue;
			for (const auto& address : adapter.addressEntries())
				if (address.ip().protocol() == QAbstractSocket::IPv4Protocol &&
					!address.broadcast().isNull())
					broadcasts.insert(address.broadcast());
		}
		for (const auto& broadcast : broadcasts)
			socket_.writeDatagram(reinterpret_cast<const char*>(bytes.data()),
				static_cast<qint64>(bytes.size()), broadcast, 41687);
		settle_.start(600);
	}
	private:
	void readReplies()
	{
		using namespace ConfigurationRpcProtocol;
		while (socket_.hasPendingDatagrams())
		{
			QByteArray datagram;
			datagram.resize(static_cast<qsizetype>(
				socket_.pendingDatagramSize()));
			QHostAddress sender;
			const qint64 received = socket_.readDatagram(datagram.data(),
				datagram.size(), &sender);
			Frame reply;
			DiscoveryAdvertisement identity;
			if (!scanning_ || received <= 0 || !Decode(
				reinterpret_cast<const uint8_t*>(datagram.constData()),
				static_cast<size_t>(received), reply) ||
				!ParseDiscoveryReply(reply, identity)) continue;
			const QString id = QString::fromUtf8(identity.instanceId);
			const bool local = sender.isLoopback();
			const QString version = QString::fromUtf8(identity.vpVersion);
			FoundTarget candidate{ id,
				QStringLiteral("%1 (%2) · %3").arg(
					local ? QStringLiteral("This computer") :
						QString::fromUtf8(identity.computerName),
					sender.toString(), version),
				sender.toString(), identity.rpcPort, local, version };
			bool duplicate = false;
			for (auto& target : found_)
			{
				if (target.instanceId != id) continue;
				if (local && !target.local) target = candidate;
				duplicate = true;
				break;
			}
			if (!duplicate && found_.size() < 10)
				found_.push_back(candidate);
			// Loopback identifies the VP on this computer. Offer it as soon as
			// it answers instead of waiting for the LAN collection interval.
			if (local && results_) results_(found_);
		}
	}
	QUdpSocket socket_;
	QTimer settle_;
	QTimer periodic_;
	QList<FoundTarget> found_;
	std::function<bool(const QList<FoundTarget>&)> results_;
	bool scanning_ = false;
	bool rescanRequested_ = false;
};

bool parseTargetAddress(const QString& value, QString& host, quint16& port)
{
	const QString trimmed = value.trimmed();
	const int colon = trimmed.lastIndexOf(u':');
	port = 41686;
	host = trimmed;
	if (colon > 0)
	{
		bool valid = false;
		const uint parsed = trimmed.mid(colon + 1).toUInt(&valid);
		if (!valid || parsed == 0 || parsed > 65535) return false;
		host = trimmed.left(colon);
		port = static_cast<quint16>(parsed);
	}
	return !host.isEmpty();
}
}
