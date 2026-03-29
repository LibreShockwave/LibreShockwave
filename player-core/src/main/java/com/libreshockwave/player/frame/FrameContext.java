package com.libreshockwave.player.frame;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.player.PlayerEvent;
import com.libreshockwave.player.behavior.BehaviorInstance;
import com.libreshockwave.player.behavior.BehaviorManager;
import com.libreshockwave.player.event.EventDispatcher;
import com.libreshockwave.player.render.SpriteRegistry;
import com.libreshockwave.player.score.ScoreBehaviorRef;
import com.libreshockwave.player.score.ScoreNavigator;
import com.libreshockwave.player.score.SpriteSpan;
import com.libreshockwave.player.sprite.SpriteState;
import com.libreshockwave.player.timeout.TimeoutManager;
import com.libreshockwave.util.ValueProvider;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.util.AncestorChainWalker;

import java.util.*;
import java.util.function.Consumer;

/**
 * Manages the execution context for a single frame.
 * Handles sprite initialization, event dispatch, and frame transitions.
 */
public class FrameContext {

    private final DirectorFile file;
    private final LingoVM vm;
    private final ScoreNavigator navigator;
    private final BehaviorManager behaviorManager;
    private final EventDispatcher eventDispatcher;

    private TimeoutManager timeoutManager;  // Set by Player for system event forwarding
    private ValueProvider<Datum> actorListSupplier;  // Provides _movie.actorList
    private SpriteRegistry spriteRegistry;  // For checking puppet state during frame transitions

    private int currentFrame = 1;
    private Integer pendingFrame = null;  // Set by go/jump commands
    private boolean inFrameScript = false;

    // Active sprite channels
    private final Set<Integer> activeChannels = new HashSet<>();
    private final Set<Integer> enteredChannels = new HashSet<>();

    // Debug logging
    private boolean debugEnabled = false;
    private Consumer<FrameEvent> eventListener;

    public FrameContext(DirectorFile file, LingoVM vm) {
        this.file = file;
        this.vm = vm;
        this.navigator = new ScoreNavigator(file);
        this.behaviorManager = new BehaviorManager(file);
        this.eventDispatcher = new EventDispatcher(file, vm, behaviorManager);
    }

    public void setDebugEnabled(boolean enabled) {
        this.debugEnabled = enabled;
        behaviorManager.setDebugEnabled(enabled);
        eventDispatcher.setDebugEnabled(enabled);
    }

    public void setEventListener(Consumer<FrameEvent> listener) {
        this.eventListener = listener;
    }

    public void setTimeoutManager(TimeoutManager timeoutManager) {
        this.timeoutManager = timeoutManager;
    }

    public void setActorListSupplier(ValueProvider<Datum> supplier) {
        this.actorListSupplier = supplier;
    }

    public void setSpriteRegistry(SpriteRegistry registry) {
        this.spriteRegistry = registry;
    }

    public int getCurrentFrame() {
        return currentFrame;
    }

    public int getFrameCount() {
        return navigator.getFrameCount();
    }

    public ScoreNavigator getNavigator() {
        return navigator;
    }

    public BehaviorManager getBehaviorManager() {
        return behaviorManager;
    }

    public EventDispatcher getEventDispatcher() {
        return eventDispatcher;
    }

    // Frame navigation

    /**
     * Go to a specific frame (queued for next advance).
     */
    public void goToFrame(int frame) {
        int max = getFrameCount();
        if (frame >= 1 && frame <= max) {
            pendingFrame = frame;
        }
    }

    /**
     * Force an immediate frame transition, bypassing the exitFrame handler.
     * Used when external navigation (e.g. mouse click → go to "label") must
     * override an exitFrame script that loops with "go to the frame".
     * In Director, user-initiated go() during mouse events takes priority.
     */
    public void forceGoToFrame(int frame) {
        int max = getFrameCount();
        if (frame < 1 || frame > max) return;

        int oldFrame = currentFrame;
        logEvent("forceGoToFrame: " + oldFrame + " -> " + frame);

        endSpritesLeavingFrame(oldFrame, frame);
        behaviorManager.clearFrameScript();

        currentFrame = frame;
        pendingFrame = null;  // Clear any pending to prevent exitFrame override
        enterFrame(frame);
    }

    /**
     * Go to a labeled frame.
     */
    public void goToLabel(String label) {
        int frame = navigator.getFrameForLabel(label);
        if (frame > 0) {
            goToFrame(frame);
        }
    }

    // Frame execution

    /**
     * Initialize the first frame (called on movie start).
     */
    public void initializeFirstFrame() {
        currentFrame = 1;
        pendingFrame = null;
        activeChannels.clear();
        enteredChannels.clear();
        behaviorManager.clear();

        logEvent("initializeFirstFrame");

        // Begin sprites for frame 1
        beginSpritesForFrame(currentFrame);

        // Create frame script instance if any
        initializeFrameScript(currentFrame);
    }

    /**
     * Execute one frame update cycle (for frame loop, not first frame).
     * First frame events are handled in Player.prepareMovie().
     * Returns true if frame was executed successfully.
     */
    public boolean executeFrame() {
        // Director broadcasts ALL frame events to actorList members, not just stepFrame.
        // Object Manager (in actorList) uses prepareFrame to poll download callbacks.
        List<Datum.ScriptInstance> actorSnapshot = getActorSnapshot();

        // 1. stepFrame -> actorList, then behaviors + frame/movie scripts
        dispatchToActorList(actorSnapshot, "stepFrame");
        dispatchEvent(PlayerEvent.STEP_FRAME);

        // 2. prepareFrame -> actorList, timeout targets, then behaviors + frame/movie scripts
        dispatchToActorList(actorSnapshot, "prepareFrame");
        if (timeoutManager != null) {
            timeoutManager.dispatchSystemEvent(vm, "prepareFrame");
        }
        dispatchEvent(PlayerEvent.PREPARE_FRAME);

        // 3. enterFrame -> actorList, then behaviors + frame/movie scripts
        dispatchToActorList(actorSnapshot, "enterFrame");
        inFrameScript = true;
        dispatchEvent(PlayerEvent.ENTER_FRAME);
        inFrameScript = false;

        return true;
    }

    /**
     * Dispatch beginSprite events to newly entered sprites.
     * Called from Player.prepareMovie() for first frame setup.
     */
    public void dispatchBeginSpriteEvents() {
        dispatchBeginSprite();
    }

    /**
     * Advance to the next frame (or pending frame if set).
     * Returns the new frame number.
     *
     * Matching dirplayer-rs: exitFrame events fire BEFORE computing the destination,
     * so that go(the frame) during exitFrame correctly sets pendingFrame for this cycle.
     */
    public int advanceFrame() {
        int oldFrame = currentFrame;

        // 1. Dispatch exitFrame events FIRST (scripts may call go() here)
        List<Datum.ScriptInstance> actorSnapshot = getActorSnapshot();
        dispatchToActorList(actorSnapshot, "exitFrame");
        if (timeoutManager != null) {
            timeoutManager.dispatchSystemEvent(vm, "exitFrame");
        }
        dispatchEvent(PlayerEvent.EXIT_FRAME);

        // 2. NOW decide destination (picks up pendingFrame set by go() during exitFrame)
        int newFrame;
        if (pendingFrame != null) {
            newFrame = pendingFrame;
            pendingFrame = null;
        } else {
            newFrame = currentFrame + 1;
        }

        int max = getFrameCount();
        if (newFrame > max) {
            newFrame = 1;  // Loop
        }

        // 3. Transition sprites and enter new frame
        if (newFrame != oldFrame) {
            logEvent("advanceFrame: " + oldFrame + " -> " + newFrame);

            endSpritesLeavingFrame(oldFrame, newFrame);
            behaviorManager.clearFrameScript();

            currentFrame = newFrame;
            enterFrame(newFrame);
        }

        return currentFrame;
    }

    /**
     * Enter a new frame (initialization).
     */
    private void enterFrame(int frame) {
        // Begin new sprites
        beginSpritesForFrame(frame);

        // Initialize frame script
        initializeFrameScript(frame);

        // Dispatch beginSprite events to newly entered sprites
        dispatchBeginSprite();

        // Clear entered set for next beginSprite dispatch
        enteredChannels.clear();
    }

    // Sprite management

    /**
     * Begin sprites that are active in the given frame.
     */
    private void beginSpritesForFrame(int frame) {
        List<SpriteSpan> spans = navigator.getActiveSprites(frame);

        for (SpriteSpan span : spans) {
            int channel = span.getChannel();

            if (!activeChannels.contains(channel)) {
                // New sprite entering
                activeChannels.add(channel);
                enteredChannels.add(channel);

                // Create behavior instances for this sprite
                for (ScoreBehaviorRef behaviorRef : span.getBehaviors()) {
                    behaviorManager.createInstance(behaviorRef, channel);
                }

                logEvent("beginSprite: channel " + channel);
            }
        }
    }

    /**
     * End sprites that are leaving when transitioning frames.
     * In Director, puppeted sprites persist across frame transitions —
     * they are NOT removed when their Score span ends.
     */
    private void endSpritesLeavingFrame(int oldFrame, int newFrame) {
        Set<Integer> newActiveChannels = navigator.getActiveChannels(newFrame);

        List<Integer> leaving = new ArrayList<>();
        for (int channel : activeChannels) {
            if (!newActiveChannels.contains(channel)) {
                // Check if this sprite is puppeted — puppeted sprites persist
                if (spriteRegistry != null) {
                    SpriteState state = spriteRegistry.get(channel);
                    if (state != null && state.isPuppet()) {
                        logEvent("endSprite SKIPPED (puppeted): channel " + channel);
                        continue;
                    }
                }
                leaving.add(channel);
            }
        }

        for (int channel : leaving) {
            // Dispatch endSprite event
            eventDispatcher.dispatchSpriteEvent(channel, PlayerEvent.END_SPRITE, List.of());

            // Mark instances as ended
            for (BehaviorInstance instance : behaviorManager.getInstancesForChannel(channel)) {
                instance.setEndSpriteCalled(true);
            }

            // Remove from active set
            activeChannels.remove(channel);
            behaviorManager.removeInstancesForChannel(channel);
            if (spriteRegistry != null) {
                spriteRegistry.remove(channel);
            }

            logEvent("endSprite: channel " + channel);
        }
    }

    /**
     * Initialize the frame script for the given frame.
     */
    private void initializeFrameScript(int frame) {
        ScoreBehaviorRef frameScript = navigator.getFrameScript(frame);
        if (frameScript != null) {
            behaviorManager.getOrCreateFrameScript(frameScript, frame);
            logEvent("initializeFrameScript: frame " + frame + " -> " + frameScript);
        } else {
            logEvent("initializeFrameScript: frame " + frame + " has no frame script");
        }


    }

    /**
     * Get a snapshot of the current actorList for frame event dispatch.
     */
    private List<Datum.ScriptInstance> getActorSnapshot() {
        if (actorListSupplier == null) return List.of();
        Datum list = actorListSupplier.get();
        if (!(list instanceof Datum.List actors)) return List.of();
        List<Datum.ScriptInstance> result = new ArrayList<>();
        for (Datum item : actors.items()) {
            if (item instanceof Datum.ScriptInstance si) result.add(si);
        }
        return result;
    }

    /**
     * Dispatch a frame event to all actorList members by walking each object's ancestor chain.
     * Director broadcasts all frame events (stepFrame, prepareFrame, enterFrame, exitFrame)
     * to actorList members, not just stepFrame.
     */
    private void dispatchToActorList(List<Datum.ScriptInstance> actors, String handlerName) {
        if (actors.isEmpty()) return;

        for (Datum.ScriptInstance instance : actors) {
            try {
                AncestorChainWalker.invokeHandler(vm, instance, handlerName, List.of(instance));
            } catch (Exception ignored) {
                // Silently skip errors in actorList event dispatch
            }
        }
    }

    // Event dispatch

    /**
     * Dispatch a player event.
     */
    private void dispatchEvent(PlayerEvent event) {
        eventDispatcher.dispatchGlobalEvent(event, List.of());
        notifyEvent(event);
    }

    /**
     * Dispatch beginSprite to newly entered sprites.
     */
    private void dispatchBeginSprite() {
        for (int channel : enteredChannels) {
            List<BehaviorInstance> instances = behaviorManager.getInstancesForChannel(channel);
            for (BehaviorInstance instance : instances) {
                if (!instance.isBeginSpriteCalled()) {
                    eventDispatcher.dispatchSpriteEvent(channel, PlayerEvent.BEGIN_SPRITE, List.of());
                    instance.setBeginSpriteCalled(true);
                }
            }
        }

        // Also dispatch beginSprite for frame behavior if new
        BehaviorInstance frameInstance = behaviorManager.getFrameScriptInstance();
        if (frameInstance != null && !frameInstance.isBeginSpriteCalled()) {
            eventDispatcher.dispatchFrameAndMovieEvent(PlayerEvent.BEGIN_SPRITE, List.of());
            frameInstance.setBeginSpriteCalled(true);
        }
    }

    // Debug and notification

    private void logEvent(String message) {
        if (debugEnabled) {
            System.out.println("[FrameContext] " + message);
        }
    }

    private void notifyEvent(PlayerEvent event) {
        if (eventListener != null) {
            eventListener.accept(new FrameEvent(event, currentFrame));
        }
    }

    /**
     * Reset the context (called on stop).
     */
    public void reset() {
        currentFrame = 1;
        pendingFrame = null;
        activeChannels.clear();
        enteredChannels.clear();
        behaviorManager.clear();
        inFrameScript = false;
    }

}
