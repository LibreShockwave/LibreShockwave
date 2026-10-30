#include "libreshockwave/player/movie_properties.hpp"

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

#include "libreshockwave/chunks/config_chunk.hpp"
#include "libreshockwave/chunks/score_chunk.hpp"

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

std::string format12Hour(const std::tm& time, bool include_seconds) {
    int hour = time.tm_hour % 12;
    if (hour == 0) {
        hour = 12;
    }
    std::ostringstream out;
    out << hour << ':' << std::setw(2) << std::setfill('0') << time.tm_min;
    if (include_seconds) {
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
      start_time_(std::chrono::steady_clock::now()) {}

void MovieProperties::setFile(DirectorFile* file) {
    file_ = file;
}

void MovieProperties::setInputState(input::InputState* input_state) {
    input_state_ = input_state;
}

void MovieProperties::setEffectiveFrameSupplier(IntSupplier supplier) {
    effective_frame_supplier_ = std::move(supplier);
}

void MovieProperties::setFrameCountSupplier(IntSupplier supplier) {
    frame_count_supplier_ = std::move(supplier);
}

void MovieProperties::setTempoSupplier(IntSupplier supplier) {
    tempo_supplier_ = std::move(supplier);
}

void MovieProperties::setTempoSetter(IntConsumer setter) {
    tempo_setter_ = std::move(setter);
}

void MovieProperties::setSoundEnabledSupplier(IntSupplier supplier) {
    sound_enabled_supplier_ = std::move(supplier);
}

void MovieProperties::setSoundEnabledSetter(IntConsumer setter) {
    sound_enabled_setter_ = std::move(setter);
}

void MovieProperties::setSoundLevelSupplier(IntSupplier supplier) {
    sound_level_supplier_ = std::move(supplier);
}

void MovieProperties::setSoundLevelSetter(IntConsumer setter) {
    sound_level_setter_ = std::move(setter);
}

void MovieProperties::setSoundKeepDeviceSupplier(IntSupplier supplier) {
    sound_keep_device_supplier_ = std::move(supplier);
}

void MovieProperties::setSoundKeepDeviceSetter(IntConsumer setter) {
    sound_keep_device_setter_ = std::move(setter);
}

void MovieProperties::setSoundMixMediaSupplier(IntSupplier supplier) {
    sound_mix_media_supplier_ = std::move(supplier);
}

void MovieProperties::setSoundMixMediaSetter(IntConsumer setter) {
    sound_mix_media_setter_ = std::move(setter);
}

void MovieProperties::setRandomSeedSupplier(IntSupplier supplier) {
    random_seed_supplier_ = std::move(supplier);
}

void MovieProperties::setRandomSeedSetter(IntConsumer setter) {
    random_seed_setter_ = std::move(setter);
}

void MovieProperties::setParamCountSupplier(IntSupplier supplier) {
    param_count_supplier_ = std::move(supplier);
}

void MovieProperties::setCastLibCountSupplier(IntSupplier supplier) {
    cast_lib_count_supplier_ = std::move(supplier);
}

void MovieProperties::setXtraNamesSupplier(XtraNamesSupplier supplier) {
    xtra_names_supplier_ = std::move(supplier);
}

void MovieProperties::setStageBackgroundColorSupplier(IntSupplier supplier) {
    stage_background_color_supplier_ = std::move(supplier);
}

void MovieProperties::setStageBackgroundColorSetter(IntConsumer setter) {
    stage_background_color_setter_ = std::move(setter);
}

void MovieProperties::setStageImageSupplier(DatumSupplier supplier) {
    stage_image_supplier_ = std::move(supplier);
}

void MovieProperties::setGoToFrameHandler(IntConsumer handler) {
    go_to_frame_handler_ = std::move(handler);
}

void MovieProperties::setGoToLabelHandler(StringConsumer handler) {
    go_to_label_handler_ = std::move(handler);
}

void MovieProperties::setFrameForLabelResolver(LabelResolver resolver) {
    frame_for_label_resolver_ = std::move(resolver);
}

void MovieProperties::setMarkerFrameResolver(MarkerResolver resolver) {
    marker_frame_resolver_ = std::move(resolver);
}

void MovieProperties::setGotoNetPageHandler(NetPageHandler handler) {
    goto_net_page_handler_ = std::move(handler);
}

void MovieProperties::setGotoNetMovieHandler(NetMovieHandler handler) {
    goto_net_movie_handler_ = std::move(handler);
}

void MovieProperties::markMovieStart() {
    movie_start_time_ = std::chrono::steady_clock::now();
}

lingo::Datum MovieProperties::getMovieProp(std::string_view prop_name) const {
    const std::string prop = lowerProp(prop_name);

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
    if (prop == "exitlock") return lingo::Datum::of(exit_lock_ ? 1 : 0);
    if (prop == "updatelock") return lingo::Datum::of(update_lock_ ? 1 : 0);
    if (prop == "itemdelimiter") return stringDatum(item_delimiter_);
    if (prop == "puppettempo") return lingo::Datum::of(puppet_tempo_);
    if (prop == "tracescript") return lingo::Datum::of(trace_script_ ? 1 : 0);
    if (prop == "tracelogfile") return stringDatum(trace_log_file_);
    if (prop == "allowcustomcaching") return lingo::Datum::of(allow_custom_caching_ ? 1 : 0);
    if (prop == "mousedownscript") return mouse_down_script_;
    if (prop == "mouseupscript") return mouse_up_script_;
    if (prop == "keydownscript") return key_down_script_;
    if (prop == "keyupscript") return key_up_script_;
    if (prop == "timeoutscript") return timeout_script_;
    if (prop == "alerthook") return alert_hook_;
    if (prop == "cursor") return cursor_;
    if (prop == "floatprecision") return lingo::Datum::of(float_precision_);
    if (prop == "beepon") return lingo::Datum::of(beep_on_ ? 1 : 0);
    if (prop == "buttonstyle") return lingo::Datum::of(button_style_);
    if (prop == "centerstage") return lingo::Datum::of(center_stage_ ? 1 : 0);
    if (prop == "checkboxaccess") return lingo::Datum::of(check_box_access_ ? 1 : 0);
    if (prop == "checkboxtype") return lingo::Datum::of(check_box_type_);
    if (prop == "colorqd") return lingo::Datum::of(color_qd_ ? 1 : 0);
    if (prop == "fixstagesize") return lingo::Datum::of(fix_stage_size_ ? 1 : 0);
    if (prop == "imagedirect") return lingo::Datum::of(image_direct_ ? 1 : 0);
    if (prop == "lastclick") return lingo::Datum::of(input_state_ != nullptr ? input_state_->lastClickTicks() : 0);
    if (prop == "lastevent") return lingo::Datum::of(input_state_ != nullptr ? input_state_->lastEventTicks() : 0);
    if (prop == "lastkey") return lingo::Datum::of(input_state_ != nullptr ? input_state_->lastKeyTicks() : 0);
    if (prop == "lastroll") return lingo::Datum::of(input_state_ != nullptr ? input_state_->lastRollTicks() : 0);
    if (prop == "soundenabled") return lingo::Datum::of(soundEnabled() ? 1 : 0);
    if (prop == "soundlevel") return lingo::Datum::of(soundLevel());
    if (prop == "soundkeepdevice") return lingo::Datum::of(soundKeepDevice() ? 1 : 0);
    if (prop == "soundmixmedia") return lingo::Datum::of(soundMixMedia() ? 1 : 0);
    if (prop == "multisound") return lingo::Datum::TRUE;
    if (prop == "netpresent") return lingo::Datum::TRUE;
    if (prop == "pausestate") return lingo::Datum::of(pause_state_ ? 1 : 0);
    if (prop == "safeplayer") return lingo::Datum::of(safe_player_ ? 1 : 0);
    if (prop == "preloadram") return lingo::Datum::of(pre_load_ram_);
    if (prop == "quicktimepresent" || prop == "videoforwindowspresent") return lingo::Datum::FALSE;
    if (prop == "stagecolor") return lingo::Datum::of(stageBackgroundColor());
    if (prop == "switchcolordepth") return lingo::Datum::of(switch_color_depth_ ? 1 : 0);
    if (prop == "timeoutlapsed") return lingo::Datum::of(input_state_ != nullptr ? input_state_->lastEventTicks() : 0);
    if (prop == "timeoutkeydown") return lingo::Datum::of(timeout_key_down_ ? 1 : 0);
    if (prop == "timeoutlength") return lingo::Datum::of(timeout_length_);
    if (prop == "timeoutmouse") return lingo::Datum::of(timeout_mouse_ ? 1 : 0);
    if (prop == "timeoutplay") return lingo::Datum::of(timeout_play_ ? 1 : 0);
    if (prop == "randomseed") return lingo::Datum::of(randomSeed());
    if (prop == "actorlist") return actor_list_;
    if (prop == "framerate" || prop == "tempo" || prop == "frametempo") return lingo::Datum::of(tempo());
    if (prop == "mousedown") return lingo::Datum::of(input_state_ != nullptr && input_state_->isMouseDown() ? 1 : 0);
    if (prop == "mouseup") return lingo::Datum::of(input_state_ == nullptr || !input_state_->isMouseDown() ? 1 : 0);
    if (prop == "mouseh") return lingo::Datum::of(input_state_ != nullptr ? input_state_->mouseH() : 0);
    if (prop == "mousev") return lingo::Datum::of(input_state_ != nullptr ? input_state_->mouseV() : 0);
    if (prop == "clickon") return lingo::Datum::of(input_state_ != nullptr ? input_state_->clickOnSprite() : 0);
    if (prop == "clickloc") {
        return input_state_ != nullptr ? lingo::Datum::intPoint(input_state_->clickLocH(), input_state_->clickLocV())
                                      : lingo::Datum::intPoint(0, 0);
    }
    if (prop == "mouseloc") {
        return input_state_ != nullptr ? lingo::Datum::intPoint(input_state_->mouseH(), input_state_->mouseV())
                                      : lingo::Datum::intPoint(0, 0);
    }
    if (prop == "rightmousedown") return lingo::Datum::of(input_state_ != nullptr && input_state_->isRightMouseDown() ? 1 : 0);
    if (prop == "doubleclick") return lingo::Datum::of(input_state_ != nullptr && input_state_->isDoubleClick() ? 1 : 0);
    if (prop == "rollover") return lingo::Datum::of(input_state_ != nullptr ? input_state_->rolloverSprite() : 0);
    if (prop == "key") return stringDatum(input_state_ != nullptr ? input_state_->lastKey() : "");
    if (prop == "keycode" || prop == "keypressed") return lingo::Datum::of(input_state_ != nullptr ? input_state_->lastKeyCode() : 0);
    if (prop == "shiftdown") return lingo::Datum::of(input_state_ != nullptr && input_state_->isShiftDown() ? 1 : 0);
    if (prop == "optiondown" || prop == "altdown") return lingo::Datum::of(input_state_ != nullptr && input_state_->isAltDown() ? 1 : 0);
    if (prop == "commanddown" || prop == "controldown") return lingo::Datum::of(input_state_ != nullptr && input_state_->isControlDown() ? 1 : 0);
    if (prop == "keyboardfocussprite") return lingo::Datum::of(input_state_ != nullptr ? input_state_->keyboardFocusSprite() : 0);
    if (prop == "selstart") return lingo::Datum::of(input_state_ != nullptr ? input_state_->selStart() : 0);
    if (prop == "selend") return lingo::Datum::of(input_state_ != nullptr ? input_state_->selEnd() : 0);
    if (prop == "colordepth") return lingo::Datum::of(colorDepth());
    if (prop == "fullcolorpermit") return lingo::Datum::TRUE;
    if (prop == "perframehook") return lingo::Datum::voidValue();
    if (prop == "number of castmembers") return lingo::Datum::of(file_ != nullptr ? static_cast<int>(file_->castMembers().size()) : 0);
    if (prop == "number of menus") return lingo::Datum::of(0);
    if (prop == "number of castlibs") {
        if (cast_lib_count_supplier_) {
            return lingo::Datum::of(cast_lib_count_supplier_());
        }
        return lingo::Datum::of(file_ != nullptr ? static_cast<int>(file_->casts().size()) : 0);
    }
    if (prop == "number of xtras") return lingo::Datum::of(static_cast<int>(registeredXtraNames().size()));
    if (prop == "xtralist") return xtraList();
    if (prop == "activewindow" || prop == "stage") return lingo::Datum::stageRef();
    if (prop == "windowlist") {
        // windowList is the list of auxiliary windows, not the player's
        // stage.  In a standalone player it is empty; exposing the stage
        // here makes Habbo's authoring-context guard call stopMovie() during
        // startup (and from its crypto routines).
        return lingo::Datum::list({});
    }
    if (prop == "emptystring") return stringDatum("");
    if (prop == "pi") return lingo::Datum::of(std::numbers::pi);
    if (prop == "enter") return stringDatum("\n");
    if (prop == "tab") return stringDatum("\t");
    if (prop == "quote") return stringDatum("\"");
    if (prop == "backspace") return stringDatum("\b");
    if (prop == "space") return stringDatum(" ");
    if (prop == "paramcount") return lingo::Datum::of(param_count_supplier_ ? param_count_supplier_() : 0);
    return lingo::Datum::voidValue();
}

bool MovieProperties::setMovieProp(std::string_view prop_name, const lingo::Datum& value) {
    const std::string prop = lowerProp(prop_name);
    if (prop == "exitlock") {
        exit_lock_ = value.boolValue();
        return true;
    }
    if (prop == "updatelock") {
        update_lock_ = value.boolValue();
        return true;
    }
    if (prop == "itemdelimiter") {
        const std::string value_string = value.stringValue();
        item_delimiter_ = value_string.empty() ? "," : value_string.substr(0, 1);
        return true;
    }
    if (prop == "puppettempo") {
        setPuppetTempo(value.intValue());
        return true;
    }
    if (prop == "tracescript") {
        trace_script_ = value.boolValue();
        return true;
    }
    if (prop == "tracelogfile") {
        trace_log_file_ = value.stringValue();
        return true;
    }
    if (prop == "allowcustomcaching") {
        allow_custom_caching_ = value.boolValue();
        return true;
    }
    if (prop == "mousedownscript") {
        mouse_down_script_ = value;
        return true;
    }
    if (prop == "mouseupscript") {
        mouse_up_script_ = value;
        return true;
    }
    if (prop == "keydownscript") {
        key_down_script_ = value;
        return true;
    }
    if (prop == "keyupscript") {
        key_up_script_ = value;
        return true;
    }
    if (prop == "timeoutscript") {
        timeout_script_ = value;
        return true;
    }
    if (prop == "actorlist") {
        actor_list_ = value;
        return true;
    }
    if (prop == "tempo" || prop == "framerate" || prop == "frametempo") {
        setTempo(value.intValue());
        return true;
    }
    if (prop == "keyboardfocussprite") {
        if (input_state_ != nullptr) {
            input_state_->setKeyboardFocusSprite(value.intValue());
        }
        return true;
    }
    if (prop == "alerthook") {
        alert_hook_ = value;
        return true;
    }
    if (prop == "cursor") {
        cursor_ = value;
        return true;
    }
    if (prop == "floatprecision") {
        float_precision_ = value.intValue();
        return true;
    }
    if (prop == "beepon") {
        beep_on_ = value.boolValue();
        return true;
    }
    if (prop == "buttonstyle") {
        button_style_ = value.intValue();
        return true;
    }
    if (prop == "centerstage") {
        center_stage_ = value.boolValue();
        return true;
    }
    if (prop == "checkboxaccess") {
        check_box_access_ = value.boolValue();
        return true;
    }
    if (prop == "checkboxtype") {
        check_box_type_ = value.intValue();
        return true;
    }
    if (prop == "colorqd") {
        color_qd_ = value.boolValue();
        return true;
    }
    if (prop == "fixstagesize") {
        fix_stage_size_ = value.boolValue();
        return true;
    }
    if (prop == "imagedirect") {
        image_direct_ = value.boolValue();
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
        pause_state_ = value.boolValue();
        return true;
    }
    if (prop == "safeplayer") {
        if (value.boolValue()) {
            safe_player_ = true;
        }
        return true;
    }
    if (prop == "preloadram") {
        pre_load_ram_ = std::max(0, value.intValue());
        return true;
    }
    if (prop == "stagecolor") {
        if (!value.isVoid()) {
            setStageBackgroundColor(colorToRgb(value));
        }
        return true;
    }
    if (prop == "switchcolordepth") {
        switch_color_depth_ = value.boolValue();
        return true;
    }
    if (prop == "timeoutkeydown") {
        timeout_key_down_ = value.boolValue();
        return true;
    }
    if (prop == "timeoutlength") {
        timeout_length_ = std::max(0, value.intValue());
        return true;
    }
    if (prop == "timeoutmouse") {
        timeout_mouse_ = value.boolValue();
        return true;
    }
    if (prop == "timeoutplay") {
        timeout_play_ = value.boolValue();
        return true;
    }
    if (prop == "randomseed") {
        setRandomSeed(value.intValue());
        return true;
    }
    if (prop == "selstart") {
        if (input_state_ != nullptr) {
            input_state_->setSelStart(value.intValue());
        }
        return true;
    }
    if (prop == "selend") {
        if (input_state_ != nullptr) {
            input_state_->setSelEnd(value.intValue());
        }
        return true;
    }
    if (prop == "debugplaybackenabled") {
        return true;
    }
    return false;
}

lingo::Datum MovieProperties::getStageProp(std::string_view prop_name) const {
    const std::string prop = lowerProp(prop_name);
    if (prop == "rect" || prop == "sourcerect" || prop == "drawrect") {
        return lingo::Datum::intRect(0, 0, stageWidth(640), stageHeight(480));
    }
    if (prop == "title") return stringDatum(stage_title_);
    if (prop == "name") return stringDatum("stage");
    if (prop == "visible") return lingo::Datum::TRUE;
    if (prop == "bgcolor") return lingo::Datum::of(stageBackgroundColor());
    if (prop == "image") return stage_image_supplier_ ? stage_image_supplier_() : lingo::Datum::voidValue();
    return getMovieProp(prop_name);
}

bool MovieProperties::setStageProp(std::string_view prop_name, const lingo::Datum& value) {
    const std::string prop = lowerProp(prop_name);
    if (prop == "title") {
        stage_title_ = value.stringValue();
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
    return setMovieProp(prop_name, value);
}

char MovieProperties::getItemDelimiter() const {
    return item_delimiter_.empty() ? ',' : item_delimiter_.front();
}

void MovieProperties::setItemDelimiter(char delimiter) {
    item_delimiter_ = std::string(1, delimiter);
}

void MovieProperties::goToFrame(int frame) const {
    if (go_to_frame_handler_) {
        go_to_frame_handler_(frame);
    }
}

void MovieProperties::goToLabel(const std::string& label) const {
    if (go_to_label_handler_) {
        go_to_label_handler_(label);
    }
}

int MovieProperties::getFrameForLabel(const std::string& label) const {
    return frame_for_label_resolver_ ? frame_for_label_resolver_(label) : 0;
}

int MovieProperties::getMarkerFrame(int marker_offset) const {
    return marker_frame_resolver_ ? marker_frame_resolver_(marker_offset) : 0;
}

void MovieProperties::gotoNetPage(const std::string& url, const std::string& target) const {
    if (goto_net_page_handler_) {
        goto_net_page_handler_(url, target);
    }
}

int MovieProperties::gotoNetMovie(const std::string& url) const {
    return goto_net_movie_handler_ ? goto_net_movie_handler_(url) : -1;
}

bool MovieProperties::exitLock() const {
    return exit_lock_;
}

bool MovieProperties::updateLock() const {
    return update_lock_;
}

const std::string& MovieProperties::itemDelimiterString() const {
    return item_delimiter_;
}

int MovieProperties::puppetTempo() const {
    return puppet_tempo_;
}

void MovieProperties::setPuppetTempo(int puppet_tempo) {
    puppet_tempo_ = std::max(0, puppet_tempo);
    if (input_state_ != nullptr) {
        input_state_->setCaretBlinkRate(tempo());
    }
}

const lingo::Datum& MovieProperties::actorList() const {
    return actor_list_;
}

const lingo::Datum& MovieProperties::alertHook() const {
    return alert_hook_;
}

int MovieProperties::effectiveFrame() const {
    return effective_frame_supplier_ ? effective_frame_supplier_() : 0;
}

int MovieProperties::frameCount() const {
    if (frame_count_supplier_) {
        return frame_count_supplier_();
    }
    return file_ != nullptr && file_->scoreChunk() ? file_->scoreChunk()->getFrameCount() : 0;
}

int MovieProperties::channelCount() const {
    return file_ != nullptr ? file_->channelCount() : 0;
}

int MovieProperties::tempo() const {
    if (tempo_supplier_) {
        return tempo_supplier_();
    }
    if (tempo_explicit_) {
        return tempo_;
    }
    return file_ != nullptr ? file_->tempo() : 15;
}

void MovieProperties::setTempo(int tempo) {
    tempo_ = tempo;
    tempo_explicit_ = true;
    if (tempo_setter_) {
        tempo_setter_(tempo);
    }
}

bool MovieProperties::soundEnabled() const {
    return sound_enabled_supplier_ ? sound_enabled_supplier_() != 0 : sound_enabled_;
}

void MovieProperties::setSoundEnabled(bool enabled) {
    sound_enabled_ = enabled;
    if (sound_enabled_setter_) {
        sound_enabled_setter_(enabled ? 1 : 0);
    }
}

int MovieProperties::soundLevel() const {
    return sound_level_supplier_ ? sound_level_supplier_() : sound_level_;
}

void MovieProperties::setSoundLevel(int level) {
    sound_level_ = std::clamp(level, 0, 7);
    if (sound_level_setter_) {
        sound_level_setter_(sound_level_);
    }
}

bool MovieProperties::soundKeepDevice() const {
    return sound_keep_device_supplier_ ? sound_keep_device_supplier_() != 0 : sound_keep_device_;
}

void MovieProperties::setSoundKeepDevice(bool keep_device) {
    sound_keep_device_ = keep_device;
    if (sound_keep_device_setter_) {
        sound_keep_device_setter_(keep_device ? 1 : 0);
    }
}

bool MovieProperties::soundMixMedia() const {
    return sound_mix_media_supplier_ ? sound_mix_media_supplier_() != 0 : sound_mix_media_;
}

void MovieProperties::setSoundMixMedia(bool mix_media) {
    sound_mix_media_ = mix_media;
    if (sound_mix_media_setter_) {
        sound_mix_media_setter_(mix_media ? 1 : 0);
    }
}

int MovieProperties::randomSeed() const {
    return random_seed_supplier_ ? random_seed_supplier_() : random_seed_;
}

void MovieProperties::setRandomSeed(int seed) {
    random_seed_ = seed;
    if (random_seed_setter_) {
        random_seed_setter_(seed);
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
    return stage_background_color_supplier_ ? stage_background_color_supplier_() : stage_background_color_;
}

void MovieProperties::setStageBackgroundColor(int rgb) {
    stage_background_color_ = rgb & 0xFFFFFF;
    if (stage_background_color_setter_) {
        stage_background_color_setter_(stage_background_color_);
    }
}

std::string MovieProperties::movieName() const {
    if (file_ == nullptr) {
        return "";
    }
    const std::string& path = file_->basePath();
    const std::size_t last_sep = path.find_last_of("/\\");
    if (last_sep != std::string::npos && last_sep + 1 < path.size()) {
        return path.substr(last_sep + 1);
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
    return xtra_names_supplier_ ? xtra_names_supplier_() : std::vector<std::string>{};
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
    if (prop == "milliseconds") {
        return lingo::Datum::of(static_cast<int>(elapsedMsSince(start_time_)));
    }
    if (movie_start_time_ == std::nullopt) {
        return lingo::Datum::of(0);
    }
    const auto millis = elapsedMsSince(*movie_start_time_);
    return lingo::Datum::of(static_cast<int>((millis * 60) / 1000));
}

long long MovieProperties::elapsedMsSince(std::chrono::steady_clock::time_point since) const {
    const auto elapsed = std::chrono::steady_clock::now() - since;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
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
