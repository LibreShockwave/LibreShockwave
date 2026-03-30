package com.libreshockwave.player;

/**
 * Observer for external cast loads that have completed and been reindexed.
 * Implementations can refresh movie-specific UI or caches without embedding
 * that logic directly in Player.
 */
public interface ExternalCastLoadHandler {
    void onExternalCastLoaded(Player player, int castLibNumber, String fileName);
}
