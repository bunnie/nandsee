#include <QDebug>
#include <QTextStream>
#include "event.h"
#include "byteswap.h"

static QList<QString> eventTypes;


static qreal getEntropy(QByteArray &data)
{
	if (data.size() <= 2)
		return 1;
	QByteArray smaller = qCompress(data, 9);
	qreal ratio = (smaller.size()*1.0)/(data.size()*1.0);
	if (ratio > 1.0)
		ratio = 9999.0/10000.0;
	return ratio;
}

QList<QString> &EventTypes()
{
    if (eventTypes.length() <= 0) {
        eventTypes.append("EVT_HELLO");
        eventTypes.append("EVT_SD_CMD");
        eventTypes.append("EVT_BUFFER_DRAIN");
        eventTypes.append("EVT_NET_CMD");
        eventTypes.append("EVT_RESET");
        eventTypes.append("EVT_NAND_ID");
        eventTypes.append("EVT_NAND_STATUS");
        eventTypes.append("EVT_NAND_PARAMETER_READ");
        eventTypes.append("EVT_NAND_READ");
        eventTypes.append("EVT_NAND_CHANGE_READ_COLUMN");
        eventTypes.append("EVT_NAND_UNKNOWN");
        eventTypes.append("EVT_NAND_RESET");
		eventTypes.append("EVT_UNKNOWN");
		eventTypes.append("EVT_NAND_DATA");

        while (eventTypes.size() <= EVT_NAND_CACHE1-1)
            eventTypes.append("");
        eventTypes.append("EVT_NAND_CACHE1");
        eventTypes.append("EVT_NAND_CACHE2");
        eventTypes.append("EVT_NAND_CACHE3");
        eventTypes.append("EVT_NAND_CACHE4");

        while (eventTypes.size() <= EVT_NAND_SANDISK_VENDOR_START-1)
            eventTypes.append("");
        eventTypes.append("EVT_NAND_SANDISK_VENDOR_START");
        eventTypes.append("EVT_NAND_SANDISK_VENDOR_PARAM");
        eventTypes.append("EVT_NAND_SANDISK_CHARGE1");
	}
    return eventTypes;
}

static qint64 streamReadData(QIODevice &source, char *data, qint64 bytesTotal) {
	qint64 bytesRead = 0;
	qint64 bytesLeft = bytesTotal;
	int result;

	while (bytesLeft > 0) {
		result = source.read(data, bytesLeft);
		if (result < 0) {
			qDebug() << "Source had error:" << source.errorString();
			return result;
		}

		if (result == 0) {
			if (!source.waitForReadyRead(-1)) {
				qDebug() << "Source is done, closing";
				break;
			}
		}

		if (result > bytesTotal) {
			qDebug() << "Very bad.  Wanted " << bytesTotal << ", but got " << result;
		}
		bytesRead += result;
		data += result;
		if (bytesLeft - result < 0) {
			qDebug() << "That was a really strange read";
		}
		bytesLeft -= result;
	}
	if (bytesLeft < 0) {
		qDebug() << "How could bytesLeft (" << bytesLeft << ") be less than zero?";
	}
	return bytesRead;
}


Event::Event(QObject *parent) :
    QObject(parent)
{
	memset(&evt, 0, sizeof(evt));
}

Event::Event(QIODevice &source, QObject *parent) :
	QObject(parent)
{
	qint64 bytesRead;

	memset(&evt, 0, sizeof(evt));
	bytesRead = streamReadData(source, (char *)(&evt.header), sizeof(evt.header));
	if (bytesRead != sizeof(evt.header)) {
		qDebug() << "Read an unexpected number of bytes:" << bytesRead << "vs" << sizeof(evt.header);
	}
	if (_ntohl(evt.header.size) > sizeof(evt)) {
		qDebug() << "Header size is VERY wrong:" << _ntohl(evt.header.size);
	}

	bytesRead = streamReadData(source,
                               ((char *)(&evt.header)) + sizeof(evt.header),
                               _ntohl(evt.header.size) - sizeof(evt.header));
	decodeEvent();
}

Event::Event(QByteArray &data, QObject *parent) :
	QObject(parent)
{
	memset(&evt, 0, sizeof(evt));
	size_t size = data.length();
	if (size > sizeof(evt))
		size = sizeof(evt);
	memcpy(&evt, data, size);
	decodeEvent();
}

Event::Event(const Event &other, QObject *parent) :
	QObject(parent)
{
    memcpy(&evt, &other.evt, sizeof(evt));
	decodeEvent();
	setIndex(other.eventIndex);
}

Event &Event::operator=(const Event &other)
{
    memcpy(&evt, &other.evt, sizeof(evt));
	decodeEvent();
    return *this;
}

bool Event::operator<(const Event &other) const
{
	if (secondsStart() < other.secondsStart())
        return true;
	if (secondsStart() > other.secondsStart())
        return false;
	if (secondsStart() == other.secondsStart()
     && nanoSecondsStart() < other.nanoSecondsStart())
        return true;
    return false;
}

void Event::decodeEvent() {

	_entropy = 1.0;

    nandIdString = "";
    if (eventType() == EVT_NAND_ID) {
        int i;
        for (i=0; i<evt.nand_id.size; i++) {
            if (i>0)
                nandIdString += " ";
            nandIdString += QString("%1").arg(evt.nand_id.id[i], 2, 16, QChar('0'));
        }
    }

    _netCmd = "";
    if (eventType() == EVT_NET_CMD) {
        _netCmd.append(evt.net_cmd.cmd[0]);
        _netCmd.append(evt.net_cmd.cmd[1]);
    }

	_nandReadColumnAddr = "";
    _nandReadRowAddr = "";
	if (eventType() == EVT_NAND_CHANGE_READ_COLUMN) {
        _nandReadColumnAddr = QString("%1 %2").arg(evt.nand_change_read_coumn.addr[1], 2, 16, QChar('0')).arg(evt.nand_change_read_coumn.addr[0], 2, 16, QChar('0'));
        _nandReadRowAddr = QString("%1 %2 %3").arg(evt.nand_change_read_coumn.addr[4], 2, 16, QChar('0')).arg(evt.nand_change_read_coumn.addr[3], 2, 16, QChar('0')).arg(evt.nand_change_read_coumn.addr[2], 2, 16, QChar('0'));
        _data.resize(_ntohl(evt.nand_change_read_coumn.count));
		memcpy(_data.data(), evt.nand_change_read_coumn.data, _ntohl(evt.nand_change_read_coumn.count));
		_entropy = getEntropy(_data);
	}

	if (eventType() == EVT_NAND_READ) {
        _nandReadColumnAddr = QString("%1 %2").arg(evt.nand_change_read_coumn.addr[1], 2, 16, QChar('0')).arg(evt.nand_change_read_coumn.addr[0], 2, 16, QChar('0'));
        _nandReadRowAddr = QString("%1 %2 %3").arg(evt.nand_change_read_coumn.addr[4], 2, 16, QChar('0')).arg(evt.nand_change_read_coumn.addr[3], 2, 16, QChar('0')).arg(evt.nand_change_read_coumn.addr[2], 2, 16, QChar('0'));
        _data.resize(_ntohl(evt.nand_read.count));
		memcpy(_data.data(), evt.nand_read.data, _ntohl(evt.nand_read.count));
		_entropy = getEntropy(_data);
	}

	if (eventType() == EVT_NAND_DATA) {
		_nandReadColumnAddr = QString("%1 %2").arg(evt.nand_data.addr[1], 2, 16, QChar('0')).arg(evt.nand_change_read_coumn.addr[0], 2, 16, QChar('0'));
		_nandReadRowAddr = QString("%1 %2 %3").arg(evt.nand_data.addr[4], 2, 16, QChar('0')).arg(evt.nand_change_read_coumn.addr[3], 2, 16, QChar('0')).arg(evt.nand_change_read_coumn.addr[2], 2, 16, QChar('0'));
		_data.resize(_ntohl(evt.nand_data.count));
		memcpy(_data.data(), evt.nand_data.data, _ntohl(evt.nand_data.count));
		_entropy = getEntropy(_data);
	}

	if (eventType() == EVT_NAND_PARAMETER_READ) {
		_data.resize(_ntohs(evt.nand_parameter_read.count));
		memcpy(_data.data(), evt.nand_parameter_read.data, _ntohs(evt.nand_parameter_read.count));
		_entropy = getEntropy(_data);
	}

    _sandiskChargeAddr = "";
	if (eventType() == EVT_NAND_SANDISK_CHARGE1) {
        _sandiskChargeAddr = QString("%1 %2 %3").arg(evt.nand_sandisk_charge1.addr[2], 2, 16, QChar('0')).arg(evt.nand_sandisk_charge1.addr[1], 2, 16, QChar('0')).arg(evt.nand_sandisk_charge1.addr[0], 2, 16, QChar('0'));
	}
	else if (eventType() == EVT_NAND_SANDISK_CHARGE2) {
		for (unsigned int i=0; i<sizeof(evt.nand_sandisk_charge2.addr); i++) {
			if (i>0)
				_sandiskChargeAddr += " ";
			_sandiskChargeAddr += QString("%1").arg(evt.nand_sandisk_charge2.addr[i], 2, 16, QChar('0'));
		}
	}

	_sdArgs = "";
	if (eventType() == EVT_SD_CMD) {
		_sdArgs = "";
        for (unsigned int i=0; i<_ntohl(evt.sd_cmd.num_args); i++) {
			if (i>0)
				_sdArgs += " ";
			_sdArgs += QString("%1").arg(evt.sd_cmd.args[i], 2, 16, QChar('0'));
		}
		_data.resize(_ntohl(evt.sd_cmd.num_results));
		memcpy(_data.data(), evt.sd_cmd.result, _ntohl(evt.sd_cmd.num_results));
		_entropy = getEntropy(_data);
	}

	_dataAsByteArray.setRawData((char *)&evt, _ntohl(evt.header.size));
}

uint32_t Event::nanoSecondsStart() const {
	return _ntohl(evt.header.nsec_start);
}

uint32_t Event::secondsStart() const {
	return _ntohl(evt.header.sec_start);
}

uint32_t Event::nanoSecondsEnd() const {
	return _ntohl(evt.header.nsec_end);
}

uint32_t Event::secondsEnd() const {
	return _ntohl(evt.header.sec_end);
}

uint32_t Event::eventSize() const {
	return _ntohl(evt.header.size);
}

enum evt_type Event::eventType() const {
    return (enum evt_type)evt.header.type;
}

const QString &Event::eventTypeStr() const {
	if (evt.header.type > EventTypes().count())
		return EventTypes()[0];
    return EventTypes()[evt.header.type];
}

const QString &Event::netCmd() const {
    return _netCmd;
}

uint32_t Event::netArg() const {
    return evt.net_cmd.arg;
}

int Event::setIndex(int index)
{
    int o = index;
	this->eventIndex = index;
    return o;
}

int Event::index()
{
	return eventIndex;
}

uint8_t Event::helloVersion() const
{
    return evt.hello.version;
}

uint8_t Event::nandIdAddr() const
{
	return evt.nand_id.addr;
}

const QString &Event::nandIdValue() const
{
    return nandIdString;
}

const QByteArray &Event::data() const
{
	return _data;
}

int Event::rawPacketSize() const
{
	return _dataAsByteArray.size();
}

const QByteArray &Event::rawPacket() const
{
	return _dataAsByteArray;
}

uint8_t Event::nandSakdiskParamAddr() const
{
	return evt.nand_unk_sandisk_param.addr;
}

uint8_t Event::nandSandiskParamData() const
{
	return evt.nand_unk_sandisk_param.data;
}

uint8_t Event::nandStatus() const
{
	return evt.nand_status.status;
}

uint8_t Event::nandParameterAddr() const
{
	return evt.nand_parameter_read.addr;
}

const QString &Event::nandSandiskChargeAddr() const
{
	return _sandiskChargeAddr;
}

const QString &Event::nandReadColumnAddr() const
{
    return _nandReadColumnAddr;
}

const QString &Event::nandReadRowAddr() const
{
    return _nandReadRowAddr;
}

uint8_t Event::sdCmdCMD() const
{
	return evt.sd_cmd.cmd & 0x3f;
}

bool Event::sdCmdIsACMD() const
{
	return evt.sd_cmd.cmd&0x80;
}

const QString &Event::sdCmdArgs() const
{
	return _sdArgs;
}

uint8_t Event::nandUnknownControl() const
{
	return evt.nand_unk.ctrl;
}

uint8_t Event::nandUnknownData() const
{
	return evt.nand_unk.data;
}

uint16_t Event::nandUnknownPins() const
{
	return evt.nand_unk.unknown;
}

qreal Event::entropy() const
{
	return _entropy;
}

QDebug operator<<(QDebug dbg, const Event &e)
{
	QString timestamp;
	QString message;
	timestamp.sprintf("<%d.%09d - %d.%09d> - ", (int)e.secondsStart(), (int)e.nanoSecondsStart(),
            e.secondsEnd(), e.nanoSecondsEnd());
	message = e.eventTypeStr() + " " + timestamp;

	switch(e.eventType()) {
		case EVT_HELLO:
			QTextStream(&message) << "Hello (v" << e.helloVersion() << ")";
			break;

            /*
		case PACKET_SD_CMD_ARG:
			if (p.sdRegister() == 0) {
				QTextStream(&message) << "CMD" << (p.sdValue()&0x3f);
			}
			else {
				QTextStream(&message) << "R" << p.sdRegister() <<"=" << p.sdValue();
			}
			break;

		case PACKET_COMMAND:
			if (p.commandState() == CMD_START)
				QTextStream(&message) << "Start " << p.commandName() << "(" << p.commandArg() << ")";
			else if (p.commandState() == CMD_STOP)
				QTextStream(&message) << "Finish " << p.commandName() << "(" << p.commandArg() << ")";
			else
				QTextStream(&message) << "Unknown state for " << p.commandName() << "(" << p.commandArg() << ")";
			break;

		case PACKET_SD_RESPONSE:
			QTextStream(&message) << "First byte of card response: " << QString::number(p.sdResponse(), 16);
			break;

		case PACKET_NAND_CYCLE: {
                QString ctrlString;
                uint8_t ctrl = p.nandControl();
                QTextStream(&message)
                    << QString("%1").arg(p.nandData(), 2, 16, QChar('0'))
                    << " " << (ctrl&NAND_ALE?"A":" ")
                    << " " << (ctrl&NAND_CLE?"C":" ")
                    << " " << (ctrl&NAND_WE?"W":" ")
                    << " " << (ctrl&NAND_RE?"R":" ")
                    << " " << (ctrl&NAND_CS?"S":" ")
                    << " " << (ctrl&NAND_RB?"B":" ")
                    << " (" << QString("%1").arg(p.nandUnknown(), 4, 16, QChar('0')) << ")";
            }
			break;

		case PACKET_SD_DATA:
			QTextStream(&message) << "512 bytes of data";
			break;

		case PACKET_ERROR:
			QTextStream(&message) << "Subsystem " << p.errorSubsystem() << ", Code " << p.errorCode() << ", Arg " << p.errorArgument() << ", Message: " << p.errorStr();
			break;

		case PACKET_RESET:
			QTextStream(&message) << "Reset (version v" << p.resetVersion() << ")";
			break;

		case PACKET_BUFFER_DRAIN:
			if (p.bufferDrainEvent() == PKT_BUFFER_DRAIN_START)
				QTextStream(&message) << "Buffer started draining";
			else if (p.bufferDrainEvent() == PKT_BUFFER_DRAIN_STOP)
				QTextStream(&message) << "Buffer finished draining";
			else
				QTextStream(&message) << "Buffer drain did something unknown: " << p.bufferDrainEvent();
			break;

		case PACKET_SD_CID:
			QTextStream(&message) << "Card CID: " << p.cid();
			break;

		case PACKET_SD_CSD:
			QTextStream(&message) << "Card CSD: " << p.csd();
			break;
            */
        case EVT_NAND_ID:
			QTextStream(&message) << "NAND ID: " << e.nandIdValue();
            break;

        case EVT_SD_CMD:
            QTextStream(&message) << "SD CMD";
            break;

        case EVT_NET_CMD:
            QTextStream(&message) << "Net command '" << e.netCmd() << " " << e.netArg() << "'";
            break;

		case EVT_UNKNOWN:
		default:
			QTextStream(&message) << "Unknown event: " << e.eventTypeStr();
			break;
	}

	dbg.nospace() << message;
	return dbg.space();
}

qint64 Event::write(QIODevice &device) {
	return device.write((const char *)&evt, eventSize());
}
