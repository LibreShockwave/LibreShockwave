#pragma once

#include <string>
#include <string_view>
#include <span>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/cast/ShapeInfo.hpp"
#include "libreshockwave/chunks/ScoreChunk.hpp"
#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/player/debug/DebugSnapshot.hpp"
#include "libreshockwave/player/timeout/TimeoutManager.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::chunks {
class SoundChunk;
class TextChunk;
class ScriptNamesChunk;
}

namespace libreshockwave::editor {

enum class DockArea {
    Left,
    Right,
    Top,
    Bottom,
    Center
};

enum class SelectionType {
    None,
    Sprite,
    CastMember,
    Frame,
    ScoreCell
};

enum class ToolNudgeDirection {
    Left,
    Right,
    Up,
    Down
};

struct EditorPanelSpec {
    std::string_view id;
    std::string_view title;
    DockArea defaultDockArea = DockArea::Center;
    bool visibleByDefault = false;
    std::string_view shortcut;
};

struct FileCommandSpec {
    std::string_view id;
    std::string_view menuText;
    std::string_view shortcut;
};

struct MainMenuText {
    std::string_view fileMenuText;
    std::string_view preferencesMenuText;
    std::string_view editMenuText;
    std::string_view viewMenuText;
    std::string_view zoomMenuText;
    std::string_view spriteOverlayMenuText;
    std::string_view gridsMenuText;
    std::string_view guidesMenuText;
    std::string_view insertMenuText;
    std::string_view mediaElementMenuText;
    std::string_view modifyMenuText;
    std::string_view controlMenuText;
    std::string_view debugMenuText;
    std::string_view windowMenuText;
    std::string_view helpMenuText;
};

struct FileCreationText {
    std::string_view newMovieTitle;
    std::string_view stageSizeLabel;
    std::string_view framesLabel;
    std::string_view spriteChannelsLabel;
    std::string_view newMoviePendingText;
    std::string_view newCastTitle;
    std::string_view castNameLabel;
    std::string_view castTypeLabel;
    std::string_view internalCastType;
    std::string_view externalCastType;
    std::string_view newCastPendingText;
};

struct FileSaveText {
    std::string_view saveTitle;
    std::string_view saveAsTitle;
    std::string_view saveAllTitle;
    std::string_view noMovieText;
    std::string_view newMoviePathText;
    std::string_view savePendingText;
    std::string_view saveAsDialogTitle;
    std::string_view directorFileFilter;
    std::string_view saveAllNoChangesText;
    std::string_view saveAllPreparedText;
    std::string_view saveAllPendingText;
    std::string_view saveAllStatusText;
};

struct FileOpenImportText {
    std::string_view openDirectorFileTitle;
    std::string_view openFailedTitle;
    std::string_view directorFileFilter;
    std::string_view importTitle;
    std::string_view importNoMovieText;
    std::string_view importMediaTitle;
    std::string_view importMediaFilter;
    std::string_view importPendingText;
};

struct PreferencePanelSpec {
    std::string_view id;
    std::string_view menuText;
};

struct GeneralPreferencesText {
    std::string_view title;
    std::string_view browseText;
    std::string_view defaultOpenFolderLabel;
    std::string_view recentProjectsLabel;
    std::string_view emptyRecentProjectsText;
    std::string_view clearRecentProjectsText;
    std::string_view chooseDefaultOpenFolderTitle;
    std::string_view savedStatusText;
};

struct PreferenceBoolOptionSpec {
    std::string_view key;
    std::string_view label;
    bool defaultValue = false;
};

struct PreferenceIntOptionSpec {
    std::string_view key;
    std::string_view label;
    int defaultValue = 0;
    int minimum = 0;
    int maximum = 100;
};

struct PreferenceChoiceOptionSpec {
    std::string_view key;
    std::string_view label;
    std::string_view defaultValue;
    std::span<const std::string_view> values;
};

struct PreferenceCategoryPanelText {
    std::string_view id;
    std::string_view title;
    std::span<const PreferenceBoolOptionSpec> boolOptions;
    std::span<const PreferenceIntOptionSpec> intOptions;
    std::span<const PreferenceChoiceOptionSpec> choiceOptions;
};

struct EditCommandSpec {
    std::string_view id;
    std::string_view menuText;
    std::string_view shortcut;
};

struct EditCommandStatusText {
    std::string_view unsupportedText;
    std::string_view unsupportedClearText;
    std::string_view clearedText;
};

struct EditSpriteCommandText {
    std::string_view editFramesTitle;
    std::string_view editFramesNoSelectionText;
    std::string_view editEntireTitle;
    std::string_view editEntireNoMovieText;
    std::string_view editEntireNoCastMemberText;
    std::string_view editEntireMemberNotLoadedText;
    std::string_view editEntireNoPanelText;
};

struct FindReplaceText {
    std::string_view findTitle;
    std::string_view findPrompt;
    std::string_view replaceTitle;
    std::string_view replaceWithPrompt;
    std::string_view replaceAllPromptPrefix;
    std::string_view replaceAllPromptSuffix;
    std::string_view noSelectedTextToFind;
    std::string_view noSelectedTextToReplace;
    std::string_view noFocusedFindText;
    std::string_view noFocusedReplaceText;
};

struct ModifyCommandSpec {
    std::string_view id;
    std::string_view menuText;
    std::string_view group;
    std::string_view shortcut;
};

struct DebugCommandSpec {
    std::string_view id;
    std::string_view menuText;
    std::string_view toolbarText;
    std::string_view shortcut;
};

struct DebugContextCommandSpec {
    std::string_view id;
    std::string_view menuText;
    bool requiresNavigationTarget = false;
};

struct DebugWatchActionSpec {
    std::string_view id;
    std::string_view buttonText;
    std::string_view tooltipText;
};

struct DebugBrowserText {
    std::string_view scriptLabel;
    std::string_view handlerLabel;
    std::string_view filterPlaceholder;
    std::string_view scriptFilterTooltip;
    std::string_view handlerFilterTooltip;
    std::string_view detailsButtonText;
    std::string_view detailsButtonTooltip;
    std::string_view bytecodeTitle;
    std::string_view lingoToggleText;
    std::string_view bytecodeToggleText;
    std::string_view lingoToggleTooltip;
};

struct DebugWatchDialogText {
    std::string_view addTitle;
    std::string_view editTitle;
    std::string_view expressionPrompt;
};

struct TraceHandlerDialogText {
    std::string_view title;
    std::string_view noMovieText;
    std::string_view noneText;
};

struct DebugDetailsText {
    std::string_view overviewTabText;
    std::string_view bytecodeTabText;
    std::string_view literalsTabText;
    std::string_view propertiesTabText;
    std::string_view globalsTabText;
    std::string_view closeButtonText;
};

struct ScriptEditorText {
    std::string_view castLabel;
    std::string_view scriptLabel;
    std::string_view handlerLabel;
    std::string_view lingoToggleText;
    std::string_view bytecodeToggleText;
    std::string_view lingoToggleTooltip;
    std::string_view emptySummaryText;
};

struct PropertyInspectorText {
    std::string_view noSelectionText;
    std::string_view memberPlaceholderText;
    std::string_view moviePlaceholderText;
    std::string_view addBehaviorText;
    std::string_view removeBehaviorText;
    std::string_view openScriptText;
    std::string_view openBehaviorScriptText;
    std::string_view behaviorScriptTitle;
    std::string_view addBehaviorTitle;
    std::string_view addBehaviorNoSpriteText;
    std::string_view addBehaviorPromptPrefix;
    std::string_view addBehaviorPromptMiddle;
    std::string_view behaviorPendingSuffix;
    std::string_view removeBehaviorTitle;
    std::string_view removeBehaviorNoSelectionText;
};

struct MemberEditorActionText {
    std::string_view applyText;
    std::string_view applyTooltip;
};

struct TextEditorText {
    std::string_view boldTooltip;
    std::string_view italicTooltip;
    std::string_view underlineTooltip;
    std::string_view localFormattingStatus;
    std::string_view localTextStatus;
    std::string_view noMemberLoadedStatus;
    std::string_view appliedRuntimeStatus;
    std::string_view applyFailedStatus;
};

struct TextFormattingDialogText {
    std::string_view fontTitle;
    std::string_view fontNoEditorText;
    std::string_view fontTextStatus;
    std::string_view fontFieldStatus;
    std::string_view paragraphTitle;
    std::string_view paragraphNoEditorText;
    std::string_view alignmentLabel;
    std::string_view leftIndentLabel;
    std::string_view rightIndentLabel;
    std::string_view firstLineLabel;
    std::string_view lineSpacingLabel;
    std::string_view paragraphStatus;
};

struct FieldEditorText {
    std::string_view toolbarTitle;
    std::string_view wrapTooltip;
    std::string_view scrollTooltip;
    std::string_view wrapEnabledStatus;
    std::string_view wrapDisabledStatus;
    std::string_view scrollEnabledStatus;
    std::string_view scrollDisabledStatus;
    std::string_view localTextStatus;
    std::string_view noMemberLoadedStatus;
    std::string_view appliedRuntimeStatus;
    std::string_view applyFailedStatus;
};

struct SoundWindowText {
    std::string_view playButtonText;
    std::string_view stopButtonText;
    std::string_view noDataTooltip;
    std::string_view playingStatus;
    std::string_view stoppedStatus;
    std::string_view playbackErrorStatus;
};

struct ColorPaletteWindowText {
    std::string_view selectorLabel;
    std::string_view unavailableText;
};

struct PaintWindowActionSpec {
    std::string_view id;
    std::string_view label;
    std::string_view tooltip;
};

struct PlaybackCommandSpec {
    std::string_view id;
    std::string_view menuText;
    std::string_view toolbarText;
    std::string_view shortcut;
};

struct PlaybackToolbarText {
    std::string_view title;
    std::string_view initialFrameText;
};

struct ToggleCommandSpec {
    std::string_view id;
    std::string_view menuText;
    bool checkedByDefault = false;
};

struct WindowCommandSpec {
    std::string_view id;
    std::string_view menuText;
};

struct MainWindowShellText {
    std::string_view defaultTitle;
    std::string_view titlePrefix;
    std::string_view centerPlaceholderText;
    std::string_view pendingPanelText;
    std::string_view layoutResetStatusText;
};

struct MainWindowSummaryText {
    std::string_view noMovieOpenText;
    std::string_view moviePlaceholderText;
    std::string_view scorePlaceholderText;
};

struct PanelContextCommandText {
    std::string_view raisePanel;
    std::string_view showPanel;
    std::string_view floatPanel;
    std::string_view dockPanel;
    std::string_view closePanel;
    std::string_view resetLayout;
};

struct DockCommandSpec {
    std::string_view id;
    std::string_view menuText;
    DockArea area = DockArea::Center;
};

struct MoveDockCommandSpec {
    std::string_view id;
    std::string_view menuText;
    DockArea area = DockArea::Center;
};

struct SplitCommandSpec {
    std::string_view id;
    std::string_view menuText;
    DockArea direction = DockArea::Right;
};

struct HelpCommandSpec {
    std::string_view id;
    std::string_view menuText;
    std::string_view dialogTitle;
    std::string_view dialogText;
};

struct InsertCommandSpec {
    std::string_view id;
    std::string_view menuText;
    std::string_view shortcut;
};

struct InsertActionText {
    std::string_view keyframeTitle;
    std::string_view keyframeNoMovieText;
    std::string_view keyframeNoSelectionText;
    std::string_view keyframePendingSuffix;
    std::string_view markerTitle;
    std::string_view markerNoMovieText;
    std::string_view markerPromptLabel;
    std::string_view markerDefaultName;
    std::string_view markerPendingSuffix;
    std::string_view removeFrameTitle;
    std::string_view removeFrameNoMovieText;
    std::string_view removeFramePendingSuffix;
    std::string_view mediaElementTitle;
    std::string_view mediaElementNoMovieText;
    std::string_view mediaElementPendingSuffix;
};

struct MediaElementSpec {
    std::string_view id;
    std::string_view menuText;
    std::string_view targetPanelId;
};

struct ViewCommandSpec {
    std::string_view id;
    std::string_view menuText;
    bool checkable = true;
};

struct ViewGridSettingsText {
    std::string_view title;
    std::string_view gridSizeLabel;
    std::string_view guideSnapThresholdLabel;
    std::string_view pixelSuffix;
    std::string_view savedStatusText;
};

struct DebugTableRow {
    std::string id;
    std::vector<std::string> cells;
    std::string detailTitle;
    std::string detailType;
    std::string detailText;
};

struct SelectionState {
    SelectionType type = SelectionType::None;
    int channel = 0;
    int frame = 0;
    int castLib = 0;
    int memberNum = 0;
};

struct MovieSummary {
    int version = 0;
    int tempo = 0;
    int channelCount = 0;
    int stageWidth = 0;
    int stageHeight = 0;
    int frameCount = 1;
    int castMemberCount = 0;
    int castCount = 0;
    int paletteCount = 0;
    int scriptCount = 0;
    int globalCount = 0;
    int propertyCount = 0;
    int externalCastCount = 0;
};

struct CastMemberRow {
    int chunkId = 0;
    std::string type;
    std::string name;
    cast::MemberType memberType = cast::MemberType::Unknown;
    int scriptId = 0;
    int regPointX = 0;
    int regPointY = 0;
    int specificDataSize = 0;
};

struct ScoreIntervalRow {
    int startFrame = 0;
    int endFrame = 0;
    int channel = 0;
    int castLib = -1;
    int castMember = -1;
    bool hasSpriteData = false;
    int posX = 0;
    int posY = 0;
    int width = 0;
    int height = 0;
    int ink = 0;
    int blend = 0;
    cast::MemberType memberType = cast::MemberType::Unknown;
    std::string memberTypeName;
    std::string memberName;
    int scriptId = 0;
};

struct FrameAppearance {
    int frame = 0;
    int channel = 0;
    std::string channelName;
    std::string frameLabel;
    int posX = 0;
    int posY = 0;
};

struct ScoreMarker {
    int frame = 0;
    std::string label;
};

struct PaletteSwatchCell {
    int x = 0;
    int y = 0;
    int size = 0;
    std::uint32_t rgb = 0;
};

struct PaletteSwatch {
    int width = 1;
    int height = 1;
    std::vector<PaletteSwatchCell> cells;
};

struct CastWindowViewModeSpec {
    bool showDetailColumns = true;
    int rowHeight = 1;
    int previewColumnWidth = 1;
    std::string statusText;
};

struct CastWindowText {
    std::string_view loadCastText;
    std::string_view loadCastTooltip;
    std::string_view gridViewText;
    std::string_view gridViewTooltip;
    std::string_view listViewText;
    std::string_view listViewTooltip;
    std::string_view searchLabel;
    std::string_view searchPlaceholder;
    std::string_view typeLabel;
    std::string_view emptySummaryText;
    std::string_view selectedMemberPlaceholderText;
    std::string_view openText;
    std::string_view openInFieldText;
    std::string_view loadExternalCastTitle;
    std::string_view loadExternalCastNoMovieText;
    std::string_view loadExternalCastSelectSlotText;
    std::string_view loadExternalCastNotExternalText;
    std::string_view loadExternalCastFileFilter;
    std::string_view loadExternalCastOpenFailedText;
    std::string_view loadExternalCastLoadFailedText;
    std::string_view exportMembersTitle;
    std::string_view exportMembersNoSelectionText;
    std::string_view exportSelectedDirectoryTitle;
    std::string_view exportMemberTitle;
    std::string_view supportedExportFileFilter;
    std::string_view exportAllSelectedDirectoryTitle;
};

struct StartScreenText {
    std::string_view windowTitle;
    std::string_view appTitle;
    std::string_view subtitle;
    std::string_view createNewMovieText;
    std::string_view createNewMovieDialogTitle;
    std::string_view createNewMoviePendingText;
    std::string_view openMovieText;
    std::string_view emptyRecentProjectsText;
    std::string_view recentProjectsHeader;
    std::string_view openDirectorFileTitle;
    std::string_view directorFileFilter;
    std::string_view fileNotFoundTitle;
    std::string_view fileNotFoundMessagePrefix;
};

struct RecentProjectsMenuText {
    std::string_view emptyText;
    std::string_view clearText;
    std::string_view missingWarningTitle;
    std::string_view missingWarningMessagePrefix;
    std::string_view clearedStatusText;
};

struct ExchangeCastMembersText {
    std::string_view title;
    std::string_view selectTwoLoadedText;
    std::string_view oneMemberNotLoadedText;
    std::string_view pendingNoteText;
    std::string_view exchangeButtonText;
    std::string_view unnamedMemberText;
    std::string_view unsetText;
};

struct MovieCastsDialogText {
    std::string_view title;
    std::string_view noMovieText;
    std::string_view internalKindText;
    std::string_view externalKindText;
    std::string_view loadedStatusText;
    std::string_view notLoadedStatusText;
    std::string_view unsetText;
};

struct MoviePropertiesDialogText {
    std::string_view title;
    std::string_view noMovieText;
    std::string_view untitledMovieText;
};

struct CastMemberPropertiesDialogText {
    std::string_view title;
    std::string_view selectMemberText;
    std::string_view memberNotLoadedText;
    std::string_view memberTitlePrefix;
    std::string_view registrationPointLabel;
    std::string_view scriptLabel;
    std::string_view castLibraryLabel;
    std::string_view unsetText;
};

struct SpritePropertiesDialogText {
    std::string_view title;
    std::string_view noMovieText;
    std::string_view selectSpriteText;
};

struct SpriteTweeningDialogText {
    std::string_view title;
    std::string_view noMovieText;
    std::string_view selectSpriteText;
    std::string_view spanLabel;
    std::string_view castMemberLabel;
    std::string_view channelLabel;
    std::string_view tweenedPropertiesTitle;
    std::string_view settingsTitle;
    std::string_view curvatureLabel;
    std::string_view easeInLabel;
    std::string_view easeOutLabel;
    std::string_view unsetText;
    std::string_view pendingNoteText;
};

struct FrameChannelDialogText {
    std::string_view titlePrefix;
    std::string_view noScoreText;
    std::string_view frameLabel;
    std::string_view tempoLabel;
    std::string_view parsedTempoEntriesLabel;
    std::string_view paletteCastLabel;
    std::string_view paletteMemberLabel;
    std::string_view descriptionLabel;
    std::string_view parsedPaletteEntriesLabel;
    std::string_view transitionLabel;
    std::string_view nativeChannelLabel;
    std::string_view parsingStatusLabel;
    std::string_view readOnlySnapshotText;
    std::string_view sound1Label;
    std::string_view sound2Label;
    std::string_view nativeChannelsLabel;
    std::string_view unsetText;
};

struct ExternalParamsDialogText {
    std::string_view windowTitle;
    std::string_view helpText;
    std::string_view addText;
    std::string_view removeText;
    std::string_view habboPresetText;
    std::string_view habboPresetTooltip;
};

struct ScriptRow {
    int scriptId = 0;
    std::string name;
    std::string type;
    int handlerCount = 0;
    int globalCount = 0;
    int propertyCount = 0;
    std::vector<std::string> handlers;
    std::vector<std::string> lingoHandlers;
    std::vector<std::string> literals;
    std::vector<std::string> properties;
    std::vector<std::string> globals;
};

struct ScriptHandlerPreview {
    std::string name;
    std::string text;
};

struct ExternalParamRow {
    std::string key;
    std::string value;
};

struct StagePoint {
    int x = 0;
    int y = 0;
    friend bool operator==(const StagePoint&, const StagePoint&) = default;
};

using CastMemberIdResolver = std::function<std::optional<int>(int castLib, int castMember)>;

[[nodiscard]] std::string memberTypeDisplayName(cast::MemberType type);
[[nodiscard]] std::string scriptTypeDisplayName(chunks::ScriptChunkType type);
[[nodiscard]] std::string selectionTypeDisplayName(SelectionType type);
[[nodiscard]] std::string selectionDetails(const SelectionState& selection);
[[nodiscard]] std::string messageWindowWelcomeText();
[[nodiscard]] std::string messageWindowCommandTranscript(std::string_view command);
[[nodiscard]] std::string messageWindowResultTranscript(std::string_view command, const lingo::Datum& result);
[[nodiscard]] std::string messageWindowErrorTranscript(std::string_view command, std::string_view error);
[[nodiscard]] bool messageWindowIsClearCommand(std::string_view command);
[[nodiscard]] std::optional<std::string> messageWindowExpressionForCommand(std::string_view command);
[[nodiscard]] std::span<const std::string_view> toolPaletteTools();
[[nodiscard]] std::string toolPaletteActiveToolText(std::string_view toolName);
[[nodiscard]] std::string toolPaletteStageToolText(std::string_view toolName, int x, int y);
[[nodiscard]] std::string_view toolPaletteNudgeDirectionText(ToolNudgeDirection direction);
[[nodiscard]] std::string toolPaletteNudgeToolText(std::string_view toolName, std::string_view direction, int pixels);
[[nodiscard]] std::string toolPaletteSelectedStatusText(std::string_view toolName);
[[nodiscard]] std::string toolPaletteToolTipText(std::string_view toolName);
[[nodiscard]] std::string toolPaletteStageInteractionText(std::string_view toolName, int x, int y);
[[nodiscard]] std::string toolPaletteStageSpriteMoveText(std::string_view toolName, int channel, int x, int y);
[[nodiscard]] std::string toolPaletteStageNoSpriteText(std::string_view toolName);
[[nodiscard]] std::string toolPaletteStageSpriteMoveFailedText(std::string_view toolName, int channel);
[[nodiscard]] std::string toolPaletteNudgeSpriteText(std::string_view toolName,
                                                     std::string_view direction,
                                                     int pixels,
                                                     int channel,
                                                     int x,
                                                     int y);
[[nodiscard]] std::string toolPaletteNudgeNoSpriteText(std::string_view toolName, std::string_view direction, int pixels);
[[nodiscard]] std::string toolPaletteNudgeSpriteFailedText(std::string_view toolName,
                                                          std::string_view direction,
                                                          int pixels,
                                                          int channel);
[[nodiscard]] std::string toolPaletteNudgeInteractionText(std::string_view toolName,
                                                          std::string_view direction,
                                                          int pixels);
[[nodiscard]] std::span<const std::string_view> paintWindowTools();
[[nodiscard]] std::string paintWindowToolTipText(std::string_view toolName);
[[nodiscard]] std::span<const PaintWindowActionSpec> paintWindowViewActionSpecs();
[[nodiscard]] std::string_view paintWindowColorActionLabel();
[[nodiscard]] std::string_view paintWindowColorActionTooltip();
[[nodiscard]] std::string paintWindowToolStatus(std::string_view toolName, bool hasBitmap, int brushSize);
[[nodiscard]] std::string paintWindowInitialText();
[[nodiscard]] std::string paintWindowReadyStatus();
[[nodiscard]] std::string paintWindowErrorStatus();
[[nodiscard]] std::string paintWindowDecodeFailedText();
[[nodiscard]] std::string routedMemberSelectedStatusText();
[[nodiscard]] std::string paintWindowLoadedStatus(std::string_view memberName,
                                                  int memberNumber,
                                                  int width,
                                                  int height,
                                                  int bitDepth);
[[nodiscard]] std::string paintWindowZoomStatus(std::string_view baseStatus, double zoom);
[[nodiscard]] std::string paintWindowColorStatus(int red, int green, int blue);
[[nodiscard]] std::string paintWindowRuntimeEditStatus(std::string_view toolName, int x, int y);
[[nodiscard]] std::string paintWindowRuntimeEditFailedStatus(std::string_view toolName);
[[nodiscard]] std::span<const std::string_view> fieldEditorToolbarActions();
[[nodiscard]] FieldEditorText fieldEditorText();
[[nodiscard]] MemberEditorActionText fieldEditorActionText();
[[nodiscard]] std::string fieldEditorInitialText();
[[nodiscard]] std::string fieldEditorReadyStatus();
[[nodiscard]] std::string fieldEditorNoDataText();
[[nodiscard]] std::string fieldEditorNoDataStatusText();
[[nodiscard]] std::string fieldEditorLoadedStatus(std::string_view memberName, int memberNumber, std::size_t characterCount);
[[nodiscard]] std::span<const std::string_view> vectorShapeToolbarActions();
[[nodiscard]] std::string vectorShapeToolTipText(std::string_view toolName);
[[nodiscard]] std::string vectorShapeToolStatus(std::string_view toolName, bool hasShape);
[[nodiscard]] std::string vectorShapeRuntimeEditStatus(std::string_view toolName, cast::ShapeType type);
[[nodiscard]] std::string vectorShapeRuntimeEditFailedStatus(std::string_view toolName);
[[nodiscard]] std::string vectorShapePlaceholderText();
[[nodiscard]] std::string vectorShapeTypeName(cast::ShapeType type);
[[nodiscard]] std::string vectorShapeLoadedStatus(std::string_view memberName, int memberNumber, const cast::ShapeInfo* shape);
[[nodiscard]] std::string vectorShapeDetailsText(const cast::ShapeInfo& shape);
[[nodiscard]] std::string soundWindowInitialText();
[[nodiscard]] std::string soundWindowReadyStatus();
[[nodiscard]] std::string soundWindowInitialTimeText();
[[nodiscard]] std::string soundWindowNoDataText();
[[nodiscard]] std::string soundWindowPlaybackUnavailableStatus();
[[nodiscard]] std::string soundWindowPlaybackErrorStatusText(std::string_view error);
[[nodiscard]] std::string soundWindowTemporaryFileTemplate(std::string_view extension);
[[nodiscard]] SoundWindowText soundWindowText();
[[nodiscard]] std::string soundWindowLoadedStatus(std::string_view memberName,
                                                  int memberNumber,
                                                  bool hasSound,
                                                  double durationSeconds);
[[nodiscard]] std::string soundWindowTimeText(double currentSeconds, double totalSeconds);
[[nodiscard]] std::span<const std::string_view> textEditorStyleActions();
[[nodiscard]] std::span<const std::string_view> textEditorFontChoices();
[[nodiscard]] std::span<const std::string_view> textEditorSizeChoices();
[[nodiscard]] TextEditorText textEditorText();
[[nodiscard]] TextFormattingDialogText textFormattingDialogText();
[[nodiscard]] std::span<const std::string_view> paragraphAlignmentLabels();
[[nodiscard]] MemberEditorActionText textEditorActionText();
[[nodiscard]] std::string textEditorInitialText();
[[nodiscard]] std::string textEditorReadyStatus();
[[nodiscard]] std::string textEditorNoDataText();
[[nodiscard]] std::string textEditorLoadedStatus(std::string_view memberName, int memberNumber, bool hasText);
[[nodiscard]] std::string textEditorContent(const chunks::TextChunk* textChunk);
[[nodiscard]] std::span<const std::string_view> colorPaletteChoices();
[[nodiscard]] ColorPaletteWindowText colorPaletteWindowText();
[[nodiscard]] std::string colorPalettePlaceholderText();
[[nodiscard]] std::string colorPaletteStatusText(std::string_view paletteName);
[[nodiscard]] std::string colorPaletteLoadedStatusText(std::string_view paletteName, std::size_t colorCount);
[[nodiscard]] std::string colorPaletteUnavailableText(std::string_view paletteName);
[[nodiscard]] std::optional<std::string> colorPaletteBuiltInSymbol(std::string_view paletteChoice);
[[nodiscard]] std::string paletteDescription(int paletteId);
[[nodiscard]] MainMenuText mainMenuText();
[[nodiscard]] std::string viewZoomMenuItemText(int percent);
[[nodiscard]] std::span<const FileCommandSpec> fileCommandSpecs();
[[nodiscard]] FileCreationText fileCreationText();
[[nodiscard]] std::span<const std::string_view> fileCreationStageSizeChoices();
[[nodiscard]] std::string fileCreationMovieStatusText(std::string_view stageSize,
                                                      int frameCount,
                                                      int spriteChannels);
[[nodiscard]] std::string fileCreationCastStatusText(std::string_view castType,
                                                     std::string_view castName);
[[nodiscard]] FileSaveText fileSaveText();
[[nodiscard]] std::string fileSavePreparedDialogText(std::string_view path);
[[nodiscard]] std::string fileSaveAsPreparedDialogText(std::string_view path);
[[nodiscard]] std::string fileSavePreparedStatusText(std::string_view displayName);
[[nodiscard]] std::string fileSaveAsPreparedStatusText(std::string_view displayName);
[[nodiscard]] std::string fileSaveAllPreparedDialogText();
[[nodiscard]] FileOpenImportText fileOpenImportText();
[[nodiscard]] std::string fileImportPreparedDialogText(std::string_view path);
[[nodiscard]] std::string fileImportPreparedStatusText(std::string_view displayName);
[[nodiscard]] std::string fileImportAppliedStatusText(std::string_view displayName, int memberNumber);
[[nodiscard]] std::string fileImportFailedStatusText(std::string_view displayName, int memberNumber);
[[nodiscard]] std::string fileImportCreatedStatusText(std::string_view displayName, int memberNumber);
[[nodiscard]] std::string fileImportCreateFailedStatusText(std::string_view displayName);
[[nodiscard]] std::span<const PreferencePanelSpec> preferencePanelSpecs();
[[nodiscard]] GeneralPreferencesText generalPreferencesText();
[[nodiscard]] std::optional<PreferenceCategoryPanelText> preferenceCategoryPanelText(std::string_view panelId);
[[nodiscard]] std::string preferenceCategorySavedStatusText(std::string_view title);
[[nodiscard]] std::span<const EditCommandSpec> editCommandSpecs();
[[nodiscard]] EditCommandStatusText editCommandStatusText();
[[nodiscard]] std::string_view editCommandSuccessStatusText(std::string_view commandId);
[[nodiscard]] std::span<const EditCommandSpec> findCommandSpecs();
[[nodiscard]] FindReplaceText findReplaceText();
[[nodiscard]] std::string findReplaceFoundStatusText(std::string_view term);
[[nodiscard]] std::string findReplaceNotFoundStatusText(std::string_view term);
[[nodiscard]] std::string findReplaceSingleStatusText(std::string_view term);
[[nodiscard]] std::string findReplaceAllStatusText(int count, std::string_view term);
[[nodiscard]] std::string findReplaceAllPromptText(std::string_view term);
[[nodiscard]] EditSpriteCommandText editSpriteCommandText();
[[nodiscard]] std::string editSpriteFramesStatusText(int frame, std::string_view channelName);
[[nodiscard]] std::string editEntireSpriteStatusText(int memberNumber);
[[nodiscard]] std::span<const ModifyCommandSpec> modifyCommandSpecs();
[[nodiscard]] std::span<const PlaybackCommandSpec> playbackCommandSpecs();
[[nodiscard]] PlaybackToolbarText playbackToolbarText();
[[nodiscard]] std::string playbackFrameText(int frame, int frameCount);
[[nodiscard]] std::span<const ToggleCommandSpec> controlToggleSpecs();
[[nodiscard]] std::span<const int> viewZoomPercentages();
[[nodiscard]] std::span<const ViewCommandSpec> viewSpriteOverlaySpecs();
[[nodiscard]] std::span<const ViewCommandSpec> viewTopLevelToggleSpecs();
[[nodiscard]] std::span<const ViewCommandSpec> viewGridSpecs();
[[nodiscard]] std::span<const ViewCommandSpec> viewGuideSpecs();
[[nodiscard]] ViewGridSettingsText viewGridSettingsText();
[[nodiscard]] std::string viewStageZoomStatusText(int percent);
[[nodiscard]] std::string viewToggleStatusText(std::string_view commandId, bool enabled);
[[nodiscard]] std::span<const WindowCommandSpec> windowCommandSpecs();
[[nodiscard]] MainWindowShellText mainWindowShellText();
[[nodiscard]] MainWindowSummaryText mainWindowSummaryText();
[[nodiscard]] std::string mainWindowTitleForMovie(std::string_view fileName);
[[nodiscard]] std::string mainWindowMovieSummaryText(const MovieSummary& summary);
[[nodiscard]] std::string mainWindowCastSummaryText(const MovieSummary& summary);
[[nodiscard]] std::string mainWindowScoreSummaryText(const MovieSummary& summary);
[[nodiscard]] PanelContextCommandText panelContextCommandText();
[[nodiscard]] std::span<const InsertCommandSpec> insertCommandSpecs();
[[nodiscard]] InsertActionText insertActionText();
[[nodiscard]] std::string insertKeyframePreparedText(int frame, std::string_view channelName);
[[nodiscard]] std::string insertKeyframeStatusText(int frame, std::string_view channelName);
[[nodiscard]] std::string insertMarkerPreparedText(std::string_view markerName, int frame);
[[nodiscard]] std::string insertMarkerStatusText(int frame);
[[nodiscard]] std::string removeFramePreparedText(int frame, int frameCount);
[[nodiscard]] std::string removeFrameStatusText(int frame);
[[nodiscard]] std::string insertMediaElementPreparedText(std::string_view mediaType);
[[nodiscard]] std::string insertMediaElementStatusText(std::string_view mediaType);
[[nodiscard]] std::span<const MediaElementSpec> mediaElementSpecs();
[[nodiscard]] const MediaElementSpec* findMediaElementSpec(std::string_view menuText);
[[nodiscard]] std::span<const DockCommandSpec> dockCommandSpecs();
[[nodiscard]] std::span<const MoveDockCommandSpec> moveDockCommandSpecs();
[[nodiscard]] std::span<const SplitCommandSpec> splitCommandSpecs();
[[nodiscard]] std::span<const DebugCommandSpec> debugCommandSpecs();
[[nodiscard]] std::string debugToolbarToolTipText(std::string_view toolbarText, std::string_view shortcut);
[[nodiscard]] std::span<const DebugContextCommandSpec> debugBytecodeContextCommandSpecs();
[[nodiscard]] std::span<const HelpCommandSpec> helpCommandSpecs();
[[nodiscard]] DebugWatchDialogText debugWatchDialogText();
[[nodiscard]] TraceHandlerDialogText traceHandlerDialogText();
[[nodiscard]] std::vector<std::string> traceHandlerNamesFromInput(std::string_view input);
[[nodiscard]] std::string traceHandlerCurrentText(std::span<const std::string> handlers);
[[nodiscard]] std::string traceHandlerDialogPrompt(std::span<const std::string> handlers);
[[nodiscard]] std::string traceHandlerStatusText(std::size_t handlerCount);
[[nodiscard]] std::span<const std::string_view> debugToolbarActions();
[[nodiscard]] std::span<const std::string_view> debugStateTabNames();
[[nodiscard]] std::span<const std::string_view> debugObjectSectionNames();
[[nodiscard]] std::span<const std::string_view> debugStackTableColumns();
[[nodiscard]] std::span<const std::string_view> debugVariableTableColumns();
[[nodiscard]] std::span<const std::string_view> debugWatchTableColumns();
[[nodiscard]] std::span<const DebugWatchActionSpec> debugWatchActionSpecs();
[[nodiscard]] DebugBrowserText debugBrowserText();
[[nodiscard]] DebugDetailsText debugDetailsText();
[[nodiscard]] std::string debugHandlerDetailsTitle(std::string_view handlerName);
[[nodiscard]] std::string debugHandlerDetailsOverviewText(std::string_view scriptName,
                                                          std::string_view handlerName,
                                                          std::string_view previewText);
[[nodiscard]] std::string debugDatumDetailsTitle(std::string_view title);
[[nodiscard]] std::string debugDatumDetailsText(std::string_view type, std::string_view value);
[[nodiscard]] std::span<const std::string_view> debugTimeoutTableColumns();
[[nodiscard]] std::span<const std::string_view> debugMoviePropertyTableColumns();
[[nodiscard]] std::string debugInitialStatusText();
[[nodiscard]] std::string debugInitialHandlerText();
[[nodiscard]] std::string debugRunningStatusText();
[[nodiscard]] std::string debugUnavailableStatusText();
[[nodiscard]] std::string debugCommandRequestedStatusText(std::string_view commandId);
[[nodiscard]] std::string debugBreakpointsClearedStatusText();
[[nodiscard]] std::string debugBreakpointToggledStatusText(int offset);
[[nodiscard]] std::string debugBreakpointEnabledToggledStatusText(int offset);
[[nodiscard]] std::string debugBytecodeListItemText(std::string_view line,
                                                    bool hasBreakpoint,
                                                    bool breakpointEnabled,
                                                    bool currentInstruction);
[[nodiscard]] std::string debugNavigatedToHandlerStatusText(std::string_view handlerName);
[[nodiscard]] std::string debugPausedStatusText(const player::debug::DebugSnapshot& snapshot);
[[nodiscard]] std::string debugPausedHandlerText(const player::debug::DebugSnapshot& snapshot);
[[nodiscard]] std::vector<DebugTableRow> debugStackRows(std::span<const lingo::Datum> stack);
[[nodiscard]] std::vector<DebugTableRow> debugVariableRows(const std::map<std::string, lingo::Datum>& values);
[[nodiscard]] std::vector<DebugTableRow> debugWatchRows(std::span<const player::debug::WatchExpression> watches);
[[nodiscard]] std::vector<DebugTableRow> debugTimeoutRows(std::span<const player::timeout::TimeoutEntry> timeouts);
[[nodiscard]] std::span<const std::string_view> debugMoviePropertyNames();
[[nodiscard]] std::vector<DebugTableRow> debugMoviePropertyRows(std::span<const std::pair<std::string, lingo::Datum>> values);
[[nodiscard]] std::string debugInstructionListingText(const player::debug::DebugSnapshot& snapshot);
[[nodiscard]] std::string debugBytecodePlaceholderText();
[[nodiscard]] std::string debugBytecodeLegendText();
[[nodiscard]] std::span<const std::string_view> detailedStackTabNames();
[[nodiscard]] std::string detailedStackWindowTitle();
[[nodiscard]] std::string detailedStackInitialStatusText();
[[nodiscard]] std::string detailedStackRunningStatusText();
[[nodiscard]] std::string detailedStackCallStackPlaceholderText();
[[nodiscard]] std::string detailedStackVmStackPlaceholderText();
[[nodiscard]] std::string detailedStackArgumentsPlaceholderText();
[[nodiscard]] std::string detailedStackReceiverPlaceholderText();
[[nodiscard]] std::string detailedStackPausedStatus(const player::debug::DebugSnapshot& snapshot);
[[nodiscard]] std::string detailedStackCallStackText(std::span<const player::debug::CallFrame> callStack);
[[nodiscard]] std::string detailedStackVmStackText(std::span<const lingo::Datum> stack);
[[nodiscard]] std::string detailedStackArgumentsText(std::span<const lingo::Datum> arguments);
[[nodiscard]] std::string detailedStackReceiverText(const std::optional<lingo::Datum>& receiver);
[[nodiscard]] std::string scriptEditorInitialText();
[[nodiscard]] ScriptEditorText scriptEditorText();
[[nodiscard]] std::span<const std::string_view> scriptEditorTableColumns();
[[nodiscard]] std::string scriptEditorSummaryText(int scripts, int globals, int properties);
[[nodiscard]] std::string scriptEditorSelectorLabel(std::string_view scriptName,
                                                    int scriptId,
                                                    std::string_view scriptType);
[[nodiscard]] std::string scriptEditorDefaultCastName();
[[nodiscard]] std::vector<std::string> castLibrarySelectorLabels(std::span<const std::string> externalCastPaths);
[[nodiscard]] std::string scriptEditorNoScriptsText();
[[nodiscard]] std::string scriptEditorNoHandlersText();
[[nodiscard]] std::string scriptEditorAllHandlersText();
[[nodiscard]] std::string scriptEditorNoBytecodeForMemberText();
[[nodiscard]] std::string formatInstructionArgument(const chunks::ScriptChunk::Instruction& instruction,
                                                    const chunks::ScriptChunk& script,
                                                    const chunks::ScriptNamesChunk* names = nullptr);
[[nodiscard]] std::string formatInstruction(const chunks::ScriptChunk::Instruction& instruction,
                                            const chunks::ScriptChunk& script,
                                            const chunks::ScriptNamesChunk* names = nullptr);
[[nodiscard]] std::string formatScriptHandlerPreview(const chunks::ScriptChunk& script,
                                                     const chunks::ScriptChunk::Handler& handler,
                                                     const chunks::ScriptNamesChunk* names = nullptr);
[[nodiscard]] std::string formatScriptHandlerLingoPreview(const chunks::ScriptChunk& script,
                                                          const chunks::ScriptChunk::Handler& handler,
                                                          const chunks::ScriptNamesChunk* names = nullptr);
[[nodiscard]] std::span<const std::string_view> propertyInspectorTabNames();
[[nodiscard]] std::span<const std::string_view> propertyInspectorSpriteLabels();
[[nodiscard]] std::span<const std::string_view> propertyInspectorMemberLabels();
[[nodiscard]] std::span<const std::string_view> propertyInspectorMovieLabels();
[[nodiscard]] PropertyInspectorText propertyInspectorText();
[[nodiscard]] std::string propertyInspectorUnsetValueText();
[[nodiscard]] std::string propertyInspectorBehaviorPlaceholderText();
[[nodiscard]] std::vector<std::string> propertyInspectorBehaviorValues(const ScoreIntervalRow& row);
[[nodiscard]] std::string propertyInspectorRuntimeBehaviorValue(int instanceId,
                                                                int scriptId,
                                                                std::string_view behaviorRef,
                                                                int propertyCount);
[[nodiscard]] std::string propertyInspectorPendingBehaviorValue(int scriptId);
[[nodiscard]] std::string propertyInspectorPendingBehaviorRemovalValue(std::string_view behaviorName);
[[nodiscard]] std::string propertyInspectorBehaviorScriptTooltipText(int scriptId);
[[nodiscard]] std::string propertyInspectorRuntimeBehaviorScriptTooltipText(int scriptId);
[[nodiscard]] std::string propertyInspectorMissingBehaviorScriptText(int scriptId);
[[nodiscard]] std::string propertyInspectorAddBehaviorPromptText(int channel, int frame);
[[nodiscard]] std::string propertyInspectorPreparedAddBehaviorText(int scriptId, int channel, int frame);
[[nodiscard]] std::string propertyInspectorPreparedAddBehaviorStatusText(int scriptId, int channel);
[[nodiscard]] std::string propertyInspectorPreparedRemoveBehaviorText(std::string_view behaviorName,
                                                                      int channel,
                                                                      int frame);
[[nodiscard]] std::string propertyInspectorPreparedRemoveBehaviorStatusText(int channel);
[[nodiscard]] std::string propertyInspectorCanceledPendingBehaviorStatusText(int channel);
[[nodiscard]] std::vector<std::string> propertyInspectorSpriteValues(const ScoreIntervalRow& row);
[[nodiscard]] std::vector<std::string> propertyInspectorMemberValues(const CastMemberRow& row,
                                                                     std::string_view castName);
[[nodiscard]] std::string propertyInspectorCastMemberHeading(int castLib, int memberNumber);
[[nodiscard]] std::vector<std::string> propertyInspectorMovieValues(const MovieSummary& summary,
                                                                    std::string_view movieName);
[[nodiscard]] std::span<const std::string_view> castWindowTypeFilterItems();
[[nodiscard]] std::span<const std::string_view> castWindowTableColumns();
[[nodiscard]] CastWindowText castWindowText();
[[nodiscard]] std::string castWindowLoadExternalCastDialogTitle(std::string_view authoredName);
[[nodiscard]] std::string castWindowLoadedExternalCastStatus(int castLib);
[[nodiscard]] std::string castWindowDefaultCastName();
[[nodiscard]] std::string castWindowReadyStatus();
[[nodiscard]] std::string castWindowNoMovieText();
[[nodiscard]] std::string castWindowNoMembersText();
[[nodiscard]] std::string castWindowNoMembersStatusText();
[[nodiscard]] std::string castWindowCastLibraryLabel(int castLib,
                                                     std::string_view authoredName,
                                                     bool internalCast,
                                                     bool externalCast,
                                                     bool loaded);
[[nodiscard]] std::string castWindowMemberDisplayName(std::string_view memberName);
[[nodiscard]] std::string castWindowMemberCountStatus(int visibleMembers, int totalMembers);
[[nodiscard]] std::string castWindowOpenSelectedButtonText();
[[nodiscard]] int castThumbnailSize();
[[nodiscard]] CastWindowViewModeSpec castWindowGridViewModeSpec();
[[nodiscard]] CastWindowViewModeSpec castWindowListViewModeSpec();
[[nodiscard]] std::string castThumbnailTypeAbbreviation(cast::MemberType type);
[[nodiscard]] std::string castWindowExportActionText();
[[nodiscard]] std::string castWindowExportSelectedActionText();
[[nodiscard]] std::string castWindowSelectAllActionText();
[[nodiscard]] std::string castWindowCopyNameActionText();
[[nodiscard]] std::string castWindowExportStartingStatus(int memberCount);
[[nodiscard]] std::string castMemberExportBaseName(const CastMemberRow& row);
[[nodiscard]] std::string castMemberFallbackExportFileName(int memberNumber);
[[nodiscard]] std::string castMemberExportFileNameWithExtension(const CastMemberRow& row, std::string_view extension);
[[nodiscard]] std::string castMemberExportFileName(const CastMemberRow& row);
[[nodiscard]] std::string castMemberExportDuplicateFileName(const CastMemberRow& row,
                                                            int suffix,
                                                            std::string_view extension);
[[nodiscard]] std::string castWindowExportedStatus(int memberCount);
[[nodiscard]] std::string castWindowExportFailedStatus(int memberCount);
[[nodiscard]] int stageWindowDefaultWidth();
[[nodiscard]] int stageWindowDefaultHeight();
[[nodiscard]] std::string stageWindowNoMovieText();
[[nodiscard]] std::string stageWindowSummaryText(std::string_view fileName, int width, int height, int frame);
[[nodiscard]] std::string stageWindowRenderedTooltip(std::string_view fileName, int frame);
[[nodiscard]] std::string stageWindowNetworkTaskStatusText(std::string_view method,
                                                           std::string_view url,
                                                           std::size_t byteCount);
[[nodiscard]] std::string stageWindowNetworkTaskFailedStatusText(std::string_view method,
                                                                 std::string_view url,
                                                                 std::string_view error);
[[nodiscard]] std::string stageWindowLocalHttpRootStatusText(std::string_view root);
[[nodiscard]] StagePoint stageWindowClampedPoint(int x, int y, int width, int height);
[[nodiscard]] int stageWindowDirectorKeyCodeFromBrowserCode(int browserKeyCode);
[[nodiscard]] int scoreCellWidth();
[[nodiscard]] int scoreCellHeight();
[[nodiscard]] int scoreHeaderHeight();
[[nodiscard]] int scoreChannelHeaderWidth();
[[nodiscard]] std::span<const std::string_view> scoreIntervalTableColumns();
[[nodiscard]] std::span<const std::string_view> scoreSpecialChannelNames();
[[nodiscard]] std::string scoreChannelName(int channelIndex);
[[nodiscard]] std::vector<std::string> scoreChannelLabels(int channelCount);
[[nodiscard]] std::string scoreIntervalChannelDisplayText(int channelIndex);
[[nodiscard]] std::string scoreOverlaySpriteLabel(int channelIndex,
                                                  int castMember,
                                                  std::string_view memberName);
[[nodiscard]] std::string scoreInitialStatusText();
[[nodiscard]] std::string scoreNoDataStatusText();
[[nodiscard]] std::string scoreLoadedStatusText(int frameCount, int channelCount);
[[nodiscard]] std::string scoreFrameStatusText(int frame);
[[nodiscard]] std::string scoreCellStatusText(int frame, int channel);
[[nodiscard]] std::string scoreMarkersText(std::span<const ScoreMarker> markers);
[[nodiscard]] std::uint32_t scoreMemberTypeColorRgb(cast::MemberType memberType);
[[nodiscard]] std::uint32_t scoreIntervalBackgroundRgb(const ScoreIntervalRow& interval);
[[nodiscard]] std::uint32_t scoreActiveFrameBackgroundRgb();
[[nodiscard]] bool scoreIntervalContainsFrame(const ScoreIntervalRow& interval, int frame);
[[nodiscard]] std::string scoreIntervalTooltip(const ScoreIntervalRow& interval);
[[nodiscard]] std::string previewMemberHeader(std::string_view memberKind,
                                              std::string_view memberName,
                                              int memberNumber,
                                              bool blankLineAfterId);
[[nodiscard]] std::string previewTextDataNotFoundText();
[[nodiscard]] std::string previewSoundDataNotFoundText();
[[nodiscard]] std::string previewPaletteDataNotFoundText();
[[nodiscard]] std::string previewNotUsedInScoreText();
[[nodiscard]] std::string previewPaletteInfoHeader(int colorCount);
[[nodiscard]] std::string previewPaletteColorLine(int index, std::uint32_t rgb);
[[nodiscard]] std::string previewPaletteInfo(const std::vector<std::uint32_t>& colors);
[[nodiscard]] std::string paletteExportText(const std::vector<std::uint32_t>& colors);
[[nodiscard]] std::string textExportText(const chunks::TextChunk& textChunk);
[[nodiscard]] std::string scriptExportText(std::string_view memberName,
                                           int memberNumber,
                                           const chunks::ScriptChunk* script,
                                           const chunks::ScriptNamesChunk* names);
[[nodiscard]] PaletteSwatch buildPaletteSwatch(std::span<const std::uint32_t> colors,
                                               int swatchSize = 16,
                                               int columns = 16);
[[nodiscard]] std::string previewFrameAppearances(const std::vector<FrameAppearance>& appearances);
[[nodiscard]] std::string previewScoreAppearances(const std::vector<FrameAppearance>& appearances,
                                                  bool includePosition);
[[nodiscard]] std::vector<FrameAppearance> buildFrameAppearancesFromScoreEntries(
    std::span<const chunks::ScoreChunk::FrameChannelEntry> entries,
    const std::map<int, std::string>& frameLabels,
    int memberId,
    const CastMemberIdResolver& resolveMemberId);
[[nodiscard]] std::string registrationPointText(int x, int y);
[[nodiscard]] std::string castMemberDetails(const CastMemberRow& row);
[[nodiscard]] std::string castMemberPreviewDetails(const CastMemberRow& row,
                                                   const std::vector<FrameAppearance>& appearances,
                                                   bool includePosition);
[[nodiscard]] std::string textMemberPreviewDetails(const CastMemberRow& row,
                                                   const chunks::TextChunk* textChunk);
[[nodiscard]] std::string soundMemberPreviewDetails(const CastMemberRow& row,
                                                    const chunks::SoundChunk* soundChunk,
                                                    const std::vector<FrameAppearance>& appearances);
[[nodiscard]] std::string paletteMemberPreviewDetails(const CastMemberRow& row,
                                                      const std::vector<std::uint32_t>* colors);
[[nodiscard]] std::string scriptMemberPreviewDetails(const CastMemberRow& row,
                                                     const chunks::ScriptChunk* script,
                                                     const chunks::ScriptNamesChunk* names);
[[nodiscard]] std::string genericMemberPreviewDetails(const CastMemberRow& row,
                                                      int specificDataSize,
                                                      const std::vector<FrameAppearance>& appearances);
[[nodiscard]] std::string buildCastMemberEditorPreview(DirectorFile& movie, const CastMemberRow& row);
[[nodiscard]] std::string editorPanelForMemberType(cast::MemberType type);
[[nodiscard]] std::string memberEditorNoCastMemberLoadedText();
[[nodiscard]] StartScreenText startScreenText();
[[nodiscard]] std::string startScreenRecentProjectText(std::string_view name,
                                                       std::string_view parent,
                                                       bool exists);
[[nodiscard]] RecentProjectsMenuText recentProjectsMenuText();
[[nodiscard]] ExchangeCastMembersText exchangeCastMembersText();
[[nodiscard]] std::span<const std::string_view> exchangeCastMembersTableColumns();
[[nodiscard]] std::span<const std::string_view> exchangeCastMembersSlotLabels();
[[nodiscard]] std::string exchangeCastMembersSizeText(int byteCount);
[[nodiscard]] std::string exchangeCastMembersDetailsText(std::string_view firstDetails,
                                                         std::string_view secondDetails);
[[nodiscard]] std::string exchangeCastMembersAppliedStatusText(int firstMemberNumber, int secondMemberNumber);
[[nodiscard]] std::string exchangeCastMembersFailedStatusText(int firstMemberNumber, int secondMemberNumber);
[[nodiscard]] MovieCastsDialogText movieCastsDialogText();
[[nodiscard]] std::span<const std::string_view> movieCastsDialogTableColumns();
[[nodiscard]] std::string movieCastsDialogMemberCountText(std::size_t memberCount);
[[nodiscard]] MoviePropertiesDialogText moviePropertiesDialogText();
[[nodiscard]] CastMemberPropertiesDialogText castMemberPropertiesDialogText();
[[nodiscard]] std::string castMemberPropertiesDialogTitle(std::string_view memberName, int memberNumber);
[[nodiscard]] SpritePropertiesDialogText spritePropertiesDialogText();
[[nodiscard]] std::string spritePropertiesDialogHeading(int frame, std::string_view channelName);
[[nodiscard]] SpriteTweeningDialogText spriteTweeningDialogText();
[[nodiscard]] std::span<const std::string_view> spriteTweeningPropertyLabels();
[[nodiscard]] std::string spriteTweeningSpanText(int startFrame, int endFrame);
[[nodiscard]] std::string spriteTweeningDetailsText(std::string_view scoreDetails);
[[nodiscard]] std::string spriteTweeningOpenedStatusText(int frame, std::string_view channelName);
[[nodiscard]] FrameChannelDialogText frameChannelDialogText();
[[nodiscard]] std::string frameChannelDialogTitle(std::string_view channelName);
[[nodiscard]] std::string frameChannelCastMemberText(int castLib, int castMember);
[[nodiscard]] std::string frameChannelNativeChannelsText(std::string_view firstChannel,
                                                         std::string_view secondChannel);
[[nodiscard]] ExternalParamsDialogText externalParamsDialogText();
[[nodiscard]] std::span<const std::string_view> externalParamsTableColumns();
[[nodiscard]] std::vector<ExternalParamRow> habboExternalParamPreset();
[[nodiscard]] std::vector<std::pair<std::string, std::string>> externalParamsForRuntime(
    std::span<const ExternalParamRow> params);
[[nodiscard]] std::vector<std::string> recentProjectsWithAdded(std::vector<std::string> recent,
                                                                std::string projectPath,
                                                                std::size_t maxProjects = 10);
[[nodiscard]] std::span<const EditorPanelSpec> editorPanelSpecs();
[[nodiscard]] const EditorPanelSpec* findEditorPanelSpec(std::string_view panelId);
[[nodiscard]] bool castMemberMatchesFilter(const CastMemberRow& row, std::string_view filter);
[[nodiscard]] bool castMemberMatchesTypeFilter(const CastMemberRow& row, std::string_view typeFilter);

[[nodiscard]] MovieSummary buildMovieSummary(DirectorFile& movie);
[[nodiscard]] std::vector<CastMemberRow> buildCastMemberRows(const DirectorFile& movie, std::string_view filter = {});
[[nodiscard]] std::vector<ScoreIntervalRow> buildScoreIntervalRows(DirectorFile& movie);
[[nodiscard]] std::vector<FrameAppearance> buildFrameAppearances(DirectorFile& movie, int memberId);
[[nodiscard]] std::vector<ScriptRow> buildScriptRows(DirectorFile& movie);

} // namespace libreshockwave::editor
