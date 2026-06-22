#include <cassert>
#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "EditorModels.hpp"
#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/chunks/ScriptNamesChunk.hpp"
#include "libreshockwave/chunks/SoundChunk.hpp"
#include "libreshockwave/chunks/TextChunk.hpp"
#include "libreshockwave/format/ChunkType.hpp"
#include "libreshockwave/id/Ids.hpp"
#include "libreshockwave/io/BinaryReader.hpp"
#include "libreshockwave/lingo/Opcode.hpp"

using libreshockwave::DirectorFile;
using libreshockwave::cast::MemberType;
using libreshockwave::chunks::ScriptChunk;
using libreshockwave::chunks::ScriptChunkType;
using libreshockwave::chunks::ScriptNamesChunk;
using libreshockwave::chunks::ScoreChunk;
using libreshockwave::chunks::SoundChunk;
using libreshockwave::chunks::TextChunk;
using libreshockwave::lingo::Datum;
using libreshockwave::player::timeout::TimeoutEntry;
using libreshockwave::player::debug::WatchExpression;
using libreshockwave::editor::CastMemberRow;
using libreshockwave::editor::FrameAppearance;
using libreshockwave::editor::MovieSummary;
using libreshockwave::editor::ScoreIntervalRow;
using libreshockwave::editor::buildCastMemberRows;
using libreshockwave::editor::buildFrameAppearances;
using libreshockwave::editor::buildFrameAppearancesFromScoreEntries;
using libreshockwave::editor::buildMovieSummary;
using libreshockwave::editor::buildPaletteSwatch;
using libreshockwave::editor::buildScoreIntervalRows;
using libreshockwave::editor::buildScriptRows;
using libreshockwave::editor::castMemberDetails;
using libreshockwave::editor::castMemberPreviewDetails;
using libreshockwave::editor::castMemberMatchesFilter;
using libreshockwave::editor::castMemberMatchesTypeFilter;
using libreshockwave::editor::castMemberPropertiesDialogText;
using libreshockwave::editor::castMemberPropertiesDialogTitle;
using libreshockwave::editor::castWindowCastLibraryLabel;
using libreshockwave::editor::castWindowDefaultCastName;
using libreshockwave::editor::castWindowMemberCountStatus;
using libreshockwave::editor::castWindowMemberDisplayName;
using libreshockwave::editor::castWindowOpenSelectedButtonText;
using libreshockwave::editor::castWindowNoMembersText;
using libreshockwave::editor::castWindowNoMovieText;
using libreshockwave::editor::castWindowReadyStatus;
using libreshockwave::editor::castThumbnailSize;
using libreshockwave::editor::castThumbnailTypeAbbreviation;
using libreshockwave::editor::castWindowGridViewModeSpec;
using libreshockwave::editor::castWindowLoadedExternalCastStatus;
using libreshockwave::editor::castWindowLoadExternalCastDialogTitle;
using libreshockwave::editor::castWindowListViewModeSpec;
using libreshockwave::editor::castWindowTypeFilterItems;
using libreshockwave::editor::castLibrarySelectorLabels;
using libreshockwave::editor::castWindowCopyNameActionText;
using libreshockwave::editor::castWindowExportActionText;
using libreshockwave::editor::castWindowNoMembersStatusText;
using libreshockwave::editor::castMemberExportBaseName;
using libreshockwave::editor::castMemberExportDuplicateFileName;
using libreshockwave::editor::castMemberFallbackExportFileName;
using libreshockwave::editor::castMemberExportFileName;
using libreshockwave::editor::castMemberExportFileNameWithExtension;
using libreshockwave::editor::castWindowExportedStatus;
using libreshockwave::editor::castWindowExportFailedStatus;
using libreshockwave::editor::castWindowExportStartingStatus;
using libreshockwave::editor::castWindowExportSelectedActionText;
using libreshockwave::editor::castWindowSelectAllActionText;
using libreshockwave::editor::castWindowTableColumns;
using libreshockwave::editor::castWindowText;
using libreshockwave::editor::colorPaletteChoices;
using libreshockwave::editor::colorPaletteBuiltInSymbol;
using libreshockwave::editor::colorPaletteLoadedStatusText;
using libreshockwave::editor::colorPalettePlaceholderText;
using libreshockwave::editor::colorPaletteStatusText;
using libreshockwave::editor::colorPaletteUnavailableText;
using libreshockwave::editor::colorPaletteWindowText;
using libreshockwave::editor::controlToggleSpecs;
using libreshockwave::editor::debugBytecodeLegendText;
using libreshockwave::editor::debugBytecodePlaceholderText;
using libreshockwave::editor::debugBytecodeContextCommandSpecs;
using libreshockwave::editor::debugBrowserText;
using libreshockwave::editor::debugBreakpointEnabledToggledStatusText;
using libreshockwave::editor::debugBreakpointsClearedStatusText;
using libreshockwave::editor::debugBreakpointToggledStatusText;
using libreshockwave::editor::debugBytecodeListItemText;
using libreshockwave::editor::debugCommandRequestedStatusText;
using libreshockwave::editor::debugCommandSpecs;
using libreshockwave::editor::debugToolbarToolTipText;
using libreshockwave::editor::debugDatumDetailsText;
using libreshockwave::editor::debugDatumDetailsTitle;
using libreshockwave::editor::debugDetailsText;
using libreshockwave::editor::debugHandlerDetailsOverviewText;
using libreshockwave::editor::debugHandlerDetailsTitle;
using libreshockwave::editor::debugInitialHandlerText;
using libreshockwave::editor::debugInitialStatusText;
using libreshockwave::editor::debugInstructionListingText;
using libreshockwave::editor::debugMoviePropertyTableColumns;
using libreshockwave::editor::debugMoviePropertyNames;
using libreshockwave::editor::debugMoviePropertyRows;
using libreshockwave::editor::debugNavigatedToHandlerStatusText;
using libreshockwave::editor::debugObjectSectionNames;
using libreshockwave::editor::debugPausedHandlerText;
using libreshockwave::editor::debugPausedStatusText;
using libreshockwave::editor::debugStackRows;
using libreshockwave::editor::debugStackTableColumns;
using libreshockwave::editor::debugStateTabNames;
using libreshockwave::editor::debugTimeoutTableColumns;
using libreshockwave::editor::debugTimeoutRows;
using libreshockwave::editor::debugToolbarActions;
using libreshockwave::editor::debugRunningStatusText;
using libreshockwave::editor::debugUnavailableStatusText;
using libreshockwave::editor::debugVariableRows;
using libreshockwave::editor::debugVariableTableColumns;
using libreshockwave::editor::debugWatchActionSpecs;
using libreshockwave::editor::debugWatchDialogText;
using libreshockwave::editor::debugWatchRows;
using libreshockwave::editor::debugWatchTableColumns;
using libreshockwave::editor::detailedStackArgumentsPlaceholderText;
using libreshockwave::editor::detailedStackArgumentsText;
using libreshockwave::editor::detailedStackCallStackText;
using libreshockwave::editor::detailedStackCallStackPlaceholderText;
using libreshockwave::editor::detailedStackInitialStatusText;
using libreshockwave::editor::detailedStackPausedStatus;
using libreshockwave::editor::detailedStackReceiverPlaceholderText;
using libreshockwave::editor::detailedStackReceiverText;
using libreshockwave::editor::detailedStackRunningStatusText;
using libreshockwave::editor::detailedStackTabNames;
using libreshockwave::editor::detailedStackVmStackPlaceholderText;
using libreshockwave::editor::detailedStackVmStackText;
using libreshockwave::editor::detailedStackWindowTitle;
using libreshockwave::editor::dockCommandSpecs;
using libreshockwave::editor::editCommandSpecs;
using libreshockwave::editor::editorPanelForMemberType;
using libreshockwave::editor::editorPanelSpecs;
using libreshockwave::editor::externalParamsForRuntime;
using libreshockwave::editor::fieldEditorActionText;
using libreshockwave::editor::fieldEditorInitialText;
using libreshockwave::editor::fieldEditorReadyStatus;
using libreshockwave::editor::fieldEditorToolbarActions;
using libreshockwave::editor::findEditorPanelSpec;
using libreshockwave::editor::findMediaElementSpec;
using libreshockwave::editor::findCommandSpecs;
using libreshockwave::editor::findReplaceAllPromptText;
using libreshockwave::editor::findReplaceAllStatusText;
using libreshockwave::editor::findReplaceFoundStatusText;
using libreshockwave::editor::findReplaceNotFoundStatusText;
using libreshockwave::editor::findReplaceSingleStatusText;
using libreshockwave::editor::findReplaceText;
using libreshockwave::editor::fileCommandSpecs;
using libreshockwave::editor::fileCreationCastStatusText;
using libreshockwave::editor::fileCreationMovieStatusText;
using libreshockwave::editor::fileCreationStageSizeChoices;
using libreshockwave::editor::fileCreationText;
using libreshockwave::editor::fileImportPreparedDialogText;
using libreshockwave::editor::fileImportPreparedStatusText;
using libreshockwave::editor::fileImportAppliedStatusText;
using libreshockwave::editor::fileImportCreatedStatusText;
using libreshockwave::editor::fileImportCreateFailedStatusText;
using libreshockwave::editor::fileImportFailedStatusText;
using libreshockwave::editor::fileOpenImportText;
using libreshockwave::editor::fileSaveAllPreparedDialogText;
using libreshockwave::editor::fileSaveAsPreparedDialogText;
using libreshockwave::editor::fileSaveAsPreparedStatusText;
using libreshockwave::editor::fileSavePreparedDialogText;
using libreshockwave::editor::fileSavePreparedStatusText;
using libreshockwave::editor::fileSaveText;
using libreshockwave::editor::formatInstruction;
using libreshockwave::editor::formatInstructionArgument;
using libreshockwave::editor::generalPreferencesText;
using libreshockwave::editor::formatScriptHandlerLingoPreview;
using libreshockwave::editor::formatScriptHandlerPreview;
using libreshockwave::editor::frameChannelCastMemberText;
using libreshockwave::editor::frameChannelDialogText;
using libreshockwave::editor::frameChannelDialogTitle;
using libreshockwave::editor::frameChannelNativeChannelsText;
using libreshockwave::editor::genericMemberPreviewDetails;
using libreshockwave::editor::externalParamsDialogText;
using libreshockwave::editor::externalParamsTableColumns;
using libreshockwave::editor::exchangeCastMembersDetailsText;
using libreshockwave::editor::exchangeCastMembersAppliedStatusText;
using libreshockwave::editor::exchangeCastMembersFailedStatusText;
using libreshockwave::editor::exchangeCastMembersSizeText;
using libreshockwave::editor::exchangeCastMembersSlotLabels;
using libreshockwave::editor::exchangeCastMembersTableColumns;
using libreshockwave::editor::exchangeCastMembersText;
using libreshockwave::editor::habboExternalParamPreset;
using libreshockwave::editor::helpCommandSpecs;
using libreshockwave::editor::editCommandStatusText;
using libreshockwave::editor::editCommandSuccessStatusText;
using libreshockwave::editor::editEntireSpriteStatusText;
using libreshockwave::editor::insertActionText;
using libreshockwave::editor::insertCommandSpecs;
using libreshockwave::editor::insertKeyframePreparedText;
using libreshockwave::editor::insertKeyframeStatusText;
using libreshockwave::editor::insertMarkerPreparedText;
using libreshockwave::editor::insertMarkerStatusText;
using libreshockwave::editor::insertMediaElementPreparedText;
using libreshockwave::editor::insertMediaElementStatusText;
using libreshockwave::editor::editSpriteCommandText;
using libreshockwave::editor::editSpriteFramesStatusText;
using libreshockwave::editor::mainWindowCastSummaryText;
using libreshockwave::editor::mainWindowMovieSummaryText;
using libreshockwave::editor::mainWindowScoreSummaryText;
using libreshockwave::editor::mainWindowShellText;
using libreshockwave::editor::mainWindowSummaryText;
using libreshockwave::editor::mainWindowTitleForMovie;
using libreshockwave::editor::mainMenuText;
using libreshockwave::editor::memberTypeDisplayName;
using libreshockwave::editor::mediaElementSpecs;
using libreshockwave::editor::messageWindowCommandTranscript;
using libreshockwave::editor::messageWindowErrorTranscript;
using libreshockwave::editor::messageWindowExpressionForCommand;
using libreshockwave::editor::messageWindowIsClearCommand;
using libreshockwave::editor::messageWindowResultTranscript;
using libreshockwave::editor::messageWindowWelcomeText;
using libreshockwave::editor::modifyCommandSpecs;
using libreshockwave::editor::moveDockCommandSpecs;
using libreshockwave::editor::movieCastsDialogMemberCountText;
using libreshockwave::editor::movieCastsDialogTableColumns;
using libreshockwave::editor::movieCastsDialogText;
using libreshockwave::editor::moviePropertiesDialogText;
using libreshockwave::editor::memberEditorNoCastMemberLoadedText;
using libreshockwave::editor::paletteDescription;
using libreshockwave::editor::paletteExportText;
using libreshockwave::editor::panelContextCommandText;
using libreshockwave::editor::paintWindowColorActionLabel;
using libreshockwave::editor::paintWindowColorActionTooltip;
using libreshockwave::editor::paintWindowDecodeFailedText;
using libreshockwave::editor::paintWindowColorStatus;
using libreshockwave::editor::paintWindowErrorStatus;
using libreshockwave::editor::paintWindowInitialText;
using libreshockwave::editor::paintWindowLoadedStatus;
using libreshockwave::editor::paintWindowRuntimeEditFailedStatus;
using libreshockwave::editor::paintWindowRuntimeEditStatus;
using libreshockwave::editor::paintWindowToolStatus;
using libreshockwave::editor::paintWindowViewActionSpecs;
using libreshockwave::editor::paintWindowZoomStatus;
using libreshockwave::editor::paintWindowReadyStatus;
using libreshockwave::editor::paintWindowTools;
using libreshockwave::editor::paragraphAlignmentLabels;
using libreshockwave::editor::playbackFrameText;
using libreshockwave::editor::preferenceCategoryPanelText;
using libreshockwave::editor::preferenceCategorySavedStatusText;
using libreshockwave::editor::preferencePanelSpecs;
using libreshockwave::editor::playbackToolbarText;
using libreshockwave::editor::propertyInspectorBehaviorScriptTooltipText;
using libreshockwave::editor::propertyInspectorBehaviorPlaceholderText;
using libreshockwave::editor::propertyInspectorRuntimeBehaviorValue;
using libreshockwave::editor::propertyInspectorRuntimeBehaviorScriptTooltipText;
using libreshockwave::editor::propertyInspectorPendingBehaviorValue;
using libreshockwave::editor::propertyInspectorPendingBehaviorRemovalValue;
using libreshockwave::editor::propertyInspectorBehaviorValues;
using libreshockwave::editor::propertyInspectorAddBehaviorPromptText;
using libreshockwave::editor::propertyInspectorCastMemberHeading;
using libreshockwave::editor::propertyInspectorMissingBehaviorScriptText;
using libreshockwave::editor::propertyInspectorMemberLabels;
using libreshockwave::editor::propertyInspectorMovieLabels;
using libreshockwave::editor::propertyInspectorMemberValues;
using libreshockwave::editor::propertyInspectorMovieValues;
using libreshockwave::editor::propertyInspectorPreparedAddBehaviorStatusText;
using libreshockwave::editor::propertyInspectorPreparedAddBehaviorText;
using libreshockwave::editor::propertyInspectorPreparedRemoveBehaviorStatusText;
using libreshockwave::editor::propertyInspectorPreparedRemoveBehaviorText;
using libreshockwave::editor::propertyInspectorCanceledPendingBehaviorStatusText;
using libreshockwave::editor::propertyInspectorSpriteLabels;
using libreshockwave::editor::propertyInspectorSpriteValues;
using libreshockwave::editor::propertyInspectorText;
using libreshockwave::editor::propertyInspectorTabNames;
using libreshockwave::editor::propertyInspectorUnsetValueText;
using libreshockwave::editor::previewMemberHeader;
using libreshockwave::editor::previewNotUsedInScoreText;
using libreshockwave::editor::previewPaletteColorLine;
using libreshockwave::editor::previewPaletteDataNotFoundText;
using libreshockwave::editor::previewPaletteInfoHeader;
using libreshockwave::editor::previewPaletteInfo;
using libreshockwave::editor::previewFrameAppearances;
using libreshockwave::editor::previewScoreAppearances;
using libreshockwave::editor::previewSoundDataNotFoundText;
using libreshockwave::editor::previewTextDataNotFoundText;
using libreshockwave::editor::paletteMemberPreviewDetails;
using libreshockwave::editor::playbackCommandSpecs;
using libreshockwave::editor::registrationPointText;
using libreshockwave::editor::recentProjectsMenuText;
using libreshockwave::editor::recentProjectsWithAdded;
using libreshockwave::editor::removeFramePreparedText;
using libreshockwave::editor::removeFrameStatusText;
using libreshockwave::editor::routedMemberSelectedStatusText;
using libreshockwave::editor::scriptTypeDisplayName;
using libreshockwave::editor::scriptEditorAllHandlersText;
using libreshockwave::editor::scriptEditorDefaultCastName;
using libreshockwave::editor::scriptEditorInitialText;
using libreshockwave::editor::scriptEditorNoBytecodeForMemberText;
using libreshockwave::editor::scriptEditorNoHandlersText;
using libreshockwave::editor::scriptEditorNoScriptsText;
using libreshockwave::editor::scriptEditorSelectorLabel;
using libreshockwave::editor::scriptEditorSummaryText;
using libreshockwave::editor::scriptEditorTableColumns;
using libreshockwave::editor::scriptEditorText;
using libreshockwave::editor::scriptExportText;
using libreshockwave::editor::scriptMemberPreviewDetails;
using libreshockwave::editor::selectionDetails;
using libreshockwave::editor::SelectionState;
using libreshockwave::editor::SelectionType;
using libreshockwave::editor::selectionTypeDisplayName;
using libreshockwave::editor::soundWindowInitialText;
using libreshockwave::editor::soundWindowInitialTimeText;
using libreshockwave::editor::soundWindowLoadedStatus;
using libreshockwave::editor::soundWindowNoDataText;
using libreshockwave::editor::soundWindowPlaybackErrorStatusText;
using libreshockwave::editor::soundWindowPlaybackUnavailableStatus;
using libreshockwave::editor::soundWindowReadyStatus;
using libreshockwave::editor::soundWindowTemporaryFileTemplate;
using libreshockwave::editor::soundWindowTimeText;
using libreshockwave::editor::soundWindowText;
using libreshockwave::editor::soundMemberPreviewDetails;
using libreshockwave::editor::splitCommandSpecs;
using libreshockwave::editor::scoreCellHeight;
using libreshockwave::editor::scoreCellWidth;
using libreshockwave::editor::scoreChannelLabels;
using libreshockwave::editor::scoreChannelName;
using libreshockwave::editor::scoreChannelHeaderWidth;
using libreshockwave::editor::scoreFrameStatusText;
using libreshockwave::editor::scoreCellStatusText;
using libreshockwave::editor::scoreHeaderHeight;
using libreshockwave::editor::scoreInitialStatusText;
using libreshockwave::editor::scoreActiveFrameBackgroundRgb;
using libreshockwave::editor::scoreIntervalBackgroundRgb;
using libreshockwave::editor::scoreIntervalContainsFrame;
using libreshockwave::editor::scoreIntervalChannelDisplayText;
using libreshockwave::editor::scoreIntervalTableColumns;
using libreshockwave::editor::scoreIntervalTooltip;
using libreshockwave::editor::scoreLoadedStatusText;
using libreshockwave::editor::scoreMarkersText;
using libreshockwave::editor::scoreMemberTypeColorRgb;
using libreshockwave::editor::scoreNoDataStatusText;
using libreshockwave::editor::scoreOverlaySpriteLabel;
using libreshockwave::editor::scoreSpecialChannelNames;
using libreshockwave::editor::spritePropertiesDialogHeading;
using libreshockwave::editor::spritePropertiesDialogText;
using libreshockwave::editor::spriteTweeningDetailsText;
using libreshockwave::editor::spriteTweeningDialogText;
using libreshockwave::editor::spriteTweeningOpenedStatusText;
using libreshockwave::editor::spriteTweeningPropertyLabels;
using libreshockwave::editor::spriteTweeningSpanText;
using libreshockwave::editor::stageWindowDefaultHeight;
using libreshockwave::editor::stageWindowDefaultWidth;
using libreshockwave::editor::stageWindowClampedPoint;
using libreshockwave::editor::stageWindowNoMovieText;
using libreshockwave::editor::stageWindowDirectorKeyCodeFromBrowserCode;
using libreshockwave::editor::stageWindowLocalHttpRootStatusText;
using libreshockwave::editor::stageWindowNetworkTaskFailedStatusText;
using libreshockwave::editor::stageWindowNetworkTaskStatusText;
using libreshockwave::editor::stageWindowRenderedTooltip;
using libreshockwave::editor::stageWindowSummaryText;
using libreshockwave::editor::startScreenRecentProjectText;
using libreshockwave::editor::startScreenText;
using libreshockwave::editor::fieldEditorLoadedStatus;
using libreshockwave::editor::fieldEditorNoDataText;
using libreshockwave::editor::fieldEditorNoDataStatusText;
using libreshockwave::editor::fieldEditorText;
using libreshockwave::editor::textEditorActionText;
using libreshockwave::editor::textEditorFontChoices;
using libreshockwave::editor::textEditorContent;
using libreshockwave::editor::textFormattingDialogText;
using libreshockwave::editor::textEditorInitialText;
using libreshockwave::editor::textEditorLoadedStatus;
using libreshockwave::editor::textEditorNoDataText;
using libreshockwave::editor::textEditorReadyStatus;
using libreshockwave::editor::textEditorSizeChoices;
using libreshockwave::editor::textEditorStyleActions;
using libreshockwave::editor::textEditorText;
using libreshockwave::editor::textExportText;
using libreshockwave::editor::textMemberPreviewDetails;
using libreshockwave::editor::paintWindowToolTipText;
using libreshockwave::editor::ToolNudgeDirection;
using libreshockwave::editor::toolPaletteActiveToolText;
using libreshockwave::editor::toolPaletteNudgeDirectionText;
using libreshockwave::editor::toolPaletteNudgeToolText;
using libreshockwave::editor::toolPaletteTools;
using libreshockwave::editor::toolPaletteSelectedStatusText;
using libreshockwave::editor::toolPaletteStageToolText;
using libreshockwave::editor::toolPaletteToolTipText;
using libreshockwave::editor::toolPaletteNudgeInteractionText;
using libreshockwave::editor::toolPaletteNudgeNoSpriteText;
using libreshockwave::editor::toolPaletteNudgeSpriteFailedText;
using libreshockwave::editor::toolPaletteNudgeSpriteText;
using libreshockwave::editor::toolPaletteStageInteractionText;
using libreshockwave::editor::toolPaletteStageNoSpriteText;
using libreshockwave::editor::toolPaletteStageSpriteMoveFailedText;
using libreshockwave::editor::toolPaletteStageSpriteMoveText;
using libreshockwave::editor::traceHandlerCurrentText;
using libreshockwave::editor::traceHandlerDialogText;
using libreshockwave::editor::traceHandlerDialogPrompt;
using libreshockwave::editor::traceHandlerNamesFromInput;
using libreshockwave::editor::traceHandlerStatusText;
using libreshockwave::editor::vectorShapeToolTipText;
using libreshockwave::editor::vectorShapePlaceholderText;
using libreshockwave::editor::vectorShapeDetailsText;
using libreshockwave::editor::vectorShapeLoadedStatus;
using libreshockwave::editor::vectorShapeRuntimeEditFailedStatus;
using libreshockwave::editor::vectorShapeRuntimeEditStatus;
using libreshockwave::editor::vectorShapeToolStatus;
using libreshockwave::editor::vectorShapeToolbarActions;
using libreshockwave::editor::vectorShapeTypeName;
using libreshockwave::editor::viewGuideSpecs;
using libreshockwave::editor::viewGridSettingsText;
using libreshockwave::editor::viewGridSpecs;
using libreshockwave::editor::viewStageZoomStatusText;
using libreshockwave::editor::viewSpriteOverlaySpecs;
using libreshockwave::editor::viewToggleStatusText;
using libreshockwave::editor::viewTopLevelToggleSpecs;
using libreshockwave::editor::viewZoomMenuItemText;
using libreshockwave::editor::viewZoomPercentages;
using libreshockwave::editor::windowCommandSpecs;
using libreshockwave::format::ChunkType;
using libreshockwave::id::ChunkId;
using libreshockwave::id::ChannelId;
using libreshockwave::id::FrameIndex;
using libreshockwave::io::ByteOrder;
using libreshockwave::lingo::Opcode;

int main() {
    assert(memberTypeDisplayName(MemberType::Bitmap) == "bitmap");
    assert(memberTypeDisplayName(MemberType::Shockwave3D) == "shockwave3d");
    assert(scriptTypeDisplayName(ScriptChunkType::MovieScript) == "Movie Script");
    assert(scriptTypeDisplayName(ScriptChunkType::Unknown) == "Unknown");
    assert(selectionTypeDisplayName(SelectionType::None) == "None");
    assert(selectionTypeDisplayName(SelectionType::CastMember) == "Cast Member");
    assert(selectionDetails({}) == "None");
    assert(selectionDetails(SelectionState{.type = SelectionType::CastMember, .castLib = 2, .memberNum = 7}) ==
           "Cast Member cast 2 member 7");
    assert(selectionDetails(SelectionState{.type = SelectionType::ScoreCell, .channel = 4, .frame = 12}) ==
           "Score Cell channel 4 frame 12");
    assert(messageWindowWelcomeText() == "Welcome to LibreShockwave Editor\n-- Type Lingo commands below\n\n");
    assert(messageWindowCommandTranscript(" put 1 + 1 ") == ">> put 1 + 1\n");
    assert(messageWindowCommandTranscript("  ").empty());
    assert(messageWindowResultTranscript(" 42 ", libreshockwave::lingo::Datum::of(42)) == ">> 42\n42\n");
    assert(messageWindowErrorTranscript("bad", "No movie loaded") == ">> bad\n-- Error: No movie loaded\n");
    assert(messageWindowIsClearCommand(" clear "));
    assert(messageWindowIsClearCommand("CLEAR"));
    assert(!messageWindowIsClearCommand("clear 1"));
    assert(messageWindowExpressionForCommand("put 1 + 1") == std::optional<std::string>{"1 + 1"});
    assert(messageWindowExpressionForCommand("? the frame") == std::optional<std::string>{"the frame"});
    assert(messageWindowExpressionForCommand(" 42 ") == std::optional<std::string>{"42"});
    assert(!messageWindowExpressionForCommand("put"));
    assert(!messageWindowExpressionForCommand(" ? "));
    assert(!messageWindowExpressionForCommand("clear"));
    assert(toolPaletteTools().size() == 14);
    assert(toolPaletteTools().front() == "Arrow");
    assert(toolPaletteTools()[4] == "Line");
    assert(toolPaletteTools().back() == "Color");
    assert(toolPaletteActiveToolText("Arrow") == "Tool: Arrow");
    assert(toolPaletteStageToolText("Line", 12, 34) == "Tool: Line @ 12,34");
    assert(toolPaletteNudgeDirectionText(ToolNudgeDirection::Left) == "left");
    assert(toolPaletteNudgeDirectionText(ToolNudgeDirection::Right) == "right");
    assert(toolPaletteNudgeDirectionText(ToolNudgeDirection::Up) == "up");
    assert(toolPaletteNudgeDirectionText(ToolNudgeDirection::Down) == "down");
    assert(toolPaletteNudgeToolText("Line", "left", 8) == "Tool: Line nudge left 8 px");
    assert(toolPaletteNudgeToolText("Line", "right", 0) == "Tool: Line nudge right 1 px");
    assert(toolPaletteSelectedStatusText("Line") == "Selected tool: Line");
    assert(toolPaletteToolTipText("Line") == "Line tool");
    assert(toolPaletteStageInteractionText("Line", 12, 34) ==
           "Tool Line at 12, 34 (stage editing not yet implemented)");
    assert(toolPaletteStageSpriteMoveText("Line", 4, 12, 34) ==
           "Tool Line moved channel 4 to 12, 34 (runtime sprite)");
    assert(toolPaletteStageNoSpriteText("Line") == "Tool Line needs a selected score sprite or cell");
    assert(toolPaletteStageSpriteMoveFailedText("Line", 4) == "Tool Line could not move channel 4");
    assert(toolPaletteNudgeSpriteText("Line", "left", 8, 4, 20, 30) ==
           "Tool Line nudged channel 4 left by 8 px to 20, 30 (runtime sprite)");
    assert(toolPaletteNudgeNoSpriteText("Line", "left", 8) ==
           "Tool Line nudge left by 8 px needs a selected score sprite or cell");
    assert(toolPaletteNudgeSpriteFailedText("Line", "left", 8, 4) ==
           "Tool Line could not nudge channel 4 left by 8 px");
    assert(toolPaletteNudgeInteractionText("Line", "left", 8) ==
           "Tool Line nudge left by 8 px (stage editing not yet implemented)");
    assert(toolPaletteNudgeInteractionText("Line", "right", 0) ==
           "Tool Line nudge right by 1 px (stage editing not yet implemented)");
    assert(paintWindowTools().size() == 9);
    assert(paintWindowToolTipText("Brush") == "Brush tool");
    assert(paintWindowTools().front() == "Pencil");
    assert(paintWindowTools()[6] == "Oval");
    assert(paintWindowTools().back() == "Lasso");
    const auto paintViewActions = paintWindowViewActionSpecs();
    assert(paintViewActions.size() == 4);
    assert(paintViewActions[0].id == "actual-size");
    assert(paintViewActions[0].label == "1:1");
    assert(paintViewActions[0].tooltip == "Original Size");
    assert(paintViewActions[1].id == "fit");
    assert(paintViewActions[1].tooltip == "Fit to View");
    assert(paintViewActions[2].id == "zoom-out");
    assert(paintViewActions[2].label == "-");
    assert(paintViewActions[3].id == "zoom-in");
    assert(paintViewActions[3].label == "+");
    assert(paintWindowColorActionLabel() == "Color...");
    assert(paintWindowColorActionTooltip() == "Choose Paint Color");
    assert(paintWindowInitialText() == "No bitmap selected");
    assert(paintWindowReadyStatus() == " Ready");
    assert(paintWindowErrorStatus() == " Error");
    assert(paintWindowDecodeFailedText() == "Failed to decode bitmap");
    assert(routedMemberSelectedStatusText() == " Selected cast member");
    assert(paintWindowLoadedStatus("Main Avatar", 5, 320, 200, 8) == " Main Avatar  320x200  8-bit");
    assert(paintWindowLoadedStatus("", 5, 320, 200, 8) == " #5  320x200  8-bit");
    assert(paintWindowZoomStatus(" Main Avatar  320x200  8-bit", 1.25) == " Main Avatar  320x200  8-bit  Zoom 125%");
    assert(paintWindowColorStatus(16, 32, 255) == " Paint color #1020FF");
    assert(paintWindowColorStatus(-1, 300, 17) == " Paint color #00FF11");
    assert(paintWindowRuntimeEditStatus("Brush", 4, 5) == " Brush applied at 4,5 (runtime bitmap)");
    assert(paintWindowRuntimeEditFailedStatus("Brush") == " Brush edit failed");
    assert(paintWindowToolStatus("Pencil", false, 1) == " Pencil tool selected (1 px) - no bitmap selected");
    assert(paintWindowToolStatus("Brush", true, 9) == " Brush tool selected (9 px) (runtime bitmap editing)");
    assert(paintWindowToolStatus("Fill", true, 9) == " Fill tool selected (runtime bitmap editing)");
    assert(paintWindowToolStatus("Line", true, 12) == " Line tool selected (runtime bitmap editing)");
    assert(paintWindowToolStatus("Line", false, 12) == " Line tool selected - no bitmap selected");
    assert(fieldEditorToolbarActions().size() == 2);
    assert(fieldEditorToolbarActions().front() == "Wrap");
    assert(fieldEditorToolbarActions().back() == "Scroll");
    const auto fieldEditorStrings = fieldEditorText();
    assert(fieldEditorStrings.toolbarTitle == " Field Editor ");
    assert(fieldEditorStrings.wrapTooltip == "Toggle line wrapping");
    assert(fieldEditorStrings.scrollTooltip == "Toggle vertical scrolling");
    assert(fieldEditorStrings.wrapEnabledStatus == " Wrap enabled");
    assert(fieldEditorStrings.wrapDisabledStatus == " Wrap disabled");
    assert(fieldEditorStrings.scrollEnabledStatus == " Scroll enabled");
    assert(fieldEditorStrings.scrollDisabledStatus == " Scroll disabled");
    assert(fieldEditorStrings.localTextStatus == " Local text change (not saved)");
    assert(fieldEditorStrings.noMemberLoadedStatus == " No field member loaded");
    assert(fieldEditorStrings.appliedRuntimeStatus == " Applied runtime field text (not saved to file)");
    assert(fieldEditorStrings.applyFailedStatus == " Failed to apply field text");
    const auto fieldActionText = fieldEditorActionText();
    assert(fieldActionText.applyText == "Apply");
    assert(fieldActionText.applyTooltip == "Apply field text to the active runtime member");
    assert(fieldEditorInitialText() == "No field member selected");
    assert(fieldEditorReadyStatus() == " Ready");
    assert(fieldEditorNoDataStatusText() == " No data");
    assert(vectorShapeToolbarActions().size() == 5);
    assert(vectorShapeToolbarActions().front() == "Pen");
    assert(vectorShapeToolbarActions().back() == "Select");
    assert(vectorShapeToolTipText("Oval") == "Oval tool");
    assert(vectorShapeToolStatus("Pen", false) == " Pen tool selected - no vector shape selected");
    assert(vectorShapeToolStatus("Line", true) == " Line tool selected (runtime vector editing)");
    assert(vectorShapeToolStatus("Pen", true) == " Pen tool selected (vector editing not yet implemented)");
    assert(vectorShapeRuntimeEditStatus("Ellipse", libreshockwave::cast::ShapeType::Oval) ==
           " Ellipse applied Oval shape (runtime vector)");
    assert(vectorShapeRuntimeEditFailedStatus("Line") == " Line vector edit failed");
    assert(vectorShapePlaceholderText() == "No vector shape selected");
    const libreshockwave::cast::ShapeInfo shapeInfo{libreshockwave::cast::ShapeType::OvalRect, 4, 5, 64, 32, 12, 3, 1, 2, 0};
    assert(vectorShapeTypeName(libreshockwave::cast::ShapeType::OvalRect) == "Round Rectangle");
    assert(vectorShapeLoadedStatus("Badge", 13, &shapeInfo) == " Badge  Round Rectangle 64x32 line 2 filled");
    assert(vectorShapeLoadedStatus("", 13, nullptr) == " #13  Shape data not found");
    assert(vectorShapeDetailsText(shapeInfo).find("Registration: 4, 5") != std::string::npos);
    assert(soundWindowInitialText() == "No sound member selected");
    assert(soundWindowReadyStatus() == " Ready");
    assert(soundWindowInitialTimeText() == "0.0s / 0.0s");
    assert(soundWindowNoDataText() == "[Sound data not found]");
    assert(soundWindowPlaybackUnavailableStatus() == " Playback backend unavailable");
    const auto soundText = soundWindowText();
    assert(soundText.playButtonText == "Play");
    assert(soundText.stopButtonText == "Stop");
    assert(soundText.noDataTooltip == "No sound data found");
    assert(soundText.playingStatus == " Playing...");
    assert(soundText.stoppedStatus == " Stopped");
    assert(soundText.playbackErrorStatus == " Playback error");
    assert(soundWindowPlaybackErrorStatusText("") == " Playback error");
    assert(soundWindowPlaybackErrorStatusText("decoder failed") == " Playback error: decoder failed");
    assert(soundWindowTemporaryFileTemplate("mp3") == "libreshockwave-sound-XXXXXX.mp3");
    assert(soundWindowTemporaryFileTemplate("") == "libreshockwave-sound-XXXXXX.bin");
    assert(soundWindowLoadedStatus("Bell", 8, true, 1.25) == " Bell  Sound loaded  1.2s");
    assert(soundWindowLoadedStatus("", 8, true, 0.0) == " #8  Sound loaded");
    assert(soundWindowLoadedStatus("Bell", 8, false, 1.25) == " Bell  Sound data not found");
    assert(soundWindowTimeText(0.0, 1.25) == "0.0s / 1.2s");
    assert(soundWindowTimeText(-1.0, -2.0) == "0.0s / 0.0s");
    assert(textEditorStyleActions().size() == 3);
    assert(textEditorStyleActions().front() == "B");
    assert(textEditorStyleActions().back() == "U");
    assert(textEditorFontChoices().size() == 3);
    assert(textEditorFontChoices()[1] == "Times New Roman");
    assert(textEditorSizeChoices().size() == 6);
    assert(textEditorSizeChoices().back() == "36");
    const auto textEditorStrings = textEditorText();
    assert(textEditorStrings.boldTooltip == "Bold");
    assert(textEditorStrings.italicTooltip == "Italic");
    assert(textEditorStrings.underlineTooltip == "Underline");
    assert(textEditorStrings.localFormattingStatus == " Local formatting change (not saved)");
    assert(textEditorStrings.localTextStatus == " Local text change (not applied)");
    assert(textEditorStrings.noMemberLoadedStatus == " No text member loaded");
    assert(textEditorStrings.appliedRuntimeStatus == " Applied runtime text change (not saved to file)");
    assert(textEditorStrings.applyFailedStatus == " Failed to apply text change");
    const auto formattingDialogText = textFormattingDialogText();
    assert(formattingDialogText.fontTitle == "Font");
    assert(formattingDialogText.fontNoEditorText == "Open the Text or Field panel before changing font settings.");
    assert(formattingDialogText.fontTextStatus == " Local font change (not saved)");
    assert(formattingDialogText.fontFieldStatus == " Local font change (not applied)");
    assert(formattingDialogText.paragraphTitle == "Paragraph");
    assert(formattingDialogText.paragraphNoEditorText == "Open the Text panel before changing paragraph settings.");
    assert(formattingDialogText.alignmentLabel == "Alignment:");
    assert(formattingDialogText.leftIndentLabel == "Left indent:");
    assert(formattingDialogText.rightIndentLabel == "Right indent:");
    assert(formattingDialogText.firstLineLabel == "First line:");
    assert(formattingDialogText.lineSpacingLabel == "Line spacing:");
    assert(formattingDialogText.paragraphStatus == " Local paragraph change (not saved)");
    const auto alignments = paragraphAlignmentLabels();
    assert(alignments.size() == 4);
    assert(alignments.front() == "Left");
    assert(alignments[1] == "Center");
    assert(alignments[2] == "Right");
    assert(alignments.back() == "Justify");
    const auto textActionText = textEditorActionText();
    assert(textActionText.applyText == "Apply");
    assert(textActionText.applyTooltip == "Apply text to the active runtime member");
    assert(fieldEditorNoDataText() == "[Field data not found]");
    assert(fieldEditorLoadedStatus("Caption", 4, 12) == " Caption  12 characters");
    assert(fieldEditorLoadedStatus("", 4, 0) == " #4  0 characters");
    assert(textEditorInitialText() == "No text member selected");
    assert(textEditorReadyStatus() == " Ready");
    assert(textEditorLoadedStatus("Greeting", 7, true) == " Greeting  Text loaded");
    assert(textEditorLoadedStatus("", 7, false) == " #7  Text data not found");
    assert(textEditorNoDataText() == "[Text data not found]");
    assert(colorPaletteChoices().size() == 9);
    assert(colorPaletteChoices().front() == "System - Win");
    assert(colorPaletteChoices()[1] == "System - Mac");
    assert(colorPaletteChoices().back() == "Web 216");
    const auto paletteText = colorPaletteWindowText();
    assert(paletteText.selectorLabel == "Palette: ");
    assert(paletteText.unavailableText == "Built-in palette data not available yet");
    assert(colorPalettePlaceholderText() == "Select a palette to preview colors");
    assert(colorPaletteStatusText("Rainbow") == "Palette: Rainbow");
    assert(colorPaletteLoadedStatusText("Rainbow", 256) == "Palette: Rainbow (256 colors)");
    assert(colorPaletteUnavailableText("Web 216") == "Web 216\nBuilt-in palette data not available yet");
    assert(colorPaletteBuiltInSymbol("System - Win") == std::optional<std::string>{"systemWin"});
    assert(colorPaletteBuiltInSymbol("Rainbow") == std::optional<std::string>{"rainbow"});
    assert(!colorPaletteBuiltInSymbol("Web 216").has_value());
    assert(paletteDescription(-1) == "System Mac");
    assert(paletteDescription(-101) == "System Windows");
    assert(paletteDescription(0) == "Cast Member #1");
    assert(paletteDescription(-42) == "Unknown (-42)");
    const auto debugCommands = debugCommandSpecs();
    assert(debugCommands.size() == 8);
    assert(debugCommands[0].id == "step-into");
    assert(debugCommands[0].menuText == "Step Into");
    assert(debugCommands[0].toolbarText == "Step Into");
    assert(debugCommands[0].shortcut == "F11");
    assert(debugToolbarToolTipText(debugCommands[0].toolbarText, debugCommands[0].shortcut) == "Step Into (F11)");
    assert(debugToolbarToolTipText("Run", "") == "Run");
    assert(debugCommands[2].shortcut == "Shift+F11");
    assert(debugCommands[5].id == "clear-breakpoints");
    assert(debugCommands[5].menuText == "Clear All Breakpoints");
    assert(debugCommands[5].toolbarText == "Clear BPs");
    assert(debugCommands[6].shortcut == "Ctrl+Shift+S");
    assert(debugCommands[7].shortcut == "Ctrl+Shift+T");
    const auto debugContextCommands = debugBytecodeContextCommandSpecs();
    assert(debugContextCommands.size() == 4);
    assert(debugContextCommands[0].id == "toggle-breakpoint");
    assert(debugContextCommands[0].menuText == "Toggle Breakpoint");
    assert(!debugContextCommands[0].requiresNavigationTarget);
    assert(debugContextCommands[1].id == "enable-disable-breakpoint");
    assert(debugContextCommands[1].menuText == "Enable/Disable Breakpoint");
    assert(!debugContextCommands[1].requiresNavigationTarget);
    assert(debugContextCommands[2].id == "go-to-definition");
    assert(debugContextCommands[2].menuText == "Go to Definition");
    assert(debugContextCommands[2].requiresNavigationTarget);
    assert(debugContextCommands[3].id == "view-handler-details");
    assert(debugContextCommands[3].menuText == "View Handler Details...");
    assert(debugContextCommands[3].requiresNavigationTarget);
    const auto menuText = mainMenuText();
    assert(menuText.fileMenuText == "&File");
    assert(menuText.preferencesMenuText == "Preferences");
    assert(menuText.editMenuText == "&Edit");
    assert(menuText.viewMenuText == "&View");
    assert(menuText.zoomMenuText == "Zoom");
    assert(menuText.spriteOverlayMenuText == "Sprite Overlay");
    assert(menuText.gridsMenuText == "Grids");
    assert(menuText.guidesMenuText == "Guides");
    assert(menuText.insertMenuText == "&Insert");
    assert(menuText.mediaElementMenuText == "Media Element");
    assert(menuText.modifyMenuText == "&Modify");
    assert(menuText.controlMenuText == "&Control");
    assert(menuText.debugMenuText == "&Debug");
    assert(menuText.windowMenuText == "&Window");
    assert(menuText.helpMenuText == "&Help");
    const auto fileCommands = fileCommandSpecs();
    assert(fileCommands.size() == 11);
    assert(fileCommands[0].id == "new-movie");
    assert(fileCommands[0].menuText == "New Movie");
    assert(fileCommands[0].shortcut == "Ctrl+N");
    assert(fileCommands[2].id == "open");
    assert(fileCommands[2].menuText == "Open...");
    assert(fileCommands[2].shortcut == "Ctrl+O");
    assert(fileCommands[3].id == "open-recent");
    assert(fileCommands[3].menuText == "Open Recent");
    assert(fileCommands[4].id == "close");
    assert(fileCommands[4].shortcut == "Ctrl+W");
    assert(fileCommands[6].id == "save-as");
    assert(fileCommands[6].shortcut == "Ctrl+Shift+S");
    assert(fileCommands[8].id == "import");
    assert(fileCommands[8].shortcut == "Ctrl+R");
    assert(fileCommands[10].id == "exit");
    assert(fileCommands[10].menuText == "Exit");
    const auto creationText = fileCreationText();
    assert(creationText.newMovieTitle == "New Movie");
    assert(creationText.stageSizeLabel == "Stage size:");
    assert(creationText.framesLabel == "Frames:");
    assert(creationText.spriteChannelsLabel == "Sprite channels:");
    assert(creationText.newMoviePendingText.find("Writing new Director movie files is still pending") !=
           std::string_view::npos);
    assert(creationText.newCastTitle == "New Cast");
    assert(creationText.castNameLabel == "Name:");
    assert(creationText.castTypeLabel == "Type:");
    assert(creationText.internalCastType == "Internal");
    assert(creationText.externalCastType == "External");
    assert(creationText.newCastPendingText.find("Adding casts to the Director file is still pending") !=
           std::string_view::npos);
    const auto stageSizeChoices = fileCreationStageSizeChoices();
    assert(stageSizeChoices.size() == 3);
    assert(stageSizeChoices.front() == "640 x 480");
    assert(stageSizeChoices.back() == "1024 x 768");
    assert(fileCreationMovieStatusText("640 x 480", 1, 48) ==
           "Prepared new movie: 640 x 480, 1 frames, 48 sprite channels");
    assert(fileCreationCastStatusText("Internal", "New Cast") == "Prepared new Internal cast: New Cast");
    const auto saveText = fileSaveText();
    assert(saveText.saveTitle == "Save");
    assert(saveText.saveAsTitle == "Save As");
    assert(saveText.saveAllTitle == "Save All");
    assert(saveText.noMovieText == "Open or create a movie before saving.");
    assert(saveText.newMoviePathText == "(new movie)");
    assert(saveText.savePendingText == "Writing Director movie files from the C++ editor is still pending.");
    assert(saveText.saveAsDialogTitle == "Save Director File As");
    assert(saveText.directorFileFilter.find("*.dir") != std::string_view::npos);
    assert(saveText.saveAllNoChangesText == "No open movie or cast changes to save.");
    assert(saveText.saveAllPreparedText == "Prepared save for the open movie and loaded cast state.");
    assert(saveText.saveAllPendingText == "Writing Director movie/cast files from the C++ editor is still pending.");
    assert(saveText.saveAllStatusText == "Prepared save all");
    assert(fileSavePreparedDialogText("/tmp/test.dir") ==
           "Prepared save for:\n/tmp/test.dir\n\nWriting Director movie files from the C++ editor is still pending.");
    assert(fileSaveAsPreparedDialogText("/tmp/new.dir") ==
           "Prepared save as:\n/tmp/new.dir\n\nWriting Director movie files from the C++ editor is still pending.");
    assert(fileSavePreparedStatusText("test.dir") == "Prepared save for test.dir");
    assert(fileSaveAsPreparedStatusText("new.dir") == "Prepared save as new.dir");
    assert(fileSaveAllPreparedDialogText() ==
           "Prepared save for the open movie and loaded cast state.\n\n"
           "Writing Director movie/cast files from the C++ editor is still pending.");
    const auto fileIoText = fileOpenImportText();
    assert(fileIoText.openDirectorFileTitle == "Open Director File");
    assert(fileIoText.openFailedTitle == "Open Failed");
    assert(fileIoText.directorFileFilter == startScreenText().directorFileFilter);
    assert(fileIoText.importTitle == "Import");
    assert(fileIoText.importNoMovieText == "Open a movie before importing media.");
    assert(fileIoText.importMediaTitle == "Import Media");
    assert(fileIoText.importMediaFilter.find("*.png") != std::string_view::npos);
    assert(fileIoText.importMediaFilter.find("*.dcr") != std::string_view::npos);
    assert(fileIoText.importPendingText.find("Select zero or one loaded cast member") != std::string_view::npos);
    assert(fileIoText.importPendingText.find("Zero selected members creates a session-local bitmap member") !=
           std::string_view::npos);
    assert(fileIoText.importPendingText.find("Persisting new cast members") != std::string_view::npos);
    assert(fileImportPreparedDialogText("/tmp/art.png") ==
           "Prepared import of:\n/tmp/art.png\n\n"
           "Select zero or one loaded cast member to import bitmap media into the current runtime session. "
           "Zero selected members creates a session-local bitmap member; one selected member replaces that member's runtime image. "
           "Persisting new cast members in the Director file is still pending. "
           "To load an external cast slot, select the external cast in the Cast panel and run File > Import again.");
    assert(fileImportPreparedStatusText("art.png") == "Prepared import of art.png");
    assert(fileImportAppliedStatusText("art.png", 12) == "Imported art.png into cast member 12");
    assert(fileImportFailedStatusText("art.png", 12) == "Import failed for art.png into cast member 12");
    assert(fileImportCreatedStatusText("art.png", 12) == "Imported art.png into new runtime cast member 12");
    assert(fileImportCreateFailedStatusText("art.png") == "Import failed for art.png; no runtime cast member was created");
    const auto preferencePanels = preferencePanelSpecs();
    assert(preferencePanels.size() == 5);
    assert(preferencePanels[0].id == "general");
    assert(preferencePanels[0].menuText == "General...");
    assert(preferencePanels[1].id == "network");
    assert(preferencePanels[2].id == "script");
    assert(preferencePanels[3].id == "sprite");
    assert(preferencePanels[4].id == "paint");
    assert(preferencePanels[4].menuText == "Paint...");
    const auto generalPrefsText = generalPreferencesText();
    assert(generalPrefsText.title == "General Preferences");
    assert(generalPrefsText.browseText == "Browse...");
    assert(generalPrefsText.defaultOpenFolderLabel == "Default open folder:");
    assert(generalPrefsText.recentProjectsLabel == "Recent Projects");
    assert(generalPrefsText.emptyRecentProjectsText == "No recent projects");
    assert(generalPrefsText.clearRecentProjectsText == "Clear Recent Projects");
    assert(generalPrefsText.chooseDefaultOpenFolderTitle == "Choose Default Open Folder");
    assert(generalPrefsText.savedStatusText == "General preferences saved");
    const auto networkPrefsText = preferenceCategoryPanelText("network");
    assert(networkPrefsText.has_value());
    assert(networkPrefsText->title == "Network Preferences");
    assert(networkPrefsText->boolOptions.size() == 3);
    assert(networkPrefsText->boolOptions[0].key == "enableNetwork");
    assert(networkPrefsText->boolOptions[0].label == "Enable network operations");
    assert(!networkPrefsText->boolOptions[0].defaultValue);
    assert(networkPrefsText->boolOptions[1].key == "allowRemoteAssets");
    assert(networkPrefsText->boolOptions[2].key == "logNetwork");
    assert(networkPrefsText->intOptions.size() == 1);
    assert(networkPrefsText->intOptions[0].key == "timeoutSeconds");
    assert(networkPrefsText->intOptions[0].label == "Request timeout:");
    assert(networkPrefsText->intOptions[0].defaultValue == 30);
    assert(networkPrefsText->intOptions[0].minimum == 1);
    assert(networkPrefsText->intOptions[0].maximum == 300);
    assert(networkPrefsText->choiceOptions.empty());
    const auto scriptPrefsText = preferenceCategoryPanelText("script");
    assert(scriptPrefsText.has_value());
    assert(scriptPrefsText->title == "Script Preferences");
    assert(scriptPrefsText->boolOptions[0].key == "syntaxHighlighting");
    assert(scriptPrefsText->boolOptions[0].defaultValue);
    assert(scriptPrefsText->boolOptions[1].key == "autoIndent");
    assert(scriptPrefsText->boolOptions[2].key == "showBytecodeAnnotations");
    assert(scriptPrefsText->intOptions[0].key == "tabWidth");
    assert(scriptPrefsText->intOptions[0].minimum == 1);
    assert(scriptPrefsText->intOptions[0].maximum == 16);
    assert(scriptPrefsText->choiceOptions.size() == 1);
    assert(scriptPrefsText->choiceOptions[0].key == "defaultView");
    assert(scriptPrefsText->choiceOptions[0].label == "Default script view:");
    assert(scriptPrefsText->choiceOptions[0].defaultValue == "Lingo");
    assert(scriptPrefsText->choiceOptions[0].values.size() == 2);
    assert(scriptPrefsText->choiceOptions[0].values[1] == "Bytecode");
    const auto spritePrefsText = preferenceCategoryPanelText("sprite");
    assert(spritePrefsText.has_value());
    assert(spritePrefsText->title == "Sprite Preferences");
    assert(spritePrefsText->boolOptions[0].key == "showOverlays");
    assert(spritePrefsText->boolOptions[1].label == "Show sprite paths");
    assert(spritePrefsText->boolOptions[2].key == "snapToGrid");
    assert(spritePrefsText->intOptions[0].key == "nudgePixels");
    assert(spritePrefsText->intOptions[0].label == "Keyboard nudge pixels:");
    assert(spritePrefsText->intOptions[0].maximum == 100);
    const auto paintPrefsText = preferenceCategoryPanelText("paint");
    assert(paintPrefsText.has_value());
    assert(paintPrefsText->title == "Paint Preferences");
    assert(paintPrefsText->boolOptions[0].key == "antialiasPreview");
    assert(paintPrefsText->boolOptions[1].key == "showTransparencyGrid");
    assert(paintPrefsText->boolOptions[2].key == "preservePaletteIndexes");
    assert(paintPrefsText->intOptions[0].key == "brushSize");
    assert(paintPrefsText->intOptions[0].defaultValue == 3);
    assert(paintPrefsText->intOptions[0].maximum == 128);
    assert(!preferenceCategoryPanelText("unknown").has_value());
    assert(preferenceCategorySavedStatusText("Paint Preferences") == "Paint Preferences saved");
    const auto editCommands = editCommandSpecs();
    assert(editCommands.size() == 11);
    assert(editCommands[0].id == "undo");
    assert(editCommands[0].menuText == "Undo");
    assert(editCommands[0].shortcut == "Ctrl+Z");
    assert(editCommands[1].id == "redo");
    assert(editCommands[1].shortcut == "Ctrl+Y");
    assert(editCommands[2].id == "cut");
    assert(editCommands[2].shortcut == "Ctrl+X");
    assert(editCommands[5].id == "clear");
    assert(editCommands[5].shortcut == "Delete");
    assert(editCommands[6].id == "select-all");
    assert(editCommands[6].shortcut == "Ctrl+A");
    assert(editCommands[7].id == "find");
    assert(editCommands[7].menuText == "Find");
    assert(editCommands[8].id == "edit-sprite-frames");
    assert(editCommands[9].id == "edit-entire-sprite");
    assert(editCommands[10].id == "exchange-cast-members");
    const auto editStatusText = editCommandStatusText();
    assert(editStatusText.unsupportedText == "No focused editor supports this command");
    assert(editStatusText.unsupportedClearText == "No focused editor supports Clear");
    assert(editStatusText.clearedText == "Cleared");
    assert(editCommandSuccessStatusText("undo") == "Undo");
    assert(editCommandSuccessStatusText("redo") == "Redo");
    assert(editCommandSuccessStatusText("cut") == "Cut");
    assert(editCommandSuccessStatusText("copy") == "Copied");
    assert(editCommandSuccessStatusText("paste") == "Pasted");
    assert(editCommandSuccessStatusText("select-all") == "Selected all");
    assert(editCommandSuccessStatusText("unknown").empty());
    const auto editSpriteText = editSpriteCommandText();
    assert(editSpriteText.editFramesTitle == "Edit Sprite Frames");
    assert(editSpriteText.editFramesNoSelectionText == "Select a score sprite or score cell first.");
    assert(editSpriteText.editEntireTitle == "Edit Entire Sprite");
    assert(editSpriteText.editEntireNoMovieText == "Open a movie first.");
    assert(editSpriteText.editEntireNoCastMemberText == "Select a score sprite with a cast member first.");
    assert(editSpriteText.editEntireMemberNotLoadedText == "The selected sprite's cast member is not loaded.");
    assert(editSpriteText.editEntireNoPanelText == "No editor panel is available for this member type.");
    assert(editSpriteFramesStatusText(12, "Ch 4") == "Editing sprite frames at frame 12, channel Ch 4");
    assert(editEntireSpriteStatusText(27) == "Opened member 27 for entire-sprite editing");
    const auto exchangeText = exchangeCastMembersText();
    assert(exchangeText.title == "Exchange Cast Members");
    assert(exchangeText.selectTwoLoadedText == "Select exactly two loaded cast members to compare before exchange.");
    assert(exchangeText.oneMemberNotLoadedText == "One of the selected members is not loaded.");
    assert(exchangeText.pendingNoteText.find("swaps supported runtime media payloads") != std::string_view::npos);
    assert(exchangeText.pendingNoteText.find("Director file cast-member exchange") != std::string_view::npos);
    assert(exchangeText.exchangeButtonText == "Exchange Runtime Media");
    assert(exchangeText.unnamedMemberText == "(unnamed)");
    assert(exchangeText.unsetText == "-");
    const auto exchangeColumns = exchangeCastMembersTableColumns();
    assert(exchangeColumns.size() == 7);
    assert(exchangeColumns.front() == "Slot");
    assert(exchangeColumns[3] == "Name");
    assert(exchangeColumns.back() == "Script");
    const auto exchangeSlots = exchangeCastMembersSlotLabels();
    assert(exchangeSlots.size() == 2);
    assert(exchangeSlots.front() == "A");
    assert(exchangeSlots.back() == "B");
    assert(exchangeCastMembersSizeText(0) == "-");
    assert(exchangeCastMembersSizeText(42) == "42 bytes");
    assert(exchangeCastMembersDetailsText("#1", "#2") == "A:\n#1\n\nB:\n#2");
    assert(exchangeCastMembersAppliedStatusText(3, 4) == "Exchanged runtime media for cast members 3 and 4");
    assert(exchangeCastMembersFailedStatusText(3, 4) == "Could not exchange runtime media for cast members 3 and 4");
    const auto findCommands = findCommandSpecs();
    assert(findCommands.size() == 4);
    assert(findCommands[0].id == "find");
    assert(findCommands[0].menuText == "Find...");
    assert(findCommands[0].shortcut == "Ctrl+F");
    assert(findCommands[1].id == "find-again");
    assert(findCommands[1].shortcut == "Ctrl+G");
    assert(findCommands[2].id == "replace");
    assert(findCommands[2].shortcut == "Ctrl+H");
    assert(findCommands[3].id == "find-selection");
    assert(findCommands[3].shortcut.empty());
    const auto findText = findReplaceText();
    assert(findText.findTitle == "Find");
    assert(findText.findPrompt == "Find:");
    assert(findText.replaceTitle == "Replace");
    assert(findText.replaceWithPrompt == "Replace with:");
    assert(findText.replaceAllPromptPrefix == "Replace all occurrences of \"");
    assert(findText.replaceAllPromptSuffix == "\"?");
    assert(findText.noSelectedTextToFind == "No selected text to find");
    assert(findText.noSelectedTextToReplace == "No selected text to replace");
    assert(findText.noFocusedFindText == "No focused text widget supports Find");
    assert(findText.noFocusedReplaceText == "No focused text widget supports Replace");
    assert(findReplaceFoundStatusText("door") == "Found \"door\"");
    assert(findReplaceNotFoundStatusText("door") == "Text not found: door");
    assert(findReplaceSingleStatusText("door") == "Replaced \"door\"");
    assert(findReplaceAllStatusText(3, "door") == "Replaced 3 occurrence(s) of \"door\"");
    assert(findReplaceAllPromptText("door") == "Replace all occurrences of \"door\"?");
    const auto modifyCommands = modifyCommandSpecs();
    assert(modifyCommands.size() == 12);
    assert(modifyCommands[0].id == "movie-properties");
    assert(modifyCommands[0].menuText == "Properties...");
    assert(modifyCommands[0].group == "Movie");
    const auto moviePropertiesText = moviePropertiesDialogText();
    assert(moviePropertiesText.title == "Movie Properties");
    assert(moviePropertiesText.noMovieText == "Open a movie to view its properties.");
    assert(moviePropertiesText.untitledMovieText == "Untitled");
    assert(modifyCommands[1].id == "movie-casts");
    const auto movieCastsText = movieCastsDialogText();
    assert(movieCastsText.title == "Movie Casts");
    assert(movieCastsText.noMovieText == "Open a movie to view its casts.");
    assert(movieCastsText.internalKindText == "Internal");
    assert(movieCastsText.externalKindText == "External");
    assert(movieCastsText.loadedStatusText == "Loaded");
    assert(movieCastsText.notLoadedStatusText == "Not loaded");
    assert(movieCastsText.unsetText == "-");
    const auto movieCastsColumns = movieCastsDialogTableColumns();
    assert(movieCastsColumns.size() == 4);
    assert(movieCastsColumns.front() == "Cast");
    assert(movieCastsColumns[1] == "Kind");
    assert(movieCastsColumns.back() == "Members");
    assert(movieCastsDialogMemberCountText(12) == "12");
    assert(modifyCommands[2].id == "external-parameters");
    assert(modifyCommands[2].shortcut == "Ctrl+Shift+E");
    assert(modifyCommands[3].id == "sprite-properties");
    assert(modifyCommands[3].group == "Sprite");
    assert(modifyCommands[4].id == "sprite-tweening");
    assert(modifyCommands[5].id == "cast-member-properties");
    assert(modifyCommands[5].group == "Cast Member");
    assert(modifyCommands[6].id == "frame-tempo");
    assert(modifyCommands[6].group == "Frame");
    assert(modifyCommands[9].id == "frame-sound");
    const auto frameDialogText = frameChannelDialogText();
    assert(frameDialogText.titlePrefix == "Frame ");
    assert(frameDialogText.noScoreText == "Open a movie with score data first.");
    assert(frameDialogText.frameLabel == "Frame:");
    assert(frameDialogText.tempoLabel == "Tempo:");
    assert(frameDialogText.parsedTempoEntriesLabel == "Parsed tempo entries:");
    assert(frameDialogText.paletteCastLabel == "Palette cast:");
    assert(frameDialogText.paletteMemberLabel == "Palette member:");
    assert(frameDialogText.descriptionLabel == "Description:");
    assert(frameDialogText.parsedPaletteEntriesLabel == "Parsed palette entries:");
    assert(frameDialogText.transitionLabel == "Transition:");
    assert(frameDialogText.nativeChannelLabel == "Native channel:");
    assert(frameDialogText.parsingStatusLabel == "Parsing status:");
    assert(frameDialogText.readOnlySnapshotText == "Read-only score channel snapshot");
    assert(frameDialogText.sound1Label == "Sound 1:");
    assert(frameDialogText.sound2Label == "Sound 2:");
    assert(frameDialogText.nativeChannelsLabel == "Native channels:");
    assert(frameDialogText.unsetText == "-");
    assert(frameChannelDialogTitle("Tempo") == "Frame Tempo");
    assert(frameChannelCastMemberText(2, 9) == "Cast 2, member 9");
    assert(frameChannelNativeChannelsText("Sound 1", "Sound 2") == "Sound 1, Sound 2");
    assert(modifyCommands[10].id == "font");
    assert(modifyCommands[10].group.empty());
    assert(modifyCommands[11].id == "paragraph");
    assert(modifyCommands[11].menuText == "Paragraph...");
    const auto playbackCommands = playbackCommandSpecs();
    assert(playbackCommands.size() == 5);
    assert(playbackCommands[0].id == "play");
    assert(playbackCommands[0].menuText == "Play");
    assert(playbackCommands[0].toolbarText == "Play");
    assert(playbackCommands[0].shortcut == "Ctrl+Alt+P");
    assert(playbackCommands[1].id == "stop");
    assert(playbackCommands[1].shortcut == "Ctrl+.");
    assert(playbackCommands[2].id == "rewind");
    assert(playbackCommands[2].shortcut == "Ctrl+Alt+R");
    assert(playbackCommands[3].id == "step-forward");
    assert(playbackCommands[3].shortcut == "Ctrl+Alt+Right");
    assert(playbackCommands[4].id == "step-backward");
    assert(playbackCommands[4].shortcut == "Ctrl+Alt+Left");
    const auto playbackText = playbackToolbarText();
    assert(playbackText.title == "Playback");
    assert(playbackText.initialFrameText == "Frame: 1");
    assert(playbackFrameText(7, 42) == "Frame: 7 / 42");
    const auto controlToggles = controlToggleSpecs();
    assert(controlToggles.size() == 1);
    assert(controlToggles[0].id == "loop-playback");
    assert(controlToggles[0].menuText == "Loop Playback");
    assert(controlToggles[0].checkedByDefault);
    const auto helpCommands = helpCommandSpecs();
    assert(helpCommands.size() == 1);
    assert(helpCommands[0].id == "about");
    assert(helpCommands[0].menuText == "About LibreShockwave Editor");
    assert(helpCommands[0].dialogTitle == "About LibreShockwave Editor");
    assert(helpCommands[0].dialogText.find("A recreation of Macromedia Director MX 2004.") != std::string_view::npos);
    assert(helpCommands[0].dialogText.find("Part of the LibreShockwave project.") != std::string_view::npos);
    const auto zoomPercentages = viewZoomPercentages();
    assert(zoomPercentages.size() == 5);
    assert(zoomPercentages[0] == 25);
    assert(zoomPercentages[2] == 100);
    assert(zoomPercentages[4] == 400);
    assert(viewZoomMenuItemText(100) == "100%");
    assert(viewZoomMenuItemText(-1) == "0%");
    assert(viewStageZoomStatusText(125) == "Stage zoom: 125%");
    const auto spriteOverlaySpecs = viewSpriteOverlaySpecs();
    assert(spriteOverlaySpecs.size() == 2);
    assert(spriteOverlaySpecs[0].id == "sprite-overlay-info");
    assert(spriteOverlaySpecs[0].menuText == "Show Info");
    assert(spriteOverlaySpecs[0].checkable);
    assert(spriteOverlaySpecs[1].id == "sprite-overlay-paths");
    assert(spriteOverlaySpecs[1].menuText == "Show Paths");
    const auto viewToggles = viewTopLevelToggleSpecs();
    assert(viewToggles.size() == 2);
    assert(viewToggles[0].id == "sprite-toolbar");
    assert(viewToggles[0].menuText == "Sprite Toolbar");
    assert(viewToggles[1].id == "keyframes");
    assert(viewToggles[1].menuText == "Keyframes");
    const auto gridSpecs = viewGridSpecs();
    assert(gridSpecs.size() == 3);
    assert(gridSpecs[0].menuText == "Show");
    assert(gridSpecs[0].checkable);
    assert(gridSpecs[1].id == "stage-grid-snap");
    assert(gridSpecs[2].id == "grid-settings");
    assert(gridSpecs[2].menuText == "Settings...");
    assert(!gridSpecs[2].checkable);
    const auto gridSettingsText = viewGridSettingsText();
    assert(gridSettingsText.title == "Grid Settings");
    assert(gridSettingsText.gridSizeLabel == "Grid size:");
    assert(gridSettingsText.guideSnapThresholdLabel == "Guide snap threshold:");
    assert(gridSettingsText.pixelSuffix == " px");
    assert(gridSettingsText.savedStatusText == "Grid settings saved");
    assert(viewToggleStatusText("keyframes", true) == "Score keyframes shown");
    assert(viewToggleStatusText("keyframes", false) == "Score keyframes hidden");
    assert(viewToggleStatusText("score-grid-lines", true) == "Score and Stage grid lines shown");
    assert(viewToggleStatusText("score-grid-lines", false) == "Score and Stage grid lines hidden");
    assert(viewToggleStatusText("sprite-overlay-info", true) == "Sprite overlay info shown");
    assert(viewToggleStatusText("sprite-overlay-paths", false) == "Sprite overlay paths hidden");
    assert(viewToggleStatusText("sprite-toolbar", true) == "Sprite toolbar shown");
    assert(viewToggleStatusText("stage-grid-snap", false) == "Stage grid snapping disabled");
    assert(viewToggleStatusText("stage-guides-show", true) == "Stage guides shown");
    assert(viewToggleStatusText("stage-guides-snap", false) == "Stage guide snapping disabled");
    assert(viewToggleStatusText("unknown", true).empty());
    const auto guideSpecs = viewGuideSpecs();
    assert(guideSpecs.size() == 2);
    assert(guideSpecs[0].id == "stage-guides-show");
    assert(guideSpecs[1].id == "stage-guides-snap");
    assert(guideSpecs[1].menuText == "Snap To");
    const auto insertCommands = insertCommandSpecs();
    assert(insertCommands.size() == 3);
    assert(insertCommands[0].id == "keyframe");
    assert(insertCommands[0].menuText == "Keyframe");
    assert(insertCommands[0].shortcut == "Ctrl+Alt+K");
    assert(insertCommands[1].id == "marker");
    assert(insertCommands[1].shortcut.empty());
    assert(insertCommands[2].id == "remove-frame");
    assert(insertCommands[2].menuText == "Remove Frame");
    const auto insertText = insertActionText();
    assert(insertText.keyframeTitle == "Insert Keyframe");
    assert(insertText.keyframeNoMovieText == "Open a movie before inserting keyframes.");
    assert(insertText.keyframeNoSelectionText == "Select a score sprite or score cell before inserting a keyframe.");
    assert(insertText.markerTitle == "Insert Marker");
    assert(insertText.markerPromptLabel == "Marker name:");
    assert(insertText.markerDefaultName == "New Marker");
    assert(insertText.removeFrameTitle == "Remove Frame");
    assert(insertText.mediaElementTitle == "Insert Media Element");
    assert(insertText.mediaElementNoMovieText == "Open a movie before creating media elements.");
    assert(insertKeyframePreparedText(9, "Ch 2") ==
           "Added a session keyframe preview at frame 9, channel Ch 2.\n\n"
           "Writing score keyframes back into the Director file is still pending.");
    assert(insertKeyframeStatusText(9, "Ch 2") == "Added session keyframe preview at frame 9, Ch 2");
    assert(insertMarkerPreparedText("Intro", 4) ==
           "Prepared marker \"Intro\" at frame 4.\n\n"
           "Writing marker labels back into the Director file is still pending.");
    assert(insertMarkerPreparedText("", 4).find("New Marker") != std::string::npos);
    assert(insertMarkerStatusText(4) == "Prepared marker at frame 4");
    assert(removeFramePreparedText(6, 42) ==
           "Added a session removal preview for frame 6 of 42.\n\n"
           "Writing frame removal back into the Director score is still pending.");
    assert(removeFrameStatusText(6) == "Added session removal preview for frame 6");
    assert(insertMediaElementPreparedText("Bitmap") ==
           "Prepared creation of a Bitmap media element.\n\n"
           "Persisting new cast members back into the Director file is still pending.");
    assert(insertMediaElementStatusText("Bitmap") == "Prepared Bitmap media element creation");
    const auto mediaElements = mediaElementSpecs();
    assert(mediaElements.size() == 6);
    assert(mediaElements[0].menuText == "Bitmap");
    assert(mediaElements[0].targetPanelId == "paint");
    assert(mediaElements[3].menuText == "Shape");
    assert(mediaElements[3].targetPanelId == "vector-shape");
    assert(mediaElements[4].menuText == "Film Loop");
    assert(mediaElements[4].targetPanelId == "cast");
    assert(findMediaElementSpec("Sound")->targetPanelId == "sound");
    assert(findMediaElementSpec("script")->menuText == "Script");
    assert(findMediaElementSpec("missing") == nullptr);
    const auto traceNames = traceHandlerNamesFromInput(" startMovie, exitFrame ,, mouseUp ");
    assert(traceNames.size() == 3);
    assert(traceNames[0] == "startMovie");
    assert(traceNames[1] == "exitFrame");
    assert(traceNames[2] == "mouseUp");
    const std::vector<std::string> traceCurrent{"mouseup", "startmovie"};
    assert(traceHandlerCurrentText({}) == "(none)");
    assert(traceHandlerCurrentText(traceCurrent) == "mouseup, startmovie");
    assert(traceHandlerDialogPrompt(traceCurrent) ==
           "Enter handler names to trace (comma-separated), or clear to remove all:\nCurrent: mouseup, startmovie");
    const auto traceText = traceHandlerDialogText();
    assert(traceText.title == "Trace Handler");
    assert(traceText.noMovieText == "No movie loaded.");
    assert(traceText.noneText == "(none)");
    assert(traceHandlerStatusText(0) == "Trace handlers cleared");
    assert(traceHandlerStatusText(2) == "Tracing 2 handler(s)");
    assert(debugToolbarActions().size() == 5);
    assert(debugToolbarActions().front() == "Step Into");
    assert(debugToolbarActions().back() == "Clear BPs");
    assert(debugStateTabNames().size() == 5);
    assert(debugStateTabNames()[0] == "Stack");
    assert(debugStateTabNames()[4] == "Objects");
    assert(debugObjectSectionNames().size() == 3);
    assert(debugObjectSectionNames()[2] == "Movie Properties");
    assert(debugStackTableColumns().size() == 3);
    assert(debugStackTableColumns()[0] == "#");
    assert(debugVariableTableColumns()[0] == "Name");
    assert(debugWatchTableColumns()[0] == "Expression");
    const auto watchActions = debugWatchActionSpecs();
    assert(watchActions.size() == 4);
    assert(watchActions[0].id == "add-watch");
    assert(watchActions[0].buttonText == "+");
    assert(watchActions[0].tooltipText == "Add watch expression");
    assert(watchActions[1].id == "remove-watch");
    assert(watchActions[1].buttonText == "-");
    assert(watchActions[1].tooltipText == "Remove selected watch");
    assert(watchActions[2].id == "edit-watch");
    assert(watchActions[2].buttonText == "Edit");
    assert(watchActions[2].tooltipText == "Edit selected watch");
    assert(watchActions[3].id == "clear-watches");
    assert(watchActions[3].buttonText == "Clear");
    assert(watchActions[3].tooltipText == "Clear all watches");
    const auto watchText = debugWatchDialogText();
    assert(watchText.addTitle == "Add Watch");
    assert(watchText.editTitle == "Edit Watch");
    assert(watchText.expressionPrompt == "Expression:");
    assert(debugTimeoutTableColumns().size() == 5);
    assert(debugTimeoutTableColumns()[1] == "Period (ms)");
    assert(debugMoviePropertyTableColumns().size() == 2);
    assert(debugMoviePropertyTableColumns()[0] == "Property");
    assert(debugMoviePropertyNames().size() == 10);
    assert(debugMoviePropertyNames().front() == "frame");
    assert(debugMoviePropertyNames().back() == "puppetTempo");
    assert(debugInitialStatusText() == "Status: Running");
    assert(debugInitialHandlerText() == "Handler: -");
    assert(debugRunningStatusText() == "Debug running");
    assert(debugUnavailableStatusText() == "Debugger unavailable");
    assert(debugCommandRequestedStatusText("step-into") == "Debug step into requested");
    assert(debugCommandRequestedStatusText("step-over") == "Debug step over requested");
    assert(debugCommandRequestedStatusText("step-out") == "Debug step out requested");
    assert(debugCommandRequestedStatusText("continue") == "Debug continue requested");
    assert(debugCommandRequestedStatusText("missing").empty());
    assert(debugBreakpointsClearedStatusText() == "Breakpoints cleared, including saved breakpoints");
    assert(debugBreakpointToggledStatusText(12) == "Breakpoint toggled at offset 12");
    assert(debugBreakpointEnabledToggledStatusText(12) == "Breakpoint enabled state toggled at offset 12");
    assert(debugBytecodeListItemText("[0001] PUSH_INT8 1", true, true, true) == "● ▶ [0001] PUSH_INT8 1");
    assert(debugBytecodeListItemText("[0001] PUSH_INT8 1", true, false, false) == "○   [0001] PUSH_INT8 1");
    assert(debugBytecodeListItemText("[0001] PUSH_INT8 1", false, false, true) == "  ▶ [0001] PUSH_INT8 1");
    assert(debugBytecodeListItemText("[0001] PUSH_INT8 1", false, false, false) == "    [0001] PUSH_INT8 1");
    assert(debugNavigatedToHandlerStatusText("mouseUp") == "Navigated to handler mouseUp");
    const auto debugBrowser = debugBrowserText();
    assert(debugBrowser.scriptLabel == "Script:");
    assert(debugBrowser.handlerLabel == "Handler:");
    assert(debugBrowser.filterPlaceholder == "Filter");
    assert(debugBrowser.scriptFilterTooltip == "Type to filter scripts");
    assert(debugBrowser.handlerFilterTooltip == "Type to filter handlers");
    assert(debugBrowser.detailsButtonText == "i");
    assert(debugBrowser.detailsButtonTooltip == "View Handler Details");
    assert(debugBrowser.bytecodeTitle == "Bytecode");
    assert(debugBrowser.lingoToggleText == "Lingo");
    assert(debugBrowser.bytecodeToggleText == "Bytecode");
    assert(debugBrowser.lingoToggleTooltip == "Toggle between bytecode and decompiled Lingo view");
    const auto debugDetails = debugDetailsText();
    assert(debugDetails.overviewTabText == "Overview");
    assert(debugDetails.bytecodeTabText == "Bytecode");
    assert(debugDetails.literalsTabText == "Literals");
    assert(debugDetails.propertiesTabText == "Properties");
    assert(debugDetails.globalsTabText == "Globals");
    assert(debugDetails.closeButtonText == "Close");
    assert(debugHandlerDetailsTitle("mouseUp") == "Handler: mouseUp");
    assert(debugHandlerDetailsOverviewText("Script 1", "mouseUp", "line 1\nline 2") ==
           "Script: Script 1\nHandler: mouseUp\n\nline 1\nline 2");
    assert(debugDatumDetailsTitle("globalVar") == "Datum Details: globalVar");
    assert(debugDatumDetailsText("integer", "42") == "Type: integer\n\nValue:\n42");
    assert(debugBytecodePlaceholderText() == "-- Select a handler to view bytecode");
    assert(debugBytecodeLegendText().find("breakpoint") != std::string::npos);
    assert(detailedStackTabNames().size() == 4);
    assert(detailedStackTabNames().front() == "Call Stack");
    assert(detailedStackTabNames().back() == "Receiver (me)");
    assert(detailedStackWindowTitle() == "Detailed Stack View");
    assert(detailedStackInitialStatusText() == "Waiting for debugger pause...");
    assert(detailedStackRunningStatusText() == "Running...");
    assert(detailedStackCallStackPlaceholderText() == "(no call stack)");
    assert(detailedStackVmStackPlaceholderText() == "(empty stack)");
    assert(detailedStackArgumentsPlaceholderText() == "(no arguments)");
    assert(detailedStackReceiverPlaceholderText() == "(no receiver)");
    libreshockwave::player::debug::DebugSnapshot debugSnapshot;
    debugSnapshot.scriptName = "Button Script";
    debugSnapshot.handlerName = "mouseUp";
    debugSnapshot.instructionOffset = 12;
    debugSnapshot.instructionIndex = 1;
    debugSnapshot.allInstructions = {
        libreshockwave::player::debug::InstructionDisplay{
            .offset = 8,
            .index = 0,
            .opcode = "PUSH_INT",
            .argument = 7,
            .annotation = "push literal",
            .hasBreakpoint = false,
        },
        libreshockwave::player::debug::InstructionDisplay{
            .offset = 12,
            .index = 1,
            .opcode = "RET",
            .annotation = "return",
            .hasBreakpoint = true,
        },
    };
    debugSnapshot.stack = {Datum::of(42), Datum::of(std::string("top"))};
    debugSnapshot.arguments = {Datum::of(std::string("argValue"))};
    debugSnapshot.locals = {{"localOne", Datum::of(123)}};
    debugSnapshot.globals = {{"globalOne", Datum::of(std::string("world"))}};
    debugSnapshot.watchResults = {WatchExpression::create("w1", "the frame").withValue(Datum::of(5))};
    debugSnapshot.receiver = Datum::scriptInstance("ButtonBehavior");
    debugSnapshot.callStack.push_back(libreshockwave::player::debug::CallFrame{
        .scriptName = "Button Script",
        .handlerName = "mouseUp",
        .arguments = {Datum::of(7)},
        .receiver = Datum::scriptInstance("ButtonBehavior"),
    });
    assert(debugPausedStatusText(debugSnapshot) == "Status: PAUSED at offset 12");
    assert(debugPausedHandlerText(debugSnapshot) == "Handler: mouseUp (Button Script)");
    const auto stackRows = debugStackRows(debugSnapshot.stack);
    assert(stackRows.size() == 2);
    assert(stackRows[0].cells[0] == "0");
    assert(stackRows[0].cells[1] == "Int");
    assert(stackRows[0].cells[2] == "42");
    assert(stackRows[0].detailTitle == "Stack[0]");
    assert(stackRows[0].detailType == "Int");
    const auto localRows = debugVariableRows(debugSnapshot.locals);
    assert(localRows.size() == 1);
    assert(localRows[0].cells[0] == "localOne");
    assert(localRows[0].cells[1] == "Int");
    assert(localRows[0].cells[2] == "123");
    assert(localRows[0].detailTitle == "localOne");
    const auto watchRows = debugWatchRows(debugSnapshot.watchResults);
    assert(watchRows.size() == 1);
    assert(watchRows[0].id == "w1");
    assert(watchRows[0].cells[0] == "the frame");
    assert(watchRows[0].cells[1] == "int");
    assert(watchRows[0].cells[2] == "5");
    const std::vector<TimeoutEntry> timeoutEntries{TimeoutEntry{
        .name = "tick",
        .periodMs = 1000,
        .handler = "onTick",
        .target = Datum::scriptInstance("Clock"),
        .persistent = true,
    }};
    const auto timeoutRows = debugTimeoutRows(timeoutEntries);
    assert(timeoutRows.size() == 1);
    assert(timeoutRows[0].cells[0] == "tick");
    assert(timeoutRows[0].cells[1] == "1000");
    assert(timeoutRows[0].cells[2] == "onTick");
    assert(timeoutRows[0].cells[4] == "Yes");
    const std::vector<std::pair<std::string, Datum>> movieProps{{"frame", Datum::of(3)}};
    const auto moviePropRows = debugMoviePropertyRows(movieProps);
    assert(moviePropRows.size() == 1);
    assert(moviePropRows[0].cells[0] == "frame");
    assert(moviePropRows[0].cells[1] == "3");
    const auto instructionListing = debugInstructionListingText(debugSnapshot);
    assert(instructionListing.find("[0008] PUSH_INT") != std::string::npos);
    assert(instructionListing.find("=> B [0012] RET") != std::string::npos);
    assert(instructionListing.find("-- return") != std::string::npos);
    assert(detailedStackPausedStatus(debugSnapshot) == "Paused at: mouseUp (offset 12)");
    assert(detailedStackCallStackText(std::span<const libreshockwave::player::debug::CallFrame>{})
               .find("(no call stack)") != std::string::npos);
    assert(detailedStackCallStackText(debugSnapshot.callStack).find("Call Stack (1 frames):") != std::string::npos);
    assert(detailedStackCallStackText(debugSnapshot.callStack).find("> [0] mouseUp(") != std::string::npos);
    assert(detailedStackVmStackText(std::span<const Datum>{}) == "(empty stack)");
    assert(detailedStackVmStackText(debugSnapshot.stack).find("[  1]") != std::string::npos);
    assert(detailedStackArgumentsText(std::span<const Datum>{}) == "(no arguments)");
    assert(detailedStackArgumentsText(debugSnapshot.arguments).find("arg1 =") != std::string::npos);
    assert(detailedStackReceiverText(std::nullopt) == "(no receiver)");
    assert(detailedStackReceiverText(debugSnapshot.receiver).find("ButtonBehavior") != std::string::npos);
    assert(scriptEditorInitialText() == "-- Select a script member to view");
    const auto scriptText = scriptEditorText();
    assert(scriptText.castLabel == " Cast: ");
    assert(scriptText.scriptLabel == " Script: ");
    assert(scriptText.handlerLabel == " Handler: ");
    assert(scriptText.lingoToggleText == "Lingo");
    assert(scriptText.bytecodeToggleText == "Bytecode");
    assert(scriptText.lingoToggleTooltip == "Toggle between bytecode and decompiled Lingo view");
    assert(scriptText.emptySummaryText == "Scripts: 0");
    assert(memberEditorNoCastMemberLoadedText() == "No cast member loaded");
    const auto scriptColumns = scriptEditorTableColumns();
    assert(scriptColumns.size() == 6);
    assert(scriptColumns[0] == "Id");
    assert(scriptColumns[1] == "Name");
    assert(scriptColumns[2] == "Type");
    assert(scriptColumns[3] == "Handlers");
    assert(scriptColumns[4] == "Globals");
    assert(scriptColumns[5] == "Properties");
    assert(scriptEditorSummaryText(3, 2, 1) == "Scripts: 3\nGlobals: 2\nProperties: 1");
    assert(scriptEditorSummaryText(-3, -2, -1) == "Scripts: 0\nGlobals: 0\nProperties: 0");
    assert(scriptEditorSelectorLabel("Behavior Script", 12, "Behavior") == "Behavior Script (Behavior)");
    assert(scriptEditorSelectorLabel("", 12, "Movie") == "#12 (Movie)");
    assert(scriptEditorDefaultCastName() == "Internal");
    const std::vector<std::string> externalCastPaths{"/movies/External Cast.cst", ""};
    assert((castLibrarySelectorLabels(externalCastPaths) == std::vector<std::string>{
                                                            "Internal",
                                                            "External Cast.cst (not loaded)",
                                                            "Cast 2 (not loaded)",
                                                        }));
    assert(scriptEditorNoScriptsText() == "(No scripts)");
    assert(scriptEditorNoHandlersText() == "(No handlers)");
    assert(scriptEditorAllHandlersText() == "(All handlers)");
    assert(scriptEditorNoBytecodeForMemberText() == "-- No bytecode found for this script member");
    const ScriptChunk bytecodeScript(nullptr,
                                     ChunkId(900),
                                     ScriptChunkType::MovieScript,
                                     0,
                                     {},
                                     {ScriptChunk::LiteralEntry{1, 0, std::string("hello"), 0.0}},
                                     {},
                                     {},
                                     {});
    const ScriptNamesChunk bytecodeNames(nullptr,
                                         ChunkId(901),
                                         {"mySymbol", "globalName", "handlerName", "traceScript", "Parent", "varRef"});
    assert(formatInstruction({12, Opcode::PUSH_CONS, 0x44, 0}, bytecodeScript, &bytecodeNames) ==
           "[0012] pushCons         0 <str> \"hello\"");
    assert(formatInstruction({13, Opcode::PUSH_SYMB, 0x45, 0}, bytecodeScript, &bytecodeNames) ==
           "[0013] pushSymb         0 #mySymbol");
    assert(formatInstructionArgument({14, Opcode::GET_GLOBAL, 0x49, 1}, bytecodeScript, &bytecodeNames) ==
           "1 (globalName)");
    assert(formatInstruction({15, Opcode::EXT_CALL, 0x57, 2}, bytecodeScript, &bytecodeNames) ==
           "[0015] extCall          2 [handlerName]");
    assert(formatInstruction({16, Opcode::THE_BUILTIN, 0x66, 3}, bytecodeScript, &bytecodeNames) ==
           "[0016] theBuiltin       3 the traceScript");
    assert(formatInstruction({17, Opcode::NEW_OBJ, 0x73, 4}, bytecodeScript, &bytecodeNames) ==
           "[0017] newObj           4 new(Parent)");
    assert(formatInstruction({18, Opcode::PUSH_VAR_REF, 0x46, 5}, bytecodeScript, &bytecodeNames) ==
           "[0018] pushVarRef       5 @varRef");
    assert(formatInstruction({19, Opcode::JMP, 0x53, 8}, bytecodeScript, &bytecodeNames) ==
           "[0019] jmp              8 -> offset 8");
    assert(formatInstruction({20, Opcode::ADD, 0x05, 0}, bytecodeScript, &bytecodeNames) ==
           "[0020] add             ");
    assert(formatInstruction({21, Opcode::PUSH_INT8, 0x41, 42}, bytecodeScript, nullptr) ==
           "[0021] pushInt8         42");
    const ScriptChunk::Handler bytecodeHandler{
        .nameId = 2,
        .handlerVectorPos = 0,
        .bytecodeLength = 4,
        .bytecodeOffset = 100,
        .argCount = 1,
        .localCount = 1,
        .globalsCount = 0,
        .lineCount = 0,
        .argNameIds = {0},
        .localNameIds = {1},
        .instructions = {
            {12, Opcode::PUSH_CONS, 0x44, 0},
            {15, Opcode::EXT_CALL, 0x57, 2},
        },
        .bytecodeIndexMap = {},
    };
    assert(formatScriptHandlerPreview(bytecodeScript, bytecodeHandler, &bytecodeNames) ==
           "on handlerName\n"
           "  -- args: mySymbol\n"
           "  -- locals: globalName\n"
           "  -- bytecodeOffset: 100  bytecodeLength: 4\n"
           "\n"
           "  [0012] pushCons         0 <str> \"hello\"           ; <hello> | push literal[0] = \"hello\"\n"
           "  [0015] extCall          2 [handlerName]           ; <handlerName()> | call external handlerName(args)\n"
           "end\n");
    const ScriptChunk scriptPreviewChunk(nullptr,
                                         ChunkId(901),
                                         ScriptChunkType::Behavior,
                                         0x2A,
                                         {bytecodeHandler},
                                         {{1, 0, std::string{"hello"}, 0.0}},
                                         {{1}},
                                         {{0}},
                                         {});
    const CastMemberRow scriptRow{.chunkId = 901,
                                  .type = "Script",
                                  .name = "Behavior Script",
                                  .memberType = MemberType::Script,
                                  .scriptId = 0,
                                  .regPointX = 0,
                                  .regPointY = 0};
    assert(scriptMemberPreviewDetails(scriptRow, nullptr, &bytecodeNames) ==
           "=== SCRIPT: Behavior Script ===\n\nMember ID: 901\n\n[No bytecode found for this script member]\n");
    assert(scriptExportText("Behavior Script", 901, nullptr, &bytecodeNames) ==
           "-- No bytecode found for script member #901\n");
    assert(scriptMemberPreviewDetails(scriptRow, &scriptPreviewChunk, &bytecodeNames) ==
           "=== SCRIPT: Behavior Script ===\n"
           "\n"
           "Member ID: 901\n"
           "Script Type: Behavior\n"
           "Behavior Flags: 0x2a\n"
           "\n"
           "--- PROPERTIES ---\n"
           "  property globalName\n"
           "\n"
           "--- GLOBALS ---\n"
           "  global mySymbol\n"
           "\n"
           "--- HANDLERS (1) ---\n"
           "\n"
           "on handlerName\n"
           "  -- args: mySymbol\n"
           "  -- locals: globalName\n"
           "  -- bytecodeOffset: 100  bytecodeLength: 4\n"
           "\n"
           "  [0012] pushCons         0 <str> \"hello\"           ; <hello> | push literal[0] = \"hello\"\n"
           "  [0015] extCall          2 [handlerName]           ; <handlerName()> | call external handlerName(args)\n"
           "end\n"
           "\n"
           "--- LITERALS (1) ---\n"
           "  [0] \"hello\"\n");
    const auto lingoPreview = formatScriptHandlerLingoPreview(scriptPreviewChunk, bytecodeHandler, &bytecodeNames);
    assert(lingoPreview.find("on handlerName") != std::string::npos ||
           lingoPreview.find("-- Decompilation error:") != std::string::npos);
    assert(propertyInspectorTabNames().size() == 4);
    assert(propertyInspectorTabNames().front() == "Sprite");
    assert(propertyInspectorTabNames().back() == "Movie");
    assert(propertyInspectorSpriteLabels().size() == 12);
    assert(propertyInspectorSpriteLabels()[2] == "X (locH):");
    assert(propertyInspectorSpriteLabels().back() == "Editable:");
    const auto spritePropertiesText = spritePropertiesDialogText();
    assert(spritePropertiesText.title == "Sprite Properties");
    assert(spritePropertiesText.noMovieText == "Open a movie first.");
    assert(spritePropertiesText.selectSpriteText == "Select a score sprite or score cell first.");
    assert(spritePropertiesDialogHeading(7, "Ch 2") == "Frame 7, Ch 2");
    const auto spriteTweeningText = spriteTweeningDialogText();
    assert(spriteTweeningText.title == "Sprite Tweening");
    assert(spriteTweeningText.noMovieText == "Open a movie first.");
    assert(spriteTweeningText.selectSpriteText == "Select a score sprite or score cell first.");
    assert(spriteTweeningText.spanLabel == "Sprite span:");
    assert(spriteTweeningText.castMemberLabel == "Cast member:");
    assert(spriteTweeningText.channelLabel == "Channel:");
    assert(spriteTweeningText.tweenedPropertiesTitle == "Tweened Properties");
    assert(spriteTweeningText.settingsTitle == "Settings");
    assert(spriteTweeningText.curvatureLabel == "Curvature:");
    assert(spriteTweeningText.easeInLabel == "Ease in:");
    assert(spriteTweeningText.easeOutLabel == "Ease out:");
    assert(spriteTweeningText.unsetText == "-");
    assert(spriteTweeningText.pendingNoteText.find("Writing tween data back into the Director score is still pending") !=
           std::string_view::npos);
    const auto tweeningProperties = spriteTweeningPropertyLabels();
    assert(tweeningProperties.size() == 8);
    assert(tweeningProperties.front() == "Path");
    assert(tweeningProperties[5] == "Foreground color");
    assert(tweeningProperties.back() == "Ink");
    assert(spriteTweeningSpanText(2, 9) == "2-9");
    assert(spriteTweeningDetailsText("Details") ==
           "Details\n\nTweening controls are shown for Java UI parity. "
           "Writing tween data back into the Director score is still pending.");
    assert(spriteTweeningOpenedStatusText(7, "Ch 2") == "Opened sprite tweening for frame 7, Ch 2");
    assert(propertyInspectorMemberLabels().size() == 7);
    assert(propertyInspectorMemberLabels().front() == "Name:");
    const auto castMemberPropertiesText = castMemberPropertiesDialogText();
    assert(castMemberPropertiesText.title == "Cast Member Properties");
    assert(castMemberPropertiesText.selectMemberText == "Select a cast member first.");
    assert(castMemberPropertiesText.memberNotLoadedText == "Selected cast member is not loaded.");
    assert(castMemberPropertiesText.memberTitlePrefix == "Member ");
    assert(castMemberPropertiesText.registrationPointLabel == "Registration Point:");
    assert(castMemberPropertiesText.scriptLabel == "Script:");
    assert(castMemberPropertiesText.castLibraryLabel == "Cast Library:");
    assert(castMemberPropertiesText.unsetText == "-");
    assert(castMemberPropertiesDialogTitle("Backdrop", 12) == "Backdrop");
    assert(castMemberPropertiesDialogTitle("", 12) == "Member 12");
    assert(propertyInspectorMovieLabels().size() == 9);
    assert(propertyInspectorMovieLabels()[1] == "Stage Width:");
    const auto inspectorText = propertyInspectorText();
    assert(inspectorText.noSelectionText == "None");
    assert(inspectorText.memberPlaceholderText == "Select a cast member to view its properties");
    assert(inspectorText.moviePlaceholderText == "Open a movie to view its properties");
    assert(inspectorText.addBehaviorText == "Add");
    assert(inspectorText.removeBehaviorText == "Remove");
    assert(inspectorText.openScriptText == "Open Script");
    assert(inspectorText.openBehaviorScriptText == "Open Behavior Script");
    assert(inspectorText.behaviorScriptTitle == "Behavior Script");
    assert(inspectorText.addBehaviorTitle == "Add Behavior");
    assert(inspectorText.addBehaviorNoSpriteText == "Select a sprite cell in the Score before adding a behavior.");
    assert(inspectorText.addBehaviorPromptPrefix == "Behavior script id for channel ");
    assert(inspectorText.addBehaviorPromptMiddle == ", frame ");
    assert(inspectorText.behaviorPendingSuffix ==
           "Writing behavior attachments back into the Director score is still pending.");
    assert(inspectorText.removeBehaviorTitle == "Remove Behavior");
    assert(inspectorText.removeBehaviorNoSelectionText == "Select an attached behavior before removing it.");
    assert(propertyInspectorCastMemberHeading(3, 12) == "Cast 3, member 12");
    assert(propertyInspectorUnsetValueText() == "-");
    assert(propertyInspectorBehaviorPlaceholderText() == "(Select a sprite to see its behaviors)");
    assert((propertyInspectorBehaviorValues(ScoreIntervalRow{}) == std::vector<std::string>{
                "(Select a sprite to see its behaviors)"}));
    assert((propertyInspectorBehaviorValues(ScoreIntervalRow{.castMember = 7}) == std::vector<std::string>{
                "(No behaviors attached)"}));
    assert((propertyInspectorBehaviorValues(ScoreIntervalRow{.castMember = 7,
                                                             .memberName = "Door",
                                                             .scriptId = 42}) == std::vector<std::string>{
                "Behavior script #42 on Door"}));
    assert(propertyInspectorRuntimeBehaviorValue(3, 42, "behavior(member 7, castLib 1)", 2) ==
           "Runtime behavior #3 script #42 - behavior(member 7, castLib 1) (2 properties)");
    assert(propertyInspectorRuntimeBehaviorValue(4, 0, "", 1) == "Runtime behavior #4 (1 property)");
    assert(propertyInspectorPendingBehaviorValue(42) == "Pending behavior script #42 (session)");
    assert(propertyInspectorPendingBehaviorRemovalValue("Behavior script #42 on Door") ==
           "Pending removal: Behavior script #42 on Door");
    assert(propertyInspectorBehaviorScriptTooltipText(42) == "Open behavior script #42");
    assert(propertyInspectorRuntimeBehaviorScriptTooltipText(42) == "Open runtime behavior script #42");
    assert(propertyInspectorMissingBehaviorScriptText(42) ==
           "Behavior script #42 is referenced by the selected sprite, but no loaded script member currently exposes that id.");
    assert(propertyInspectorAddBehaviorPromptText(2, 7) == "Behavior script id for channel 2, frame 7:");
    assert(propertyInspectorPreparedAddBehaviorText(42, 2, 7) ==
           "Added a session behavior preview for script #42 for sprite channel 2 at frame 7.\n\n"
           "Writing behavior attachments back into the Director score is still pending.");
    assert(propertyInspectorPreparedAddBehaviorStatusText(42, 2) ==
           "Added session behavior script #42 for channel 2");
    assert(propertyInspectorPreparedRemoveBehaviorText("Behavior script #42 on Door", 2, 7) ==
           "Added a session removal preview for \"Behavior script #42 on Door\" from sprite channel 2 at frame 7.\n\n"
           "Writing behavior attachments back into the Director score is still pending.");
    assert(propertyInspectorPreparedRemoveBehaviorStatusText(2) ==
           "Added session behavior removal preview for channel 2");
    assert(propertyInspectorCanceledPendingBehaviorStatusText(2) == "Canceled session behavior preview for channel 2");
    assert((propertyInspectorSpriteValues(ScoreIntervalRow{
                .startFrame = 2,
                .endFrame = 4,
                .channel = 6,
                .castLib = 1,
                .castMember = 12,
                .hasSpriteData = true,
                .posX = 111,
                .posY = 222,
                .width = 32,
                .height = 24,
                .ink = 8,
                .blend = 75,
                .memberName = "Backdrop",
            }) == std::vector<std::string>{
                      "Ch 1", "Cast 1, member 12 - Backdrop", "111", "222", "32", "24",
                      "8", "75", "6", "true", "-", "-"}));
    assert((propertyInspectorSpriteValues(ScoreIntervalRow{.channel = 6}) == std::vector<std::string>{
                "Ch 1", "-", "-", "-", "-", "-", "-", "-", "6", "-", "-", "-"}));
    const CastMemberRow inspectorMember{
        .chunkId = 12,
        .type = "bitmap",
        .name = "Backdrop",
        .memberType = MemberType::Bitmap,
        .specificDataSize = 42,
    };
    assert((propertyInspectorMemberValues(inspectorMember, "Internal") == std::vector<std::string>{
                "Backdrop", "12", "Internal", "bitmap", "42 bytes", "-", "-"}));
    assert((propertyInspectorMemberValues(CastMemberRow{.chunkId = 3}, "Internal") == std::vector<std::string>{
                "(unnamed)", "3", "Internal", "-", "-", "-", "-"}));
    assert((propertyInspectorMovieValues(MovieSummary{.version = 1100,
                                                      .tempo = 15,
                                                      .stageWidth = 320,
                                                      .stageHeight = 200,
                                                      .frameCount = 24,
                                                      .castCount = 1,
                                                      .paletteCount = 2},
                                         "movie.dir") == std::vector<std::string>{
                                                        "movie.dir", "320", "200", "-", "2", "15", "24", "1", "-"}));
    assert(castWindowTypeFilterItems().size() == 11);
    assert(castWindowTypeFilterItems().front() == "All Types");
    assert(castWindowTypeFilterItems()[1] == "Bitmap");
    assert(castWindowTypeFilterItems().back() == "Transition");
    const auto castColumns = castWindowTableColumns();
    assert(castColumns.size() == 6);
    assert(castColumns.front() == "Preview");
    assert(castColumns[1] == "Chunk");
    assert(castColumns.back() == "Reg Point");
    const auto castText = castWindowText();
    assert(castText.loadCastText == "Load Cast...");
    assert(castText.loadCastTooltip == "Load a local file into the selected external cast slot");
    assert(castText.gridViewText == "Grid");
    assert(castText.gridViewTooltip == "Thumbnail preview mode");
    assert(castText.listViewText == "List");
    assert(castText.listViewTooltip == "List view");
    assert(castText.searchLabel == " Search: ");
    assert(castText.searchPlaceholder == "Search");
    assert(castText.typeLabel == " Type: ");
    assert(castText.emptySummaryText == "Cast members: 0");
    assert(castText.selectedMemberPlaceholderText == "Select a cast member");
    assert(castWindowOpenSelectedButtonText() == "Open Selected");
    assert(castText.openText == "Open");
    assert(castText.openInFieldText == "Open in Field");
    assert(castText.loadExternalCastTitle == "Load External Cast");
    assert(castText.loadExternalCastNoMovieText == "No movie loaded.");
    assert(castText.loadExternalCastSelectSlotText == "Select an external cast slot first.");
    assert(castText.loadExternalCastNotExternalText == "Selected cast is not an external cast.");
    assert(castText.loadExternalCastFileFilter ==
           "Director Cast Files (*.cst *.cct *.dir *.dxr *.dcr);;All Files (*)");
    assert(castText.loadExternalCastOpenFailedText == "Unable to open selected cast file.");
    assert(castText.loadExternalCastLoadFailedText == "Selected file could not be loaded as this cast.");
    assert(castText.exportMembersTitle == "Export Cast Members");
    assert(castText.exportMembersNoSelectionText == "Select one or more cast members to export.");
    assert(castText.exportSelectedDirectoryTitle == "Export Selected Cast Members");
    assert(castText.exportMemberTitle == "Export Cast Member");
    assert(castText.supportedExportFileFilter ==
           "Supported Export (*.png *.mp3 *.wav *.pal *.ls *.txt *.bin);;All Files (*)");
    assert(castText.exportAllSelectedDirectoryTitle == "Export All Selected");
    assert(castWindowLoadExternalCastDialogTitle("") == "Load External Cast");
    assert(castWindowLoadExternalCastDialogTitle("External.cst") == "Load External Cast: External.cst");
    assert(castWindowLoadedExternalCastStatus(3) == "Loaded external cast 3");
    assert(castWindowDefaultCastName() == "Internal");
    assert(castWindowCastLibraryLabel(1, "", true, false, true) == "Internal");
    assert(castWindowCastLibraryLabel(2, "   ", false, false, true) == "Cast 2");
    assert(castWindowCastLibraryLabel(3, "", false, true, false) == "Cast 3 (not loaded)");
    assert(castWindowCastLibraryLabel(3, "External Cast", false, true, false) == "External Cast (not loaded)");
    assert(castWindowCastLibraryLabel(2, "Artwork", false, false, true) == "Artwork");
    assert(castWindowMemberDisplayName("") == "(unnamed)");
    assert(castWindowMemberDisplayName("Backdrop") == "Backdrop");
    assert(castWindowMemberCountStatus(7, 12) == " 7 of 12 members");
    assert(castWindowReadyStatus() == " Ready");
    assert(castWindowNoMovieText() == "No movie loaded");
    assert(castWindowNoMembersText() == "No members");
    assert(castWindowNoMembersStatusText() == " No members");
    assert(castWindowExportActionText() == "Export...");
    assert(castWindowExportSelectedActionText() == "Export All Selected (Ctrl+Shift+E)");
    assert(castWindowSelectAllActionText() == "Select All (Ctrl+A)");
    assert(castWindowCopyNameActionText() == "Copy Name");
    assert(castWindowExportStartingStatus(1) == " Exporting 1 member");
    assert(castWindowExportStartingStatus(3) == " Exporting 3 selected members");
    assert(castMemberExportBaseName(CastMemberRow{.chunkId = 7, .type = "bitmap", .name = "Title Card!"}) ==
           "Title_Card_");
    assert(castMemberFallbackExportFileName(7) == "member_7.bin");
    assert(castMemberFallbackExportFileName(-1) == "member_0.bin");
    assert(castMemberExportFileNameWithExtension(CastMemberRow{.chunkId = 7, .type = "bitmap", .name = "Title Card!"},
                                                 "mp3") == "Title_Card_.mp3");
    assert(castMemberExportFileNameWithExtension(CastMemberRow{.chunkId = 7, .type = "bitmap", .name = "Title Card!"},
                                                 "") == "Title_Card_.bin");
    assert(castMemberExportFileName(CastMemberRow{
               .chunkId = 7, .type = "bitmap", .name = "Title Card!", .memberType = MemberType::Bitmap}) ==
           "Title_Card_.png");
    assert(castMemberExportFileName(CastMemberRow{.chunkId = 8, .type = "picture", .memberType = MemberType::Picture}) ==
           "picture_8.png");
    assert(castMemberExportFileName(CastMemberRow{.chunkId = 9, .type = "sound", .memberType = MemberType::Sound}) ==
           "sound_9.wav");
    assert(castMemberExportFileName(CastMemberRow{.chunkId = 10, .type = "palette", .memberType = MemberType::Palette}) ==
           "palette_10.pal");
    assert(castMemberExportFileName(CastMemberRow{.chunkId = 11, .type = "script", .memberType = MemberType::Script}) ==
           "script_11.ls");
    assert(castMemberExportFileName(CastMemberRow{.chunkId = 12, .type = "text", .memberType = MemberType::Text}) ==
           "text_12.txt");
    assert(castMemberExportDuplicateFileName(CastMemberRow{.chunkId = 7, .type = "bitmap", .name = "Title Card!"},
                                             2,
                                             "png") == "Title_Card__2.png");
    assert(castMemberExportDuplicateFileName(CastMemberRow{.chunkId = 7, .type = "bitmap", .name = "Title Card!"},
                                             0,
                                             "") == "Title_Card__1.bin");
    assert(castWindowExportedStatus(1) == " Exported 1 member");
    assert(castWindowExportedStatus(3) == " Exported 3 selected members");
    assert(castWindowExportFailedStatus(1) == " Failed to export member");
    assert(castWindowExportFailedStatus(3) == " Failed to export 3 selected members");
    assert(castThumbnailSize() == 48);
    const auto gridSpec = castWindowGridViewModeSpec();
    assert(!gridSpec.showDetailColumns);
    assert(gridSpec.rowHeight == 76);
    assert(gridSpec.previewColumnWidth == 72);
    assert(gridSpec.statusText == " Thumbnail grid mode");
    const auto listSpec = castWindowListViewModeSpec();
    assert(listSpec.showDetailColumns);
    assert(listSpec.rowHeight == 56);
    assert(listSpec.previewColumnWidth == 64);
    assert(listSpec.statusText == " List view mode");
    assert(castThumbnailTypeAbbreviation(MemberType::Bitmap) == "BMP");
    assert(castThumbnailTypeAbbreviation(MemberType::Picture) == "PIC");
    assert(castThumbnailTypeAbbreviation(MemberType::Script) == "SCR");
    assert(castThumbnailTypeAbbreviation(MemberType::Sound) == "SND");
    assert(castThumbnailTypeAbbreviation(MemberType::Palette) == "PAL");
    assert(castThumbnailTypeAbbreviation(MemberType::Unknown) == "?");
    assert(stageWindowDefaultWidth() == 640);
    assert(stageWindowDefaultHeight() == 480);
    assert(stageWindowNoMovieText() == "No movie loaded");
    assert(stageWindowSummaryText("movie.dir", 320, 200, 7) == "movie.dir\n320 x 200\nFrame 7");
    assert(stageWindowRenderedTooltip("movie.dir", 7) == "Rendered movie.dir frame 7");
    assert(stageWindowRenderedTooltip("", 1) == "Rendered movie frame 1");
    assert(stageWindowNetworkTaskStatusText("GET", "http://localhost/test.bin", 42) ==
           "Network GET: http://localhost/test.bin (42 bytes)");
    assert(stageWindowNetworkTaskFailedStatusText("POST", "http://localhost/post", "timeout") ==
           "Network POST failed: http://localhost/post (timeout)");
    assert(stageWindowLocalHttpRootStatusText("/tmp/htdocs") == "Local HTTP root: /tmp/htdocs");
    assert((stageWindowClampedPoint(-5, 999, 320, 200) == libreshockwave::editor::StagePoint{.x = 0, .y = 199}));
    assert((stageWindowClampedPoint(12, 34, 320, 200) == libreshockwave::editor::StagePoint{.x = 12, .y = 34}));
    assert(stageWindowDirectorKeyCodeFromBrowserCode(13) == 36);
    assert(stageWindowDirectorKeyCodeFromBrowserCode(65) == 0);
    assert(stageWindowDirectorKeyCodeFromBrowserCode(37) == 123);
    assert(scoreCellWidth() == 12);
    assert(scoreCellHeight() == 14);
    assert(scoreHeaderHeight() == 20);
    assert(scoreChannelHeaderWidth() == 100);
    const auto scoreColumns = scoreIntervalTableColumns();
    assert(scoreColumns.size() == 7);
    assert(scoreColumns.front() == "Start");
    assert(scoreColumns[2] == "Channel");
    assert(scoreColumns.back() == "Name");
    assert(scoreSpecialChannelNames().size() == 6);
    assert(scoreSpecialChannelNames().front() == "Tempo");
    assert(scoreSpecialChannelNames().back() == "Script");
    assert(scoreChannelName(0) == "Tempo");
    assert(scoreChannelName(5) == "Script");
    assert(scoreChannelName(6) == "Ch 1");
    assert(scoreChannelName(12) == "Ch 7");
    assert(scoreChannelName(-1) == "Ch 0");
    assert(scoreIntervalChannelDisplayText(6) == "6 (Ch 1)");
    assert(scoreOverlaySpriteLabel(6, 12, "") == "Ch 6, member 12");
    assert(scoreOverlaySpriteLabel(6, 12, "Backdrop") == "Ch 6: Backdrop");
    const auto channelLabels = scoreChannelLabels(8);
    assert(channelLabels.size() == 8);
    assert(channelLabels.front() == "Tempo");
    assert(channelLabels[5] == "Script");
    assert(channelLabels[6] == "Ch 1");
    assert(scoreChannelLabels(0).empty());
    assert(scoreInitialStatusText() == "Frame: 1 | Channel: -");
    assert(scoreNoDataStatusText() == "Score: No score data");
    assert(scoreLoadedStatusText(120, 48) == "120 frames, 48 channels");
    assert(scoreFrameStatusText(9) == "Frame: 9");
    assert(scoreCellStatusText(9, 0) == "Frame: 9 | Channel: Tempo");
    assert(scoreCellStatusText(9, 7) == "Frame: 9 | Channel: Ch 2");
    assert(scoreMarkersText({}) == "Markers: none");
    const std::array<libreshockwave::editor::ScoreMarker, 2> markers{{
        {.frame = 1, .label = "Intro"},
        {.frame = 12, .label = ""},
    }};
    assert(scoreMarkersText(markers) == "Markers:  1: Intro  12: (unnamed)");
    assert(scoreMemberTypeColorRgb(MemberType::Bitmap) == 0x99CCFF);
    assert(scoreMemberTypeColorRgb(MemberType::Text) == 0xFFFF99);
    assert(scoreMemberTypeColorRgb(MemberType::Shape) == 0x99FF99);
    assert(scoreMemberTypeColorRgb(MemberType::Script) == 0xCC99FF);
    assert(scoreMemberTypeColorRgb(MemberType::Sound) == 0xFFCC99);
    assert(scoreMemberTypeColorRgb(MemberType::FilmLoop) == 0x99FFFF);
    assert(scoreMemberTypeColorRgb(MemberType::Button) == 0xFFCCCC);
    assert(scoreMemberTypeColorRgb(MemberType::Font) == 0xCCCCCC);
    assert(scoreMemberTypeColorRgb(MemberType::Palette) == 0xCCFFCC);
    assert(scoreMemberTypeColorRgb(MemberType::Transition) == 0xCCCCFF);
    assert(scoreMemberTypeColorRgb(MemberType::Xtra) == 0xFF9999);
    assert(scoreMemberTypeColorRgb(MemberType::Unknown) == 0xC8C8C8);
    assert(scoreActiveFrameBackgroundRgb() == 0xFFE680);
    assert(scoreIntervalBackgroundRgb(ScoreIntervalRow{.castMember = -1}) == 0xFFFFFF);
    const ScoreIntervalRow scoreInterval{
        .startFrame = 2,
        .endFrame = 4,
        .channel = 6,
        .castLib = 1,
        .castMember = 3,
        .memberType = MemberType::Bitmap,
        .memberTypeName = "bitmap",
        .memberName = "Backdrop",
    };
    assert(scoreIntervalBackgroundRgb(scoreInterval) == 0x99CCFF);
    assert(!scoreIntervalContainsFrame(scoreInterval, 1));
    assert(scoreIntervalContainsFrame(scoreInterval, 2));
    assert(scoreIntervalContainsFrame(scoreInterval, 4));
    assert(!scoreIntervalContainsFrame(scoreInterval, 5));
    assert(scoreIntervalTooltip(scoreInterval) ==
           "Ch 1\nFrames: 2-4\nCast: 1, Member: 3\nBackdrop\nType: bitmap");
    assert(previewMemberHeader("TEXT", "Greeting", 12, true) == "=== TEXT: Greeting ===\n\nMember ID: 12\n\n");
    assert(previewMemberHeader("SCRIPT", "Behavior", 3, false) == "=== SCRIPT: Behavior ===\n\nMember ID: 3\n");
    assert(previewTextDataNotFoundText() == "[Text data not found]");
    assert(previewSoundDataNotFoundText() == "[Sound data not found]");
    assert(previewPaletteDataNotFoundText() == "[Palette data not found]");
    assert(previewNotUsedInScoreText() == "Not used in score");
    assert(previewPaletteInfoHeader(2) == "--- Palette Info ---\nColor Count: 2\n\n--- Colors ---");
    assert(previewPaletteColorLine(5, 0x1234AB) == "[  5] #1234AB (R: 18 G: 52 B:171)");
    assert(previewPaletteInfo({0x000000, 0xFFFFFF}) ==
           "--- Palette Info ---\n"
           "Color Count: 2\n"
           "\n"
           "--- Colors ---\n"
           "[  0] #000000 (R:  0 G:  0 B:  0)\n"
           "[  1] #FFFFFF (R:255 G:255 B:255)\n");
    assert(paletteExportText({0x000000, 0x1234AB}) == "JASC-PAL\n0100\n2\n0 0 0\n18 52 171\n");
    const auto emptySwatch = buildPaletteSwatch({});
    assert(emptySwatch.width == 1);
    assert(emptySwatch.height == 1);
    assert(emptySwatch.cells.empty());
    const std::vector<std::uint32_t> swatchColors{0x112233, 0x445566, 0x778899};
    const auto swatch = buildPaletteSwatch(swatchColors, 8, 2);
    assert(swatch.width == 16);
    assert(swatch.height == 16);
    assert(swatch.cells.size() == 3);
    assert(swatch.cells[0].x == 0);
    assert(swatch.cells[0].y == 0);
    assert(swatch.cells[0].size == 8);
    assert(swatch.cells[0].rgb == 0x112233);
    assert(swatch.cells[1].x == 8);
    assert(swatch.cells[1].y == 0);
    assert(swatch.cells[2].x == 0);
    assert(swatch.cells[2].y == 8);
    const std::vector<FrameAppearance> appearances{
        {.frame = 1, .channel = 6, .channelName = "Ch 1", .frameLabel = "start", .posX = 10, .posY = 20},
        {.frame = 2, .channel = 6, .channelName = "Ch 1", .posX = 12, .posY = 22},
        {.frame = 4, .channel = 3, .channelName = "Sound 1", .frameLabel = "cue", .posX = 0, .posY = 0},
    };
    assert(previewFrameAppearances({}) == "Not used in score");
    assert(previewFrameAppearances(appearances) == "Frames 1-2 (Ch 1), Frame 4 (Sound 1)");
    assert(previewScoreAppearances({}, true) == "Not used in score\n");
    assert(previewScoreAppearances(appearances, true) ==
           "Frames 1-2 (Ch 1), Frame 4 (Sound 1)\n"
           "\n"
           "Detailed appearances:\n"
           "  Frame 1, Ch 1 at (10, 20) [start]\n"
           "  Frame 2, Ch 1 at (12, 22)\n"
           "  Frame 4, Sound 1 at (0, 0) [cue]\n");
    assert(previewScoreAppearances(appearances, false) ==
           "Frames 1-2 (Ch 1), Frame 4 (Sound 1)\n"
           "\n"
           "Detailed appearances:\n"
           "  Frame 1, Ch 1 [start]\n"
           "  Frame 2, Ch 1\n"
           "  Frame 4, Sound 1 [cue]\n");
    const std::vector<FrameAppearance> manyRanges{
        {.frame = 1, .channel = 6, .channelName = "Ch 1"},
        {.frame = 3, .channel = 6, .channelName = "Ch 1"},
        {.frame = 5, .channel = 6, .channelName = "Ch 1"},
        {.frame = 7, .channel = 6, .channelName = "Ch 1"},
        {.frame = 9, .channel = 6, .channelName = "Ch 1"},
        {.frame = 11, .channel = 6, .channelName = "Ch 1"},
    };
    assert(previewFrameAppearances(manyRanges) ==
           "Frame 1 (Ch 1), Frame 3 (Ch 1), Frame 5 (Ch 1) ... and 3 more");
    auto channelData = ScoreChunk::ChannelData::empty();
    channelData.castLib = 1;
    channelData.castMember = 20;
    channelData.posX = 111;
    channelData.posY = 222;
    auto secondChannelData = channelData;
    secondChannelData.posX = 333;
    secondChannelData.posY = 444;
    auto ignoredChannelData = channelData;
    ignoredChannelData.castMember = 99;
    const std::vector<ScoreChunk::FrameChannelEntry> frameEntries{
        {FrameIndex(4), ChannelId(7), secondChannelData},
        {FrameIndex(2), ChannelId(6), channelData},
        {FrameIndex(3), ChannelId(8), ignoredChannelData},
    };
    const auto foundAppearances = buildFrameAppearancesFromScoreEntries(
        frameEntries,
        {{2, "label-from-java-index"}, {4, "later"}},
        500,
        [](int castLib, int castMember) -> std::optional<int> {
            if (castLib == 1 && castMember == 20) {
                return 500;
            }
            if (castLib == 1 && castMember == 99) {
                return 999;
            }
            return std::nullopt;
        });
    assert(foundAppearances.size() == 2);
    assert(foundAppearances[0].frame == 3);
    assert(foundAppearances[0].channel == 6);
    assert(foundAppearances[0].channelName == "Ch 1");
    assert(foundAppearances[0].frameLabel == "label-from-java-index");
    assert(foundAppearances[0].posX == 111);
    assert(foundAppearances[0].posY == 222);
    assert(foundAppearances[1].frame == 5);
    assert(foundAppearances[1].channel == 7);
    assert(foundAppearances[1].channelName == "Ch 2");
    assert(foundAppearances[1].frameLabel == "later");
    assert(buildFrameAppearancesFromScoreEntries(frameEntries, {}, 500, {}).empty());
    assert(registrationPointText(-4, 12) == "-4, 12");
    assert(editorPanelSpecs().size() == 14);
    assert(editorPanelSpecs().front().id == "stage");
    assert(editorPanelSpecs().front().visibleByDefault);
    assert(editorPanelSpecs().front().shortcut == "Ctrl+1");
    assert(findEditorPanelSpec("cast") != nullptr);
    assert(findEditorPanelSpec("cast")->title == "Cast");
    assert(findEditorPanelSpec("cast")->shortcut == "Ctrl+3");
    assert(findEditorPanelSpec("script")->shortcut == "Ctrl+0");
    assert(findEditorPanelSpec("message")->shortcut == "Ctrl+Shift+M");
    assert(findEditorPanelSpec("paint") != nullptr);
    assert(!findEditorPanelSpec("paint")->visibleByDefault);
    assert(findEditorPanelSpec("paint")->shortcut == "Ctrl+5");
    assert(findEditorPanelSpec("vector-shape")->shortcut.empty());
    assert(findEditorPanelSpec("color-palettes")->shortcut == "Ctrl+Alt+7");
    assert(findEditorPanelSpec("bytecode-debugger")->shortcut == "Ctrl+Shift+D");
    assert(findEditorPanelSpec("missing") == nullptr);
    const auto windowCommands = windowCommandSpecs();
    assert(windowCommands.size() == 1);
    assert(windowCommands[0].id == "reset-layout");
    assert(windowCommands[0].menuText == "Reset Layout");
    const auto shellText = mainWindowShellText();
    assert(shellText.defaultTitle == "LibreShockwave Editor - Director MX 2004");
    assert(shellText.titlePrefix == "LibreShockwave Editor - ");
    assert(shellText.centerPlaceholderText == "LibreShockwave Director Studio");
    assert(shellText.pendingPanelText == "Port pending");
    assert(shellText.layoutResetStatusText == "Layout reset to default");
    const auto summaryText = mainWindowSummaryText();
    assert(summaryText.noMovieOpenText == "No movie open");
    assert(summaryText.moviePlaceholderText == "Open a movie to view its properties");
    assert(summaryText.scorePlaceholderText == "No score loaded");
    assert(mainWindowTitleForMovie("") == "LibreShockwave Editor - Director MX 2004");
    assert(mainWindowTitleForMovie("movie.dir") == "LibreShockwave Editor - movie.dir");
    const MovieSummary mainWindowSummary{.version = 1100,
                                         .tempo = 15,
                                         .channelCount = 48,
                                         .frameCount = 120,
                                         .castMemberCount = 34,
                                         .castCount = 2,
                                         .paletteCount = 3,
                                         .externalCastCount = 1};
    assert(mainWindowMovieSummaryText(mainWindowSummary) == "Version 1100\nTempo 15\nChannels 48\nExternal casts 1");
    assert(mainWindowCastSummaryText(mainWindowSummary) == "Cast members: 34\nCasts: 2\nPalettes: 3");
    assert(mainWindowScoreSummaryText(mainWindowSummary) == "Frames: 120\nChannels: 48\nTempo: 15");
    const auto panelContext = panelContextCommandText();
    assert(panelContext.raisePanel == "Raise Panel");
    assert(panelContext.showPanel == "Show Panel");
    assert(panelContext.floatPanel == "Float");
    assert(panelContext.dockPanel == "Dock");
    assert(panelContext.closePanel == "Close Panel");
    assert(panelContext.resetLayout == "Reset Layout");
    const auto dockCommands = dockCommandSpecs();
    assert(dockCommands.size() == 5);
    assert(dockCommands[0].id == "dock-left");
    assert(dockCommands[0].menuText == "Dock Left");
    assert(dockCommands[0].area == libreshockwave::editor::DockArea::Left);
    assert(dockCommands[1].menuText == "Dock Right");
    assert(dockCommands[1].area == libreshockwave::editor::DockArea::Right);
    assert(dockCommands[2].menuText == "Dock Top");
    assert(dockCommands[2].area == libreshockwave::editor::DockArea::Top);
    assert(dockCommands[3].menuText == "Dock Bottom");
    assert(dockCommands[3].area == libreshockwave::editor::DockArea::Bottom);
    assert(dockCommands[4].id == "dock-center");
    assert(dockCommands[4].menuText == "Dock Center");
    assert(dockCommands[4].area == libreshockwave::editor::DockArea::Center);
    const auto moveDockCommands = moveDockCommandSpecs();
    assert(moveDockCommands.size() == 5);
    assert(moveDockCommands[0].id == "move-to-left");
    assert(moveDockCommands[0].menuText == "Move to Left");
    assert(moveDockCommands[0].area == libreshockwave::editor::DockArea::Left);
    assert(moveDockCommands[1].menuText == "Move to Right");
    assert(moveDockCommands[1].area == libreshockwave::editor::DockArea::Right);
    assert(moveDockCommands[2].menuText == "Move to Top");
    assert(moveDockCommands[2].area == libreshockwave::editor::DockArea::Top);
    assert(moveDockCommands[3].menuText == "Move to Bottom");
    assert(moveDockCommands[3].area == libreshockwave::editor::DockArea::Bottom);
    assert(moveDockCommands[4].id == "move-to-center");
    assert(moveDockCommands[4].menuText == "Move to Center");
    assert(moveDockCommands[4].area == libreshockwave::editor::DockArea::Center);
    const auto splitCommands = splitCommandSpecs();
    assert(splitCommands.size() == 4);
    assert(splitCommands[0].id == "split-left");
    assert(splitCommands[0].menuText == "Split Left");
    assert(splitCommands[0].direction == libreshockwave::editor::DockArea::Left);
    assert(splitCommands[1].menuText == "Split Right");
    assert(splitCommands[1].direction == libreshockwave::editor::DockArea::Right);
    assert(splitCommands[2].menuText == "Split Up");
    assert(splitCommands[2].direction == libreshockwave::editor::DockArea::Top);
    assert(splitCommands[3].menuText == "Split Down");
    assert(splitCommands[3].direction == libreshockwave::editor::DockArea::Bottom);

    const CastMemberRow bitmapRow{.chunkId = 5,
                                  .type = "Bitmap",
                                  .name = "Main Avatar",
                                  .memberType = MemberType::Bitmap,
                                  .scriptId = 0,
                                  .regPointX = 1,
                                  .regPointY = 2};
    assert(castMemberMatchesFilter(bitmapRow, ""));
    assert(castMemberMatchesFilter(bitmapRow, "avatar"));
    assert(castMemberMatchesFilter(bitmapRow, "BITMAP"));
    assert(!castMemberMatchesFilter(bitmapRow, "sound"));
    assert(castMemberMatchesTypeFilter(bitmapRow, "All Types"));
    assert(castMemberMatchesTypeFilter(bitmapRow, "Bitmap"));
    assert(!castMemberMatchesTypeFilter(bitmapRow, "Sound"));
    assert(editorPanelForMemberType(MemberType::Bitmap) == "paint");
    assert(editorPanelForMemberType(MemberType::Picture) == "paint");
    assert(editorPanelForMemberType(MemberType::Text) == "text");
    assert(editorPanelForMemberType(MemberType::RichText) == "text");
    assert(editorPanelForMemberType(MemberType::Button) == "text");
    assert(editorPanelForMemberType(MemberType::Script) == "script");
    assert(editorPanelForMemberType(MemberType::Sound) == "sound");
    assert(editorPanelForMemberType(MemberType::Shape) == "vector-shape");
    assert(editorPanelForMemberType(MemberType::Palette) == "color-palettes");
    assert(castMemberDetails(bitmapRow) == "#5  Bitmap  Main Avatar  reg 1, 2  opens paint");
    assert(castMemberPreviewDetails(bitmapRow, appearances, true) ==
           "#5  Bitmap  Main Avatar  reg 1, 2  opens paint\n"
           "\n"
           "Score appearances:\n"
           "Frames 1-2 (Ch 1), Frame 4 (Sound 1)\n"
           "\n"
           "Detailed appearances:\n"
           "  Frame 1, Ch 1 at (10, 20) [start]\n"
           "  Frame 2, Ch 1 at (12, 22)\n"
           "  Frame 4, Sound 1 at (0, 0) [cue]\n");
    const CastMemberRow textRow{.chunkId = 7,
                                .type = "Text",
                                .name = "Greeting",
                                .memberType = MemberType::Text,
                                .scriptId = 0,
                                .regPointX = 0,
                                .regPointY = 0};
    const TextChunk textChunk(nullptr,
                              ChunkId(700),
                              "Line 1\r\nLine 2\rLine 3",
                              {{0, 6, 3, 12, 0x0F, 0, 0, 0}});
    assert(textEditorContent(&textChunk) == "Line 1\nLine 2\nLine 3");
    assert(textEditorContent(nullptr) == "[Text data not found]");
    assert(textExportText(textChunk) == "Line 1\nLine 2\nLine 3");
    assert(textMemberPreviewDetails(textRow, &textChunk) ==
           "=== TEXT: Greeting ===\n"
           "\n"
           "Member ID: 7\n"
           "\n"
           "--- Text Content ---\n"
           "Line 1\n"
           "Line 2\n"
           "Line 3\n"
           "\n"
           "--- Formatting Runs ---\n"
           "  Offset 0: Font #3, Size 12, Style 0x0F\n");
    assert(textMemberPreviewDetails(textRow, nullptr) ==
           "=== TEXT: Greeting ===\n\nMember ID: 7\n\n[Text data not found]\n");
    const CastMemberRow soundRow{.chunkId = 8,
                                 .type = "Sound",
                                 .name = "Pop",
                                 .memberType = MemberType::Sound,
                                 .scriptId = 0,
                                 .regPointX = 0,
                                 .regPointY = 0};
    const SoundChunk soundChunk(nullptr, ChunkId(800), 22050, 2, 16, 1, {0, 1, 2, 3}, "raw_pcm");
    assert(soundMemberPreviewDetails(soundRow, &soundChunk, appearances) ==
           "=== SOUND: Pop ===\n"
           "\n"
           "Member ID: 8\n"
           "\n"
           "--- Audio Properties ---\n"
           "Codec: PCM (16-bit)\n"
           "Sample Rate: 22050 Hz\n"
           "Bits Per Sample: 16\n"
           "Channels: Mono\n"
           "Duration: 0.00 seconds\n"
           "Audio Data Size: 4 bytes\n"
           "\n"
           "--- Score Appearances ---\n"
           "Frames 1-2 (Ch 1), Frame 4 (Sound 1)\n"
           "\n"
           "Detailed appearances:\n"
           "  Frame 1, Ch 1 [start]\n"
           "  Frame 2, Ch 1\n"
           "  Frame 4, Sound 1 [cue]\n");
    const CastMemberRow paletteRow{.chunkId = 9,
                                   .type = "Palette",
                                   .name = "Warm",
                                   .memberType = MemberType::Palette,
                                   .scriptId = 0,
                                   .regPointX = 0,
                                   .regPointY = 0};
    const std::vector<std::uint32_t> paletteColors{0x112233};
    assert(paletteMemberPreviewDetails(paletteRow, &paletteColors) ==
           "=== PALETTE: Warm ===\n"
           "\n"
           "Member ID: 9\n"
           "\n"
           "--- Palette Info ---\n"
           "Color Count: 1\n"
           "\n"
           "--- Colors ---\n"
           "[  0] #112233 (R: 17 G: 34 B: 51)\n");
    assert(paletteMemberPreviewDetails(paletteRow, nullptr) ==
           "=== PALETTE: Warm ===\n\nMember ID: 9\n\n[Palette data not found]\n");
    assert(genericMemberPreviewDetails(bitmapRow, 3, {}) ==
           "=== BITMAP: Main Avatar ===\n"
           "\n"
           "Member ID: 5\n"
           "Type: Bitmap (1)\n"
           "\n"
           "Specific Data: 3 bytes\n"
           "\n"
           "--- Score Appearances ---\n"
           "Not used in score\n");
    const auto startText = startScreenText();
    assert(startText.windowTitle == "LibreShockwave Editor");
    assert(startText.appTitle == "LibreShockwave Editor");
    assert(startText.subtitle == "Director MX 2004");
    assert(startText.createNewMovieText == "Create New Movie");
    assert(startText.createNewMovieDialogTitle == "Create New Movie");
    assert(startText.createNewMoviePendingText.find("Writing new Director movie files is still pending") !=
           std::string_view::npos);
    assert(startText.openMovieText == "Open Movie...");
    assert(startText.emptyRecentProjectsText == "No recent projects. Open a movie to get started.");
    assert(startText.recentProjectsHeader == "Recent Projects:");
    assert(startText.openDirectorFileTitle == "Open Director File");
    assert(startText.directorFileFilter.find("*.dir") != std::string_view::npos);
    assert(startText.fileNotFoundTitle == "File Not Found");
    assert(startText.fileNotFoundMessagePrefix == "File not found:\n");
    assert(startScreenRecentProjectText("movie.dir", "/movies", true) == "movie.dir - /movies");
    assert(startScreenRecentProjectText("missing.dir", "/movies", false) == "missing.dir - /movies (missing)");
    const auto recentMenuText = recentProjectsMenuText();
    assert(recentMenuText.emptyText == "No Recent Projects");
    assert(recentMenuText.clearText == "Clear Recent Projects");
    assert(recentMenuText.missingWarningTitle == "Open Recent");
    assert(recentMenuText.missingWarningMessagePrefix == "Recent project no longer exists:\n");
    assert(recentMenuText.clearedStatusText == "Recent projects cleared");
    const auto externalText = externalParamsDialogText();
    assert(externalText.windowTitle == "External Parameters");
    assert(externalText.helpText == "Set key-value pairs accessible via externalParamValue() in Lingo scripts.");
    assert(externalText.addText == "Add");
    assert(externalText.removeText == "Remove");
    assert(externalText.habboPresetText == "Habbo Preset");
    assert(externalText.habboPresetTooltip == "Load default Habbo external parameters");
    const auto externalColumns = externalParamsTableColumns();
    assert(externalColumns.size() == 2);
    assert(externalColumns.front() == "Key");
    assert(externalColumns.back() == "Value");
    const auto habboPreset = habboExternalParamPreset();
    assert(habboPreset.size() == 5);
    assert(habboPreset.front().key == "sw1");
    assert(habboPreset.front().value.find("site.url=http://www.habbo.co.uk") != std::string::npos);
    assert(habboPreset.back().key == "sw5");
    assert(habboPreset.back().value.find("external_variables.txt") != std::string::npos);
    const std::array<libreshockwave::editor::ExternalParamRow, 3> externalParams{{
        {.key = "sw1", .value = "one"},
        {.key = "", .value = "ignored"},
        {.key = "sw2", .value = "two"},
    }};
    assert((externalParamsForRuntime(externalParams) == std::vector<std::pair<std::string, std::string>>{
        {"sw1", "one"},
        {"sw2", "two"},
    }));
    auto recent = recentProjectsWithAdded({"a.dir", "b.dir", "c.dir"}, "b.dir", 3);
    assert((recent == std::vector<std::string>{"b.dir", "a.dir", "c.dir"}));
    recent = recentProjectsWithAdded(recent, "d.dir", 3);
    assert((recent == std::vector<std::string>{"d.dir", "b.dir", "a.dir"}));

    DirectorFile emptyMovie(ByteOrder::BigEndian, false, 1100, ChunkType::RIFX);
    const auto summary = buildMovieSummary(emptyMovie);
    assert(summary.version == 1100);
    assert(summary.tempo == 15);
    assert(summary.channelCount == 120);
    assert(summary.frameCount == 1);
    assert(summary.castMemberCount == 0);
    assert(summary.scriptCount == 0);
    assert(summary.externalCastCount == 0);
    assert(buildCastMemberRows(emptyMovie).empty());
    assert(buildFrameAppearances(emptyMovie, 1).empty());
    assert(buildScoreIntervalRows(emptyMovie).empty());
    assert(buildScriptRows(emptyMovie).empty());

    return 0;
}
