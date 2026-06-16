#include "libreshockwave/player/MovieProperties.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <utility>

#include "libreshockwave/chunks/ConfigChunk.hpp"
#include "libreshockwave/chunks/ScoreChunk.hpp"

namespace libreshockwave::player {
namespace {

std::tm localTimeNow() {
    const std::time_t now = std::time(nullptr);
    std::tm result{};
    if (const std::tm* local = std::localtime(&now)) {
        result = *local;
    }
    return result;
}

std::string monthName(int month) {
    static constexpr std::array<std::string_view, 12> months{
        "January",
        "February",
        "March",
        "April",
        "May",
        "June",
        "July",
        "August",
        "September",
        "October",
        "November",
        "December"
    };
    if (month < 0 || month >= static_cast<int>(months.size())) {
        return "";
    }
    return std::string(months[static_cast<std::size_t>(month)]);
}

std::string abbreviatedMonthName(int month) {
    static constexpr std::array<std::string_view, 12> months{
        "Jan",
        "Feb",
        "Mar",
        "Apr",
        "May",
        "Jun",
        "Jul",
        "Aug",
        "Sep",
        "Oct",
        "Nov",
        "Dec"
    };
    if (month < 0 || month >= static_cast<int>(months.size())) {
        return "";
    }
    return std::string(months[static_cast<std::size_t>(month)]);
}

std::string format12Hour(const std::tm& time, bool includeSeconds) {
    int hour = time.tm_hour % 12;
    if (hour == 0) {
        hour = 12;
    }
    std::ostringstream out;
    out << hour << ':' << std::setw(2) << std::setfill('0') << time.tm_min;
    if (includeSeconds) {
        out << ':' << std::setw(2) << std::setfill('0') << time.tm_sec;
    }
    out << (time.tm_hour < 12 ? " AM" : " PM");
    return out.str();
}

lingo::Datum stringDatum(std::string value) {
    return lingo::Datum::of(std::move(value));
}

} // namespace

MovieProperties::MovieProperties(DirectorFile* file)
    : file_(file),
      startTime_(std::chrono::steady_clock::now()) {}

void MovieProperties::setFile(DirectorFile* file) {
    file_ = file;
}

void MovieProperties::setInputState(input::InputState* inputState) {
    inputState_ = inputState;
}

void MovieProperties::setEffectiveFrameSupplier(IntSupplier supplier) {
    effectiveFrameSupplier_ = std::move(supplier);
}

void MovieProperties::setFrameCountSupplier(IntSupplier supplier) {
    frameCountSupplier_ = std::move(supplier);
}

void MovieProperties::setTempoSupplier(IntSupplier supplier) {
    tempoSupplier_ = std::move(supplier);
}

void MovieProperties::setTempoSetter(IntConsumer setter) {
    tempoSetter_ = std::move(setter);
}

void MovieProperties::setSoundEnabledSupplier(IntSupplier supplier) {
    soundEnabledSupplier_ = std::move(supplier);
}

void MovieProperties::setSoundEnabledSetter(IntConsumer setter) {
    soundEnabledSetter_ = std::move(setter);
}

void MovieProperties::setSoundLevelSupplier(IntSupplier supplier) {
    soundLevelSupplier_ = std::move(supplier);
}

void MovieProperties::setSoundLevelSetter(IntConsumer setter) {
    soundLevelSetter_ = std::move(setter);
}

void MovieProperties::setSoundKeepDeviceSupplier(IntSupplier supplier) {
    soundKeepDeviceSupplier_ = std::move(supplier);
}

void MovieProperties::setSoundKeepDeviceSetter(IntConsumer setter) {
    soundKeepDeviceSetter_ = std::move(setter);
}

void MovieProperties::setSoundMixMediaSupplier(IntSupplier supplier) {
    soundMixMediaSupplier_ = std::move(supplier);
}

void MovieProperties::setSoundMixMediaSetter(IntConsumer setter) {
    soundMixMediaSetter_ = std::move(setter);
}

void MovieProperties::setRandomSeedSupplier(IntSupplier supplier) {
    randomSeedSupplier_ = std::move(supplier);
}

void MovieProperties::setRandomSeedSetter(IntConsumer setter) {
    randomSeedSetter_ = std::move(setter);
}

void MovieProperties::setParamCountSupplier(IntSupplier supplier) {
    paramCountSupplier_ = std::move(supplier);
}

void MovieProperties::setCastLibCountSupplier(IntSupplier supplier) {
    castLibCountSupplier_ = std::move(supplier);
}

void MovieProperties::setXtraNamesSupplier(XtraNamesSupplier supplier) {
    xtraNamesSupplier_ = std::move(supplier);
}

void MovieProperties::setStageBackgroundColorSupplier(IntSupplier supplier) {
    stageBackgroundColorSupplier_ = std::move(supplier);
}

void MovieProperties::setStageBackgroundColorSetter(IntConsumer setter) {
    stageBackgroundColorSetter_ = std::move(setter);
}

void MovieProperties::setStageImageSupplier(DatumSupplier supplier) {
    stageImageSupplier_ = std::move(supplier);
}

void MovieProperties::setGoToFrameHandler(IntConsumer handler) {
    goToFrameHandler_ = std::move(handler);
}

void MovieProperties::setGoToLabelHandler(StringConsumer handler) {
    goToLabelHandler_ = std::move(handler);
}

void MovieProperties::setFrameForLabelResolver(LabelResolver resolver) {
    frameForLabelResolver_ = std::move(resolver);
}

void MovieProperties::setMarkerFrameResolver(MarkerResolver resolver) {
    markerFrameResolver_ = std::move(resolver);
}

void MovieProperties::setGotoNetPageHandler(NetPageHandler handler) {
    gotoNetPageHandler_ = std::move(handler);
}

void MovieProperties::setGotoNetMovieHandler(NetMovieHandler handler) {
    gotoNetMovieHandler_ = std::move(handler);
}

lingo::Datum MovieProperties::getMovieProp(std::string_view propName) const {
    const std::string prop = lowerProp(propName);

    if (prop == "return") return stringDatum("\r");
    if (prop == "void") return lingo::Datum::voidValue();
    if (prop == "true") return lingo::Datum::TRUE;
    if (prop == "false") return lingo::Datum::FALSE;

    if (prop == "frame") return lingo::Datum::of(effectiveFrame());
    if (prop == "lastframe") return lingo::Datum::of(frameCount());
    if (prop == "lastchannel") return lingo::Datum::of(channelCount());
    if (prop == "stageright") return lingo::Datum::of(stageWidth());
    if (prop == "stageleft") return lingo::Datum::of(0);
    if (prop == "stagetop") return lingo::Datum::of(0);
    if (prop == "stagebottom") return lingo::Datum::of(stageHeight());
    if (prop == "name" || prop == "moviename") return stringDatum(movieName());
    if (prop == "moviepath") return stringDatum(moviePath());
    if (prop == "path") return stringDatum(file_ != nullptr ? file_->basePath() : "");
    if (prop == "platform") return stringDatum("Windows,32");
    if (prop == "runmode") return stringDatum("Plugin");
    if (prop == "productversion") return stringDatum("10.1");
    if (prop == "environment") return stringDatum("Java");
    if (prop == "date" || prop == "short date" || prop == "abbr date" || prop == "long date" ||
        prop == "time" || prop == "short time" || prop == "abbr time" || prop == "long time") {
        return dateTimeProp(prop);
    }
    if (prop == "timer" || prop == "ticks" || prop == "milliseconds") {
        return elapsedProp(prop);
    }
    if (prop == "exitlock") return lingo::Datum::of(exitLock_ ? 1 : 0);
    if (prop == "updatelock") return lingo::Datum::of(updateLock_ ? 1 : 0);
    if (prop == "itemdelimiter") return stringDatum(itemDelimiter_);
    if (prop == "puppettempo") return lingo::Datum::of(puppetTempo_);
    if (prop == "tracescript") return lingo::Datum::of(traceScript_ ? 1 : 0);
    if (prop == "tracelogfile") return stringDatum(traceLogFile_);
    if (prop == "allowcustomcaching") return lingo::Datum::of(allowCustomCaching_ ? 1 : 0);
    if (prop == "mousedownscript") return mouseDownScript_;
    if (prop == "mouseupscript") return mouseUpScript_;
    if (prop == "keydownscript") return keyDownScript_;
    if (prop == "keyupscript") return keyUpScript_;
    if (prop == "timeoutscript") return timeoutScript_;
    if (prop == "alerthook") return alertHook_;
    if (prop == "cursor") return cursor_;
    if (prop == "floatprecision") return lingo::Datum::of(floatPrecision_);
    if (prop == "beepon") return lingo::Datum::of(beepOn_ ? 1 : 0);
    if (prop == "buttonstyle") return lingo::Datum::of(buttonStyle_);
    if (prop == "centerstage") return lingo::Datum::of(centerStage_ ? 1 : 0);
    if (prop == "checkboxaccess") return lingo::Datum::of(checkBoxAccess_ ? 1 : 0);
    if (prop == "checkboxtype") return lingo::Datum::of(checkBoxType_);
    if (prop == "colorqd") return lingo::Datum::of(colorQD_ ? 1 : 0);
    if (prop == "fixstagesize") return lingo::Datum::of(fixStageSize_ ? 1 : 0);
    if (prop == "imagedirect") return lingo::Datum::of(imageDirect_ ? 1 : 0);
    if (prop == "lastclick") return lingo::Datum::of(inputState_ != nullptr ? inputState_->lastClickTicks() : 0);
    if (prop == "lastevent") return lingo::Datum::of(inputState_ != nullptr ? inputState_->lastEventTicks() : 0);
    if (prop == "lastkey") return lingo::Datum::of(inputState_ != nullptr ? inputState_->lastKeyTicks() : 0);
    if (prop == "lastroll") return lingo::Datum::of(inputState_ != nullptr ? inputState_->lastRollTicks() : 0);
    if (prop == "soundenabled") return lingo::Datum::of(soundEnabled() ? 1 : 0);
    if (prop == "soundlevel") return lingo::Datum::of(soundLevel());
    if (prop == "soundkeepdevice") return lingo::Datum::of(soundKeepDevice() ? 1 : 0);
    if (prop == "soundmixmedia") return lingo::Datum::of(soundMixMedia() ? 1 : 0);
    if (prop == "multisound") return lingo::Datum::TRUE;
    if (prop == "netpresent") return lingo::Datum::TRUE;
    if (prop == "pausestate") return lingo::Datum::of(pauseState_ ? 1 : 0);
    if (prop == "safeplayer") return lingo::Datum::of(safePlayer_ ? 1 : 0);
    if (prop == "preloadram") return lingo::Datum::of(preLoadRAM_);
    if (prop == "quicktimepresent" || prop == "videoforwindowspresent") return lingo::Datum::FALSE;
    if (prop == "stagecolor") return lingo::Datum::of(stageBackgroundColor());
    if (prop == "switchcolordepth") return lingo::Datum::of(switchColorDepth_ ? 1 : 0);
    if (prop == "timeoutlapsed") return lingo::Datum::of(inputState_ != nullptr ? inputState_->lastEventTicks() : 0);
    if (prop == "timeoutkeydown") return lingo::Datum::of(timeoutKeyDown_ ? 1 : 0);
    if (prop == "timeoutlength") return lingo::Datum::of(timeoutLength_);
    if (prop == "timeoutmouse") return lingo::Datum::of(timeoutMouse_ ? 1 : 0);
    if (prop == "timeoutplay") return lingo::Datum::of(timeoutPlay_ ? 1 : 0);
    if (prop == "randomseed") return lingo::Datum::of(randomSeed());
    if (prop == "actorlist") return actorList_;
    if (prop == "framerate" || prop == "tempo" || prop == "frametempo") return lingo::Datum::of(tempo());
    if (prop == "mousedown") return lingo::Datum::of(inputState_ != nullptr && inputState_->isMouseDown() ? 1 : 0);
    if (prop == "mouseup") return lingo::Datum::of(inputState_ == nullptr || !inputState_->isMouseDown() ? 1 : 0);
    if (prop == "mouseh") return lingo::Datum::of(inputState_ != nullptr ? inputState_->mouseH() : 0);
    if (prop == "mousev") return lingo::Datum::of(inputState_ != nullptr ? inputState_->mouseV() : 0);
    if (prop == "clickon") return lingo::Datum::of(inputState_ != nullptr ? inputState_->clickOnSprite() : 0);
    if (prop == "clickloc") {
        return inputState_ != nullptr ? lingo::Datum::intPoint(inputState_->clickLocH(), inputState_->clickLocV())
                                      : lingo::Datum::intPoint(0, 0);
    }
    if (prop == "mouseloc") {
        return inputState_ != nullptr ? lingo::Datum::intPoint(inputState_->mouseH(), inputState_->mouseV())
                                      : lingo::Datum::intPoint(0, 0);
    }
    if (prop == "rightmousedown") return lingo::Datum::of(inputState_ != nullptr && inputState_->isRightMouseDown() ? 1 : 0);
    if (prop == "doubleclick") return lingo::Datum::of(inputState_ != nullptr && inputState_->isDoubleClick() ? 1 : 0);
    if (prop == "rollover") return lingo::Datum::of(inputState_ != nullptr ? inputState_->rolloverSprite() : 0);
    if (prop == "key") return stringDatum(inputState_ != nullptr ? inputState_->lastKey() : "");
    if (prop == "keycode" || prop == "keypressed") return lingo::Datum::of(inputState_ != nullptr ? inputState_->lastKeyCode() : 0);
    if (prop == "shiftdown") return lingo::Datum::of(inputState_ != nullptr && inputState_->isShiftDown() ? 1 : 0);
    if (prop == "optiondown" || prop == "altdown") return lingo::Datum::of(inputState_ != nullptr && inputState_->isAltDown() ? 1 : 0);
    if (prop == "commanddown" || prop == "controldown") return lingo::Datum::of(inputState_ != nullptr && inputState_->isControlDown() ? 1 : 0);
    if (prop == "keyboardfocussprite") return lingo::Datum::of(inputState_ != nullptr ? inputState_->keyboardFocusSprite() : 0);
    if (prop == "selstart") return lingo::Datum::of(inputState_ != nullptr ? inputState_->selStart() : 0);
    if (prop == "selend") return lingo::Datum::of(inputState_ != nullptr ? inputState_->selEnd() : 0);
    if (prop == "colordepth") return lingo::Datum::of(colorDepth());
    if (prop == "fullcolorpermit") return lingo::Datum::TRUE;
    if (prop == "perframehook") return lingo::Datum::voidValue();
    if (prop == "number of castmembers") return lingo::Datum::of(file_ != nullptr ? static_cast<int>(file_->castMembers().size()) : 0);
    if (prop == "number of menus") return lingo::Datum::of(0);
    if (prop == "number of castlibs") {
        if (castLibCountSupplier_) {
            return lingo::Datum::of(castLibCountSupplier_());
        }
        return lingo::Datum::of(file_ != nullptr ? static_cast<int>(file_->casts().size()) : 0);
    }
    if (prop == "number of xtras") return lingo::Datum::of(static_cast<int>(registeredXtraNames().size()));
    if (prop == "xtralist") return xtraList();
    if (prop == "activewindow" || prop == "stage") return lingo::Datum::stageRef();
    if (prop == "emptystring") return stringDatum("");
    if (prop == "pi") return lingo::Datum::of(std::numbers::pi);
    if (prop == "enter") return stringDatum("\n");
    if (prop == "tab") return stringDatum("\t");
    if (prop == "quote") return stringDatum("\"");
    if (prop == "backspace") return stringDatum("\b");
    if (prop == "space") return stringDatum(" ");
    if (prop == "paramcount") return lingo::Datum::of(paramCountSupplier_ ? paramCountSupplier_() : 0);
    return lingo::Datum::voidValue();
}

bool MovieProperties::setMovieProp(std::string_view propName, const lingo::Datum& value) {
    const std::string prop = lowerProp(propName);
    if (prop == "exitlock") {
        exitLock_ = value.boolValue();
        return true;
    }
    if (prop == "updatelock") {
        updateLock_ = value.boolValue();
        return true;
    }
    if (prop == "itemdelimiter") {
        const std::string valueString = value.stringValue();
        itemDelimiter_ = valueString.empty() ? "," : valueString.substr(0, 1);
        return true;
    }
    if (prop == "puppettempo") {
        setPuppetTempo(value.intValue());
        return true;
    }
    if (prop == "tracescript") {
        traceScript_ = value.boolValue();
        return true;
    }
    if (prop == "tracelogfile") {
        traceLogFile_ = value.stringValue();
        return true;
    }
    if (prop == "allowcustomcaching") {
        allowCustomCaching_ = value.boolValue();
        return true;
    }
    if (prop == "mousedownscript") {
        mouseDownScript_ = value;
        return true;
    }
    if (prop == "mouseupscript") {
        mouseUpScript_ = value;
        return true;
    }
    if (prop == "keydownscript") {
        keyDownScript_ = value;
        return true;
    }
    if (prop == "keyupscript") {
        keyUpScript_ = value;
        return true;
    }
    if (prop == "timeoutscript") {
        timeoutScript_ = value;
        return true;
    }
    if (prop == "actorlist") {
        actorList_ = value;
        return true;
    }
    if (prop == "tempo" || prop == "framerate" || prop == "frametempo") {
        setTempo(value.intValue());
        return true;
    }
    if (prop == "keyboardfocussprite") {
        if (inputState_ != nullptr) {
            inputState_->setKeyboardFocusSprite(value.intValue());
        }
        return true;
    }
    if (prop == "alerthook") {
        alertHook_ = value;
        return true;
    }
    if (prop == "cursor") {
        cursor_ = value;
        return true;
    }
    if (prop == "floatprecision") {
        floatPrecision_ = value.intValue();
        return true;
    }
    if (prop == "beepon") {
        beepOn_ = value.boolValue();
        return true;
    }
    if (prop == "buttonstyle") {
        buttonStyle_ = value.intValue();
        return true;
    }
    if (prop == "centerstage") {
        centerStage_ = value.boolValue();
        return true;
    }
    if (prop == "checkboxaccess") {
        checkBoxAccess_ = value.boolValue();
        return true;
    }
    if (prop == "checkboxtype") {
        checkBoxType_ = value.intValue();
        return true;
    }
    if (prop == "colorqd") {
        colorQD_ = value.boolValue();
        return true;
    }
    if (prop == "fixstagesize") {
        fixStageSize_ = value.boolValue();
        return true;
    }
    if (prop == "imagedirect") {
        imageDirect_ = value.boolValue();
        return true;
    }
    if (prop == "soundenabled") {
        setSoundEnabled(value.boolValue());
        return true;
    }
    if (prop == "soundlevel") {
        setSoundLevel(value.intValue());
        return true;
    }
    if (prop == "soundkeepdevice") {
        setSoundKeepDevice(value.boolValue());
        return true;
    }
    if (prop == "soundmixmedia") {
        setSoundMixMedia(value.boolValue());
        return true;
    }
    if (prop == "pausestate") {
        pauseState_ = value.boolValue();
        return true;
    }
    if (prop == "safeplayer") {
        if (value.boolValue()) {
            safePlayer_ = true;
        }
        return true;
    }
    if (prop == "preloadram") {
        preLoadRAM_ = std::max(0, value.intValue());
        return true;
    }
    if (prop == "stagecolor") {
        if (!value.isVoid()) {
            setStageBackgroundColor(colorToRgb(value));
        }
        return true;
    }
    if (prop == "switchcolordepth") {
        switchColorDepth_ = value.boolValue();
        return true;
    }
    if (prop == "timeoutkeydown") {
        timeoutKeyDown_ = value.boolValue();
        return true;
    }
    if (prop == "timeoutlength") {
        timeoutLength_ = std::max(0, value.intValue());
        return true;
    }
    if (prop == "timeoutmouse") {
        timeoutMouse_ = value.boolValue();
        return true;
    }
    if (prop == "timeoutplay") {
        timeoutPlay_ = value.boolValue();
        return true;
    }
    if (prop == "randomseed") {
        setRandomSeed(value.intValue());
        return true;
    }
    if (prop == "selstart") {
        if (inputState_ != nullptr) {
            inputState_->setSelStart(value.intValue());
        }
        return true;
    }
    if (prop == "selend") {
        if (inputState_ != nullptr) {
            inputState_->setSelEnd(value.intValue());
        }
        return true;
    }
    if (prop == "debugplaybackenabled") {
        return true;
    }
    return false;
}

lingo::Datum MovieProperties::getStageProp(std::string_view propName) const {
    const std::string prop = lowerProp(propName);
    if (prop == "rect" || prop == "sourcerect" || prop == "drawrect") {
        return lingo::Datum::intRect(0, 0, stageWidth(640), stageHeight(480));
    }
    if (prop == "title") return stringDatum(stageTitle_);
    if (prop == "name") return stringDatum("stage");
    if (prop == "visible") return lingo::Datum::TRUE;
    if (prop == "bgcolor") return lingo::Datum::of(stageBackgroundColor());
    if (prop == "image") return stageImageSupplier_ ? stageImageSupplier_() : lingo::Datum::voidValue();
    return getMovieProp(propName);
}

bool MovieProperties::setStageProp(std::string_view propName, const lingo::Datum& value) {
    const std::string prop = lowerProp(propName);
    if (prop == "title") {
        stageTitle_ = value.stringValue();
        return true;
    }
    if (prop == "visible") {
        return true;
    }
    if (prop == "bgcolor") {
        if (!value.isVoid()) {
            setStageBackgroundColor(colorToRgb(value));
        }
        return true;
    }
    return setMovieProp(propName, value);
}

char MovieProperties::getItemDelimiter() const {
    return itemDelimiter_.empty() ? ',' : itemDelimiter_.front();
}

void MovieProperties::setItemDelimiter(char delimiter) {
    itemDelimiter_ = std::string(1, delimiter);
}

void MovieProperties::goToFrame(int frame) const {
    if (goToFrameHandler_) {
        goToFrameHandler_(frame);
    }
}

void MovieProperties::goToLabel(const std::string& label) const {
    if (goToLabelHandler_) {
        goToLabelHandler_(label);
    }
}

int MovieProperties::getFrameForLabel(const std::string& label) const {
    return frameForLabelResolver_ ? frameForLabelResolver_(label) : 0;
}

int MovieProperties::getMarkerFrame(int markerOffset) const {
    return markerFrameResolver_ ? markerFrameResolver_(markerOffset) : 0;
}

void MovieProperties::gotoNetPage(const std::string& url, const std::string& target) const {
    if (gotoNetPageHandler_) {
        gotoNetPageHandler_(url, target);
    }
}

int MovieProperties::gotoNetMovie(const std::string& url) const {
    return gotoNetMovieHandler_ ? gotoNetMovieHandler_(url) : -1;
}

bool MovieProperties::exitLock() const {
    return exitLock_;
}

bool MovieProperties::updateLock() const {
    return updateLock_;
}

const std::string& MovieProperties::itemDelimiterString() const {
    return itemDelimiter_;
}

int MovieProperties::puppetTempo() const {
    return puppetTempo_;
}

void MovieProperties::setPuppetTempo(int puppetTempo) {
    puppetTempo_ = std::max(0, puppetTempo);
    if (inputState_ != nullptr) {
        inputState_->setCaretBlinkRate(tempo());
    }
}

const lingo::Datum& MovieProperties::actorList() const {
    return actorList_;
}

const lingo::Datum& MovieProperties::alertHook() const {
    return alertHook_;
}

int MovieProperties::effectiveFrame() const {
    return effectiveFrameSupplier_ ? effectiveFrameSupplier_() : 0;
}

int MovieProperties::frameCount() const {
    if (frameCountSupplier_) {
        return frameCountSupplier_();
    }
    return file_ != nullptr && file_->scoreChunk() ? file_->scoreChunk()->getFrameCount() : 0;
}

int MovieProperties::channelCount() const {
    return file_ != nullptr ? file_->channelCount() : 0;
}

int MovieProperties::tempo() const {
    if (tempoSupplier_) {
        return tempoSupplier_();
    }
    if (tempoExplicit_) {
        return tempo_;
    }
    return file_ != nullptr ? file_->tempo() : 15;
}

void MovieProperties::setTempo(int tempo) {
    tempo_ = tempo;
    tempoExplicit_ = true;
    if (tempoSetter_) {
        tempoSetter_(tempo);
    }
}

bool MovieProperties::soundEnabled() const {
    return soundEnabledSupplier_ ? soundEnabledSupplier_() != 0 : soundEnabled_;
}

void MovieProperties::setSoundEnabled(bool enabled) {
    soundEnabled_ = enabled;
    if (soundEnabledSetter_) {
        soundEnabledSetter_(enabled ? 1 : 0);
    }
}

int MovieProperties::soundLevel() const {
    return soundLevelSupplier_ ? soundLevelSupplier_() : soundLevel_;
}

void MovieProperties::setSoundLevel(int level) {
    soundLevel_ = std::clamp(level, 0, 7);
    if (soundLevelSetter_) {
        soundLevelSetter_(soundLevel_);
    }
}

bool MovieProperties::soundKeepDevice() const {
    return soundKeepDeviceSupplier_ ? soundKeepDeviceSupplier_() != 0 : soundKeepDevice_;
}

void MovieProperties::setSoundKeepDevice(bool keepDevice) {
    soundKeepDevice_ = keepDevice;
    if (soundKeepDeviceSetter_) {
        soundKeepDeviceSetter_(keepDevice ? 1 : 0);
    }
}

bool MovieProperties::soundMixMedia() const {
    return soundMixMediaSupplier_ ? soundMixMediaSupplier_() != 0 : soundMixMedia_;
}

void MovieProperties::setSoundMixMedia(bool mixMedia) {
    soundMixMedia_ = mixMedia;
    if (soundMixMediaSetter_) {
        soundMixMediaSetter_(mixMedia ? 1 : 0);
    }
}

int MovieProperties::randomSeed() const {
    return randomSeedSupplier_ ? randomSeedSupplier_() : randomSeed_;
}

void MovieProperties::setRandomSeed(int seed) {
    randomSeed_ = seed;
    if (randomSeedSetter_) {
        randomSeedSetter_(seed);
    }
}

int MovieProperties::stageWidth(int fallback) const {
    return file_ != nullptr ? file_->stageWidth() : fallback;
}

int MovieProperties::stageHeight(int fallback) const {
    return file_ != nullptr ? file_->stageHeight() : fallback;
}

int MovieProperties::colorDepth() const {
    if (file_ != nullptr && file_->config()) {
        const int depth = file_->config()->bgColor();
        if (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16 || depth == 32) {
            return depth;
        }
    }
    return 32;
}

int MovieProperties::stageBackgroundColor() const {
    return stageBackgroundColorSupplier_ ? stageBackgroundColorSupplier_() : stageBackgroundColor_;
}

void MovieProperties::setStageBackgroundColor(int rgb) {
    stageBackgroundColor_ = rgb & 0xFFFFFF;
    if (stageBackgroundColorSetter_) {
        stageBackgroundColorSetter_(stageBackgroundColor_);
    }
}

std::string MovieProperties::movieName() const {
    if (file_ == nullptr) {
        return "";
    }
    const std::string& path = file_->basePath();
    const std::size_t lastSep = path.find_last_of("/\\");
    if (lastSep != std::string::npos && lastSep + 1 < path.size()) {
        return path.substr(lastSep + 1);
    }
    return path;
}

std::string MovieProperties::moviePath() const {
    if (file_ == nullptr) {
        return "";
    }
    std::string path = file_->basePath();
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path.push_back('/');
    }
    return path;
}

lingo::Datum MovieProperties::xtraList() const {
    std::vector<lingo::Datum> items;
    for (const auto& name : registeredXtraNames()) {
        auto entry = lingo::Datum::propList();
        entry.propListValue().put(lingo::Datum::of(std::string("name")), lingo::Datum::of(name));
        entry.propListValue().put(lingo::Datum::of(std::string("fileName")), lingo::Datum::of(name + ".x32"));
        items.push_back(std::move(entry));
    }
    return lingo::Datum::list(std::move(items));
}

std::vector<std::string> MovieProperties::registeredXtraNames() const {
    return xtraNamesSupplier_ ? xtraNamesSupplier_() : std::vector<std::string>{};
}

lingo::Datum MovieProperties::dateTimeProp(std::string_view prop) const {
    const std::tm now = localTimeNow();
    const int year = now.tm_year + 1900;
    const int month = now.tm_mon + 1;
    const int day = now.tm_mday;
    std::ostringstream out;

    if (prop == "date") {
        out << std::setw(2) << std::setfill('0') << month << '/'
            << std::setw(2) << std::setfill('0') << day << '/'
            << year;
        return stringDatum(out.str());
    }
    if (prop == "short date") {
        out << month << '/' << day << '/' << std::setw(2) << std::setfill('0') << (year % 100);
        return stringDatum(out.str());
    }
    if (prop == "abbr date") {
        out << abbreviatedMonthName(now.tm_mon) << ' ' << day << ", " << year;
        return stringDatum(out.str());
    }
    if (prop == "long date") {
        out << monthName(now.tm_mon) << ' ' << day << ", " << year;
        return stringDatum(out.str());
    }
    if (prop == "long time") {
        return stringDatum(format12Hour(now, true));
    }
    return stringDatum(format12Hour(now, false));
}

lingo::Datum MovieProperties::elapsedProp(std::string_view prop) const {
    const auto elapsed = std::chrono::steady_clock::now() - startTime_;
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    if (prop == "milliseconds") {
        return lingo::Datum::of(static_cast<int>(millis));
    }
    return lingo::Datum::of(static_cast<int>((millis * 60) / 1000));
}

std::string MovieProperties::lowerProp(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

int MovieProperties::colorToRgb(const lingo::Datum& value) {
    if (const auto* color = value.asColorRef()) {
        return (color->r << 16) | (color->g << 8) | color->b;
    }
    return value.intValue() & 0xFFFFFF;
}

} // namespace libreshockwave::player
