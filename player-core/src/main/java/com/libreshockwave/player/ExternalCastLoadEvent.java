package com.libreshockwave.player;

/**
 * Generic notification emitted when an external cast library becomes visible
 * to the runtime after loading.
 */
public record ExternalCastLoadEvent(int castLibNumber, String fileName) {
}
