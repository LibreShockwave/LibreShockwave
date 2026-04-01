# LibreShockwave

[![Website](https://img.shields.io/badge/Website-libreshockwave.net-orange?logo=googlechrome&logoColor=white)](https://libreshockwave.net)

A Java project for parsing Macromedia/Adobe Director and Shockwave files (.dir, .dxr, .dcr, .cct, .cst).

It **won't** *just* be an emulator, the goal is to eventually become a full software suite and ecosystem, with a Director player, alongside a replacement for Director MX (2004, 11.5, etc) as an open source replacement for Macromedia/Adobe Shockwave.

## Requirements

- Java 21 or later

## Building

```bash
./gradlew build
```

<<<<<<< Updated upstream
=======
## MCP Debug Server

LibreShockwave includes a local MCP server product in the `mcp` module for AI-assisted debugging of headless player sessions.

### Run

```bash
./gradlew :mcp:run
```

### Test

```bash
./gradlew :mcp:test
```

The server exposes debugger tools (session start/stop, stepping, breakpoints, watches, input simulation, frame PNG capture) and resources:
- `debug://snapshot/current`
- `debug://events/recent`
- `debug://frame/latest.png`

For full setup and usage (including Codex/Claude configuration and `libre.oldskooler.org` movie params), see:
- [`mcp/README.md`](mcp/README.md)

>>>>>>> Stashed changes
## Supported Formats

- RIFX container (big-endian and little-endian)
- Afterburner-compressed files (.dcr, .cct)
- Director versions 4 through 12

## Capabilities

### Reading
- Cast members (bitmaps, text, scripts, sounds, shapes, palettes, fonts)
- Lingo bytecode with symbol resolution
- Score/timeline data (frames, channels, labels, behaviour intervals)
- File metadata (stage dimensions, tempo, version)

### Asset Extraction
- Bitmaps: 1/2/4/8/16/32-bit depths, palette support, PNG export
- Text: Field (type 3) and Text (type 12) cast members via STXT chunks
- Sound: PCM to WAV conversion, MP3 extraction, IMA ADPCM decoding
- Palettes: Built-in Director palettes and custom CLUT chunks
- Fonts: PFR1 (Portable Font Resource) extraction from XMED chunks, export to TrueType (.ttf)

## Player & Lingo VM

> **Note:** The Lingo VM, desktop player, and WASM player are under active development and are not production-ready. Expect missing features, incomplete Lingo coverage, and breaking changes.

LibreShockwave includes a Lingo bytecode virtual machine and player that can load and run Director movies. The VM executes compiled Lingo scripts, handles score playback, sprite rendering, and external cast loading — bringing `.dcr` and `.dir` files back to life.

**[Try the live demo →](https://libre.oldskooler.org/)** — a nightly build of the web player is deployed and ready to use. Load any `.dcr` or `.dir` file to test it in your browser.

The player is available in two forms:
- **Desktop** (`player-swing`) — Swing-based UI with an integrated Lingo debugger
- **Web** (`player-wasm`) — Compiled to WebAssembly via TeaVM, runs in any modern browser

All player functionality is decoupled from the SDK and VM via the `player-core` module, which provides platform-independent playback logic (score traversal, event dispatch, sprite management, bitmap decoding).

<img width="1920" height="1200" alt="image" src="https://github.com/user-attachments/assets/ea55ad95-bc72-4994-89e6-d0259bf0cc6b" />

## Editor

The `editor` module is a Swing-based Director MX 2004–style authoring environment. It provides an IDE-like interface with dockable panels for inspecting, editing, and playing back Director movies — bringing back the classic Director workflow as open source software.

Panels include: Stage, Score, Cast, Script Editor, Property Inspector, Behavior Inspector, Color Palettes, Library Palette, Paint, Text/Field Editors, Vector Shape, Markers, Message Window, and a Bytecode Debugger. Layouts are persisted to `~/.libreshockwave/layout.json`.

![](https://cdn.discordapp.com/attachments/1481540357849354272/1482322819680309258/image.png?ex=69b68834&is=69b536b4&hm=78debce49e047fe4e4e2c1ea7ca875cc7174ba95c7f89c873dd34a9438c28f29&)

### Running

```bash
./gradlew :editor:run
```

## Using player-core as a Library

The `player-core` module provides platform-independent playback logic with no UI dependencies. Use it to build custom players (JavaFX, headless renderer, server-side processor, etc.).

### Dependency

```groovy
implementation project(':player-core')  // transitively includes :vm and :sdk
```

### Minimal Example

```java
import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.Player;
import com.libreshockwave.player.render.FrameSnapshot;

DirectorFile file = DirectorFile.load(Path.of("movie.dcr"));
Player player = new Player(file);
player.play();

// Game loop
while (player.tick()) {
    FrameSnapshot snap = player.getFrameSnapshot();
    Bitmap frame = snap.renderFrame();              // composites all sprites with ink effects
    BufferedImage image = frame.toBufferedImage();   // ready to draw or save
}

player.shutdown();
```

Each call to `tick()` advances one frame and returns `true` while the movie is still playing. `renderFrame()` composites all sprites (bitmap, text, shape) with ink effects into a single image using pure software rendering — no AWT dependency required.

<details>
<summary>Custom networking</summary>

For environments without `java.net.http` (e.g. WASM, Android), pass a `NetProvider` to the constructor:

```java
Player player = new Player(file, new NetBuiltins.NetProvider() {
    public int preloadNetThing(String url) { /* start async fetch, return task ID */ }
    public int postNetText(String url, String postData) { /* POST, return task ID */ }
    public boolean netDone(Integer taskId) { /* true when complete */ }
    public String netTextResult(Integer taskId) { /* response body */ }
    public int netError(Integer taskId) { /* 0 = OK, negative = error */ }
    public String getStreamStatus(Integer taskId) { /* "Connecting", "Complete", etc. */ }
});
```

</details>

<details>
<summary>External parameters</summary>

Shockwave movies read `<PARAM>` tags from the embedding HTML. Pass these before calling `play()`:

```java
player.setExternalParams(Map.of(
    "sw1", "external.variables.txt=http://example.com/vars.txt",
    "sw2", "connection.info.host=127.0.0.1"
));
```

</details>

<details>
<summary>Event listeners</summary>

```java
// Player events (enterFrame, mouseDown, etc.)
player.setEventListener(event -> {
    System.out.println(event.event() + " at frame " + event.frame());
});

// Notified when an external cast finishes loading
player.setCastLoadedListener(() -> {
    System.out.println("A cast finished loading");
});
```

</details>

<details>
<summary>Error handling</summary>

```java
// Listen for Lingo script errors
player.setErrorListener((message, exception) -> {
    System.err.println("Lingo error: " + message);

    // The exception carries the Lingo call stack at the point of the error
    String callStack = exception.formatLingoCallStack();
    if (callStack != null) {
        System.err.println(callStack);
    }

    // Or inspect individual frames
    for (var frame : exception.getLingoCallStack()) {
        System.out.println(frame.handlerName() + " in " + frame.scriptName()
            + " [bytecode " + frame.bytecodeIndex() + "]");
    }
});
```

You can also get the call stack at any time during execution (e.g. from a TraceListener or breakpoint):

```java
// Get the live Lingo call stack (empty list when no handlers are executing)
List<LingoVM.CallStackFrame> stack = player.getLingoCallStack();

// Or as a formatted string
String formatted = player.formatLingoCallStack();
```

</details>

<details>
<summary>Debug playback</summary>

Debug playback controls `put` output, error call stacks, and diagnostic logging. It is **enabled by default**.

```java
// Disable debug output (suppresses put/error logging to stderr)
DebugConfig.setDebugPlaybackEnabled(false);

// Re-enable
DebugConfig.setDebugPlaybackEnabled(true);
```

For bytecode-level debugging (breakpoints, stepping, watch expressions), use the desktop player's built-in debugger or attach a `DebugControllerApi`:

```java
DebugController debugger = new DebugController();
player.setDebugController(debugger);

// Add a breakpoint (scriptId, handlerName, bytecodeOffset)
debugger.addBreakpoint(42, "enterFrame", 0);

// Step controls (when paused at a breakpoint)
debugger.stepInto();
debugger.stepOver();
debugger.stepOut();
debugger.continueExecution();

// Inspect state when paused
DebugSnapshot snap = debugger.getCurrentSnapshot();
snap.locals();     // local variables
snap.globals();    // global variables
snap.stack();      // operand stack
snap.callStack();  // call frames
```

</details>

<details>
<summary>Lifecycle</summary>

| Method | Description |
|--------|-------------|
| `play()` | Prepare the movie and begin playback |
| `tick()` | Advance one frame; returns `false` when the movie has stopped |
| `pause()` | Pause playback (keeps state) |
| `resume()` | Resume after pause |
| `stop()` | Stop playback and reset to frame 1 |
| `shutdown()` | Release all resources (thread pools, caches) |

</details>

## Usage

### Loading a File

```java
import com.libreshockwave.DirectorFile;
import java.nio.file.Path;

// From file path
DirectorFile file = DirectorFile.load(Path.of("movie.dcr"));

// From byte array
DirectorFile file = DirectorFile.load(bytes);
```

<details>
<summary>Accessing metadata</summary>

```java
DirectorFile file = DirectorFile.load(Path.of("movie.dcr"));

file.isAfterburner();                    // true if compressed
file.getEndian();                        // BIG_ENDIAN (Mac) or LITTLE_ENDIAN (Windows)
file.getStageWidth();                    // stage width in pixels
file.getStageHeight();                   // stage height in pixels
file.getTempo();                         // frames per second
file.getConfig().directorVersion();      // internal version number
file.getChannelCount();                  // sprite channels (48-1000 depending on version)
```

</details>

<details>
<summary>Iterating cast members</summary>

```java
for (CastMemberChunk member : file.getCastMembers()) {
    int id = member.id();
    String name = member.name();

    if (member.isBitmap()) { /* ... */ }
    if (member.isScript()) { /* ... */ }
    if (member.isSound()) { /* ... */ }
    if (member.isField()) { /* old-style text */ }
    if (member.isText()) { /* rich text */ }
    if (member.hasTextContent()) { /* either field or text */ }
}
```

</details>

<details>
<summary>Extracting bitmaps</summary>

```java
for (CastMemberChunk member : file.getCastMembers()) {
    if (!member.isBitmap()) continue;

    file.decodeBitmap(member).ifPresent(bitmap -> {
        BufferedImage image = bitmap.toBufferedImage();
        ImageIO.write(image, "PNG", new File(member.name() + ".png"));
    });
}
```

</details>

<details>
<summary>Extracting text</summary>

```java
KeyTableChunk keyTable = file.getKeyTable();

for (CastMemberChunk member : file.getCastMembers()) {
    if (!member.hasTextContent()) continue;

    for (KeyTableChunk.KeyTableEntry entry : keyTable.getEntriesForOwner(member.id())) {
        if (entry.fourccString().equals("STXT")) {
            Chunk chunk = file.getChunk(entry.sectionId());
            if (chunk instanceof TextChunk textChunk) {
                String text = textChunk.text();
            }
            break;
        }
    }
}
```

</details>

<details>
<summary>Extracting sounds</summary>

```java
import com.libreshockwave.audio.SoundConverter;

for (CastMemberChunk member : file.getCastMembers()) {
    if (!member.isSound()) continue;

    for (KeyTableChunk.KeyTableEntry entry : keyTable.getEntriesForOwner(member.id())) {
        if (entry.fourccString().equals("snd ")) {
            SoundChunk sound = (SoundChunk) file.getChunk(entry.sectionId());

            if (sound.isMp3()) {
                byte[] mp3 = SoundConverter.extractMp3(sound);
            } else {
                byte[] wav = SoundConverter.toWav(sound);
            }
            break;
        }
    }
}
```

</details>

<details>
<summary>Extracting fonts (PFR1 → TTF)</summary>

Director files can embed fonts as PFR1 (Portable Font Resource) data inside XMED chunks attached to OLE-type cast members. LibreShockwave can parse these and convert them to standard TrueType (.ttf) files.

```java
import com.libreshockwave.font.Pfr1Font;
import com.libreshockwave.font.Pfr1TtfConverter;

// Find XMED chunks with PFR1 data
KeyTableChunk keyTable = file.getKeyTable();
int xmedFourcc = ChunkType.XMED.getFourCC();

for (CastMemberChunk member : file.getCastMembers()) {
    var entry = keyTable.findEntry(member.id(), xmedFourcc);
    if (entry == null) continue;

    Chunk chunk = file.getChunk(entry.sectionId());
    if (!(chunk instanceof RawChunk raw)) continue;

    byte[] data = raw.data();
    if (data == null || data.length < 4) continue;
    if (data[0] != 'P' || data[1] != 'F' || data[2] != 'R' || data[3] != '1') continue;

    // Parse PFR1 and convert to TTF
    Pfr1Font font = Pfr1Font.parse(data);
    byte[] ttfBytes = Pfr1TtfConverter.convert(font, font.fontName);
    Files.write(Path.of(member.name() + ".ttf"), ttfBytes);
}
```

The player automatically detects PFR1 fonts when cast libraries load, converts them to TTF in memory, and registers them for pixel-perfect text rendering.

</details>

<details>
<summary>Accessing scripts and bytecode</summary>

```java
ScriptNamesChunk names = file.getScriptNames();

for (ScriptChunk script : file.getScripts()) {
    // Script-level declarations
    List<String> globals = script.getGlobalNames(names);
    List<String> properties = script.getPropertyNames(names);

    for (ScriptChunk.Handler handler : script.handlers()) {
        String handlerName = names.getName(handler.nameId());
        int argCount = handler.argCount();
        int localCount = handler.localCount();

        // Argument and local variable names
        for (int id : handler.argNameIds()) {
            String argName = names.getName(id);
        }
        for (int id : handler.localNameIds()) {
            String localName = names.getName(id);
        }

        // Bytecode instructions
        for (ScriptChunk.Handler.Instruction instr : handler.instructions()) {
            int offset = instr.offset();
            Opcode opcode = instr.opcode();
            int argument = instr.argument();
        }
    }
}
```

</details>

<details>
<summary>Aggregating globals and properties</summary>

```java
// All unique globals across all scripts
Set<String> allGlobals = file.getAllGlobalNames();

// All unique properties across all scripts
Set<String> allProperties = file.getAllPropertyNames();

// Detailed info per script
for (DirectorFile.ScriptInfo info : file.getScriptInfoList()) {
    info.scriptId();
    info.scriptName();
    info.scriptType();
    info.globals();
    info.properties();
    info.handlers();
}
```

</details>

<details>
<summary>Reading score data</summary>

```java
if (file.hasScore()) {
    ScoreChunk score = file.getScoreChunk();
    int frames = score.getFrameCount();
    int channels = score.getChannelCount();

    // Frame labels
    FrameLabelsChunk labels = file.getFrameLabelsChunk();
    if (labels != null) {
        for (FrameLabelsChunk.FrameLabel label : labels.labels()) {
            int frameNum = label.frameNum();
            String labelName = label.label();
        }
    }

    // Behaviour intervals
    for (ScoreChunk.FrameInterval interval : score.frameIntervals()) {
        int start = interval.startFrame();
        int end = interval.endFrame();
        int scriptId = interval.scriptId();
    }
}
```

</details>

<details>
<summary>Accessing raw chunks</summary>

```java
// All chunk metadata
for (DirectorFile.ChunkInfo info : file.getAllChunkInfo()) {
    int id = info.id();
    ChunkType type = info.type();
    int offset = info.offset();
    int length = info.length();
}

// Specific chunk by ID
Chunk chunk = file.getChunk(42);

// Type-safe chunk access
file.getChunk(42, BitmapChunk.class).ifPresent(bitmap -> {
    byte[] data = bitmap.data();
});
```

</details>

<details>
<summary>External cast files</summary>

```java
for (String castPath : file.getExternalCastPaths()) {
    Path resolved = baseDir.resolve(castPath);
    if (Files.exists(resolved)) {
        DirectorFile castFile = DirectorFile.load(resolved);
    }
}
```

</details>

<details>
<summary>Saving files</summary>

```java
// Load compressed/protected file
DirectorFile file = DirectorFile.load(Path.of("protected.dcr"));

// Save as unprotected RIFX (decompiles scripts automatically)
file.save(Path.of("unprotected.dir"));

// Or get bytes
byte[] rifxData = file.saveToBytes();
```

</details>

## Web Player (player-wasm)

The `player-wasm` module compiles the player for the browser using [TeaVM](https://teavm.org/) v0.13's standard WebAssembly backend. It produces a `.wasm` file with a JavaScript library that runs in all modern browsers.

WASM is a pure computation engine with **zero `@Import` annotations** — JS owns networking (`fetch`), canvas rendering, and the animation loop. All WASM execution runs in a **Web Worker** so slow Lingo scripts never block the main thread.

### Building

```bash
./gradlew :player-wasm:generateWasm
```

This compiles the Java player to WebAssembly and assembles all files (WASM binary, JS runtime, HTML, CSS) into a single serveable directory at `player-wasm/build/dist/`.

### Running locally

```bash
./gradlew :player-wasm:generateWasm
npx serve player-wasm/build/dist
# Open http://localhost:3000
```

### Deploying

Copy the contents of `player-wasm/build/dist/` to your web server. The included `index.html` is a ready-made player page with file picker, URL bar, transport controls, and a params editor.

### Embedding in Any Web Page

Include `shockwave-lib.js` and add a `<canvas>`. That's it.

```html
<canvas id="stage" width="640" height="480"></canvas>
<script src="shockwave-lib.js"></script>
<script>
  var player = LibreShockwave.create("stage");
  player.load("http://example.com/movie.dcr");
</script>
```

The following files must be served from the same directory as the script:

| File | Purpose |
|------|---------|
| `shockwave-lib.js` | Player library (the only `<script>` you need) |
| `shockwave-worker.js` | Web Worker — runs the WASM engine off the main thread |
| `player-wasm.wasm` | Compiled player engine |
| `player-wasm.wasm-runtime.js` | TeaVM runtime (loaded by the worker automatically) |

<details>
<summary>JavaScript API</summary>

```js
// Create a player on a <canvas> element (by ID or element reference)
var player = LibreShockwave.create("my-canvas", {
    basePath:      "/wasm/",                 // where the WASM files live (auto-detected by default)
    params:        { sw1: "key=value" },     // Shockwave <PARAM> tags
    autoplay:      true,                     // start playing after load (default: true)
    remember:      true,                     // persist params in localStorage (default: false)
    debugPlayback: true,                     // enable put/error logging (default: true)
    onLoad:        function(info) {},        // { width, height, frameCount, tempo }
    onError:       function(msg) {},         // error message string
    onFrame:       function(frame, total) {} // called each frame
});

// Load a movie
player.load("http://localhost/movie.dcr");  // from URL
player.loadFile(fileInput.files[0]);        // from <input type="file">

// Playback
player.play();
player.pause();
player.stop();
player.goToFrame(10);
player.stepForward();
player.stepBackward();

// External parameters (Shockwave PARAM tags)
player.setParam("sw1", "external.variables.txt=http://localhost/gamedata/external_variables.txt");
player.setParams({ sw1: "...", sw2: "..." });

// State
player.getCurrentFrame();  // current frame number
player.getFrameCount();    // total frames

// Debugging
player.setDebugPlayback(true);   // enable/disable put output & error stack traces
player.getCallStack().then(function(stack) {
    console.log(stack);          // Lingo call stack (async, returns Promise<string>)
});

// Reset (terminates worker, creates fresh WASM instance)
player.reset().then(function() {
    player.load("http://localhost/movie.dcr");  // load on a clean slate
});

// Clean up
player.destroy();
```

</details>

<details>
<summary>Architecture</summary>

```
Main thread (shockwave-lib.js)            Worker (shockwave-worker.js)
──────────────────────────────            ────────────────────────────
fetch() .dcr file
  postMessage('loadMovie', bytes)  →      WasmEntry.loadMovie()
                                            → Player + QueuedNetProvider created
  postMessage('preloadCasts')      →      preloadCasts() + pumpNetworkCollect()
                                            → fetch() cast files
  ← postMessage('castsDone')

  postMessage('play')              →      WasmEntry.play()

requestAnimationFrame loop:
  postMessage('tick')              →      WasmEntry.tick()
                                            → Lingo VM executes (may be slow — OK
                                              because it's off the main thread)
                                            → pumpNetworkCollect()
                                              → fetch() any queued URLs
                                              → deliverFetchResult()
                                            → getFrameDataJson()
                                            → getBitmapData() for new members
  ← postMessage('frame', fd, bitmaps)
  createImageBitmap() + cache
  Canvas 2D drawImage()
```

**Key design decisions:**
- No `@Import` — WASM never calls JS; JS polls for pending network requests
- Web Worker — WASM tick runs off the main thread; Lingo scripts that take
  hundreds of ms during loading never cause `requestAnimationFrame` violations
- Worker owns networking — `fetch()` runs in the worker; no relay through main thread
- Zero-copy bitmap transfer — RGBA bytes sent from worker via `Transferable`;
  main thread caches `ImageBitmap` objects; only new/changed members are sent each frame
- Single rendering path — sprite JSON + bitmap cache (no pixel buffer fallback)
- Fallback URLs in JSON — worker handles retry logic (.cct → .cst on 404)

</details>

<details>
<summary>Module structure</summary>

```
player-wasm/
  build.gradle                          # TeaVM plugin config + assembleWasm task
  src/main/java/.../wasm/
    WasmEntry.java                      # All @Export methods (single entry point)
    WasmPlayer.java                     # Player wrapper (deferred play, tick resilience)
    QueuedNetProvider.java              # Polling-based NetProvider (no @Import)
    SpriteDataExporter.java             # Frame data JSON + bitmap cache
  src/main/resources/web/
    index.html                          # Player page with toolbar and transport controls
    shockwave-lib.js                    # Main-thread library: creates Worker, renders canvas
    shockwave-worker.js                 # Web Worker: WASM engine + network pump
    libreshockwave.css                  # Styling
```

</details>

### Development Notes

<details>
<summary>TeaVM 0.13 WASM code-gen bugs (5 known issues)</summary>

#### TeaVM string switch — never use Java keywords as `case` labels

TeaVM 0.13 silently miscompiles `switch` statements that use Java keywords as string case labels. The case body is never entered at runtime even though equality holds in plain Java:

```java
// BROKEN in TeaVM WASM — case "char" is silently skipped:
return switch (chunkType) {
    case "char" -> str.substring(start - 1, end);
    case "word" -> ...
};

// CORRECT — use if-else instead:
if ("char".equals(chunkType)) {
    return str.substring(start - 1, end);
} else if ("word".equals(chunkType)) {
    ...
}
```

Affected keywords include `char`, `int`, `void`, `class`, `new`, `return`, and any other Java reserved word. Use `if-else` chains with `.equals()` whenever a string variable might match a keyword at runtime.

#### TeaVM nested `yield switch` — avoid nested switch expressions

TeaVM 0.13 silently fails on nested `switch` expressions that use `yield`. When a `yield switch(...) { ... }` appears inside an outer switch expression, the inner switch crashes at WASM runtime with no error:

```java
// BROKEN in TeaVM WASM — inner switch silently throws:
return switch (method) {
    case "count" -> {
        yield switch (chunkType) {
            case "char" -> Datum.of(text.length());
            case "word" -> Datum.of(trimmed.split("\\s+").length);
            default -> Datum.ZERO;
        };
    }
    default -> Datum.VOID;
};

// CORRECT — use if-else instead of the inner switch:
return switch (method) {
    case "count" -> {
        if ("char".equals(chunkType)) {
            yield Datum.of(text.length());
        } else if ("word".equals(chunkType)) {
            yield Datum.of(trimmed.split("\\s+").length);
        } else {
            yield Datum.ZERO;
        }
    }
    default -> Datum.VOID;
};
```

The outer switch expression with `yield` works fine — only the **nesting** of a second switch expression inside it causes the issue.

#### TeaVM `int[]` array element assignment reordering

TeaVM 0.13 reorders element assignments when writing to `int[]` arrays. Both array initializers (`new int[] { a, b }`) and explicit index assignment (`arr[0] = a; arr[1] = b;`) can have their values swapped in the compiled WASM output:

```java
// BROKEN in TeaVM WASM — elements get swapped:
int offset = reader.readU16();
int frame = reader.readU16();
int[] entry = new int[2];
entry[0] = offset;  // may contain frame at runtime
entry[1] = frame;   // may contain offset at runtime
list.add(entry);

// ALSO BROKEN — initializer has same problem:
list.add(new int[] { offset, frame });

// CORRECT — use parallel List<Integer> instead:
List<Integer> offsets = new ArrayList<>();
List<Integer> frames = new ArrayList<>();
offsets.add(offset);
frames.add(frame);
```

Use parallel `List<Integer>` or separate variables instead of multi-element `int[]` arrays when the element order matters.

#### TeaVM chained method calls in `HashMap.put()`/`get()` arguments

TeaVM 0.13 garbles values when chained method calls appear as arguments to `HashMap.put()` or `HashMap.get()`. The compiled WASM produces wrong keys and/or values:

```java
// BROKEN in TeaVM WASM — garbled HashMap keys/values:
map.put(label.getName().toLowerCase(), label.getFrame().value());

// CORRECT — extract to local variables first:
String key = label.getName().toLowerCase();
int value = label.getFrame().value();
map.put(key, value);
```

Always extract chained method call results into local variables before passing them as arguments to collection methods like `put()`, `get()`, `add()`, etc.

#### TeaVM `%n` format specifier not supported

TeaVM 0.13 does not support the `%n` (platform newline) format specifier in `String.format()` and `System.out.printf()`. Using `%n` throws `UnknownFormatConversionException` at runtime, which silently crashes any code path that contains it:

```java
// BROKEN in TeaVM WASM — throws UnknownFormatConversionException:
System.out.printf("value=%d%n", x);

// CORRECT — use literal \n instead:
System.out.printf("value=%d\n", x);
```

Always use `\n` instead of `%n` in format strings. This applies to all code reachable from WASM, including debug logging behind feature flags.

</details>

<details>
<summary>Multiuser Xtra — WebSocket proxy setup</summary>

Director movies that use the **Multiuser Xtra** need a WebSocket-to-TCP proxy because browsers can't open raw TCP sockets. [websockify](https://github.com/novnc/websockify) bridges WebSocket connections from the browser to the TCP server.

Each TCP port your movie connects to needs its own websockify instance. For example, Habbo connects to a game server (port 30001) and a MUS server (port 38101).

#### Installing websockify

```bash
pip install websockify
```

#### Quick start

```bash
# Proxy WebSocket :30001 → TCP 127.0.0.1:30087
python -m websockify --verbose --traffic 30001 127.0.0.1:30087
```

Replace ports as needed. The first argument is the WebSocket listen port, the second is the TCP target (`host:port`).

#### Running as a service (Linux — systemd)

Create `/etc/systemd/system/websockify-game.service`:

```ini
[Unit]
Description=websockify game proxy (WS :30001 → TCP :30087)
After=network.target

[Service]
ExecStart=/usr/bin/python3 -m websockify 30001 127.0.0.1:30087
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Then enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable websockify-game
sudo systemctl start websockify-game
```

Repeat with a second unit file for each additional port (e.g. `websockify-mus.service` for the MUS connection).
</details>

### Known Limitations

- No mouse/keyboard event forwarding to Lingo VM (planned)
- No Lingo debugger in WASM (available in desktop player)
- 32-bit JPEG-based bitmaps (ediM+ALFA) render as placeholders

## Tools

### Running Tests

```bash
# All unit tests
./gradlew :sdk:test :vm:test
```

#### SDK Tests (`sdk/src/test/`)

| Class | Description |
|-------|-------------|
| `DirectorFileTest` | Integration tests for loading and parsing Director files |
| `DcrFileTest` | DCR file parsing with external cast support |
| `SdkFeatureTest` | Comprehensive SDK feature tests (local files + HTTP) |
| `Bitmap32BitTest` | 32-bit bitmap decoding |
| `BitmapExtractTest` | Bitmap extraction by name from Director files |
| `ScriptTypeTest` | Script type identification and classification |
| `SoundExtractionTest` | Sound extraction (PCM → WAV, MP3) |
| `Pfr1ToTtfTest` | PFR1 font → TTF conversion + pixel comparison against reference |
| `Pfr1ParseTest` | PFR1 binary format parser diagnostics |

```bash
# Run SDK integration tests (JavaExec, not JUnit)
./gradlew :sdk:runTests           # DirectorFileTest
./gradlew :sdk:runDcrTests        # DcrFileTest (optional: -Pfile=path/to/file.dcr)
./gradlew :sdk:runFeatureTests    # SdkFeatureTest
```

#### VM Tests (`vm/src/test/`)

| Class | Description |
|-------|-------------|
| `LingoVMTest` | VM execution, stack operations, control flow (21 tests) |
| `OpcodeTest` | Opcode encoding/decoding and argument handling (51 tests) |
| `DatumTest` | Datum type conversions and equality (11 tests) |
| `ScriptInstanceTest` | Script instance lifecycle, properties, list operations (48 tests) |

```bash
# Run VM tests via JavaExec
./gradlew :vm:runVmTests
```

## Architecture

### Modules

| Module | Description |
|--------|-------------|
| `sdk` | Core library for parsing Director/Shockwave files |
| `vm` | Lingo bytecode virtual machine |
| `player-core` | Platform-independent playback engine (score, events, rendering data) |
| `player-wasm` | Browser player compiled to WebAssembly via TeaVM |
| `editor` | Swing-based Director MX 2004–style authoring environment |

<details>
<summary>SDK packages</summary>

- `com.libreshockwave` - Main `DirectorFile` class
- `com.libreshockwave.chunks` - Chunk type parsers (CASt, Lscr, BITD, STXT, etc.)
- `com.libreshockwave.bitmap` - Bitmap decoding and palette handling
- `com.libreshockwave.audio` - Sound conversion utilities
- `com.libreshockwave.lingo` - Opcode definitions and decompiler
- `com.libreshockwave.io` - Binary readers/writers
- `com.libreshockwave.format` - File format utilities (Afterburner, chunk types)
- `com.libreshockwave.cast` - Cast member type definitions
- `com.libreshockwave.font` - PFR1 font parser, TTF converter, bitmap font rasterizer

</details>

## References

This implementation draws from:

- [dirplayer-rs](https://github.com/igorlira/dirplayer-rs) by Igor Lira
- [ProjectorRays](https://github.com/ProjectorRays/ProjectorRays) by Debby Servilla
- ScummVM Director engine documentation

## Licence

This project uses a split licence model:

- **`sdk/` and `vm/`** — Licensed under the [Apache License 2.0](sdk/LICENSE)
- **`player-core/`, `player-swing/`, `player-simulator/`, and `player-wasm/`** — Licensed under the [GNU General Public License v3.0](player-core/LICENSE)

See the `LICENSE` file in each module directory for full terms.
