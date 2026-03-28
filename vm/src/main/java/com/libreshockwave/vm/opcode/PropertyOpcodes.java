package com.libreshockwave.vm.opcode;

import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.lingo.StringChunkType;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider;
import com.libreshockwave.vm.builtin.timeout.TimeoutBuiltins;
import com.libreshockwave.vm.builtin.data.TypeBuiltins;
import com.libreshockwave.vm.builtin.xtra.XtraBuiltins;
import com.libreshockwave.vm.opcode.dispatch.ImageMethodDispatcher;
import com.libreshockwave.vm.opcode.dispatch.SoundChannelMethodDispatcher;
import com.libreshockwave.vm.util.AncestorChainWalker;
import com.libreshockwave.vm.util.StringChunkUtils;

import java.util.Map;

/**
 * Property access opcodes.
 */
public final class PropertyOpcodes {

    private PropertyOpcodes() {}

    public static void register(Map<Opcode, OpcodeHandler> handlers) {
        handlers.put(Opcode.GET_PROP, PropertyOpcodes::getProp);
        handlers.put(Opcode.SET_PROP, PropertyOpcodes::setProp);
        handlers.put(Opcode.GET_MOVIE_PROP, PropertyOpcodes::getMovieProp);
        handlers.put(Opcode.SET_MOVIE_PROP, PropertyOpcodes::setMovieProp);
        handlers.put(Opcode.GET_OBJ_PROP, PropertyOpcodes::getObjProp);
        handlers.put(Opcode.SET_OBJ_PROP, PropertyOpcodes::setObjProp);
        handlers.put(Opcode.THE_BUILTIN, PropertyOpcodes::theBuiltin);
        handlers.put(Opcode.GET, PropertyOpcodes::get);
        handlers.put(Opcode.SET, PropertyOpcodes::set);
        handlers.put(Opcode.GET_FIELD, PropertyOpcodes::getField);
        handlers.put(Opcode.GET_CHAINED_PROP, PropertyOpcodes::getChainedProp);
        handlers.put(Opcode.GET_TOP_LEVEL_PROP, PropertyOpcodes::getTopLevelProp);
    }

    private static boolean getProp(ExecutionContext ctx) {
        String propName = ctx.resolveName(ctx.getArgument());
        if (ctx.getReceiver() instanceof Datum.ScriptInstance si) {
            // Walk the ancestor chain to find the property
            Datum value = AncestorChainWalker.getProperty(si, propName);
            ctx.push(value);
        } else {
            ctx.push(Datum.VOID);
        }
        return true;
    }

    private static boolean setProp(ExecutionContext ctx) {
        String propName = ctx.resolveName(ctx.getArgument());
        Datum value = ctx.pop();
        if (ctx.getReceiver() instanceof Datum.ScriptInstance si) {
            AncestorChainWalker.setProperty(si, propName, value);
            ctx.tracePropertySet(propName, value);
        }
        return true;
    }

    private static boolean getMovieProp(ExecutionContext ctx) {
        String propName = ctx.resolveName(ctx.getArgument());
        MoviePropertyProvider provider = MoviePropertyProvider.getProvider();

        if (provider != null) {
            Datum value = provider.getMovieProp(propName);
            ctx.push(value);
        } else {
            // Fallback for common constants when no provider is available
            ctx.push(getBuiltinConstant(propName));
        }
        return true;
    }

    private static boolean setMovieProp(ExecutionContext ctx) {
        String propName = ctx.resolveName(ctx.getArgument());
        Datum value = ctx.pop();
        MoviePropertyProvider provider = MoviePropertyProvider.getProvider();

        if (provider != null) {
            provider.setMovieProp(propName, value);
        }
        return true;
    }

    /**
     * Get built-in constants that don't require a provider.
     */
    static Datum getBuiltinConstant(String propName) {
        if ("pi".equalsIgnoreCase(propName)) return Datum.of(Math.PI);
        if ("true".equalsIgnoreCase(propName)) return Datum.TRUE;
        if ("false".equalsIgnoreCase(propName)) return Datum.FALSE;
        if ("void".equalsIgnoreCase(propName)) return Datum.VOID;
        if ("empty".equalsIgnoreCase(propName) || "emptystring".equalsIgnoreCase(propName)) return Datum.EMPTY_STRING;
        if ("return".equalsIgnoreCase(propName)) return Datum.of("\r");
        if ("enter".equalsIgnoreCase(propName)) return Datum.of("\n");
        if ("tab".equalsIgnoreCase(propName)) return Datum.of("\t");
        if ("quote".equalsIgnoreCase(propName)) return Datum.of("\"");
        if ("backspace".equalsIgnoreCase(propName)) return Datum.of("\b");
        if ("space".equalsIgnoreCase(propName)) return Datum.of(" ");
        return Datum.VOID;
    }

    private static boolean getObjProp(ExecutionContext ctx) {
        String propName = ctx.resolveName(ctx.getArgument());
        Datum obj = ctx.pop();

        Datum result = switch (obj) {
            case Datum.CastLibRef clr -> getCastLibProp(clr, propName);
            case Datum.CastMemberRef cmr -> getCastMemberProp(cmr, propName);
            case Datum.ScriptInstance si -> AncestorChainWalker.getProperty(si, propName);
            case Datum.XtraInstance xi -> XtraBuiltins.getProperty(xi, propName);
            case Datum.TimeoutRef tr -> TimeoutBuiltins.getProperty(tr, propName);
            case Datum.PropList pl -> getPropListProp(pl, propName);
            case Datum.List list -> getListProp(list, propName);
            case Datum.Str str -> getStringProp(str, propName);
            case Datum.MovieRef m -> {
                MoviePropertyProvider provider = MoviePropertyProvider.getProvider();
                yield provider != null ? provider.getMovieProp(propName) : Datum.VOID;
            }
            case Datum.PlayerRef p -> getBuiltinConstant(propName);
            case Datum.SpriteRef sr -> {
                SpritePropertyProvider spriteProvider = SpritePropertyProvider.getProvider();
                yield spriteProvider != null ? spriteProvider.getSpriteProp(sr.channelNum(), propName) : Datum.VOID;
            }
            case Datum.SoundChannel sc -> SoundChannelMethodDispatcher.getProperty(sc, propName);
            case Datum.Int intVal -> {
                // Director: property access on integers routes to sprite properties.
                // e.g., 42.bgColor == sprite(42).bgColor. Window system stores pSprite
                // as an integer channel number, then does pSprite.bgColor = tColor.
                SpritePropertyProvider spriteProvider = SpritePropertyProvider.getProvider();
                if (spriteProvider != null && !"ilk".equalsIgnoreCase(propName)) {
                    yield spriteProvider.getSpriteProp(intVal.value(), propName);
                }
                if ("ilk".equalsIgnoreCase(propName)) {
                    yield Datum.symbol(TypeBuiltins.getIlkType(obj));
                }
                yield Datum.VOID;
            }
            case Datum.StageRef s -> {
                MoviePropertyProvider stageProvider = MoviePropertyProvider.getProvider();
                yield stageProvider != null ? stageProvider.getStageProp(propName) : Datum.VOID;
            }
            case Datum.ImageRef ir -> ImageMethodDispatcher.getProperty(ir, propName);
            case Datum.Point point -> getPointProp(point, propName);
            case Datum.Rect rect -> getRectProp(rect, propName);
            case Datum.Color color -> {
                if ("red".equalsIgnoreCase(propName)) yield Datum.of(color.r());
                if ("green".equalsIgnoreCase(propName)) yield Datum.of(color.g());
                if ("blue".equalsIgnoreCase(propName)) yield Datum.of(color.b());
                if ("ilk".equalsIgnoreCase(propName)) yield Datum.symbol("color");
                yield Datum.VOID;
            }
            case Datum d when d.isVoid() -> Datum.VOID;
            default -> {
                if ("ilk".equalsIgnoreCase(propName)) {
                    yield Datum.symbol(TypeBuiltins.getIlkType(obj));
                }
                System.err.println("[LingoVM] Missing get accessor: " + propName + " on " + obj.getClass().getSimpleName());
                System.err.println(ctx.formatCallStack());
                yield Datum.VOID;
            }
        };

        ctx.push(result);
        return true;
    }

    private static Datum getPointProp(Datum.Point point, String propName) {
        if ("loch".equalsIgnoreCase(propName) || "x".equalsIgnoreCase(propName)) return Datum.of(point.x());
        if ("locv".equalsIgnoreCase(propName) || "y".equalsIgnoreCase(propName)) return Datum.of(point.y());
        return Datum.VOID;
    }

    private static Datum getRectProp(Datum.Rect rect, String propName) {
        if ("left".equalsIgnoreCase(propName)) return Datum.of(rect.left());
        if ("top".equalsIgnoreCase(propName)) return Datum.of(rect.top());
        if ("right".equalsIgnoreCase(propName)) return Datum.of(rect.right());
        if ("bottom".equalsIgnoreCase(propName)) return Datum.of(rect.bottom());
        if ("width".equalsIgnoreCase(propName)) return Datum.of(rect.right() - rect.left());
        if ("height".equalsIgnoreCase(propName)) return Datum.of(rect.bottom() - rect.top());
        return Datum.VOID;
    }

    private static Datum getListProp(Datum.List list, String propName) {
        if ("count".equalsIgnoreCase(propName) || "length".equalsIgnoreCase(propName)) return Datum.of(list.items().size());
        if ("ilk".equalsIgnoreCase(propName)) return Datum.symbol("list");
        try {
            int index = Integer.parseInt(propName) - 1;
            if (index >= 0 && index < list.items().size()) return list.items().get(index);
        } catch (NumberFormatException e) { /* not a number */ }
        return Datum.VOID;
    }

    private static Datum getPropListProp(Datum.PropList pl, String propName) {
        if ("count".equalsIgnoreCase(propName) || "length".equalsIgnoreCase(propName)) return Datum.of(pl.size());
        // Check PropList's own keys first (e.g. [#ilk:#struct] in font structs),
        // then fall back to built-in ilk property
        Datum value = pl.get(propName);
        if (value != null) return value;
        if ("ilk".equalsIgnoreCase(propName)) return Datum.symbol("propList");
        return Datum.VOID;
    }

    private static Datum getStringProp(Datum.Str str, String propName) {
        if ("length".equalsIgnoreCase(propName)) return Datum.of(str.value().length());
        if ("ilk".equalsIgnoreCase(propName)) return Datum.symbol("string");
        return Datum.VOID;
    }

    private static boolean setObjProp(ExecutionContext ctx) {
        String propName = ctx.resolveName(ctx.getArgument());
        Datum value = ctx.pop();
        Datum obj = ctx.pop();

        switch (obj) {
            case Datum.CastLibRef clr -> setCastLibProp(clr, propName, value);
            case Datum.CastMemberRef cmr -> setCastMemberProp(cmr, propName, value);
            case Datum.ScriptInstance si -> {
                AncestorChainWalker.setProperty(si, propName, value);
                ctx.tracePropertySet(propName, value);
            }
            case Datum.XtraInstance xi -> XtraBuiltins.setProperty(xi, propName, value);
            case Datum.TimeoutRef tr -> TimeoutBuiltins.setProperty(tr, propName, value);
            case Datum.PropList pl -> pl.put(propName, true, value);
            case Datum.MovieRef m -> {
                MoviePropertyProvider provider = MoviePropertyProvider.getProvider();
                if (provider != null) {
                    provider.setMovieProp(propName, value);
                }
            }
            case Datum.SpriteRef sr -> {
                SpritePropertyProvider spriteProvider = SpritePropertyProvider.getProvider();
                if (spriteProvider != null) {
                    spriteProvider.setSpriteProp(sr.channelNum(), propName, value);
                }
            }
            case Datum.SoundChannel sc -> SoundChannelMethodDispatcher.setProperty(sc, propName, value);
            case Datum.Int intVal -> {
                // Director: property assignment on integers routes to sprite properties.
                // e.g., 42.bgColor = color == sprite(42).bgColor = color.
                SpritePropertyProvider spriteProvider = SpritePropertyProvider.getProvider();
                if (spriteProvider != null) {
                    spriteProvider.setSpriteProp(intVal.value(), propName, value);
                }
            }
            case Datum.StageRef s -> {
                MoviePropertyProvider stageProvider = MoviePropertyProvider.getProvider();
                if (stageProvider != null) {
                    stageProvider.setStageProp(propName, value);
                }
            }
            case Datum.Rect rect -> {
                int v = value.toInt();
                if ("left".equalsIgnoreCase(propName)) rect.setLeft(v);
                else if ("top".equalsIgnoreCase(propName)) rect.setTop(v);
                else if ("right".equalsIgnoreCase(propName)) rect.setRight(v);
                else if ("bottom".equalsIgnoreCase(propName)) rect.setBottom(v);
            }
            case Datum.Point point -> {
                int v = value.toInt();
                if ("loch".equalsIgnoreCase(propName) || "x".equalsIgnoreCase(propName)) point.setX(v);
                else if ("locv".equalsIgnoreCase(propName) || "y".equalsIgnoreCase(propName)) point.setY(v);
            }
            case Datum.ImageRef ir -> ImageMethodDispatcher.setProperty(ir, propName, value);
            default -> {
                System.err.println("[LingoVM] Missing set accessor: " + propName + " on " + obj.getClass().getSimpleName());
                System.err.println(ctx.formatCallStack());
            }
        }

        return true;
    }

    /**
     * Get a property from a cast library reference.
     */
    private static Datum getCastLibProp(Datum.CastLibRef clr, String propName) {
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            return Datum.VOID;
        }
        return provider.getCastLibProp(clr.castLibNum(), propName);
    }

    /**
     * Set a property on a cast library reference.
     */
    private static boolean setCastLibProp(Datum.CastLibRef clr, String propName, Datum value) {
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            return false;
        }
        return provider.setCastLibProp(clr.castLibNum(), propName, value);
    }

    /**
     * Get a property from a cast member reference.
     */
    private static Datum getCastMemberProp(Datum.CastMemberRef cmr, String propName) {
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            if ("number".equalsIgnoreCase(propName)) return Datum.of((cmr.castLibNum() << 16) | (cmr.memberNum() & 0xFFFF));
            if ("membernum".equalsIgnoreCase(propName)) return Datum.of(cmr.memberNum());
            if ("castlibnum".equalsIgnoreCase(propName)) return Datum.of(cmr.castLibNum());
            if ("castlib".equalsIgnoreCase(propName)) return new Datum.CastLibRef(cmr.castLib());
            return Datum.VOID;
        }

        // Delegate to provider for full property access with lazy loading
        return provider.getMemberProp(cmr.castLibNum(), cmr.memberNum(), propName);
    }

    /**
     * Set a property on a cast member reference.
     */
    private static boolean setCastMemberProp(Datum.CastMemberRef cmr, String propName, Datum value) {
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            return false;
        }

        return provider.setMemberProp(cmr.castLibNum(), cmr.memberNum(), propName, value);
    }

    private static boolean theBuiltin(ExecutionContext ctx) {
        // THE_BUILTIN is used for "the" expressions that take an argument
        // e.g., "the paramCount", "the name of member 1"
        // An arglist is pushed before this opcode and must be popped
        Datum argListDatum = ctx.pop();

        String propName = ctx.resolveName(ctx.getArgument());

        if ("paramcount".equalsIgnoreCase(propName)) {
            ctx.push(Datum.of(ctx.getScope().getArguments().size()));
            return true;
        }
        if ("result".equalsIgnoreCase(propName)) {
            ctx.push(ctx.getScope().getReturnValue());
            return true;
        }

        // First try movie properties
        MoviePropertyProvider provider = MoviePropertyProvider.getProvider();
        if (provider != null) {
            Datum value = provider.getMovieProp(propName);
            if (!value.isVoid()) {
                ctx.push(value);
                return true;
            }
        }

        // Fall back to built-in constants
        ctx.push(getBuiltinConstant(propName));
        return true;
    }

    /**
     * GET opcode (0x5C) - Get a property by ID.
     * Stack: [..., propertyId] -> [..., value]
     * For some property types, also pops a target (sprite number, etc.)
     */
    private static boolean get(ExecutionContext ctx) {
        int propertyId = ctx.pop().toInt();
        int propertyType = ctx.getArgument();

        MoviePropertyProvider movieProvider = MoviePropertyProvider.getProvider();
        SpritePropertyProvider spriteProvider = SpritePropertyProvider.getProvider();

        Datum result = switch (propertyType) {
            case 0x00 -> {
                // Movie property or last chunk
                if (propertyId <= 0x0b) {
                    String propName = PropertyIdMappings.getMoviePropName(propertyId);
                    if (propName != null && movieProvider != null) {
                        yield movieProvider.getMovieProp(propName);
                    }
                    yield Datum.VOID;
                } else {
                    // Last chunk: propertyId 0x0c=item, 0x0d=word, 0x0e=char, 0x0f=line
                    String str = ctx.pop().toStr();
                    int chunkTypeCode = propertyId - 0x0b;
                    try {
                        StringChunkType chunkType = StringChunkType.fromCode(chunkTypeCode);
                        char delimiter = MoviePropertyProvider.ItemDelimiterCache._char;
                        String lastChunk = StringChunkUtils.getLastChunk(str, chunkType, delimiter);
                        yield Datum.of(lastChunk);
                    } catch (IllegalArgumentException e) {
                        yield Datum.VOID;
                    }
                }
            }
            case 0x01 -> {
                // Number of chunks: propertyId 0x01=item, 0x02=word, 0x03=char, 0x04=line
                String str = ctx.pop().toStr();
                try {
                    StringChunkType chunkType = StringChunkType.fromCode(propertyId);
                    char delimiter = MoviePropertyProvider.ItemDelimiterCache._char;
                    int count = StringChunkUtils.countChunks(str, chunkType, delimiter);
                    yield Datum.of(count);
                } catch (IllegalArgumentException e) {
                    yield Datum.VOID;
                }
            }
            case 0x06 -> {
                // Sprite property
                String propName = PropertyIdMappings.getSpritePropName(propertyId);
                int spriteNum = ctx.pop().toInt();
                if (propName != null && spriteProvider != null) {
                    yield spriteProvider.getSpriteProp(spriteNum, propName);
                }
                yield Datum.VOID;
            }
            case 0x07 -> {
                // Animation property
                String propName = PropertyIdMappings.getAnimPropName(propertyId);
                if (propName != null && movieProvider != null) {
                    yield movieProvider.getMovieProp(propName);
                }
                yield Datum.VOID;
            }
            case 0x08 -> {
                // Anim2 property
                if (propertyId == 0x02) {
                    // "number of castMembers of castLib N" - pops cast lib number from stack
                    int castLibNum = ctx.pop().toInt();
                    CastLibProvider castProvider = CastLibProvider.getProvider();
                    if (castProvider != null) {
                        int count = castProvider.getMemberCount(castLibNum);
                        yield Datum.of(count);
                    }
                    yield Datum.ZERO;
                }
                String propName = PropertyIdMappings.getAnim2PropName(propertyId);
                if (propName != null && movieProvider != null) {
                    yield movieProvider.getMovieProp(propName);
                }
                yield Datum.VOID;
            }
            case 0x09 -> {
                // Alias to 0x07 (animation properties)
                String propName = PropertyIdMappings.getAnimPropName(propertyId);
                if (propName != null && movieProvider != null) {
                    yield movieProvider.getMovieProp(propName);
                }
                yield Datum.VOID;
            }
            case 0x0b -> {
                // Sound channel property
                String propName = PropertyIdMappings.getSoundPropName(propertyId);
                int channelNum = ctx.pop().toInt();
                if (propName != null && channelNum >= 1 && channelNum <= 8) {
                    yield SoundChannelMethodDispatcher.getProperty(
                            new Datum.SoundChannel(channelNum), propName);
                }
                yield Datum.VOID;
            }
            default -> Datum.VOID;
        };

        ctx.push(result);
        return true;
    }

    /**
     * GET_FIELD opcode (0x1B) - Get the text content of a field.
     * Stack: [..., fieldNameOrNum, castId?] -> [..., fieldText]
     * For Director 5+, pops both castId and fieldNameOrNum.
     * For earlier versions, just pops fieldNameOrNum.
     */
    private static boolean getField(ExecutionContext ctx) {
        // Pop the cast ID first (for D5+), then the field identifier
        // Note: The order depends on how the bytecode was compiled
        Datum castIdDatum = ctx.pop();
        Datum fieldNameOrNum = ctx.pop();

        // Determine cast ID (0 means search all casts)
        int castId = castIdDatum.toInt();

        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            ctx.push(Datum.EMPTY_STRING);
            return true;
        }

        // Get the field value
        Object identifier = fieldNameOrNum instanceof Datum.Str s ? s.value()
                : fieldNameOrNum instanceof Datum.Int i ? i.value()
                : fieldNameOrNum.toStr();

        String fieldValue = provider.getFieldValue(identifier, castId);
        ctx.push(Datum.of(fieldValue));
        return true;
    }

    /**
     * SET opcode (0x5D) - Set a property by ID.
     * Stack: [..., value, propertyId] -> [...]
     * For some property types, also pops a target (sprite number, etc.)
     */
    private static boolean set(ExecutionContext ctx) {
        int propertyId = ctx.pop().toInt();
        Datum value = ctx.pop();
        int propertyType = ctx.getArgument();

        MoviePropertyProvider movieProvider = MoviePropertyProvider.getProvider();
        SpritePropertyProvider spriteProvider = SpritePropertyProvider.getProvider();

        switch (propertyType) {
            case 0x00 -> {
                // Movie property
                if (propertyId <= 0x0b) {
                    String propName = PropertyIdMappings.getMoviePropName(propertyId);
                    if (propName != null && movieProvider != null) {
                        movieProvider.setMovieProp(propName, value);
                    }
                }
            }
            case 0x04 -> {
                // Sound channel property
                String propName = PropertyIdMappings.getSoundPropName(propertyId);
                int channelNum = ctx.pop().toInt();
                if (propName != null && channelNum >= 1 && channelNum <= 8) {
                    SoundChannelMethodDispatcher.setProperty(
                            new Datum.SoundChannel(channelNum), propName, value);
                }
            }
            case 0x06 -> {
                // Sprite property
                String propName = PropertyIdMappings.getSpritePropName(propertyId);
                int spriteNum = ctx.pop().toInt();
                if (propName != null && spriteProvider != null) {
                    spriteProvider.setSpriteProp(spriteNum, propName, value);
                }
            }
            case 0x07 -> {
                // Animation property
                String propName = PropertyIdMappings.getAnimPropName(propertyId);
                if (propName != null && movieProvider != null) {
                    movieProvider.setMovieProp(propName, value);
                }
            }
        }

        return true;
    }

    /**
     * GET_CHAINED_PROP (0x70) - Get a property from a chained object.
     * Used for expressions like obj.prop, list[1], str.length.
     * Stack: [..., obj] -> [..., value]
     */
    private static boolean getChainedProp(ExecutionContext ctx) {
        String propName = ctx.resolveName(ctx.getArgument());
        Datum obj = ctx.pop();

        // Check if propName is a numeric index
        boolean isNumericIndex = false;
        int numericIndex = 0;
        try {
            numericIndex = Integer.parseInt(propName);
            isNumericIndex = true;
        } catch (NumberFormatException e) {
            // Not numeric
        }

        Datum result = switch (obj) {
            case Datum.ScriptInstance si -> {
                if (isNumericIndex) {
                    yield Datum.VOID;
                }
                yield AncestorChainWalker.getProperty(si, propName);
            }
            case Datum.List list -> {
                if (isNumericIndex) {
                    // Lingo uses 1-based indexing
                    int zeroIndex = numericIndex - 1;
                    if (zeroIndex >= 0 && zeroIndex < list.items().size()) {
                        yield list.items().get(zeroIndex);
                    }
                    yield Datum.VOID;
                }
                yield getChainedObjProp(obj, propName);
            }
            case Datum.PropList pl -> getChainedObjProp(obj, propName);
            case Datum.Str str -> {
                if ("length".equals(propName)) {
                    yield Datum.of(str.value().length());
                }
                yield getChainedObjProp(obj, propName);
            }
            case Datum.SpriteRef sr -> {
                SpritePropertyProvider spriteProvider = SpritePropertyProvider.getProvider();
                if (spriteProvider != null) {
                    yield spriteProvider.getSpriteProp(sr.channelNum(), propName);
                }
                yield Datum.VOID;
            }
            case Datum.SoundChannel sc -> SoundChannelMethodDispatcher.getProperty(sc, propName);
            case Datum.CastMemberRef cmr -> getCastMemberProp(cmr, propName);
            case Datum.CastLibRef clr -> getCastLibProp(clr, propName);
            case Datum.MovieRef m -> {
                MoviePropertyProvider provider = MoviePropertyProvider.getProvider();
                if (provider != null) {
                    yield provider.getMovieProp(propName);
                }
                yield Datum.VOID;
            }
            case Datum.PlayerRef p -> {
                // Player properties - limited set
                yield getBuiltinConstant(propName);
            }
            case Datum.Point point -> getPointProp(point, propName);
            case Datum.Rect rect -> getRectProp(rect, propName);
            case Datum.Color color -> {
                if ("red".equalsIgnoreCase(propName)) yield Datum.of(color.r());
                if ("green".equalsIgnoreCase(propName)) yield Datum.of(color.g());
                if ("blue".equalsIgnoreCase(propName)) yield Datum.of(color.b());
                yield Datum.VOID;
            }
            case Datum.XtraInstance xi -> XtraBuiltins.getProperty(xi, propName);
            case Datum.TimeoutRef tr -> TimeoutBuiltins.getProperty(tr, propName);
            case Datum.StageRef s -> {
                MoviePropertyProvider stageProvider = MoviePropertyProvider.getProvider();
                yield stageProvider != null ? stageProvider.getStageProp(propName) : Datum.VOID;
            }
            case Datum.ImageRef ir -> ImageMethodDispatcher.getProperty(ir, propName);
            case Datum.Int intVal -> {
                // Director: chained property access on integers routes to sprite properties.
                // e.g., pBuffer.image where pBuffer is a sprite channel number (integer).
                // The window system stores pSprite/pBuffer as integer channel numbers.
                SpritePropertyProvider spriteProvider = SpritePropertyProvider.getProvider();
                if (spriteProvider != null && !"ilk".equalsIgnoreCase(propName)) {
                    yield spriteProvider.getSpriteProp(intVal.value(), propName);
                }
                if ("ilk".equalsIgnoreCase(propName)) {
                    yield Datum.symbol(TypeBuiltins.getIlkType(obj));
                }
                yield Datum.VOID;
            }
            default -> getChainedObjProp(obj, propName);
        };

        ctx.push(result);
        return true;
    }

    /**
     * Helper for GET_CHAINED_PROP: get property from common object types.
     */
    private static Datum getChainedObjProp(Datum obj, String propName) {
        return switch (obj) {
            case Datum.PropList pl -> {
                Datum found = pl.get(propName);
                yield found != null ? found : Datum.VOID;
            }
            case Datum.ScriptInstance si -> AncestorChainWalker.getProperty(si, propName);
            case Datum.CastMemberRef cmr -> getCastMemberProp(cmr, propName);
            case Datum.CastLibRef clr -> getCastLibProp(clr, propName);
            case Datum.StageRef s -> {
                MoviePropertyProvider stageProvider = MoviePropertyProvider.getProvider();
                yield stageProvider != null ? stageProvider.getStageProp(propName) : Datum.VOID;
            }
            case Datum.ImageRef ir -> ImageMethodDispatcher.getProperty(ir, propName);
            case Datum.SoundChannel sc -> SoundChannelMethodDispatcher.getProperty(sc, propName);
            default -> Datum.VOID;
        };
    }

    /**
     * GET_TOP_LEVEL_PROP (0x72) - Get a top-level property.
     * Used for _movie, _player access.
     * Stack: [...] -> [..., value]
     */
    private static boolean getTopLevelProp(ExecutionContext ctx) {
        String propName = ctx.resolveName(ctx.getArgument());

        Datum result = switch (propName) {
            case "_player" -> Datum.PLAYER;
            case "_movie" -> Datum.MOVIE;
            default -> {
                System.err.println("[WARN] GET_TOP_LEVEL_PROP: unknown prop: " + propName);
                yield Datum.VOID;
            }
        };

        ctx.push(result);
        return true;
    }
}
