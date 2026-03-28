package com.libreshockwave.player;

import com.libreshockwave.vm.TraceListener;
import com.libreshockwave.vm.datum.Datum;

public interface PlayerCompatibilityProfile {

    PlayerCompatibilityProfile NONE = new PlayerCompatibilityProfile() {};

    default void beforePrepareMovie(Player player) {}

    default void afterFrameExecution(Player player) {}

    default void onExternalCastLoaded(Player player, int castLibNumber, String fileName) {}

    default void onHandlerExit(Player player, TraceListener.HandlerInfo info, Datum returnValue) {}
}
