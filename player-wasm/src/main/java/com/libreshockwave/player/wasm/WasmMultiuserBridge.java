package com.libreshockwave.player.wasm;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.xtra.MultiuserNetBridge;

import java.util.ArrayList;
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

        PendingRequest(int type, int instanceId) {
            this.type = type;
            this.instanceId = instanceId;
        }
    }

    private final List<PendingRequest> pendingRequests = new ArrayList<>();
    private final Map<Integer, Boolean> connectedMap = new HashMap<>();
    private final Map<Integer, List<NetMessage>> messageQueues = new HashMap<>();
    // Legacy keepalives can arrive while authored Lingo is still finishing crypto setup.
    // Echoing that plaintext PONG after the server has enabled crypto closes the socket.
    private final Map<Integer, Boolean> suppressNextPlaintextPong = new HashMap<>();
    private static final int LEGACY_PONG_COMMAND = 196;

    // --- MultiuserNetBridge implementation ---

    @Override
    public void requestConnect(int instanceId, String host, int port) {
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
        pendingRequests.add(req);
    }

    @Override
    public void requestDisconnect(int instanceId) {
        PendingRequest req = new PendingRequest(REQ_DISCONNECT, instanceId);
        pendingRequests.add(req);
        connectedMap.remove(instanceId);
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
    }

    void notifyDisconnected(int instanceId) {
        connectedMap.remove(instanceId);
        suppressNextPlaintextPong.remove(instanceId);
    }

    void notifyError(int instanceId, int errorCode) {
        queueMessage(instanceId, new NetMessage(errorCode, "System", "ConnectionProblem", new Datum.Str("")));
    }

    void deliverMessage(int instanceId, int errorCode, String senderID, String subject, String content) {
        if (isLegacyPlaintextKeepalive(content)) {
            suppressNextPlaintextPong.put(instanceId, true);
        }
        queueMessage(instanceId, new NetMessage(errorCode, senderID, subject, new Datum.Str(content)));
    }

    private void queueMessage(int instanceId, NetMessage msg) {
        messageQueues.computeIfAbsent(instanceId, k -> new ArrayList<>()).add(msg);
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
}
