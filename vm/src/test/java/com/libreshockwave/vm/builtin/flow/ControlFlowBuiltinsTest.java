package com.libreshockwave.vm.builtin.flow;

import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.util.LinkedHashMap;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotSame;
import static org.junit.jupiter.api.Assertions.assertSame;

class ControlFlowBuiltinsTest {

    @Test
    void snapshotStructArgsForCallClonesMessageStructsButKeepsConnectionReference() {
        Datum.ScriptInstance connection = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        Datum.List nested = new Datum.List(List.of(Datum.of("payload")));
        Datum.PropList message = new Datum.PropList();
        message.putTyped("ilk", true, Datum.symbol("struct"));
        message.putTyped("connection", true, connection);
        message.putTyped("subject", true, Datum.of("85"));
        message.putTyped("content", true, nested);

        List<Datum> snapshotArgs = ControlFlowBuiltins.snapshotStructArgsForCall(List.of(message));

        Datum.PropList copied = (Datum.PropList) snapshotArgs.getFirst();
        assertNotSame(message, copied);
        assertSame(connection, copied.getOrDefault("connection", true, Datum.VOID));
        assertNotSame(nested, copied.getOrDefault("content", true, Datum.VOID));

        nested.items().add(Datum.of("mutated"));
        assertEquals(1, ((Datum.List) copied.getOrDefault("content", true, Datum.VOID)).items().size());
    }

    @Test
    void snapshotStructArgsForCallLeavesNonMessageStructArgsUntouched() {
        Datum.PropList plain = new Datum.PropList();
        plain.putTyped("name", true, Datum.of("plain"));

        List<Datum> snapshotArgs = ControlFlowBuiltins.snapshotStructArgsForCall(List.of(plain));

        assertSame(plain, snapshotArgs.getFirst());
    }
}
