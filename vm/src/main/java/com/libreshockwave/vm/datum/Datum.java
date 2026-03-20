package com.libreshockwave.vm.datum;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.id.CastLibId;
import com.libreshockwave.id.ChannelId;
import com.libreshockwave.id.MemberId;
import com.libreshockwave.id.VarType;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.function.Supplier;

/**
 * Runtime value in the Lingo VM.
 * Represents all possible value types in the Director scripting language.
 * Similar to dirplayer-rs datum.rs.
 */
public sealed interface Datum {

    /** Mutable holder for the active palette (no Supplier lambda — TeaVM wraps
     *  lambda invocation in monitorEnterSync which throws in the WASM backend).
     *  No volatile — WASM is single-threaded. */
    final class PaletteHolder {
        static Palette palette;
    }

    /**
     * Set the active palette directly. Called by Player before each frame tick
     * (via setupProviders) so that Datum colour resolution uses the current palette.
     */
    static void setActivePalette(Palette palette) {
        PaletteHolder.palette = palette;
    }

    /**
     * Get the active palette for color resolution.
     * Falls back to System Mac palette if none is set.
     */
    static Palette getActivePalette() {
        Palette p = PaletteHolder.palette;
        return p != null ? p : Palette.SYSTEM_MAC_PALETTE;
    }

    /** Void/null value */
    record Void() implements Datum {
        @Override
        public String toString() { return "<Void>"; }
    }

    /** Integer value */
    record Int(int value) implements Datum {
        @Override
        public String toString() { return String.valueOf(value); }
    }

    /** Floating point value */
    record Float(double value) implements Datum {
        @Override
        public String toString() { return String.valueOf(value); }
    }

    /** String value */
    record Str(String value) implements Datum {
        @Override
        public String toString() { return "\"" + value + "\""; }
    }

    /** Symbol value (like #symbol in Lingo) */
    record Symbol(String name) implements Datum {
        @Override
        public String toString() { return "#" + name; }
    }

    /** Linear list [a, b, c] */
    record List(java.util.List<Datum> items) implements Datum {
        public List {
            items = new ArrayList<>(items);
        }
        @Override
        public String toString() {
            StringBuilder sb = new StringBuilder("[");
            for (int i = 0; i < items.size(); i++) {
                if (i > 0) sb.append(", ");
                sb.append(items.get(i));
            }
            return sb.append("]").toString();
        }
    }

    /** Key-value entry in a PropList. */
    record PropEntry(String key, Datum value) {}

    /**
     * Property list [#a: 1, #b: 2].
     * Supports duplicate keys — Director's PropList is an ordered list of key-value pairs.
     */
    final class PropList implements Datum {
        private final java.util.List<PropEntry> entries;

        public PropList() {
            this.entries = new ArrayList<>();
        }

        public PropList(java.util.List<PropEntry> entries) {
            this.entries = new ArrayList<>(entries);
        }

        public java.util.List<PropEntry> entries() { return entries; }
        public int size() { return entries.size(); }
        public boolean isEmpty() { return entries.isEmpty(); }

        /** Get first value matching key (case-insensitive, Director semantics). */
        public Datum get(String key) {
            for (PropEntry e : entries) {
                if (e.key().equalsIgnoreCase(key)) return e.value();
            }
            return null;
        }

        /** Get first value matching key, or default if not found. */
        public Datum getOrDefault(String key, Datum defaultVal) {
            Datum v = get(key);
            return v != null ? v : defaultVal;
        }

        /** Set first matching key's value, or append if not found (case-insensitive). */
        public void put(String key, Datum value) {
            for (int i = 0; i < entries.size(); i++) {
                if (entries.get(i).key().equalsIgnoreCase(key)) {
                    entries.set(i, new PropEntry(entries.get(i).key(), value));
                    return;
                }
            }
            entries.add(new PropEntry(key, value));
        }

        /** Always append — allows duplicate keys (Director's addProp behavior). */
        public void add(String key, Datum value) {
            entries.add(new PropEntry(key, value));
        }

        /** Remove first entry matching key (case-insensitive). */
        public void remove(String key) {
            for (int i = 0; i < entries.size(); i++) {
                if (entries.get(i).key().equalsIgnoreCase(key)) {
                    entries.remove(i);
                    return;
                }
            }
        }

        /** Check if any entry has this key (case-insensitive). */
        public boolean containsKey(String key) {
            for (PropEntry e : entries) {
                if (e.key().equalsIgnoreCase(key)) return true;
            }
            return false;
        }

        /** Get value at position (0-based). */
        public Datum getValue(int index) {
            return entries.get(index).value();
        }

        /** Get key at position (0-based). */
        public String getKey(int index) {
            return entries.get(index).key();
        }

        /** Set value at position (0-based), preserving the key. */
        public void setValue(int index, Datum value) {
            entries.set(index, new PropEntry(entries.get(index).key(), value));
        }

        /** Remove entry at position (0-based). */
        public void removeAt(int index) {
            entries.remove(index);
        }

        /** Find 1-based position of key (case-insensitive), or 0 if not found. */
        public int findPos(String key) {
            for (int i = 0; i < entries.size(); i++) {
                if (entries.get(i).key().equalsIgnoreCase(key)) return i + 1;
            }
            return 0;
        }

        @Override
        public String toString() {
            StringBuilder sb = new StringBuilder("[");
            for (int i = 0; i < entries.size(); i++) {
                if (i > 0) sb.append(", ");
                sb.append("#").append(entries.get(i).key()).append(": ").append(entries.get(i).value());
            }
            return sb.append("]").toString();
        }

        @Override
        public boolean equals(Object o) {
            return o instanceof PropList pl && entries.equals(pl.entries);
        }

        @Override
        public int hashCode() {
            return entries.hashCode();
        }
    }

    /** Sprite reference */
    record SpriteRef(ChannelId channel) implements Datum {
        public static SpriteRef of(int channel) { return new SpriteRef(new ChannelId(channel)); }
        public int channelNum() { return channel.value(); }
        @Override
        public String toString() { return "sprite(" + channel.value() + ")"; }
    }

    /** Cast member reference */
    record CastMemberRef(CastLibId castLib, MemberId member) implements Datum {
        /** Safe factory: returns VOID for invalid castLib/member (0 means "no member" in Director). */
        public static Datum of(int castLib, int member) {
            if (castLib < 1 || member < 1) return VOID;
            return new CastMemberRef(new CastLibId(castLib), new MemberId(member));
        }
        public int castLibNum() { return castLib.value(); }
        public int memberNum() { return member.value(); }
        @Override
        public String toString() { return "member(" + member.value() + ", " + castLib.value() + ")"; }
    }

    /** Script instance reference */
    record ScriptInstance(int scriptId, Map<String, Datum> properties) implements Datum {
        @Override
        public String toString() { return "<script instance " + scriptId + ">"; }
    }

    /** Point value - mutable to match Director's value semantics (setAt support) */
    final class Point implements Datum {
        private int x, y;
        public Point(int x, int y) { this.x = x; this.y = y; }
        public int x() { return x; }
        public int y() { return y; }
        public void setX(int x) { this.x = x; }
        public void setY(int y) { this.y = y; }
        public void setComponent(int index, int value) {
            switch (index) { case 1 -> x = value; case 2 -> y = value; }
        }
        @Override public String toString() { return "point(" + x + ", " + y + ")"; }
        @Override public boolean equals(Object o) {
            return o instanceof Point p && x == p.x && y == p.y;
        }
        @Override public int hashCode() { return Objects.hash(x, y); }
    }

    /** Rectangle value - mutable to match Director's value semantics (setAt support) */
    final class Rect implements Datum {
        private int left, top, right, bottom;
        public Rect(int left, int top, int right, int bottom) {
            this.left = left; this.top = top; this.right = right; this.bottom = bottom;
        }
        public int left() { return left; }
        public int top() { return top; }
        public int right() { return right; }
        public int bottom() { return bottom; }
        public void setLeft(int v) { left = v; }
        public void setTop(int v) { top = v; }
        public void setRight(int v) { right = v; }
        public void setBottom(int v) { bottom = v; }
        public void setComponent(int index, int value) {
            switch (index) { case 1 -> left = value; case 2 -> top = value; case 3 -> right = value; case 4 -> bottom = value; }
        }
        public int width() { return right - left; }
        public int height() { return bottom - top; }
        @Override public String toString() { return "rect(" + left + ", " + top + ", " + right + ", " + bottom + ")"; }
        @Override public boolean equals(Object o) {
            return o instanceof Rect r && left == r.left && top == r.top && right == r.right && bottom == r.bottom;
        }
        @Override public int hashCode() { return Objects.hash(left, top, right, bottom); }
    }

    /** Color value */
    record Color(int r, int g, int b) implements Datum {
        @Override
        public String toString() { return "color(" + r + ", " + g + ", " + b + ")"; }
    }

    /**
     * Palette-indexed color value. Created by paletteIndex(n).
     * Carries the raw palette index and gets resolved through the target bitmap's palette
     * when used in fill(), copyPixels(), etc.
     */
    record PaletteIndexColor(int index) implements Datum {
        @Override
        public String toString() { return "paletteIndex(" + index + ")"; }
    }

    /**
     * Image reference (wraps a bitmap for Director's image API).
     * Can either hold a direct bitmap or a supplier that resolves to a member's current bitmap.
     * The supplier form is used for member.image so that Lingo variables like pImg = member.image
     * stay in sync even after member.image = newImage replaces the member's bitmap.
     */
    final class ImageRef implements Datum {
        private final Bitmap directBitmap;
        private final java.util.function.Supplier<Bitmap> bitmapSupplier;

        /** Create an ImageRef wrapping a specific bitmap (standalone images, duplicates, etc.) */
        public ImageRef(Bitmap bitmap) {
            this.directBitmap = bitmap;
            this.bitmapSupplier = null;
        }

        /** Create an ImageRef that resolves the bitmap lazily (for member.image live references) */
        public ImageRef(java.util.function.Supplier<Bitmap> supplier) {
            this.directBitmap = null;
            this.bitmapSupplier = supplier;
        }

        public Bitmap bitmap() {
            return bitmapSupplier != null ? bitmapSupplier.get() : directBitmap;
        }

        /** Returns true if this is a live reference (backed by a supplier). */
        public boolean isLive() { return bitmapSupplier != null; }

        @Override
        public String toString() {
            Bitmap bmp = bitmap();
            return bmp != null ? "(image " + bmp.getWidth() + "x" + bmp.getHeight() + ")" : "(image null)";
        }
    }

    /** Xtra reference (the Xtra class itself) */
    record XtraRef(String xtraName) implements Datum {
        @Override
        public String toString() { return "<Xtra \"" + xtraName + "\">"; }
    }

    /** Xtra instance reference */
    record XtraInstance(String xtraName, int instanceId) implements Datum {
        @Override
        public String toString() { return "<XtraInstance \"" + xtraName + "\" #" + instanceId + ">"; }
    }

    /** Cast library reference */
    record CastLibRef(CastLibId castLibNumber) implements Datum {
        public static CastLibRef of(int castLibNumber) { return new CastLibRef(new CastLibId(castLibNumber)); }
        public int castLibNum() { return castLibNumber.value(); }
        @Override
        public String toString() { return "castLib(" + castLibNumber.value() + ")"; }
    }

    /** Stage reference (the stage window) */
    record StageRef() implements Datum {
        @Override
        public String toString() { return "(the stage)"; }
    }

    /** Movie reference (_movie) */
    record MovieRef() implements Datum {
        @Override
        public String toString() { return "(the movie)"; }
    }

    /** Player reference (_player) */
    record PlayerRef() implements Datum {
        @Override
        public String toString() { return "(the player)"; }
    }

    /** Script reference (returned by script() function) */
    record ScriptRef(CastLibId castLib, MemberId member) implements Datum {
        /** Safe factory: returns VOID for invalid castLib/member (0 means "no script" in Director). */
        public static Datum of(int castLib, int member) {
            if (castLib < 1 || member < 1) return VOID;
            return new ScriptRef(new CastLibId(castLib), new MemberId(member));
        }
        public int castLibNum() { return castLib.value(); }
        public int memberNum() { return member.value(); }
        @Override
        public String toString() { return "<script " + member.value() + ", " + castLib.value() + ">"; }
    }

    /** Sound channel reference (returned by sound() builtin) */
    record SoundChannel(int channelNum) implements Datum {
        @Override
        public String toString() { return "<sound channel " + channelNum + ">"; }
    }

    /** Timeout reference (returned by timeout() builtin) */
    record TimeoutRef(String name) implements Datum {
        @Override
        public String toString() { return "timeout(\"" + name + "\")"; }
    }

    /** Mutable reference to a VM variable (for chunk mutation operations like delete) */
    record VarRef(VarType varType, int rawIndex) implements Datum {
        @Override
        public String toString() { return "<varref:" + varType + "," + rawIndex + ">"; }
    }

    /** Reference to a chunk range within a variable (used by delete/put chunk operations) */
    record ChunkRef(VarType varType, int rawIndex, String chunkType, int start, int end) implements Datum {
        @Override
        public String toString() { return "<chunkref:" + chunkType + "[" + start + ".." + end + "]>"; }
    }

    /** Argument list for function calls (expects return value).
     *  No defensive copy — popArgs() already creates a fresh ArrayList. */
    record ArgList(java.util.List<Datum> items) implements Datum {
        public int count() { return items.size(); }
        @Override
        public String toString() { return "<arglist:" + items.size() + ">"; }
    }

    /** Argument list for function calls (no return value expected).
     *  No defensive copy — popArgs() already creates a fresh ArrayList. */
    record ArgListNoRet(java.util.List<Datum> items) implements Datum {
        public int count() { return items.size(); }
        @Override
        public String toString() { return "<arglist-noret:" + items.size() + ">"; }
    }

    // Singleton instances for common values
    Datum VOID = new Void();
    Datum ZERO = new Int(0);
    Datum ONE = new Int(1);
    Datum TRUE = new Int(1);
    Datum FALSE = new Int(0);
    Datum EMPTY_STRING = new Str("");
    Datum STAGE = new StageRef();
    Datum MOVIE = new MovieRef();
    Datum PLAYER = new PlayerRef();

    // Common property key constants
    String PROP_ANCESTOR = "ancestor";
    String PROP_SCRIPT_REF = "__scriptRef__";

    // Factory methods
    static Datum of(int value) {
        return switch (value) {
            case 0 -> ZERO;
            case 1 -> ONE;
            default -> new Int(value);
        };
    }

    static Datum of(double value) {
        return new Float(value);
    }

    static Datum of(String value) {
        if (value == null || value.isEmpty()) return EMPTY_STRING;
        return new Str(value);
    }

    static Datum symbol(String name) {
        return new Symbol(name);
    }

    static Datum list(Datum... items) {
        return new List(java.util.List.of(items));
    }

    static Datum list(java.util.List<Datum> items) {
        return new List(items);
    }

    static Datum propList() {
        return new PropList();
    }

    static Datum propList(Map<String, Datum> props) {
        PropList pl = new PropList();
        for (Map.Entry<String, Datum> e : props.entrySet()) {
            pl.add(e.getKey(), e.getValue());
        }
        return pl;
    }

    static Datum propList(java.util.List<PropEntry> entries) {
        return new PropList(entries);
    }

    // Type checking
    default boolean isVoid() { return this instanceof Void; }
    default boolean isInt() { return this instanceof Int; }
    default boolean isFloat() { return this instanceof Float; }
    default boolean isNumber() { return isInt() || isFloat(); }
    default boolean isString() { return this instanceof Str; }
    default boolean isSymbol() { return this instanceof Symbol; }
    default boolean isList() { return this instanceof List; }
    default boolean isPropList() { return this instanceof PropList; }
    default boolean isImage() { return this instanceof ImageRef; }

    default String typeName() {
        return switch (this) {
            case Void v -> "void";
            case Int i -> "int";
            case Float f -> "float";
            case Str s -> "string";
            case Symbol sym -> "symbol";
            case List l -> "list";
            case PropList pl -> "propList";
            case SpriteRef sr -> "sprite";
            case CastMemberRef cm -> "member";
            case ScriptInstance si -> "script";
            case Point p -> "point";
            case Rect r -> "rect";
            case Color c -> "color";
            case PaletteIndexColor pic -> "color";
            case ImageRef ir -> "image";
            case XtraRef xr -> "xtra";
            case XtraInstance xi -> "xtraInstance";
            case CastLibRef cl -> "castLib";
            case StageRef st -> "stage";
            case MovieRef m -> "movie";
            case PlayerRef p -> "player";
            case ScriptRef sr -> "script";
            case TimeoutRef tr -> "timeout";
            default -> getClass().getSimpleName().toLowerCase();
        };
    }

    /**
     * Director-style equality comparison.
     * Handles cross-type comparisons: symbol/string (case-insensitive),
     * number/void (VOID == 0 is true), numeric coercion (int == float).
     */
    default boolean lingoEquals(Datum other) {
        if (this == other) return true;
        // VOID and number comparisons: VOID == 0 is TRUE
        if ((this.isVoid() && other.isNumber()) || (this.isNumber() && other.isVoid())) {
            return this.toDouble() == other.toDouble();
        }
        if (this.isNumber() && other.isNumber()) {
            return this.toDouble() == other.toDouble();
        }
        // String/symbol cross-type comparison (case-insensitive)
        if ((this.isString() || this.isSymbol()) && (other.isString() || other.isSymbol())) {
            return this.toStr().equalsIgnoreCase(other.toStr());
        }
        return this.equals(other);
    }

    default boolean isTruthy() {
        return switch (this) {
            case Void v -> false;
            case Int i -> i.value() != 0;
            case Float f -> f.value() != 0.0;
            case Str s -> !s.value().isEmpty();
            default -> true;
        };
    }

    // Type coercion
    default int toInt() {
        return switch (this) {
            case Int i -> i.value();
            case Float f -> (int) f.value();
            case Str s -> {
                try {
                    yield Integer.parseInt(s.value().trim());
                } catch (NumberFormatException e) {
                    yield 0;
                }
            }
            case Color c -> (c.r() << 16) | (c.g() << 8) | c.b();
            case PaletteIndexColor pic -> {
                // Resolve through active palette for toInt() conversion
                Palette pal = getActivePalette();
                yield pal.getColor(pic.index()) & 0xFFFFFF;
            }
            default -> 0;
        };
    }

    default double toDouble() {
        return switch (this) {
            case Int i -> i.value();
            case Float f -> f.value();
            case Str s -> {
                try {
                    yield Double.parseDouble(s.value().trim());
                } catch (NumberFormatException e) {
                    yield 0.0;
                }
            }
            default -> 0.0;
        };
    }

    default String toStr() {
        return switch (this) {
            case Void v -> "";
            case Int i -> String.valueOf(i.value());
            case Float f -> String.valueOf(f.value());
            case Str s -> s.value();
            case Symbol s -> s.name();
            default -> toString();
        };
    }

    /**
     * Convert this Datum to a property-key name string.
     * Symbols yield their name directly; all other types use toStr().
     */
    default String toKeyName() {
        return this instanceof Symbol s ? s.name() : toStr();
    }

    /**
     * Convert a Datum color to ARGB int.
     * Handles: Color(r,g,b), packed RGB int, grayscale ramp index.
     * In Director, raw integer color values 0-255 use a grayscale ramp (0=white, 255=black).
     * Palette index colors are created via paletteIndex() which produces a Color datum.
     */
    static int datumToArgb(Datum colorDatum) {
        if (colorDatum instanceof Color c) {
            return 0xFF000000 | (c.r() << 16) | (c.g() << 8) | c.b();
        } else if (colorDatum instanceof PaletteIndexColor pic) {
            // Resolve through active palette (no target bitmap context available here)
            Palette pal = getActivePalette();
            int rgb = pal.getColor(pic.index());
            return 0xFF000000 | (rgb & 0xFFFFFF);
        } else if (colorDatum instanceof Int i) {
            int val = i.value();
            if (val > 255) {
                return 0xFF000000 | (val & 0xFFFFFF);
            } else {
                // Director grayscale ramp: 0 = white, 255 = black
                int gray = 255 - val;
                return 0xFF000000 | (gray << 16) | (gray << 8) | gray;
            }
        }
        return 0xFF000000;
    }

    /**
     * Convert a Datum color to ARGB, resolving PaletteIndexColor through the given bitmap's palette.
     * This is the preferred method when a target bitmap is available (e.g., in fill(), copyPixels()).
     */
    static int datumToArgb(Datum colorDatum, Bitmap targetBitmap) {
        if (colorDatum instanceof PaletteIndexColor pic && targetBitmap != null) {
            int rgb = targetBitmap.resolvePaletteIndex(pic.index(), getActivePalette());
            return 0xFF000000 | (rgb & 0xFFFFFF);
        }
        return datumToArgb(colorDatum);
    }

    /**
     * Create a deep copy of this Datum.
     * For immutable types (Int, Float, Str, Symbol, etc.), returns the same instance.
     * For mutable types (List, PropList, ScriptInstance, ArgList, ArgListNoRet),
     * creates a new instance with deep-copied contents.
     */
    default Datum deepCopy() {
        return switch (this) {
            // Immutable types - return same instance
            case Void v -> v;
            case Int i -> i;
            case Float f -> f;
            case Str s -> s;
            case Symbol sym -> sym;
            case Point p -> new Point(p.x(), p.y());
            case Rect r -> new Rect(r.left(), r.top(), r.right(), r.bottom());
            case Color c -> c;
            case PaletteIndexColor pic -> pic;
            case ImageRef ir -> ir.isLive() ? ir : new ImageRef(ir.bitmap().copy());
            case SpriteRef sr -> sr;
            case CastMemberRef cm -> cm;
            case CastLibRef cl -> cl;
            case StageRef st -> st;
            case MovieRef m -> m;
            case PlayerRef p -> p;
            case ScriptRef sr -> sr;
            case XtraRef xr -> xr;
            case XtraInstance xi -> xi;
            case TimeoutRef tr -> tr;
            case SoundChannel sc -> sc;
            case VarRef vr -> vr;
            case ChunkRef cr -> cr;

            // Mutable types - deep copy
            case List list -> {
                java.util.List<Datum> copiedItems = new ArrayList<>(list.items().size());
                for (Datum item : list.items()) {
                    copiedItems.add(item.deepCopy());
                }
                yield new List(copiedItems);
            }
            case PropList pl -> {
                java.util.List<PropEntry> copiedEntries = new ArrayList<>(pl.entries().size());
                for (PropEntry entry : pl.entries()) {
                    copiedEntries.add(new PropEntry(entry.key(), entry.value().deepCopy()));
                }
                yield new PropList(copiedEntries);
            }
            // ScriptInstance: return same instance (Director's duplicate() is shallow for objects)
            case ScriptInstance si -> si;
            case ArgList al -> {
                java.util.List<Datum> copiedItems = new ArrayList<>(al.items().size());
                for (Datum item : al.items()) {
                    copiedItems.add(item.deepCopy());
                }
                yield new ArgList(copiedItems);
            }
            case ArgListNoRet al -> {
                java.util.List<Datum> copiedItems = new ArrayList<>(al.items().size());
                for (Datum item : al.items()) {
                    copiedItems.add(item.deepCopy());
                }
                yield new ArgListNoRet(copiedItems);
            }
        };
    }
}
