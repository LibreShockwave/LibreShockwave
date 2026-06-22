#include "EditorModels.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/bitmap/Palette.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/FrameLabelsChunk.hpp"
#include "libreshockwave/chunks/SoundChunk.hpp"
#include "libreshockwave/chunks/ScoreChunk.hpp"
#include "libreshockwave/chunks/ScriptNamesChunk.hpp"
#include "libreshockwave/chunks/TextChunk.hpp"
#include "libreshockwave/format/ScriptFormatUtils.hpp"
#include "libreshockwave/lingo/decompiler/LingoDecompiler.hpp"
#include "libreshockwave/lingo/vm/trace/InstructionAnnotator.hpp"
#include "libreshockwave/lingo/vm/datum/DatumFormatter.hpp"
#include "libreshockwave/player/audio/SoundManager.hpp"
#include "libreshockwave/player/input/DirectorKeyCodes.hpp"

namespace libreshockwave::editor {
namespace {

constexpr std::array<EditorPanelSpec, 14> kEditorPanelSpecs{{
    {"stage", "Stage", DockArea::Left, true, "Ctrl+1"},
    {"score", "Score", DockArea::Bottom, true, "Ctrl+2"},
    {"cast", "Cast", DockArea::Right, true, "Ctrl+3"},
    {"property-inspector", "Property Inspector", DockArea::Right, true, "Ctrl+4"},
    {"script", "Script", DockArea::Bottom, true, "Ctrl+0"},
    {"message", "Message", DockArea::Bottom, true, "Ctrl+Shift+M"},
    {"tool-palette", "Tool Palette", DockArea::Left, true, "Ctrl+7"},
    {"paint", "Paint", DockArea::Left, false, "Ctrl+5"},
    {"vector-shape", "Vector Shape", DockArea::Left, false, ""},
    {"text", "Text", DockArea::Right, false, "Ctrl+6"},
    {"field", "Field", DockArea::Right, false, ""},
    {"sound", "Sound", DockArea::Right, false, ""},
    {"color-palettes", "Color Palettes", DockArea::Right, false, "Ctrl+Alt+7"},
    {"bytecode-debugger", "Bytecode Debugger", DockArea::Bottom, false, "Ctrl+Shift+D"},
}};

constexpr MainMenuText kMainMenuText{
    .fileMenuText = "&File",
    .preferencesMenuText = "Preferences",
    .editMenuText = "&Edit",
    .viewMenuText = "&View",
    .zoomMenuText = "Zoom",
    .spriteOverlayMenuText = "Sprite Overlay",
    .gridsMenuText = "Grids",
    .guidesMenuText = "Guides",
    .insertMenuText = "&Insert",
    .mediaElementMenuText = "Media Element",
    .modifyMenuText = "&Modify",
    .controlMenuText = "&Control",
    .debugMenuText = "&Debug",
    .windowMenuText = "&Window",
    .helpMenuText = "&Help",
};

constexpr std::array<std::string_view, 14> kToolPaletteTools{{
    "Arrow",
    "Rotate",
    "Hand",
    "Zoom",
    "Line",
    "Rect",
    "Round Rect",
    "Ellipse",
    "Text",
    "Field",
    "Button",
    "Check Box",
    "Radio",
    "Color",
}};

constexpr std::array<std::string_view, 9> kPaintWindowTools{{
    "Pencil",
    "Brush",
    "Eraser",
    "Fill",
    "Line",
    "Rect",
    "Oval",
    "Select",
    "Lasso",
}};

constexpr std::array<PaintWindowActionSpec, 4> kPaintWindowViewActionSpecs{{
    {"actual-size", "1:1", "Original Size"},
    {"fit", "Fit", "Fit to View"},
    {"zoom-out", "-", "Zoom Out"},
    {"zoom-in", "+", "Zoom In"},
}};

constexpr std::array<std::string_view, 2> kFieldEditorToolbarActions{{
    "Wrap",
    "Scroll",
}};

constexpr std::array<std::string_view, 5> kVectorShapeToolbarActions{{
    "Pen",
    "Line",
    "Rect",
    "Ellipse",
    "Select",
}};

constexpr std::array<std::string_view, 3> kTextEditorStyleActions{{
    "B",
    "I",
    "U",
}};

constexpr std::array<std::string_view, 3> kTextEditorFontChoices{{
    "Arial",
    "Times New Roman",
    "Courier New",
}};

constexpr std::array<std::string_view, 6> kTextEditorSizeChoices{{
    "12",
    "14",
    "16",
    "18",
    "24",
    "36",
}};

constexpr std::array<std::string_view, 4> kParagraphAlignmentLabels{{
    "Left",
    "Center",
    "Right",
    "Justify",
}};

constexpr std::array<std::string_view, 9> kColorPaletteChoices{{
    "System - Win",
    "System - Mac",
    "Rainbow",
    "Grayscale",
    "Pastels",
    "Vivid",
    "NTSC",
    "Metallic",
    "Web 216",
}};

constexpr std::array<std::string_view, 5> kDebugToolbarActions{{
    "Step Into",
    "Step Over",
    "Step Out",
    "Continue",
    "Clear BPs",
}};

constexpr std::array<FileCommandSpec, 11> kFileCommandSpecs{{
    {"new-movie", "New Movie", "Ctrl+N"},
    {"new-cast", "New Cast", ""},
    {"open", "Open...", "Ctrl+O"},
    {"open-recent", "Open Recent", ""},
    {"close", "Close", "Ctrl+W"},
    {"save", "Save", "Ctrl+S"},
    {"save-as", "Save As...", "Ctrl+Shift+S"},
    {"save-all", "Save All", ""},
    {"import", "Import...", "Ctrl+R"},
    {"export", "Export...", ""},
    {"exit", "Exit", ""},
}};

constexpr std::array<std::string_view, 3> kFileCreationStageSizeChoices{{
    "640 x 480",
    "800 x 600",
    "1024 x 768",
}};

constexpr FileCreationText kFileCreationText{
    .newMovieTitle = "New Movie",
    .stageSizeLabel = "Stage size:",
    .framesLabel = "Frames:",
    .spriteChannelsLabel = "Sprite channels:",
    .newMoviePendingText =
        "Movie creation is prepared for Java UI parity. "
        "Writing new Director movie files is still pending.",
    .newCastTitle = "New Cast",
    .castNameLabel = "Name:",
    .castTypeLabel = "Type:",
    .internalCastType = "Internal",
    .externalCastType = "External",
    .newCastPendingText =
        "Cast creation is prepared for Java UI parity. "
        "Adding casts to the Director file is still pending.",
};

constexpr FileSaveText kFileSaveText{
    .saveTitle = "Save",
    .saveAsTitle = "Save As",
    .saveAllTitle = "Save All",
    .noMovieText = "Open or create a movie before saving.",
    .newMoviePathText = "(new movie)",
    .savePendingText = "Writing Director movie files from the C++ editor is still pending.",
    .saveAsDialogTitle = "Save Director File As",
    .directorFileFilter = "Director Files (*.dir *.dxr *.dcr *.cct *.cst);;All Files (*)",
    .saveAllNoChangesText = "No open movie or cast changes to save.",
    .saveAllPreparedText = "Prepared save for the open movie and loaded cast state.",
    .saveAllPendingText = "Writing Director movie/cast files from the C++ editor is still pending.",
    .saveAllStatusText = "Prepared save all",
};

constexpr FileOpenImportText kFileOpenImportText{
    .openDirectorFileTitle = "Open Director File",
    .openFailedTitle = "Open Failed",
    .directorFileFilter = "Director Files (*.dir *.dxr *.dcr *.cct *.cst);;All Files (*)",
    .importTitle = "Import",
    .importNoMovieText = "Open a movie before importing media.",
    .importMediaTitle = "Import Media",
    .importMediaFilter =
        "Importable Media (*.png *.bmp *.jpg *.jpeg *.gif *.mp3 *.wav *.aiff *.txt *.ls "
        "*.cst *.cct *.dir *.dxr *.dcr);;All Files (*)",
    .importPendingText =
        "Select zero or one loaded cast member to import bitmap media into the current runtime session. "
        "Zero selected members creates a session-local bitmap member; one selected member replaces that member's runtime image. "
        "Persisting new cast members in the Director file is still pending. "
        "To load an external cast slot, select the external cast in the Cast panel and run File > Import again.",
};

constexpr std::array<PreferencePanelSpec, 5> kPreferencePanelSpecs{{
    {"general", "General..."},
    {"network", "Network..."},
    {"script", "Script..."},
    {"sprite", "Sprite..."},
    {"paint", "Paint..."},
}};

constexpr GeneralPreferencesText kGeneralPreferencesText{
    .title = "General Preferences",
    .browseText = "Browse...",
    .defaultOpenFolderLabel = "Default open folder:",
    .recentProjectsLabel = "Recent Projects",
    .emptyRecentProjectsText = "No recent projects",
    .clearRecentProjectsText = "Clear Recent Projects",
    .chooseDefaultOpenFolderTitle = "Choose Default Open Folder",
    .savedStatusText = "General preferences saved",
};

constexpr std::array<PreferenceBoolOptionSpec, 3> kNetworkPreferenceBoolOptions{{
    {"enableNetwork", "Enable network operations", false},
    {"allowRemoteAssets", "Allow remote asset loading", false},
    {"logNetwork", "Log network activity", false},
}};

constexpr std::array<PreferenceIntOptionSpec, 1> kNetworkPreferenceIntOptions{{
    {"timeoutSeconds", "Request timeout:", 30, 1, 300},
}};

constexpr std::array<PreferenceBoolOptionSpec, 3> kScriptPreferenceBoolOptions{{
    {"syntaxHighlighting", "Syntax highlighting", true},
    {"autoIndent", "Auto-indent", true},
    {"showBytecodeAnnotations", "Show bytecode annotations", true},
}};

constexpr std::array<PreferenceIntOptionSpec, 1> kScriptPreferenceIntOptions{{
    {"tabWidth", "Tab width:", 4, 1, 16},
}};

constexpr std::array<std::string_view, 2> kScriptDefaultViewChoices{{
    "Lingo",
    "Bytecode",
}};

constexpr std::array<PreferenceChoiceOptionSpec, 1> kScriptPreferenceChoiceOptions{{
    {"defaultView", "Default script view:", "Lingo", kScriptDefaultViewChoices},
}};

constexpr std::array<PreferenceBoolOptionSpec, 3> kSpritePreferenceBoolOptions{{
    {"showOverlays", "Show sprite overlays", true},
    {"showPaths", "Show sprite paths", false},
    {"snapToGrid", "Snap sprite edits to grid", false},
}};

constexpr std::array<PreferenceIntOptionSpec, 1> kSpritePreferenceIntOptions{{
    {"nudgePixels", "Keyboard nudge pixels:", 1, 1, 100},
}};

constexpr std::array<PreferenceBoolOptionSpec, 3> kPaintPreferenceBoolOptions{{
    {"antialiasPreview", "Antialias preview", true},
    {"showTransparencyGrid", "Show transparency grid", true},
    {"preservePaletteIndexes", "Preserve palette indexes", true},
}};

constexpr std::array<PreferenceIntOptionSpec, 1> kPaintPreferenceIntOptions{{
    {"brushSize", "Default brush size:", 3, 1, 128},
}};

constexpr std::array<EditCommandSpec, 11> kEditCommandSpecs{{
    {"undo", "Undo", "Ctrl+Z"},
    {"redo", "Redo", "Ctrl+Y"},
    {"cut", "Cut", "Ctrl+X"},
    {"copy", "Copy", "Ctrl+C"},
    {"paste", "Paste", "Ctrl+V"},
    {"clear", "Clear", "Delete"},
    {"select-all", "Select All", "Ctrl+A"},
    {"find", "Find", ""},
    {"edit-sprite-frames", "Edit Sprite Frames", ""},
    {"edit-entire-sprite", "Edit Entire Sprite", ""},
    {"exchange-cast-members", "Exchange Cast Members", ""},
}};

constexpr EditCommandStatusText kEditCommandStatusText{
    .unsupportedText = "No focused editor supports this command",
    .unsupportedClearText = "No focused editor supports Clear",
    .clearedText = "Cleared",
};

constexpr std::array<std::pair<std::string_view, std::string_view>, 6> kEditCommandSuccessStatuses{{
    {"undo", "Undo"},
    {"redo", "Redo"},
    {"cut", "Cut"},
    {"copy", "Copied"},
    {"paste", "Pasted"},
    {"select-all", "Selected all"},
}};

constexpr std::array<EditCommandSpec, 4> kFindCommandSpecs{{
    {"find", "Find...", "Ctrl+F"},
    {"find-again", "Find Again", "Ctrl+G"},
    {"replace", "Replace...", "Ctrl+H"},
    {"find-selection", "Find Selection", ""},
}};

constexpr FindReplaceText kFindReplaceText{
    .findTitle = "Find",
    .findPrompt = "Find:",
    .replaceTitle = "Replace",
    .replaceWithPrompt = "Replace with:",
    .replaceAllPromptPrefix = "Replace all occurrences of \"",
    .replaceAllPromptSuffix = "\"?",
    .noSelectedTextToFind = "No selected text to find",
    .noSelectedTextToReplace = "No selected text to replace",
    .noFocusedFindText = "No focused text widget supports Find",
    .noFocusedReplaceText = "No focused text widget supports Replace",
};

constexpr EditSpriteCommandText kEditSpriteCommandText{
    .editFramesTitle = "Edit Sprite Frames",
    .editFramesNoSelectionText = "Select a score sprite or score cell first.",
    .editEntireTitle = "Edit Entire Sprite",
    .editEntireNoMovieText = "Open a movie first.",
    .editEntireNoCastMemberText = "Select a score sprite with a cast member first.",
    .editEntireMemberNotLoadedText = "The selected sprite's cast member is not loaded.",
    .editEntireNoPanelText = "No editor panel is available for this member type.",
};

constexpr std::array<ModifyCommandSpec, 12> kModifyCommandSpecs{{
    {"movie-properties", "Properties...", "Movie", ""},
    {"movie-casts", "Casts...", "Movie", ""},
    {"external-parameters", "External Parameters...", "Movie", "Ctrl+Shift+E"},
    {"sprite-properties", "Properties...", "Sprite", ""},
    {"sprite-tweening", "Tweening...", "Sprite", ""},
    {"cast-member-properties", "Properties...", "Cast Member", ""},
    {"frame-tempo", "Tempo...", "Frame", ""},
    {"frame-palette", "Palette...", "Frame", ""},
    {"frame-transition", "Transition...", "Frame", ""},
    {"frame-sound", "Sound...", "Frame", ""},
    {"font", "Font...", "", ""},
    {"paragraph", "Paragraph...", "", ""},
}};

constexpr std::array<PlaybackCommandSpec, 5> kPlaybackCommandSpecs{{
    {"play", "Play", "Play", "Ctrl+Alt+P"},
    {"stop", "Stop", "Stop", "Ctrl+."},
    {"rewind", "Rewind", "Rewind", "Ctrl+Alt+R"},
    {"step-forward", "Step Forward", "Step Forward", "Ctrl+Alt+Right"},
    {"step-backward", "Step Backward", "Step Backward", "Ctrl+Alt+Left"},
}};

constexpr PlaybackToolbarText kPlaybackToolbarText{
    .title = "Playback",
    .initialFrameText = "Frame: 1",
};

constexpr std::array<ToggleCommandSpec, 1> kControlToggleSpecs{{
    {"loop-playback", "Loop Playback", true},
}};

constexpr std::array<HelpCommandSpec, 1> kHelpCommandSpecs{{
    {"about", "About LibreShockwave Editor", "About LibreShockwave Editor",
     "LibreShockwave Editor\n\nA recreation of Macromedia Director MX 2004.\n\nPart of the LibreShockwave project."},
}};

constexpr StartScreenText kStartScreenText{
    .windowTitle = "LibreShockwave Editor",
    .appTitle = "LibreShockwave Editor",
    .subtitle = "Director MX 2004",
    .createNewMovieText = "Create New Movie",
    .createNewMovieDialogTitle = "Create New Movie",
    .createNewMoviePendingText =
        "New movie setup is available from File > New Movie in the Qt editor. "
        "Writing new Director movie files is still pending.",
    .openMovieText = "Open Movie...",
    .emptyRecentProjectsText = "No recent projects. Open a movie to get started.",
    .recentProjectsHeader = "Recent Projects:",
    .openDirectorFileTitle = "Open Director File",
    .directorFileFilter = "Director Files (*.dir *.dxr *.dcr *.cct *.cst);;All Files (*)",
    .fileNotFoundTitle = "File Not Found",
    .fileNotFoundMessagePrefix = "File not found:\n",
};

constexpr RecentProjectsMenuText kRecentProjectsMenuText{
    .emptyText = "No Recent Projects",
    .clearText = "Clear Recent Projects",
    .missingWarningTitle = "Open Recent",
    .missingWarningMessagePrefix = "Recent project no longer exists:\n",
    .clearedStatusText = "Recent projects cleared",
};

constexpr ExchangeCastMembersText kExchangeCastMembersText{
    .title = "Exchange Cast Members",
    .selectTwoLoadedText = "Select exactly two loaded cast members to compare before exchange.",
    .oneMemberNotLoadedText = "One of the selected members is not loaded.",
    .pendingNoteText =
        "Exchange swaps supported runtime media payloads for the current editor session. "
        "Director file cast-member exchange, names, score references, and unsupported media types remain pending.",
    .exchangeButtonText = "Exchange Runtime Media",
    .unnamedMemberText = "(unnamed)",
    .unsetText = "-",
};

constexpr std::array<std::string_view, 7> kExchangeCastMembersTableColumns{{
    "Slot",
    "Cast",
    "Member",
    "Name",
    "Type",
    "Size",
    "Script",
}};

constexpr std::array<std::string_view, 2> kExchangeCastMembersSlotLabels{{
    "A",
    "B",
}};

constexpr MovieCastsDialogText kMovieCastsDialogText{
    .title = "Movie Casts",
    .noMovieText = "Open a movie to view its casts.",
    .internalKindText = "Internal",
    .externalKindText = "External",
    .loadedStatusText = "Loaded",
    .notLoadedStatusText = "Not loaded",
    .unsetText = "-",
};

constexpr std::array<std::string_view, 4> kMovieCastsDialogTableColumns{{
    "Cast",
    "Kind",
    "Status",
    "Members",
}};

constexpr MoviePropertiesDialogText kMoviePropertiesDialogText{
    .title = "Movie Properties",
    .noMovieText = "Open a movie to view its properties.",
    .untitledMovieText = "Untitled",
};

constexpr CastMemberPropertiesDialogText kCastMemberPropertiesDialogText{
    .title = "Cast Member Properties",
    .selectMemberText = "Select a cast member first.",
    .memberNotLoadedText = "Selected cast member is not loaded.",
    .memberTitlePrefix = "Member ",
    .registrationPointLabel = "Registration Point:",
    .scriptLabel = "Script:",
    .castLibraryLabel = "Cast Library:",
    .unsetText = "-",
};

constexpr SpritePropertiesDialogText kSpritePropertiesDialogText{
    .title = "Sprite Properties",
    .noMovieText = "Open a movie first.",
    .selectSpriteText = "Select a score sprite or score cell first.",
};

constexpr SpriteTweeningDialogText kSpriteTweeningDialogText{
    .title = "Sprite Tweening",
    .noMovieText = "Open a movie first.",
    .selectSpriteText = "Select a score sprite or score cell first.",
    .spanLabel = "Sprite span:",
    .castMemberLabel = "Cast member:",
    .channelLabel = "Channel:",
    .tweenedPropertiesTitle = "Tweened Properties",
    .settingsTitle = "Settings",
    .curvatureLabel = "Curvature:",
    .easeInLabel = "Ease in:",
    .easeOutLabel = "Ease out:",
    .unsetText = "-",
    .pendingNoteText =
        "Tweening controls are shown for Java UI parity. "
        "Writing tween data back into the Director score is still pending.",
};

constexpr std::array<std::string_view, 8> kSpriteTweeningPropertyLabels{{
    "Path",
    "Size",
    "Rotation",
    "Skew",
    "Blend",
    "Foreground color",
    "Background color",
    "Ink",
}};

constexpr FrameChannelDialogText kFrameChannelDialogText{
    .titlePrefix = "Frame ",
    .noScoreText = "Open a movie with score data first.",
    .frameLabel = "Frame:",
    .tempoLabel = "Tempo:",
    .parsedTempoEntriesLabel = "Parsed tempo entries:",
    .paletteCastLabel = "Palette cast:",
    .paletteMemberLabel = "Palette member:",
    .descriptionLabel = "Description:",
    .parsedPaletteEntriesLabel = "Parsed palette entries:",
    .transitionLabel = "Transition:",
    .nativeChannelLabel = "Native channel:",
    .parsingStatusLabel = "Parsing status:",
    .readOnlySnapshotText = "Read-only score channel snapshot",
    .sound1Label = "Sound 1:",
    .sound2Label = "Sound 2:",
    .nativeChannelsLabel = "Native channels:",
    .unsetText = "-",
};

constexpr ExternalParamsDialogText kExternalParamsDialogText{
    .windowTitle = "External Parameters",
    .helpText = "Set key-value pairs accessible via externalParamValue() in Lingo scripts.",
    .addText = "Add",
    .removeText = "Remove",
    .habboPresetText = "Habbo Preset",
    .habboPresetTooltip = "Load default Habbo external parameters",
};

constexpr std::array<std::string_view, 2> kExternalParamsTableColumns{{
    "Key",
    "Value",
}};

constexpr std::array<int, 5> kViewZoomPercentages{{
    25,
    50,
    100,
    200,
    400,
}};

constexpr std::array<ViewCommandSpec, 2> kViewSpriteOverlaySpecs{{
    {"sprite-overlay-info", "Show Info", true},
    {"sprite-overlay-paths", "Show Paths", true},
}};

constexpr std::array<ViewCommandSpec, 2> kViewTopLevelToggleSpecs{{
    {"sprite-toolbar", "Sprite Toolbar", true},
    {"keyframes", "Keyframes", true},
}};

constexpr std::array<ViewCommandSpec, 3> kViewGridSpecs{{
    {"score-grid-lines", "Show", true},
    {"stage-grid-snap", "Snap To", true},
    {"grid-settings", "Settings...", false},
}};

constexpr std::array<ViewCommandSpec, 2> kViewGuideSpecs{{
    {"stage-guides-show", "Show", true},
    {"stage-guides-snap", "Snap To", true},
}};

constexpr ViewGridSettingsText kViewGridSettingsText{
    .title = "Grid Settings",
    .gridSizeLabel = "Grid size:",
    .guideSnapThresholdLabel = "Guide snap threshold:",
    .pixelSuffix = " px",
    .savedStatusText = "Grid settings saved",
};

constexpr std::array<WindowCommandSpec, 1> kWindowCommandSpecs{{
    {"reset-layout", "Reset Layout"},
}};

constexpr MainWindowShellText kMainWindowShellText{
    .defaultTitle = "LibreShockwave Editor - Director MX 2004",
    .titlePrefix = "LibreShockwave Editor - ",
    .centerPlaceholderText = "LibreShockwave Director Studio",
    .pendingPanelText = "Port pending",
    .layoutResetStatusText = "Layout reset to default",
};

constexpr MainWindowSummaryText kMainWindowSummaryText{
    .noMovieOpenText = "No movie open",
    .moviePlaceholderText = "Open a movie to view its properties",
    .scorePlaceholderText = "No score loaded",
};

constexpr PanelContextCommandText kPanelContextCommandText{
    .raisePanel = "Raise Panel",
    .showPanel = "Show Panel",
    .floatPanel = "Float",
    .dockPanel = "Dock",
    .closePanel = "Close Panel",
    .resetLayout = "Reset Layout",
};

constexpr std::array<DockCommandSpec, 5> kDockCommandSpecs{{
    {"dock-left", "Dock Left", DockArea::Left},
    {"dock-right", "Dock Right", DockArea::Right},
    {"dock-top", "Dock Top", DockArea::Top},
    {"dock-bottom", "Dock Bottom", DockArea::Bottom},
    {"dock-center", "Dock Center", DockArea::Center},
}};

constexpr std::array<MoveDockCommandSpec, 5> kMoveDockCommandSpecs{{
    {"move-to-left", "Move to Left", DockArea::Left},
    {"move-to-right", "Move to Right", DockArea::Right},
    {"move-to-top", "Move to Top", DockArea::Top},
    {"move-to-bottom", "Move to Bottom", DockArea::Bottom},
    {"move-to-center", "Move to Center", DockArea::Center},
}};

constexpr std::array<SplitCommandSpec, 4> kSplitCommandSpecs{{
    {"split-left", "Split Left", DockArea::Left},
    {"split-right", "Split Right", DockArea::Right},
    {"split-up", "Split Up", DockArea::Top},
    {"split-down", "Split Down", DockArea::Bottom},
}};

constexpr std::array<InsertCommandSpec, 3> kInsertCommandSpecs{{
    {"keyframe", "Keyframe", "Ctrl+Alt+K"},
    {"marker", "Marker", ""},
    {"remove-frame", "Remove Frame", ""},
}};

constexpr InsertActionText kInsertActionText{
    .keyframeTitle = "Insert Keyframe",
    .keyframeNoMovieText = "Open a movie before inserting keyframes.",
    .keyframeNoSelectionText = "Select a score sprite or score cell before inserting a keyframe.",
    .keyframePendingSuffix = "Writing score keyframes back into the Director file is still pending.",
    .markerTitle = "Insert Marker",
    .markerNoMovieText = "Open a movie before inserting markers.",
    .markerPromptLabel = "Marker name:",
    .markerDefaultName = "New Marker",
    .markerPendingSuffix = "Writing marker labels back into the Director file is still pending.",
    .removeFrameTitle = "Remove Frame",
    .removeFrameNoMovieText = "Open a movie before removing frames.",
    .removeFramePendingSuffix = "Writing frame removal back into the Director score is still pending.",
    .mediaElementTitle = "Insert Media Element",
    .mediaElementNoMovieText = "Open a movie before creating media elements.",
    .mediaElementPendingSuffix =
        "Persisting new cast members back into the Director file is still pending.",
};

constexpr std::array<MediaElementSpec, 6> kMediaElementSpecs{{
    {"bitmap", "Bitmap", "paint"},
    {"text", "Text", "text"},
    {"script", "Script", "script"},
    {"shape", "Shape", "vector-shape"},
    {"film-loop", "Film Loop", "cast"},
    {"sound", "Sound", "sound"},
}};

constexpr std::array<DebugCommandSpec, 8> kDebugCommandSpecs{{
    {"step-into", "Step Into", "Step Into", "F11"},
    {"step-over", "Step Over", "Step Over", "F10"},
    {"step-out", "Step Out", "Step Out", "Shift+F11"},
    {"continue", "Continue", "Continue", "F5"},
    {"toggle-breakpoint", "Toggle Breakpoint", "", "F9"},
    {"clear-breakpoints", "Clear All Breakpoints", "Clear BPs", ""},
    {"detailed-stack", "Detailed Stack Window", "", "Ctrl+Shift+S"},
    {"trace-handler", "Trace Handler...", "", "Ctrl+Shift+T"},
}};

constexpr std::array<DebugContextCommandSpec, 4> kDebugBytecodeContextCommandSpecs{{
    {"toggle-breakpoint", "Toggle Breakpoint", false},
    {"enable-disable-breakpoint", "Enable/Disable Breakpoint", false},
    {"go-to-definition", "Go to Definition", true},
    {"view-handler-details", "View Handler Details...", true},
}};

constexpr std::array<std::string_view, 5> kDebugStateTabNames{{
    "Stack",
    "Locals",
    "Globals",
    "Watches",
    "Objects",
}};

constexpr std::array<std::string_view, 3> kDebugObjectSectionNames{{
    "Timeouts",
    "Globals",
    "Movie Properties",
}};

constexpr std::array<std::string_view, 3> kDebugStackTableColumns{{
    "#",
    "Type",
    "Value",
}};

constexpr std::array<std::string_view, 3> kDebugVariableTableColumns{{
    "Name",
    "Type",
    "Value",
}};

constexpr std::array<std::string_view, 3> kDebugWatchTableColumns{{
    "Expression",
    "Type",
    "Value",
}};

constexpr std::array<DebugWatchActionSpec, 4> kDebugWatchActionSpecs{{
    {"add-watch", "+", "Add watch expression"},
    {"remove-watch", "-", "Remove selected watch"},
    {"edit-watch", "Edit", "Edit selected watch"},
    {"clear-watches", "Clear", "Clear all watches"},
}};

constexpr DebugBrowserText kDebugBrowserText{
    .scriptLabel = "Script:",
    .handlerLabel = "Handler:",
    .filterPlaceholder = "Filter",
    .scriptFilterTooltip = "Type to filter scripts",
    .handlerFilterTooltip = "Type to filter handlers",
    .detailsButtonText = "i",
    .detailsButtonTooltip = "View Handler Details",
    .bytecodeTitle = "Bytecode",
    .lingoToggleText = "Lingo",
    .bytecodeToggleText = "Bytecode",
    .lingoToggleTooltip = "Toggle between bytecode and decompiled Lingo view",
};

constexpr DebugWatchDialogText kDebugWatchDialogText{
    .addTitle = "Add Watch",
    .editTitle = "Edit Watch",
    .expressionPrompt = "Expression:",
};

constexpr TraceHandlerDialogText kTraceHandlerDialogText{
    .title = "Trace Handler",
    .noMovieText = "No movie loaded.",
    .noneText = "(none)",
};

constexpr DebugDetailsText kDebugDetailsText{
    .overviewTabText = "Overview",
    .bytecodeTabText = "Bytecode",
    .literalsTabText = "Literals",
    .propertiesTabText = "Properties",
    .globalsTabText = "Globals",
    .closeButtonText = "Close",
};

constexpr std::array<std::string_view, 5> kDebugTimeoutTableColumns{{
    "Name",
    "Period (ms)",
    "Handler",
    "Target",
    "Persistent",
}};

constexpr std::array<std::string_view, 2> kDebugMoviePropertyTableColumns{{
    "Property",
    "Value",
}};

constexpr std::array<std::string_view, 10> kDebugMoviePropertyNames{{
    "frame",
    "lastFrame",
    "tempo",
    "timer",
    "ticks",
    "movieName",
    "platform",
    "exitLock",
    "itemDelimiter",
    "puppetTempo",
}};

constexpr std::array<std::string_view, 4> kDetailedStackTabNames{{
    "Call Stack",
    "VM Stack",
    "Arguments",
    "Receiver (me)",
}};

constexpr std::array<std::string_view, 4> kPropertyInspectorTabNames{{
    "Sprite",
    "Member",
    "Behavior",
    "Movie",
}};

constexpr std::array<std::string_view, 12> kPropertyInspectorSpriteLabels{{
    "Sprite:",
    "Member:",
    "X (locH):",
    "Y (locV):",
    "Width:",
    "Height:",
    "Ink:",
    "Blend:",
    "locZ:",
    "Visible:",
    "Moveable:",
    "Editable:",
}};

constexpr std::array<std::string_view, 7> kPropertyInspectorMemberLabels{{
    "Name:",
    "Number:",
    "Cast:",
    "Type:",
    "Size:",
    "Modified:",
    "Comments:",
}};

constexpr std::array<std::string_view, 9> kPropertyInspectorMovieLabels{{
    "Movie Name:",
    "Stage Width:",
    "Stage Height:",
    "Stage Color:",
    "Palette:",
    "Tempo:",
    "Total Frames:",
    "Total Casts:",
    "Copyright:",
}};

constexpr PropertyInspectorText kPropertyInspectorText{
    .noSelectionText = "None",
    .memberPlaceholderText = "Select a cast member to view its properties",
    .moviePlaceholderText = "Open a movie to view its properties",
    .addBehaviorText = "Add",
    .removeBehaviorText = "Remove",
    .openScriptText = "Open Script",
    .openBehaviorScriptText = "Open Behavior Script",
    .behaviorScriptTitle = "Behavior Script",
    .addBehaviorTitle = "Add Behavior",
    .addBehaviorNoSpriteText = "Select a sprite cell in the Score before adding a behavior.",
    .addBehaviorPromptPrefix = "Behavior script id for channel ",
    .addBehaviorPromptMiddle = ", frame ",
    .behaviorPendingSuffix = "Writing behavior attachments back into the Director score is still pending.",
    .removeBehaviorTitle = "Remove Behavior",
    .removeBehaviorNoSelectionText = "Select an attached behavior before removing it.",
};

constexpr std::array<std::string_view, 11> kCastWindowTypeFilterItems{{
    "All Types",
    "Bitmap",
    "Script",
    "Sound",
    "Text",
    "Button",
    "Shape",
    "Film Loop",
    "Palette",
    "Field",
    "Transition",
}};

constexpr std::array<std::string_view, 6> kCastWindowTableColumns{{
    "Preview",
    "Chunk",
    "Type",
    "Name",
    "Script",
    "Reg Point",
}};

constexpr std::array<std::string_view, 6> kScoreSpecialChannelNames{{
    "Tempo",
    "Palette",
    "Transition",
    "Sound 1",
    "Sound 2",
    "Script",
}};

constexpr std::array<std::string_view, 7> kScoreIntervalTableColumns{{
    "Start",
    "End",
    "Channel",
    "Cast",
    "Member",
    "Type",
    "Name",
}};

std::string toLower(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

std::string toUpper(std::string_view value) {
    std::string upper;
    upper.reserve(value.size());
    for (const unsigned char ch : value) {
        upper.push_back(static_cast<char>(std::toupper(ch)));
    }
    return upper;
}

bool containsIgnoringCase(std::string_view value, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    return toLower(value).find(toLower(needle)) != std::string::npos;
}

bool hasName(const chunks::ScriptNamesChunk* names, int index) {
    return names != nullptr && index >= 0 && index < static_cast<int>(names->names().size());
}

std::string resolveScriptName(const chunks::ScriptNamesChunk* names, int nameId) {
    return hasName(names, nameId) ? names->getName(nameId) : "#" + std::to_string(nameId);
}

std::string resolveParamName(const chunks::ScriptNamesChunk* names,
                             const chunks::ScriptChunk::Handler& handler,
                             int index) {
    if (index >= 0 && index < static_cast<int>(handler.argNameIds.size())) {
        return resolveScriptName(names, handler.argNameIds[static_cast<std::size_t>(index)]);
    }
    return "param" + std::to_string(index);
}

std::string resolveLocalName(const chunks::ScriptNamesChunk* names,
                             const chunks::ScriptChunk::Handler& handler,
                             int index) {
    if (index >= 0 && index < static_cast<int>(handler.localNameIds.size())) {
        return resolveScriptName(names, handler.localNameIds[static_cast<std::size_t>(index)]);
    }
    return "local" + std::to_string(index);
}

std::string floatArgumentToString(int argument) {
    std::ostringstream out;
    out << std::bit_cast<float>(argument);
    return out.str();
}

std::string trimCopy(std::string_view value) {
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return {begin, end};
}

std::string normalizedLineEndings(std::string text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            normalized.push_back('\n');
        } else {
            normalized.push_back(text[i]);
        }
    }
    return normalized;
}

std::string stackDescription(const chunks::ScriptChunk& script,
                             const chunks::ScriptChunk::Handler& handler,
                             const chunks::ScriptChunk::Instruction& instruction,
                             const chunks::ScriptNamesChunk* names) {
    const int arg = instruction.argument;
    switch (instruction.opcode) {
        case lingo::Opcode::RET: return "return from handler";
        case lingo::Opcode::RET_FACTORY: return "return from factory/new";
        case lingo::Opcode::PUSH_ZERO: return "push 0";
        case lingo::Opcode::MUL: return "pop b, a; push a * b";
        case lingo::Opcode::ADD: return "pop b, a; push a + b";
        case lingo::Opcode::SUB: return "pop b, a; push a - b";
        case lingo::Opcode::DIV: return "pop b, a; push a / b";
        case lingo::Opcode::MOD: return "pop b, a; push a mod b";
        case lingo::Opcode::INV: return "pop a; push -a";
        case lingo::Opcode::JOIN_STR: return "pop b, a; push a & b";
        case lingo::Opcode::JOIN_PAD_STR: return "pop b, a; push a && b";
        case lingo::Opcode::LT: return "pop b, a; push a < b";
        case lingo::Opcode::LT_EQ: return "pop b, a; push a <= b";
        case lingo::Opcode::NT_EQ: return "pop b, a; push a <> b";
        case lingo::Opcode::EQ: return "pop b, a; push a = b";
        case lingo::Opcode::GT: return "pop b, a; push a > b";
        case lingo::Opcode::GT_EQ: return "pop b, a; push a >= b";
        case lingo::Opcode::AND: return "pop b, a; push a AND b";
        case lingo::Opcode::OR: return "pop b, a; push a OR b";
        case lingo::Opcode::NOT: return "pop a; push NOT a";
        case lingo::Opcode::CONTAINS_STR: return "pop b, a; push a contains b";
        case lingo::Opcode::CONTAINS_0_STR: return "pop b, a; push b starts a";
        case lingo::Opcode::GET_CHUNK: return "pop chunkExpr; push chunk value";
        case lingo::Opcode::HILITE_CHUNK: return "hilite chunk expression";
        case lingo::Opcode::ONTO_SPR: return "spriteBox: sprite intersects sprite";
        case lingo::Opcode::INTO_SPR: return "spriteBox: sprite within sprite";
        case lingo::Opcode::GET_FIELD: return "pop fieldRef; push field value";
        case lingo::Opcode::START_TELL: return "begin tell target";
        case lingo::Opcode::END_TELL: return "end tell";
        case lingo::Opcode::PUSH_LIST: return "pop N items; push [list]";
        case lingo::Opcode::PUSH_PROP_LIST: return "pop N key/value pairs; push [propList]";
        case lingo::Opcode::SWAP: return "swap top two stack values";
        case lingo::Opcode::PUSH_INT8:
        case lingo::Opcode::PUSH_INT16:
        case lingo::Opcode::PUSH_INT32:
            return "push " + std::to_string(arg);
        case lingo::Opcode::PUSH_FLOAT32:
            return "push " + floatArgumentToString(arg);
        case lingo::Opcode::PUSH_CONS: {
            const auto& literals = script.literals();
            if (arg >= 0 && arg < static_cast<int>(literals.size())) {
                const auto& literal = literals[static_cast<std::size_t>(arg)];
                return "push literal[" + std::to_string(arg) + "] = " +
                       format::formatLiteralValue(literal.value, 40);
            }
            return "push literal[" + std::to_string(arg) + "]";
        }
        case lingo::Opcode::PUSH_SYMB: return "push #" + resolveScriptName(names, arg);
        case lingo::Opcode::PUSH_VAR_REF: return "push @" + resolveScriptName(names, arg) + " (variable ref)";
        case lingo::Opcode::PUSH_CHUNK_VAR_REF: return "push chunk varRef @" + resolveScriptName(names, arg);
        case lingo::Opcode::PUSH_ARG_LIST: return "build argList, count=" + std::to_string(arg);
        case lingo::Opcode::PUSH_ARG_LIST_NO_RET: return "build argList (no return), count=" + std::to_string(arg);
        case lingo::Opcode::GET_GLOBAL:
        case lingo::Opcode::GET_GLOBAL2:
            return "push global(" + resolveScriptName(names, arg) + ")";
        case lingo::Opcode::SET_GLOBAL:
        case lingo::Opcode::SET_GLOBAL2:
            return "pop -> global(" + resolveScriptName(names, arg) + ")";
        case lingo::Opcode::GET_PROP: return "push property(" + resolveScriptName(names, arg) + ")";
        case lingo::Opcode::SET_PROP: return "pop -> property(" + resolveScriptName(names, arg) + ")";
        case lingo::Opcode::GET_PARAM: return "push param(" + resolveParamName(names, handler, arg) + ")";
        case lingo::Opcode::SET_PARAM: return "pop -> param(" + resolveParamName(names, handler, arg) + ")";
        case lingo::Opcode::GET_LOCAL: return "push local(" + resolveLocalName(names, handler, arg) + ")";
        case lingo::Opcode::SET_LOCAL: return "pop -> local(" + resolveLocalName(names, handler, arg) + ")";
        case lingo::Opcode::GET_OBJ_PROP: return "pop obj; push obj." + resolveScriptName(names, arg);
        case lingo::Opcode::SET_OBJ_PROP: return "pop val, obj; obj." + resolveScriptName(names, arg) + " = val";
        case lingo::Opcode::GET_MOVIE_PROP: return "push the " + resolveScriptName(names, arg);
        case lingo::Opcode::SET_MOVIE_PROP: return "pop -> the " + resolveScriptName(names, arg);
        case lingo::Opcode::GET_TOP_LEVEL_PROP: return "push top-level " + resolveScriptName(names, arg);
        case lingo::Opcode::GET_CHAINED_PROP: return "pop obj; push obj." + resolveScriptName(names, arg) + " (chained)";
        case lingo::Opcode::LOCAL_CALL: return "call " + resolveScriptName(names, arg) + "(args)";
        case lingo::Opcode::EXT_CALL: return "call external " + resolveScriptName(names, arg) + "(args)";
        case lingo::Opcode::OBJ_CALL: return "pop obj; call obj." + resolveScriptName(names, arg) + "(args)";
        case lingo::Opcode::OBJ_CALL_V4: return "call (v4) " + resolveScriptName(names, arg) + "(args)";
        case lingo::Opcode::TELL_CALL: return "tell target: call " + resolveScriptName(names, arg) + "(args)";
        case lingo::Opcode::JMP: return "jump -> offset " + std::to_string(arg);
        case lingo::Opcode::JMP_IF_Z: return "pop; jump if FALSE -> offset " + std::to_string(arg);
        case lingo::Opcode::END_REPEAT: return "jump (end repeat) -> offset " + std::to_string(arg);
        case lingo::Opcode::PUT: return "put value into " + resolveScriptName(names, arg);
        case lingo::Opcode::PUT_CHUNK: return "put value into chunk of " + resolveScriptName(names, arg);
        case lingo::Opcode::DELETE_CHUNK: return "delete chunk of " + resolveScriptName(names, arg);
        case lingo::Opcode::GET: return "push " + resolveScriptName(names, arg);
        case lingo::Opcode::SET: return "pop -> " + resolveScriptName(names, arg);
        case lingo::Opcode::THE_BUILTIN: return "push the " + resolveScriptName(names, arg) + " (builtin)";
        case lingo::Opcode::NEW_OBJ: return "pop args; push new(" + resolveScriptName(names, arg) + ")";
        case lingo::Opcode::PEEK: return "peek stack[" + std::to_string(arg) + "] (dup)";
        case lingo::Opcode::POP: return "discard top " + std::to_string(arg) + " stack value(s)";
        case lingo::Opcode::CALL_JAVASCRIPT: return "call JavaScript bridge";
        case lingo::Opcode::INVALID:
            return {};
    }
    return {};
}

int frameCount(const DirectorFile& movie) {
    return movie.scoreChunk() ? movie.scoreChunk()->getFrameCount() : 1;
}

std::string sanitizeFileName(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-') {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('_');
        }
    }
    return result;
}

} // namespace

std::string memberTypeDisplayName(cast::MemberType type) {
    return std::string(cast::name(type));
}

std::string scriptTypeDisplayName(chunks::ScriptChunkType type) {
    switch (type) {
        case chunks::ScriptChunkType::Score:
            return "Score";
        case chunks::ScriptChunkType::Behavior:
            return "Behavior";
        case chunks::ScriptChunkType::MovieScript:
            return "Movie Script";
        case chunks::ScriptChunkType::Parent:
            return "Parent";
        case chunks::ScriptChunkType::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

std::string selectionTypeDisplayName(SelectionType type) {
    switch (type) {
        case SelectionType::None:
            return "None";
        case SelectionType::Sprite:
            return "Sprite";
        case SelectionType::CastMember:
            return "Cast Member";
        case SelectionType::Frame:
            return "Frame";
        case SelectionType::ScoreCell:
            return "Score Cell";
    }
    return "None";
}

std::string selectionDetails(const SelectionState& selection) {
    std::ostringstream out;
    out << selectionTypeDisplayName(selection.type);
    switch (selection.type) {
        case SelectionType::None:
            break;
        case SelectionType::Sprite:
            out << " channel " << selection.channel << " frame " << selection.frame;
            break;
        case SelectionType::CastMember:
            out << " cast " << selection.castLib << " member " << selection.memberNum;
            break;
        case SelectionType::Frame:
            out << " " << selection.frame;
            break;
        case SelectionType::ScoreCell:
            out << " channel " << selection.channel << " frame " << selection.frame;
            break;
    }
    return out.str();
}

std::string messageWindowWelcomeText() {
    return "Welcome to LibreShockwave Editor\n-- Type Lingo commands below\n\n";
}

std::string messageWindowCommandTranscript(std::string_view command) {
    const auto trimmed = trimCopy(command);
    if (trimmed.empty()) {
        return {};
    }
    std::ostringstream out;
    out << ">> " << trimmed << "\n";
    return out.str();
}

std::string messageWindowResultTranscript(std::string_view command, const lingo::Datum& result) {
    auto transcript = messageWindowCommandTranscript(command);
    if (transcript.empty()) {
        return {};
    }
    transcript.append(lingo::vm::datum::formatBrief(result));
    transcript.push_back('\n');
    return transcript;
}

std::string messageWindowErrorTranscript(std::string_view command, std::string_view error) {
    auto transcript = messageWindowCommandTranscript(command);
    if (transcript.empty()) {
        return {};
    }
    transcript.append("-- Error: ");
    transcript.append(error);
    transcript.push_back('\n');
    return transcript;
}

bool messageWindowIsClearCommand(std::string_view command) {
    return toLower(trimCopy(command)) == "clear";
}

std::optional<std::string> messageWindowExpressionForCommand(std::string_view command) {
    const auto trimmed = trimCopy(command);
    if (trimmed.empty() || messageWindowIsClearCommand(trimmed)) {
        return std::nullopt;
    }

    const auto lowered = toLower(trimmed);
    if (lowered == "put") {
        return std::nullopt;
    }
    if (lowered.starts_with("put ")) {
        return trimCopy(std::string_view(trimmed).substr(4));
    }
    if (trimmed.front() == '?') {
        const auto expression = trimCopy(std::string_view(trimmed).substr(1));
        return expression.empty() ? std::nullopt : std::optional<std::string>{expression};
    }
    return trimmed;
}

std::span<const std::string_view> toolPaletteTools() {
    return kToolPaletteTools;
}

std::string toolPaletteActiveToolText(std::string_view toolName) {
    std::ostringstream out;
    out << "Tool: " << toolName;
    return out.str();
}

std::string toolPaletteStageToolText(std::string_view toolName, int x, int y) {
    std::ostringstream out;
    out << "Tool: " << toolName << " @ " << x << "," << y;
    return out.str();
}

std::string_view toolPaletteNudgeDirectionText(ToolNudgeDirection direction) {
    switch (direction) {
        case ToolNudgeDirection::Left: return "left";
        case ToolNudgeDirection::Right: return "right";
        case ToolNudgeDirection::Up: return "up";
        case ToolNudgeDirection::Down: return "down";
    }
    return "";
}

std::string toolPaletteNudgeToolText(std::string_view toolName, std::string_view direction, int pixels) {
    std::ostringstream out;
    out << "Tool: " << toolName << " nudge " << direction << " " << std::max(1, pixels) << " px";
    return out.str();
}

std::string toolPaletteSelectedStatusText(std::string_view toolName) {
    std::ostringstream out;
    out << "Selected tool: " << toolName;
    return out.str();
}

std::string toolPaletteToolTipText(std::string_view toolName) {
    std::ostringstream out;
    out << toolName << " tool";
    return out.str();
}

std::string toolPaletteStageInteractionText(std::string_view toolName, int x, int y) {
    std::ostringstream out;
    out << "Tool " << toolName << " at " << x << ", " << y << " (stage editing not yet implemented)";
    return out.str();
}

std::string toolPaletteStageSpriteMoveText(std::string_view toolName, int channel, int x, int y) {
    std::ostringstream out;
    out << "Tool " << toolName << " moved channel " << channel << " to " << x << ", " << y
        << " (runtime sprite)";
    return out.str();
}

std::string toolPaletteStageNoSpriteText(std::string_view toolName) {
    return "Tool " + std::string{toolName} + " needs a selected score sprite or cell";
}

std::string toolPaletteStageSpriteMoveFailedText(std::string_view toolName, int channel) {
    std::ostringstream out;
    out << "Tool " << toolName << " could not move channel " << channel;
    return out.str();
}

std::string toolPaletteNudgeSpriteText(std::string_view toolName,
                                       std::string_view direction,
                                       int pixels,
                                       int channel,
                                       int x,
                                       int y) {
    std::ostringstream out;
    out << "Tool " << toolName << " nudged channel " << channel << " " << direction
        << " by " << std::max(1, pixels) << " px to " << x << ", " << y
        << " (runtime sprite)";
    return out.str();
}

std::string toolPaletteNudgeNoSpriteText(std::string_view toolName, std::string_view direction, int pixels) {
    std::ostringstream out;
    out << "Tool " << toolName << " nudge " << direction << " by " << std::max(1, pixels)
        << " px needs a selected score sprite or cell";
    return out.str();
}

std::string toolPaletteNudgeSpriteFailedText(std::string_view toolName,
                                             std::string_view direction,
                                             int pixels,
                                             int channel) {
    std::ostringstream out;
    out << "Tool " << toolName << " could not nudge channel " << channel << " "
        << direction << " by " << std::max(1, pixels) << " px";
    return out.str();
}

std::string toolPaletteNudgeInteractionText(std::string_view toolName, std::string_view direction, int pixels) {
    std::ostringstream out;
    out << "Tool " << toolName << " nudge " << direction << " by " << std::max(1, pixels) << " px"
        << " (stage editing not yet implemented)";
    return out.str();
}

std::span<const std::string_view> paintWindowTools() {
    return kPaintWindowTools;
}

std::string paintWindowToolTipText(std::string_view toolName) {
    return toolPaletteToolTipText(toolName);
}

std::span<const PaintWindowActionSpec> paintWindowViewActionSpecs() {
    return kPaintWindowViewActionSpecs;
}

std::string_view paintWindowColorActionLabel() {
    return "Color...";
}

std::string_view paintWindowColorActionTooltip() {
    return "Choose Paint Color";
}

std::string paintWindowToolStatus(std::string_view toolName, bool hasBitmap, int brushSize) {
    std::ostringstream out;
    out << " " << toolName << " tool selected";
    if (toolName == "Brush" || toolName == "Eraser" || toolName == "Pencil") {
        out << " (" << std::max(1, brushSize) << " px)";
    }
    if (hasBitmap) {
        if (toolName == "Pencil" || toolName == "Brush" || toolName == "Eraser" || toolName == "Fill" ||
            toolName == "Line" || toolName == "Rect" || toolName == "Oval") {
            out << " (runtime bitmap editing)";
        } else {
            out << " (bitmap editing not yet implemented)";
        }
    } else {
        out << " - no bitmap selected";
    }
    return out.str();
}

std::string paintWindowInitialText() {
    return "No bitmap selected";
}

std::string paintWindowReadyStatus() {
    return " Ready";
}

std::string paintWindowErrorStatus() {
    return " Error";
}

std::string paintWindowDecodeFailedText() {
    return "Failed to decode bitmap";
}

std::string routedMemberSelectedStatusText() {
    return " Selected cast member";
}

std::string paintWindowLoadedStatus(std::string_view memberName,
                                    int memberNumber,
                                    int width,
                                    int height,
                                    int bitDepth) {
    std::ostringstream out;
    out << " " << (!memberName.empty() ? std::string{memberName} : "#" + std::to_string(memberNumber))
        << "  " << width << "x" << height
        << "  " << bitDepth << "-bit";
    return out.str();
}

std::string paintWindowZoomStatus(std::string_view baseStatus, double zoom) {
    const int percent = static_cast<int>(std::lround(std::max(0.0, zoom) * 100.0));
    std::ostringstream out;
    out << baseStatus << "  Zoom " << percent << "%";
    return out.str();
}

std::string paintWindowColorStatus(int red, int green, int blue) {
    const auto clampChannel = [](int channel) {
        return std::clamp(channel, 0, 255);
    };
    std::ostringstream out;
    out << " Paint color #"
        << std::uppercase << std::hex << std::setfill('0')
        << std::setw(2) << clampChannel(red)
        << std::setw(2) << clampChannel(green)
        << std::setw(2) << clampChannel(blue);
    return out.str();
}

std::string paintWindowRuntimeEditStatus(std::string_view toolName, int x, int y) {
    std::ostringstream out;
    out << " " << toolName << " applied at " << x << "," << y << " (runtime bitmap)";
    return out.str();
}

std::string paintWindowRuntimeEditFailedStatus(std::string_view toolName) {
    return " " + std::string{toolName} + " edit failed";
}

std::span<const std::string_view> fieldEditorToolbarActions() {
    return kFieldEditorToolbarActions;
}

FieldEditorText fieldEditorText() {
    return {
        .toolbarTitle = " Field Editor ",
        .wrapTooltip = "Toggle line wrapping",
        .scrollTooltip = "Toggle vertical scrolling",
        .wrapEnabledStatus = " Wrap enabled",
        .wrapDisabledStatus = " Wrap disabled",
        .scrollEnabledStatus = " Scroll enabled",
        .scrollDisabledStatus = " Scroll disabled",
        .localTextStatus = " Local text change (not saved)",
        .noMemberLoadedStatus = " No field member loaded",
        .appliedRuntimeStatus = " Applied runtime field text (not saved to file)",
        .applyFailedStatus = " Failed to apply field text",
    };
}

MemberEditorActionText fieldEditorActionText() {
    return {
        .applyText = "Apply",
        .applyTooltip = "Apply field text to the active runtime member",
    };
}

std::string fieldEditorInitialText() {
    return "No field member selected";
}

std::string fieldEditorReadyStatus() {
    return " Ready";
}

std::string fieldEditorNoDataText() {
    return "[Field data not found]";
}

std::string fieldEditorNoDataStatusText() {
    return " No data";
}

std::string fieldEditorLoadedStatus(std::string_view memberName, int memberNumber, std::size_t characterCount) {
    std::ostringstream out;
    out << " " << (!memberName.empty() ? std::string{memberName} : "#" + std::to_string(memberNumber))
        << "  " << characterCount << " characters";
    return out.str();
}

std::span<const std::string_view> vectorShapeToolbarActions() {
    return kVectorShapeToolbarActions;
}

std::string vectorShapeToolTipText(std::string_view toolName) {
    return toolPaletteToolTipText(toolName);
}

std::string vectorShapeToolStatus(std::string_view toolName, bool hasShape) {
    std::ostringstream out;
    out << " " << toolName << " tool selected";
    if (hasShape) {
        if (toolName == "Line" || toolName == "Rect" || toolName == "Ellipse") {
            out << " (runtime vector editing)";
        } else {
            out << " (vector editing not yet implemented)";
        }
    } else {
        out << " - no vector shape selected";
    }
    return out.str();
}

std::string vectorShapeRuntimeEditStatus(std::string_view toolName, cast::ShapeType type) {
    std::ostringstream out;
    out << " " << toolName << " applied " << vectorShapeTypeName(type) << " shape (runtime vector)";
    return out.str();
}

std::string vectorShapeRuntimeEditFailedStatus(std::string_view toolName) {
    return " " + std::string{toolName} + " vector edit failed";
}

std::string vectorShapePlaceholderText() {
    return "No vector shape selected";
}

std::string vectorShapeTypeName(cast::ShapeType type) {
    switch (type) {
        case cast::ShapeType::Rect: return "Rectangle";
        case cast::ShapeType::OvalRect: return "Round Rectangle";
        case cast::ShapeType::Oval: return "Oval";
        case cast::ShapeType::Line: return "Line";
        case cast::ShapeType::Unknown: return "Unknown";
    }
    return "Unknown";
}

std::string vectorShapeLoadedStatus(std::string_view memberName, int memberNumber, const cast::ShapeInfo* shape) {
    std::ostringstream out;
    out << " " << (memberName.empty() ? "#" + std::to_string(memberNumber) : std::string(memberName)) << "  ";
    if (shape == nullptr) {
        out << "Shape data not found";
    } else {
        out << vectorShapeTypeName(shape->shapeType) << " "
            << shape->width << "x" << shape->height
            << " line " << shape->lineThickness
            << (shape->isFilled() ? " filled" : " outline");
    }
    return out.str();
}

std::string vectorShapeDetailsText(const cast::ShapeInfo& shape) {
    std::ostringstream out;
    out << "Type: " << vectorShapeTypeName(shape.shapeType) << "\n"
        << "Size: " << shape.width << " x " << shape.height << "\n"
        << "Registration: " << shape.regX << ", " << shape.regY << "\n"
        << "Filled: " << (shape.isFilled() ? "yes" : "no") << "\n"
        << "Line thickness: " << shape.lineThickness << "\n"
        << "Line direction: " << shape.lineDirection << "\n"
        << "Color index: " << shape.color << "\n"
        << "Back color index: " << shape.backColor << "\n"
        << "Fill type: " << shape.fillType;
    return out.str();
}

std::string soundWindowInitialText() {
    return "No sound member selected";
}

std::string soundWindowReadyStatus() {
    return " Ready";
}

std::string soundWindowInitialTimeText() {
    return "0.0s / 0.0s";
}

std::string soundWindowNoDataText() {
    return previewSoundDataNotFoundText();
}

std::string soundWindowPlaybackUnavailableStatus() {
    return " Playback backend unavailable";
}

std::string soundWindowPlaybackErrorStatusText(std::string_view error) {
    const auto text = soundWindowText();
    if (error.empty()) {
        return std::string{text.playbackErrorStatus};
    }

    std::ostringstream out;
    out << text.playbackErrorStatus << ": " << error;
    return out.str();
}

std::string soundWindowTemporaryFileTemplate(std::string_view extension) {
    std::ostringstream out;
    out << "libreshockwave-sound-XXXXXX.";
    if (extension.empty()) {
        out << "bin";
    } else {
        out << extension;
    }
    return out.str();
}

SoundWindowText soundWindowText() {
    return {
        .playButtonText = "Play",
        .stopButtonText = "Stop",
        .noDataTooltip = "No sound data found",
        .playingStatus = " Playing...",
        .stoppedStatus = " Stopped",
        .playbackErrorStatus = " Playback error",
    };
}

std::string soundWindowLoadedStatus(std::string_view memberName,
                                    int memberNumber,
                                    bool hasSound,
                                    double durationSeconds) {
    std::ostringstream out;
    out << " " << (!memberName.empty() ? std::string{memberName} : "#" + std::to_string(memberNumber));
    if (!hasSound) {
        out << "  Sound data not found";
    } else {
        out << "  Sound loaded";
        if (durationSeconds > 0.0) {
            out << "  " << std::fixed << std::setprecision(1) << durationSeconds << "s";
        }
    }
    return out.str();
}

std::string soundWindowTimeText(double currentSeconds, double totalSeconds) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << std::max(0.0, currentSeconds)
        << "s / "
        << std::max(0.0, totalSeconds)
        << "s";
    return out.str();
}

std::span<const std::string_view> textEditorStyleActions() {
    return kTextEditorStyleActions;
}

std::span<const std::string_view> textEditorFontChoices() {
    return kTextEditorFontChoices;
}

std::span<const std::string_view> textEditorSizeChoices() {
    return kTextEditorSizeChoices;
}

TextEditorText textEditorText() {
    return {
        .boldTooltip = "Bold",
        .italicTooltip = "Italic",
        .underlineTooltip = "Underline",
        .localFormattingStatus = " Local formatting change (not saved)",
        .localTextStatus = " Local text change (not applied)",
        .noMemberLoadedStatus = " No text member loaded",
        .appliedRuntimeStatus = " Applied runtime text change (not saved to file)",
        .applyFailedStatus = " Failed to apply text change",
    };
}

TextFormattingDialogText textFormattingDialogText() {
    return {
        .fontTitle = "Font",
        .fontNoEditorText = "Open the Text or Field panel before changing font settings.",
        .fontTextStatus = " Local font change (not saved)",
        .fontFieldStatus = " Local font change (not applied)",
        .paragraphTitle = "Paragraph",
        .paragraphNoEditorText = "Open the Text panel before changing paragraph settings.",
        .alignmentLabel = "Alignment:",
        .leftIndentLabel = "Left indent:",
        .rightIndentLabel = "Right indent:",
        .firstLineLabel = "First line:",
        .lineSpacingLabel = "Line spacing:",
        .paragraphStatus = " Local paragraph change (not saved)",
    };
}

std::span<const std::string_view> paragraphAlignmentLabels() {
    return kParagraphAlignmentLabels;
}

MemberEditorActionText textEditorActionText() {
    return {
        .applyText = "Apply",
        .applyTooltip = "Apply text to the active runtime member",
    };
}

std::string textEditorInitialText() {
    return "No text member selected";
}

std::string textEditorReadyStatus() {
    return " Ready";
}

std::string textEditorNoDataText() {
    return previewTextDataNotFoundText();
}

std::string textEditorLoadedStatus(std::string_view memberName, int memberNumber, bool hasText) {
    std::ostringstream out;
    out << " " << (!memberName.empty() ? std::string{memberName} : "#" + std::to_string(memberNumber));
    out << (hasText ? "  Text loaded" : "  Text data not found");
    return out.str();
}

std::string textEditorContent(const chunks::TextChunk* textChunk) {
    if (textChunk == nullptr) {
        return textEditorNoDataText();
    }
    return normalizedLineEndings(textChunk->text());
}

std::span<const std::string_view> colorPaletteChoices() {
    return kColorPaletteChoices;
}

ColorPaletteWindowText colorPaletteWindowText() {
    return {
        .selectorLabel = "Palette: ",
        .unavailableText = "Built-in palette data not available yet",
    };
}

std::string colorPalettePlaceholderText() {
    return "Select a palette to preview colors";
}

std::string colorPaletteStatusText(std::string_view paletteName) {
    std::ostringstream out;
    out << "Palette: " << paletteName;
    return out.str();
}

std::string colorPaletteLoadedStatusText(std::string_view paletteName, std::size_t colorCount) {
    std::ostringstream out;
    out << "Palette: " << paletteName << " (" << colorCount << " colors)";
    return out.str();
}

std::string colorPaletteUnavailableText(std::string_view paletteName) {
    std::ostringstream out;
    out << paletteName << "\n" << colorPaletteWindowText().unavailableText;
    return out.str();
}

std::optional<std::string> colorPaletteBuiltInSymbol(std::string_view paletteChoice) {
    if (paletteChoice == "System - Win") {
        return "systemWin";
    }
    if (paletteChoice == "System - Mac") {
        return "systemMac";
    }
    if (paletteChoice == "Rainbow") {
        return "rainbow";
    }
    if (paletteChoice == "Grayscale") {
        return "grayscale";
    }
    if (paletteChoice == "Metallic") {
        return "metallic";
    }
    return std::nullopt;
}

std::string paletteDescription(int paletteId) {
    switch (paletteId) {
        case -1:
            return "System Mac";
        case -2:
            return "Rainbow";
        case -3:
            return "Grayscale";
        case -4:
            return "Pastels";
        case -5:
            return "Vivid";
        case -6:
            return "NTSC";
        case -7:
            return "Metallic";
        case -101:
            return "System Windows";
        case -102:
            return "System Windows (D4)";
        default:
            break;
    }
    if (paletteId >= 0) {
        return "Cast Member #" + std::to_string(paletteId + 1);
    }
    return "Unknown (" + std::to_string(paletteId) + ")";
}

std::span<const std::string_view> debugToolbarActions() {
    return kDebugToolbarActions;
}

MainMenuText mainMenuText() {
    return kMainMenuText;
}

std::string viewZoomMenuItemText(int percent) {
    return std::to_string(std::max(0, percent)) + "%";
}

std::span<const FileCommandSpec> fileCommandSpecs() {
    return kFileCommandSpecs;
}

FileCreationText fileCreationText() {
    return kFileCreationText;
}

std::span<const std::string_view> fileCreationStageSizeChoices() {
    return kFileCreationStageSizeChoices;
}

std::string fileCreationMovieStatusText(std::string_view stageSize,
                                        int frameCount,
                                        int spriteChannels) {
    std::ostringstream out;
    out << "Prepared new movie: "
        << stageSize
        << ", "
        << frameCount
        << " frames, "
        << spriteChannels
        << " sprite channels";
    return out.str();
}

std::string fileCreationCastStatusText(std::string_view castType,
                                       std::string_view castName) {
    return "Prepared new " + std::string{castType} + " cast: " + std::string{castName};
}

FileSaveText fileSaveText() {
    return kFileSaveText;
}

std::string fileSavePreparedDialogText(std::string_view path) {
    std::ostringstream out;
    out << "Prepared save for:\n"
        << path
        << "\n\n"
        << kFileSaveText.savePendingText;
    return out.str();
}

std::string fileSaveAsPreparedDialogText(std::string_view path) {
    std::ostringstream out;
    out << "Prepared save as:\n"
        << path
        << "\n\n"
        << kFileSaveText.savePendingText;
    return out.str();
}

std::string fileSavePreparedStatusText(std::string_view displayName) {
    return "Prepared save for " + std::string{displayName};
}

std::string fileSaveAsPreparedStatusText(std::string_view displayName) {
    return "Prepared save as " + std::string{displayName};
}

std::string fileSaveAllPreparedDialogText() {
    std::ostringstream out;
    out << kFileSaveText.saveAllPreparedText
        << "\n\n"
        << kFileSaveText.saveAllPendingText;
    return out.str();
}

FileOpenImportText fileOpenImportText() {
    return kFileOpenImportText;
}

std::string fileImportPreparedDialogText(std::string_view path) {
    std::ostringstream out;
    out << "Prepared import of:\n"
        << path
        << "\n\n"
        << kFileOpenImportText.importPendingText;
    return out.str();
}

std::string fileImportPreparedStatusText(std::string_view displayName) {
    return "Prepared import of " + std::string{displayName};
}

std::string fileImportAppliedStatusText(std::string_view displayName, int memberNumber) {
    std::ostringstream out;
    out << "Imported " << displayName << " into cast member " << memberNumber;
    return out.str();
}

std::string fileImportFailedStatusText(std::string_view displayName, int memberNumber) {
    std::ostringstream out;
    out << "Import failed for " << displayName << " into cast member " << memberNumber;
    return out.str();
}

std::string fileImportCreatedStatusText(std::string_view displayName, int memberNumber) {
    std::ostringstream out;
    out << "Imported " << displayName << " into new runtime cast member " << memberNumber;
    return out.str();
}

std::string fileImportCreateFailedStatusText(std::string_view displayName) {
    return "Import failed for " + std::string{displayName} + "; no runtime cast member was created";
}

std::span<const PreferencePanelSpec> preferencePanelSpecs() {
    return kPreferencePanelSpecs;
}

GeneralPreferencesText generalPreferencesText() {
    return kGeneralPreferencesText;
}

std::optional<PreferenceCategoryPanelText> preferenceCategoryPanelText(std::string_view panelId) {
    if (panelId == "network") {
        return PreferenceCategoryPanelText{
            .id = "network",
            .title = "Network Preferences",
            .boolOptions = kNetworkPreferenceBoolOptions,
            .intOptions = kNetworkPreferenceIntOptions,
            .choiceOptions = {},
        };
    }
    if (panelId == "script") {
        return PreferenceCategoryPanelText{
            .id = "script",
            .title = "Script Preferences",
            .boolOptions = kScriptPreferenceBoolOptions,
            .intOptions = kScriptPreferenceIntOptions,
            .choiceOptions = kScriptPreferenceChoiceOptions,
        };
    }
    if (panelId == "sprite") {
        return PreferenceCategoryPanelText{
            .id = "sprite",
            .title = "Sprite Preferences",
            .boolOptions = kSpritePreferenceBoolOptions,
            .intOptions = kSpritePreferenceIntOptions,
            .choiceOptions = {},
        };
    }
    if (panelId == "paint") {
        return PreferenceCategoryPanelText{
            .id = "paint",
            .title = "Paint Preferences",
            .boolOptions = kPaintPreferenceBoolOptions,
            .intOptions = kPaintPreferenceIntOptions,
            .choiceOptions = {},
        };
    }
    return std::nullopt;
}

std::string preferenceCategorySavedStatusText(std::string_view title) {
    return std::string{title} + " saved";
}

std::span<const EditCommandSpec> editCommandSpecs() {
    return kEditCommandSpecs;
}

EditCommandStatusText editCommandStatusText() {
    return kEditCommandStatusText;
}

std::string_view editCommandSuccessStatusText(std::string_view commandId) {
    const auto it = std::find_if(kEditCommandSuccessStatuses.begin(),
                                 kEditCommandSuccessStatuses.end(),
                                 [commandId](const auto& status) {
                                     return status.first == commandId;
                                 });
    return it == kEditCommandSuccessStatuses.end() ? std::string_view{} : it->second;
}

std::span<const EditCommandSpec> findCommandSpecs() {
    return kFindCommandSpecs;
}

FindReplaceText findReplaceText() {
    return kFindReplaceText;
}

std::string findReplaceFoundStatusText(std::string_view term) {
    return "Found \"" + std::string{term} + "\"";
}

std::string findReplaceNotFoundStatusText(std::string_view term) {
    return "Text not found: " + std::string{term};
}

std::string findReplaceSingleStatusText(std::string_view term) {
    return "Replaced \"" + std::string{term} + "\"";
}

std::string findReplaceAllStatusText(int count, std::string_view term) {
    std::ostringstream out;
    out << "Replaced " << count << " occurrence(s) of \"" << term << "\"";
    return out.str();
}

std::string findReplaceAllPromptText(std::string_view term) {
    return std::string{kFindReplaceText.replaceAllPromptPrefix} + std::string{term} +
           std::string{kFindReplaceText.replaceAllPromptSuffix};
}

EditSpriteCommandText editSpriteCommandText() {
    return kEditSpriteCommandText;
}

std::string editSpriteFramesStatusText(int frame, std::string_view channelName) {
    std::ostringstream out;
    out << "Editing sprite frames at frame " << frame << ", channel " << channelName;
    return out.str();
}

std::string editEntireSpriteStatusText(int memberNumber) {
    std::ostringstream out;
    out << "Opened member " << memberNumber << " for entire-sprite editing";
    return out.str();
}

std::span<const ModifyCommandSpec> modifyCommandSpecs() {
    return kModifyCommandSpecs;
}

std::span<const PlaybackCommandSpec> playbackCommandSpecs() {
    return kPlaybackCommandSpecs;
}

PlaybackToolbarText playbackToolbarText() {
    return kPlaybackToolbarText;
}

std::string playbackFrameText(int frame, int frameCount) {
    std::ostringstream out;
    out << "Frame: " << frame << " / " << frameCount;
    return out.str();
}

std::span<const ToggleCommandSpec> controlToggleSpecs() {
    return kControlToggleSpecs;
}

std::span<const int> viewZoomPercentages() {
    return kViewZoomPercentages;
}

std::span<const ViewCommandSpec> viewSpriteOverlaySpecs() {
    return kViewSpriteOverlaySpecs;
}

std::span<const ViewCommandSpec> viewTopLevelToggleSpecs() {
    return kViewTopLevelToggleSpecs;
}

std::span<const ViewCommandSpec> viewGridSpecs() {
    return kViewGridSpecs;
}

std::span<const ViewCommandSpec> viewGuideSpecs() {
    return kViewGuideSpecs;
}

ViewGridSettingsText viewGridSettingsText() {
    return kViewGridSettingsText;
}

std::string viewStageZoomStatusText(int percent) {
    std::ostringstream out;
    out << "Stage zoom: " << percent << "%";
    return out.str();
}

std::string viewToggleStatusText(std::string_view commandId, bool enabled) {
    if (commandId == "keyframes") {
        return enabled ? "Score keyframes shown" : "Score keyframes hidden";
    }
    if (commandId == "score-grid-lines") {
        return enabled ? "Score and Stage grid lines shown" : "Score and Stage grid lines hidden";
    }
    if (commandId == "sprite-overlay-info") {
        return enabled ? "Sprite overlay info shown" : "Sprite overlay info hidden";
    }
    if (commandId == "sprite-overlay-paths") {
        return enabled ? "Sprite overlay paths shown" : "Sprite overlay paths hidden";
    }
    if (commandId == "sprite-toolbar") {
        return enabled ? "Sprite toolbar shown" : "Sprite toolbar hidden";
    }
    if (commandId == "stage-grid-snap") {
        return enabled ? "Stage grid snapping enabled" : "Stage grid snapping disabled";
    }
    if (commandId == "stage-guides-show") {
        return enabled ? "Stage guides shown" : "Stage guides hidden";
    }
    if (commandId == "stage-guides-snap") {
        return enabled ? "Stage guide snapping enabled" : "Stage guide snapping disabled";
    }
    return {};
}

std::span<const WindowCommandSpec> windowCommandSpecs() {
    return kWindowCommandSpecs;
}

MainWindowShellText mainWindowShellText() {
    return kMainWindowShellText;
}

MainWindowSummaryText mainWindowSummaryText() {
    return kMainWindowSummaryText;
}

std::string mainWindowTitleForMovie(std::string_view fileName) {
    if (fileName.empty()) {
        return std::string{kMainWindowShellText.defaultTitle};
    }
    return std::string{kMainWindowShellText.titlePrefix} + std::string{fileName};
}

std::string mainWindowMovieSummaryText(const MovieSummary& summary) {
    std::ostringstream out;
    out << "Version " << summary.version
        << "\nTempo " << summary.tempo
        << "\nChannels " << summary.channelCount
        << "\nExternal casts " << summary.externalCastCount;
    return out.str();
}

std::string mainWindowCastSummaryText(const MovieSummary& summary) {
    std::ostringstream out;
    out << "Cast members: " << summary.castMemberCount
        << "\nCasts: " << summary.castCount
        << "\nPalettes: " << summary.paletteCount;
    return out.str();
}

std::string mainWindowScoreSummaryText(const MovieSummary& summary) {
    std::ostringstream out;
    out << "Frames: " << summary.frameCount
        << "\nChannels: " << summary.channelCount
        << "\nTempo: " << summary.tempo;
    return out.str();
}

PanelContextCommandText panelContextCommandText() {
    return kPanelContextCommandText;
}

std::span<const InsertCommandSpec> insertCommandSpecs() {
    return kInsertCommandSpecs;
}

InsertActionText insertActionText() {
    return kInsertActionText;
}

std::string insertKeyframePreparedText(int frame, std::string_view channelName) {
    std::ostringstream out;
    out << "Added a session keyframe preview at frame "
        << frame
        << ", channel "
        << channelName
        << ".\n\n"
        << kInsertActionText.keyframePendingSuffix;
    return out.str();
}

std::string insertKeyframeStatusText(int frame, std::string_view channelName) {
    std::ostringstream out;
    out << "Added session keyframe preview at frame " << frame << ", " << channelName;
    return out.str();
}

std::string insertMarkerPreparedText(std::string_view markerName, int frame) {
    const auto name = markerName.empty() ? kInsertActionText.markerDefaultName : markerName;
    std::ostringstream out;
    out << "Prepared marker \""
        << name
        << "\" at frame "
        << frame
        << ".\n\n"
        << kInsertActionText.markerPendingSuffix;
    return out.str();
}

std::string insertMarkerStatusText(int frame) {
    std::ostringstream out;
    out << "Prepared marker at frame " << frame;
    return out.str();
}

std::string removeFramePreparedText(int frame, int frameCount) {
    std::ostringstream out;
    out << "Added a session removal preview for frame "
        << frame
        << " of "
        << frameCount
        << ".\n\n"
        << kInsertActionText.removeFramePendingSuffix;
    return out.str();
}

std::string removeFrameStatusText(int frame) {
    std::ostringstream out;
    out << "Added session removal preview for frame " << frame;
    return out.str();
}

std::string insertMediaElementPreparedText(std::string_view mediaType) {
    std::ostringstream out;
    out << "Prepared creation of a "
        << mediaType
        << " media element.\n\n"
        << kInsertActionText.mediaElementPendingSuffix;
    return out.str();
}

std::string insertMediaElementStatusText(std::string_view mediaType) {
    std::ostringstream out;
    out << "Prepared " << mediaType << " media element creation";
    return out.str();
}

std::span<const MediaElementSpec> mediaElementSpecs() {
    return kMediaElementSpecs;
}

const MediaElementSpec* findMediaElementSpec(std::string_view menuText) {
    const auto specs = mediaElementSpecs();
    const auto found = std::find_if(specs.begin(), specs.end(), [menuText](const auto& spec) {
        return spec.menuText == menuText || spec.id == menuText;
    });
    return found == specs.end() ? nullptr : &*found;
}

std::span<const DockCommandSpec> dockCommandSpecs() {
    return kDockCommandSpecs;
}

std::span<const MoveDockCommandSpec> moveDockCommandSpecs() {
    return kMoveDockCommandSpecs;
}

std::span<const SplitCommandSpec> splitCommandSpecs() {
    return kSplitCommandSpecs;
}

std::span<const DebugCommandSpec> debugCommandSpecs() {
    return kDebugCommandSpecs;
}

std::string debugToolbarToolTipText(std::string_view toolbarText, std::string_view shortcut) {
    if (shortcut.empty()) {
        return std::string{toolbarText};
    }

    std::ostringstream out;
    out << toolbarText << " (" << shortcut << ")";
    return out.str();
}

std::span<const DebugContextCommandSpec> debugBytecodeContextCommandSpecs() {
    return kDebugBytecodeContextCommandSpecs;
}

std::span<const HelpCommandSpec> helpCommandSpecs() {
    return kHelpCommandSpecs;
}

DebugWatchDialogText debugWatchDialogText() {
    return kDebugWatchDialogText;
}

TraceHandlerDialogText traceHandlerDialogText() {
    return kTraceHandlerDialogText;
}

std::vector<std::string> traceHandlerNamesFromInput(std::string_view input) {
    std::vector<std::string> handlers;
    std::size_t start = 0;
    while (start <= input.size()) {
        const auto comma = input.find(',', start);
        const auto end = comma == std::string_view::npos ? input.size() : comma;
        auto token = input.substr(start, end - start);
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) {
            token.remove_prefix(1);
        }
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) {
            token.remove_suffix(1);
        }
        if (!token.empty()) {
            handlers.emplace_back(token);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return handlers;
}

std::string traceHandlerCurrentText(std::span<const std::string> handlers) {
    if (handlers.empty()) {
        return std::string{kTraceHandlerDialogText.noneText};
    }
    std::vector<std::string> sorted{handlers.begin(), handlers.end()};
    std::sort(sorted.begin(), sorted.end());
    std::ostringstream out;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << sorted[i];
    }
    return out.str();
}

std::string traceHandlerDialogPrompt(std::span<const std::string> handlers) {
    std::ostringstream out;
    out << "Enter handler names to trace (comma-separated), or clear to remove all:\n"
        << "Current: " << traceHandlerCurrentText(handlers);
    return out.str();
}

std::string traceHandlerStatusText(std::size_t handlerCount) {
    if (handlerCount == 0) {
        return "Trace handlers cleared";
    }
    std::ostringstream out;
    out << "Tracing " << handlerCount << " handler(s)";
    return out.str();
}

std::span<const std::string_view> debugStateTabNames() {
    return kDebugStateTabNames;
}

std::span<const std::string_view> debugObjectSectionNames() {
    return kDebugObjectSectionNames;
}

std::span<const std::string_view> debugStackTableColumns() {
    return kDebugStackTableColumns;
}

std::span<const std::string_view> debugVariableTableColumns() {
    return kDebugVariableTableColumns;
}

std::span<const std::string_view> debugWatchTableColumns() {
    return kDebugWatchTableColumns;
}

std::span<const DebugWatchActionSpec> debugWatchActionSpecs() {
    return kDebugWatchActionSpecs;
}

DebugBrowserText debugBrowserText() {
    return kDebugBrowserText;
}

DebugDetailsText debugDetailsText() {
    return kDebugDetailsText;
}

std::string debugHandlerDetailsTitle(std::string_view handlerName) {
    return "Handler: " + std::string{handlerName};
}

std::string debugHandlerDetailsOverviewText(std::string_view scriptName,
                                            std::string_view handlerName,
                                            std::string_view previewText) {
    std::ostringstream out;
    out << "Script: " << scriptName
        << "\nHandler: " << handlerName
        << "\n\n"
        << previewText;
    return out.str();
}

std::string debugDatumDetailsTitle(std::string_view title) {
    return "Datum Details: " + std::string{title};
}

std::string debugDatumDetailsText(std::string_view type, std::string_view value) {
    std::ostringstream out;
    out << "Type: " << type
        << "\n\nValue:\n"
        << value;
    return out.str();
}

std::span<const std::string_view> debugTimeoutTableColumns() {
    return kDebugTimeoutTableColumns;
}

std::span<const std::string_view> debugMoviePropertyTableColumns() {
    return kDebugMoviePropertyTableColumns;
}

std::string debugInitialStatusText() {
    return "Status: Running";
}

std::string debugInitialHandlerText() {
    return "Handler: -";
}

std::string debugRunningStatusText() {
    return "Debug running";
}

std::string debugUnavailableStatusText() {
    return "Debugger unavailable";
}

std::string debugCommandRequestedStatusText(std::string_view commandId) {
    if (commandId == "step-into") {
        return "Debug step into requested";
    }
    if (commandId == "step-over") {
        return "Debug step over requested";
    }
    if (commandId == "step-out") {
        return "Debug step out requested";
    }
    if (commandId == "continue") {
        return "Debug continue requested";
    }
    return {};
}

std::string debugBreakpointsClearedStatusText() {
    return "Breakpoints cleared, including saved breakpoints";
}

std::string debugBreakpointToggledStatusText(int offset) {
    std::ostringstream out;
    out << "Breakpoint toggled at offset " << offset;
    return out.str();
}

std::string debugBreakpointEnabledToggledStatusText(int offset) {
    std::ostringstream out;
    out << "Breakpoint enabled state toggled at offset " << offset;
    return out.str();
}

std::string debugBytecodeListItemText(std::string_view line,
                                      bool hasBreakpoint,
                                      bool breakpointEnabled,
                                      bool currentInstruction) {
    std::ostringstream out;
    if (hasBreakpoint) {
        out << (breakpointEnabled ? "● " : "○ ");
    } else {
        out << "  ";
    }
    out << (currentInstruction ? "▶ " : "  ");
    out << line;
    return out.str();
}

std::string debugNavigatedToHandlerStatusText(std::string_view handlerName) {
    return "Navigated to handler " + std::string{handlerName};
}

std::string debugPausedStatusText(const player::debug::DebugSnapshot& snapshot) {
    std::ostringstream out;
    out << "Status: PAUSED at offset " << snapshot.instructionOffset;
    return out.str();
}

std::string debugPausedHandlerText(const player::debug::DebugSnapshot& snapshot) {
    std::ostringstream out;
    out << "Handler: " << snapshot.handlerName << " (" << snapshot.scriptName << ")";
    return out.str();
}

std::vector<DebugTableRow> debugStackRows(std::span<const lingo::Datum> stack) {
    std::vector<DebugTableRow> rows;
    rows.reserve(stack.size());
    for (std::size_t i = 0; i < stack.size(); ++i) {
        rows.push_back(DebugTableRow{
            .cells = {
                std::to_string(i),
                lingo::vm::datum::getTypeName(stack[i]),
                lingo::vm::datum::format(stack[i]),
            },
            .detailTitle = "Stack[" + std::to_string(i) + "]",
            .detailType = lingo::vm::datum::getTypeName(stack[i]),
            .detailText = lingo::vm::datum::formatDetailed(stack[i], 0),
        });
    }
    return rows;
}

std::vector<DebugTableRow> debugVariableRows(const std::map<std::string, lingo::Datum>& values) {
    std::vector<DebugTableRow> rows;
    rows.reserve(values.size());
    for (const auto& [name, value] : values) {
        rows.push_back(DebugTableRow{
            .cells = {
                name,
                lingo::vm::datum::getTypeName(value),
                lingo::vm::datum::format(value),
            },
            .detailTitle = name,
            .detailType = lingo::vm::datum::getTypeName(value),
            .detailText = lingo::vm::datum::formatDetailed(value, 0),
        });
    }
    return rows;
}

std::vector<DebugTableRow> debugWatchRows(std::span<const player::debug::WatchExpression> watches) {
    std::vector<DebugTableRow> rows;
    rows.reserve(watches.size());
    for (const auto& watch : watches) {
        rows.push_back(DebugTableRow{.id = watch.id, .cells = {
            watch.expression,
            watch.getTypeName(),
            watch.getResultDisplay(),
        }});
    }
    return rows;
}

std::vector<DebugTableRow> debugTimeoutRows(std::span<const player::timeout::TimeoutEntry> timeouts) {
    std::vector<DebugTableRow> rows;
    rows.reserve(timeouts.size());
    for (const auto& timeout : timeouts) {
        rows.push_back(DebugTableRow{
            .cells = {
                timeout.name,
                std::to_string(timeout.periodMs),
                timeout.handler,
                lingo::vm::datum::format(timeout.target),
                timeout.persistent ? "Yes" : "No",
            },
            .detailTitle = "Timeout: " + timeout.name,
            .detailType = lingo::vm::datum::getTypeName(timeout.target),
            .detailText = lingo::vm::datum::formatDetailed(timeout.target, 0),
        });
    }
    return rows;
}

std::span<const std::string_view> debugMoviePropertyNames() {
    return kDebugMoviePropertyNames;
}

std::vector<DebugTableRow> debugMoviePropertyRows(std::span<const std::pair<std::string, lingo::Datum>> values) {
    std::vector<DebugTableRow> rows;
    rows.reserve(values.size());
    for (const auto& [name, value] : values) {
        rows.push_back(DebugTableRow{
            .cells = {
                name,
                lingo::vm::datum::format(value),
            },
            .detailTitle = "Movie: " + name,
            .detailType = lingo::vm::datum::getTypeName(value),
            .detailText = lingo::vm::datum::formatDetailed(value, 0),
        });
    }
    return rows;
}

std::string debugInstructionListingText(const player::debug::DebugSnapshot& snapshot) {
    if (snapshot.allInstructions.empty()) {
        return debugBytecodePlaceholderText();
    }

    std::ostringstream out;
    for (const auto& instruction : snapshot.allInstructions) {
        const bool current = instruction.index == snapshot.instructionIndex ||
                             instruction.offset == snapshot.instructionOffset;
        out << (current ? "=> " : "   ");
        out << (instruction.hasBreakpoint ? "B " : "  ");
        out << "[" << std::right << std::setw(4) << std::setfill('0') << instruction.offset << "] ";
        out << std::setfill(' ') << std::left << std::setw(16) << instruction.opcode;
        if (instruction.argument != 0) {
            out << " " << instruction.argument;
        }
        if (!instruction.annotation.empty()) {
            out << "  -- " << instruction.annotation;
        }
        out << "\n";
    }
    return out.str();
}

std::string debugBytecodePlaceholderText() {
    return "-- Select a handler to view bytecode";
}

std::string debugBytecodeLegendText() {
    return "red dot=breakpoint   gray circle=disabled   arrow=current   blue underline=navigate";
}

std::span<const std::string_view> detailedStackTabNames() {
    return kDetailedStackTabNames;
}

std::string detailedStackWindowTitle() {
    return "Detailed Stack View";
}

std::string detailedStackInitialStatusText() {
    return "Waiting for debugger pause...";
}

std::string detailedStackRunningStatusText() {
    return "Running...";
}

std::string detailedStackCallStackPlaceholderText() {
    return "(no call stack)";
}

std::string detailedStackVmStackPlaceholderText() {
    return "(empty stack)";
}

std::string detailedStackArgumentsPlaceholderText() {
    return "(no arguments)";
}

std::string detailedStackReceiverPlaceholderText() {
    return "(no receiver)";
}

std::string detailedStackPausedStatus(const player::debug::DebugSnapshot& snapshot) {
    std::ostringstream out;
    out << "Paused at: " << snapshot.handlerName << " (offset " << snapshot.instructionOffset << ")";
    return out.str();
}

std::string detailedStackCallStackText(std::span<const player::debug::CallFrame> callStack) {
    if (callStack.empty()) {
        return detailedStackCallStackPlaceholderText();
    }

    std::ostringstream out;
    out << "Call Stack (" << callStack.size() << " frames):\n";
    out << "--------------------------------------------------\n\n";
    for (std::size_t reverseIndex = callStack.size(); reverseIndex > 0; --reverseIndex) {
        const auto& frame = callStack[reverseIndex - 1];
        const auto depth = callStack.size() - reverseIndex;
        out << (depth == 0 ? "> " : "  ");
        out << "[" << depth << "] " << frame.handlerName << "(";
        for (std::size_t i = 0; i < frame.arguments.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << lingo::vm::datum::formatBrief(frame.arguments[i]);
        }
        out << ")\n";
        out << "     in: " << frame.scriptName << "\n";
        if (frame.receiver.has_value()) {
            out << "     me: " << lingo::vm::datum::formatBrief(*frame.receiver) << "\n";
        }
        out << "\n";
    }
    return out.str();
}

std::string detailedStackVmStackText(std::span<const lingo::Datum> stack) {
    if (stack.empty()) {
        return detailedStackVmStackPlaceholderText();
    }

    std::ostringstream out;
    for (std::size_t i = 0; i < stack.size(); ++i) {
        out << "[" << std::setw(3) << i << "] "
            << lingo::vm::datum::formatDetailed(stack[i], 0)
            << "\n";
    }
    return out.str();
}

std::string detailedStackArgumentsText(std::span<const lingo::Datum> arguments) {
    if (arguments.empty()) {
        return detailedStackArgumentsPlaceholderText();
    }

    std::ostringstream out;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        out << "arg" << (i + 1) << " = "
            << lingo::vm::datum::formatDetailed(arguments[i], 0)
            << "\n";
    }
    return out.str();
}

std::string detailedStackReceiverText(const std::optional<lingo::Datum>& receiver) {
    if (!receiver.has_value()) {
        return detailedStackReceiverPlaceholderText();
    }
    return lingo::vm::datum::formatDetailed(*receiver, 0);
}

std::string scriptEditorInitialText() {
    return "-- Select a script member to view";
}

ScriptEditorText scriptEditorText() {
    return ScriptEditorText{
        .castLabel = " Cast: ",
        .scriptLabel = " Script: ",
        .handlerLabel = " Handler: ",
        .lingoToggleText = "Lingo",
        .bytecodeToggleText = "Bytecode",
        .lingoToggleTooltip = "Toggle between bytecode and decompiled Lingo view",
        .emptySummaryText = "Scripts: 0",
    };
}

std::span<const std::string_view> scriptEditorTableColumns() {
    static constexpr std::array<std::string_view, 6> columns{{
        "Id",
        "Name",
        "Type",
        "Handlers",
        "Globals",
        "Properties",
    }};
    return columns;
}

std::string scriptEditorSummaryText(int scripts, int globals, int properties) {
    std::ostringstream out;
    out << "Scripts: " << std::max(0, scripts)
        << "\nGlobals: " << std::max(0, globals)
        << "\nProperties: " << std::max(0, properties);
    return out.str();
}

std::string scriptEditorSelectorLabel(std::string_view scriptName, int scriptId, std::string_view scriptType) {
    std::ostringstream out;
    if (scriptName.empty()) {
        out << "#" << scriptId;
    } else {
        out << scriptName;
    }
    out << " (" << scriptType << ")";
    return out.str();
}

std::string scriptEditorDefaultCastName() {
    return "Internal";
}

std::vector<std::string> castLibrarySelectorLabels(std::span<const std::string> externalCastPaths) {
    std::vector<std::string> labels;
    labels.reserve(externalCastPaths.size() + 1U);
    labels.push_back(castWindowDefaultCastName());
    for (std::size_t index = 0; index < externalCastPaths.size(); ++index) {
        const std::filesystem::path path(externalCastPaths[index]);
        auto label = path.filename().string();
        if (label.empty()) {
            label = "Cast " + std::to_string(index + 1U);
        }
        labels.push_back(label + " (not loaded)");
    }
    return labels;
}

std::string scriptEditorNoScriptsText() {
    return "(No scripts)";
}

std::string scriptEditorNoHandlersText() {
    return "(No handlers)";
}

std::string scriptEditorAllHandlersText() {
    return "(All handlers)";
}

std::string scriptEditorNoBytecodeForMemberText() {
    return "-- No bytecode found for this script member";
}

std::string formatInstructionArgument(const chunks::ScriptChunk::Instruction& instruction,
                                      const chunks::ScriptChunk& script,
                                      const chunks::ScriptNamesChunk* names) {
    const int arg = instruction.argument;
    const auto opcode = instruction.opcode;

    if (opcode == lingo::Opcode::PUSH_CONS) {
        const auto& literals = script.literals();
        if (arg >= 0 && arg < static_cast<int>(literals.size())) {
            const auto& literal = literals[static_cast<std::size_t>(arg)];
            return std::to_string(arg) + " <" + format::getLiteralTypeNameShort(literal.type) + "> " +
                   format::formatLiteralValue(literal.value, 40);
        }
    }

    if (opcode == lingo::Opcode::PUSH_SYMB && hasName(names, arg)) {
        return std::to_string(arg) + " #" + names->getName(arg);
    }

    switch (opcode) {
        case lingo::Opcode::GET_GLOBAL:
        case lingo::Opcode::SET_GLOBAL:
        case lingo::Opcode::GET_GLOBAL2:
        case lingo::Opcode::SET_GLOBAL2:
        case lingo::Opcode::GET_PROP:
        case lingo::Opcode::SET_PROP:
        case lingo::Opcode::GET_OBJ_PROP:
        case lingo::Opcode::SET_OBJ_PROP:
        case lingo::Opcode::GET_MOVIE_PROP:
        case lingo::Opcode::SET_MOVIE_PROP:
        case lingo::Opcode::GET_TOP_LEVEL_PROP:
        case lingo::Opcode::GET_CHAINED_PROP:
            if (hasName(names, arg)) {
                return std::to_string(arg) + " (" + names->getName(arg) + ")";
            }
            break;
        case lingo::Opcode::LOCAL_CALL:
        case lingo::Opcode::EXT_CALL:
        case lingo::Opcode::OBJ_CALL:
        case lingo::Opcode::OBJ_CALL_V4:
        case lingo::Opcode::TELL_CALL:
            if (hasName(names, arg)) {
                return std::to_string(arg) + " [" + names->getName(arg) + "]";
            }
            break;
        case lingo::Opcode::PUT:
        case lingo::Opcode::GET:
        case lingo::Opcode::SET:
            if (hasName(names, arg)) {
                return std::to_string(arg) + " (" + names->getName(arg) + ")";
            }
            break;
        case lingo::Opcode::THE_BUILTIN:
            if (hasName(names, arg)) {
                return std::to_string(arg) + " the " + names->getName(arg);
            }
            break;
        case lingo::Opcode::NEW_OBJ:
            if (hasName(names, arg)) {
                return std::to_string(arg) + " new(" + names->getName(arg) + ")";
            }
            break;
        case lingo::Opcode::PUSH_VAR_REF:
            if (hasName(names, arg)) {
                return std::to_string(arg) + " @" + names->getName(arg);
            }
            break;
        default:
            break;
    }

    if (lingo::isJump(opcode)) {
        return std::to_string(arg) + " -> offset " + std::to_string(arg);
    }

    return std::to_string(arg);
}

std::string formatInstruction(const chunks::ScriptChunk::Instruction& instruction,
                              const chunks::ScriptChunk& script,
                              const chunks::ScriptNamesChunk* names) {
    std::ostringstream out;
    out << "[" << std::setw(4) << std::setfill('0') << instruction.offset << "] ";
    out << std::setfill(' ') << std::left << std::setw(16) << lingo::mnemonic(instruction.opcode);
    if (instruction.rawOpcode >= 0x40) {
        out << " " << formatInstructionArgument(instruction, script, names);
    }
    return out.str();
}

std::string formatScriptHandlerPreview(const chunks::ScriptChunk& script,
                                       const chunks::ScriptChunk::Handler& handler,
                                       const chunks::ScriptNamesChunk* names) {
    std::ostringstream out;
    out << "on " << script.getHandlerName(handler, names) << "\n";
    if (!handler.argNameIds.empty()) {
        out << "  -- args: ";
        for (std::size_t index = 0; index < handler.argNameIds.size(); ++index) {
            if (index > 0) {
                out << ", ";
            }
            const int nameId = handler.argNameIds[index];
            out << (names ? names->getName(nameId) : "#" + std::to_string(nameId));
        }
        out << "\n";
    }
    if (!handler.localNameIds.empty()) {
        out << "  -- locals: ";
        for (std::size_t index = 0; index < handler.localNameIds.size(); ++index) {
            if (index > 0) {
                out << ", ";
            }
            const int nameId = handler.localNameIds[index];
            out << (names ? names->getName(nameId) : "#" + std::to_string(nameId));
        }
        out << "\n";
    }

    out << "  -- bytecodeOffset: " << handler.bytecodeOffset
        << "  bytecodeLength: " << handler.bytecodeLength << "\n\n";
    for (const auto& instruction : handler.instructions) {
        const auto formatted = formatInstruction(instruction, script, names);
        out << "  " << formatted;
        const auto annotation = lingo::vm::trace::InstructionAnnotator::annotate(script,
                                                                                 &handler,
                                                                                 instruction,
                                                                                 names,
                                                                                 true);
        const auto stackComment = stackDescription(script, handler, instruction, names);
        if (!annotation.empty() || !stackComment.empty()) {
            const auto lineLength = formatted.size() + 2U;
            out << std::string(lineLength < 52U ? 52U - lineLength : 1U, ' ') << "; ";
            if (!annotation.empty()) {
                out << annotation;
                if (!stackComment.empty()) {
                    out << " | ";
                }
            }
            out << stackComment;
        }
        out << "\n";
    }
    out << "end\n";
    return out.str();
}

std::string formatScriptHandlerLingoPreview(const chunks::ScriptChunk& script,
                                            const chunks::ScriptChunk::Handler& handler,
                                            const chunks::ScriptNamesChunk* names) {
    try {
        lingo::decompiler::LingoDecompiler decompiler;
        return decompiler.decompileHandler(handler, script, names);
    } catch (const std::exception& ex) {
        return "-- Decompilation error: " + std::string{ex.what()};
    }
}

std::span<const std::string_view> propertyInspectorTabNames() {
    return kPropertyInspectorTabNames;
}

std::span<const std::string_view> propertyInspectorSpriteLabels() {
    return kPropertyInspectorSpriteLabels;
}

std::span<const std::string_view> propertyInspectorMemberLabels() {
    return kPropertyInspectorMemberLabels;
}

std::span<const std::string_view> propertyInspectorMovieLabels() {
    return kPropertyInspectorMovieLabels;
}

PropertyInspectorText propertyInspectorText() {
    return kPropertyInspectorText;
}

std::string propertyInspectorUnsetValueText() {
    return "-";
}

std::string propertyInspectorBehaviorPlaceholderText() {
    return "(Select a sprite to see its behaviors)";
}

std::vector<std::string> propertyInspectorBehaviorValues(const ScoreIntervalRow& row) {
    if (row.castMember < 0) {
        return {propertyInspectorBehaviorPlaceholderText()};
    }
    if (row.scriptId <= 0) {
        return {"(No behaviors attached)"};
    }

    std::ostringstream out;
    out << "Behavior script #" << row.scriptId;
    if (!row.memberName.empty()) {
        out << " on " << row.memberName;
    }
    return {out.str()};
}

std::string propertyInspectorRuntimeBehaviorValue(int instanceId,
                                                 int scriptId,
                                                 std::string_view behaviorRef,
                                                 int propertyCount) {
    std::ostringstream out;
    out << "Runtime behavior #" << instanceId;
    if (scriptId > 0) {
        out << " script #" << scriptId;
    }
    if (!behaviorRef.empty()) {
        out << " - " << behaviorRef;
    }
    out << " (" << propertyCount << (propertyCount == 1 ? " property" : " properties") << ")";
    return out.str();
}

std::string propertyInspectorPendingBehaviorValue(int scriptId) {
    std::ostringstream out;
    out << "Pending behavior script #" << scriptId << " (session)";
    return out.str();
}

std::string propertyInspectorPendingBehaviorRemovalValue(std::string_view behaviorName) {
    return "Pending removal: " + std::string{behaviorName};
}

std::string propertyInspectorBehaviorScriptTooltipText(int scriptId) {
    std::ostringstream out;
    out << "Open behavior script #" << scriptId;
    return out.str();
}

std::string propertyInspectorRuntimeBehaviorScriptTooltipText(int scriptId) {
    std::ostringstream out;
    out << "Open runtime behavior script #" << scriptId;
    return out.str();
}

std::string propertyInspectorMissingBehaviorScriptText(int scriptId) {
    std::ostringstream out;
    out << "Behavior script #"
        << scriptId
        << " is referenced by the selected sprite, but no loaded script member currently exposes that id.";
    return out.str();
}

std::string propertyInspectorAddBehaviorPromptText(int channel, int frame) {
    std::ostringstream out;
    out << kPropertyInspectorText.addBehaviorPromptPrefix
        << channel
        << kPropertyInspectorText.addBehaviorPromptMiddle
        << frame
        << ":";
    return out.str();
}

std::string propertyInspectorPreparedAddBehaviorText(int scriptId, int channel, int frame) {
    std::ostringstream out;
    out << "Added a session behavior preview for script #"
        << scriptId
        << " for sprite channel "
        << channel
        << " at frame "
        << frame
        << ".\n\n"
        << kPropertyInspectorText.behaviorPendingSuffix;
    return out.str();
}

std::string propertyInspectorPreparedAddBehaviorStatusText(int scriptId, int channel) {
    std::ostringstream out;
    out << "Added session behavior script #" << scriptId << " for channel " << channel;
    return out.str();
}

std::string propertyInspectorPreparedRemoveBehaviorText(std::string_view behaviorName,
                                                       int channel,
                                                       int frame) {
    std::ostringstream out;
    out << "Added a session removal preview for \""
        << behaviorName
        << "\" from sprite channel "
        << channel
        << " at frame "
        << frame
        << ".\n\n"
        << kPropertyInspectorText.behaviorPendingSuffix;
    return out.str();
}

std::string propertyInspectorPreparedRemoveBehaviorStatusText(int channel) {
    std::ostringstream out;
    out << "Added session behavior removal preview for channel " << channel;
    return out.str();
}

std::string propertyInspectorCanceledPendingBehaviorStatusText(int channel) {
    std::ostringstream out;
    out << "Canceled session behavior preview for channel " << channel;
    return out.str();
}

std::vector<std::string> propertyInspectorSpriteValues(const ScoreIntervalRow& row) {
    const auto unset = propertyInspectorUnsetValueText();
    std::vector<std::string> values;
    values.reserve(kPropertyInspectorSpriteLabels.size());

    values.push_back(scoreChannelName(row.channel));
    if (row.castMember >= 0) {
        std::ostringstream member;
        member << "Cast " << row.castLib << ", member " << row.castMember;
        if (!row.memberName.empty()) {
            member << " - " << row.memberName;
        }
        values.push_back(member.str());
    } else {
        values.push_back(unset);
    }

    values.push_back(row.hasSpriteData ? std::to_string(row.posX) : unset);
    values.push_back(row.hasSpriteData ? std::to_string(row.posY) : unset);
    values.push_back(row.hasSpriteData ? std::to_string(row.width) : unset);
    values.push_back(row.hasSpriteData ? std::to_string(row.height) : unset);
    values.push_back(row.hasSpriteData ? std::to_string(row.ink) : unset);
    values.push_back(row.hasSpriteData ? std::to_string(row.blend) : unset);
    values.push_back(std::to_string(row.channel));
    values.push_back(row.castMember >= 0 ? "true" : unset);
    values.push_back(unset);
    values.push_back(unset);
    return values;
}

std::vector<std::string> propertyInspectorMemberValues(const CastMemberRow& row, std::string_view castName) {
    std::vector<std::string> values;
    values.reserve(kPropertyInspectorMemberLabels.size());
    values.push_back(row.name.empty() ? "(unnamed)" : row.name);
    values.push_back(std::to_string(row.chunkId));
    values.emplace_back(castName);
    values.push_back(row.type.empty() ? propertyInspectorUnsetValueText() : row.type);
    values.push_back(row.specificDataSize > 0 ? std::to_string(row.specificDataSize) + " bytes"
                                              : propertyInspectorUnsetValueText());
    values.push_back(propertyInspectorUnsetValueText());
    values.push_back(propertyInspectorUnsetValueText());
    return values;
}

std::string propertyInspectorCastMemberHeading(int castLib, int memberNumber) {
    std::ostringstream out;
    out << "Cast " << castLib << ", member " << memberNumber;
    return out.str();
}

std::vector<std::string> propertyInspectorMovieValues(const MovieSummary& summary, std::string_view movieName) {
    std::vector<std::string> values;
    values.reserve(kPropertyInspectorMovieLabels.size());
    values.push_back(movieName.empty() ? propertyInspectorUnsetValueText() : std::string(movieName));
    values.push_back(std::to_string(summary.stageWidth));
    values.push_back(std::to_string(summary.stageHeight));
    values.push_back(propertyInspectorUnsetValueText());
    values.push_back(std::to_string(summary.paletteCount));
    values.push_back(std::to_string(summary.tempo));
    values.push_back(std::to_string(summary.frameCount));
    values.push_back(std::to_string(summary.castCount));
    values.push_back(propertyInspectorUnsetValueText());
    return values;
}

std::span<const std::string_view> castWindowTypeFilterItems() {
    return kCastWindowTypeFilterItems;
}

std::span<const std::string_view> castWindowTableColumns() {
    return kCastWindowTableColumns;
}

CastWindowText castWindowText() {
    return {
        .loadCastText = "Load Cast...",
        .loadCastTooltip = "Load a local file into the selected external cast slot",
        .gridViewText = "Grid",
        .gridViewTooltip = "Thumbnail preview mode",
        .listViewText = "List",
        .listViewTooltip = "List view",
        .searchLabel = " Search: ",
        .searchPlaceholder = "Search",
        .typeLabel = " Type: ",
        .emptySummaryText = "Cast members: 0",
        .selectedMemberPlaceholderText = "Select a cast member",
        .openText = "Open",
        .openInFieldText = "Open in Field",
        .loadExternalCastTitle = "Load External Cast",
        .loadExternalCastNoMovieText = "No movie loaded.",
        .loadExternalCastSelectSlotText = "Select an external cast slot first.",
        .loadExternalCastNotExternalText = "Selected cast is not an external cast.",
        .loadExternalCastFileFilter = "Director Cast Files (*.cst *.cct *.dir *.dxr *.dcr);;All Files (*)",
        .loadExternalCastOpenFailedText = "Unable to open selected cast file.",
        .loadExternalCastLoadFailedText = "Selected file could not be loaded as this cast.",
        .exportMembersTitle = "Export Cast Members",
        .exportMembersNoSelectionText = "Select one or more cast members to export.",
        .exportSelectedDirectoryTitle = "Export Selected Cast Members",
        .exportMemberTitle = "Export Cast Member",
        .supportedExportFileFilter = "Supported Export (*.png *.mp3 *.wav *.pal *.ls *.txt *.bin);;All Files (*)",
        .exportAllSelectedDirectoryTitle = "Export All Selected",
    };
}

std::string castWindowLoadExternalCastDialogTitle(std::string_view authoredName) {
    const auto text = castWindowText();
    if (authoredName.empty()) {
        return std::string{text.loadExternalCastTitle};
    }
    return std::string{text.loadExternalCastTitle} + ": " + std::string{authoredName};
}

std::string castWindowLoadedExternalCastStatus(int castLib) {
    std::ostringstream out;
    out << "Loaded external cast " << castLib;
    return out.str();
}

std::string castWindowDefaultCastName() {
    return "Internal";
}

std::string castWindowReadyStatus() {
    return " Ready";
}

std::string castWindowNoMovieText() {
    return "No movie loaded";
}

std::string castWindowNoMembersText() {
    return "No members";
}

std::string castWindowNoMembersStatusText() {
    return " " + castWindowNoMembersText();
}

std::string castWindowCastLibraryLabel(int castLib,
                                       std::string_view authoredName,
                                       bool internalCast,
                                       bool externalCast,
                                       bool loaded) {
    auto name = authoredName;
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) {
        name.remove_prefix(1);
    }
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
        name.remove_suffix(1);
    }
    std::string label{name};
    if (label.empty()) {
        label = internalCast ? castWindowDefaultCastName() : "Cast " + std::to_string(castLib);
    }
    if (externalCast && !loaded) {
        label += " (not loaded)";
    }
    return label;
}

std::string castWindowMemberDisplayName(std::string_view memberName) {
    if (memberName.empty()) {
        return "(unnamed)";
    }
    return std::string{memberName};
}

std::string castWindowMemberCountStatus(int visibleMembers, int totalMembers) {
    std::ostringstream out;
    out << " " << std::max(0, visibleMembers) << " of " << std::max(0, totalMembers) << " members";
    return out.str();
}

std::string castWindowOpenSelectedButtonText() {
    return "Open Selected";
}

int castThumbnailSize() {
    return 48;
}

CastWindowViewModeSpec castWindowGridViewModeSpec() {
    const int thumbnail = castThumbnailSize();
    return CastWindowViewModeSpec{
        .showDetailColumns = false,
        .rowHeight = thumbnail + 28,
        .previewColumnWidth = thumbnail + 24,
        .statusText = " Thumbnail grid mode",
    };
}

CastWindowViewModeSpec castWindowListViewModeSpec() {
    const int thumbnail = castThumbnailSize();
    return CastWindowViewModeSpec{
        .showDetailColumns = true,
        .rowHeight = thumbnail + 8,
        .previewColumnWidth = thumbnail + 16,
        .statusText = " List view mode",
    };
}

std::string castThumbnailTypeAbbreviation(cast::MemberType type) {
    switch (type) {
        case cast::MemberType::Bitmap:
            return "BMP";
        case cast::MemberType::Picture:
            return "PIC";
        case cast::MemberType::FilmLoop:
            return "LOOP";
        case cast::MemberType::Text:
            return "TXT";
        case cast::MemberType::RichText:
            return "RTF";
        case cast::MemberType::Palette:
            return "PAL";
        case cast::MemberType::Sound:
            return "SND";
        case cast::MemberType::Button:
            return "BTN";
        case cast::MemberType::Shape:
            return "SHP";
        case cast::MemberType::Movie:
            return "MOV";
        case cast::MemberType::DigitalVideo:
            return "VID";
        case cast::MemberType::Script:
            return "SCR";
        case cast::MemberType::Transition:
            return "TRN";
        case cast::MemberType::Xtra:
            return "XTR";
        case cast::MemberType::Font:
            return "FNT";
        case cast::MemberType::Shockwave3D:
            return "3D";
        case cast::MemberType::Null:
            return "NULL";
        case cast::MemberType::Unknown:
            return "?";
    }
    return "?";
}

std::string castWindowExportActionText() {
    return "Export...";
}

std::string castWindowExportSelectedActionText() {
    return "Export All Selected (Ctrl+Shift+E)";
}

std::string castWindowSelectAllActionText() {
    return "Select All (Ctrl+A)";
}

std::string castWindowCopyNameActionText() {
    return "Copy Name";
}

std::string castWindowExportStartingStatus(int memberCount) {
    std::ostringstream out;
    out << " Exporting";
    if (memberCount > 1) {
        out << " " << memberCount << " selected members";
    } else {
        out << " 1 member";
    }
    return out.str();
}

std::string castMemberExportBaseName(const CastMemberRow& row) {
    auto safeName = sanitizeFileName(row.name);
    if (!safeName.empty()) {
        return safeName;
    }
    std::ostringstream out;
    out << (row.type.empty() ? "member" : row.type) << "_" << row.chunkId;
    return sanitizeFileName(out.str());
}

std::string castMemberFallbackExportFileName(int memberNumber) {
    std::ostringstream out;
    out << "member_" << std::max(0, memberNumber) << ".bin";
    return out.str();
}

std::string castMemberExportFileNameWithExtension(const CastMemberRow& row, std::string_view extension) {
    std::ostringstream out;
    out << castMemberExportBaseName(row) << ".";
    if (extension.empty()) {
        out << "bin";
    } else {
        out << extension;
    }
    return out.str();
}

std::string castMemberExportFileName(const CastMemberRow& row) {
    switch (row.memberType) {
        case cast::MemberType::Bitmap:
        case cast::MemberType::Picture:
            return castMemberExportBaseName(row) + ".png";
        case cast::MemberType::Sound:
            return castMemberExportBaseName(row) + ".wav";
        case cast::MemberType::Palette:
            return castMemberExportBaseName(row) + ".pal";
        case cast::MemberType::Script:
            return castMemberExportBaseName(row) + ".ls";
        case cast::MemberType::Text:
        case cast::MemberType::RichText:
        case cast::MemberType::Button:
            return castMemberExportBaseName(row) + ".txt";
        default:
            return castMemberExportBaseName(row) + ".bin";
    }
}

std::string castMemberExportDuplicateFileName(const CastMemberRow& row, int suffix, std::string_view extension) {
    std::ostringstream out;
    out << castMemberExportBaseName(row) << "_" << std::max(1, suffix) << ".";
    if (extension.empty()) {
        out << "bin";
    } else {
        out << extension;
    }
    return out.str();
}

std::string castWindowExportedStatus(int memberCount) {
    std::ostringstream out;
    out << " Exported ";
    if (memberCount == 1) {
        out << "1 member";
    } else {
        out << memberCount << " selected members";
    }
    return out.str();
}

std::string castWindowExportFailedStatus(int memberCount) {
    std::ostringstream out;
    out << " Failed to export ";
    if (memberCount == 1) {
        out << "member";
    } else {
        out << memberCount << " selected members";
    }
    return out.str();
}

int stageWindowDefaultWidth() {
    return 640;
}

int stageWindowDefaultHeight() {
    return 480;
}

std::string stageWindowNoMovieText() {
    return "No movie loaded";
}

std::string stageWindowSummaryText(std::string_view fileName, int width, int height, int frame) {
    std::ostringstream out;
    out << fileName << "\n" << width << " x " << height << "\nFrame " << frame;
    return out.str();
}

std::string stageWindowRenderedTooltip(std::string_view fileName, int frame) {
    std::ostringstream out;
    out << "Rendered " << (fileName.empty() ? "movie" : std::string{fileName}) << " frame " << frame;
    return out.str();
}

std::string stageWindowNetworkTaskStatusText(std::string_view method,
                                             std::string_view url,
                                             std::size_t byteCount) {
    std::ostringstream out;
    out << "Network " << method << ": " << url << " (" << byteCount << " bytes)";
    return out.str();
}

std::string stageWindowNetworkTaskFailedStatusText(std::string_view method,
                                                   std::string_view url,
                                                   std::string_view error) {
    std::ostringstream out;
    out << "Network " << method << " failed: " << url << " (" << error << ")";
    return out.str();
}

std::string stageWindowLocalHttpRootStatusText(std::string_view root) {
    return "Local HTTP root: " + std::string{root};
}

StagePoint stageWindowClampedPoint(int x, int y, int width, int height) {
    return StagePoint{
        .x = std::clamp(x, 0, std::max(0, width - 1)),
        .y = std::clamp(y, 0, std::max(0, height - 1)),
    };
}

int stageWindowDirectorKeyCodeFromBrowserCode(int browserKeyCode) {
    return ::libreshockwave::player::input::DirectorKeyCodes::fromBrowserKeyCode(browserKeyCode);
}

int scoreCellWidth() {
    return 12;
}

int scoreCellHeight() {
    return 14;
}

int scoreHeaderHeight() {
    return 20;
}

int scoreChannelHeaderWidth() {
    return 100;
}

std::span<const std::string_view> scoreIntervalTableColumns() {
    return kScoreIntervalTableColumns;
}

std::span<const std::string_view> scoreSpecialChannelNames() {
    return kScoreSpecialChannelNames;
}

std::string scoreChannelName(int channelIndex) {
    if (channelIndex >= 0 && channelIndex < static_cast<int>(kScoreSpecialChannelNames.size())) {
        return std::string{kScoreSpecialChannelNames[static_cast<std::size_t>(channelIndex)]};
    }

    std::ostringstream out;
    if (channelIndex < static_cast<int>(kScoreSpecialChannelNames.size())) {
        out << "Ch " << (channelIndex + 1);
    } else {
        out << "Ch " << (channelIndex - static_cast<int>(kScoreSpecialChannelNames.size()) + 1);
    }
    return out.str();
}

std::vector<std::string> scoreChannelLabels(int channelCount) {
    std::vector<std::string> labels;
    labels.reserve(static_cast<std::size_t>(std::max(0, channelCount)));
    for (int channel = 0; channel < channelCount; ++channel) {
        labels.push_back(scoreChannelName(channel));
    }
    return labels;
}

std::string scoreIntervalChannelDisplayText(int channelIndex) {
    std::ostringstream out;
    out << channelIndex << " (" << scoreChannelName(channelIndex) << ")";
    return out.str();
}

std::string scoreOverlaySpriteLabel(int channelIndex, int castMember, std::string_view memberName) {
    std::ostringstream out;
    out << "Ch " << channelIndex;
    if (memberName.empty()) {
        out << ", member " << castMember;
    } else {
        out << ": " << memberName;
    }
    return out.str();
}

std::string scoreInitialStatusText() {
    return "Frame: 1 | Channel: -";
}

std::string scoreNoDataStatusText() {
    return "Score: No score data";
}

std::string scoreLoadedStatusText(int frameCount, int channelCount) {
    std::ostringstream out;
    out << frameCount << " frames, " << channelCount << " channels";
    return out.str();
}

std::string scoreFrameStatusText(int frame) {
    std::ostringstream out;
    out << "Frame: " << frame;
    return out.str();
}

std::string scoreCellStatusText(int frame, int channel) {
    std::ostringstream out;
    out << "Frame: " << frame << " | Channel: " << scoreChannelName(channel);
    return out.str();
}

std::string scoreMarkersText(std::span<const ScoreMarker> markers) {
    if (markers.empty()) {
        return "Markers: none";
    }

    std::ostringstream out;
    out << "Markers:";
    for (const auto& marker : markers) {
        out << "  " << marker.frame << ": ";
        if (marker.label.empty()) {
            out << "(unnamed)";
        } else {
            out << marker.label;
        }
    }
    return out.str();
}

std::uint32_t scoreMemberTypeColorRgb(cast::MemberType memberType) {
    switch (memberType) {
        case cast::MemberType::Bitmap:
        case cast::MemberType::Picture:
            return 0x99CCFF;
        case cast::MemberType::Text:
        case cast::MemberType::RichText:
            return 0xFFFF99;
        case cast::MemberType::Shape:
            return 0x99FF99;
        case cast::MemberType::Script:
            return 0xCC99FF;
        case cast::MemberType::Sound:
            return 0xFFCC99;
        case cast::MemberType::FilmLoop:
            return 0x99FFFF;
        case cast::MemberType::Button:
            return 0xFFCCCC;
        case cast::MemberType::Font:
            return 0xCCCCCC;
        case cast::MemberType::Palette:
            return 0xCCFFCC;
        case cast::MemberType::Transition:
            return 0xCCCCFF;
        case cast::MemberType::Xtra:
            return 0xFF9999;
        case cast::MemberType::Null:
        case cast::MemberType::Movie:
        case cast::MemberType::DigitalVideo:
        case cast::MemberType::Shockwave3D:
        case cast::MemberType::Unknown:
            return 0xC8C8C8;
    }
    return 0xC8C8C8;
}

std::uint32_t scoreIntervalBackgroundRgb(const ScoreIntervalRow& interval) {
    if (interval.castMember < 0) {
        return 0xFFFFFF;
    }
    return scoreMemberTypeColorRgb(interval.memberType);
}

std::uint32_t scoreActiveFrameBackgroundRgb() {
    return 0xFFE680;
}

bool scoreIntervalContainsFrame(const ScoreIntervalRow& interval, int frame) {
    return frame >= interval.startFrame && frame <= interval.endFrame;
}

std::string scoreIntervalTooltip(const ScoreIntervalRow& interval) {
    std::ostringstream out;
    out << scoreChannelName(interval.channel);
    out << "\nFrames: " << interval.startFrame << "-" << interval.endFrame;
    if (interval.castMember >= 0) {
        out << "\nCast: " << interval.castLib << ", Member: " << interval.castMember;
        if (!interval.memberName.empty()) {
            out << "\n" << interval.memberName;
        }
        if (!interval.memberTypeName.empty()) {
            out << "\nType: " << interval.memberTypeName;
        }
    }
    return out.str();
}

std::string previewMemberHeader(std::string_view memberKind,
                                std::string_view memberName,
                                int memberNumber,
                                bool blankLineAfterId) {
    std::ostringstream out;
    out << "=== " << memberKind << ": " << memberName << " ===\n\n";
    out << "Member ID: " << memberNumber << "\n";
    if (blankLineAfterId) {
        out << "\n";
    }
    return out.str();
}

std::string previewTextDataNotFoundText() {
    return "[Text data not found]";
}

std::string previewSoundDataNotFoundText() {
    return "[Sound data not found]";
}

std::string previewPaletteDataNotFoundText() {
    return "[Palette data not found]";
}

std::string previewNotUsedInScoreText() {
    return "Not used in score";
}

std::string previewPaletteInfoHeader(int colorCount) {
    std::ostringstream out;
    out << "--- Palette Info ---\n";
    out << "Color Count: " << colorCount << "\n";
    out << "\n--- Colors ---";
    return out.str();
}

std::string previewPaletteColorLine(int index, std::uint32_t rgb) {
    const auto r = static_cast<int>((rgb >> 16U) & 0xFFU);
    const auto g = static_cast<int>((rgb >> 8U) & 0xFFU);
    const auto b = static_cast<int>(rgb & 0xFFU);
    std::ostringstream out;
    out << "[" << std::setw(3) << index << "] #";
    out << std::uppercase << std::hex << std::setfill('0')
        << std::setw(2) << r << std::setw(2) << g << std::setw(2) << b;
    out << std::dec << std::setfill(' ');
    out << " (R:" << std::setw(3) << r << " G:" << std::setw(3) << g << " B:" << std::setw(3) << b << ")";
    return out.str();
}

std::string previewPaletteInfo(const std::vector<std::uint32_t>& colors) {
    std::ostringstream out;
    out << previewPaletteInfoHeader(static_cast<int>(colors.size())) << "\n";
    for (std::size_t i = 0; i < colors.size(); ++i) {
        out << previewPaletteColorLine(static_cast<int>(i), colors[i]) << "\n";
    }
    return out.str();
}

std::string paletteExportText(const std::vector<std::uint32_t>& colors) {
    std::ostringstream out;
    out << "JASC-PAL\n0100\n" << colors.size() << "\n";
    for (const auto rgb : colors) {
        out << static_cast<int>((rgb >> 16U) & 0xFFU) << " "
            << static_cast<int>((rgb >> 8U) & 0xFFU) << " "
            << static_cast<int>(rgb & 0xFFU) << "\n";
    }
    return out.str();
}

std::string textExportText(const chunks::TextChunk& textChunk) {
    const auto& source = textChunk.text();
    std::string text;
    text.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (source[index] == '\r') {
            text.push_back('\n');
            if (index + 1U < source.size() && source[index + 1U] == '\n') {
                ++index;
            }
        } else {
            text.push_back(source[index]);
        }
    }
    return text;
}

std::string scriptExportText(std::string_view memberName,
                             int memberNumber,
                             const chunks::ScriptChunk* script,
                             const chunks::ScriptNamesChunk* names) {
    std::ostringstream out;
    if (script == nullptr) {
        out << "-- No bytecode found for script member #" << memberNumber << "\n";
        return out.str();
    }

    out << "-- Script: " << memberName << "\n";
    out << "-- Type: " << scriptTypeDisplayName(script->resolvedScriptType()) << "\n\n";
    for (const auto& property : script->getPropertyNames(names)) {
        out << "property " << property << "\n";
    }
    if (!script->properties().empty()) {
        out << "\n";
    }
    for (const auto& global : script->getGlobalNames(names)) {
        out << "global " << global << "\n";
    }
    if (!script->globals().empty()) {
        out << "\n";
    }
    for (const auto& handler : script->handlers()) {
        out << formatScriptHandlerPreview(*script, handler, names) << "\n";
    }
    return out.str();
}

PaletteSwatch buildPaletteSwatch(std::span<const std::uint32_t> colors, int swatchSize, int columns) {
    if (colors.empty() || swatchSize <= 0) {
        return PaletteSwatch{};
    }

    const int count = static_cast<int>(colors.size());
    const int cols = columns > 0 ? columns : static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    const int rows = static_cast<int>(std::ceil(static_cast<double>(count) / static_cast<double>(cols)));
    PaletteSwatch swatch{
        .width = cols * swatchSize,
        .height = rows * swatchSize,
        .cells = {},
    };
    swatch.cells.reserve(colors.size());
    for (int index = 0; index < count; ++index) {
        swatch.cells.push_back(PaletteSwatchCell{
            .x = (index % cols) * swatchSize,
            .y = (index / cols) * swatchSize,
            .size = swatchSize,
            .rgb = colors[static_cast<std::size_t>(index)] & 0xFFFFFFU,
        });
    }
    return swatch;
}

std::string previewFrameAppearances(const std::vector<FrameAppearance>& appearances) {
    if (appearances.empty()) {
        return previewNotUsedInScoreText();
    }

    std::vector<std::string> ranges;
    int rangeStart = -1;
    int rangeEnd = -1;
    int lastChannel = -1;
    std::string lastChannelName;

    const auto pushRange = [&ranges](int start, int end, const std::string& channelName) {
        std::ostringstream range;
        if (start == end) {
            range << "Frame " << start << " (" << channelName << ")";
        } else {
            range << "Frames " << start << "-" << end << " (" << channelName << ")";
        }
        ranges.push_back(range.str());
    };

    for (const auto& appearance : appearances) {
        if (lastChannel == appearance.channel && rangeEnd + 1 == appearance.frame) {
            rangeEnd = appearance.frame;
            continue;
        }

        if (rangeStart > 0) {
            pushRange(rangeStart, rangeEnd, lastChannelName);
        }

        rangeStart = appearance.frame;
        rangeEnd = appearance.frame;
        lastChannel = appearance.channel;
        lastChannelName = appearance.channelName;
    }

    if (rangeStart > 0) {
        pushRange(rangeStart, rangeEnd, lastChannelName);
    }

    std::ostringstream out;
    const auto visibleRangeCount = ranges.size() <= 5 ? ranges.size() : 3U;
    for (std::size_t i = 0; i < visibleRangeCount; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << ranges[i];
    }
    if (ranges.size() > 5) {
        out << " ... and " << (ranges.size() - 3U) << " more";
    }
    return out.str();
}

std::string previewScoreAppearances(const std::vector<FrameAppearance>& appearances, bool includePosition) {
    std::ostringstream out;
    if (appearances.empty()) {
        out << previewNotUsedInScoreText() << "\n";
        return out.str();
    }

    out << previewFrameAppearances(appearances) << "\n";
    if (appearances.size() > 20) {
        return out.str();
    }

    out << "\nDetailed appearances:\n";
    for (const auto& appearance : appearances) {
        out << "  Frame " << appearance.frame << ", " << appearance.channelName;
        if (includePosition) {
            out << " at (" << appearance.posX << ", " << appearance.posY << ")";
        }
        if (!appearance.frameLabel.empty()) {
            out << " [" << appearance.frameLabel << "]";
        }
        out << "\n";
    }
    return out.str();
}

std::vector<FrameAppearance> buildFrameAppearancesFromScoreEntries(
    std::span<const chunks::ScoreChunk::FrameChannelEntry> entries,
    const std::map<int, std::string>& frameLabels,
    int memberId,
    const CastMemberIdResolver& resolveMemberId) {
    std::vector<FrameAppearance> appearances;
    if (!resolveMemberId) {
        return appearances;
    }

    for (const auto& entry : entries) {
        const auto resolvedMemberId = resolveMemberId(entry.data.castLib, entry.data.castMember);
        if (!resolvedMemberId || *resolvedMemberId != memberId) {
            continue;
        }
        const int frameIndex = entry.frameIndex.value();
        const auto label = frameLabels.find(frameIndex);
        appearances.push_back(FrameAppearance{
            .frame = frameIndex + 1,
            .channel = entry.channelIndex.value(),
            .channelName = scoreChannelName(entry.channelIndex.value()),
            .frameLabel = label != frameLabels.end() ? label->second : std::string{},
            .posX = entry.data.posX,
            .posY = entry.data.posY,
        });
    }

    std::sort(appearances.begin(), appearances.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.frame != rhs.frame) {
            return lhs.frame < rhs.frame;
        }
        return lhs.channel < rhs.channel;
    });
    return appearances;
}

std::string registrationPointText(int x, int y) {
    std::ostringstream out;
    out << x << ", " << y;
    return out.str();
}

std::string editorPanelForMemberType(cast::MemberType type) {
    switch (type) {
        case cast::MemberType::Bitmap:
        case cast::MemberType::Picture:
            return "paint";
        case cast::MemberType::Text:
        case cast::MemberType::RichText:
        case cast::MemberType::Button:
            return "text";
        case cast::MemberType::Script:
            return "script";
        case cast::MemberType::Sound:
            return "sound";
        case cast::MemberType::Shape:
            return "vector-shape";
        case cast::MemberType::Null:
        case cast::MemberType::FilmLoop:
        case cast::MemberType::Movie:
        case cast::MemberType::DigitalVideo:
        case cast::MemberType::Transition:
        case cast::MemberType::Xtra:
        case cast::MemberType::Font:
        case cast::MemberType::Shockwave3D:
        case cast::MemberType::Unknown:
            return {};
        case cast::MemberType::Palette:
            return "color-palettes";
    }
    return {};
}

std::string memberEditorNoCastMemberLoadedText() {
    return "No cast member loaded";
}

StartScreenText startScreenText() {
    return kStartScreenText;
}

std::string startScreenRecentProjectText(std::string_view name, std::string_view parent, bool exists) {
    std::ostringstream out;
    out << name << " - " << parent;
    if (!exists) {
        out << " (missing)";
    }
    return out.str();
}

RecentProjectsMenuText recentProjectsMenuText() {
    return kRecentProjectsMenuText;
}

ExchangeCastMembersText exchangeCastMembersText() {
    return kExchangeCastMembersText;
}

std::span<const std::string_view> exchangeCastMembersTableColumns() {
    return kExchangeCastMembersTableColumns;
}

std::span<const std::string_view> exchangeCastMembersSlotLabels() {
    return kExchangeCastMembersSlotLabels;
}

std::string exchangeCastMembersSizeText(int byteCount) {
    if (byteCount <= 0) {
        return std::string{kExchangeCastMembersText.unsetText};
    }
    std::ostringstream out;
    out << byteCount << " bytes";
    return out.str();
}

std::string exchangeCastMembersDetailsText(std::string_view firstDetails, std::string_view secondDetails) {
    std::ostringstream out;
    out << kExchangeCastMembersSlotLabels[0] << ":\n" << firstDetails << "\n\n"
        << kExchangeCastMembersSlotLabels[1] << ":\n" << secondDetails;
    return out.str();
}

std::string exchangeCastMembersAppliedStatusText(int firstMemberNumber, int secondMemberNumber) {
    std::ostringstream out;
    out << "Exchanged runtime media for cast members " << firstMemberNumber << " and " << secondMemberNumber;
    return out.str();
}

std::string exchangeCastMembersFailedStatusText(int firstMemberNumber, int secondMemberNumber) {
    std::ostringstream out;
    out << "Could not exchange runtime media for cast members " << firstMemberNumber << " and " << secondMemberNumber;
    return out.str();
}

MovieCastsDialogText movieCastsDialogText() {
    return kMovieCastsDialogText;
}

std::span<const std::string_view> movieCastsDialogTableColumns() {
    return kMovieCastsDialogTableColumns;
}

std::string movieCastsDialogMemberCountText(std::size_t memberCount) {
    return std::to_string(memberCount);
}

MoviePropertiesDialogText moviePropertiesDialogText() {
    return kMoviePropertiesDialogText;
}

CastMemberPropertiesDialogText castMemberPropertiesDialogText() {
    return kCastMemberPropertiesDialogText;
}

std::string castMemberPropertiesDialogTitle(std::string_view memberName, int memberNumber) {
    if (!memberName.empty()) {
        return std::string{memberName};
    }
    return std::string{kCastMemberPropertiesDialogText.memberTitlePrefix} + std::to_string(memberNumber);
}

SpritePropertiesDialogText spritePropertiesDialogText() {
    return kSpritePropertiesDialogText;
}

std::string spritePropertiesDialogHeading(int frame, std::string_view channelName) {
    std::ostringstream out;
    out << "Frame " << frame << ", " << channelName;
    return out.str();
}

SpriteTweeningDialogText spriteTweeningDialogText() {
    return kSpriteTweeningDialogText;
}

std::span<const std::string_view> spriteTweeningPropertyLabels() {
    return kSpriteTweeningPropertyLabels;
}

std::string spriteTweeningSpanText(int startFrame, int endFrame) {
    std::ostringstream out;
    out << startFrame << "-" << endFrame;
    return out.str();
}

std::string spriteTweeningDetailsText(std::string_view scoreDetails) {
    std::ostringstream out;
    out << scoreDetails << "\n\n" << kSpriteTweeningDialogText.pendingNoteText;
    return out.str();
}

std::string spriteTweeningOpenedStatusText(int frame, std::string_view channelName) {
    std::ostringstream out;
    out << "Opened sprite tweening for frame " << frame << ", " << channelName;
    return out.str();
}

FrameChannelDialogText frameChannelDialogText() {
    return kFrameChannelDialogText;
}

std::string frameChannelDialogTitle(std::string_view channelName) {
    return std::string{kFrameChannelDialogText.titlePrefix} + std::string{channelName};
}

std::string frameChannelCastMemberText(int castLib, int castMember) {
    std::ostringstream out;
    out << "Cast " << castLib << ", member " << castMember;
    return out.str();
}

std::string frameChannelNativeChannelsText(std::string_view firstChannel, std::string_view secondChannel) {
    std::ostringstream out;
    out << firstChannel << ", " << secondChannel;
    return out.str();
}

ExternalParamsDialogText externalParamsDialogText() {
    return kExternalParamsDialogText;
}

std::span<const std::string_view> externalParamsTableColumns() {
    return kExternalParamsTableColumns;
}

std::string castMemberDetails(const CastMemberRow& row) {
    std::ostringstream out;
    out << "#" << row.chunkId << "  " << row.type << "  " << (row.name.empty() ? "(unnamed)" : row.name);
    if (row.scriptId > 0) {
        out << "  script " << row.scriptId;
    }
    out << "  reg " << registrationPointText(row.regPointX, row.regPointY);
    const auto panel = editorPanelForMemberType(row.memberType);
    if (!panel.empty()) {
        out << "  opens " << panel;
    }
    return out.str();
}

std::string castMemberPreviewDetails(const CastMemberRow& row,
                                     const std::vector<FrameAppearance>& appearances,
                                     bool includePosition) {
    std::ostringstream out;
    out << castMemberDetails(row) << "\n\nScore appearances:\n";
    out << previewScoreAppearances(appearances, includePosition);
    return out.str();
}

std::string textMemberPreviewDetails(const CastMemberRow& row, const chunks::TextChunk* textChunk) {
    const auto typeName = row.memberType == cast::MemberType::Button ? std::string_view{"BUTTON"} : std::string_view{"TEXT"};
    std::ostringstream out;
    out << previewMemberHeader(typeName, row.name, row.chunkId, true);
    if (textChunk == nullptr) {
        out << previewTextDataNotFoundText() << "\n";
        return out.str();
    }

    out << "--- Text Content ---\n";
    out << normalizedLineEndings(textChunk->text()) << "\n\n";
    if (!textChunk->runs().empty()) {
        out << "--- Formatting Runs ---\n";
        for (const auto& run : textChunk->runs()) {
            out << "  Offset " << run.startOffset
                << ": Font #" << run.fontId
                << ", Size " << run.fontSize
                << ", Style 0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(2)
                << (run.fontStyle & 0xFF)
                << std::dec << std::setfill(' ') << "\n";
        }
    }
    return out.str();
}

std::string soundMemberPreviewDetails(const CastMemberRow& row,
                                      const chunks::SoundChunk* soundChunk,
                                      const std::vector<FrameAppearance>& appearances) {
    std::ostringstream out;
    out << previewMemberHeader("SOUND", row.name, row.chunkId, true);
    if (soundChunk != nullptr) {
        out << "--- Audio Properties ---\n";
        out << "Codec: " << (soundChunk->isMp3() ? "MP3" : "PCM (16-bit)") << "\n";
        out << "Sample Rate: " << soundChunk->sampleRate() << " Hz\n";
        out << "Bits Per Sample: " << soundChunk->bitsPerSample() << "\n";
        out << "Channels: " << (soundChunk->channelCount() == 1 ? "Mono" : "Stereo") << "\n";
        out << "Duration: " << std::fixed << std::setprecision(2) << soundChunk->durationSeconds() << " seconds\n";
        out << "Audio Data Size: " << soundChunk->audioData().size() << " bytes\n";
    } else {
        out << previewSoundDataNotFoundText() << "\n";
    }

    out << "\n--- Score Appearances ---\n";
    out << previewScoreAppearances(appearances, false);
    return out.str();
}

std::string paletteMemberPreviewDetails(const CastMemberRow& row, const std::vector<std::uint32_t>* colors) {
    std::ostringstream out;
    out << previewMemberHeader("PALETTE", row.name, row.chunkId, true);
    if (colors != nullptr) {
        out << previewPaletteInfo(*colors);
    } else {
        out << previewPaletteDataNotFoundText() << "\n";
    }
    return out.str();
}

std::string scriptMemberPreviewDetails(const CastMemberRow& row,
                                       const chunks::ScriptChunk* script,
                                       const chunks::ScriptNamesChunk* names) {
    std::ostringstream out;
    out << previewMemberHeader("SCRIPT", row.name, row.chunkId, false);
    if (script == nullptr) {
        out << "\n[No bytecode found for this script member]\n";
        return out.str();
    }

    out << "Script Type: " << scriptTypeDisplayName(script->resolvedScriptType()) << "\n";
    out << "Behavior Flags: 0x" << std::hex << std::nouppercase << script->behaviorFlags() << std::dec << "\n\n";

    if (!script->properties().empty()) {
        out << "--- PROPERTIES ---\n";
        for (const auto& property : script->getPropertyNames(names)) {
            out << "  property " << property << "\n";
        }
        out << "\n";
    }

    if (!script->globals().empty()) {
        out << "--- GLOBALS ---\n";
        for (const auto& global : script->getGlobalNames(names)) {
            out << "  global " << global << "\n";
        }
        out << "\n";
    }

    out << "--- HANDLERS (" << script->handlers().size() << ") ---\n\n";
    for (const auto& handler : script->handlers()) {
        out << formatScriptHandlerPreview(*script, handler, names) << "\n";
    }

    if (!script->literals().empty()) {
        out << "--- LITERALS (" << script->literals().size() << ") ---\n";
        for (std::size_t index = 0; index < script->literals().size(); ++index) {
            const auto& literal = script->literals()[index];
            out << "  [" << index << "] " << format::formatLiteralValue(literal.value) << "\n";
        }
    }
    return out.str();
}

std::string genericMemberPreviewDetails(const CastMemberRow& row,
                                        int specificDataSize,
                                        const std::vector<FrameAppearance>& appearances) {
    std::ostringstream out;
    out << "=== " << toUpper(row.type) << ": " << row.name << " ===\n\n";
    out << "Member ID: " << row.chunkId << "\n";
    out << "Type: " << row.type << " (" << cast::code(row.memberType) << ")\n";
    if (specificDataSize > 0) {
        out << "\nSpecific Data: " << specificDataSize << " bytes\n";
    }
    out << "\n--- Score Appearances ---\n";
    out << previewScoreAppearances(appearances, true);
    return out.str();
}

std::string buildCastMemberEditorPreview(DirectorFile& movie, const CastMemberRow& row) {
    const auto member = movie.getCastMemberByNumber(0, row.chunkId);
    const auto appearances = buildFrameAppearances(movie, row.chunkId);
    switch (row.memberType) {
        case cast::MemberType::Text:
        case cast::MemberType::RichText:
        case cast::MemberType::Button: {
            const auto text = member ? movie.getTextForMember(member) : nullptr;
            return textMemberPreviewDetails(row, text.get());
        }
        case cast::MemberType::Sound: {
            const auto sound = member ? player::audio::SoundManager::findSoundForMember(movie, member) : nullptr;
            return soundMemberPreviewDetails(row, sound.get(), appearances);
        }
        case cast::MemberType::Palette: {
            const auto palette = movie.resolvePaletteByMemberNumber(row.chunkId);
            const auto* colors = palette ? &palette->colors() : nullptr;
            return paletteMemberPreviewDetails(row, colors);
        }
        case cast::MemberType::Script: {
            const auto script = member ? movie.getScriptForCastMember(member) : nullptr;
            const auto names = script ? movie.getScriptNamesForScript(script) : nullptr;
            return scriptMemberPreviewDetails(row, script.get(), names.get());
        }
        default:
            return genericMemberPreviewDetails(row,
                                               member ? static_cast<int>(member->specificData().size()) : 0,
                                               appearances);
    }
}

std::vector<ExternalParamRow> habboExternalParamPreset() {
    return {
        {"sw1", "site.url=http://www.habbo.co.uk;url.prefix=http://www.habbo.co.uk"},
        {"sw2", "connection.info.host=au.h4bbo.net;connection.info.port=30101"},
        {"sw3", "client.reload.url=http://h4bbo.net/"},
        {"sw4", "connection.mus.host=au.h4bbo.net;connection.mus.port=38101"},
        {"sw5",
         "external.variables.txt=http://sandbox.h4bbo.net/gamedata/external_variables.txt;"
         "external.texts.txt=http://sandbox.h4bbo.net/gamedata/external_texts.txt"},
    };
}

std::vector<std::pair<std::string, std::string>> externalParamsForRuntime(std::span<const ExternalParamRow> params) {
    std::vector<std::pair<std::string, std::string>> runtimeParams;
    runtimeParams.reserve(params.size());
    for (const auto& param : params) {
        if (!param.key.empty()) {
            runtimeParams.emplace_back(param.key, param.value);
        }
    }
    return runtimeParams;
}

std::vector<std::string> recentProjectsWithAdded(std::vector<std::string> recent,
                                                 std::string projectPath,
                                                 std::size_t maxProjects) {
    recent.erase(std::remove(recent.begin(), recent.end(), projectPath), recent.end());
    if (!projectPath.empty()) {
        recent.insert(recent.begin(), std::move(projectPath));
    }
    if (recent.size() > maxProjects) {
        recent.resize(maxProjects);
    }
    return recent;
}

std::span<const EditorPanelSpec> editorPanelSpecs() {
    return kEditorPanelSpecs;
}

const EditorPanelSpec* findEditorPanelSpec(std::string_view panelId) {
    const auto specs = editorPanelSpecs();
    const auto found = std::find_if(specs.begin(), specs.end(), [panelId](const auto& spec) {
        return spec.id == panelId;
    });
    return found == specs.end() ? nullptr : &*found;
}

bool castMemberMatchesFilter(const CastMemberRow& row, std::string_view filter) {
    if (filter.empty()) {
        return true;
    }
    return containsIgnoringCase(row.name, filter) || containsIgnoringCase(row.type, filter);
}

bool castMemberMatchesTypeFilter(const CastMemberRow& row, std::string_view typeFilter) {
    if (typeFilter.empty() || typeFilter == kCastWindowTypeFilterItems.front()) {
        return true;
    }
    return containsIgnoringCase(row.type, typeFilter);
}

MovieSummary buildMovieSummary(DirectorFile& movie) {
    return MovieSummary{
        .version = movie.version(),
        .tempo = movie.tempo(),
        .channelCount = movie.channelCount(),
        .stageWidth = movie.stageWidth(),
        .stageHeight = movie.stageHeight(),
        .frameCount = frameCount(movie),
        .castMemberCount = static_cast<int>(movie.castMembers().size()),
        .castCount = static_cast<int>(movie.casts().size()),
        .paletteCount = static_cast<int>(movie.palettes().size()),
        .scriptCount = static_cast<int>(movie.scripts().size()),
        .globalCount = static_cast<int>(movie.getAllGlobalNames().size()),
        .propertyCount = static_cast<int>(movie.getAllPropertyNames().size()),
        .externalCastCount = static_cast<int>(movie.getExternalCastPaths().size()),
    };
}

std::vector<CastMemberRow> buildCastMemberRows(const DirectorFile& movie, std::string_view filter) {
    std::vector<CastMemberRow> rows;
    for (const auto& member : movie.castMembers()) {
        if (!member) {
            continue;
        }
        CastMemberRow row{
            .chunkId = member->id().value(),
            .type = memberTypeDisplayName(member->memberType()),
            .name = member->name(),
            .memberType = member->memberType(),
            .scriptId = member->scriptId(),
            .regPointX = member->regPointX(),
            .regPointY = member->regPointY(),
            .specificDataSize = static_cast<int>(member->specificData().size()),
        };
        if (castMemberMatchesFilter(row, filter)) {
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

std::vector<ScoreIntervalRow> buildScoreIntervalRows(DirectorFile& movie) {
    std::vector<ScoreIntervalRow> rows;
    if (!movie.scoreChunk()) {
        return rows;
    }
    const auto score = movie.scoreChunk();
    for (const auto& interval : movie.scoreChunk()->frameIntervals()) {
        auto memberType = cast::MemberType::Unknown;
        std::string memberTypeName;
        std::string memberName;
        int scriptId = 0;
        const int castLib = interval.secondary ? interval.secondary->castLib : -1;
        const int castMember = interval.secondary ? interval.secondary->castMember : -1;
        const chunks::ScoreChunk::ChannelData* spriteData = nullptr;
        if (interval.secondary) {
            if (const auto member = movie.getCastMemberByIndex(castLib, castMember)) {
                memberType = member->memberType();
                memberTypeName = memberTypeDisplayName(memberType);
                memberName = member->name();
                scriptId = member->scriptId();
            }
        }
        for (const auto& entry : score->frameData().frameChannelData) {
            if (entry.channelIndex.value() != interval.primary.channelIndex) {
                continue;
            }
            const int entryFrame = entry.frameIndex.value();
            if (entryFrame + 1 == interval.primary.startFrame &&
                (!interval.secondary || (entry.data.castLib == castLib && entry.data.castMember == castMember))) {
                spriteData = &entry.data;
                break;
            }
            if (!spriteData && entryFrame == interval.primary.startFrame &&
                (!interval.secondary || (entry.data.castLib == castLib && entry.data.castMember == castMember))) {
                spriteData = &entry.data;
            }
        }
        rows.push_back(ScoreIntervalRow{
            .startFrame = interval.primary.startFrame,
            .endFrame = interval.primary.endFrame,
            .channel = interval.primary.channelIndex,
            .castLib = castLib,
            .castMember = castMember,
            .hasSpriteData = spriteData != nullptr,
            .posX = spriteData ? spriteData->posX : 0,
            .posY = spriteData ? spriteData->posY : 0,
            .width = spriteData ? spriteData->width : 0,
            .height = spriteData ? spriteData->height : 0,
            .ink = spriteData ? spriteData->ink : 0,
            .blend = spriteData ? spriteData->blendByte : 0,
            .memberType = memberType,
            .memberTypeName = memberTypeName,
            .memberName = memberName,
            .scriptId = scriptId,
        });
    }
    return rows;
}

std::vector<FrameAppearance> buildFrameAppearances(DirectorFile& movie, int memberId) {
    const auto score = movie.scoreChunk();
    if (!score) {
        return {};
    }

    std::map<int, std::string> frameLabels;
    if (const auto labels = movie.frameLabelsChunk()) {
        for (const auto& label : labels->labels()) {
            frameLabels[label.frameNum.value()] = label.label;
        }
    }

    return buildFrameAppearancesFromScoreEntries(score->frameData().frameChannelData,
                                                 frameLabels,
                                                 memberId,
                                                 [&movie](int castLib, int castMember) -> std::optional<int> {
                                                     const auto member = movie.getCastMemberByIndex(castLib, castMember);
                                                     if (!member) {
                                                         return std::nullopt;
                                                     }
                                                     return member->id().value();
                                                 });
}

std::vector<ScriptRow> buildScriptRows(DirectorFile& movie) {
    std::vector<ScriptRow> rows;
    for (const auto& script : movie.scripts()) {
        if (!script) {
            continue;
        }
        const auto names = movie.getScriptNamesForScript(script);
        std::vector<std::string> handlerPreviews;
        handlerPreviews.reserve(script->handlers().size());
        std::vector<std::string> lingoPreviews;
        lingoPreviews.reserve(script->handlers().size());
        for (const auto& handler : script->handlers()) {
            handlerPreviews.push_back(formatScriptHandlerPreview(*script, handler, names.get()));
            lingoPreviews.push_back(formatScriptHandlerLingoPreview(*script, handler, names.get()));
        }
        std::vector<std::string> literals;
        literals.reserve(script->literals().size());
        for (std::size_t index = 0; index < script->literals().size(); ++index) {
            const auto& literal = script->literals()[index];
            std::ostringstream line;
            line << "[" << index << "] " << format::getLiteralTypeName(literal.type)
                 << "  " << format::formatLiteralValue(literal.value);
            literals.push_back(line.str());
        }
        auto properties = names ? script->getPropertyNames(names.get()) : std::vector<std::string>{};
        for (std::size_t index = 0; index < properties.size(); ++index) {
            properties[index] = "[" + std::to_string(index) + "] " + properties[index];
        }
        auto globals = names ? script->getGlobalNames(names.get()) : std::vector<std::string>{};
        for (std::size_t index = 0; index < globals.size(); ++index) {
            globals[index] = "[" + std::to_string(index) + "] " + globals[index];
        }
        rows.push_back(ScriptRow{
            .scriptId = script->id().value(),
            .name = movie.getScriptName(script),
            .type = scriptTypeDisplayName(script->resolvedScriptType()),
            .handlerCount = static_cast<int>(script->handlers().size()),
            .globalCount = static_cast<int>(globals.size()),
            .propertyCount = static_cast<int>(properties.size()),
            .handlers = std::move(handlerPreviews),
            .lingoHandlers = std::move(lingoPreviews),
            .literals = std::move(literals),
            .properties = std::move(properties),
            .globals = std::move(globals),
        });
    }
    return rows;
}

} // namespace libreshockwave::editor
