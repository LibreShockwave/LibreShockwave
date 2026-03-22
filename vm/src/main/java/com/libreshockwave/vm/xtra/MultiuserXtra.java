package com.libreshockwave.vm.xtra;

import com.libreshockwave.vm.datum.Datum;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Implementation of the Director Multiuser Xtra.
 * Provides network messaging for Lingo scripts (used by Habbo Hotel's MUS connection).
 *
 * Lingo usage:
 *   pXtra = new(xtra("Multiuser"))
 *   pXtra.setNetBufferLimits(16384, 102400, 100)
 *   pXtra.setNetMessageHandler(#xtraMsgHandler, me)
 *   pXtra.connectToNetServer("*", "*", host, port, "*", 0)
 *   pXtra.sendNetMessage("*", subject, content)
 *   pXtra.checkNetMessages(1)  -- fires callback
 *   tMsg = pXtra.getNetMessage()  -- inside callback
 */
public class MultiuserXtra implements Xtra {

    private final MultiuserNetBridge netBridge;
    private final ScriptCallback scriptCallback;
    private final Map<Integer, InstanceState> instances = new HashMap<>();
    private int nextInstanceId = 1;

    public MultiuserXtra(MultiuserNetBridge netBridge, ScriptCallback scriptCallback) {
        this.netBridge = netBridge;
        this.scriptCallback = scriptCallback;
    }

    @Override
    public String getName() {
        return "Multiuser";
    }

    @Override
    public int createInstance(List<Datum> args) {
        int id = nextInstanceId++;
        instances.put(id, new InstanceState());
        return id;
    }

    @Override
    public void destroyInstance(int instanceId) {
        InstanceState state = instances.remove(instanceId);
        if (state != null) {
            netBridge.requestDisconnect(instanceId);
            netBridge.destroyInstance(instanceId);
        }
    }

    @Override
    public Datum callHandler(int instanceId, String handlerName, List<Datum> args) {
        InstanceState state = instances.get(instanceId);
        if (state == null) {
            System.err.println("[MultiuserXtra] Instance not found: " + instanceId);
            return Datum.VOID;
        }

        return switch (handlerName.toLowerCase()) {
            case "setnetbufferlimits" -> setNetBufferLimits(state, args);
            case "setnetmessagehandler" -> setNetMessageHandler(state, args);
            case "connecttonetserver" -> connectToNetServer(instanceId, state, args);
            case "sendnetmessage" -> sendNetMessage(instanceId, state, args);
            case "getnetmessage" -> getNetMessage(state);
            case "checknetmessages" -> checkNetMessages(instanceId, state, args);
            case "getnumberwaitingnetmessages" -> getNumberWaitingNetMessages(instanceId, state);
            case "getneterrorstring" -> getNetErrorString(args);
            default -> {
                System.err.println("[MultiuserXtra] Unknown handler: " + handlerName);
                yield Datum.VOID;
            }
        };
    }

    @Override
    public Datum getProperty(int instanceId, String propertyName) {
        return Datum.VOID;
    }

    @Override
    public void setProperty(int instanceId, String propertyName, Datum value) {
    }

    /**
     * Auto-process pending messages for instances with registered handlers.
     * Called each frame by the Player via XtraManager.tickAll().
     * This emulates Director's behavior where setNetMessageHandler callbacks
     * fire automatically when messages arrive, without explicit checkNetMessages calls.
     */
    @Override
    public void tick() {
        for (var entry : instances.entrySet()) {
            int instanceId = entry.getKey();
            InstanceState state = entry.getValue();

            if (state.callbackHandler == null || state.callbackTarget == null) {
                continue;
            }

            // Poll new messages from the bridge
            List<MultiuserNetBridge.NetMessage> messages = netBridge.pollMessages(instanceId);
            state.messageQueue.addAll(messages);

            // Process all pending messages
            while (!state.messageQueue.isEmpty()) {
                state.currentMessage = state.messageQueue.remove(0);
                try {
                    scriptCallback.invoke(state.callbackTarget, state.callbackHandler, List.of());
                } catch (Exception e) {
                    System.err.println("[MultiuserXtra] Auto-callback error: " + e.getMessage());
                }
            }
            state.currentMessage = null;
        }
    }

    // --- Handler implementations ---

    private Datum setNetBufferLimits(InstanceState state, List<Datum> args) {
        if (args.size() >= 3) {
            state.bufferMin = args.get(0).toInt();
            state.bufferMax = args.get(1).toInt();
            state.bufferUrgency = args.get(2).toInt();
        }
        return Datum.ZERO;
    }

    private Datum setNetMessageHandler(InstanceState state, List<Datum> args) {
        if (args.size() >= 2) {
            Datum handlerArg = args.get(0);
            Datum targetArg = args.get(1);

            if (handlerArg.isVoid() || targetArg.isVoid()) {
                // Clear the handler (disconnect pattern)
                state.callbackHandler = null;
                state.callbackTarget = null;
            } else {
                state.callbackHandler = handlerArg instanceof Datum.Symbol sym
                        ? sym.name() : handlerArg.toStr();
                state.callbackTarget = targetArg;
            }
        }
        return Datum.ZERO; // 0 = success
    }

    private Datum connectToNetServer(int instanceId, InstanceState state, List<Datum> args) {
        // connectToNetServer(senderID, user, host, port, appID, encryptFlag)
        if (args.size() >= 4) {
            String host = args.get(2).toStr();
            int port = args.get(3).toInt();
            state.host = host;
            state.port = port;
            netBridge.requestConnect(instanceId, host, port);
        }
        return Datum.ZERO;
    }

    private Datum sendNetMessage(int instanceId, InstanceState state, List<Datum> args) {
        // sendNetMessage(senderID, subject, content)
        if (args.size() >= 3) {
            String senderID = args.get(0).toStr();
            String subject = args.get(1).toStr();
            Datum content = args.get(2);
            netBridge.requestSend(instanceId, senderID, subject, content);
        }
        return Datum.ZERO;
    }

    private Datum getNetMessage(InstanceState state) {
        if (state.currentMessage == null) {
            return Datum.VOID;
        }

        MultiuserNetBridge.NetMessage msg = state.currentMessage;
        Datum.PropList pl = new Datum.PropList();
        pl.add("errorCode", Datum.of(msg.errorCode()), true);
        pl.add("senderID", Datum.of(msg.senderID()), true);
        pl.add("subject", Datum.of(msg.subject()), true);
        pl.add("content", msg.content() != null ? msg.content() : Datum.VOID, true);
        return pl;
    }

    private Datum checkNetMessages(int instanceId, InstanceState state, List<Datum> args) {
        int count = args.isEmpty() ? 1 : args.get(0).toInt();

        // Poll messages from the bridge
        List<MultiuserNetBridge.NetMessage> messages = netBridge.pollMessages(instanceId);
        state.messageQueue.addAll(messages);

        int processed = 0;
        for (int i = 0; i < count && !state.messageQueue.isEmpty(); i++) {
            state.currentMessage = state.messageQueue.remove(0);
            processed++;

            // Fire the registered callback
            if (state.callbackHandler != null && state.callbackTarget != null) {
                try {
                    scriptCallback.invoke(state.callbackTarget, state.callbackHandler, List.of());
                } catch (Exception e) {
                    System.err.println("[MultiuserXtra] Callback error: " + e.getMessage());
                }
            }
        }

        state.currentMessage = null;
        return Datum.of(processed);
    }

    private Datum getNumberWaitingNetMessages(int instanceId, InstanceState state) {
        // Include any not-yet-polled messages from the bridge
        List<MultiuserNetBridge.NetMessage> messages = netBridge.pollMessages(instanceId);
        state.messageQueue.addAll(messages);
        return Datum.of(state.messageQueue.size());
    }

    private Datum getNetErrorString(List<Datum> args) {
        if (args.isEmpty()) return Datum.of("");
        int code = args.get(0).toInt();
        return Datum.of(switch (code) {
            case 0 -> "No error";
            case -1 -> "Memory allocation error";
            case -2 -> "Network error";
            case -3 -> "Connection refused";
            case -4 -> "Connection timed out";
            case -5 -> "Invalid message";
            case -6 -> "Invalid server address";
            default -> "Unknown error (" + code + ")";
        });
    }

    // --- Instance state ---

    private static class InstanceState {
        String host;
        int port;
        int bufferMin;
        int bufferMax;
        int bufferUrgency;
        String callbackHandler;
        Datum callbackTarget;
        MultiuserNetBridge.NetMessage currentMessage;
        final List<MultiuserNetBridge.NetMessage> messageQueue = new ArrayList<>();
    }
}
