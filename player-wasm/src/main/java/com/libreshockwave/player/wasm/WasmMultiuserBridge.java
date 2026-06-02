package com.libreshockwave.player.wasm;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.BitmapDecoder;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.cast.BitmapInfo;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.xtra.MultiuserNetBridge;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Queue-based MultiuserNetBridge for WASM.
 * Java queues requests (connect/send/disconnect); JS polls them each tick.
 * JS delivers events (connected/message/disconnected) back via WasmEntry exports.
 */
public class WasmMultiuserBridge implements MultiuserNetBridge {

    // --- Pending requests (Java → JS) ---

    static final int REQ_CONNECT    = 0;
    static final int REQ_SEND       = 1;
    static final int REQ_DISCONNECT = 2;

    static class PendingRequest {
        final int type;
        final int instanceId;
        String host;
        int port;
        String senderID;
        String subject;
        String content;
        byte[] wireBytes;

        PendingRequest(int type, int instanceId) {
            this.type = type;
            this.instanceId = instanceId;
        }

        String wireContent() {
            return serializeWireContent(subject, content);
        }

        byte[] wireBytes() {
            if (wireBytes != null) {
                return wireBytes;
            }
            return wireContent().getBytes(StandardCharsets.ISO_8859_1);
        }
    }

    private final List<PendingRequest> pendingRequests = new ArrayList<>();
    private final Map<Integer, Boolean> connectedMap = new HashMap<>();
    private final Map<Integer, List<NetMessage>> messageQueues = new HashMap<>();
    private final Map<Integer, Integer> modeMap = new HashMap<>();
    // Legacy keepalives can arrive while authored Lingo is still finishing crypto setup.
    // Echoing that plaintext PONG after the server has enabled crypto closes the socket.
    private final Map<Integer, Boolean> suppressNextPlaintextPong = new HashMap<>();
    private static final int LEGACY_PONG_COMMAND = 196;
    private static final int SMUS_MODE = 0;
    private static final int SMUS_HEADER_SIZE = 6;
    private static final byte[] SMUS_LOGON_FRAME = new byte[] {
            114, 0, 0, 0, 0, 74, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 5, 76, 111, 103, 111, 110, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 0, 0, 0, 6, 83, 121, 115, 116, 101, 109, (byte) 140,
            (byte) 176, 97, (byte) 202, 17, 83, (byte) 160, 87, (byte) 248,
            108, (byte) 216, (byte) 205, 75, 46, (byte) 248, 42, 99, (byte) 218,
            105, 109, 96, 31, 47, 89, (byte) 198, (byte) 167, 52, 63,
            (byte) 226, (byte) 246, 113, 57, 56, 69, (byte) 194, 54, (byte) 206, 60
    };

    // --- MultiuserNetBridge implementation ---

    @Override
    public void requestConnect(int instanceId, String host, int port, int mode) {
        modeMap.put(instanceId, mode);
        PendingRequest req = new PendingRequest(REQ_CONNECT, instanceId);
        req.host = host;
        req.port = port;
        pendingRequests.add(req);
    }

    @Override
    public void requestSend(int instanceId, String senderID, String subject, Datum content) {
        String contentString = content.toStr();
        if (shouldSuppressPlaintextPong(instanceId, contentString)) {
            return;
        }

        PendingRequest req = new PendingRequest(REQ_SEND, instanceId);
        req.senderID = senderID;
        req.subject = subject;
        req.content = contentString;
        if (isSmusInstance(instanceId)) {
            req.wireBytes = packSmusMessage(senderID, subject, content);
        }
        pendingRequests.add(req);
    }

    @Override
    public void requestDisconnect(int instanceId) {
        PendingRequest req = new PendingRequest(REQ_DISCONNECT, instanceId);
        pendingRequests.add(req);
        connectedMap.remove(instanceId);
        modeMap.remove(instanceId);
        suppressNextPlaintextPong.remove(instanceId);
    }

    @Override
    public boolean isConnected(int instanceId) {
        return connectedMap.getOrDefault(instanceId, false);
    }

    @Override
    public List<NetMessage> pollMessages(int instanceId) {
        List<NetMessage> queue = messageQueues.remove(instanceId);
        return queue != null ? queue : List.of();
    }

    @Override
    public void destroyInstance(int instanceId) {
        connectedMap.remove(instanceId);
        messageQueues.remove(instanceId);
        modeMap.remove(instanceId);
        suppressNextPlaintextPong.remove(instanceId);
    }

    // --- JS polling API ---

    List<PendingRequest> getPendingRequests() {
        return pendingRequests;
    }

    PendingRequest getRequest(int index) {
        return index >= 0 && index < pendingRequests.size() ? pendingRequests.get(index) : null;
    }

    void drainPendingRequests() {
        pendingRequests.clear();
    }

    // --- JS delivery API ---

    void notifyConnected(int instanceId) {
        connectedMap.put(instanceId, true);
        // Director's Multiuser Xtra reports a successful connection with this
        // exact system message. Habbo r31 waits for it before sending SSO.
        queueMessage(instanceId, new NetMessage(0, "System", "ConnectToNetServer", new Datum.Str("")));
        if (isSmusInstance(instanceId)) {
            PendingRequest req = new PendingRequest(REQ_SEND, instanceId);
            req.subject = "Logon";
            req.wireBytes = packSmusLogon();
            pendingRequests.add(req);
        }
    }

    void notifyDisconnected(int instanceId) {
        connectedMap.remove(instanceId);
        suppressNextPlaintextPong.remove(instanceId);
    }

    void notifyError(int instanceId, int errorCode) {
        queueMessage(instanceId, new NetMessage(errorCode, "System", "ConnectionProblem", new Datum.Str("")));
    }

    void deliverMessage(int instanceId, int errorCode, String senderID, String subject, String content) {
        if (isSmusInstance(instanceId)) {
            deliverSmusMessage(instanceId, content);
            return;
        }
        if (isLegacyPlaintextKeepalive(content)) {
            suppressNextPlaintextPong.put(instanceId, true);
        }
        queueMessage(instanceId, new NetMessage(errorCode, senderID, subject, new Datum.Str(content)));
    }

    void deliverMessageBytes(int instanceId, byte[] data) {
        if (isSmusInstance(instanceId)) {
            deliverSmusMessage(instanceId, data);
            return;
        }
        deliverMessage(instanceId, 0, "", "", new String(data, StandardCharsets.ISO_8859_1));
    }

    private void queueMessage(int instanceId, NetMessage msg) {
        messageQueues.computeIfAbsent(instanceId, k -> new ArrayList<>()).add(msg);
    }

    private boolean isSmusInstance(int instanceId) {
        return modeMap.getOrDefault(instanceId, 1) == SMUS_MODE;
    }

    private byte[] packSmusLogon() {
        return SMUS_LOGON_FRAME.clone();
    }

    private byte[] packSmusMessage(String senderID, String subject, Datum content) {
        String sender = senderID != null && !senderID.isEmpty() ? senderID : "*";
        String normalizedSubject = subject != null ? subject : "";
        byte[] encodedContent = encodeLingoValue(content);
        return packSmusFrame(0, 0, normalizedSubject, sender, List.of("System"), encodedContent);
    }

    private void deliverSmusMessage(int instanceId, String data) {
        deliverSmusMessage(instanceId, data.getBytes(StandardCharsets.ISO_8859_1));
    }

    private void deliverSmusMessage(int instanceId, byte[] data) {
        try {
            if (data.length < SMUS_HEADER_SIZE) {
                WasmEntry.log("[MultiuserXtra] short SMUS frame instance=" + instanceId + " bytes=" + data.length);
                return;
            }
            int bodyLength = readInt(data, 2);
            if ((data[0] & 0xFF) != 114 || data[1] != 0
                    || bodyLength < 0 || bodyLength > data.length - SMUS_HEADER_SIZE) {
                WasmEntry.log("[MultiuserXtra] incomplete SMUS frame instance=" + instanceId
                        + " body=" + bodyLength + " bytes=" + data.length);
                return;
            }
            SmusFrame message = unpackSmusBody(data, SMUS_HEADER_SIZE, bodyLength);
            Datum content = decodeLingoValue(message.content);
            WasmEntry.log("[MultiuserXtra] SMUS recv instance=" + instanceId
                    + " subject=" + message.subject
                    + " sender=" + message.sender
                    + " contentType=" + content.getClass().getSimpleName());
            if ("Logon".equalsIgnoreCase(message.subject)) {
                return;
            }
            queueMessage(instanceId, new NetMessage(message.errorCode, message.sender, message.subject, content));
        } catch (Throwable e) {
            WasmEntry.log("[MultiuserXtra] SMUS decode failed instance=" + instanceId + ": " + e);
            queueMessage(instanceId, new NetMessage(-5, "System", "ConnectionProblem", new Datum.Str("")));
        }
    }

    private static byte[] packSmusFrame(int errorCode, int timestamp, String subject,
                                        String sender, List<String> recipients, byte[] content) {
        ByteWriter body = new ByteWriter();
        body.writeInt(errorCode);
        body.writeInt(timestamp);
        body.writeChunk(subject);
        body.writeChunk(sender);
        body.writeInt(recipients.size());
        for (String recipient : recipients) {
            body.writeChunk(recipient);
        }
        body.writeBytes(content);

        byte[] bodyBytes = body.toByteArray();
        ByteWriter frame = new ByteWriter();
        frame.writeByte(114);
        frame.writeByte(0);
        frame.writeInt(bodyBytes.length);
        frame.writeBytes(bodyBytes);
        return frame.toByteArray();
    }

    private static SmusFrame unpackSmusBody(byte[] data, int offset, int length) {
        ByteReader in = new ByteReader(data, offset, offset + length);
        int errorCode = in.readInt();
        in.readInt(); // timestamp
        String subject = in.readChunkString();
        String sender = in.readChunkString();
        int recipientCount = in.readInt();
        for (int i = 0; i < recipientCount; i++) {
            in.readChunkString();
        }
        return new SmusFrame(errorCode, subject, sender, in.readRemaining());
    }

    private static byte[] encodeLingoValue(Datum value) {
        ByteWriter out = new ByteWriter();
        writeLingoValue(out, value);
        return out.toByteArray();
    }

    private static void writeLingoValue(ByteWriter out, Datum value) {
        if (value == null || value.isVoid()) {
            out.writeShort(0);
        } else if (value instanceof Datum.Int i) {
            out.writeShort(1);
            out.writeInt(i.value());
        } else if (value instanceof Datum.Symbol s) {
            out.writeShort(2);
            out.writeChunk(s.name());
        } else if (value instanceof Datum.Str s) {
            out.writeShort(3);
            out.writeChunk(s.value());
        } else if (value instanceof Datum.Media media) {
            out.writeShort(20);
            out.writeChunk(media.bytes());
        } else if (value instanceof Datum.Float f) {
            out.writeShort(6);
            out.writeDouble(f.value());
        } else if (value instanceof Datum.List l) {
            out.writeShort(7);
            out.writeInt(l.items().size());
            for (Datum item : l.items()) {
                writeLingoValue(out, item);
            }
        } else if (value instanceof Datum.Point p) {
            out.writeShort(8);
            writeLingoValue(out, Datum.of(p.x()));
            writeLingoValue(out, Datum.of(p.y()));
        } else if (value instanceof Datum.Rect r) {
            out.writeShort(9);
            writeLingoValue(out, Datum.of(r.left()));
            writeLingoValue(out, Datum.of(r.top()));
            writeLingoValue(out, Datum.of(r.right()));
            writeLingoValue(out, Datum.of(r.bottom()));
        } else if (value instanceof Datum.PropList pl) {
            out.writeShort(10);
            out.writeInt(pl.entries().size());
            for (Datum.PropEntry entry : pl.entries()) {
                out.writeShort(2);
                out.writeChunk(entry.key());
                writeLingoValue(out, entry.value());
            }
        } else if (value instanceof Datum.Color c) {
            out.writeShort(18);
            out.writeByte(1);
            out.writeByte(c.r());
            out.writeByte(c.g());
            out.writeByte(c.b());
        } else {
            out.writeShort(3);
            out.writeChunk(value.toStr());
        }
    }

    private static Datum decodeLingoValue(byte[] bytes) {
        return readLingoValue(new ByteReader(bytes, 0, bytes.length));
    }

    private static Datum readLingoValue(ByteReader in) {
        int type = in.readUnsignedShort();
        return switch (type) {
            case 0 -> Datum.VOID;
            case 1 -> Datum.of(in.readInt());
            case 2 -> Datum.symbol(in.readChunkString());
            case 3 -> Datum.of(in.readChunkString());
            case 5, 20 -> {
                byte[] media = in.readChunkBytes();
                WasmEntry.log("[MultiuserXtra] decoded media type=" + type
                        + " bytes=" + media.length
                        + " preview=" + previewBytes(media, 16));
                yield new Datum.Media(media);
            }
            case 6 -> Datum.of(in.readDouble());
            case 7 -> {
                int count = in.readInt();
                List<Datum> values = new ArrayList<>(count);
                for (int i = 0; i < count; i++) {
                    values.add(readLingoValue(in));
                }
                yield new Datum.List(values);
            }
            case 8 -> new Datum.Point(readLingoValue(in).toInt(), readLingoValue(in).toInt());
            case 9 -> new Datum.Rect(readLingoValue(in).toInt(), readLingoValue(in).toInt(),
                    readLingoValue(in).toInt(), readLingoValue(in).toInt());
            case 10 -> {
                int count = in.readInt();
                Datum.PropList pl = new Datum.PropList();
                for (int i = 0; i < count; i++) {
                    Datum key = readLingoValue(in);
                    String keyName = key instanceof Datum.Symbol s ? s.name() : key.toStr();
                    pl.add(keyName, readLingoValue(in), key instanceof Datum.Symbol);
                }
                maybeLogPhotoPropList(pl);
                yield pl;
            }
            case 18 -> {
                in.readUnsignedByte();
                yield new Datum.Color(in.readUnsignedByte(), in.readUnsignedByte(), in.readUnsignedByte());
            }
            default -> new Datum.Media(in.readRemainingWithPrefix(type));
        };
    }

    private static int readInt(byte[] data, int offset) {
        return ((data[offset] & 0xFF) << 24)
                | ((data[offset + 1] & 0xFF) << 16)
                | ((data[offset + 2] & 0xFF) << 8)
                | (data[offset + 3] & 0xFF);
    }

    private static String previewBytes(byte[] data, int limit) {
        StringBuilder sb = new StringBuilder();
        int count = Math.min(data.length, limit);
        for (int i = 0; i < count; i++) {
            if (i > 0) {
                sb.append(' ');
            }
            int b = data[i] & 0xFF;
            if (b < 16) {
                sb.append('0');
            }
            sb.append(Integer.toHexString(b));
        }
        return sb.toString();
    }

    private static void maybeLogPhotoPropList(Datum.PropList pl) {
        Datum image = pl.get("image");
        Datum cs = pl.get("cs");
        if (!(image instanceof Datum.Media media) || cs == null || cs.isVoid()) {
            return;
        }
        PhotoMediaInfo info = inspectPhotoMedia(media.bytes());
        WasmEntry.log("[MultiuserXtra] BINARYDATA image bytes=" + media.bytes().length
                + " serverCs=" + cs.toInt()
                + " localCs=" + (info != null ? info.checksum : -1)
                + " size=" + (info != null ? info.width + "x" + info.height : "unknown")
                + " bitDepth=" + (info != null ? info.bitDepth : -1)
                + " pitch=" + (info != null ? info.pitch : -1));
    }

    private static PhotoMediaInfo inspectPhotoMedia(byte[] data) {
        int marker = findBitdMarker(data);
        if (marker < 32 || marker + 8 > data.length) {
            return null;
        }
        int bitdLen = readIntLE(data, marker + 4);
        int bitdStart = marker + 8;
        if (bitdLen <= 0 || bitdStart + bitdLen > data.length) {
            return null;
        }
        BitmapInfo info = BitmapInfo.parse(Arrays.copyOfRange(data, marker - 32, marker), 1200);
        byte[] bitd = Arrays.copyOfRange(data, bitdStart, bitdStart + bitdLen);
        Bitmap bitmap = BitmapDecoder.decode(bitd, info.width(), info.height(), info.bitDepth(),
                Palette.SYSTEM_MAC_PALETTE, true, 1200, info.pitch());
        return new PhotoMediaInfo(info.width(), info.height(), info.bitDepth(), info.pitch(),
                photoChecksum(bitmap));
    }

    private static int photoChecksum(Bitmap bitmap) {
        int[] factors = {3, 2, 73, 28, 83, 21, 43, 90, 92, 91, 37, 4, 3, 84,
                12, 102, 103, 108, 97, 43, 44, 89, 109, 65, 61, -4, 76};
        int acc = 0;
        int width = bitmap.getWidth();
        int height = bitmap.getHeight();
        for (int i = 1; i <= 100; i++) {
            Integer index = bitmap.getPaletteIndex(i % width, (i * i) % height);
            acc = (acc + ((index != null ? index : 0) * factors[i % factors.length])) % 85000;
        }
        return acc;
    }

    private static int findBitdMarker(byte[] data) {
        for (int i = 32; i <= data.length - 8; i++) {
            if (data[i] == 'D' && data[i + 1] == 'T' && data[i + 2] == 'I' && data[i + 3] == 'B') {
                return i;
            }
        }
        return -1;
    }

    private static int readIntLE(byte[] data, int offset) {
        return (data[offset] & 0xFF)
                | ((data[offset + 1] & 0xFF) << 8)
                | ((data[offset + 2] & 0xFF) << 16)
                | ((data[offset + 3] & 0xFF) << 24);
    }

    private record PhotoMediaInfo(int width, int height, int bitDepth, int pitch, int checksum) {}

    private record SmusFrame(int errorCode, String subject, String sender, byte[] content) {}

    private static final class ByteWriter {
        private byte[] data = new byte[128];
        private int size;

        void writeByte(int value) {
            ensure(size + 1);
            data[size++] = (byte) value;
        }

        void writeShort(int value) {
            writeByte(value >>> 8);
            writeByte(value);
        }

        void writeInt(int value) {
            writeByte(value >>> 24);
            writeByte(value >>> 16);
            writeByte(value >>> 8);
            writeByte(value);
        }

        void writeDouble(double value) {
            long bits = Double.doubleToRawLongBits(value);
            writeInt((int) (bits >>> 32));
            writeInt((int) bits);
        }

        void writeChunk(String value) {
            writeChunk((value != null ? value : "").getBytes(StandardCharsets.ISO_8859_1));
        }

        void writeChunk(byte[] bytes) {
            writeInt(bytes.length);
            writeBytes(bytes);
            if ((bytes.length & 1) == 1) {
                writeByte(0);
            }
        }

        void writeBytes(byte[] bytes) {
            ensure(size + bytes.length);
            System.arraycopy(bytes, 0, data, size, bytes.length);
            size += bytes.length;
        }

        byte[] toByteArray() {
            byte[] out = new byte[size];
            System.arraycopy(data, 0, out, 0, size);
            return out;
        }

        private void ensure(int required) {
            if (required <= data.length) {
                return;
            }
            int newLength = data.length;
            while (newLength < required) {
                newLength *= 2;
            }
            byte[] next = new byte[newLength];
            System.arraycopy(data, 0, next, 0, size);
            data = next;
        }
    }

    private static final class ByteReader {
        private final byte[] data;
        private final int end;
        private int pos;

        ByteReader(byte[] data, int start, int end) {
            this.data = data;
            this.pos = start;
            this.end = end;
        }

        int readUnsignedByte() {
            return data[pos++] & 0xFF;
        }

        int readUnsignedShort() {
            return (readUnsignedByte() << 8) | readUnsignedByte();
        }

        int readInt() {
            int value = WasmMultiuserBridge.readInt(data, pos);
            pos += 4;
            return value;
        }

        double readDouble() {
            long high = readInt() & 0xFFFFFFFFL;
            long low = readInt() & 0xFFFFFFFFL;
            return Double.longBitsToDouble((high << 32) | low);
        }

        String readChunkString() {
            return new String(readChunkBytes(), StandardCharsets.ISO_8859_1);
        }

        byte[] readChunkBytes() {
            int length = readInt();
            byte[] out = new byte[length];
            System.arraycopy(data, pos, out, 0, length);
            pos += length;
            if ((length & 1) == 1) {
                pos++;
            }
            return out;
        }

        byte[] readRemaining() {
            byte[] out = new byte[end - pos];
            System.arraycopy(data, pos, out, 0, out.length);
            pos = end;
            return out;
        }

        byte[] readRemainingWithPrefix(int type) {
            byte[] remaining = readRemaining();
            byte[] out = new byte[remaining.length + 2];
            out[0] = (byte) (type >>> 8);
            out[1] = (byte) type;
            System.arraycopy(remaining, 0, out, 2, remaining.length);
            return out;
        }
    }

    private boolean shouldSuppressPlaintextPong(int instanceId, String content) {
        if (!Boolean.TRUE.equals(suppressNextPlaintextPong.get(instanceId))) {
            return false;
        }
        if (!isLegacyPlaintextPong(content)) {
            return false;
        }
        suppressNextPlaintextPong.remove(instanceId);
        return true;
    }

    private static boolean isLegacyPlaintextKeepalive(String content) {
        return content != null
                && content.length() == 3
                && content.charAt(0) == '@'
                && content.charAt(1) == 'r'
                && content.charAt(2) == 1;
    }

    private static boolean isLegacyPlaintextPong(String content) {
        return content != null
                && content.length() == 5
                && content.charAt(0) == '@'
                && content.charAt(1) == '@'
                && content.charAt(2) == 'B'
                && decodeShockwaveCommand(content.charAt(3), content.charAt(4)) == LEGACY_PONG_COMMAND;
    }

    private static int decodeShockwaveCommand(char high, char low) {
        return ((high & 63) * 64) | (low & 63);
    }

    static String serializeWireContent(String subject, String content) {
        String normalizedSubject = subject != null ? subject : "";
        String normalizedContent = content != null ? content : "";
        if (normalizedSubject.isEmpty() || "0".equals(normalizedSubject)) {
            return normalizedContent;
        }
        if (normalizedContent.isEmpty()) {
            return normalizedSubject;
        }
        return normalizedSubject + " " + normalizedContent;
    }
}
