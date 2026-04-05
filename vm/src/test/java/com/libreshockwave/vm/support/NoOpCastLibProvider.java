package com.libreshockwave.vm.support;

import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;

/**
 * Test helper with neutral CastLibProvider defaults so tests only override
 * behavior they actually assert against.
 */
public class NoOpCastLibProvider implements CastLibProvider {

    @Override
    public int getCastLibByNumber(int castLibNumber) {
        return castLibNumber;
    }

    @Override
    public int getCastLibByName(String name) {
        return 0;
    }

    @Override
    public Datum getCastLibProp(int castLibNumber, String propName) {
        return Datum.VOID;
    }

    @Override
    public boolean setCastLibProp(int castLibNumber, String propName, Datum value) {
        return false;
    }

    @Override
    public Datum getMember(int castLibNumber, int memberNumber) {
        return Datum.CastMemberRef.of(castLibNumber, memberNumber);
    }

    @Override
    public Datum getMemberByName(int castLibNumber, String memberName) {
        return Datum.VOID;
    }

    @Override
    public int getCastLibCount() {
        return 0;
    }

    @Override
    public Datum getMemberProp(int castLibNumber, int memberNumber, String propName) {
        return Datum.VOID;
    }

    @Override
    public boolean setMemberProp(int castLibNumber, int memberNumber, String propName, Datum value) {
        return false;
    }
}
