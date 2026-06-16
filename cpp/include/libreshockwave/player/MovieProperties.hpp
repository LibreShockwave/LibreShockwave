#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/player/input/InputState.hpp"

namespace libreshockwave::player {

class MovieProperties {
public:
    using IntSupplier = std::function<int()>;
    using IntConsumer = std::function<void(int)>;
    using StringConsumer = std::function<void(const std::string&)>;
    using LabelResolver = std::function<int(const std::string&)>;
    using MarkerResolver = std::function<int(int)>;
    using NetPageHandler = std::function<void(const std::string& url, const std::string& target)>;
    using NetMovieHandler = std::function<int(const std::string& url)>;
    using XtraNamesSupplier = std::function<std::vector<std::string>()>;
    using DatumSupplier = std::function<lingo::Datum()>;

    explicit MovieProperties(DirectorFile* file = nullptr);

    void setFile(DirectorFile* file);
    void setInputState(input::InputState* inputState);
    void setEffectiveFrameSupplier(IntSupplier supplier);
    void setFrameCountSupplier(IntSupplier supplier);
    void setTempoSupplier(IntSupplier supplier);
    void setTempoSetter(IntConsumer setter);
    void setSoundEnabledSupplier(IntSupplier supplier);
    void setSoundEnabledSetter(IntConsumer setter);
    void setSoundLevelSupplier(IntSupplier supplier);
    void setSoundLevelSetter(IntConsumer setter);
    void setSoundKeepDeviceSupplier(IntSupplier supplier);
    void setSoundKeepDeviceSetter(IntConsumer setter);
    void setSoundMixMediaSupplier(IntSupplier supplier);
    void setSoundMixMediaSetter(IntConsumer setter);
    void setRandomSeedSupplier(IntSupplier supplier);
    void setRandomSeedSetter(IntConsumer setter);
    void setParamCountSupplier(IntSupplier supplier);
    void setCastLibCountSupplier(IntSupplier supplier);
    void setXtraNamesSupplier(XtraNamesSupplier supplier);
    void setStageBackgroundColorSupplier(IntSupplier supplier);
    void setStageBackgroundColorSetter(IntConsumer setter);
    void setStageImageSupplier(DatumSupplier supplier);
    void setGoToFrameHandler(IntConsumer handler);
    void setGoToLabelHandler(StringConsumer handler);
    void setFrameForLabelResolver(LabelResolver resolver);
    void setMarkerFrameResolver(MarkerResolver resolver);
    void setGotoNetPageHandler(NetPageHandler handler);
    void setGotoNetMovieHandler(NetMovieHandler handler);

    [[nodiscard]] lingo::Datum getMovieProp(std::string_view propName) const;
    [[nodiscard]] bool setMovieProp(std::string_view propName, const lingo::Datum& value);
    [[nodiscard]] lingo::Datum getStageProp(std::string_view propName) const;
    [[nodiscard]] bool setStageProp(std::string_view propName, const lingo::Datum& value);

    [[nodiscard]] char getItemDelimiter() const;
    void setItemDelimiter(char delimiter);
    void goToFrame(int frame) const;
    void goToLabel(const std::string& label) const;
    [[nodiscard]] int getFrameForLabel(const std::string& label) const;
    [[nodiscard]] int getMarkerFrame(int markerOffset) const;
    void gotoNetPage(const std::string& url, const std::string& target) const;
    [[nodiscard]] int gotoNetMovie(const std::string& url) const;

    [[nodiscard]] bool exitLock() const;
    [[nodiscard]] bool updateLock() const;
    [[nodiscard]] const std::string& itemDelimiterString() const;
    [[nodiscard]] int puppetTempo() const;
    void setPuppetTempo(int puppetTempo);
    [[nodiscard]] const lingo::Datum& actorList() const;
    [[nodiscard]] const lingo::Datum& alertHook() const;

private:
    [[nodiscard]] int effectiveFrame() const;
    [[nodiscard]] int frameCount() const;
    [[nodiscard]] int channelCount() const;
    [[nodiscard]] int tempo() const;
    void setTempo(int tempo);
    [[nodiscard]] bool soundEnabled() const;
    void setSoundEnabled(bool enabled);
    [[nodiscard]] int soundLevel() const;
    void setSoundLevel(int level);
    [[nodiscard]] bool soundKeepDevice() const;
    void setSoundKeepDevice(bool keepDevice);
    [[nodiscard]] bool soundMixMedia() const;
    void setSoundMixMedia(bool mixMedia);
    [[nodiscard]] int randomSeed() const;
    void setRandomSeed(int seed);
    [[nodiscard]] int stageWidth(int fallback = 0) const;
    [[nodiscard]] int stageHeight(int fallback = 0) const;
    [[nodiscard]] int colorDepth() const;
    [[nodiscard]] int stageBackgroundColor() const;
    void setStageBackgroundColor(int rgb);
    [[nodiscard]] std::string movieName() const;
    [[nodiscard]] std::string moviePath() const;
    [[nodiscard]] lingo::Datum xtraList() const;
    [[nodiscard]] std::vector<std::string> registeredXtraNames() const;
    [[nodiscard]] lingo::Datum dateTimeProp(std::string_view prop) const;
    [[nodiscard]] lingo::Datum elapsedProp(std::string_view prop) const;
    [[nodiscard]] static std::string lowerProp(std::string_view value);
    [[nodiscard]] static int colorToRgb(const lingo::Datum& value);

    DirectorFile* file_{nullptr};
    input::InputState* inputState_{nullptr};

    bool exitLock_{false};
    bool updateLock_{false};
    std::string itemDelimiter_{","};
    int puppetTempo_{0};
    bool traceScript_{false};
    std::string traceLogFile_;
    bool allowCustomCaching_{false};
    lingo::Datum mouseDownScript_{lingo::Datum::of(std::string())};
    lingo::Datum mouseUpScript_{lingo::Datum::of(std::string())};
    lingo::Datum keyDownScript_{lingo::Datum::of(std::string())};
    lingo::Datum keyUpScript_{lingo::Datum::of(std::string())};
    lingo::Datum timeoutScript_{lingo::Datum::of(std::string())};
    lingo::Datum alertHook_{lingo::Datum::voidValue()};
    lingo::Datum cursor_{lingo::Datum::of(-1)};
    int floatPrecision_{4};
    std::string stageTitle_;
    lingo::Datum actorList_{lingo::Datum::list()};
    int tempo_{15};
    bool tempoExplicit_{false};
    bool beepOn_{true};
    int buttonStyle_{0};
    bool centerStage_{false};
    bool checkBoxAccess_{false};
    int checkBoxType_{0};
    bool colorQD_{true};
    bool fixStageSize_{false};
    bool imageDirect_{false};
    bool pauseState_{false};
    bool soundEnabled_{true};
    int soundLevel_{7};
    bool soundKeepDevice_{true};
    bool soundMixMedia_{true};
    bool safePlayer_{true};
    int preLoadRAM_{0};
    bool switchColorDepth_{false};
    bool timeoutKeyDown_{true};
    int timeoutLength_{0};
    bool timeoutMouse_{true};
    bool timeoutPlay_{true};
    int randomSeed_{0};
    int stageBackgroundColor_{0};

    std::chrono::steady_clock::time_point startTime_;

    IntSupplier effectiveFrameSupplier_;
    IntSupplier frameCountSupplier_;
    IntSupplier tempoSupplier_;
    IntConsumer tempoSetter_;
    IntSupplier soundEnabledSupplier_;
    IntConsumer soundEnabledSetter_;
    IntSupplier soundLevelSupplier_;
    IntConsumer soundLevelSetter_;
    IntSupplier soundKeepDeviceSupplier_;
    IntConsumer soundKeepDeviceSetter_;
    IntSupplier soundMixMediaSupplier_;
    IntConsumer soundMixMediaSetter_;
    IntSupplier randomSeedSupplier_;
    IntConsumer randomSeedSetter_;
    IntSupplier paramCountSupplier_;
    IntSupplier castLibCountSupplier_;
    XtraNamesSupplier xtraNamesSupplier_;
    IntSupplier stageBackgroundColorSupplier_;
    IntConsumer stageBackgroundColorSetter_;
    DatumSupplier stageImageSupplier_;
    IntConsumer goToFrameHandler_;
    StringConsumer goToLabelHandler_;
    LabelResolver frameForLabelResolver_;
    MarkerResolver markerFrameResolver_;
    NetPageHandler gotoNetPageHandler_;
    NetMovieHandler gotoNetMovieHandler_;
};

} // namespace libreshockwave::player
