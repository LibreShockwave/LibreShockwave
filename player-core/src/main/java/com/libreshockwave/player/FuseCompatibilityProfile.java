package com.libreshockwave.player;

import com.libreshockwave.vm.TraceListener;
import com.libreshockwave.vm.datum.Datum;

public final class FuseCompatibilityProfile implements PlayerCompatibilityProfile {

    @Override
    public void onHandlerExit(Player player, TraceListener.HandlerInfo info, Datum returnValue) {
        String handlerName = info.handlerName();
        if (handlerName.equalsIgnoreCase("showprogram")
                || handlerName.equalsIgnoreCase("hideprogram")
                || handlerName.equalsIgnoreCase("createvisualizer")
                || handlerName.equalsIgnoreCase("removevisualizer")
                || handlerName.equalsIgnoreCase("pauseupdate")
                || handlerName.equalsIgnoreCase("resetclient")
                || handlerName.equalsIgnoreCase("alerthook")) {
            player.getStageRenderer().resetVisualState();
        }
    }
}
