package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.builtin.media.SoundProvider;

import java.util.List;

/**
 * Handles method calls and property access on SoundChannel datums.
 * Sound channels are returned by sound(N) and support methods like
 * play(), stop(), queue(), isBusy(), and properties like volume, member.
 *
 * Delegates to SoundProvider for actual playback when available.
 */
public final class SoundChannelMethodDispatcher {

    private SoundChannelMethodDispatcher() {}

    public static Datum dispatch(Datum.SoundChannel channel, String methodName, List<Datum> args) {
        int ch = channel.channelNum();
        SoundProvider provider = SoundProvider.getProvider();
        String method = methodName.toLowerCase();

        return switch (method) {
            // Playback control
            case "play", "queue" -> {
                if (provider != null && !args.isEmpty()) {
                    provider.play(ch, args.get(0));
                }
                yield Datum.VOID;
            }
            case "stop", "fadeout" -> {
                if (provider != null) {
                    provider.stop(ch);
                }
                yield Datum.VOID;
            }
            case "pause", "resume", "unpause" -> Datum.VOID;
            case "playfile" -> Datum.VOID;
            case "playnext" -> Datum.VOID;
            case "breakloop" -> Datum.VOID;
            case "rewind" -> Datum.VOID;
            case "fadein" -> Datum.VOID;
            case "fadeto" -> Datum.VOID;

            // Status queries
            case "isbusy" -> provider != null && provider.isPlaying(ch) ? Datum.TRUE : Datum.FALSE;
            case "status" -> provider != null && provider.isPlaying(ch) ? Datum.of(1) : Datum.ZERO;
            case "elapsedtime", "currenttime" -> provider != null
                    ? Datum.of(provider.getElapsedTime(ch))
                    : Datum.ZERO;

            // Playlist
            case "setplaylist" -> Datum.VOID;
            case "getplaylist" -> Datum.list();

            // Properties accessed as methods
            case "volume" -> {
                if (!args.isEmpty()) {
                    if (provider != null) {
                        provider.setVolume(ch, args.get(0).toInt());
                    }
                    yield Datum.VOID;
                }
                yield Datum.of(provider != null ? provider.getVolume(ch) : 255);
            }
            case "pan" -> Datum.ZERO;
            case "member" -> Datum.VOID;

            // ilk
            case "ilk" -> new Datum.Symbol("instance");

            default -> Datum.VOID;
        };
    }

    /**
     * Get a property from a sound channel.
     */
    public static Datum getProperty(Datum.SoundChannel channel, String propName) {
        int ch = channel.channelNum();
        SoundProvider provider = SoundProvider.getProvider();
        String prop = propName.toLowerCase();

        return switch (prop) {
            case "volume" -> Datum.of(provider != null ? provider.getVolume(ch) : 255);
            case "pan" -> Datum.ZERO;
            case "status" -> provider != null && provider.isPlaying(ch) ? Datum.of(1) : Datum.ZERO;
            case "member" -> Datum.VOID;
            case "loopcount" -> Datum.of(1);
            case "loopstarttime" -> Datum.ZERO;
            case "loopendtime" -> Datum.ZERO;
            case "starttime" -> Datum.ZERO;
            case "endtime" -> Datum.ZERO;
            case "elapsedtime", "currenttime" -> provider != null
                    ? Datum.of(provider.getElapsedTime(ch))
                    : Datum.ZERO;
            default -> Datum.VOID;
        };
    }

    /**
     * Set a property on a sound channel.
     */
    public static boolean setProperty(Datum.SoundChannel channel, String propName, Datum value) {
        int ch = channel.channelNum();
        SoundProvider provider = SoundProvider.getProvider();
        String prop = propName.toLowerCase();

        return switch (prop) {
            case "volume" -> {
                if (provider != null) {
                    provider.setVolume(ch, value.toInt());
                }
                yield true;
            }
            case "pan", "loopcount", "starttime", "endtime",
                 "loopstarttime", "loopendtime" -> true;
            default -> false;
        };
    }
}
