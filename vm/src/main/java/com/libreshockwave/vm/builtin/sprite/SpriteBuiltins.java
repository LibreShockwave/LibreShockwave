package com.libreshockwave.vm.builtin.sprite;

import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.LingoVM;

import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/**
 * Sprite and stage control builtin functions for Lingo.
 *
 * Provides:
 * - puppetTempo(rate) - set the tempo programmatically
 * - puppetSprite(spriteNum, enabled) - take/release programmatic control of a sprite
 * - cursor(type) - set the cursor (no-op stub)
 * - pauseUpdate() - pause screen updates (no-op stub)
 * - updateStage() - force screen update (no-op stub)
 */
public final class SpriteBuiltins {

    private SpriteBuiltins() {}

    public static void register(Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins) {
        builtins.put("puppettempo", SpriteBuiltins::puppetTempo);
        builtins.put("puppetsprite", SpriteBuiltins::puppetSprite);
        builtins.put("cursor", SpriteBuiltins::cursor);
        builtins.put("pauseupdate", (vm, args) -> Datum.VOID);  // No-op stub
        builtins.put("updatestage", (vm, args) -> Datum.VOID);  // No-op stub
        builtins.put("movetofront", (vm, args) -> Datum.VOID);  // No-op stub
        builtins.put("movetoback", (vm, args) -> Datum.VOID);  // No-op stub
    }

    /**
     * puppetSprite(spriteNum, enabled)
     * Takes programmatic control of a sprite channel.
     * When puppet is TRUE, the sprite is controlled by script instead of the score.
     */
    private static Datum puppetSprite(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) {
            return Datum.VOID;
        }

        int spriteNum = args.get(0).toInt();
        boolean enabled = args.get(1).isTruthy();

        SpritePropertyProvider provider = SpritePropertyProvider.getProvider();
        if (provider != null) {
            provider.setSpriteProp(spriteNum, "puppet", Datum.of(enabled ? 1 : 0));
        }

        return Datum.VOID;
    }

    /**
     * puppetTempo(rate)
     * Sets the tempo of the movie programmatically.
     * The tempo remains in effect until another puppetTempo call or until
     * the movie encounters a tempo setting in the score.
     * Set to 0 to return to score tempo.
     */
    private static Datum puppetTempo(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.VOID;
        }

        int tempo = args.get(0).toInt();

        MoviePropertyProvider provider = MoviePropertyProvider.getProvider();
        if (provider != null) {
            provider.setMovieProp("puppetTempo", Datum.of(tempo));
        }

        return Datum.VOID;
    }

    private static Datum cursor(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.VOID;
        }

        MoviePropertyProvider provider = MoviePropertyProvider.getProvider();
        if (provider != null) {
            provider.setMovieProp("cursor", args.get(0));
        }

        return Datum.VOID;
    }
}
