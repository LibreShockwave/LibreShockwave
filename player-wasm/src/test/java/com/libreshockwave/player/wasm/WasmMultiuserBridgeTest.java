package com.libreshockwave.player.wasm;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.xtra.MultiuserNetBridge;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class WasmMultiuserBridgeTest {

    @Test
    void plaintextKeepaliveStillReachesLingoHandler() {
        WasmMultiuserBridge bridge = new WasmMultiuserBridge();

        bridge.deliverMessage(1, 0, "", "", "@r\u0001");

        List<MultiuserNetBridge.NetMessage> messages = bridge.pollMessages(1);
        assertEquals(1, messages.size());
        assertEquals("@r\u0001", messages.get(0).content().toStr());
    }

    @Test
    void plaintextKeepaliveSuppressesMatchingPlaintextPong() {
        WasmMultiuserBridge bridge = new WasmMultiuserBridge();

        bridge.deliverMessage(1, 0, "", "", "@r\u0001");
        bridge.requestSend(1, "0", "0", new Datum.Str("@@BCD"));

        assertTrue(bridge.getPendingRequests().isEmpty());
    }

    @Test
    void plaintextPongQueuesWhenNoKeepaliveWasSeen() {
        WasmMultiuserBridge bridge = new WasmMultiuserBridge();

        bridge.requestSend(1, "0", "0", new Datum.Str("@@BCD"));

        List<WasmMultiuserBridge.PendingRequest> messages = bridge.getPendingRequests();
        assertEquals(1, messages.size());
        assertEquals("@@BCD", messages.get(0).content);
    }
}
