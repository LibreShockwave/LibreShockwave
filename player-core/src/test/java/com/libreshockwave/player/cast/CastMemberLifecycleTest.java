package com.libreshockwave.player.cast;

import com.libreshockwave.cast.MemberType;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

class CastMemberLifecycleTest {

    @Test
    void eraseClearsDynamicMemberTextAndName() {
        CastMember member = new CastMember(1, 10001, MemberType.TEXT);
        member.setProp("name", Datum.of("room_status"));
        member.setProp("text", Datum.of("hello"));

        Datum result = member.callMethod("erase", List.of());

        assertEquals(1, result.toInt());
        assertEquals("", member.getName());
        assertEquals("", member.getTextContent());
    }
}
