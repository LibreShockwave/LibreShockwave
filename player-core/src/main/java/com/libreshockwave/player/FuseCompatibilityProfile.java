package com.libreshockwave.player;

import com.libreshockwave.vm.TraceListener;
import com.libreshockwave.vm.datum.Datum;

import java.util.Locale;

public final class FuseCompatibilityProfile implements PlayerCompatibilityProfile {

    private boolean frameProxyCreated;
    private boolean visualizerRepairPending;

    @Override
    public void beforePrepareMovie(Player player) {
        frameProxyCreated = false;
        visualizerRepairPending = false;
        player.ensureBuiltinVariableValue("connection.info.id", Datum.symbol("info"));
        player.ensureBuiltinVariableValue("connection.room.id", Datum.symbol("room"));
    }

    @Override
    public void afterFrameExecution(Player player) {
        if (!visualizerRepairPending) {
            return;
        }
        if (player.repairVisualizerSpritesIfNeeded()) {
            visualizerRepairPending = false;
        }
    }

    @Override
    public void onHandlerExit(Player player, TraceListener.HandlerInfo info, Datum returnValue) {
        if (!frameProxyCreated
                && info.handlerName().equalsIgnoreCase("constructObjectManager")
                && returnValue instanceof Datum.ScriptInstance) {
            frameProxyCreated = true;
            player.getTimeoutManager().createTimeout(
                    "fuse_frameProxy", Integer.MAX_VALUE, "null", returnValue);
        }

        String handlerName = info.handlerName().toLowerCase(Locale.ROOT);
        if (handlerName.equals("createvisualizer")
                || handlerName.equals("createwrapper")
                || handlerName.equals("updatelayout")
                || handlerName.equals("showprogram")) {
            visualizerRepairPending = true;
        } else if (handlerName.equals("removevisualizer")
                || handlerName.equals("hideprogram")) {
            visualizerRepairPending = false;
        }
    }
}
