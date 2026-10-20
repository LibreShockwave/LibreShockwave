#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/director_file.hpp"
#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/player/input/input_state.hpp"

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
    void setInputState(input::InputState* input_state);
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

    [[nodiscard]] lingo::Datum getMovieProp(std::string_view prop_name) const;
    [[nodiscard]] bool setMovieProp(std::string_view prop_name, const lingo::Datum& value);
    [[nodiscard]] lingo::Datum getStageProp(std::string_view prop_name) const;
    [[nodiscard]] bool setStageProp(std::string_view prop_name, const lingo::Datum& value);

    [[nodiscard]] char getItemDelimiter() const;
    void setItemDelimiter(char delimiter);
    void goToFrame(int frame) const;
    void goToLabel(const std::string& label) const;
    [[nodiscard]] int getFrameForLabel(const std::string& label) const;
    [[nodiscard]] int getMarkerFrame(int marker_offset) const;
    void gotoNetPage(const std::string& url, const std::string& target) const;
    [[nodiscard]] int gotoNetMovie(const std::string& url) const;

    [[nodiscard]] bool exitLock() const;
    [[nodiscard]] bool updateLock() const;
    [[nodiscard]] const std::string& itemDelimiterString() const;
    [[nodiscard]] int puppetTempo() const;
    void setPuppetTempo(int puppet_tempo);
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
    void setSoundKeepDevice(bool keep_device);
    [[nodiscard]] bool soundMixMedia() const;
    void setSoundMixMedia(bool mix_media);
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
    input::InputState* input_state_{nullptr};

    bool exit_lock_{false};
    bool update_lock_{false};
    std::string item_delimiter_{","};
    int puppet_tempo_{0};
    bool trace_script_{false};
    std::string trace_log_file_;
    bool allow_custom_caching_{false};
    lingo::Datum mouse_down_script_{lingo::Datum::of(std::string())};
    lingo::Datum mouse_up_script_{lingo::Datum::of(std::string())};
    lingo::Datum key_down_script_{lingo::Datum::of(std::string())};
    lingo::Datum key_up_script_{lingo::Datum::of(std::string())};
    lingo::Datum timeout_script_{lingo::Datum::of(std::string())};
    lingo::Datum alert_hook_{lingo::Datum::voidValue()};
    lingo::Datum cursor_{lingo::Datum::of(-1)};
    int float_precision_{4};
    std::string stage_title_;
    lingo::Datum actor_list_{lingo::Datum::list()};
    int tempo_{15};
    bool tempo_explicit_{false};
    bool beep_on_{true};
    int button_style_{0};
    bool center_stage_{false};
    bool check_box_access_{false};
    int check_box_type_{0};
    bool color_qd_{true};
    bool fix_stage_size_{false};
    bool image_direct_{false};
    bool pause_state_{false};
    bool sound_enabled_{true};
    int sound_level_{7};
    bool sound_keep_device_{true};
    bool sound_mix_media_{true};
    bool safe_player_{true};
    int pre_load_ram_{0};
    bool switch_color_depth_{false};
    bool timeout_key_down_{true};
    int timeout_length_{0};
    bool timeout_mouse_{true};
    bool timeout_play_{true};
    int random_seed_{0};
    int stage_background_color_{0};

    std::chrono::steady_clock::time_point start_time_;

    IntSupplier effective_frame_supplier_;
    IntSupplier frame_count_supplier_;
    IntSupplier tempo_supplier_;
    IntConsumer tempo_setter_;
    IntSupplier sound_enabled_supplier_;
    IntConsumer sound_enabled_setter_;
    IntSupplier sound_level_supplier_;
    IntConsumer sound_level_setter_;
    IntSupplier sound_keep_device_supplier_;
    IntConsumer sound_keep_device_setter_;
    IntSupplier sound_mix_media_supplier_;
    IntConsumer sound_mix_media_setter_;
    IntSupplier random_seed_supplier_;
    IntConsumer random_seed_setter_;
    IntSupplier param_count_supplier_;
    IntSupplier cast_lib_count_supplier_;
    XtraNamesSupplier xtra_names_supplier_;
    IntSupplier stage_background_color_supplier_;
    IntConsumer stage_background_color_setter_;
    DatumSupplier stage_image_supplier_;
    IntConsumer go_to_frame_handler_;
    StringConsumer go_to_label_handler_;
    LabelResolver frame_for_label_resolver_;
    MarkerResolver marker_frame_resolver_;
    NetPageHandler goto_net_page_handler_;
    NetMovieHandler goto_net_movie_handler_;
};

} // namespace libreshockwave::player
