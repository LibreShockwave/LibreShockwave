package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.input.InputState;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;

import java.time.LocalDate;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.function.BiConsumer;

/**
 * Provides movie-level property access for Lingo scripts.
 * Implements the "the" property expressions like:
 * - the frame
 * - the moviePath
 * - the stageRight/stageLeft/stageTop/stageBottom
 * - the exitLock
 * etc.
 */
public class MovieProperties implements MoviePropertyProvider {

    private final Player player;
    private final DirectorFile file;

    // Writable movie properties
    private boolean exitLock = false;
    private boolean updateLock = false;
    private String itemDelimiter = ",";
    private int puppetTempo = 0;
    private boolean traceScript = false;
    private String traceLogFile = "";
    private boolean allowCustomCaching = false;
    private Datum alertHook = Datum.VOID;
    private Datum cursor = Datum.of(-1);
    private int floatPrecision = 4;

    // Stage properties
    private String stageTitle = "";

    // actorList: objects in this list receive stepFrame on each frame advance
    private Datum actorList = new Datum.List(new java.util.ArrayList<>());

    // Input state (mouse/keyboard)
    private InputState inputState;

    // Timer start time (in millis)
    private final long startTime;
    private BiConsumer<String, String> gotoNetPageHandler;

    public MovieProperties(Player player, DirectorFile file) {
        this.player = player;
        this.file = file;
        this.startTime = System.currentTimeMillis();
    }

    public void setInputState(InputState inputState) {
        this.inputState = inputState;
    }

    public void setGotoNetPageHandler(BiConsumer<String, String> gotoNetPageHandler) {
        this.gotoNetPageHandler = gotoNetPageHandler;
    }

    @Override
    public Datum getMovieProp(String propName) {
        // Normalize property name to lowercase for matching
        String prop = propName.toLowerCase();

        // TeaVM v0.13 workaround: Java keywords/literals cannot be used as string switch case labels
        if ("return".equals(prop)) return Datum.of("\r");
        if ("void".equals(prop)) return Datum.VOID;
        if ("true".equals(prop)) return Datum.TRUE;
        if ("false".equals(prop)) return Datum.FALSE;

        return switch (prop) {
            // Frame and playback
            case "frame" -> Datum.of(player.getCurrentFrame());
            case "lastframe" -> Datum.of(player.getFrameCount());
            case "lastchannel" -> Datum.of(file != null ? file.getChannelCount() : 0);

            // Stage dimensions
            case "stageright" -> Datum.of(file != null ? file.getStageWidth() : 0);
            case "stageleft" -> Datum.of(0);
            case "stagetop" -> Datum.of(0);
            case "stagebottom" -> Datum.of(file != null ? file.getStageHeight() : 0);

            // Movie info
            case "moviename" -> Datum.of(getMovieName());
            case "moviepath" -> Datum.of(getMoviePath());
            case "path" -> Datum.of(file != null ? file.getBasePath() : "");

            // System info
            case "platform" -> Datum.of("Windows,32");
            case "runmode" -> Datum.of("Plugin");
            case "productversion" -> Datum.of("10.1");
            case "environment" -> Datum.of("Java");

            // Date and time
            case "date" -> Datum.of(LocalDate.now().format(DateTimeFormatter.ofPattern("MM/dd/yyyy")));
            case "short date" -> Datum.of(LocalDate.now().format(DateTimeFormatter.ofPattern("M/d/yy")));
            case "long date" -> Datum.of(LocalDate.now().format(DateTimeFormatter.ofPattern("MMMM d, yyyy")));
            case "time" -> Datum.of(LocalTime.now().format(DateTimeFormatter.ofPattern("h:mm a")));
            case "short time" -> Datum.of(LocalTime.now().format(DateTimeFormatter.ofPattern("h:mm a")));
            case "long time" -> Datum.of(LocalTime.now().format(DateTimeFormatter.ofPattern("h:mm:ss a")));

            // Timer (in ticks - 60ths of a second)
            case "timer", "ticks" -> {
                long elapsed = System.currentTimeMillis() - startTime;
                int ticks = (int) ((elapsed * 60) / 1000);
                yield Datum.of(ticks);
            }
            case "milliseconds" -> Datum.of((int) (System.currentTimeMillis() - startTime));

            // Writable properties
            case "exitlock" -> Datum.of(exitLock ? 1 : 0);
            case "updatelock" -> Datum.of(updateLock ? 1 : 0);
            case "itemdelimiter" -> Datum.of(itemDelimiter);
            case "puppettempo" -> Datum.of(puppetTempo);
            case "tracescript" -> Datum.of(traceScript ? 1 : 0);
            case "tracelogfile" -> Datum.of(traceLogFile);
            case "allowcustomcaching" -> Datum.of(allowCustomCaching ? 1 : 0);
            case "alerthook" -> alertHook;
            case "cursor" -> cursor;
            case "floatprecision" -> Datum.of(floatPrecision);

            // actorList
            case "actorlist" -> actorList;

            // Tempo
            case "framerate", "tempo", "frametempo" -> Datum.of(player.getTempo());

            // Mouse state
            case "mousedown" -> Datum.of(inputState != null && inputState.isMouseDown() ? 1 : 0);
            case "mouseup" -> Datum.of(inputState == null || !inputState.isMouseDown() ? 1 : 0);
            case "mouseh" -> Datum.of(inputState != null ? inputState.getMouseH() : 0);
            case "mousev" -> Datum.of(inputState != null ? inputState.getMouseV() : 0);
            case "clickon" -> Datum.of(inputState != null ? inputState.getClickOnSprite() : 0);
            case "clickloc" -> {
                if (inputState != null) {
                    yield new Datum.Point(inputState.getClickLocH(), inputState.getClickLocV());
                }
                yield new Datum.Point(0, 0);
            }
            case "mouseloc" -> {
                if (inputState != null) {
                    yield new Datum.Point(inputState.getMouseH(), inputState.getMouseV());
                }
                yield new Datum.Point(0, 0);
            }
            case "rightmousedown" -> Datum.of(inputState != null && inputState.isRightMouseDown() ? 1 : 0);
            case "rollover" -> Datum.of(inputState != null ? inputState.getRolloverSprite() : 0);

            // Key state
            case "key" -> Datum.of(inputState != null ? inputState.getLastKey() : "");
            case "keycode" -> Datum.of(inputState != null ? inputState.getLastKeyCode() : 0);
            case "keypressed" -> Datum.of(inputState != null ? inputState.getLastKeyCode() : 0);
            case "shiftdown" -> Datum.of(inputState != null && inputState.isShiftDown() ? 1 : 0);
            case "optiondown", "altdown" -> Datum.of(inputState != null && inputState.isAltDown() ? 1 : 0);
            case "commanddown", "controldown" -> Datum.of(inputState != null && inputState.isControlDown() ? 1 : 0);

            // Keyboard focus
            case "keyboardfocussprite" -> Datum.of(inputState != null ? inputState.getKeyboardFocusSprite() : 0);

            // Text selection
            case "selstart" -> Datum.of(inputState != null ? inputState.getSelStart() : 0);
            case "selend" -> Datum.of(inputState != null ? inputState.getSelEnd() : 0);

            // Color depth.
            // Director window scripts allocate intermediate buffers with
            // image(w, h, the colorDepth), so this must reflect the movie's
            // authored stage depth rather than a hardcoded modern 32-bit value.
            case "colordepth" -> {
                if (file != null && file.getConfig() != null) {
                    int depth = file.getConfig().bgColor();
                    if (depth == 1 || depth == 2 || depth == 4
                            || depth == 8 || depth == 16 || depth == 32) {
                        yield Datum.of(depth);
                    }
                }
                yield Datum.of(32);
            }

            // Anim2 properties
            case "perframehook" -> Datum.VOID;
            case "number of castmembers" -> {
                if (file != null) {
                    yield Datum.of(file.getCastMembers().size());
                }
                yield Datum.ZERO;
            }
            case "number of menus" -> Datum.ZERO;
            case "number of castlibs" -> {
                com.libreshockwave.vm.builtin.cast.CastLibProvider castProvider =
                    com.libreshockwave.vm.builtin.cast.CastLibProvider.getProvider();
                if (castProvider != null) {
                    yield Datum.of(castProvider.getCastLibCount());
                }
                if (file != null) {
                    yield Datum.of(file.getCasts().size());
                }
                yield Datum.ZERO;
            }
            case "number of xtras" -> Datum.ZERO;

            // Window/Stage
            case "stage" -> Datum.STAGE;

            // Misc
            case "emptystring" -> Datum.EMPTY_STRING;
            case "pi" -> Datum.of(Math.PI);
            case "enter" -> Datum.of("\n");
            case "tab" -> Datum.of("\t");
            case "quote" -> Datum.of("\"");
            case "backspace" -> Datum.of("\b");
            case "space" -> Datum.of(" ");

            // Execution context properties
            case "paramcount" -> {
                // Returns the number of arguments passed to the current handler
                var vm = player.getVM();
                if (vm != null) {
                    var scope = vm.getCurrentScope();
                    if (scope != null) {
                        yield Datum.of(scope.getArguments().size());
                    }
                }
                yield Datum.ZERO;
            }

            // Unknown property
            default -> {
                System.err.println("[MovieProperties] Unknown property: " + propName);
                yield Datum.VOID;
            }
        };
    }

    @Override
    public boolean setMovieProp(String propName, Datum value) {
        String prop = propName.toLowerCase();

        switch (prop) {
            case "exitlock" -> {
                exitLock = value.isTruthy();
                return true;
            }
            case "updatelock" -> {
                updateLock = value.isTruthy();
                return true;
            }
            case "itemdelimiter" -> {
                String s = value.toStr();
                itemDelimiter = s.isEmpty() ? "," : s.substring(0, 1);
                ItemDelimiterCache._char = itemDelimiter.charAt(0);
                return true;
            }
            case "puppettempo" -> {
                setPuppetTempo(value.toInt());
                return true;
            }
            case "tracescript" -> {
                traceScript = value.isTruthy();
                return true;
            }
            case "tracelogfile" -> {
                traceLogFile = value.toStr();
                return true;
            }
            case "allowcustomcaching" -> {
                allowCustomCaching = value.isTruthy();
                return true;
            }
            case "actorlist" -> {
                actorList = value;
                return true;
            }
            case "tempo", "framerate", "frametempo" -> {
                player.setTempo(value.toInt());
                return true;
            }
            case "keyboardfocussprite" -> {
                if (inputState != null) {
                    inputState.setKeyboardFocusSprite(value.toInt());
                }
                return true;
            }
            case "alerthook" -> {
                alertHook = value;
                return true;
            }
            case "cursor" -> {
                cursor = value != null ? value : Datum.of(-1);
                return true;
            }
            case "floatprecision" -> {
                floatPrecision = value.toInt();
                return true;
            }
            case "selstart" -> {
                if (inputState != null) {
                    inputState.setSelStart(value.toInt());
                }
                return true;
            }
            case "selend" -> {
                if (inputState != null) {
                    inputState.setSelEnd(value.toInt());
                }
                return true;
            }
            case "debugplaybackenabled" -> {
                // Accepted as no-op (matching dirplayer-rs TODO)
                return true;
            }
            default -> {
                System.err.println("[MovieProperties] Cannot set read-only property: " + propName);
                return false;
            }
        }
    }

    @Override
    public Datum getStageProp(String propName) {
        String prop = propName.toLowerCase();
        return switch (prop) {
            case "rect", "sourcerect", "drawrect" -> {
                int w = file != null ? file.getStageWidth() : 640;
                int h = file != null ? file.getStageHeight() : 480;
                yield new Datum.Rect(0, 0, w, h);
            }
            case "title" -> Datum.of(stageTitle);
            case "visible" -> Datum.TRUE;
            case "bgcolor" -> Datum.of(player.getStageRenderer().getBackgroundColor());
            case "image" -> {
                Bitmap stageImage = player.getStageRenderer().getStageImage();
                yield new Datum.ImageRef(stageImage);
            }
            default -> getMovieProp(propName);
        };
    }

    @Override
    public boolean setStageProp(String propName, Datum value) {
        String prop = propName.toLowerCase();
        return switch (prop) {
            case "title" -> {
                stageTitle = value.toStr();
                yield true;
            }
            case "visible" -> true; // no-op, always visible
            case "bgcolor" -> {
                if (value instanceof Datum.Color c) {
                    player.getStageRenderer().setBackgroundColor(
                        (c.r() << 16) | (c.g() << 8) | c.b());
                } else if (!value.isVoid()) {
                    int rgb = Datum.datumToArgb(value) & 0xFFFFFF;
                    player.getStageRenderer().setBackgroundColor(rgb);
                }
                yield true;
            }
            default -> setMovieProp(propName, value);
        };
    }

    @Override
    public void goToFrame(int frame) {
        player.goToFrame(frame);
    }

    @Override
    public void goToLabel(String label) {
        player.goToLabel(label);
    }

    @Override
    public void gotoNetPage(String url, String target) {
        if (gotoNetPageHandler != null) {
            gotoNetPageHandler.accept(url, target);
        }
    }

    private String getMovieName() {
        if (file != null && file.getBasePath() != null) {
            String path = file.getBasePath();
            int lastSep = Math.max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
            if (lastSep >= 0 && lastSep < path.length() - 1) {
                return path.substring(lastSep + 1);
            }
            return path;
        }
        return "";
    }

    private String getMoviePath() {
        if (file != null && file.getBasePath() != null) {
            String path = file.getBasePath();
            if (!path.endsWith("/") && !path.endsWith("\\")) {
                path += "/";
            }
            return path;
        }
        return "";
    }

    // Accessors for direct property access

    public boolean isExitLock() {
        return exitLock;
    }

    public boolean isUpdateLock() {
        return updateLock;
    }

    @Override
    public char getItemDelimiter() {
        return ItemDelimiterCache._char;
    }

    public String getItemDelimiterString() {
        return itemDelimiter;
    }

    public int getPuppetTempo() {
        return puppetTempo;
    }

    public void setPuppetTempo(int puppetTempo) {
        this.puppetTempo = Math.max(0, puppetTempo);
        if (inputState != null) {
            inputState.setCaretBlinkRate(player.getTempo());
        }
    }

    public Datum getActorList() {
        return actorList;
    }

    public Datum getAlertHook() {
        return alertHook;
    }
}
