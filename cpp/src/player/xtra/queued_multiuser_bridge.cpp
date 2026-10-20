#include "libreshockwave/player/xtra/queued_multiuser_bridge.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace libreshockwave::player::xtra {
namespace {

constexpr int smus_mode = 0;
constexpr int smus_header_size = 6;

constexpr std::uint8_t smus_logon_frame[] = {
    114, 0, 0, 0, 0, 74, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 5, 76, 111, 103, 111, 110, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 6, 83, 121, 115, 116, 101, 109, 140,
    176, 97, 202, 17, 83, 160, 87, 248, 108, 216, 205,
    75, 46, 248, 42, 99, 218, 105, 109, 96, 31, 47, 89,
    198, 167, 52, 63, 226, 246, 113, 57, 56, 69, 194, 54, 206, 60
};

std::vector<std::uint8_t> bytesFromString(std::string_view value) {
    return {value.begin(), value.end()};
}

std::string stringFromBytes(const std::vector<std::uint8_t>& bytes) {
    return {bytes.begin(), bytes.end()};
}

std::string safeDatumString(const lingo::Datum& value) {
    try {
        return value.stringValue();
    } catch (...) {
        return value.typeString();
    }
}

std::vector<std::string> recipientsFromDatum(const lingo::Datum& value) {
    std::vector<std::string> result;
    if (value.isVoid()) {
        return result;
    }
    if (value.isList()) {
        const auto& items = value.listValue().items();
        result.reserve(items.size());
        for (const auto& item : items) {
            result.push_back(safeDatumString(item));
        }
        return result;
    }
    result.push_back(safeDatumString(value));
    return result;
}

class ByteWriter {
public:
    void writeByte(int value) {
        data_.push_back(static_cast<std::uint8_t>(value & 0xFF));
    }

    void writeShort(int value) {
        writeByte(value >> 8);
        writeByte(value);
    }

    void writeInt(int value) {
        writeByte(value >> 24);
        writeByte(value >> 16);
        writeByte(value >> 8);
        writeByte(value);
    }

    void writeDouble(double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        writeInt(static_cast<int>(bits >> 32U));
        writeInt(static_cast<int>(bits));
    }

    void writeChunk(std::string_view value) {
        writeChunk(bytesFromString(value));
    }

    void writeChunk(const std::vector<std::uint8_t>& bytes) {
        writeInt(static_cast<int>(bytes.size()));
        writeBytes(bytes);
        if ((bytes.size() & 1U) != 0U) {
            writeByte(0);
        }
    }

    void writeBytes(const std::vector<std::uint8_t>& bytes) {
        data_.insert(data_.end(), bytes.begin(), bytes.end());
    }

    [[nodiscard]] std::vector<std::uint8_t> toByteArray() const {
        return data_;
    }

private:
    std::vector<std::uint8_t> data_;
};

class ByteReader {
public:
    ByteReader(const std::vector<std::uint8_t>& data, int start, int end)
        : data_(data),
          pos_(start),
          end_(std::clamp(end, 0, static_cast<int>(data.size()))) {
        if (start < 0 || start > end_) {
            throw std::out_of_range("Invalid byte reader range");
        }
    }

    [[nodiscard]] int readUnsignedByte() {
        require(1);
        return data_[static_cast<std::size_t>(pos_++)] & 0xFF;
    }

    [[nodiscard]] int readUnsignedShort() {
        return (readUnsignedByte() << 8) | readUnsignedByte();
    }

    [[nodiscard]] int readInt() {
        require(4);
        const int value = (readUnsignedByte() << 24) |
                          (readUnsignedByte() << 16) |
                          (readUnsignedByte() << 8) |
                          readUnsignedByte();
        return value;
    }

    [[nodiscard]] double readDouble() {
        const auto high = static_cast<std::uint64_t>(static_cast<std::uint32_t>(readInt()));
        const auto low = static_cast<std::uint64_t>(static_cast<std::uint32_t>(readInt()));
        const std::uint64_t bits = (high << 32U) | low;
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    [[nodiscard]] std::string readChunkString() {
        return stringFromBytes(readChunkBytes());
    }

    [[nodiscard]] std::vector<std::uint8_t> readChunkBytes() {
        const int length = readInt();
        if (length < 0) {
            throw std::out_of_range("Negative chunk length");
        }
        require(length);
        const auto begin = data_.begin() + pos_;
        const auto end = begin + length;
        std::vector<std::uint8_t> result(begin, end);
        pos_ += length;
        if ((length & 1) != 0) {
            require(1);
            ++pos_;
        }
        return result;
    }

    [[nodiscard]] std::vector<std::uint8_t> readRemaining() {
        const auto begin = data_.begin() + pos_;
        const auto end = data_.begin() + end_;
        std::vector<std::uint8_t> result(begin, end);
        pos_ = end_;
        return result;
    }

    [[nodiscard]] std::vector<std::uint8_t> readRemainingWithPrefix(int type) {
        auto remaining = readRemaining();
        std::vector<std::uint8_t> result;
        result.reserve(remaining.size() + 2U);
        result.push_back(static_cast<std::uint8_t>((type >> 8) & 0xFF));
        result.push_back(static_cast<std::uint8_t>(type & 0xFF));
        result.insert(result.end(), remaining.begin(), remaining.end());
        return result;
    }

private:
    void require(int byte_count) const {
        if (byte_count < 0 || pos_ + byte_count > end_) {
            throw std::out_of_range("SMUS frame truncated");
        }
    }

    const std::vector<std::uint8_t>& data_;
    int pos_;
    int end_;
};

void writeLingoValue(ByteWriter& out, const lingo::Datum& value);

void writePropListKey(ByteWriter& out, const lingo::Datum& key) {
    if (const auto* symbol = key.asSymbol()) {
        out.writeShort(2);
        out.writeChunk(symbol->name);
        return;
    }
    out.writeShort(3);
    out.writeChunk(safeDatumString(key));
}

void writeLingoValue(ByteWriter& out, const lingo::Datum& value) {
    if (value.isVoid()) {
        out.writeShort(0);
    } else if (const auto* integer = value.asInt()) {
        out.writeShort(1);
        out.writeInt(integer->value);
    } else if (const auto* symbol = value.asSymbol()) {
        out.writeShort(2);
        out.writeChunk(symbol->name);
    } else if (value.isString()) {
        out.writeShort(3);
        out.writeChunk(value.stringValue());
    } else if (const auto* media = value.asMedia()) {
        out.writeShort(5);
        out.writeChunk(media->bytes);
    } else if (const auto* floating = value.asFloat()) {
        out.writeShort(6);
        out.writeDouble(floating->value);
    } else if (value.isList()) {
        const auto& items = value.listValue().items();
        out.writeShort(7);
        out.writeInt(static_cast<int>(items.size()));
        for (const auto& item : items) {
            writeLingoValue(out, item);
        }
    } else if (const auto* point = value.asIntPoint()) {
        out.writeShort(8);
        writeLingoValue(out, lingo::Datum::of(point->x));
        writeLingoValue(out, lingo::Datum::of(point->y));
    } else if (const auto* rect = value.asIntRect()) {
        out.writeShort(9);
        writeLingoValue(out, lingo::Datum::of(rect->left));
        writeLingoValue(out, lingo::Datum::of(rect->top));
        writeLingoValue(out, lingo::Datum::of(rect->right));
        writeLingoValue(out, lingo::Datum::of(rect->bottom));
    } else if (value.isPropList()) {
        const auto& properties = value.propListValue().properties();
        out.writeShort(10);
        out.writeInt(static_cast<int>(properties.size()));
        for (const auto& entry : properties) {
            writePropListKey(out, entry.first);
            writeLingoValue(out, entry.second);
        }
    } else if (const auto* color = value.asColorRef()) {
        out.writeShort(18);
        out.writeByte(1);
        out.writeByte(color->r);
        out.writeByte(color->g);
        out.writeByte(color->b);
    } else {
        out.writeShort(3);
        out.writeChunk(safeDatumString(value));
    }
}

lingo::Datum readLingoValue(ByteReader& in) {
    const int type = in.readUnsignedShort();
    switch (type) {
        case 0:
            return lingo::Datum::voidValue();
        case 1:
            return lingo::Datum::of(in.readInt());
        case 2:
            return lingo::Datum::symbol(in.readChunkString());
        case 3:
            return lingo::Datum::of(in.readChunkString());
        case 5:
        case 20:
            return lingo::Datum::media(in.readChunkBytes());
        case 6:
            return lingo::Datum::of(static_cast<float>(in.readDouble()));
        case 7: {
            const int count = in.readInt();
            if (count < 0) {
                throw std::out_of_range("Negative list count");
            }
            std::vector<lingo::Datum> values;
            values.reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i) {
                values.push_back(readLingoValue(in));
            }
            return lingo::Datum::list(std::move(values));
        }
        case 8: {
            const int x = readLingoValue(in).intValue();
            const int y = readLingoValue(in).intValue();
            return lingo::Datum::intPoint(x, y);
        }
        case 9: {
            const int left = readLingoValue(in).intValue();
            const int top = readLingoValue(in).intValue();
            const int right = readLingoValue(in).intValue();
            const int bottom = readLingoValue(in).intValue();
            return lingo::Datum::intRect(left, top, right, bottom);
        }
        case 10: {
            const int count = in.readInt();
            if (count < 0) {
                throw std::out_of_range("Negative prop-list count");
            }
            auto result = lingo::Datum::propList();
            auto& props = result.propListValue();
            for (int i = 0; i < count; ++i) {
                auto key = readLingoValue(in);
                auto value = readLingoValue(in);
                props.appendProperty(std::move(key), std::move(value));
            }
            return result;
        }
        case 18: {
            (void)in.readUnsignedByte();
            const int r = in.readUnsignedByte();
            const int g = in.readUnsignedByte();
            const int b = in.readUnsignedByte();
            return lingo::Datum::colorRef(r, g, b);
        }
        default:
            return lingo::Datum::media(in.readRemainingWithPrefix(type));
    }
}

} // namespace

std::string QueuedMultiuserBridge::PendingRequest::wireContent() const {
    return serializeWireContent(subject, content);
}

std::vector<std::uint8_t> QueuedMultiuserBridge::PendingRequest::wireBytes() const {
    if (wire_bytes_override.has_value()) {
        return *wire_bytes_override;
    }
    return bytesFromString(wireContent());
}

void QueuedMultiuserBridge::requestConnect(int instance_id,
                                           const std::string& host,
                                           int port,
                                           int mode,
                                           const ConnectOptions& options) {
    modes_[instance_id] = mode;
    sender_i_ds_[instance_id] = options.user_name;
    PendingRequest request;
    request.type = REQ_CONNECT;
    request.instance_id = instance_id;
    request.host = host;
    request.port = port;
    pending_requests_.push_back(std::move(request));
}

void QueuedMultiuserBridge::requestSend(int instance_id,
                                        const lingo::Datum& recipients,
                                        const std::string& subject,
                                        const lingo::Datum& content) {
    const auto content_string = safeDatumString(content);
    const auto recipient_list = recipientsFromDatum(recipients);
    const std::string sender_id = senderForInstance(instance_id);

    PendingRequest request;
    request.type = REQ_SEND;
    request.instance_id = instance_id;
    request.sender_id = sender_id;
    request.recipients = recipient_list;
    request.subject = subject;
    request.content = content_string;
    if (isSmusInstance(instance_id)) {
        request.wire_bytes_override = packSmusMessage(sender_id, recipient_list, subject, content);
    }
    pending_requests_.push_back(std::move(request));
}

void QueuedMultiuserBridge::requestDisconnect(int instance_id) {
    PendingRequest request;
    request.type = REQ_DISCONNECT;
    request.instance_id = instance_id;
    pending_requests_.push_back(std::move(request));
    connected_.erase(instance_id);
    modes_.erase(instance_id);
    sender_i_ds_.erase(instance_id);
}

bool QueuedMultiuserBridge::isConnected(int instance_id) const {
    const auto found = connected_.find(instance_id);
    return found != connected_.end() && found->second;
}

std::vector<QueuedMultiuserBridge::NetMessage> QueuedMultiuserBridge::pollMessages(int instance_id) {
    auto found = message_queues_.find(instance_id);
    if (found == message_queues_.end()) {
        return {};
    }
    auto result = std::move(found->second);
    message_queues_.erase(found);
    return result;
}

void QueuedMultiuserBridge::destroyInstance(int instance_id) {
    connected_.erase(instance_id);
    message_queues_.erase(instance_id);
    modes_.erase(instance_id);
    sender_i_ds_.erase(instance_id);
}

const std::vector<QueuedMultiuserBridge::PendingRequest>& QueuedMultiuserBridge::pendingRequests() const {
    return pending_requests_;
}

const QueuedMultiuserBridge::PendingRequest* QueuedMultiuserBridge::getRequest(int index) const {
    if (index < 0 || index >= static_cast<int>(pending_requests_.size())) {
        return nullptr;
    }
    return &pending_requests_[static_cast<std::size_t>(index)];
}

void QueuedMultiuserBridge::drainPendingRequests() {
    pending_requests_.clear();
}

void QueuedMultiuserBridge::notifyConnected(int instance_id) {
    connected_[instance_id] = true;
    queueMessage(instance_id, NetMessage{0, "System", "ConnectToNetServer", lingo::Datum::of(std::string())});
    if (isSmusInstance(instance_id)) {
        PendingRequest request;
        request.type = REQ_SEND;
        request.instance_id = instance_id;
        request.sender_id = senderForInstance(instance_id);
        request.recipients = {"System"};
        request.subject = "Logon";
        request.wire_bytes_override = packSmusLogon();
        pending_requests_.push_back(std::move(request));
    }
}

void QueuedMultiuserBridge::notifyDisconnected(int instance_id) {
    connected_.erase(instance_id);
}

void QueuedMultiuserBridge::notifyError(int instance_id, int error_code) {
    queueMessage(instance_id, NetMessage{error_code, "System", "ConnectionProblem", lingo::Datum::of(std::string())});
}

void QueuedMultiuserBridge::deliverMessage(int instance_id,
                                           int error_code,
                                           std::string sender_id,
                                           std::string subject,
                                           std::string content) {
    if (isSmusInstance(instance_id)) {
        deliverSmusMessage(instance_id, bytesFromString(content));
        return;
    }
    queueMessage(instance_id, NetMessage{error_code, std::move(sender_id), std::move(subject), lingo::Datum::of(std::move(content))});
}

void QueuedMultiuserBridge::deliverMessageBytes(int instance_id, const std::vector<std::uint8_t>& data) {
    if (isSmusInstance(instance_id)) {
        deliverSmusMessage(instance_id, data);
        return;
    }
    deliverMessage(instance_id, 0, "", "", stringFromBytes(data));
}

std::string QueuedMultiuserBridge::serializeWireContent(std::string_view subject, std::string_view content) {
    if (subject.empty() || subject == "0") {
        return std::string(content);
    }
    if (content.empty()) {
        return std::string(subject);
    }
    return std::string(subject) + " " + std::string(content);
}

int QueuedMultiuserBridge::decodeShockwaveCommand(char high, char low) {
    return ((static_cast<unsigned char>(high) & 63) * 64) | (static_cast<unsigned char>(low) & 63);
}

bool QueuedMultiuserBridge::isLegacyPlaintextPong(std::string_view content) {
    return content == "@@BCD";
}

bool QueuedMultiuserBridge::isSmusInstance(int instance_id) const {
    const auto found = modes_.find(instance_id);
    return found != modes_.end() && found->second == smus_mode;
}

std::string QueuedMultiuserBridge::senderForInstance(int instance_id) const {
    const auto found = sender_i_ds_.find(instance_id);
    if (found == sender_i_ds_.end()) {
        return {};
    }
    return found->second;
}

std::vector<std::uint8_t> QueuedMultiuserBridge::packSmusLogon() const {
    return {std::begin(smus_logon_frame), std::end(smus_logon_frame)};
}

std::vector<std::uint8_t> QueuedMultiuserBridge::packSmusMessage(const std::string& sender_id,
                                                                 const std::vector<std::string>& recipients,
                                                                 const std::string& subject,
                                                                 const lingo::Datum& content) const {
    const std::string sender = sender_id.empty() ? "*" : sender_id;
    const std::vector<std::string> effective_recipients = recipients.empty() ? std::vector<std::string>{"System"} : recipients;
    return packSmusFrame(0, 0, subject, sender, effective_recipients, encodeLingoValue(content));
}

void QueuedMultiuserBridge::deliverSmusMessage(int instance_id, const std::vector<std::uint8_t>& data) {
    try {
        if (static_cast<int>(data.size()) < smus_header_size) {
            return;
        }
        const int body_length = (static_cast<int>(data[2]) << 24) |
                               (static_cast<int>(data[3]) << 16) |
                               (static_cast<int>(data[4]) << 8) |
                               static_cast<int>(data[5]);
        if (data[0] != 114 || data[1] != 0 || body_length < 0 ||
            body_length > static_cast<int>(data.size()) - smus_header_size) {
            return;
        }

        const auto message = unpackSmusBody(data, smus_header_size, body_length);
        if (message.subject == "Logon" || message.subject == "logon") {
            return;
        }
        queueMessage(instance_id, NetMessage{message.error_code,
                                            message.sender,
                                            message.subject,
                                            decodeLingoValue(message.content)});
    } catch (...) {
        queueMessage(instance_id, NetMessage{-5, "System", "ConnectionProblem", lingo::Datum::of(std::string())});
    }
}

void QueuedMultiuserBridge::queueMessage(int instance_id, NetMessage message) {
    message_queues_[instance_id].push_back(std::move(message));
}

std::vector<std::uint8_t> QueuedMultiuserBridge::packSmusFrame(int error_code,
                                                               int timestamp,
                                                               std::string_view subject,
                                                               std::string_view sender,
                                                               const std::vector<std::string>& recipients,
                                                               const std::vector<std::uint8_t>& content) {
    ByteWriter body;
    body.writeInt(error_code);
    body.writeInt(timestamp);
    body.writeChunk(subject);
    body.writeChunk(sender);
    body.writeInt(static_cast<int>(recipients.size()));
    for (const auto& recipient : recipients) {
        body.writeChunk(recipient);
    }
    body.writeBytes(content);

    const auto body_bytes = body.toByteArray();
    ByteWriter frame;
    frame.writeByte(114);
    frame.writeByte(0);
    frame.writeInt(static_cast<int>(body_bytes.size()));
    frame.writeBytes(body_bytes);
    return frame.toByteArray();
}

QueuedMultiuserBridge::SmusFrame QueuedMultiuserBridge::unpackSmusBody(const std::vector<std::uint8_t>& data,
                                                                       int offset,
                                                                       int length) {
    ByteReader in(data, offset, offset + length);
    SmusFrame result;
    result.error_code = in.readInt();
    (void)in.readInt();
    result.subject = in.readChunkString();
    result.sender = in.readChunkString();
    const int recipient_count = in.readInt();
    if (recipient_count < 0) {
        throw std::out_of_range("Negative recipient count");
    }
    for (int i = 0; i < recipient_count; ++i) {
        (void)in.readChunkString();
    }
    result.content = in.readRemaining();
    return result;
}

std::vector<std::uint8_t> QueuedMultiuserBridge::encodeLingoValue(const lingo::Datum& value) {
    ByteWriter out;
    writeLingoValue(out, value);
    return out.toByteArray();
}

lingo::Datum QueuedMultiuserBridge::decodeLingoValue(const std::vector<std::uint8_t>& bytes) {
    ByteReader in(bytes, 0, static_cast<int>(bytes.size()));
    return readLingoValue(in);
}

} // namespace libreshockwave::player::xtra
