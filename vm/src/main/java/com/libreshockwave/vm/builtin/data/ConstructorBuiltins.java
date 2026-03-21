package com.libreshockwave.vm.builtin.data;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.builtin.xtra.XtraBuiltins;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.LingoVM;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/**
 * Constructor builtin functions (point, rect, color).
 */
public final class ConstructorBuiltins {

    private ConstructorBuiltins() {}

    public static void register(Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins) {
        builtins.put("point", ConstructorBuiltins::point);
        builtins.put("rect", ConstructorBuiltins::rect);
        builtins.put("union", ConstructorBuiltins::union);
        builtins.put("intersect", ConstructorBuiltins::intersect);
        builtins.put("color", ConstructorBuiltins::color);
        builtins.put("rgb", ConstructorBuiltins::rgb);
        builtins.put("paletteindex", ConstructorBuiltins::paletteIndex);
        builtins.put("sprite", ConstructorBuiltins::sprite);
        builtins.put("new", ConstructorBuiltins::newInstance);
    }

    /**
     * new(obj, args...)
     * Creates a new instance of an Xtra or parent script.
     * Example: set instance = new(xtra("Multiuser"))
     * Example: set obj = new(script("MyScript"), arg1, arg2)
     */
    private static Datum newInstance(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.VOID;
        }

        Datum target = args.get(0);
        List<Datum> constructorArgs = args.size() > 1
            ? new ArrayList<>(args.subList(1, args.size()))
            : new ArrayList<>();

        // Handle new(#memberType, castLib) - create new cast member
        if (target instanceof Datum.Symbol typeSymbol && !constructorArgs.isEmpty()
                && constructorArgs.get(0) instanceof Datum.CastLibRef clr) {
            CastLibProvider provider = CastLibProvider.getProvider();
            if (provider != null) {
                return provider.createMember(clr.castLibNum(), typeSymbol.name());
            }
            return Datum.VOID;
        }

        // Handle Xtra instances
        if (target instanceof Datum.XtraRef xtraRef) {
            return XtraBuiltins.createInstance(xtraRef, constructorArgs);
        }

        // Handle script references - create a new script instance
        if (target instanceof Datum.ScriptRef scriptRef) {
            return createScriptInstance(vm, scriptRef, constructorArgs);
        }

        // Handle parent scripts (ScriptInstance) - clone behavior
        if (target instanceof Datum.ScriptInstance) {
            // TODO: Clone/create instance of parent script
            return Datum.VOID;
        }

        return Datum.VOID;
    }

    /**
     * Create a new script instance from a ScriptRef.
     * Looks up the script by cast member reference and creates an instance.
     */
    private static Datum createScriptInstance(LingoVM vm, Datum.ScriptRef scriptRef,
                                               List<Datum> args) {
        // Create a new ScriptInstance with unique ID
        int instanceId = nextInstanceId++;
        Map<String, Datum> properties = new LinkedHashMap<>();

        // Store the script reference for method dispatch
        properties.put(Datum.PROP_SCRIPT_REF, scriptRef);

        // Pre-initialize declared properties to VOID (matching dirplayer-rs behavior)
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider != null) {
            java.util.List<String> propNames = provider.getScriptPropertyNames(
                scriptRef.castLibNum(), scriptRef.memberNum());
            for (String name : propNames) {
                properties.put(name, Datum.VOID);
            }
        }

        Datum.ScriptInstance instance = new Datum.ScriptInstance(instanceId, properties);

        // Call the script's "new" handler if it exists (matching dirplayer-rs script.rs:208-231).
        // This is how ancestor chains get built:
        //   on new me → me.ancestor = new(script("ParentClass")) → return me
        // No infinite recursion because each level is a different script.
        if (provider != null) {
            CastLibProvider.HandlerLocation location = provider.findHandlerInScript(
                scriptRef.castLibNum(), scriptRef.memberNum(), "new");
            if (location != null && location.script() instanceof ScriptChunk script
                    && location.handler() instanceof ScriptChunk.Handler handler) {
                Datum result = vm.executeHandler(script, handler, args, instance);
                // Return handler's return value (usually 'me' after ancestor setup)
                if (result != null && !result.isVoid()) {
                    return result;
                }
            }
        }

        return instance;
    }

    private static int nextInstanceId = 1;

    private static Datum point(LingoVM vm, List<Datum> args) {
        int x = args.size() > 0 ? args.get(0).toInt() : 0;
        int y = args.size() > 1 ? args.get(1).toInt() : 0;
        return new Datum.Point(x, y);
    }

    private static Datum rect(LingoVM vm, List<Datum> args) {
        // Director supports rect(point1, point2) → rect(p1.x, p1.y, p2.x, p2.y)
        if (args.size() == 2 && args.get(0) instanceof Datum.Point p1 && args.get(1) instanceof Datum.Point p2) {
            return new Datum.Rect(p1.x(), p1.y(), p2.x(), p2.y());
        }
        int left = args.size() > 0 ? args.get(0).toInt() : 0;
        int top = args.size() > 1 ? args.get(1).toInt() : 0;
        int right = args.size() > 2 ? args.get(2).toInt() : 0;
        int bottom = args.size() > 3 ? args.get(3).toInt() : 0;
        return new Datum.Rect(left, top, right, bottom);
    }

    /**
     * union(rect1, rect2) - returns the smallest rect encompassing both rects.
     */
    private static Datum union(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;
        if (!(args.get(0) instanceof Datum.Rect r1) || !(args.get(1) instanceof Datum.Rect r2)) {
            return Datum.VOID;
        }
        // If either rect is empty (zero area), return the other
        boolean r1Empty = (r1.right() <= r1.left()) || (r1.bottom() <= r1.top());
        boolean r2Empty = (r2.right() <= r2.left()) || (r2.bottom() <= r2.top());
        if (r1Empty && r2Empty) return new Datum.Rect(0, 0, 0, 0);
        if (r1Empty) return r2;
        if (r2Empty) return r1;
        return new Datum.Rect(
                Math.min(r1.left(), r2.left()),
                Math.min(r1.top(), r2.top()),
                Math.max(r1.right(), r2.right()),
                Math.max(r1.bottom(), r2.bottom()));
    }

    /**
     * intersect(rect1, rect2) - returns the intersection of two rects.
     */
    private static Datum intersect(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;
        if (!(args.get(0) instanceof Datum.Rect r1) || !(args.get(1) instanceof Datum.Rect r2)) {
            return Datum.VOID;
        }
        int left = Math.max(r1.left(), r2.left());
        int top = Math.max(r1.top(), r2.top());
        int right = Math.min(r1.right(), r2.right());
        int bottom = Math.min(r1.bottom(), r2.bottom());
        if (right <= left || bottom <= top) {
            return new Datum.Rect(0, 0, 0, 0);
        }
        return new Datum.Rect(left, top, right, bottom);
    }

    private static Datum color(LingoVM vm, List<Datum> args) {
        int r = args.size() > 0 ? args.get(0).toInt() : 0;
        int g = args.size() > 1 ? args.get(1).toInt() : 0;
        int b = args.size() > 2 ? args.get(2).toInt() : 0;
        return new Datum.Color(r, g, b);
    }

    /**
     * paletteIndex(index) - create a color by looking up an index in the active movie palette.
     * In Director, paletteIndex(n) resolves index n through the current palette to produce
     * an RGB color. Used extensively by Habbo's window system for row backgrounds, buttons, etc.
     */
    private static Datum paletteIndex(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return new Datum.PaletteIndexColor(0);
        }
        return new Datum.PaletteIndexColor(args.get(0).toInt() & 0xFF);
    }

    /**
     * rgb(r, g, b) - create color from RGB components
     * rgb("#RRGGBB") - create color from hex string
     * rgb(paletteIndex) - create color from palette index (treated as grayscale)
     */
    private static Datum rgb(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return new Datum.Color(0, 0, 0);
        }
        Datum first = args.get(0);
        // rgb(colorDatum) - pass through Color unchanged
        if (first instanceof Datum.Color c && args.size() == 1) {
            return c;
        }
        // rgb("#RRGGBB") - hex string
        if (first.isString()) {
            String hex = first.toStr().trim();
            if (hex.startsWith("#")) {
                hex = hex.substring(1);
            }
            try {
                int colorVal = Integer.parseInt(hex, 16);
                int r = (colorVal >> 16) & 0xFF;
                int g = (colorVal >> 8) & 0xFF;
                int b = colorVal & 0xFF;
                return new Datum.Color(r, g, b);
            } catch (NumberFormatException e) {
                return new Datum.Color(0, 0, 0);
            }
        }
        // rgb(r, g, b) - three integer components
        if (args.size() >= 3) {
            return new Datum.Color(args.get(0).toInt(), args.get(1).toInt(), args.get(2).toInt());
        }
        // rgb(paletteIndex) - single integer, treat as grayscale or palette
        int val = first.toInt();
        return new Datum.Color((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
    }

    /**
     * sprite(channelNum) - return a sprite reference for the given channel number.
     * In Director, sprite(n) returns a reference to sprite channel n.
     */
    private static Datum sprite(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.VOID;
        }
        int channel = args.get(0).toInt();
        return Datum.SpriteRef.of(channel);
    }
}
