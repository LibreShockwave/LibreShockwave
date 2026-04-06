package com.libreshockwave.vm;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.vm.datum.Datum;

import java.util.List;
import java.util.Set;
import java.util.function.Consumer;

/**
 * Handles Director's alertHook mechanism for Lingo error suppression.
 * When a script error occurs, Director calls the alertHook handler set on the movie.
 * If the handler returns true, the error is suppressed.
 * Also tracks error handler depth to prevent recursive error handling.
 * Extracted from LingoVM.
 */
public class AlertHookHandler {

    static final Set<String> ERROR_HANDLER_NAMES = Set.of("alerthook");

    // Track if we're currently inside an error handler to prevent recursive error handling
    private int errorHandlerDepth = 0;

    // Debug callback for error handler depth tracing
    private Consumer<String> errorHandlerSkipCallback;

    public void setErrorHandlerSkipCallback(Consumer<String> callback) {
        this.errorHandlerSkipCallback = callback;
    }

    public int getErrorHandlerDepth() {
        return errorHandlerDepth;
    }

    public void incrementDepth() {
        errorHandlerDepth++;
    }

    public void decrementDepth() {
        errorHandlerDepth--;
    }

    /**
     * Check if the given handler is an error handler and if it should be skipped
     * due to recursion. Logs via the skip callback if set.
     * @return true if the handler should be skipped (recursive error handler)
     */
    public boolean shouldSkipErrorHandler(String handlerName, List<Datum> args) {
        String hn = handlerName.toLowerCase();
        boolean isErrorHandler = ERROR_HANDLER_NAMES.contains(hn);
        if (!isErrorHandler) return false;

        if (errorHandlerDepth > 0) {
            if (errorHandlerSkipCallback != null) {
                errorHandlerSkipCallback.accept("SKIP:" + handlerName + " depth=" + errorHandlerDepth);
            }
            return true;
        }

        if (errorHandlerSkipCallback != null) {
            String argStr = args.size() > 1 ? " msg=" + args.get(1) : "";
            errorHandlerSkipCallback.accept("ENTER:" + handlerName + " depth=" + errorHandlerDepth + argStr);
        }
        return false;
    }

    /**
     * Check if the given handler name is an error handler.
     */
    public boolean isErrorHandler(String handlerName) {
        return ERROR_HANDLER_NAMES.contains(handlerName.toLowerCase());
    }

    /**
     * Fire the alertHook handler if one is set.
     * @param errorType The error type/method string to pass to the handler
     * @param errorMsg The error message to pass to the handler
     * @param executeHandler callback to execute the handler
     * @return true if the error was handled (suppressed), false otherwise
     */
    public boolean fireAlertHook(String errorType,
                                  String errorMsg,
                                  HandlerExecutor executeHandler) {
        if (errorHandlerDepth > 0) {
            return false; // Prevent recursion
        }

        var provider = com.libreshockwave.vm.builtin.movie.MoviePropertyProvider.getProvider();
        if (provider == null) {
            return false;
        }

        Datum hookValue = provider.getMovieProp("alertHook");
        if (hookValue == null || hookValue.isVoid()) {
            return false;
        }

        if (!(hookValue instanceof Datum.ScriptInstance hookInstance)) {
            return false;
        }

        // Find the "alertHook" handler in the instance's script
        Datum.ScriptRef scriptRef = null;
        Datum scriptRefDatum = hookInstance.properties().get(Datum.PROP_SCRIPT_REF);
        if (scriptRefDatum instanceof Datum.ScriptRef sr) {
            scriptRef = sr;
        }

        var castProvider = com.libreshockwave.vm.builtin.cast.CastLibProvider.getProvider();
        if (castProvider == null) {
            return false;
        }

        com.libreshockwave.vm.builtin.cast.CastLibProvider.HandlerLocation location;
        if (scriptRef != null) {
            location = castProvider.findHandlerInScript(scriptRef.castLibNum(), scriptRef.memberNum(), "alertHook");
        } else {
            location = castProvider.findHandlerInScript(hookInstance.scriptId(), "alertHook");
        }

        if (location == null || !(location.script() instanceof ScriptChunk script)
                || !(location.handler() instanceof ScriptChunk.Handler handler)) {
            return false;
        }

        try {
            List<Datum> alertArgs = List.of(
                    Datum.of(errorType != null ? errorType : ""),
                    Datum.of(errorMsg != null ? errorMsg : "")
            );
            Datum result = executeHandler.execute(script, handler, alertArgs, hookInstance);
            return result != null && result.isTruthy();
        } catch (Exception e) {
            System.err.println("[LingoVM] alertHook handler failed: " + e.getMessage());
            return false;
        }
    }

    /**
     * Functional interface for executing a handler (passed from LingoVM).
     */
    @FunctionalInterface
    public interface HandlerExecutor {
        Datum execute(ScriptChunk script, ScriptChunk.Handler handler,
                      List<Datum> args, Datum receiver);
    }
}
