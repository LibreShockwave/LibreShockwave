package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.util.FileUtil;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.chunks.ScriptNamesChunk;
import com.libreshockwave.player.behavior.BehaviorManager;
import com.libreshockwave.player.cast.CastLibManager;
import com.libreshockwave.player.event.EventDispatcher;
import com.libreshockwave.player.frame.FrameContext;
import com.libreshockwave.player.input.InputState;
import com.libreshockwave.player.net.NetManager;
import com.libreshockwave.player.render.pipeline.BitmapCache;
import com.libreshockwave.player.render.pipeline.FrameRenderPipeline;
import com.libreshockwave.player.render.pipeline.FrameSnapshot;
import com.libreshockwave.player.render.pipeline.SpriteBaker;
import com.libreshockwave.player.render.pipeline.StageRenderer;
import com.libreshockwave.player.score.ScoreNavigator;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.datum.LingoException;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.TraceListener;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.builtin.net.ExternalParamProvider;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import com.libreshockwave.vm.builtin.net.NetBuiltins;
import com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider;
import com.libreshockwave.vm.builtin.media.SoundProvider;
import com.libreshockwave.vm.builtin.timeout.TimeoutProvider;
import com.libreshockwave.vm.builtin.xtra.XtraBuiltins;
import com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins;
import com.libreshockwave.vm.builtin.flow.UpdateProvider;
import com.libreshockwave.player.audio.AudioBackend;
import com.libreshockwave.player.audio.SoundManager;
import com.libreshockwave.player.debug.LifecycleDiagnostics;
import com.libreshockwave.player.timeout.TimeoutManager;
import com.libreshockwave.vm.xtra.MultiuserNetBridge;
import com.libreshockwave.vm.xtra.MultiuserXtra;
import com.libreshockwave.player.xtra.SocketMultiuserBridge;
import com.libreshockwave.vm.xtra.XtraManager;
import com.libreshockwave.player.debug.DebugControllerApi;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.ByteArrayInputStream;
import java.util.*;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

/**
 * Director movie player.
 * Handles frame playback, event dispatch, and score traversal.
 * Uses modular components for score navigation, behavior management, and event dispatch.
 */
public class Player implements UpdateProvider {

    private final DirectorFile file;
    private final LingoVM vm;
    private final FrameContext frameContext;
    private final StageRenderer stageRenderer;
    private final NetManager netManager;  // null when using external NetProvider (e.g. TeaVM)
    private final XtraManager xtraManager;
    private final MovieProperties movieProperties;
    private final SpriteProperties spriteProperties;
    private final CastLibManager castLibManager;
    private final TimeoutManager timeoutManager;
    private final SoundManager soundManager;
    private final BitmapCache bitmapCache;
    private final SpriteBaker spriteBaker;
    private final FrameRenderPipeline frameRenderPipeline;
    private final InputState inputState;

    // Extracted components
    private final BitmapResolver bitmapResolver;
    private final CursorManager cursorManager;
    private final InputHandler inputHandler;

    private final PlayerTraceListener playerTraceListener;

    private PlayerState state = PlayerState.STOPPED;
    private int tempo;  // Frames per second

    // Event listeners for external notification
    private Consumer<PlayerEventInfo> eventListener;

    // Compatibility listener: notified when any external cast finishes loading.
    private Runnable castLoadedListener;
    private Consumer<ExternalCastLoadEvent> externalCastLoadListener;

    // Error listener (called on Lingo script errors)
    private java.util.function.BiConsumer<String, LingoException> errorListener;
    private volatile String lastScriptErrorMessage = "";
    private volatile String lastScriptErrorStack = "";
    private volatile long lastScriptErrorTimeMs = 0L;

    // Debug mode
    private boolean debugEnabled = false;

    // Debug controller for bytecode debugging
    private DebugControllerApi debugController;

    // Executor for running VM on background thread (required for debugger blocking)
    // Lazy-initialized to avoid creating threads in environments that don't support them (e.g. TeaVM)
    private ExecutorService vmExecutor;

    // Executor for parsing external cast files off the network thread
    // Lazy-initialized to avoid pulling in java.util.concurrent in TeaVM environments
    private ExecutorService castParserExecutor;
    private Runnable castParserShutdown;  // Shutdown hook, avoids referencing ExecutorService in shutdown()
    private Runnable vmExecutorShutdown;  // Shutdown hook, avoids referencing ExecutorService in shutdown()
    private final java.util.concurrent.atomic.AtomicBoolean vmRunning = new java.util.concurrent.atomic.AtomicBoolean(false);
    private final java.util.List<ExternalCastLoadHandler> externalCastLoadHandlers = new java.util.ArrayList<>();
    private PlayerCompatibilityProfile compatibilityProfile = PlayerCompatibilityProfile.NONE;
    private final List<Datum> updatingObjects = new ArrayList<>();
    private final Map<String, Datum> initialBuiltinVariables = new LinkedHashMap<>();

    // External parameters (Shockwave PARAM tags)
    private final Map<String, String> externalParams = new LinkedHashMap<>();
    private final ExternalParamProvider externalParamProvider = new ExternalParamProvider() {
        @Override
        public String getParamValue(String name) {
            for (var entry : externalParams.entrySet()) {
                if (entry.getKey().equalsIgnoreCase(name)) {
                    return entry.getValue();
                }
            }
            return null;
        }

        @Override
        public String getParamName(int index) {
            if (index < 1 || index > externalParams.size()) return null;
            int i = 1;
            for (String key : externalParams.keySet()) {
                if (i == index) return key;
                i++;
            }
            return null;
        }

        @Override
        public int getParamCount() {
            return externalParams.size();
        }

        @Override
        public Map<String, String> getAllParams() {
            return Collections.unmodifiableMap(externalParams);
        }
    };

    // Optional override for the network provider (used by player-wasm to substitute FetchNetManager)
    private NetBuiltins.NetProvider overrideNetProvider;

    public Player(DirectorFile file) {
        this.file = file;
        this.vm = new LingoVM(file);
        this.frameContext = new FrameContext(file, vm);
        this.stageRenderer = new StageRenderer(file);
        // Apply stage background color from movie config (palette-aware)
        if (file != null && file.getConfig() != null) {
            this.stageRenderer.setDefaultBackgroundColor(file.getConfig().stageColorRGB());
        }
        this.netManager = new NetManager();
        this.xtraManager = new XtraManager();
        registerMultiuserXtra(new SocketMultiuserBridge());
        this.movieProperties = new MovieProperties(this, file);
        this.spriteProperties = new SpriteProperties(stageRenderer.getSpriteRegistry());
        // Initialize cast parser executor (only needed for desktop player with NetManager)
        this.castParserExecutor = Executors.newFixedThreadPool(
            Math.max(2, Runtime.getRuntime().availableProcessors() / 2),
            r -> { Thread t = new Thread(r, "CastParser"); t.setDaemon(true); return t; }
        );
        this.castParserShutdown = () -> castParserExecutor.shutdownNow();

        // Cast data request callback: when Lingo sets castLib.fileName, load the data from
        // NetManager's cache (already downloaded by preloadNetThing) into the specific cast.
        this.castLibManager = new CastLibManager(file, (castLibNumber, fileName) ->
            loadCastFromNetCache(castLibNumber, fileName));
        this.stageRenderer.setCastLibManager(castLibManager);
        this.spriteProperties.setCastLibManager(castLibManager);
        this.timeoutManager = new TimeoutManager();
        this.soundManager = new SoundManager(castLibManager);
        this.bitmapCache = new BitmapCache();
        this.spriteBaker = new SpriteBaker(bitmapCache, castLibManager, this);
        this.frameRenderPipeline = new FrameRenderPipeline(stageRenderer, spriteBaker);
        this.inputState = new InputState();
        this.bitmapResolver = new BitmapResolver(file, castLibManager, frameContext);
        // Set initial palette (updated each tick in setupProviders)
        com.libreshockwave.vm.datum.Datum.setActivePalette(bitmapResolver.getMoviePalette());
        this.cursorManager = new CursorManager(stageRenderer, inputState, castLibManager,
                bitmapResolver, this::getCurrentFrame, () -> frameContext.getEventDispatcher(),
                () -> movieProperties.getMovieProp("cursor"));
        this.inputHandler = new InputHandler(inputState, stageRenderer, castLibManager,
                this::getCurrentFrame, () -> frameContext.getEventDispatcher());
        this.movieProperties.setInputState(inputState);
        this.frameContext.setTimeoutManager(timeoutManager);
        this.frameContext.getEventDispatcher().setCastLibManager(castLibManager);
        this.frameContext.getEventDispatcher().setSpriteRegistry(stageRenderer.getSpriteRegistry());
        this.frameContext.setSpriteRegistry(stageRenderer.getSpriteRegistry());
        this.frameContext.getBehaviorManager().setCastLibManager(castLibManager);
        this.frameContext.setActorListSupplier(movieProperties::getActorList);
        this.playerTraceListener = new PlayerTraceListener();
        vm.setTraceListener(playerTraceListener);

        // Use the movie's authored config tempo as the base frame rate.
        // Score tempo channel and puppetTempo can still override this per-frame.
        this.tempo = file.getConfig().tempo() > 0 ? file.getConfig().tempo() : 15;

        // Set up AWT JPEG decoder for ediM bitmap support (desktop only)
        DirectorFile.setJpegDecoder(jpegData -> {
            try {
                BufferedImage img = ImageIO.read(new ByteArrayInputStream(jpegData));
                if (img == null) return null;
                Bitmap bmp = new Bitmap(img.getWidth(), img.getHeight(), 32);
                for (int y = 0; y < img.getHeight(); y++) {
                    for (int x = 0; x < img.getWidth(); x++) {
                        bmp.setPixel(x, y, img.getRGB(x, y));
                    }
                }
                return bmp;
            } catch (Exception e) {
                return null;
            }
        });

        // Set software text renderer (no AWT dependency)
        com.libreshockwave.player.cast.CastMember.setTextRenderer(new com.libreshockwave.player.render.output.SimpleTextRenderer());
        com.libreshockwave.player.cast.CastMember.setPaletteResolver(castLibManager::resolvePaletteByMember);
        com.libreshockwave.player.cast.CastMember.setMemberResolver(castLibManager::resolveMember);
        // Wire up member visual change callback to bump sprite revision
        com.libreshockwave.player.cast.CastMember.setMemberVisualChangedCallback(
                () -> stageRenderer.getSpriteRegistry().bumpRevision());
        com.libreshockwave.vm.opcode.dispatch.ImageMethodDispatcher.setImageMutationCallback(
                () -> stageRenderer.getSpriteRegistry().bumpRevision());
        com.libreshockwave.player.cast.CastMember.setMemberSlotRetiredCallback(
                (castLib, memberNum) -> stageRenderer.getSpriteRegistry().clearDynamicMemberBindings(castLib, memberNum));

        // Set base path for network requests from the file location
        if (file != null && file.getBasePath() != null && !file.getBasePath().isEmpty()) {
            netManager.setBasePath(file.getBasePath());
        }

        // Wire up network completion callback to handle external cast loading.
        // This runs synchronously so cast member lookup/state is ready before
        // scripts observe a completed net task.
        netManager.setCompletionCallback((fileName, data) -> {
            handleExternalCastFetch(fileName, data);
        });

        // Wire up event notifications
        frameContext.setEventListener(event -> {
            if (eventListener != null) {
                eventListener.accept(new PlayerEventInfo(event.event(), event.frame(), 0));
            }
            // Notify stage renderer of frame changes
            if (event.event() == PlayerEvent.ENTER_FRAME) {
                stageRenderer.onFrameEnter(event.frame());
            }
        });
    }

    /**
     * Constructor for environments that provide their own NetProvider (e.g. TeaVM/browser).
     * Skips creating the JVM NetManager to avoid pulling in java.util.concurrent classes.
     */
    public Player(DirectorFile file, NetBuiltins.NetProvider netProvider,
                  java.util.function.BiConsumer<Integer, String> castDataRequestCallback) {
        this.file = file;
        this.vm = new LingoVM(file);
        this.frameContext = new FrameContext(file, vm);
        this.stageRenderer = new StageRenderer(file);
        // Apply stage background color from movie config (palette-aware)
        if (file != null && file.getConfig() != null) {
            this.stageRenderer.setDefaultBackgroundColor(file.getConfig().stageColorRGB());
        }
        this.netManager = null;
        this.overrideNetProvider = netProvider;
        this.xtraManager = new XtraManager();
        // No auto-register here — TeaVM has no socket support.
        // WASM callers should call registerMultiuserXtra() with their own bridge.
        this.movieProperties = new MovieProperties(this, file);
        this.spriteProperties = new SpriteProperties(stageRenderer.getSpriteRegistry());
        this.castLibManager = new CastLibManager(file, (castLibNum, fileName) -> {
            handleCastDataRequest(castLibNum, fileName, castDataRequestCallback);
        });
        this.stageRenderer.setCastLibManager(castLibManager);
        this.spriteProperties.setCastLibManager(castLibManager);
        this.timeoutManager = new TimeoutManager();
        this.soundManager = new SoundManager(castLibManager);
        this.bitmapCache = new BitmapCache();
        this.spriteBaker = new SpriteBaker(bitmapCache, castLibManager, this);
        this.frameRenderPipeline = new FrameRenderPipeline(stageRenderer, spriteBaker);
        this.inputState = new InputState();
        this.bitmapResolver = new BitmapResolver(file, castLibManager, frameContext);
        // Set initial palette (updated each tick in setupProviders)
        com.libreshockwave.vm.datum.Datum.setActivePalette(bitmapResolver.getMoviePalette());
        this.cursorManager = new CursorManager(stageRenderer, inputState, castLibManager,
                bitmapResolver, this::getCurrentFrame, () -> frameContext.getEventDispatcher(),
                () -> movieProperties.getMovieProp("cursor"));
        this.inputHandler = new InputHandler(inputState, stageRenderer, castLibManager,
                this::getCurrentFrame, () -> frameContext.getEventDispatcher());
        this.movieProperties.setInputState(inputState);
        // Set simple text renderer for TeaVM/WASM (no AWT)
        com.libreshockwave.player.cast.CastMember.setTextRenderer(new com.libreshockwave.player.render.output.SimpleTextRenderer());
        com.libreshockwave.player.cast.CastMember.setPaletteResolver(castLibManager::resolvePaletteByMember);
        com.libreshockwave.player.cast.CastMember.setMemberResolver(castLibManager::resolveMember);
        com.libreshockwave.player.cast.CastMember.setMemberVisualChangedCallback(
                () -> stageRenderer.getSpriteRegistry().bumpRevision());
        com.libreshockwave.vm.opcode.dispatch.ImageMethodDispatcher.setImageMutationCallback(
                () -> stageRenderer.getSpriteRegistry().bumpRevision());
        com.libreshockwave.player.cast.CastMember.setMemberSlotRetiredCallback(
                (castLib, memberNum) -> stageRenderer.getSpriteRegistry().clearDynamicMemberBindings(castLib, memberNum));
        this.frameContext.setTimeoutManager(timeoutManager);
        this.frameContext.getEventDispatcher().setCastLibManager(castLibManager);
        this.frameContext.getEventDispatcher().setSpriteRegistry(stageRenderer.getSpriteRegistry());
        this.frameContext.setSpriteRegistry(stageRenderer.getSpriteRegistry());
        this.frameContext.getBehaviorManager().setCastLibManager(castLibManager);
        this.frameContext.setActorListSupplier(movieProperties::getActorList);
        this.playerTraceListener = new PlayerTraceListener();
        vm.setTraceListener(playerTraceListener);

        // Use the movie's authored config tempo as the base frame rate.
        // Score tempo channel and puppetTempo can still override this per-frame.
        this.tempo = file.getConfig().tempo() > 0 ? file.getConfig().tempo() : 15;

        // Wire up event notifications
        frameContext.setEventListener(event -> {
            if (eventListener != null) {
                eventListener.accept(new PlayerEventInfo(event.event(), event.frame(), 0));
            }
            // Notify stage renderer of frame changes
            if (event.event() == PlayerEvent.ENTER_FRAME) {
                stageRenderer.onFrameEnter(event.frame());
            }
        });
    }

    // Accessors

    public DirectorFile getFile() {
        return file;
    }

    public LingoVM getVM() {
        return vm;
    }

    public FrameContext getFrameContext() {
        return frameContext;
    }

    public ScoreNavigator getNavigator() {
        return frameContext.getNavigator();
    }

    public BehaviorManager getBehaviorManager() {
        return frameContext.getBehaviorManager();
    }

    public EventDispatcher getEventDispatcher() {
        return frameContext.getEventDispatcher();
    }

    public StageRenderer getStageRenderer() {
        return stageRenderer;
    }

    public NetManager getNetManager() {
        return netManager;
    }

    public XtraManager getXtraManager() {
        return xtraManager;
    }

    /**
     * Register the Multiuser Xtra with a platform-specific network bridge.
     * Call this before play() to enable Lingo's xtra("Multiuser") functionality.
     */
    public void registerMultiuserXtra(MultiuserNetBridge netBridge) {
        MultiuserXtra multiuserXtra = new MultiuserXtra(netBridge, (target, handlerName, args) -> {
            if (target instanceof Datum.ScriptInstance si) {
                invokeOnScriptInstance(si, handlerName, args);
            } else {
                try {
                    vm.callHandler(handlerName, args);
                } catch (Exception e) {
                    System.err.println("[MultiuserXtra] Callback error: " + e.getMessage());
                }
            }
        });
        xtraManager.registerXtra(multiuserXtra);
    }

    /**
     * Invoke a handler on a script instance, walking the ancestor chain.
     * Shared utility used by timeout callbacks, Xtra callbacks, etc.
     */
    private void invokeOnScriptInstance(Datum.ScriptInstance target,
                                         String handlerName, java.util.List<Datum> args) {
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            try { vm.callHandler(handlerName, args); }
            catch (Exception e) {
                System.err.println("[Player] Error in script callback " + handlerName + ": " + e.getMessage());
            }
            return;
        }

        Datum.ScriptInstance current = target;
        for (int i = 0; i < 20; i++) {
            Datum scriptRefDatum = current.properties().get(Datum.PROP_SCRIPT_REF);
            CastLibProvider.HandlerLocation location;

            if (scriptRefDatum instanceof Datum.ScriptRef sr) {
                location = provider.findHandlerInScript(sr.castLibNum(), sr.memberNum(), handlerName);
            } else {
                location = provider.findHandlerInScript(current.scriptId(), handlerName);
            }

            if (location != null && location.script() instanceof ScriptChunk script
                    && location.handler() instanceof ScriptChunk.Handler handler) {
                try {
                    vm.executeHandler(script, handler, args, target);
                } catch (Exception e) {
                    System.err.println("[Player] Error in script callback " + handlerName + ": " + e.getMessage());
                }
                return;
            }

            Datum ancestor = current.properties().get(Datum.PROP_ANCESTOR);
            if (ancestor instanceof Datum.ScriptInstance ancestorInstance) {
                current = ancestorInstance;
            } else {
                break;
            }
        }

        try { vm.callHandler(handlerName, args); }
        catch (Exception e) {
            System.err.println("[Player] Error in script callback " + handlerName + " (global): " + e.getMessage());
        }
    }

    public MovieProperties getMovieProperties() {
        return movieProperties;
    }

    public CastLibManager getCastLibManager() {
        return castLibManager;
    }

    /**
     * Called when a network fetch completes. If the fetched URL is a cast file
     * (.cct/.cst), caches the raw data in CastLibManager and parses it into
     * the matching cast library so members are available immediately.
     */
    public void onNetFetchComplete(String url, byte[] data) {
        if (url == null || data == null) return;
        String lower = url.toLowerCase();
        int qi = lower.indexOf('?');
        if (qi > 0) lower = lower.substring(0, qi);
        if (!lower.endsWith(".cct") && !lower.endsWith(".cst")) return;

        handleExternalCastFetch(url, data);
    }

    public SoundManager getSoundManager() {
        return soundManager;
    }

    /**
     * Set audio backend for sound playback.
     * Call before play() to enable sound. Platform-specific:
     * - player-swing: SwingAudioBackend (javax.sound.sampled)
     * - player-wasm: WasmAudioBackend (Web Audio API via JS bridge)
     */
    public void setAudioBackend(AudioBackend backend) {
        soundManager.setBackend(backend);
    }

    /**
     * Set external parameters (Shockwave PARAM tags).
     * These are accessible to Lingo scripts via externalParamValue(), etc.
     */
    public void setExternalParams(Map<String, String> params) {
        externalParams.clear();
        if (params != null) {
            externalParams.putAll(params);
        }
    }

    /**
     * Get external parameters.
     */
    public Map<String, String> getExternalParams() {
        return Collections.unmodifiableMap(externalParams);
    }

    public BitmapResolver getBitmapResolver() {
        return bitmapResolver;
    }

    public void onSynchronousExternalCastLoad(int castLibNumber) {
        if (castLibNumber <= 0) {
            return;
        }
        bitmapCache.clear();
        vm.invalidateHandlerCache();
        stageRenderer.getSpriteRegistry().bumpRevision();
        notifyExternalCastLoaded(castLibNumber);
    }

    public boolean loadExternalCastFromCachedData(int castLibNumber, byte[] data) {
        return loadExternalCastFromCachedData(castLibNumber, data, null);
    }

    public boolean loadExternalCastFromCachedData(int castLibNumber, byte[] data, Runnable afterLoad) {
        if (castLibNumber <= 0 || data == null || data.length == 0) {
            return false;
        }
        // Director exposes the new cast contents immediately once a slot is
        // fulfilled; deferring the swap breaks movie code that reindexes or
        // initializes the cast on the next line after setting castLib.fileName.
        return applyExternalCastDataNow(castLibNumber, data, afterLoad);
    }

    private boolean applyExternalCastDataNow(int castLibNumber, byte[] data, Runnable afterLoad) {
        if (!castLibManager.setExternalCastData(castLibNumber, data)) {
            return false;
        }
        onSynchronousExternalCastLoad(castLibNumber);
        if (afterLoad != null) {
            afterLoad.run();
        }
        return true;
    }

    public void addExternalCastLoadHandler(ExternalCastLoadHandler handler) {
        if (handler != null) {
            externalCastLoadHandlers.add(handler);
        }
    }

    private void notifyExternalCastLoaded(int castLibNumber) {
        var castLib = castLibManager.getCastLib(castLibNumber);
        if (castLib == null) {
            return;
        }

        String fileName = castLib.getFileName();
        LifecycleDiagnostics.logExternalCastLoaded(castLibNumber, fileName);
        ExternalCastLoadEvent event = new ExternalCastLoadEvent(castLibNumber, fileName);
        compatibilityProfile.onExternalCastLoaded(this, castLibNumber, fileName);
        for (ExternalCastLoadHandler handler : externalCastLoadHandlers) {
            handler.onExternalCastLoaded(this, castLibNumber, fileName);
        }
        if (externalCastLoadListener != null) {
            externalCastLoadListener.accept(event);
        }
        if (castLoadedListener != null) {
            castLoadedListener.run();
        }
    }

    public CursorManager getCursorManager() {
        return cursorManager;
    }

    public InputHandler getInputHandler() {
        return inputHandler;
    }

    public TimeoutManager getTimeoutManager() {
        return timeoutManager;
    }

    public void setCompatibilityProfile(PlayerCompatibilityProfile compatibilityProfile) {
        this.compatibilityProfile = compatibilityProfile != null
                ? compatibilityProfile
                : PlayerCompatibilityProfile.NONE;
    }

    /**
     * Register a builtin variable value that should exist before authored movie
     * startup handlers run. This is launcher/bootstrap configuration, not movie
     * compatibility logic.
     */
    public void setInitialBuiltinVariable(String variableName, Datum defaultValue) {
        if (variableName == null || variableName.isEmpty()) {
            return;
        }
        initialBuiltinVariables.put(variableName,
                defaultValue != null ? defaultValue.deepCopy() : Datum.VOID);
    }

    /**
     * Replace all configured builtin bootstrap variables.
     */
    public void setInitialBuiltinVariables(Map<String, Datum> values) {
        initialBuiltinVariables.clear();
        if (values == null || values.isEmpty()) {
            return;
        }
        for (var entry : values.entrySet()) {
            setInitialBuiltinVariable(entry.getKey(), entry.getValue());
        }
    }

    /**
     * Preload all external cast libraries.
     * This triggers async loading of any casts that have external paths/URLs.
     * Use this to make all scripts available for debugging before playback starts.
     * @return The number of casts that were queued for loading
     */
    public int preloadAllCasts() {
        NetBuiltins.NetProvider provider = overrideNetProvider != null ? overrideNetProvider : netManager;
        if (provider == null) return 0;
        int count = 0;
        for (var entry : castLibManager.getCastLibs().entrySet()) {
            var castLib = entry.getValue();
            if (castLib.isExternal() && !castLib.isLoaded() && !castLib.isFetching()) {
                String rawPath = castLib.getFileName();
                if (rawPath != null && !rawPath.isEmpty()) {
                    // Normalize Mac colon-separated paths (e.g. "Sulake:...:mobiles.cct") to just filename
                    String fileName = FileUtil.getFileName(rawPath);
                    castLib.markFetching();
                    provider.preloadNetThing(fileName);
                    count++;
                }
            }
        }
        return count;
    }

    public PlayerState getState() {
        return state;
    }

    public int getCurrentFrame() {
        return frameContext.getCurrentFrame();
    }

    /**
     * Get the effective tempo (frames per second).
     * Priority: puppetTempo > score tempo channel > config tempo.
     */
    public int getTempo() {
        int puppetTempo = movieProperties.getPuppetTempo();
        if (puppetTempo > 0) {
            return puppetTempo;
        }
        // Check per-frame tempo from score's tempo channel (0-indexed)
        if (file != null) {
            int scoreTempo = file.getScoreTempo(frameContext.getCurrentFrame() - 1);
            if (scoreTempo > 0) {
                return scoreTempo;
            }
        }
        return tempo;
    }

    /**
     * Set the base tempo (from score).
     * This can be overridden by puppetTempo.
     */
    public void setTempo(int tempo) {
        this.tempo = tempo > 0 ? tempo : 15;
        inputState.setCaretBlinkRate(this.tempo);
    }

    /**
     * Get the base tempo (ignoring puppetTempo).
     */
    public int getBaseTempo() {
        return tempo;
    }

    public int getFrameCount() {
        return frameContext.getFrameCount();
    }

    /**
     * Get a snapshot of the current frame for rendering.
     * This captures all sprite states at the moment it's called.
     */
    public FrameSnapshot getFrameSnapshot() {
        int frame = getCurrentFrame();
        FrameSnapshot snapshot = frameRenderPipeline.renderFrame(frame);
        return new FrameSnapshot(
            snapshot.frameNumber(),
            snapshot.stageWidth(),
            snapshot.stageHeight(),
            snapshot.backgroundColor(),
            snapshot.sprites(),
            String.format("Frame %d | %s", frame, state.name()),
            snapshot.stageImage(),
            snapshot.bakeTick(),
            snapshot.pipelineTrace()
        );
    }

    /**
     * Get the bitmap cache (for external cache management).
     */
    public BitmapCache getBitmapCache() {
        return bitmapCache;
    }

    public void setEventListener(Consumer<PlayerEventInfo> listener) {
        this.eventListener = listener;
    }

    /**
     * Set a listener to be notified when external cast libraries are loaded.
     * Compatibility API: use setExternalCastLoadListener for a typed event.
     */
    public void setCastLoadedListener(Runnable listener) {
        this.castLoadedListener = listener;
    }

    /**
     * Set a generic listener for external cast load completion.
     * This exposes the cast identity without embedding movie-specific logic in Player.
     */
    public void setExternalCastLoadListener(Consumer<ExternalCastLoadEvent> listener) {
        this.externalCastLoadListener = listener;
    }

    /**
     * Set a listener for Lingo script errors.
     * Called with the error message and the Lingo call stack at the point of the error.
     * The LingoException carries the call stack — use {@code e.getLingoCallStack()} or
     * {@code e.formatLingoCallStack()} to inspect it.
     */
    public void setErrorListener(java.util.function.BiConsumer<String, com.libreshockwave.vm.datum.LingoException> listener) {
        this.errorListener = listener;
    }

    /**
     * Get the current Lingo call stack. Safe to call at any time.
     * Returns an empty list when no handlers are executing.
     */
    public List<LingoVM.CallStackFrame> getLingoCallStack() {
        return vm.getCallStack();
    }

    /**
     * Get the current Lingo call stack as a formatted string.
     * Returns "(empty)" when no handlers are executing.
     */
    public String formatLingoCallStack() {
        return vm.formatCallStack();
    }

    public String getRecentScriptErrorMessage(long maxAgeMs) {
        if (maxAgeMs < 0) {
            maxAgeMs = 0;
        }
        long age = System.currentTimeMillis() - lastScriptErrorTimeMs;
        return lastScriptErrorTimeMs > 0 && age <= maxAgeMs ? lastScriptErrorMessage : "";
    }

    public String getRecentScriptErrorStack(long maxAgeMs) {
        if (maxAgeMs < 0) {
            maxAgeMs = 0;
        }
        long age = System.currentTimeMillis() - lastScriptErrorTimeMs;
        return lastScriptErrorTimeMs > 0 && age <= maxAgeMs ? lastScriptErrorStack : "";
    }

    public void setDebugEnabled(boolean enabled) {
        this.debugEnabled = enabled;
        frameContext.setDebugEnabled(enabled);
        if (enabled) {
            dumpScriptInfo();
        }
    }

    /**
     * Set the debug controller for bytecode-level debugging.
     * The controller will receive TraceListener callbacks and can pause/step the VM.
     * The controller is installed as a delegate inside our PlayerTraceListener so core
     * compatibility hooks continue to work alongside debugging.
     */
    public void setDebugController(DebugControllerApi controller) {
        this.debugController = controller;
        playerTraceListener.setDelegate(controller);
    }

    /**
     * Get the debug controller.
     */
    public DebugControllerApi getDebugController() {
        return debugController;
    }

    /**
     * Check if VM is currently running on background thread.
     */
    public boolean isVmRunning() {
        return vmRunning.get();
    }

    /**
     * Get or lazily create the VM executor service.
     * Only used by async methods (playAsync, tickAsync, stepFrameAsync, shutdown).
     */
    private ExecutorService getVmExecutor() {
        if (vmExecutor == null) {
            ExecutorService exec = Executors.newSingleThreadExecutor(r -> {
                Thread t = new Thread(r, "LingoVM-Executor");
                t.setDaemon(true);
                return t;
            });
            vmExecutor = exec;
            vmExecutorShutdown = () -> {
                exec.shutdown();
                try {
                    if (!exec.awaitTermination(2, TimeUnit.SECONDS)) {
                        exec.shutdownNow();
                    }
                } catch (InterruptedException e) {
                    exec.shutdownNow();
                    Thread.currentThread().interrupt();
                }
            };
        }
        return vmExecutor;
    }

    /**
     * Set an override network provider. When set, this provider is used
     * instead of the default NetManager in setupProviders().
     * Used by player-wasm to substitute FetchNetManager for browser fetch().
     */
    public void setNetProvider(NetBuiltins.NetProvider provider) {
        this.overrideNetProvider = provider;
    }

    /**
     * Dump information about loaded scripts for debugging.
     */
    public void dumpScriptInfo() {
        if (file == null) {
            System.out.println("[Player] No file loaded");
            return;
        }

        ScriptNamesChunk names = file.getScriptNames();
        System.out.println("[Player] === Script Summary ===");
        System.out.println("[Player] Total scripts: " + file.getScripts().size());

        int movieScripts = 0;
        int behaviors = 0;
        int parents = 0;
        int unknown = 0;

        for (ScriptChunk script : file.getScripts()) {
            String typeName = script.getScriptType() != null ? script.getScriptType().name() : "null";
            switch (script.getScriptType()) {
                case MOVIE_SCRIPT -> movieScripts++;
                case BEHAVIOR -> behaviors++;
                case PARENT -> parents++;
                default -> unknown++;
            }

            // List handlers in movie scripts
            if (script.getScriptType() == ScriptChunk.ScriptType.MOVIE_SCRIPT) {
                System.out.println("[Player] Movie script #" + script.id() + " handlers:");
                for (ScriptChunk.Handler handler : script.handlers()) {
                    String handlerName = names != null ? names.getName(handler.nameId()) : "name#" + handler.nameId();
                    System.out.println("[Player]   - " + handlerName);
                }
            }
        }

        System.out.println("[Player] Movie scripts: " + movieScripts);
        System.out.println("[Player] Behaviors: " + behaviors);
        System.out.println("[Player] Parent scripts: " + parents);
        System.out.println("[Player] Unknown type: " + unknown);
        System.out.println("[Player] ======================");
    }

    // Frame labels (delegated to navigator)

    public int getFrameForLabel(String label) {
        return frameContext.getNavigator().getFrameForLabel(label);
    }

    public Set<String> getFrameLabels() {
        return frameContext.getNavigator().getFrameLabels();
    }

    // Playback control

    /**
     * Start playback from the beginning.
     * This runs synchronously - use playAsync() when debugger is enabled.
     */
    public void play() {
        boolean wasStopped = state == PlayerState.STOPPED;
        state = PlayerState.PLAYING;  // Set first so tick() works even if prepareMovie errors
        if (wasStopped) {
            prepareMovie();
        }
        log("play()");
    }

    /**
     * Start playback asynchronously on background thread.
     * Use this when the debugger is enabled to prevent UI freezing.
     * @param onReady Callback when movie is prepared and ready to play (called on VM thread)
     */
    public void playAsync(Runnable onReady) {
        if (state != PlayerState.STOPPED) {
            state = PlayerState.PLAYING;
            if (onReady != null) onReady.run();
            return;
        }

        vmRunning.set(true);
        getVmExecutor().submit(() -> {
            try {
                prepareMovie();
                state = PlayerState.PLAYING;
                log("playAsync()");
            } finally {
                vmRunning.set(false);
                if (onReady != null) {
                    onReady.run();
                }
            }
        });
    }

    /**
     * Pause playback at the current frame.
     */
    public void pause() {
        if (state == PlayerState.PLAYING) {
            state = PlayerState.PAUSED;
            log("pause()");
        }
    }

    /**
     * Resume playback from a paused state.
     */
    public void resume() {
        if (state == PlayerState.PAUSED) {
            state = PlayerState.PLAYING;
            log("resume()");
        }
    }

    /**
     * Stop playback and reset to frame 1.
     */
    public void stop() {
        if (state != PlayerState.STOPPED) {
            log("stop()");
            setupProviders();
            try {
                // stopMovie -> timeout targets first, then movie scripts
                timeoutManager.dispatchSystemEvent(vm, "stopMovie");
                frameContext.getEventDispatcher().dispatchToMovieScripts(PlayerEvent.STOP_MOVIE, List.of());
            } finally {
                clearProviders();
            }
            frameContext.reset();
            stageRenderer.reset();
            timeoutManager.clear();
            playerTraceListener.reset();
            state = PlayerState.STOPPED;
        }
    }

    /**
     * Go to a specific frame.
     */
    public void goToFrame(int frame) {
        frameContext.goToFrame(frame);
    }

    /**
     * Go to a labeled frame.
     */
    public void goToLabel(String label) {
        frameContext.goToLabel(label);
    }

    /**
     * Step forward one frame (manual advance).
     * This runs synchronously - use stepFrameAsync() when debugger is enabled.
     */
    public void stepFrame() {
        if (state == PlayerState.STOPPED) {
            prepareMovie();
            state = PlayerState.PAUSED;
        }

        setupProviders();
        try {
            frameContext.executeFrame();
            timeoutManager.processTimeouts(vm, System.currentTimeMillis());
            frameContext.advanceFrame();
        } finally {
            clearProviders();
        }
    }

    /**
     * Step forward one frame asynchronously on background thread.
     * Use this when the debugger is enabled to prevent UI freezing.
     * @param onComplete Callback when frame is complete (called on VM thread)
     */
    public void stepFrameAsync(Runnable onComplete) {
        if (vmRunning.get()) {
            return;
        }

        vmRunning.set(true);
        getVmExecutor().submit(() -> {
            try {
                if (state == PlayerState.STOPPED) {
                    prepareMovie();
                    state = PlayerState.PAUSED;
                }

                // Update debug controller with current globals
                if (debugController != null) {
                    debugController.setGlobalsSnapshot(vm.getGlobals());
                }

                // Execute frames in a loop - if stepping completes without pausing
                // (e.g., handler/frame ended on last instruction), continue to
                // the next frame so stepping can pause on its first instruction
                do {
                    setupProviders();
                    try {
                        inputHandler.processInputEvents();
                        frameContext.executeFrame();
                        // Process Xtra callbacks after frame execution so room
                        // object creation sees any cast updates from this frame.
                        xtraManager.tickAll();
                        compatibilityProfile.afterFrameExecution(this);
                        timeoutManager.processTimeouts(vm, System.currentTimeMillis());
                        frameContext.advanceFrame();
                    } finally {
                        clearProviders();
                    }
                } while (debugController != null && debugController.isAwaitingStepContinuation());
            } finally {
                vmRunning.set(false);
                if (onComplete != null) {
                    onComplete.run();
                }
            }
        });
    }

    // Frame execution (called by external timer/loop)

    /**
     * Execute one frame tick. Call this at tempo rate.
     * This is synchronous - for debugging support, use tickAsync() instead.
     * @return true if still playing, false if stopped/paused
     */
    public boolean tick() {
        if (state != PlayerState.PLAYING) {
            return state == PlayerState.PAUSED;
        }
        LifecycleDiagnostics.setEnabled(vm.getTracedHandlers().contains("lifecycle"));

        // Update debug controller with current globals
        if (debugController != null) {
            debugController.setGlobalsSnapshot(vm.getGlobals());
        }

        // Set up thread-local providers before script execution
        setupProviders();
        // Arm the tick-level deadline if configured (default 30s, 0 = disabled).
        long deadlineMs = vm.getTickDeadlineMs();
        if (deadlineMs > 0) {
            vm.setTickDeadline(System.currentTimeMillis() + deadlineMs);
        }
        try {
            // Process queued mouse/keyboard input events before frame execution
            inputHandler.processInputEvents();
            frameContext.executeFrame();
            // Process Xtra callbacks after frame execution so room object
            // creation sees any cast updates from this frame.
            xtraManager.tickAll();
            compatibilityProfile.afterFrameExecution(this);
            timeoutManager.processTimeouts(vm, System.currentTimeMillis());
            processUpdatingObjects();
            frameContext.advanceFrame();
        } finally {
            vm.setTickDeadline(0);
            clearProviders();
        }
        return true;
    }

    /**
     * Execute one frame tick asynchronously on a background thread.
     * Use this when the debugger is enabled to allow the VM to be paused
     * without blocking the UI thread.
     * @param onComplete Callback invoked on completion (on the VM thread)
     */
    public void tickAsync(Runnable onComplete) {
        if (state != PlayerState.PLAYING || vmRunning.get()) {
            return;
        }

        // Update debug controller with current globals
        if (debugController != null) {
            debugController.setGlobalsSnapshot(vm.getGlobals());
        }

        vmRunning.set(true);
        getVmExecutor().submit(() -> {
            try {
                // Execute frames in a loop - if stepping completes without pausing
                // (e.g., handler/frame ended on last instruction), continue to
                // the next frame so stepping can pause on its first instruction
                do {
                    setupProviders();
                    try {
                        inputHandler.processInputEvents();
                        frameContext.executeFrame();
                        // Process Xtra callbacks after frame execution so room
                        // object creation sees any cast updates from this frame.
                        xtraManager.tickAll();
                        compatibilityProfile.afterFrameExecution(this);
                        timeoutManager.processTimeouts(vm, System.currentTimeMillis());
                        processUpdatingObjects();
                        frameContext.advanceFrame();
                    } finally {
                        clearProviders();
                    }
                } while (debugController != null && debugController.isAwaitingStepContinuation());
            } finally {
                vmRunning.set(false);
                if (onComplete != null) {
                    onComplete.run();
                }
            }
        });
    }

    /**
     * Set up thread-local providers for builtin functions.
     */
    private void setupProviders() {
        NetBuiltins.setProvider(overrideNetProvider != null ? overrideNetProvider : netManager);
        XtraBuiltins.setManager(xtraManager);
        MoviePropertyProvider.setProvider(movieProperties);
        SpritePropertyProvider.setProvider(spriteProperties);
        CastLibProvider.setProvider(castLibManager);
        TimeoutProvider.setProvider(timeoutManager);
        UpdateProvider.setProvider(this);
        ExternalParamProvider.setProvider(externalParamProvider);
        SoundProvider.setProvider(soundManager);
        // Refresh palette each tick so Datum colour resolution uses the current frame's palette.
        // Only override if no puppetPalette is active — puppetPalette takes priority over
        // the score's palette channel, matching Director's behavior.
        if (!com.libreshockwave.vm.datum.Datum.isPuppetPaletteActive()) {
            var moviePal = bitmapResolver.getMoviePalette();
            com.libreshockwave.vm.datum.Datum.setActivePalette(moviePal);
        }
    }

    /**
     * Clear thread-local providers after script execution.
     */
    private void clearProviders() {
        NetBuiltins.clearProvider();
        XtraBuiltins.clearManager();
        MoviePropertyProvider.clearProvider();
        SpritePropertyProvider.clearProvider();
        CastLibProvider.clearProvider();
        TimeoutProvider.clearProvider();
        UpdateProvider.clearProvider();
        ExternalParamProvider.clearProvider();
        SoundProvider.clearProvider();
    }

    // Movie lifecycle - follows dirplayer-rs flow exactly

    private void prepareMovie() {
        log("prepareMovie()");

        setupProviders();
        try {
            applyInitialBuiltinVariables();
            compatibilityProfile.beforePrepareMovie(this);

            // 0. Initiate fetch of all external casts (async network requests)
            preloadAllCasts();

            // 1. Preload casts with preloadMode=2 (BeforeFrameOne / MovieLoaded)
            // dirplayer-rs: Mode 2 = BeforeFrameOne, Mode 1 = AfterFrameOne
            castLibManager.preloadCasts(2);

            // 2. prepareMovie -> timeout targets first, then movie scripts
            timeoutManager.dispatchSystemEvent(vm, "prepareMovie");
            frameContext.getEventDispatcher().dispatchToMovieScripts(PlayerEvent.PREPARE_MOVIE, List.of());

            // prepareMovie handlers are allowed to change preloadMode and trigger preloadNetThing()
            // for external casts that frame-1 startup logic depends on. Make those casts visible
            // before beginSprite/prepareFrame/enterFrame rather than deferring until after the
            // entire first frame, otherwise first-frame bootstrap scripts can observe missing
            // handlers and variables from freshly requested startup casts.
            castLibManager.preloadCasts(1);

            // 3. Initialize sprites for frame 1
            frameContext.initializeFirstFrame();

            // 4. beginSprite events
            frameContext.dispatchBeginSpriteEvents();

            // 5. prepareFrame -> timeout targets first, then behaviors + frame/movie scripts
            timeoutManager.dispatchSystemEvent(vm, "prepareFrame");
            frameContext.getEventDispatcher().dispatchGlobalEvent(PlayerEvent.PREPARE_FRAME, List.of());

            // 6. startMovie -> movie scripts first, then timeout targets
            frameContext.getEventDispatcher().dispatchToMovieScripts(PlayerEvent.START_MOVIE, List.of());
            timeoutManager.dispatchSystemEvent(vm, "startMovie");

            // 7. enterFrame -> dispatched to all behaviors + frame/movie scripts
            frameContext.getEventDispatcher().dispatchGlobalEvent(PlayerEvent.ENTER_FRAME, List.of());

            // 8. exitFrame -> timeout targets first, then behaviors + frame/movie scripts
            timeoutManager.dispatchSystemEvent(vm, "exitFrame");
            frameContext.getEventDispatcher().dispatchGlobalEvent(PlayerEvent.EXIT_FRAME, List.of());

            // 9. Re-run preloadMode=1 pass in case first-frame scripts requested additional casts.
            castLibManager.preloadCasts(1);

            // Frame loop will handle subsequent frames
        } finally {
            clearProviders();
        }
    }

    void ensureBuiltinVariableValue(String variableName, Datum defaultValue) {
        Datum exists = vm.callBuiltin("variableExists", List.of(Datum.of(variableName)));
        if (exists.isTruthy()) {
            return;
        }
        vm.callBuiltin("setVariable", List.of(Datum.of(variableName), defaultValue));
    }

    private void applyInitialBuiltinVariables() {
        if (initialBuiltinVariables.isEmpty()) {
            return;
        }
        for (var entry : initialBuiltinVariables.entrySet()) {
            ensureBuiltinVariableValue(entry.getKey(), entry.getValue());
        }
    }

    /**
     * Called when Lingo sets castLib.fileName — loads the data from NetManager's
     * download cache into the specific cast by number.
     */
    private void loadCastFromNetCache(int castLibNumber, String fileName) {
        if (netManager != null) {
            byte[] data = netManager.getCachedData(fileName);
            if (data != null) {
                loadExternalCastFromCachedData(castLibNumber, data);
                return;
            }
        }
        String baseName = FileUtil.getFileNameWithoutExtension(FileUtil.getFileName(fileName));
        byte[] cached = castLibManager.getCachedExternalData(baseName);
        if (cached != null) {
            loadExternalCastFromCachedData(castLibNumber, cached);
        }
    }

    private void handleExternalCastFetch(String url, byte[] data) {
        castLibManager.cacheExternalData(url, data);
        try {
            java.util.List<Integer> requestedCastNums = castLibManager.getRequestedExternalCastSlots(url);
            if (requestedCastNums.isEmpty()) {
                return;
            }
            for (Integer castNum : requestedCastNums) {
                loadExternalCastFromCachedData(castNum, data);
            }
        } catch (Throwable e) {
            System.err.println("[Player] External cast load failed for " + url + ": "
                    + e.getClass().getName() + ": " + e.getMessage());
        }
    }

    private void handleCastDataRequest(int castLibNum, String fileName, java.util.function.BiConsumer<Integer, String> fallbackCallback) {
        String baseName = FileUtil.getFileNameWithoutExtension(FileUtil.getFileName(fileName));
        byte[] cached = castLibManager.getCachedExternalData(baseName);
        if (cached != null) {
            loadExternalCastFromCachedData(castLibNum, cached);
            return;
        }
        if (fallbackCallback != null) {
            fallbackCallback.accept(castLibNum, fileName);
        }
    }

    /**
     * Fire a test error through the VM's alertHook mechanism.
     * Sets up providers, fires the hook, then cleans up.
     * Used to verify error dialog appearance.
     * @return true if alertHook handled the error
     */
    public boolean fireTestError(String errorMsg) {
        setupProviders();
        try {
            return vm.fireAlertHook(errorMsg);
        } finally {
            clearProviders();
        }
    }

    // --- Input handling ---

    /**
     * Get the input state for reading mouse/keyboard state.
     */
    public InputState getInputState() {
        return inputState;
    }


    /**
     * Shutdown the player and release resources.
     * Call this when the player is no longer needed.
     */
    public void shutdown() {
        stop();
        if (netManager != null) {
            netManager.shutdown();
        }

        // Shutdown cast parser executor (only exists in desktop player)
        if (castParserShutdown != null) {
            castParserShutdown.run();
        }

        // Reset debug controller (releases any blocked threads)
        if (debugController != null) {
            debugController.reset();
        }

        // Shutdown VM executor (only if it was created)
        if (vmExecutorShutdown != null) {
            vmExecutorShutdown.run();
        }
    }

    // Debug logging

    private void log(String message) {
        if (debugEnabled) {
            System.out.println("[Player] " + message);
        }
    }

    /**
     * Delegating TraceListener that forwards callbacks to the active compatibility
     * profile and an optional debug delegate.
     */
    private class PlayerTraceListener implements TraceListener {

        private TraceListener delegate;

        void setDelegate(TraceListener delegate) {
            this.delegate = delegate;
        }

        void reset() {}

        @Override
        public void onHandlerEnter(HandlerInfo info) {
            LifecycleDiagnostics.logHandlerEnter(info);
            compatibilityProfile.onHandlerEnter(Player.this, info);
            if (delegate != null) delegate.onHandlerEnter(info);
        }

        @Override
        public void onHandlerExit(HandlerInfo info, Datum returnValue) {
            LifecycleDiagnostics.logHandlerExit(info, returnValue);
            compatibilityProfile.onHandlerExit(Player.this, info, returnValue);
            if (delegate != null) delegate.onHandlerExit(info, returnValue);
        }

        @Override
        public boolean needsInstructionTrace() {
            return delegate != null && delegate.needsInstructionTrace();
        }

        @Override
        public void onInstruction(InstructionInfo info) {
            if (delegate != null) delegate.onInstruction(info);
        }

        @Override
        public void onVariableSet(String type, String name, Datum value) {
            if (delegate != null) delegate.onVariableSet(type, name, value);
        }

        @Override
        public void onError(String message, Exception error) {
            LifecycleDiagnostics.logError(message, error);
            if (delegate != null) delegate.onError(message, error);
            lastScriptErrorTimeMs = System.currentTimeMillis();
            lastScriptErrorMessage = message != null ? message : "";
            if (error instanceof LingoException leForState) {
                lastScriptErrorStack = leForState.formatLingoCallStack();
            } else {
                lastScriptErrorStack = "";
            }
            if (errorListener != null) {
                LingoException le = error instanceof LingoException ? (LingoException) error : null;
                errorListener.accept(message, le);
            }
        }

        @Override
        public void onDebugMessage(String message) {
            if (delegate != null) delegate.onDebugMessage(message);
        }
    }

    // --- UpdateProvider implementation ---

    @Override
    public void receiveUpdate(Datum target) {
        if (target != null && !target.isVoid()) {
            if (!updatingObjects.contains(target)) {
                updatingObjects.add(target);
            }
        }
    }

    @Override
    public void removeUpdate(Datum target) {
        if (target != null) {
            updatingObjects.remove(target);
        }
    }

    private void processUpdatingObjects() {
        if (updatingObjects.isEmpty()) return;

        // Create a snapshot to allow removeUpdate() during iteration
        List<Datum> snapshot = new ArrayList<>(updatingObjects);
        for (Datum target : snapshot) {
            if (target instanceof Datum.ScriptInstance si) {
                ControlFlowBuiltins.callHandlerOnInstance(vm, si, "update", List.of());
            }
        }
    }
}
