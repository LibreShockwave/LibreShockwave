#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include <QLabel>
#include <QColor>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include "EditorContext.hpp"
#include "EditorModels.hpp"
#include "libreshockwave/player/debug/DebugStateListener.hpp"

class QAction;
class QAudioOutput;
class QButtonGroup;
class QCloseEvent;
class QComboBox;
class QListWidget;
class QDialog;
class QDockWidget;
class QLineEdit;
class QMediaPlayer;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QTableWidget;
class QTextEdit;
class QTemporaryFile;
class QTimer;
class QWidget;

namespace libreshockwave::player {
class Player;

namespace debug {
class DebugController;
}

}

namespace libreshockwave::chunks {
class CastMemberChunk;
}

namespace libreshockwave::editor {

class ScoreGridWidget;
class ScoreChannelHeaderWidget;
class ScoreMarkerWidget;

struct PendingScoreKeyframe {
    int channel = 0;
    int frame = 0;
};

struct PendingBehaviorChange {
    int channel = 0;
    int frame = 0;
    int scriptId = 0;
    QString behaviorName;
    bool removal = false;
};

class MainWindow final : public QMainWindow, public ::libreshockwave::player::debug::DebugStateListener {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openInitialPath(const QString& path);
    void showStartScreen();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void onPaused(const ::libreshockwave::player::debug::DebugSnapshot& snapshot) override;
    void onResumed() override;
    void onBreakpointsChanged() override;
    void onWatchExpressionsChanged() override;

private:
    void createMenus();
    void createToolbar();
    void createPanels();
    void createPanel(const EditorPanelSpec& spec, QWidget* content);
    void connectContext();
    void createNewMovie();
    void createNewCast();
    void saveMovie();
    void saveMovieAs();
    void saveAllMovies();
    void openFileDialog();
    void openPath(const QString& path);
    void refreshRecentProjectsMenu();
    void importMedia();
    void editGeneralPreferences();
    void editPreferencePanel(const QString& panelName);
    void editExternalParams();
    void showMoviePropertiesDialog();
    void showMovieCastsDialog();
    void showFrameChannelDialog(const QString& channelName);
    void showCastMemberPropertiesDialog();
    void showSpritePropertiesDialog();
    void showSpriteTweeningDialog();
    void showTextFontDialog();
    void showTextParagraphDialog();
    void insertKeyframe();
    void insertMarker();
    void removeCurrentFrame();
    void insertMediaElement(const QString& mediaType);
    void editSelectedSpriteFrames();
    void editSelectedEntireSprite();
    void showExchangeCastMembersDialog();
    void performFocusedEditAction(const char* actionName, const QString& statusText);
    void clearFocusedSelection();
    void findInFocusedWidget();
    void findAgainInFocusedWidget();
    void findSelectionInFocusedWidget();
    void replaceInFocusedWidget();
    bool findTextInFocusedWidget(const QString& term);
    bool replaceSelectedTextInFocusedWidget(const QString& replacement);
    int replaceAllInFocusedWidget(const QString& term, const QString& replacement);
    QString focusedSelectedText() const;
    void resetLayout();
    void dockPanelToArea(QDockWidget* dock, DockArea area);
    void splitPanelFromTabGroup(QDockWidget* dock, DockArea direction);
    void saveLayout();
    bool restoreSavedLayout();
    void applyDefaultLayout();
    void updateMovieViews();
    void updateFrameLabel(int frame);
    void updateScorePlaybackHead(int frame);
    void syncSelectionToPanels(const SelectionState& selection);
    void syncCastSelectionToTable(const SelectionState& selection);
    void syncScoreSelectionToViews(const SelectionState& selection);
    void setStageZoom(int percent);
    void updateStageCanvasSize();
    void setScoreKeyframesVisible(bool visible);
    void setScoreGridLinesVisible(bool visible);
    void setSpriteOverlayInfoVisible(bool visible);
    void setSpriteOverlayPathsVisible(bool visible);
    void setSpriteToolbarVisible(bool visible);
    void setStageGridSnapEnabled(bool enabled);
    void setStageGuidesVisible(bool visible);
    void setStageGuidesSnapEnabled(bool enabled);
    void showGridSettingsDialog();
    void refreshStageFrame();
    void setPaintZoom(double zoom);
    void fitPaintToView();
    void updatePaintPreview();
    void configureStagePlayerLocalHttpRoot();
    void syncExternalParamsToStagePlayer();
    void attachDebugControllerToStagePlayer();
    void loadSavedBreakpoints();
    void saveCurrentBreakpoints();
    void clearSavedBreakpoints();
    void triggerDebugCommand(std::string_view commandId);
    bool handleStageMouseEvent(QObject* watched, QEvent* event);
    bool handleStageKeyEvent(QObject* watched, QEvent* event);
    bool handlePaintMouseEvent(QObject* watched, QEvent* event);
    bool moveSelectedStageSprite(std::string_view toolName, int x, int y);
    bool nudgeSelectedStageSprite(std::string_view toolName,
                                  std::string_view direction,
                                  int deltaX,
                                  int deltaY,
                                  int pixels);
    bool applyVectorShapeTool(const QString& toolName);
    void updateWindowTitle();
    void advancePlaybackFrame();
    void updateScriptEditorPreview();
    void refreshBytecodeDebuggerScriptFilter();
    void updateBytecodeDebuggerPreview();
    void setDebugBytecodeListing(const QString& text);
    void selectDebugInstructionOffset(int offset);
    void showBytecodeHandlerDetails();
    void showBytecodeContextMenu(const QPoint& pos);
    void toggleSelectedBytecodeBreakpoint();
    void toggleSelectedBytecodeBreakpointEnabled();
    void navigateToDebugHandler(const QString& handlerName, bool showDetails);
    void showTraceHandlerDialog();
    void updatePropertyInspectorForSelection(const SelectionState& selection);
    void openSelectedBehaviorScript();
    void prepareAddBehaviorToSelectedSprite();
    void prepareRemoveSelectedBehavior();
    void loadSelectedExternalCast();
    void applyTextEditorChanges();
    void applyFieldEditorChanges();
    struct CastMemberRef {
        int castLib = 0;
        int memberNum = 0;
    };
    [[nodiscard]] std::shared_ptr<DirectorFile> sourceFileForCastLib(int castLib);
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> castMemberForCastLib(int castLib, int memberNum);
    bool exportCastMemberToFile(int castLib, int memberNum, const QString& path);
    int exportCastMembersToDirectory(const QList<CastMemberRef>& memberRefs, const QString& directory);
    QList<CastMemberRef> selectedCastMemberRefs() const;
    void exportSelectedCastMembers();
    void openSelectedCastMember();
    void showPanel(const QString& panelId);
    bool selectScriptEditorScriptId(int scriptId);
    void showDetailedStackWindow(bool visible);
    void updateDetailedStackFromSnapshot(const ::libreshockwave::player::debug::DebugSnapshot& snapshot);
    void markDetailedStackRunning();
    void refreshDebugObjectsTables();
    void refreshDebugWatchTable();
    void showDebugDatumDetails(QTableWidget* table, int row, int column);
    void addDebugWatch();
    void editSelectedDebugWatch();
    void removeSelectedDebugWatch();
    void clearDebugWatches();
    [[nodiscard]] int currentDebugScriptId() const;
    [[nodiscard]] QString currentDebugHandlerName() const;
    [[nodiscard]] std::optional<int> selectedDebugInstructionOffset() const;
    [[nodiscard]] QString selectedDebugCallTargetName() const;
    [[nodiscard]] QString decorateDebugHandlerPreview(const QString& preview, int scriptId, const QString& handlerName) const;
    [[nodiscard]] bool debugHandlerExists(const QString& handlerName) const;
    QDialog* createDetailedStackWindow();
    void loadCastMemberIntoPanel(const QString& panelId, const QString& details, int castLib, int memberNum);
    QWidget* makePanelContent(const QString& title, const QStringList& lines = {});
    QWidget* makeStagePanel();
    QWidget* makeScorePanel();
    QWidget* makeCastPanel();
    QWidget* makePropertyInspectorPanel();
    QWidget* makeScriptPanel();
    QWidget* makeMessagePanel();
    QWidget* makeToolPalettePanel();
    QWidget* makePaintPanel();
    QWidget* makeTextEditorPanel();
    QWidget* makeFieldEditorPanel();
    QWidget* makeVectorShapePanel();
    QWidget* makeSoundPanel();
    QWidget* makeColorPalettesPanel();
    QWidget* makeBytecodeDebuggerPanel();

    EditorContext context_;
    std::unique_ptr<::libreshockwave::player::Player> stagePlayer_;
    std::shared_ptr<::libreshockwave::player::debug::DebugController> debugController_;
    QMenu* recentProjectsMenu_ = nullptr;
    QLabel* frameLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* stageSummary_ = nullptr;
    QFrame* stageCanvasFrame_ = nullptr;
    QList<QAction*> stageZoomActions_;
    QAction* scoreKeyframesAction_ = nullptr;
    QAction* scoreGridLinesAction_ = nullptr;
    QAction* spriteOverlayInfoAction_ = nullptr;
    QAction* spriteOverlayPathsAction_ = nullptr;
    QAction* spriteToolbarAction_ = nullptr;
    QAction* stageGridSnapAction_ = nullptr;
    QAction* stageGuidesShowAction_ = nullptr;
    QAction* stageGuidesSnapAction_ = nullptr;
    int stageZoomPercent_ = 100;
    QString lastFindText_;
    QLabel* movieSummary_ = nullptr;
    QLabel* selectionSummary_ = nullptr;
    QLabel* selectedMemberDetails_ = nullptr;
    QList<QLineEdit*> propertySpriteFields_;
    QList<QLineEdit*> propertyMemberFields_;
    QList<QLineEdit*> propertyMovieFields_;
    QListWidget* behaviorList_ = nullptr;
    QPlainTextEdit* memberSummary_ = nullptr;
    QLabel* castSummary_ = nullptr;
    QLabel* scoreSummary_ = nullptr;
    QLabel* scoreStatusLabel_ = nullptr;
    QWidget* scoreMarkerRow_ = nullptr;
    ScoreMarkerWidget* scoreMarkers_ = nullptr;
    ScoreChannelHeaderWidget* scoreChannelHeader_ = nullptr;
    std::vector<ScoreMarker> pendingScoreMarkers_;
    std::vector<PendingScoreKeyframe> pendingScoreKeyframes_;
    std::vector<int> pendingRemovedFrames_;
    std::vector<PendingBehaviorChange> pendingBehaviorChanges_;
    QLabel* scriptSummary_ = nullptr;
    QMap<QString, QLabel*> panelLoadLabels_;
    QLineEdit* castSearch_ = nullptr;
    QComboBox* castSelector_ = nullptr;
    QComboBox* castTypeFilter_ = nullptr;
    QPushButton* castGridViewButton_ = nullptr;
    QPushButton* castListViewButton_ = nullptr;
    QPushButton* openCastMemberButton_ = nullptr;
    QListWidget* castGrid_ = nullptr;
    QTableWidget* castTable_ = nullptr;
    QLabel* castStatusLabel_ = nullptr;
    QHash<int, QPixmap> castThumbnailCache_;
    ScoreGridWidget* scoreGrid_ = nullptr;
    QTableWidget* scoreTable_ = nullptr;
    QTableWidget* scriptTable_ = nullptr;
    QPlainTextEdit* messageOutput_ = nullptr;
    QLineEdit* messageInput_ = nullptr;
    QPlainTextEdit* scriptPreview_ = nullptr;
    QComboBox* scriptCastSelector_ = nullptr;
    QComboBox* scriptSelector_ = nullptr;
    QComboBox* scriptHandlerSelector_ = nullptr;
    QPushButton* scriptLingoToggle_ = nullptr;
    QLabel* debugStatusLabel_ = nullptr;
    QLabel* debugHandlerLabel_ = nullptr;
    QTableWidget* debugStackTable_ = nullptr;
    QTableWidget* debugLocalsTable_ = nullptr;
    QTableWidget* debugGlobalsTable_ = nullptr;
    QTableWidget* debugWatchesTable_ = nullptr;
    QTableWidget* debugTimeoutsTable_ = nullptr;
    QTableWidget* debugObjectGlobalsTable_ = nullptr;
    QTableWidget* debugMoviePropertiesTable_ = nullptr;
    QLabel* detailedStackStatus_ = nullptr;
    QPlainTextEdit* detailedStackCallStackText_ = nullptr;
    QPlainTextEdit* detailedStackVmStackText_ = nullptr;
    QPlainTextEdit* detailedStackArgumentsText_ = nullptr;
    QPlainTextEdit* detailedStackReceiverText_ = nullptr;
    QLineEdit* debugScriptFilter_ = nullptr;
    QLineEdit* debugHandlerFilter_ = nullptr;
    QComboBox* debugScriptSelector_ = nullptr;
    QComboBox* debugHandlerSelector_ = nullptr;
    QPushButton* debugHandlerDetailsButton_ = nullptr;
    QPushButton* debugLingoToggle_ = nullptr;
    QListWidget* debugBytecodeList_ = nullptr;
    QStringList debugScriptLabels_;
    QList<int> debugScriptIds_;
    QList<QStringList> debugScriptHandlerPreviews_;
    QList<QStringList> debugScriptLingoHandlerPreviews_;
    QList<QStringList> debugScriptLiterals_;
    QList<QStringList> debugScriptProperties_;
    QList<QStringList> debugScriptGlobals_;
    QLabel* paintImageLabel_ = nullptr;
    QLabel* paintStatus_ = nullptr;
    QScrollArea* paintScrollArea_ = nullptr;
    QPixmap paintOriginalPixmap_;
    QImage paintOriginalImage_;
    QString paintBaseStatus_;
    int paintEditorCastLib_ = -1;
    int paintEditorMemberNum_ = -1;
    double paintZoom_ = 1.0;
    int paintBrushSize_ = 3;
    QColor paintDrawColor_ = Qt::black;
    bool paintAntialiasPreview_ = true;
    bool paintShowTransparencyGrid_ = true;
    bool paintDragActive_ = false;
    QPoint paintDragStart_;
    QString paintDragTool_;
    QPlainTextEdit* fieldText_ = nullptr;
    QLabel* fieldStatus_ = nullptr;
    int fieldEditorCastLib_ = -1;
    int fieldEditorMemberNum_ = -1;
    QTextEdit* textEditorText_ = nullptr;
    QLabel* textEditorStatus_ = nullptr;
    int textEditorCastLib_ = -1;
    int textEditorMemberNum_ = -1;
    QLabel* vectorShapeCanvasLabel_ = nullptr;
    QPlainTextEdit* vectorShapeDetails_ = nullptr;
    QLabel* vectorShapeStatus_ = nullptr;
    int vectorShapeEditorCastLib_ = -1;
    int vectorShapeEditorMemberNum_ = -1;
    bool vectorShapeLoaded_ = false;
    QPlainTextEdit* soundInfo_ = nullptr;
    QPushButton* soundPlayButton_ = nullptr;
    QPushButton* soundStopButton_ = nullptr;
    QProgressBar* soundProgress_ = nullptr;
    QLabel* soundTimeLabel_ = nullptr;
    QLabel* soundStatus_ = nullptr;
#ifdef LIBRESHOCKWAVE_HAVE_QT_MULTIMEDIA
    QMediaPlayer* soundPlayer_ = nullptr;
    QAudioOutput* soundAudioOutput_ = nullptr;
    std::unique_ptr<QTemporaryFile> soundTempFile_;
    double soundLoadedDuration_ = 0.0;
#endif
    QLabel* colorPalettePreviewLabel_ = nullptr;
    QPlainTextEdit* colorPaletteInfo_ = nullptr;
    QTimer* playbackTimer_ = nullptr;
    std::unique_ptr<QDialog> detailedStackWindow_;
    QButtonGroup* toolButtons_ = nullptr;
    QLabel* activeToolLabel_ = nullptr;
    int activeToolId_ = 0;
    QMap<QString, QDockWidget*> panels_;
    QMap<QString, QAction*> panelActions_;
};

} // namespace libreshockwave::editor
