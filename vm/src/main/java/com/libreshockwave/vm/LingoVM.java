package com.libreshockwave.vm;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.chunks.ScriptNamesChunk;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.builtin.BuiltinRegistry;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.datum.DatumFormatter;
import com.libreshockwave.vm.datum.LingoException;
import com.libreshockwave.vm.opcode.ExecutionContext;
import com.libreshockwave.vm.opcode.OpcodeHandler;
import com.libreshockwave.vm.opcode.OpcodeRegistry;
import com.libreshockwave.vm.trace.ConsoleTracePrinter;
import com.libreshockwave.vm.trace.TracingHelper;

import java.util.*;

/**
 * Lingo Virtual Machine.
 * Executes bytecode from ScriptChunk handlers.
 * Similar to dirplayer-rs handler_manager.rs.
 */
public class LingoVM {
    // Match dirplayer-rs MAX_STACK_SIZE
    private static final int MAX_CALL_STACK_DEPTH = 50;

    private final DirectorFile file;
    private final Map<String, Datum> globals;
    private final Map<String, Datum> prefs;
    private final Deque<Scope> callStack;
    private final BuiltinRegistry builtins;
    private final OpcodeRegistry opcodeRegistry;
    private final TracingHelper tracingHelper;
    private final ConsoleTracePrinter consolePrinter;
    private final Map<String, HandlerRef> handlerCache = new HashMap<>();
    private final Set<String> missingHandlerCache = new HashSet<>();
    private final Map<ScriptChunk.Handler, String> handlerNameCache = new IdentityHashMap<>();
    private final Map<ScriptChunk.Handler, Boolean> handlerFirstParamMeCache = new IdentityHashMap<>();
    private final Map<ScriptChunk.LiteralEntry, Datum> literalDatumCache = new IdentityHashMap<>();
    private final Map<String, Datum> symbolDatumCache = new HashMap<>();
    private final Deque<DeferredScriptInstanceCall> deferredScriptInstanceCalls = new ArrayDeque<>();
    private final Deque<Runnable> deferredTasks = new ArrayDeque<>();
    private boolean explicitRandomSeed = false;
    private long randomState = 0;
    private int randomSeed = 0;
    private boolean flushingDeferredScriptInstanceCalls = false;
    private boolean flushingDeferredTasks = false;
    private static final ThreadLocal<LingoVM> CURRENT_VM = new ThreadLocal<>();

    private boolean traceEnabled = false;
    private int stepLimit = 0;  // 0 = unlimited

    // Tick-level deadline: when set, all handlers within the current tick must
    // complete before this wall-clock time. Prevents infinite loops that span
    // multiple short handler invocations (where per-handler timeout wouldn't fire).
    private long tickDeadline = 0;  // 0 = no tick-level timeout
    // Disabled by default: the per-handler 60s timeout already protects against
    // runaway loops, and a shorter per-tick deadline can falsely kill legitimate
    // long-running handlers such as replaceChunks on large payloads in WASM.
    private long tickDeadlineMs = 0;  // configurable duration (0 = disabled)

    // Event propagation callback (set by EventDispatcher)
    private Runnable passCallback;

    // Trace listener for debug UI
    private TraceListener traceListener;

    // Error state - when true, no more handlers will execute (like dirplayer-rs stop())
    private boolean inErrorState = false;

    // stopEvent flag - set by Lingo stopEvent() to prevent further sprite event dispatch
    private boolean eventStopped = false;

    // Static GC callback: invoked during GC safepoints to clear caches.
    // Set by player layer (e.g. WasmEntry) to release file caches and audio chunks
    // DURING long-running handlers like the dump, not just after they complete.
    private static Runnable gcCallback;

    // AlertHook handling (error suppression via Lingo's alertHook mechanism)
    private final AlertHookHandler alertHookHandler = new AlertHookHandler();

    public void setErrorHandlerSkipCallback(java.util.function.Consumer<String> callback) {
        alertHookHandler.setErrorHandlerSkipCallback(callback);
    }

    // Function trace hooks: when a handler name (lowercase) is in this set,
    // print its call stack after the call is entered.
    private final Set<String> tracedHandlers = new java.util.HashSet<>();

    public void addTraceHandler(String name) {
        tracedHandlers.add(normalizeLookupName(name));
    }

    public void removeTraceHandler(String name) {
        tracedHandlers.remove(normalizeLookupName(name));
    }

    public void clearTraceHandlers() {
        tracedHandlers.clear();
    }

    public Set<String> getTracedHandlers() {
        return java.util.Collections.unmodifiableSet(tracedHandlers);
    }

    public LingoVM(DirectorFile file) {
        this.file = file;
        this.globals = new HashMap<>();
        this.prefs = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);
        this.callStack = new ArrayDeque<>();
        this.builtins = new BuiltinRegistry();
        this.opcodeRegistry = new OpcodeRegistry();
        this.tracingHelper = new TracingHelper();
        this.consolePrinter = new ConsoleTracePrinter();
        this.cachedBuiltinInvoker = (name, args) -> builtins.invoke(name, this, args);
        setRandomSeed(0);
        String initialRandomSeed = System.getenv("LS_INITIAL_RANDOM_SEED");
        if (initialRandomSeed != null && !initialRandomSeed.isBlank()) {
            try {
                setRandomSeed(Integer.parseInt(initialRandomSeed.trim()));
            } catch (NumberFormatException ignored) {
                // Ignore invalid external override and keep the default startup seed.
            }
        }
        registerPassBuiltin();
    }

    public int randomInt(int max) {
        int result;
        if (max <= 0) {
            result = 1;
        } else {
            result = nextSeededRandomInt(max) + 1;
        }
        traceRandomCall(max, result);
        return result;
    }

    private void traceRandomCall(int max, int result) {
        if (!tracedHandlers.contains("random")) {
            return;
        }
        StringBuilder sb = new StringBuilder("[TRACE] random(");
        sb.append(max).append(")=").append(result);
        if (explicitRandomSeed) {
            sb.append(" seed=").append(randomSeed);
        }
        Scope scope = callStack.peek();
        if (scope != null) {
            sb.append(" at ")
                    .append(scope.getScript().getHandlerName(scope.getHandler()))
                    .append(" in \"")
                    .append(scope.getScript().getDisplayName())
                    .append('"');
        }
        System.out.println(sb.toString());
    }

    public int getRandomSeed() {
        return randomSeed;
    }

    public void setRandomSeed(int seed) {
        this.randomSeed = seed;
        this.explicitRandomSeed = true;
        this.randomState = scrambleRandomSeed(seed);
    }

    private int nextSeededRandomInt(int bound) {
        if ((bound & -bound) == bound) {
            return (int) ((bound * (long) nextRandomBits(31)) >> 31);
        }

        int bits;
        int value;
        do {
            bits = nextRandomBits(31);
            value = bits % bound;
        } while (bits - value + (bound - 1) < 0);
        return value;
    }

    private int nextRandomBits(int bits) {
        randomState = (randomState * 0x5DEECE66DL + 0xBL) & ((1L << 48) - 1);
        return (int) (randomState >>> (48 - bits));
    }

    private static long scrambleRandomSeed(long seed) {
        return (seed ^ 0x5DEECE66DL) & ((1L << 48) - 1);
    }

    private record DeferredScriptInstanceCall(Datum.ScriptInstance instance, String methodName, List<Datum> args) {}

    private void registerPassBuiltin() {
        // Register pass separately since it needs access to passCallback
        builtins.register("pass", (vm, args) -> {
            if (vm.passCallback != null) {
                vm.passCallback.run();
            }
            return Datum.VOID;
        });
        // stopEvent() prevents further event propagation to other sprites
        builtins.register("stopEvent", (vm, args) -> {
            vm.eventStopped = true;
            return Datum.VOID;
        });
    }

    // Configuration

    public void setTraceEnabled(boolean enabled) {
        this.traceEnabled = enabled;
        this.opcodeRegistry.setTraceEnabled(enabled);
    }

    public void setTraceListener(TraceListener listener) {
        this.traceListener = listener;
    }

    public TraceListener getTraceListener() {
        return traceListener;
    }

    /**
     * Notify the trace listener of an error (e.g. Lingo-level error() calls).
     */
    public void fireTraceError(String message, Exception error) {
        if (traceListener != null) {
            traceListener.onError(message, error);
        }
    }

    /**
     * Set a per-handler instruction step limit. 0 = unlimited (the default).
     */
    public void setStepLimit(int limit) {
        this.stepLimit = limit;
    }

    /**
     * Set the tick-level deadline duration in milliseconds. All handlers within
     * a single tick must complete within this time. 0 = disabled. Default: 30000.
     */
    public void setTickDeadlineMs(long ms) {
        this.tickDeadlineMs = ms;
    }

    public long getTickDeadlineMs() {
        return tickDeadlineMs;
    }

    /**
     * Arm the tick-level deadline using the configured duration.
     * Called by Player at the start of each tick.
     */
    public void setTickDeadline(long deadline) {
        this.tickDeadline = deadline;
    }

    /**
     * Set a callback to be invoked when a script calls pass().
     * Used by EventDispatcher to stop event propagation.
     */
    public void setPassCallback(Runnable callback) {
        this.passCallback = callback;
    }

    /**
     * Set a static callback invoked during GC safepoints.
     * Used by the WASM player to clear file caches and release audio chunks
     * DURING long handlers (like the 25s text dump), not just after they finish.
     */
    public static void setGCCallback(Runnable callback) {
        gcCallback = callback;
    }

    /**
     * Clear the pass callback.
     */
    public void clearPassCallback() {
        this.passCallback = null;
    }

    // Global variable access

    public Datum getGlobal(String name) {
        return globals.getOrDefault(name, Datum.VOID);
    }

    public void setGlobal(String name, Datum value) {
        globals.put(name, value);
    }

    public Map<String, Datum> getGlobals() {
        return Collections.unmodifiableMap(globals);
    }

    public void clearGlobals() {
        globals.clear();
    }

    public Datum getPref(String name) {
        if (name == null) {
            return Datum.VOID;
        }
        return prefs.getOrDefault(name, Datum.VOID);
    }

    public Datum setPref(String name, Datum value) {
        if (name == null || name.isEmpty()) {
            return Datum.VOID;
        }
        Datum stored = Datum.of(value != null ? value.toStr() : "");
        prefs.put(name, stored);
        return stored;
    }

    public Map<String, Datum> getPrefs() {
        return Collections.unmodifiableMap(prefs);
    }

    // Call stack access

    /**
     * A single frame in the Lingo call stack.
     */
    public record CallStackFrame(
            String handlerName,
            String scriptName,
            int bytecodeIndex,
            List<String> arguments
    ) {
        public CallStackFrame(String handlerName, String scriptName, int bytecodeIndex) {
            this(handlerName, scriptName, bytecodeIndex, List.of());
        }

        public CallStackFrame {
            arguments = arguments == null ? List.of() : List.copyOf(arguments);
        }
    }

    public int getCallStackDepth() {
        return callStack.size();
    }

    public Scope getCurrentScope() {
        return callStack.peek();
    }

    /**
     * Get the current Lingo call stack as a list of frames (top of stack first).
     * Safe to call at any time — returns an empty list when no handlers are executing.
     */
    public List<CallStackFrame> getCallStack() {
        if (callStack.isEmpty()) {
            return List.of();
        }
        List<CallStackFrame> frames = new ArrayList<>();
        for (Scope scope : callStack) {
            frames.add(toCallStackFrame(scope));
        }
        return frames;
    }

    // Handler execution

    /**
     * Find a handler by name in any script.
     * Searches the main file first, then external cast libraries.
     * @param handlerName The handler name to find
     * @return The script and handler, or null if not found
     */
    public HandlerRef findHandler(String handlerName) {
        if (handlerName == null || handlerName.isEmpty()) {
            return null;
        }

        String cacheKey = normalizeLookupName(handlerName);
        HandlerRef cached = handlerCache.get(cacheKey);
        if (cached != null) {
            return cached;
        }
        if (missingHandlerCache.contains(cacheKey)) {
            return null;
        }

        // First search the main file
        if (file != null) {
            for (ScriptChunk script : file.getScripts()) {
                if (!isGlobalHandlerScriptType(script.getScriptType())) {
                    continue;
                }
                ScriptChunk.Handler handler = script.findHandler(handlerName);
                if (handler != null) {
                    HandlerRef ref = new HandlerRef(script, handler);
                    handlerCache.put(cacheKey, ref);
                    return ref;
                }
            }
        }

        // Then search external cast libraries via CastLibProvider
        var provider = com.libreshockwave.vm.builtin.cast.CastLibProvider.getProvider();
        if (provider != null) {
            var location = provider.findHandler(handlerName);
            if (location != null && location.script() instanceof ScriptChunk script
                    && location.handler() instanceof ScriptChunk.Handler handler) {
                HandlerRef ref = new HandlerRef(script, handler);
                handlerCache.put(cacheKey, ref);
                return ref;
            }
        }

        missingHandlerCache.add(cacheKey);
        return null;
    }

    static boolean isGlobalHandlerScriptType(ScriptChunk.ScriptType scriptType) {
        return scriptType == ScriptChunk.ScriptType.MOVIE_SCRIPT;
    }

    /**
     * Invalidate cached global handler lookups.
     * Required when external casts become visible or are replaced.
     */
    public void invalidateHandlerCache() {
        handlerCache.clear();
        missingHandlerCache.clear();
    }

    public static LingoVM getCurrentVM() {
        return CURRENT_VM.get();
    }

    public boolean hasActiveCallStack() {
        return !callStack.isEmpty();
    }

    public boolean isFlushingDeferredScriptInstanceCalls() {
        return flushingDeferredScriptInstanceCalls;
    }

    public void deferScriptInstanceCall(Datum.ScriptInstance instance, String methodName, List<Datum> args) {
        if (instance == null || methodName == null || methodName.isEmpty()) {
            return;
        }
        deferredScriptInstanceCalls.addLast(new DeferredScriptInstanceCall(
                instance,
                methodName,
                List.copyOf(args)));
    }

    /**
     * Queue a task for the next safe VM boundary managed by the caller
     * (typically the end of the current player tick).
     */
    public void deferTask(Runnable task) {
        if (task == null) {
            return;
        }
        deferredTasks.addLast(task);
    }

    public boolean isFlushingDeferredTasks() {
        return flushingDeferredTasks;
    }

    private void flushDeferredScriptInstanceCalls() {
        if (flushingDeferredScriptInstanceCalls || deferredScriptInstanceCalls.isEmpty() || !callStack.isEmpty()) {
            return;
        }

        flushingDeferredScriptInstanceCalls = true;
        try {
            while (!deferredScriptInstanceCalls.isEmpty()) {
                DeferredScriptInstanceCall call = deferredScriptInstanceCalls.removeFirst();
                com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins.callHandlerOnInstance(
                        this,
                        call.instance(),
                        call.methodName(),
                        call.args());
            }
        } finally {
            flushingDeferredScriptInstanceCalls = false;
        }
    }

    public void flushDeferredTasks() {
        if (flushingDeferredTasks || deferredTasks.isEmpty() || !callStack.isEmpty()) {
            return;
        }

        flushingDeferredTasks = true;
        try {
            while (!deferredTasks.isEmpty()) {
                Runnable task = deferredTasks.removeFirst();
                task.run();
                if (!callStack.isEmpty()) {
                    break;
                }
            }
        } finally {
            flushingDeferredTasks = false;
        }
    }

    /**
     * Find a handler in a specific script.
     */
    public HandlerRef findHandler(ScriptChunk script, String handlerName) {
        ScriptChunk.Handler handler = script.findHandler(handlerName);
        if (handler != null) {
            return new HandlerRef(script, handler);
        }
        return null;
    }

    /**
     * Call a handler by name with arguments.
     * Global script handlers take precedence over builtins.
     * @param handlerName The handler name
     * @param args Arguments to pass
     * @return The return value
     */
    public Datum callHandler(String handlerName, List<Datum> args) {
        HandlerRef ref = findHandler(handlerName);
        if (ref != null) {
            return executeHandler(ref.script(), ref.handler(), args, null);
        }
        if (builtins.contains(handlerName)) {
            return builtins.invoke(handlerName, this, args);
        }
        return Datum.VOID;
    }

    /**
     * Invoke a builtin directly, bypassing global script handlers.
     * Used by engine/bootstrap code that must access runtime services before
     * movie-level APIs or managers exist.
     */
    public Datum callBuiltin(String handlerName, List<Datum> args) {
        if (builtins.contains(handlerName)) {
            return builtins.invoke(handlerName, this, args);
        }
        return Datum.VOID;
    }

    /**
     * Call a handler with a receiver (for behaviors/parent scripts).
     */
    public Datum callHandler(String handlerName, List<Datum> args, Datum receiver) {
        HandlerRef ref = findHandler(handlerName);
        if (ref == null) {
            return Datum.VOID;
        }
        return executeHandler(ref.script(), ref.handler(), args, receiver);
    }

    static boolean shouldSkipDeconstructReentry(
            String normalizedHandlerName,
            Datum effectiveReceiver,
            ScriptChunk currentScript,
            String existingNormalizedHandlerName,
            Datum existingReceiver,
            ScriptChunk existingScript) {
        return "deconstruct".equals(normalizedHandlerName)
                && "deconstruct".equals(existingNormalizedHandlerName)
                && effectiveReceiver == existingReceiver
                && currentScript == existingScript;
    }

    /**
     * Execute a specific handler with arguments.
     */
    public Datum executeHandler(ScriptChunk script, ScriptChunk.Handler handler,
                                List<Datum> args, Datum receiver) {
        // Like dirplayer-rs: if we're in an error state, don't execute any more handlers
        if (inErrorState) {
            return Datum.VOID;
        }

        // Prevent recursive error handling - if we're already in an error handler
        // and trying to call another error handler, return VOID
        String handlerName = getHandlerName(script, handler);
        boolean isErrorHandler = alertHookHandler.isErrorHandler(handlerName);
        if (isErrorHandler && alertHookHandler.shouldSkipErrorHandler(handlerName, args)) {
            return Datum.VOID;
        }
        String hn = normalizeLookupName(handlerName);

        // Reentrancy guard: if this same script's deconstruct is already on the
        // call stack for the same receiver, skip re-entry. Ancestor class chains
        // legitimately call derived.deconstruct -> callAncestor(#deconstruct),
        // so different scripts in the same chain must still be allowed to run.
        if ("deconstruct".equals(hn)) {
            // Resolve the effective receiver: explicit receiver, or args[0] if it's a ScriptInstance
            Datum effectiveReceiver = (receiver != null && !receiver.isVoid()) ? receiver
                    : (!args.isEmpty() && args.get(0) instanceof Datum.ScriptInstance ? args.get(0) : null);
            if (effectiveReceiver != null) {
                for (Scope existing : callStack) {
                    String existingHandler = normalizeLookupName(existing.getScript()
                            .getHandlerName(existing.getHandler()));
                    if (shouldSkipDeconstructReentry(
                            hn, effectiveReceiver, script,
                            existingHandler, existing.getReceiver(), existing.getScript())) {
                        return Datum.VOID;
                    }
                }
            }
        }

        if (callStack.size() >= MAX_CALL_STACK_DEPTH) {
            throw new LingoException("Call stack overflow (max " + MAX_CALL_STACK_DEPTH + " frames)");
        }

        // Function trace hook
        if (!tracedHandlers.isEmpty() && tracedHandlers.contains(hn)) {
            StringBuilder sb = new StringBuilder("[TRACE] ");
            sb.append(handlerName).append('(');
            for (int i = 0; i < args.size(); i++) {
                if (i > 0) sb.append(", ");
                sb.append(formatTraceArgument(args.get(i)));
            }
            sb.append(')');
            String scriptName = script.getScriptName();
            if (scriptName != null && !scriptName.isEmpty()) {
                sb.append(" in \"").append(scriptName).append('"');
            }
            System.out.println(sb.toString());
            System.out.println(formatCallStack());
        }

        // If there's a receiver (for parent script methods), prepend it to args as param0
        // This matches dirplayer-rs behavior where the receiver is included in scope.args
        List<Datum> effectiveArgs = args;
        Datum scopeReceiver = receiver;
        if (receiver != null && !receiver.isVoid()) {
            if (handlerDeclaresMeAsFirstParam(script, handler)) {
                effectiveArgs = new ArrayList<>(args.size() + 1);
                effectiveArgs.add(receiver);
                effectiveArgs.addAll(args);
            }
        } else {
            // No explicit receiver — derive from first arg if it's a ScriptInstance.
            // This handles LOCAL_CALL where the Lingo code explicitly passes 'me'
            // (e.g., searchTask(me, arg)). args[0] IS 'me' and should be used
            // as the receiver for property access within the handler.
            if (!args.isEmpty() && args.get(0) instanceof Datum.ScriptInstance) {
                scopeReceiver = args.get(0);
            }
        }

        Scope scope = new Scope(script, handler, effectiveArgs, scopeReceiver);
        callStack.push(scope);
        LingoVM previousVm = CURRENT_VM.get();
        CURRENT_VM.set(this);

        // Track error handler depth
        if (isErrorHandler) {
            alertHookHandler.incrementDepth();
        }

        // Notify trace listener of handler entry
        TraceListener.HandlerInfo handlerInfo = null;
        if (traceListener != null || traceEnabled) {
            handlerInfo = tracingHelper.buildHandlerInfo(script, handler, args, receiver, globals);

            if (traceEnabled) {
                consolePrinter.onHandlerEnter(handlerInfo);
            }
            if (traceListener != null) {
                traceListener.onHandlerEnter(handlerInfo);
            }
        }

        Datum result = Datum.VOID;
        try {
            // Create a single ExecutionContext per handler invocation and reuse it.
            // Previously we created a new one per instruction (~292K allocations for dump),
            // generating ~876K garbage objects that overwhelmed the WASM GC.
            ScriptChunk.Handler.Instruction firstInstr = scope.getCurrentInstruction();
            if (firstInstr == null) {
                return Datum.VOID;
            }
            ExecutionContext ctx = createExecutionContext(scope, firstInstr);
            int steps = 0;
            long startTime = System.currentTimeMillis();
            long lastGcTime = startTime;
            while (scope.hasMoreInstructions() && !scope.isReturned()) {
                steps++;
                if (stepLimit > 0 && steps > stepLimit) {
                    throw new LingoException("Step limit exceeded (" + stepLimit
                            + " instructions) in handler '" + handlerName + "'");
                }
                // Time-based GC safepoint for WASM: compact heap during long-running handlers.
                // 1s interval is aggressive but necessary: during the 25s text dump, the heap
                // fills with temporary strings/PropLists. Clearing caches via gcCallback frees
                // fileCache + audio/raw chunks that would otherwise cause post-dump OOB.
                // Check every 8192 instructions (not 1024) to reduce WASM→JS boundary
                // crossings from System.currentTimeMillis(). With DevTools open (especially
                // Network inspector), each Date.now() call is more expensive due to Chrome
                // instrumentation overhead — fewer checks avoids this bottleneck.
                if ((steps & 0xFFFF) == 0) {
                    long now = System.currentTimeMillis();
                    if (now - lastGcTime >= 1000) {
                        if (gcCallback != null) {
                            gcCallback.run();
                        }
                        // Don't call System.gc() — forced GC triggers TeaVM defrag
                        // that can corrupt pointers. Let automatic GC handle compaction.
                        lastGcTime = now;
                    }
                    // Hard timeout: no single handler should run for more than 60 seconds.
                    // The dump handler takes ~12s; anything over 60s is likely an infinite loop.
                    if (now - startTime > 60000) {
                        throw new LingoException("Handler timeout (60s, " + steps
                                + " instructions) in handler '" + handlerName + "'");
                    }
                    // Tick-level deadline: catches infinite loops that span multiple
                    // short handler invocations within a single tick.
                    if (tickDeadline > 0 && now > tickDeadline) {
                        throw new LingoException("Tick deadline exceeded in handler '"
                                + handlerName + "' (" + steps + " instructions)");
                    }
                }
                executeInstruction(scope, ctx);
            }

            result = scope.getReturnValue();
        } catch (Exception e) {
            // Attach Lingo call stack to LingoExceptions (only once, at the deepest frame)
            if (e instanceof LingoException le && le.getLingoCallStack() == null) {
                le.setLingoCallStack(getCallStack());
            }
            if (DebugConfig.isDebugPlaybackEnabled()) {
                System.err.println(formatCallStack());
                // Print Java stack trace for unexpected exceptions (NPE etc.)
                if (!(e instanceof LingoException)) {
                    e.printStackTrace(System.err);
                }
            }
            if (traceListener != null) {
                traceListener.onError("Error in " + script.getHandlerName(handler), e);
            }
            // Try alertHook before rethrowing — if it returns true, suppress the error
            if (!isErrorHandler && fireAlertHook("Script Error", e.getMessage())) {
                result = Datum.VOID; // Error suppressed by alertHook
            } else {
                throw e;
            }
        } finally {
            // Always notify handler exit, even on exception path.
            // Critical for debugger callDepth tracking - without this,
            // exceptions cause callDepth to drift and break step-over.
            if (traceListener != null && handlerInfo != null) {
                traceListener.onHandlerExit(handlerInfo, result);
            }
            if (traceEnabled && handlerInfo != null) {
                consolePrinter.onHandlerExit(handlerInfo, result);
            }
            callStack.pop();
            if (isErrorHandler) {
                alertHookHandler.decrementDepth();
            }
            if (callStack.isEmpty()) {
                flushDeferredScriptInstanceCalls();
            }
            if (previousVm != null) {
                CURRENT_VM.set(previousVm);
            } else {
                CURRENT_VM.remove();
            }
        }
        return result;
    }

    private boolean handlerDeclaresMeAsFirstParam(ScriptChunk script, ScriptChunk.Handler handler) {
        Boolean cached = handlerFirstParamMeCache.get(handler);
        if (cached != null) {
            return cached;
        }
        boolean declaresMe = computeHandlerDeclaresMeAsFirstParam(script, handler);
        handlerFirstParamMeCache.put(handler, declaresMe);
        return declaresMe;
    }

    private static boolean computeHandlerDeclaresMeAsFirstParam(ScriptChunk script, ScriptChunk.Handler handler) {
        if (handler.argNameIds().isEmpty() || script == null || script.file() == null) {
            return false;
        }
        ScriptNamesChunk names = script.file().getScriptNamesForScript(script);
        if (names == null) {
            return false;
        }
        return "me".equalsIgnoreCase(names.getName(handler.argNameIds().getFirst()));
    }

    static String formatTraceArgument(Datum value) {
        if (value == null) {
            return "<null>";
        }
        if (value.isVoid()) {
            return "<VOID>";
        }
        if (value instanceof Datum.Symbol symbol) {
            return "#" + symbol.name();
        }
        if (value instanceof Datum.Str str) {
            return "\"" + str.value() + "\"";
        }
        if (value instanceof Datum.FieldText fieldText) {
            return "\"" + fieldText.value() + "\"";
        }
        return value.toStr();
    }

    private String getHandlerName(ScriptChunk script, ScriptChunk.Handler handler) {
        String cached = handlerNameCache.get(handler);
        if (cached != null) {
            return cached;
        }
        String resolved = script.getHandlerName(handler);
        handlerNameCache.put(handler, resolved);
        return resolved;
    }

    public static String normalizeLookupName(String name) {
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            if (c >= 'A' && c <= 'Z') {
                return name.toLowerCase(Locale.ROOT);
            }
        }
        return name;
    }

    /**
     * Set the error state. When true, no more handlers will execute.
     * Like dirplayer-rs stop() behavior.
     */
    public void setErrorState(boolean errorState) {
        this.inErrorState = errorState;
    }

    /**
     * Check if VM is in error state.
     */
    public boolean isInErrorState() {
        return inErrorState;
    }

    /**
     * Reset the error state to allow execution to continue.
     * Call this at the start of each frame or event dispatch.
     */
    public void resetErrorState() {
        this.inErrorState = false;
    }

    /** Check if stopEvent() was called during the current event dispatch. */
    public boolean isEventStopped() {
        return eventStopped;
    }

    /** Reset the stopEvent flag at the start of each input event dispatch. */
    public void resetEventStopped() {
        this.eventStopped = false;
    }

    /**
     * Fire the alertHook handler if one is set.
     * Delegates to AlertHookHandler.
     */
    public boolean fireAlertHook(String errorMsg) {
        return fireAlertHook("Alert", errorMsg);
    }

    public boolean fireAlertHook(String errorType, String errorMsg) {
        return alertHookHandler.fireAlertHook(errorType, errorMsg, this::executeHandler);
    }

    /**
     * Format the current Lingo call stack as a human-readable string.
     * Iterates top-to-bottom through the call stack deque.
     */
    public String formatCallStack() {
        if (callStack.isEmpty()) {
            return "Lingo call stack: (empty)";
        }
        StringBuilder sb = new StringBuilder("Lingo call stack:\n");
        for (Scope scope : callStack) {
            appendCallStackFrame(sb, toCallStackFrame(scope));
        }
        return sb.toString();
    }

    private CallStackFrame toCallStackFrame(Scope scope) {
        List<Datum> displayArguments = scope.getDisplayArguments();
        Datum receiver = scope.getReceiver();
        if (receiver != null && !receiver.isVoid()
                && !displayArguments.isEmpty()
                && displayArguments.getFirst() == receiver) {
            displayArguments = displayArguments.subList(1, displayArguments.size());
        }
        List<String> arguments = displayArguments.stream()
                .map(DatumFormatter::formatExpanded)
                .toList();
        return new CallStackFrame(
                scope.getScript().getHandlerName(scope.getHandler()),
                scope.getScript().getDisplayName(),
                scope.getBytecodeIndex(),
                arguments
        );
    }

    public static void appendCallStackFrame(StringBuilder sb, CallStackFrame frame) {
        sb.append("  at ").append(frame.handlerName()).append('(');
        for (int i = 0; i < frame.arguments().size(); i++) {
            if (i > 0) sb.append(", ");
            sb.append(frame.arguments().get(i));
        }
        sb.append(") (").append(frame.scriptName()).append(")")
          .append(" [bytecode ").append(frame.bytecodeIndex()).append(']');
        sb.append('\n');
    }

    /**
     * Execute a single bytecode instruction using a reusable ExecutionContext.
     */
    private void executeInstruction(Scope scope, ExecutionContext ctx) {
        ScriptChunk.Handler.Instruction instr = scope.getCurrentInstruction();
        if (instr == null) {
            scope.setReturned(true);
            return;
        }

        // Trace before execution
        boolean needsInstr = traceEnabled || (traceListener != null && traceListener.needsInstructionTrace());
        if (needsInstr) {
            TraceListener.InstructionInfo instrInfo = tracingHelper.buildInstructionInfo(scope, instr, globals);
            if (traceEnabled) {
                consolePrinter.onInstruction(instrInfo);
            }
            if (traceListener != null) {
                traceListener.onInstruction(instrInfo);
            }
        }

        Opcode op = instr.opcode();

        OpcodeHandler handler = opcodeRegistry.get(op);
        if (handler != null) {
            ctx.setInstruction(instr);
            if (executeFastInstruction(scope, ctx, op)) {
                return;
            }
            boolean advance = handler.execute(ctx);
            if (advance) {
                scope.advanceBytecodeIndex();
            }
        } else {
            System.err.println("[LingoVM] Unimplemented opcode: " + op);
            scope.advanceBytecodeIndex();
        }
    }

    private boolean executeFastInstruction(Scope scope, ExecutionContext ctx, Opcode op) {
        return switch (op) {
            case PUSH_ZERO -> {
                scope.push(Datum.ZERO);
                scope.advanceBytecodeIndex();
                yield true;
            }
            case PUSH_INT8, PUSH_INT16, PUSH_INT32 -> {
                scope.push(Datum.of(ctx.getArgument()));
                scope.advanceBytecodeIndex();
                yield true;
            }
            case PUSH_FLOAT32 -> {
                scope.push(Datum.of(Float.intBitsToFloat(ctx.getArgument())));
                scope.advanceBytecodeIndex();
                yield true;
            }
            case PUSH_CONS -> {
                scope.push(resolveLiteralDatum(scope, ctx.getScaledArgument()));
                scope.advanceBytecodeIndex();
                yield true;
            }
            case PUSH_SYMB -> {
                scope.push(resolveSymbolDatum(ctx.resolveName(ctx.getArgument())));
                scope.advanceBytecodeIndex();
                yield true;
            }
            case GET_LOCAL -> {
                scope.push(scope.getLocal(ctx.getScaledArgument()));
                scope.advanceBytecodeIndex();
                yield true;
            }
            case SET_LOCAL -> {
                scope.setLocal(ctx.getScaledArgument(), scope.pop());
                scope.advanceBytecodeIndex();
                yield true;
            }
            case GET_PARAM -> {
                scope.push(scope.getParam(ctx.getScaledArgument()));
                scope.advanceBytecodeIndex();
                yield true;
            }
            case SET_PARAM -> {
                scope.setParam(ctx.getScaledArgument(), scope.pop());
                scope.advanceBytecodeIndex();
                yield true;
            }
            case SWAP -> {
                scope.swap();
                scope.advanceBytecodeIndex();
                yield true;
            }
            case POP -> {
                int count = ctx.getArgument();
                scope.drop(count <= 1 ? 1 : count);
                scope.advanceBytecodeIndex();
                yield true;
            }
            case PEEK -> {
                scope.push(scope.peek(ctx.getArgument()));
                scope.advanceBytecodeIndex();
                yield true;
            }
            case RET -> {
                scope.setReturnValue(scope.pop());
                scope.advanceBytecodeIndex();
                yield true;
            }
            case RET_FACTORY -> {
                scope.setReturnValue(Datum.VOID);
                scope.advanceBytecodeIndex();
                yield true;
            }
            case JMP -> {
                ctx.jumpTo(ctx.getInstructionOffset() + ctx.getArgument());
                yield true;
            }
            case JMP_IF_Z -> {
                Datum cond = scope.pop();
                if (!cond.isTruthy()) {
                    ctx.jumpTo(ctx.getInstructionOffset() + ctx.getArgument());
                } else {
                    scope.advanceBytecodeIndex();
                }
                yield true;
            }
            case END_REPEAT -> {
                ctx.jumpTo(ctx.getInstructionOffset() - ctx.getArgument());
                yield true;
            }
            case PUSH_ARG_LIST -> {
                scope.push(new Datum.ArgList(ctx.popArgs(ctx.getArgument())));
                scope.advanceBytecodeIndex();
                yield true;
            }
            case PUSH_ARG_LIST_NO_RET -> {
                scope.push(new Datum.ArgListNoRet(ctx.popArgs(ctx.getArgument())));
                scope.advanceBytecodeIndex();
                yield true;
            }
            case ADD -> fastAdd(scope);
            case SUB -> fastSub(scope);
            case MUL -> fastMul(scope);
            case DIV -> fastDiv(scope);
            case MOD -> fastMod(scope);
            case INV -> fastInv(scope);
            case EQ -> fastEq(scope);
            case NT_EQ -> fastNtEq(scope);
            case LT -> fastCompare(scope, '<');
            case LT_EQ -> fastCompare(scope, 'l');
            case GT -> fastCompare(scope, '>');
            case GT_EQ -> fastCompare(scope, 'g');
            default -> false;
        };
    }

    private Datum resolveLiteralDatum(Scope scope, int index) {
        List<ScriptChunk.LiteralEntry> literals = scope.getScript().literals();
        if (index < 0 || index >= literals.size()) {
            return Datum.VOID;
        }
        ScriptChunk.LiteralEntry lit = literals.get(index);
        Datum cached = literalDatumCache.get(lit);
        if (cached != null) {
            return cached;
        }
        Datum value = switch (lit.type()) {
            case 1 -> Datum.of((String) lit.value());
            case 4 -> Datum.of((Integer) lit.value());
            case 9 -> Datum.of(lit.numericValue());
            default -> Datum.VOID;
        };
        literalDatumCache.put(lit, value);
        return value;
    }

    private Datum resolveSymbolDatum(String name) {
        return symbolDatumCache.computeIfAbsent(name, Datum::symbol);
    }

    private static boolean fastAdd(Scope scope) {
        Datum b = scope.peek();
        Datum a = scope.peek(1);
        if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
            scope.replaceTopTwo(Datum.of(ai.value() + bi.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Float bf) {
            scope.replaceTopTwo(Datum.of(af.value() + bf.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Int ai && b instanceof Datum.Float bf) {
            scope.replaceTopTwo(Datum.of(ai.value() + bf.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Int bi) {
            scope.replaceTopTwo(Datum.of(af.value() + bi.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        return false;
    }

    private static boolean fastSub(Scope scope) {
        Datum b = scope.peek();
        Datum a = scope.peek(1);
        if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
            scope.replaceTopTwo(Datum.of(ai.value() - bi.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Float bf) {
            scope.replaceTopTwo(Datum.of(af.value() - bf.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Int ai && b instanceof Datum.Float bf) {
            scope.replaceTopTwo(Datum.of(ai.value() - bf.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Int bi) {
            scope.replaceTopTwo(Datum.of(af.value() - bi.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        return false;
    }

    private static boolean fastMul(Scope scope) {
        Datum b = scope.peek();
        Datum a = scope.peek(1);
        if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
            scope.replaceTopTwo(Datum.of(ai.value() * bi.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Float bf) {
            scope.replaceTopTwo(Datum.of(af.value() * bf.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Int ai && b instanceof Datum.Float bf) {
            scope.replaceTopTwo(Datum.of(ai.value() * bf.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Int bi) {
            scope.replaceTopTwo(Datum.of(af.value() * bi.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        return false;
    }

    private static boolean fastDiv(Scope scope) {
        Datum b = scope.peek();
        Datum a = scope.peek(1);
        if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
            int divisor = bi.value();
            if (divisor == 0) throw new LingoException("Division by zero");
            scope.replaceTopTwo(Datum.of(ai.value() / divisor));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Float bf) {
            double divisor = bf.value();
            if (divisor == 0) throw new LingoException("Division by zero");
            scope.replaceTopTwo(Datum.of(af.value() / divisor));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Int ai && b instanceof Datum.Float bf) {
            double divisor = bf.value();
            if (divisor == 0) throw new LingoException("Division by zero");
            scope.replaceTopTwo(Datum.of(ai.value() / divisor));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Int bi) {
            int divisor = bi.value();
            if (divisor == 0) throw new LingoException("Division by zero");
            scope.replaceTopTwo(Datum.of(af.value() / divisor));
            scope.advanceBytecodeIndex();
            return true;
        }
        return false;
    }

    private static boolean fastMod(Scope scope) {
        Datum b = scope.peek();
        Datum a = scope.peek(1);
        if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
            int divisor = bi.value();
            if (divisor == 0) throw new LingoException("Modulo by zero");
            scope.replaceTopTwo(Datum.of(ai.value() % divisor));
            scope.advanceBytecodeIndex();
            return true;
        }
        return false;
    }

    private static boolean fastInv(Scope scope) {
        Datum a = scope.peek();
        if (a instanceof Datum.Int ai) {
            scope.replaceTop(Datum.of(-ai.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        if (a instanceof Datum.Float af) {
            scope.replaceTop(Datum.of(-af.value()));
            scope.advanceBytecodeIndex();
            return true;
        }
        return false;
    }

    private static boolean fastEq(Scope scope) {
        Datum b = scope.peek();
        Datum a = scope.peek(1);
        Boolean result = fastNumericEquals(a, b);
        if (result == null && a != b) return false;
        scope.replaceTopTwo(Boolean.TRUE.equals(result) || a == b ? Datum.TRUE : Datum.FALSE);
        scope.advanceBytecodeIndex();
        return true;
    }

    private static boolean fastNtEq(Scope scope) {
        Datum b = scope.peek();
        Datum a = scope.peek(1);
        Boolean result = fastNumericEquals(a, b);
        if (result == null && a != b) return false;
        scope.replaceTopTwo(Boolean.TRUE.equals(result) || a == b ? Datum.FALSE : Datum.TRUE);
        scope.advanceBytecodeIndex();
        return true;
    }

    private static Boolean fastNumericEquals(Datum a, Datum b) {
        if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) return ai.value() == bi.value();
        if (a instanceof Datum.Float af && b instanceof Datum.Float bf) return af.value() == bf.value();
        if (a instanceof Datum.Int ai && b instanceof Datum.Float bf) return ai.value() == bf.value();
        if (a instanceof Datum.Float af && b instanceof Datum.Int bi) return af.value() == bi.value();
        return null;
    }

    private static boolean fastCompare(Scope scope, char op) {
        Datum b = scope.peek();
        Datum a = scope.peek(1);
        boolean result;
        if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
            result = compare(ai.value(), bi.value(), op);
        } else if (a instanceof Datum.Float af && b instanceof Datum.Float bf) {
            result = compare(af.value(), bf.value(), op);
        } else if (a instanceof Datum.Int ai && b instanceof Datum.Float bf) {
            result = compare(ai.value(), bf.value(), op);
        } else if (a instanceof Datum.Float af && b instanceof Datum.Int bi) {
            result = compare(af.value(), bi.value(), op);
        } else {
            return false;
        }
        scope.replaceTopTwo(result ? Datum.TRUE : Datum.FALSE);
        scope.advanceBytecodeIndex();
        return true;
    }

    private static boolean compare(double a, double b, char op) {
        return switch (op) {
            case '<' -> a < b;
            case 'l' -> a <= b;
            case '>' -> a > b;
            case 'g' -> a >= b;
            default -> false;
        };
    }

    // Cached callbacks for ExecutionContext — allocated once, reused across all handlers.
    // Previously these were recreated per-instruction, generating ~876K garbage objects
    // during the dump handler alone (~292K instructions × 3 allocations each).
    private final ExecutionContext.GlobalAccessor cachedGlobalAccessor =
            new ExecutionContext.GlobalAccessor() {
                @Override
                public Datum getGlobal(String name) {
                    return LingoVM.this.getGlobal(name);
                }
                @Override
                public void setGlobal(String name, Datum value) {
                    LingoVM.this.setGlobal(name, value);
                }
            };
    private final ExecutionContext.BuiltinInvoker cachedBuiltinInvoker;

    /**
     * Create an execution context for opcode handlers.
     */
    private ExecutionContext createExecutionContext(Scope scope, ScriptChunk.Handler.Instruction instr) {
        return new ExecutionContext(
            scope,
            this,
            instr,
            builtins,
            traceListener,
            this::executeHandler,
            this::findHandler,
            cachedGlobalAccessor,
            cachedBuiltinInvoker,
            this::setErrorState,
            this::formatCallStack
        );
    }
}
