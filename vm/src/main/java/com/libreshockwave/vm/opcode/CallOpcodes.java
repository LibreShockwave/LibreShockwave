package com.libreshockwave.vm.opcode;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.id.VarType;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.DebugConfig;
import com.libreshockwave.vm.HandlerRef;
import com.libreshockwave.vm.datum.LingoException;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.builtin.sprite.SpriteEventBrokerSupport;
import com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider;
import com.libreshockwave.vm.builtin.timeout.TimeoutBuiltins;
import com.libreshockwave.vm.builtin.xtra.XtraBuiltins;
import com.libreshockwave.vm.opcode.dispatch.ImageMethodDispatcher;
import com.libreshockwave.vm.opcode.dispatch.ListMethodDispatcher;
import com.libreshockwave.vm.opcode.dispatch.SoundChannelMethodDispatcher;
import com.libreshockwave.vm.opcode.dispatch.PropListMethodDispatcher;
import com.libreshockwave.vm.opcode.dispatch.ScriptInstanceMethodDispatcher;
import com.libreshockwave.vm.opcode.dispatch.StringMethodDispatcher;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Function call opcodes.
 */
public final class CallOpcodes {

    private CallOpcodes() {}

    /**
     * Safely execute a handler, catching exceptions and returning VOID on error.
     * This matches dirplayer-rs behavior where errors stop execution but don't propagate
     * as exceptions that could trigger recursive error handling.
     */
    static Datum safeExecuteHandler(ExecutionContext ctx, ScriptChunk script,
                                     ScriptChunk.Handler handler, List<Datum> args, Datum receiver) {
        try {
            return ctx.executeHandler(script, handler, args, receiver);
        } catch (LingoException e) {
            if (DebugConfig.isDebugPlaybackEnabled()) {
                System.err.println(e.getMessage());
                System.err.println(ctx.formatCallStack());
            }
            ctx.setErrorState(true);
            return Datum.VOID;
        }
    }

    public static void register(Map<Opcode, OpcodeHandler> handlers) {
        handlers.put(Opcode.LOCAL_CALL, CallOpcodes::localCall);
        handlers.put(Opcode.EXT_CALL, CallOpcodes::extCall);
        handlers.put(Opcode.OBJ_CALL, CallOpcodes::objCall);
    }

    private static boolean localCall(ExecutionContext ctx) {
        ScriptChunk.Handler targetHandler = ctx.findLocalHandler(ctx.getArgument());
        if (targetHandler != null) {
            Datum argListDatum = ctx.pop();
            boolean noRet = argListDatum instanceof Datum.ArgListNoRet;
            List<Datum> args = getArgs(argListDatum);
            // If the Lingo source explicitly passes 'me' as the first arg
            // (e.g., searchTask(me, arg)), the args already include the receiver.
            // Pass null as receiver to prevent executeHandler from double-prepending it.
            // The receiver for property access will be derived from args[0] in executeHandler.
            Datum receiver = ctx.getReceiver();
            if (!args.isEmpty() && args.get(0) == receiver) {
                // The bytecode pushed 'me' as the first arg. Check if the target handler
                // declares 'me' as its first parameter. If it does (e.g., "on new me"),
                // param0=me and args are correct as-is. If it doesn't (e.g.,
                // "on createRoomItemImage tNum, tColor"), the handler's param0 should be
                // the first EXPLICIT arg, so we need to strip 'me' from args and pass it
                // as the receiver (which executeHandler will NOT prepend since we also pass
                // it separately in a way that doesn't double-add).
                boolean handlerDeclaresMeAsParam = false;
                if (!targetHandler.argNameIds().isEmpty()) {
                    String firstArgName = ctx.resolveName(targetHandler.argNameIds().get(0));
                    handlerDeclaresMeAsParam = "me".equalsIgnoreCase(firstArgName);
                }
                if (handlerDeclaresMeAsParam) {
                    // Handler expects param0=me, args already correct: [me, arg1, arg2, ...]
                    receiver = null;
                } else {
                    // Handler does NOT count 'me' in params. Strip it from args so that
                    // param0 = first explicit arg. Pass 'me' as the receiver for property access.
                    receiver = args.get(0);
                    args = new java.util.ArrayList<>(args.subList(1, args.size()));
                }
            }
            Datum result = safeExecuteHandler(ctx, ctx.getScript(), targetHandler, args, receiver);
            if (!noRet) {
                ctx.push(result);
            }
        }
        return true;
    }

    private static boolean extCall(ExecutionContext ctx) {
        String handlerName = ctx.resolveName(ctx.getArgument());
        Datum argListDatum = ctx.pop();
        boolean noRet = argListDatum instanceof Datum.ArgListNoRet;
        List<Datum> args = getArgs(argListDatum);

        Datum result;
        if (ctx.isBuiltin(handlerName)) {
            result = ctx.invokeBuiltin(handlerName, args);
        } else {
            HandlerRef ref = ctx.findHandler(handlerName);
            if (ref != null) {
                result = safeExecuteHandler(ctx, ref.script(), ref.handler(), args, null);
            } else {
                // Check built-in constants (SPACE, RETURN, QUOTE, etc.)
                // Director compiles these as EXT_CALL with 0 args when used without "the"
                if (args.isEmpty()) {
                    result = PropertyOpcodes.getBuiltinConstant(handlerName);
                } else {
                    System.err.println("[LingoVM] Missing builtin/handler: " + handlerName);
                    System.err.println(ctx.formatCallStack());
                    result = Datum.VOID;
                }
            }
        }
        if (!noRet) {
            ctx.push(result);
        }
        return true;
    }

    private static boolean objCall(ExecutionContext ctx) {
        int nameIdx = ctx.getArgument();
        String methodName = ctx.resolveName(nameIdx);
        Datum argListDatum = ctx.pop();
        boolean noRet = argListDatum instanceof Datum.ArgListNoRet;
        List<Datum> args = getArgs(argListDatum);
        Datum target = args.isEmpty() ? Datum.VOID : args.remove(0);

        Datum result = dispatchMethod(ctx, target, methodName, args);

        if (!noRet) {
            ctx.push(result);
        }
        return true;
    }

    /**
     * Dispatch a method call to the appropriate handler based on target type.
     */
    private static Datum dispatchMethod(ExecutionContext ctx, Datum target,
                                        String methodName, List<Datum> args) {
        return switch (target) {
            case Datum.List list -> ListMethodDispatcher.dispatch(list, methodName, args);
            case Datum.PropList propList -> PropListMethodDispatcher.dispatch(propList, methodName, args);
            case Datum.ScriptInstance instance -> ScriptInstanceMethodDispatcher.dispatch(ctx, instance, methodName, args);
            case Datum.ScriptRef scriptRef -> handleScriptRefMethod(ctx, scriptRef, methodName, args);
            case Datum.Point point -> handlePointMethod(point, methodName, args);
            case Datum.Rect rect -> handleRectMethod(rect, methodName, args);
            case Datum.Str str -> StringMethodDispatcher.dispatch(str, methodName, args);
            case Datum.TimeoutRef ref -> TimeoutBuiltins.handleMethod(ref, methodName, args);
            case Datum.SoundChannel sc -> SoundChannelMethodDispatcher.dispatch(sc, methodName, args);
            case Datum.XtraInstance xi -> XtraBuiltins.callHandler(xi, methodName, args);
            case Datum.ImageRef imageRef -> ImageMethodDispatcher.dispatch(imageRef, methodName, args);
            case Datum.SpriteRef sr -> {
                // Method calls on sprite references dispatch to the sprite's scriptInstanceList behaviors.
                // e.g., sprite(N).setcursor(#arrow) → Event Broker Behavior's on setcursor handler
                SpritePropertyProvider spriteProvider = SpritePropertyProvider.getProvider();
                if (spriteProvider != null) {
                    Datum listDatum = spriteProvider.getSpriteProp(sr.channelNum(), "scriptinstancelist");
                    if (listDatum instanceof Datum.List scriptList) {
                        for (Datum item : scriptList.items()) {
                            if (item instanceof Datum.ScriptInstance si) {
                                Datum r = ScriptInstanceMethodDispatcher.dispatch(ctx, si, methodName, args);
                                if (!r.isVoid()) {
                                    yield r;
                                }
                            }
                        }
                    }
                    Datum brokerResult = SpriteEventBrokerSupport.dispatchSpriteMethod(
                            sr.channelNum(), methodName, args);
                    if (!brokerResult.isVoid()) {
                        yield brokerResult;
                    }
                }
                yield Datum.VOID;
            }
            case Datum.CastMemberRef cmr -> {
                // Method calls on cast member references (e.g., member.charPosToLoc)
                CastLibProvider provider = CastLibProvider.getProvider();
                if (provider != null) {
                    yield provider.callMemberMethod(cmr.castLibNum(), cmr.memberNum(), methodName, args);
                }
                yield Datum.VOID;
            }
            case Datum.VarRef varRef -> handleVarRefMethod(ctx, varRef, methodName, args);
            case Datum.ChunkRef chunkRef -> handleChunkRefMethod(ctx, chunkRef, methodName, args);
            case Datum.MovieRef m -> {
                // Method calls on _movie - try as builtin with args
                if (ctx.isBuiltin(methodName)) {
                    yield ctx.invokeBuiltin(methodName, args);
                }
                yield Datum.VOID;
            }
            default -> {
                // Try to find the method as a global handler (with target as first arg)
                if (ctx.isBuiltin(methodName)) {
                    List<Datum> fullArgs = new ArrayList<>();
                    fullArgs.add(target);
                    fullArgs.addAll(args);
                    yield ctx.invokeBuiltin(methodName, fullArgs);
                }
                yield Datum.VOID;
            }
        };
    }

    /**
     * Handle method calls on script references (e.g., calling new() on a script).
     */
    private static Datum handleScriptRefMethod(ExecutionContext ctx, Datum.ScriptRef scriptRef,
                                               String methodName, List<Datum> args) {
        if ("new".equalsIgnoreCase(methodName)) {
            // Create a new instance of the script
            List<Datum> fullArgs = new ArrayList<>();
            fullArgs.add(scriptRef);
            fullArgs.addAll(args);
            return ctx.invokeBuiltin("new", fullArgs);
        }
        return Datum.VOID;
    }

    /**
     * Handle method calls on point values.
     */
    private static Datum handlePointMethod(Datum.Point point, String methodName, List<Datum> args) {
        if ("getat".equalsIgnoreCase(methodName)) {
            if (args.isEmpty()) return Datum.VOID;
            int index = args.get(0).toInt();
            return index == 1 ? Datum.of(point.x()) : index == 2 ? Datum.of(point.y()) : Datum.VOID;
        } else if ("setat".equalsIgnoreCase(methodName)) {
            if (args.size() < 2) return Datum.VOID;
            point.setComponent(args.get(0).toInt(), args.get(1).toInt());
            return Datum.VOID;
        } else if ("inside".equalsIgnoreCase(methodName)) {
            // point.inside(rect) — returns TRUE if the point is inside the rect
            if (args.isEmpty()) return Datum.FALSE;
            Datum arg = args.get(0);
            if (arg instanceof Datum.Rect r) {
                boolean inside = point.x() >= r.left() && point.x() < r.right()
                        && point.y() >= r.top() && point.y() < r.bottom();
                return inside ? Datum.TRUE : Datum.FALSE;
            }
            return Datum.FALSE;
        }
        return Datum.VOID;
    }

    /**
     * Handle method calls on rect values.
     */
    private static Datum handleRectMethod(Datum.Rect rect, String methodName, List<Datum> args) {
        if ("getat".equalsIgnoreCase(methodName)) {
            if (args.isEmpty()) return Datum.VOID;
            int index = args.get(0).toInt();
            return switch (index) {
                case 1 -> Datum.of(rect.left());
                case 2 -> Datum.of(rect.top());
                case 3 -> Datum.of(rect.right());
                case 4 -> Datum.of(rect.bottom());
                default -> Datum.VOID;
            };
        } else if ("setat".equalsIgnoreCase(methodName)) {
            if (args.size() < 2) return Datum.VOID;
            rect.setComponent(args.get(0).toInt(), args.get(1).toInt());
            return Datum.VOID;
        }
        return Datum.VOID;
    }

    /**
     * Handle method calls on a VarRef (mutable variable reference).
     * Supports getPropRef and getProp to create chunk references or extract chunk values.
     */
    private static Datum handleVarRefMethod(ExecutionContext ctx, Datum.VarRef varRef,
                                            String methodName, List<Datum> args) {
        Datum value = resolveVarRef(ctx, varRef);
        String str = value.toStr();

        if ("getpropref".equalsIgnoreCase(methodName)) {
            if (args.size() < 2) return Datum.VOID;
            String chunkType = args.get(0) instanceof Datum.Symbol s ? s.name() : "char";
            int start = args.get(1).toInt();
            int end = args.size() >= 3 ? args.get(2).toInt() : start;
            return new Datum.ChunkRef(varRef.varType(), varRef.rawIndex(), chunkType, start, end);
        } else if ("getprop".equalsIgnoreCase(methodName)) {
            if (args.size() < 2) return Datum.EMPTY_STRING;
            String chunkType = args.get(0) instanceof Datum.Symbol s ? s.name() : "char";
            int start = args.get(1).toInt();
            int end = args.size() >= 3 ? args.get(2).toInt() : start;
            return Datum.of(getStringChunk(str, chunkType, start, end));
        } else {
            if (value instanceof Datum.Str s) {
                return StringMethodDispatcher.dispatch(s, methodName, args);
            }
            return Datum.VOID;
        }
    }

    /**
     * Handle method calls on a ChunkRef (chunk range within a variable).
     * Supports delete to remove the referenced chunk from the original variable.
     */
    private static Datum handleChunkRefMethod(ExecutionContext ctx, Datum.ChunkRef chunkRef,
                                              String methodName, List<Datum> args) {
        if ("delete".equalsIgnoreCase(methodName)) {
            Datum.VarRef varRef = new Datum.VarRef(chunkRef.varType(), chunkRef.rawIndex());
            Datum value = resolveVarRef(ctx, varRef);
            String str = value.toStr();
            String newStr = deleteChunkRange(str, chunkRef.chunkType(), chunkRef.start(), chunkRef.end());
            setVarRef(ctx, varRef, Datum.of(newStr));
            return Datum.VOID;
        }
        return Datum.VOID;
    }

    /**
     * Resolve a VarRef to its current value.
     */
    private static Datum resolveVarRef(ExecutionContext ctx, Datum.VarRef varRef) {
        int variableMultiplier = ctx.getVariableMultiplier();
        int index = varRef.rawIndex() / variableMultiplier;
        return switch (varRef.varType()) {
            case LOCAL -> ctx.getLocal(index);
            case PARAM -> ctx.getParam(index);
            case PROPERTY -> {
                Datum receiver = ctx.getReceiver();
                if (receiver instanceof Datum.ScriptInstance si) {
                    String propName = ctx.resolveName(varRef.rawIndex());
                    Datum v = si.properties().get(propName);
                    yield v != null ? v : Datum.VOID;
                }
                yield Datum.VOID;
            }
            case GLOBAL, GLOBAL2 -> {
                String name = ctx.resolveName(varRef.rawIndex());
                yield ctx.getGlobal(name);
            }
            default -> Datum.VOID;
        };
    }

    /**
     * Set the value of a VarRef.
     */
    private static void setVarRef(ExecutionContext ctx, Datum.VarRef varRef, Datum value) {
        int variableMultiplier = ctx.getVariableMultiplier();
        int index = varRef.rawIndex() / variableMultiplier;
        switch (varRef.varType()) {
            case LOCAL -> ctx.setLocal(index, value);
            case PARAM -> ctx.setParam(index, value);
            case PROPERTY -> {
                Datum receiver = ctx.getReceiver();
                if (receiver instanceof Datum.ScriptInstance si) {
                    String propName = ctx.resolveName(varRef.rawIndex());
                    si.properties().put(propName, value);
                }
            }
            case GLOBAL, GLOBAL2 -> {
                String name = ctx.resolveName(varRef.rawIndex());
                ctx.setGlobal(name, value);
            }
            default -> { /* FIELD not supported in VarRef context */ }
        }
    }

    /**
     * Get a chunk range from a string.
     * Uses if-else instead of switch to avoid TeaVM compiler bug with case "char".
     */
    private static String getStringChunk(String str, String chunkType, int start, int end) {
        if (str.isEmpty() || start < 1) return "";
        if ("char".equalsIgnoreCase(chunkType)) {
            int s = Math.max(0, start - 1);
            int e = Math.min(str.length(), end);
            if (s >= str.length() || s >= e) return "";
            return str.substring(s, e);
        }
        return str;
    }

    private static String deleteChunkRange(String str, String chunkType, int start, int end) {
        if (str.isEmpty() || start < 1) return str;
        if ("char".equalsIgnoreCase(chunkType)) {
            int s = Math.max(0, start - 1);
            int e = Math.min(str.length(), end);
            if (s >= str.length()) return str;
            return str.substring(0, s) + str.substring(e);
        }
        return str;
    }

    /**
     * Extract arguments from an arglist datum.
     * Arguments are stored directly in the ArgList/ArgListNoRet items.
     */
    /**
     * Extract arguments from an arglist datum.
     * Returns the inner list directly (ArgList constructor already copies).
     * This avoids a redundant ArrayList allocation on every function call.
     */
    private static List<Datum> getArgs(Datum argListDatum) {
        if (argListDatum instanceof Datum.ArgList al) {
            return al.items();
        } else if (argListDatum instanceof Datum.ArgListNoRet al) {
            return al.items();
        } else {
            return List.of();
        }
    }
}
