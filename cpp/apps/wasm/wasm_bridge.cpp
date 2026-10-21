#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif
#ifdef __EMSCRIPTEN__
#define LSW_EXPORT(name) __attribute__((export_name(name)))
#else
#define LSW_EXPORT(name)
#endif

#include "libreshockwave/moviesource/movie_source.hpp"
#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/cast/cast_member.hpp"
#include "libreshockwave/lingo/vm/opcode_registry.hpp"
#include "libreshockwave/player/input_handler.hpp"
#include "libreshockwave/player/player.hpp"
#include "libreshockwave/player/player_state.hpp"
#include "libreshockwave/player/audio/queued_audio_backend.hpp"
#include "libreshockwave/player/debug/debug_controller_api.hpp"
#include "libreshockwave/player/event/event_dispatcher.hpp"
#include "libreshockwave/player/input/director_key_codes.hpp"
#include "libreshockwave/player/media/queued_jpeg_decoder.hpp"
#include "libreshockwave/player/render/pipeline/render_sprite.hpp"
#include "libreshockwave/player/net/queued_net_provider.hpp"
#include "libreshockwave/player/xtra/queued_multiuser_bridge.hpp"
#include "libreshockwave/util/file_util.hpp"

namespace {

using libreshockwave::moviesource::MovieSource;
using libreshockwave::bitmap::Bitmap;
using libreshockwave::player::InputHandler;
using libreshockwave::player::Player;
using libreshockwave::player::PlayerEvent;
using libreshockwave::player::PlayerState;
using libreshockwave::player::handlerName;
using libreshockwave::player::audio::QueuedAudioBackend;
using libreshockwave::player::input::DirectorKeyCodes;
using libreshockwave::player::media::QueuedJpegDecoder;
using libreshockwave::player::net::QueuedNetProvider;
using libreshockwave::player::render::pipeline::RenderSprite;
using libreshockwave::player::xtra::QueuedMultiuserBridge;

// Fetch completion can synchronously finish Director cast-load callbacks.
// Some legacy movies do real work there, including Habbo's sec.cct crypto setup,
// so the default must be a guardrail rather than a one-frame budget.
constexpr int DEFAULT_WASM_SCRIPT_DEADLINE_MS = 60000;
constexpr int SLOW_WASM_OPERATION_WARNING_MS = 1000;

struct WasmPlayerContext {
    std::shared_ptr<MovieSource> file;
    std::unique_ptr<QueuedNetProvider> net_provider;
    std::unique_ptr<QueuedMultiuserBridge> multiuser_bridge;
    std::unique_ptr<QueuedAudioBackend> audio_backend;
    std::unique_ptr<QueuedJpegDecoder> jpeg_decoder;
    std::unique_ptr<Player> player;
    std::vector<std::pair<std::string, std::string>> external_params;
    std::vector<std::uint8_t> frame_rgba;
    int frame_width{0};
    int frame_height{0};
    int frame_background{0};
    bool tempo_override_enabled{false};
    int tempo_override{0};
    bool preload_casts{true};
    std::string movie_source;
    std::string last_error;
    std::string last_status;
    std::string scratch;
    bool debug_playback_enabled{false};
    int slow_handler_warning_ms{SLOW_WASM_OPERATION_WARNING_MS};
    std::vector<std::pair<std::string, std::string>> debug_messages;
};

std::unordered_map<int, std::unique_ptr<WasmPlayerContext>> contexts;
int next_handle = 1;

WasmPlayerContext* getContext(int handle) {
    const auto found = contexts.find(handle);
    return found == contexts.end() ? nullptr : found->second.get();
}

std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string toLower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

std::string sourceDirectory(std::string_view source) {
    if (source.empty()) {
        return {};
    }
    const std::string value(source);
    const auto query = value.find_first_of("?#");
    const std::string clean = query == std::string::npos ? value : value.substr(0, query);
    const auto slash = clean.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    return clean.substr(0, slash + 1);
}

std::uint8_t blendChannel(std::uint8_t foreground, std::uint8_t background, std::uint8_t alpha) {
    return static_cast<std::uint8_t>(
        (static_cast<int>(foreground) * alpha + static_cast<int>(background) * (255 - alpha) + 127) / 255);
}

std::string jsonEscape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const unsigned char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(ch >> 4U) & 0xFU]);
                    out.push_back(hex[ch & 0xFU]);
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

void appendJsonString(std::ostringstream& out, std::string_view value) {
    out << '"' << jsonEscape(value) << '"';
}

void queueDebugMessage(WasmPlayerContext& ctx, std::string level, std::string message) {
    if (message.empty()) {
        return;
    }
    constexpr std::size_t k_max_debug_messages = 500;
    if (ctx.debug_messages.size() >= k_max_debug_messages) {
        ctx.debug_messages.erase(ctx.debug_messages.begin());
    }
    ctx.debug_messages.emplace_back(std::move(level), std::move(message));
}

std::int64_t elapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
}

void queueSlowOperationMessage(WasmPlayerContext& ctx,
                               std::string_view operation,
                               std::chrono::steady_clock::time_point start) {
    const auto elapsed = elapsedMs(start);
    if (elapsed < SLOW_WASM_OPERATION_WARNING_MS) {
        return;
    }
    queueDebugMessage(ctx,
                      "warning",
                      std::string(operation) + " took " + std::to_string(elapsed) + "ms");
}

std::string debugMessagesJson(const WasmPlayerContext& ctx) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& [level, message] : ctx.debug_messages) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"level\":";
        appendJsonString(out, level);
        out << ",\"message\":";
        appendJsonString(out, message);
        out << '}';
    }
    out << ']';
    return out.str();
}

std::string base64Encode(const std::uint8_t* bytes, std::size_t length) {
    constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((length + 2U) / 3U) * 4U);
    for (std::size_t index = 0; index < length; index += 3U) {
        const std::uint32_t b0 = bytes[index];
        const std::uint32_t b1 = index + 1U < length ? bytes[index + 1U] : 0;
        const std::uint32_t b2 = index + 2U < length ? bytes[index + 2U] : 0;
        const std::uint32_t triple = (b0 << 16U) | (b1 << 8U) | b2;
        out.push_back(table[(triple >> 18U) & 0x3FU]);
        out.push_back(table[(triple >> 12U) & 0x3FU]);
        out.push_back(index + 1U < length ? table[(triple >> 6U) & 0x3FU] : '=');
        out.push_back(index + 2U < length ? table[triple & 0x3FU] : '=');
    }
    return out;
}

std::string base64Encode(const std::vector<std::uint8_t>& bytes) {
    return bytes.empty() ? std::string() : base64Encode(bytes.data(), bytes.size());
}

std::vector<std::uint8_t> bitmapToRgba(const Bitmap& bitmap, int background_rgb) {
    std::vector<std::uint8_t> rgba;
    if (bitmap.width() <= 0 || bitmap.height() <= 0 ||
        bitmap.pixels().size() != static_cast<std::size_t>(bitmap.width() * bitmap.height())) {
        return rgba;
    }

    const auto background_red = static_cast<std::uint8_t>((background_rgb >> 16) & 0xFF);
    const auto background_green = static_cast<std::uint8_t>((background_rgb >> 8) & 0xFF);
    const auto background_blue = static_cast<std::uint8_t>(background_rgb & 0xFF);

    rgba.reserve(bitmap.pixels().size() * 4U);
    for (const std::uint32_t argb : bitmap.pixels()) {
        const auto alpha = static_cast<std::uint8_t>((argb >> 24) & 0xFF);
        const auto red = static_cast<std::uint8_t>((argb >> 16) & 0xFF);
        const auto green = static_cast<std::uint8_t>((argb >> 8) & 0xFF);
        const auto blue = static_cast<std::uint8_t>(argb & 0xFF);
        rgba.push_back(blendChannel(red, background_red, alpha));
        rgba.push_back(blendChannel(green, background_green, alpha));
        rgba.push_back(blendChannel(blue, background_blue, alpha));
        rgba.push_back(0xFF);
    }
    return rgba;
}

std::uint32_t fnv1a32(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t hash = 2166136261U;
    for (const auto byte : bytes) {
        hash ^= static_cast<std::uint32_t>(byte);
        hash *= 16777619U;
    }
    return hash;
}

std::string cursorCssForCode(int code) {
    switch (code) {
        case libreshockwave::player::CursorManager::IBEAM_CURSOR:
            return "text";
        case libreshockwave::player::CursorManager::WAIT_CURSOR:
            return "wait";
        case libreshockwave::player::CursorManager::POINTER_CURSOR:
            return "pointer";
        case libreshockwave::player::CursorManager::ARROW_CURSOR:
        case libreshockwave::player::CursorManager::DEFAULT_CURSOR:
        default:
            return "default";
    }
}

std::vector<std::pair<std::string, std::string>> parseParams(const char* text) {
    std::vector<std::pair<std::string, std::string>> params;
    if (text == nullptr || *text == '\0') {
        return params;
    }

    std::string input(text);
    for (char& ch : input) {
        if (ch == '&' || ch == '\r') {
            ch = '\n';
        }
    }

    std::size_t offset = 0;
    while (offset <= input.size()) {
        const auto next = input.find('\n', offset);
        const auto token = trim(input.substr(offset, next == std::string::npos ? std::string::npos : next - offset));
        if (!token.empty()) {
            if (const auto equals = token.find('='); equals != std::string::npos) {
                const std::string key = trim(std::string_view(token).substr(0, equals));
                const std::string value = trim(std::string_view(token).substr(equals + 1));
                if (!key.empty()) {
                    params.emplace_back(key, value);
                }
            }
        }
        if (next == std::string::npos) {
            break;
        }
        offset = next + 1;
    }
    return params;
}

bool isAlreadyLoadedCastRequest(const WasmPlayerContext& ctx, std::string_view url) {
    if (ctx.player == nullptr) {
        return false;
    }

    const std::string file_name = libreshockwave::util::getFileName(url);
    const std::string lower_file_name = toLower(file_name);
    const bool cast_like_request = lower_file_name.ends_with(".cct") ||
                                 lower_file_name.ends_with(".cst") ||
                                 lower_file_name.find('.') == std::string::npos;
    if (!cast_like_request) {
        return false;
    }

    const std::string base_name = libreshockwave::util::getFileNameWithoutExtension(file_name);
    if (base_name.empty()) {
        return false;
    }

    for (const auto& [number, cast_lib] : ctx.player->castLibManager().castLibs()) {
        (void)number;
        if (cast_lib == nullptr) {
            continue;
        }

        const bool name_matches = !cast_lib->name().empty() && equalsIgnoreCase(cast_lib->name(), base_name);
        const std::string cast_file_base_name =
            libreshockwave::util::getFileNameWithoutExtension(libreshockwave::util::getFileName(cast_lib->fileName()));
        const bool file_matches = !cast_file_base_name.empty() && equalsIgnoreCase(cast_file_base_name, base_name);
        if (!name_matches && !file_matches) {
            continue;
        }

        if (!cast_lib->isExternal() && cast_lib->isLoaded()) {
            return true;
        }
        if (cast_lib->isExternal() && cast_lib->isFetched()) {
            return true;
        }
    }
    return false;
}

void applyTempo(WasmPlayerContext& ctx) {
    if (ctx.player == nullptr) {
        return;
    }
    const int base_tempo = ctx.file != nullptr && ctx.file->tempo() > 0 ? ctx.file->tempo() : 15;
    ctx.player->setTempo(ctx.tempo_override_enabled ? ctx.tempo_override : base_tempo);
}

void setError(WasmPlayerContext& ctx, std::string message) {
    ctx.last_error = std::move(message);
    ctx.last_status = ctx.last_error;
    queueDebugMessage(ctx, "error", ctx.last_error);
}

void clearError(WasmPlayerContext& ctx) {
    ctx.last_error.clear();
}

int renderCurrentFrame(WasmPlayerContext& ctx) {
    if (ctx.player == nullptr) {
        ctx.frame_rgba.clear();
        ctx.frame_width = 0;
        ctx.frame_height = 0;
        ctx.frame_background = 0;
        return 0;
    }

    const auto snapshot = ctx.player->frameSnapshot();
    auto frame = snapshot.renderPresentableFrame();
    ctx.frame_rgba = bitmapToRgba(frame, snapshot.background_color);
    ctx.frame_width = frame.width();
    ctx.frame_height = frame.height();
    ctx.frame_background = snapshot.background_color;
    return ctx.frame_rgba.empty() ? 0 : 1;
}

std::string frameInfoJson(WasmPlayerContext& ctx) {
    std::ostringstream out;
    const int current_frame = ctx.player != nullptr ? ctx.player->currentFrame() : 0;
    const int frame_count = ctx.player != nullptr ? ctx.player->frameCount() : 0;
    const int tempo = ctx.player != nullptr ? std::max(1, ctx.player->tempo()) : 15;
    const int base_tempo = ctx.player != nullptr ? ctx.player->baseTempo() : 15;
    const std::string state = ctx.player != nullptr ? std::string(libreshockwave::player::name(ctx.player->state())) : "NONE";
    int cursor_code = libreshockwave::player::CursorManager::ARROW_CURSOR;
    std::optional<std::array<int, 2>> cursor_reg;
    std::optional<Bitmap> cursor_bitmap;
    if (ctx.player != nullptr) {
        cursor_code = ctx.player->cursorManager().getCursorAtMouse();
        if (cursor_code == libreshockwave::player::CursorManager::CUSTOM_BITMAP_CURSOR) {
            cursor_reg = ctx.player->cursorManager().getCursorRegPoint();
            cursor_bitmap = ctx.player->cursorManager().getCursorBitmap();
        }
    }

    out << '{'
        << "\"width\":" << ctx.frame_width
        << ",\"height\":" << ctx.frame_height
        << ",\"checksum\":" << fnv1a32(ctx.frame_rgba)
        << ",\"background\":" << ctx.frame_background
        << ",\"current_frame\":" << current_frame
        << ",\"frameCount\":" << frame_count
        << ",\"tempo\":" << tempo
        << ",\"base_tempo\":" << base_tempo
        << ",\"tempoOverride\":" << (ctx.tempo_override_enabled ? ctx.tempo_override : 0)
        << ",\"state\":";
    appendJsonString(out, state);
    out << ",\"status\":";
    appendJsonString(out, ctx.last_status);
    out << ",\"error\":";
    appendJsonString(out, ctx.last_error);
    out << ",\"cursor\":{\"code\":" << cursor_code << ",\"css\":";
    appendJsonString(out, cursorCssForCode(cursor_code));
    if (cursor_reg.has_value()) {
        out << ",\"hotX\":" << (*cursor_reg)[0] << ",\"hotY\":" << (*cursor_reg)[1];
    }
    if (cursor_bitmap.has_value()) {
        const auto rgba = bitmapToRgba(*cursor_bitmap, 0xFFFFFF);
        if (!rgba.empty()) {
            out << ",\"width\":" << cursor_bitmap->width()
                << ",\"height\":" << cursor_bitmap->height()
                << ",\"pixels\":\"" << base64Encode(rgba) << '"';
        }
    }
    out << "}}";
    return out.str();
}

std::string frameSpritesJson(WasmPlayerContext& ctx) {
    struct ColorBounds {
        int count = 0;
        int min_x = 0;
        int min_y = 0;
        int max_x = 0;
        int max_y = 0;
    };

    std::ostringstream out;
    auto append_bitmap_summary = [](std::ostringstream& stream, const char* key, const Bitmap& bitmap) {
        int min_x = bitmap.width();
        int min_y = bitmap.height();
        int max_x = -1;
        int max_y = -1;
        int opaque_pixels = 0;
        int translucent_pixels = 0;
        std::unordered_map<std::uint32_t, ColorBounds> color_bounds;
        for (int y = 0; y < bitmap.height(); ++y) {
            for (int x = 0; x < bitmap.width(); ++x) {
                const auto pixel = bitmap.getPixel(x, y);
                const int alpha = static_cast<int>((pixel >> 24U) & 0xFFU);
                if (alpha == 0) {
                    continue;
                }
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
                if (alpha == 255) {
                    ++opaque_pixels;
                } else {
                    ++translucent_pixels;
                }
                const std::uint32_t rgb = pixel & 0x00FFFFFFU;
                auto& bounds = color_bounds[rgb];
                if (bounds.count == 0) {
                    bounds.min_x = bounds.max_x = x;
                    bounds.min_y = bounds.max_y = y;
                } else {
                    bounds.min_x = std::min(bounds.min_x, x);
                    bounds.min_y = std::min(bounds.min_y, y);
                    bounds.max_x = std::max(bounds.max_x, x);
                    bounds.max_y = std::max(bounds.max_y, y);
                }
                ++bounds.count;
            }
        }
        stream << ",\"" << key << "\":{\"width\":" << bitmap.width()
               << ",\"height\":" << bitmap.height()
               << ",\"bitDepth\":" << bitmap.bitDepth()
               << ",\"palette_indices\":" << (bitmap.paletteIndices().has_value() ? "true" : "false")
               << ",\"nativeAlpha\":" << (bitmap.isNativeAlpha() ? "true" : "false")
               << ",\"scriptModified\":" << (bitmap.isScriptModified() ? "true" : "false")
               << ",\"scriptFillBacking\":" << (bitmap.hasScriptFillBacking() ? "true" : "false")
               << ",\"preserveScriptFillBacking\":" << (bitmap.preservesScriptFillBacking() ? "true" : "false")
               << ",\"rectangularMedia\":" << (bitmap.isRectangularMedia() ? "true" : "false")
               << ",\"textRendered\":" << (bitmap.isTextRendered() ? "true" : "false")
               << ",\"transparent\":" << (bitmap.hasTransparentPixels() ? "true" : "false")
               << ",\"translucent\":" << (bitmap.hasTranslucentPixels() ? "true" : "false")
               << ",\"opaquePixels\":" << opaque_pixels
               << ",\"translucentPixels\":" << translucent_pixels;
        if (max_x >= 0) {
            stream << ",\"alphaBounds\":{\"left\":" << min_x
                   << ",\"top\":" << min_y
                   << ",\"right\":" << (max_x + 1)
                   << ",\"bottom\":" << (max_y + 1)
                   << '}';
        } else {
            stream << ",\"alphaBounds\":null";
        }
        std::vector<std::pair<std::uint32_t, ColorBounds>> colors(color_bounds.begin(), color_bounds.end());
        std::sort(colors.begin(), colors.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.second.count != rhs.second.count) {
                return lhs.second.count > rhs.second.count;
            }
            return lhs.first < rhs.first;
        });
        stream << ",\"topColors\":[";
        const std::size_t color_limit = std::min<std::size_t>(colors.size(), 16);
        for (std::size_t index = 0; index < color_limit; ++index) {
            if (index > 0) {
                stream << ',';
            }
            const auto& [rgb, bounds] = colors[index];
            stream << "{\"rgb\":" << rgb
                   << ",\"count\":" << bounds.count
                   << ",\"bounds\":{\"left\":" << bounds.min_x
                   << ",\"top\":" << bounds.min_y
                   << ",\"right\":" << (bounds.max_x + 1)
                   << ",\"bottom\":" << (bounds.max_y + 1)
                   << "}}";
        }
        stream << ']';
        const std::array<std::pair<int, int>, 12> probe_points{{
            {0, 0}, {1, 1}, {4, 1}, {5, 1}, {5, 2}, {5, 3},
            {5, 4}, {5, 5}, {bitmap.width() / 2, 1}, {bitmap.width() / 2, 2},
            {std::max(0, bitmap.width() - 6), 2}, {std::max(0, bitmap.width() - 2), 1}
        }};
        stream << ",\"probePixels\":[";
        bool first_probe = true;
        const auto palette_indices = bitmap.paletteIndices();
        for (const auto& [x, y] : probe_points) {
            if (x < 0 || y < 0 || x >= bitmap.width() || y >= bitmap.height()) {
                continue;
            }
            if (!first_probe) {
                stream << ',';
            }
            first_probe = false;
            const auto pixel = bitmap.getPixel(x, y);
            stream << "{\"x\":" << x
                   << ",\"y\":" << y
                   << ",\"argb\":" << pixel;
            if (palette_indices.has_value()) {
                const auto offset = static_cast<std::size_t>(y * bitmap.width() + x);
                if (offset < palette_indices->size()) {
                    stream << ",\"paletteIndex\":" << static_cast<int>((*palette_indices)[offset]);
                }
            }
            stream << '}';
        }
        stream << "]}";
    };

    out << "{\"sprites\":[";
    if (ctx.player != nullptr) {
        const auto& sprites = ctx.player->stageRenderer().lastBakedSprites();
        bool first = true;
        for (const auto& sprite : sprites) {
            if (!first) {
                out << ',';
            }
            first = false;
            const auto baked = sprite.bakedBitmap();
            out << "{\"channel\":" << sprite.channel()
                << ",\"x\":" << sprite.x()
                << ",\"y\":" << sprite.y()
                << ",\"width\":" << sprite.width()
                << ",\"height\":" << sprite.height()
                << ",\"locZ\":" << sprite.locZ()
                << ",\"visible\":" << (sprite.isVisible() ? "true" : "false")
                << ",\"type\":";
            appendJsonString(out, std::string(libreshockwave::player::render::pipeline::name(sprite.type())));
            out << ",\"castMemberId\":" << sprite.castMemberId()
                << ",\"member_name\":";
            if (auto member_name = sprite.memberName(); member_name.has_value()) {
                appendJsonString(out, *member_name);
            } else {
                out << "null";
            }
            out << ",\"ink\":" << sprite.ink()
                << ",\"blend\":" << sprite.blend()
                << ",\"flipH\":" << (sprite.isFlipH() ? "true" : "false")
                << ",\"flipV\":" << (sprite.isFlipV() ? "true" : "false")
                << ",\"rotation\":" << sprite.rotation()
                << ",\"skew\":" << sprite.skew()
                << ",\"directorMirrorH\":" << (sprite.hasDirectorHorizontalMirror() ? "true" : "false")
                << ",\"foreColor\":" << sprite.foreColor()
                << ",\"backColor\":" << sprite.backColor();
            const auto sprite_state = ctx.player->stageRenderer().spriteRegistry().get(sprite.channel());
            out << ",\"scriptInstanceCount\":" << (sprite_state != nullptr ? sprite_state->scriptInstanceList().size() : 0)
                << ",\"hasKeyDownHandler\":"
                << (ctx.player->eventDispatcher().spriteHasHandler(sprite.channel(), handlerName(PlayerEvent::KeyDown)) ? "true" : "false")
                << ",\"hasMouseDownHandler\":"
                << (ctx.player->eventDispatcher().spriteHasHandler(sprite.channel(), handlerName(PlayerEvent::MouseDown)) ? "true" : "false")
                << ",\"hasMouseUpHandler\":"
                << (ctx.player->eventDispatcher().spriteHasHandler(sprite.channel(), handlerName(PlayerEvent::MouseUp)) ? "true" : "false")
                << ",\"hasBakedBitmap\":" << (baked != nullptr ? "true" : "false");
            if (baked != nullptr) {
                int min_x = baked->width();
                int min_y = baked->height();
                int max_x = -1;
                int max_y = -1;
                int opaque_pixels = 0;
                int translucent_pixels = 0;
                std::unordered_map<std::uint32_t, ColorBounds> color_bounds;
                for (int y = 0; y < baked->height(); ++y) {
                    for (int x = 0; x < baked->width(); ++x) {
                        const auto pixel = baked->getPixel(x, y);
                        const int alpha = static_cast<int>((pixel >> 24U) & 0xFFU);
                        if (alpha == 0) {
                            continue;
                        }
                        min_x = std::min(min_x, x);
                        min_y = std::min(min_y, y);
                        max_x = std::max(max_x, x);
                        max_y = std::max(max_y, y);
                        if (alpha == 255) {
                            ++opaque_pixels;
                        } else {
                            ++translucent_pixels;
                        }
                        const std::uint32_t rgb = pixel & 0x00FFFFFFU;
                        auto& bounds = color_bounds[rgb];
                        if (bounds.count == 0) {
                            bounds.min_x = bounds.max_x = x;
                            bounds.min_y = bounds.max_y = y;
                        } else {
                            bounds.min_x = std::min(bounds.min_x, x);
                            bounds.min_y = std::min(bounds.min_y, y);
                            bounds.max_x = std::max(bounds.max_x, x);
                            bounds.max_y = std::max(bounds.max_y, y);
                        }
                        ++bounds.count;
                    }
                }
                out << ",\"bitmap\":{\"width\":" << baked->width()
                    << ",\"height\":" << baked->height()
                    << ",\"bitDepth\":" << baked->bitDepth()
                    << ",\"palette_indices\":" << (baked->paletteIndices().has_value() ? "true" : "false")
                    << ",\"nativeAlpha\":" << (baked->isNativeAlpha() ? "true" : "false")
                    << ",\"scriptModified\":" << (baked->isScriptModified() ? "true" : "false")
                    << ",\"scriptFillBacking\":" << (baked->hasScriptFillBacking() ? "true" : "false")
                    << ",\"preserveScriptFillBacking\":" << (baked->preservesScriptFillBacking() ? "true" : "false")
                    << ",\"rectangularMedia\":" << (baked->isRectangularMedia() ? "true" : "false")
                    << ",\"textRendered\":" << (baked->isTextRendered() ? "true" : "false")
                    << ",\"transparent\":" << (baked->hasTransparentPixels() ? "true" : "false")
                    << ",\"translucent\":" << (baked->hasTranslucentPixels() ? "true" : "false")
                    << ",\"opaquePixels\":" << opaque_pixels
                    << ",\"translucentPixels\":" << translucent_pixels;
                if (max_x >= 0) {
                    out << ",\"alphaBounds\":{\"left\":" << min_x
                        << ",\"top\":" << min_y
                        << ",\"right\":" << (max_x + 1)
                        << ",\"bottom\":" << (max_y + 1)
                        << '}';
                } else {
                    out << ",\"alphaBounds\":null";
                }
                std::vector<std::pair<std::uint32_t, ColorBounds>> colors(color_bounds.begin(), color_bounds.end());
                std::sort(colors.begin(), colors.end(), [](const auto& lhs, const auto& rhs) {
                    if (lhs.second.count != rhs.second.count) {
                        return lhs.second.count > rhs.second.count;
                    }
                    return lhs.first < rhs.first;
                });
                out << ",\"topColors\":[";
                const std::size_t color_limit = std::min<std::size_t>(colors.size(), 16);
                for (std::size_t index = 0; index < color_limit; ++index) {
                    if (index > 0) {
                        out << ',';
                    }
                    const auto& [rgb, bounds] = colors[index];
                    out << "{\"rgb\":" << rgb
                        << ",\"count\":" << bounds.count
                        << ",\"bounds\":{\"left\":" << bounds.min_x
                        << ",\"top\":" << bounds.min_y
                        << ",\"right\":" << (bounds.max_x + 1)
                        << ",\"bottom\":" << (bounds.max_y + 1)
                        << "}}";
                }
                out << ']';
                const std::array<std::pair<int, int>, 12> probe_points{{
                    {0, 0}, {1, 1}, {4, 1}, {5, 1}, {5, 2}, {5, 3},
                    {5, 4}, {5, 5}, {baked->width() / 2, 1}, {baked->width() / 2, 2},
                    {std::max(0, baked->width() - 6), 2}, {std::max(0, baked->width() - 2), 1}
                }};
                out << ",\"probePixels\":[";
                bool first_probe = true;
                const auto palette_indices = baked->paletteIndices();
                for (const auto& [x, y] : probe_points) {
                    if (x < 0 || y < 0 || x >= baked->width() || y >= baked->height()) {
                        continue;
                    }
                    if (!first_probe) {
                        out << ',';
                    }
                    first_probe = false;
                    const auto pixel = baked->getPixel(x, y);
                    out << "{\"x\":" << x
                        << ",\"y\":" << y
                        << ",\"argb\":" << pixel;
                    if (palette_indices.has_value()) {
                        const auto offset = static_cast<std::size_t>(y * baked->width() + x);
                        if (offset < palette_indices->size()) {
                            out << ",\"paletteIndex\":" << static_cast<int>((*palette_indices)[offset]);
                        }
                    }
                    out << '}';
                }
                out << ']';
                out << '}';
            }
            if (auto authored_member = sprite.castMember(); authored_member != nullptr && authored_member->isTextXtra()) {
                out << ",\"authoredXmed\":";
                auto* file = authored_member->file();
                if (file != nullptr) {
                    auto* non_const_file = const_cast<MovieSource*>(file);
                    auto chunk = std::const_pointer_cast<libreshockwave::chunks::CastMemberChunk>(authored_member);
                    std::optional<libreshockwave::cast::XmedStyledText> styled;
                    if (auto asset = non_const_file->castMemberAssetForChunk(chunk); asset) {
                        styled = non_const_file->xmedStyledTextForMember(asset);
                    }
                    if (styled.has_value()) {
                        out << "{\"alignment\":";
                        appendJsonString(out, styled->alignment);
                        out << ",\"paragraphStyleIndex\":" << styled->primary_paragraph_style_index
                            << ",\"paragraphAlignmentCode\":" << styled->primary_paragraph_alignment_code
                            << ",\"paragraphStyleCount\":" << styled->paragraph_style_count
                            << ",\"fontName\":";
                        appendJsonString(out, styled->font_name);
                        out << ",\"fontSize\":" << styled->font_size
                            << ",\"fontStyle\":";
                        appendJsonString(out, styled->fontStyleString());
                        out << ",\"width\":" << styled->width
                            << ",\"height\":" << styled->height
                            << ",\"wordWrap\":" << (styled->word_wrap ? "true" : "false")
                            << ",\"antialias\":" << (styled->antialias ? "true" : "false")
                            << ",\"fixedLineSpace\":" << styled->fixed_line_space
                            << ",\"text\":";
                        appendJsonString(out, styled->text);
                        out << '}';
                    } else {
                        out << "null";
                    }
                } else {
                    out << "null";
                }
            }
            if (auto dynamic_member = sprite.dynamicMember(); dynamic_member != nullptr) {
                out << ",\"dynamic_member\":{\"id\":" << dynamic_member->id()
                    << ",\"castLib\":" << dynamic_member->castLib()
                    << ",\"memberNum\":" << dynamic_member->memberNum()
                    << ",\"name\":";
                appendJsonString(out, dynamic_member->name());
                out << ",\"width\":" << dynamic_member->width()
                    << ",\"height\":" << dynamic_member->height()
                    << ",\"regX\":" << dynamic_member->regX()
                    << ",\"regY\":" << dynamic_member->regY()
                    << ",\"runtimeDynamic\":" << (dynamic_member->isRuntimeDynamic() ? "true" : "false")
                    << ",\"hasRuntimeBitmap\":" << (dynamic_member->runtimeBitmap() != nullptr ? "true" : "false");
                if (dynamic_member->isTextLike()) {
                    out << ",\"text\":";
                    appendJsonString(out,
                                     ctx.player->castLibManager()
                                         .getMemberProp(dynamic_member->castLib(), dynamic_member->memberNum(), "text")
                                         .stringValue());
                    out << ",\"textFont\":";
                    appendJsonString(out, dynamic_member->textFont());
                    out << ",\"textFontSize\":" << dynamic_member->textFontSize()
                        << ",\"textFontStyle\":";
                    appendJsonString(out, dynamic_member->textFontStyle());
                    out << ",\"textAlignment\":";
                    appendJsonString(out, dynamic_member->textAlignment());
                    out << ",\"textRect\":{\"left\":" << dynamic_member->textRectLeft()
                        << ",\"top\":" << dynamic_member->textRectTop()
                        << ",\"right\":" << dynamic_member->textRectRight()
                        << ",\"bottom\":" << dynamic_member->textRectBottom()
                        << "},\"textWordWrap\":" << (dynamic_member->textWordWrap() ? "true" : "false")
                        << ",\"textAntialias\":" << (dynamic_member->textAntialias() ? "true" : "false")
                        << ",\"textFixedLineSpace\":" << dynamic_member->textFixedLineSpace()
                        << ",\"textTopSpacing\":" << dynamic_member->textTopSpacing();
                }
                out << ",\"memberType\":";
                appendJsonString(out, std::string(::libreshockwave::cast::name(dynamic_member->memberType())));
                out << '}';
                if (auto live = dynamic_member->runtimeBitmap(); live != nullptr) {
                    append_bitmap_summary(out, "liveBitmap", *live);
                }
            } else {
                out << ",\"dynamic_member\":null";
            }
            out << '}';
        }
    }
    out << "]}";
    return out.str();
}

std::string fetchRequestsJson(const std::vector<QueuedNetProvider::PendingRequest>& requests) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& request : requests) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"taskId\":" << request.task_id << ",\"url\":";
        appendJsonString(out, request.url);
        out << ",\"method\":";
        appendJsonString(out, request.method);
        out << ",\"postData\":";
        if (request.post_data.has_value()) {
            appendJsonString(out, *request.post_data);
        } else {
            out << "null";
        }
        out << ",\"fallbacks\":[";
        for (std::size_t index = 0; index < request.fallbacks.size(); ++index) {
            if (index > 0) {
                out << ',';
            }
            appendJsonString(out, request.fallbacks[index]);
        }
        out << "]}";
    }
    out << ']';
    return out.str();
}

std::optional<std::pair<std::string, int>> parseHostPort(std::string_view value) {
    const std::string trimmed_value = trim(value);
    const std::size_t separator = trimmed_value.rfind(':');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= trimmed_value.size()) {
        return std::nullopt;
    }
    const std::string host(trim(std::string_view(trimmed_value).substr(0, separator)));
    const std::string port_text(trim(std::string_view(trimmed_value).substr(separator + 1)));
    if (host.empty() || port_text.empty()) {
        return std::nullopt;
    }
    int port = 0;
    for (const unsigned char ch : port_text) {
        if (!std::isdigit(ch)) {
            return std::nullopt;
        }
        port = port * 10 + static_cast<int>(ch - '0');
        if (port > 65535) {
            return std::nullopt;
        }
    }
    if (port <= 0) {
        return std::nullopt;
    }
    return std::pair{host, port};
}

std::optional<std::pair<std::string, int>> externalParamHostPort(
    const std::vector<std::pair<std::string, std::string>>& params) {
    for (const auto& [name, value] : params) {
        (void)name;
        if (const auto endpoint = parseHostPort(value)) {
            return endpoint;
        }
    }
    return std::nullopt;
}

std::string multiuserRequestsJson(const std::vector<QueuedMultiuserBridge::PendingRequest>& requests,
                                  const std::vector<std::pair<std::string, std::string>>& external_params) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    const auto fallback_endpoint = externalParamHostPort(external_params);
    for (const auto& request : requests) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"type\":" << request.type << ",\"instanceId\":" << request.instance_id;
        if (request.type == QueuedMultiuserBridge::REQ_CONNECT) {
            std::string host = request.host;
            int port = request.port;
            if ((host.empty() || equalsIgnoreCase(host, "VOID")) && port <= 0 && fallback_endpoint) {
                host = fallback_endpoint->first;
                port = fallback_endpoint->second;
            }
            out << ",\"host\":";
            appendJsonString(out, host);
            out << ",\"port\":" << port;
        } else if (request.type == QueuedMultiuserBridge::REQ_SEND) {
            out << ",\"senderID\":";
            appendJsonString(out, request.sender_id);
            out << ",\"subject\":";
            appendJsonString(out, request.subject);
            out << ",\"content\":";
            appendJsonString(out, request.content);
            out << ",\"bytes\":\"" << base64Encode(request.wireBytes()) << '"';
        }
        out << '}';
    }
    out << ']';
    return out.str();
}

std::string audioCommandsJson(const std::vector<QueuedAudioBackend::SoundCommand>& commands) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& command : commands) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"action\":";
        appendJsonString(out, command.action);
        out << ",\"channel\":" << command.channel_num
            << ",\"loopCount\":" << command.loop_count
            << ",\"volume\":" << command.volume;
        if (command.format.has_value()) {
            out << ",\"format\":";
            appendJsonString(out, *command.format);
        }
        if (command.audio_data.has_value()) {
            out << ",\"byteLength\":" << command.audio_data->size()
                << ",\"data\":\"" << base64Encode(*command.audio_data) << '"';
        }
        out << '}';
    }
    out << ']';
    return out.str();
}

std::string jpegDecodeRequestsJson(QueuedJpegDecoder& decoder) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (int index = 0; index < decoder.pendingCount(); ++index) {
        const int id = decoder.pendingId(index);
        if (id == 0 || decoder.prepareData(id) <= 0 || decoder.currentData() == nullptr) {
            continue;
        }
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"id\":" << id
            << ",\"data\":\"" << base64Encode(*decoder.currentData()) << "\"}";
    }
    out << ']';
    return out.str();
}

void processQueuedInput(WasmPlayerContext& ctx) {
    if (ctx.player == nullptr) {
        return;
    }
    (void)ctx.player->inputHandler().processInputEvents();
}

const char* scratch(WasmPlayerContext& ctx, std::string value) {
    ctx.scratch = std::move(value);
    return ctx.scratch.c_str();
}

class WasmScriptDeadlineGuard {
public:
    explicit WasmScriptDeadlineGuard(Player* player)
        : player_(player) {
        if (player_ == nullptr) {
            return;
        }
        previous_deadline_ms_ = player_->vm().tickDeadlineMs();
        previous_deadline_ = player_->vm().tickDeadline();
        if (previous_deadline_ms_ == 0) {
            player_->vm().setTickDeadlineMs(DEFAULT_WASM_SCRIPT_DEADLINE_MS);
            changed_deadline_ms_ = true;
        }
        if (previous_deadline_ == 0) {
            player_->vm().armTickDeadline();
            changed_deadline_ = true;
        }
    }

    ~WasmScriptDeadlineGuard() {
        if (player_ == nullptr) {
            return;
        }
        if (changed_deadline_) {
            player_->vm().setTickDeadline(previous_deadline_);
        }
        if (changed_deadline_ms_) {
            player_->vm().setTickDeadlineMs(previous_deadline_ms_);
        }
    }

private:
    Player* player_{nullptr};
    std::int64_t previous_deadline_ms_{0};
    std::int64_t previous_deadline_{0};
    bool changed_deadline_ms_{false};
    bool changed_deadline_{false};
};

void processHostXtraCallbacks(WasmPlayerContext& ctx) {
    if (ctx.player == nullptr || ctx.player->isVmRunning()) {
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    WasmScriptDeadlineGuard script_deadline(ctx.player.get());
    ctx.player->xtraManager().tickAll();
    ctx.player->vm().flushDeferredTasks();
    queueSlowOperationMessage(ctx, "host xtra callbacks", started);
}

std::string operationError(std::string_view operation, std::string_view detail) {
    std::string message(operation);
    if (!detail.empty()) {
        message += ": ";
        message += detail;
    }
    return message;
}

template <typename Callback>
void guardedVoid(WasmPlayerContext& ctx, std::string_view operation, Callback&& callback) {
    const auto start = std::chrono::steady_clock::now();
    try {
        callback();
        queueSlowOperationMessage(ctx, operation, start);
    } catch (const std::exception& error) {
        setError(ctx, operationError(operation, error.what()));
    } catch (...) {
        setError(ctx, operationError(operation, "unknown error"));
    }
}

template <typename Result, typename Callback>
Result guardedResult(WasmPlayerContext& ctx,
                     std::string_view operation,
                     Result fallback,
                     Callback&& callback) {
    const auto start = std::chrono::steady_clock::now();
    try {
        auto result = callback();
        queueSlowOperationMessage(ctx, operation, start);
        return result;
    } catch (const std::exception& error) {
        setError(ctx, operationError(operation, error.what()));
    } catch (...) {
        setError(ctx, operationError(operation, "unknown error"));
    }
    return fallback;
}

} // namespace

// Internal accessors for the debugger bridge (debugger/cpp/debugger_bridge.cpp).
// They are compiled into the same .wasm module so they resolve at link time.

libreshockwave::player::Player* lswInternalGetPlayer(int handle) {
    const auto found = contexts.find(handle);
    return found == contexts.end() ? nullptr : found->second->player.get();
}

std::shared_ptr<MovieSource> lswInternalGetFile(int handle) {
    const auto found = contexts.find(handle);
    return found == contexts.end() ? nullptr : found->second->file;
}

const char* lswInternalJsonScratch(int handle, std::string value) {
    const auto found = contexts.find(handle);
    if (found == contexts.end()) {
        return "";
    }
    auto& ctx = *found->second;
    ctx.scratch = std::move(value);
    return ctx.scratch.c_str();
}

extern "C" {

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_create") int lswCreate() {
    const int handle = next_handle++;
    contexts.emplace(handle, std::make_unique<WasmPlayerContext>());
    return handle;
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_destroy") void lswDestroy(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr) {
        if (ctx->player != nullptr) {
            guardedVoid(*ctx, "destroy", [&]() {
                ctx->player->shutdown();
            });
        }
        ctx->jpeg_decoder.reset();
    }
    contexts.erase(handle);
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_load_movie") int lswLoadMovie(int handle,
                                        const char* source,
                                        const std::uint8_t* bytes,
                                        int byte_count,
                                        const char* params_text) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || bytes == nullptr || byte_count <= 0) {
        return 0;
    }

    try {
        if (ctx->player != nullptr) {
            ctx->player->shutdown();
        }
        ctx->player.reset();
        ctx->audio_backend.reset();
        ctx->jpeg_decoder.reset();
        ctx->multiuser_bridge.reset();
        ctx->net_provider.reset();
        ctx->file.reset();
        ctx->frame_rgba.clear();
        ctx->frame_width = 0;
        ctx->frame_height = 0;
        clearError(*ctx);

        ctx->movie_source = source != nullptr ? std::string(source) : std::string();
        if (params_text != nullptr) {
            ctx->external_params = parseParams(params_text);
        }

        std::vector<std::uint8_t> data(bytes, bytes + byte_count);
        auto file = MovieSource::load(ctx->movie_source, data);
        file->setBasePath(sourceDirectory(ctx->movie_source));

        auto net_provider = std::make_unique<QueuedNetProvider>(ctx->movie_source);
        auto multiuser_bridge = std::make_unique<QueuedMultiuserBridge>();
        auto audio_backend = std::make_unique<QueuedAudioBackend>();
        auto jpeg_decoder = std::make_unique<QueuedJpegDecoder>(file.get());
        jpeg_decoder->install();
        auto player = std::make_unique<Player>(file, net_provider.get());
        player->registerMultiuserXtra(*multiuser_bridge);
        player->setAudioBackend(audio_backend.get());
        player->setExternalParams(ctx->external_params);
        player->builtinContext().output_handler = [ctx](std::string_view kind, const std::string& text) {
            if (ctx != nullptr) {
                queueDebugMessage(*ctx, std::string(kind), text);
            }
        };
        player->setErrorListener([ctx](std::string_view message, std::string_view detail) {
            std::string error(message);
            if (!detail.empty()) {
                error += ": ";
                error += detail;
            }
            setError(*ctx, std::move(error));
        });
        player->movieProperties().setGotoNetPageHandler([ctx](const std::string& url, const std::string& target) {
            (void)target;
            if (ctx != nullptr && ctx->net_provider != nullptr) {
                (void)ctx->net_provider->beginMovieNavigation(url);
            }
        });
        player->movieProperties().setGotoNetMovieHandler([ctx](const std::string& url) {
            if (ctx == nullptr || ctx->net_provider == nullptr) {
                return -1;
            }
            return ctx->net_provider->beginMovieNavigation(url);
        });
        net_provider->setFetchCompleteCallback([ctx](const std::string& url,
                                                    const std::vector<std::uint8_t>& data) {
            if (ctx != nullptr && ctx->player != nullptr) {
                ctx->player->onNetFetchComplete(url, data);
            }
        });
        net_provider->setSatisfiedFetchPredicate([ctx](std::string_view url) {
            return ctx != nullptr && isAlreadyLoadedCastRequest(*ctx, url);
        });

        ctx->file = std::move(file);
        ctx->net_provider = std::move(net_provider);
        ctx->multiuser_bridge = std::move(multiuser_bridge);
        ctx->audio_backend = std::move(audio_backend);
        ctx->jpeg_decoder = std::move(jpeg_decoder);
        ctx->player = std::move(player);
        ctx->player->setDebugEnabled(ctx->debug_playback_enabled);
        ctx->player->setSlowHandlerWarningThresholdMs(ctx->slow_handler_warning_ms);
        applyTempo(*ctx);

        if (ctx->preload_casts) {
            (void)ctx->player->preloadAllCasts();
        }
        ctx->last_status = "Loaded " + ctx->movie_source;
        (void)renderCurrentFrame(*ctx);
        return 1;
    } catch (const std::exception& error) {
        ctx->jpeg_decoder.reset();
        setError(*ctx, error.what());
        return 0;
    } catch (...) {
        ctx->jpeg_decoder.reset();
        setError(*ctx, "Unknown load error");
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_set_external_params") void lswSetExternalParams(int handle, const char* params_text) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr) {
        return;
    }
    guardedVoid(*ctx, "set external params", [&]() {
        ctx->external_params = parseParams(params_text);
        if (ctx->player != nullptr) {
            ctx->player->setExternalParams(ctx->external_params);
        }
    });
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_set_preload_casts") void lswSetPreloadCasts(int handle, int preload_casts) {
    if (auto* ctx = getContext(handle)) {
        ctx->preload_casts = preload_casts != 0;
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_set_debug_playback_enabled") void lswSetDebugPlaybackEnabled(int handle, int enabled) {
    if (auto* ctx = getContext(handle)) {
        ctx->debug_playback_enabled = enabled != 0;
        if (ctx->player != nullptr) {
            guardedVoid(*ctx, "set debug playback", [&]() {
                ctx->player->setDebugEnabled(ctx->debug_playback_enabled);
            });
        }
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_set_slow_handler_warning_ms") void lswSetSlowHandlerWarningMs(int handle, int milliseconds) {
    if (auto* ctx = getContext(handle)) {
        ctx->slow_handler_warning_ms = std::max(0, milliseconds);
        if (ctx->player != nullptr) {
            guardedVoid(*ctx, "set slow handler warning threshold", [&]() {
                ctx->player->setSlowHandlerWarningThresholdMs(ctx->slow_handler_warning_ms);
            });
        }
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_set_tempo_override") void lswSetTempoOverride(int handle, int tempo) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr) {
        return;
    }
    guardedVoid(*ctx, "set tempo override", [&]() {
        ctx->tempo_override_enabled = tempo > 0;
        ctx->tempo_override = tempo > 0 ? tempo : 0;
        applyTempo(*ctx);
    });
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_tempo") int lswTempo(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        return std::max(1, ctx->player->tempo());
    }
    return 15;
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_base_tempo") int lswBaseTempo(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        return std::max(1, ctx->player->baseTempo());
    }
    return 15;
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_play") void lswPlay(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "play", [&]() {
            if (ctx->player->state() == PlayerState::Stopped) {
                ctx->player->play();
            } else {
                ctx->player->resume();
            }
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_pause") void lswPause(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "pause", [&]() {
            ctx->player->pause();
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_stop") void lswStop(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "stop", [&]() {
            ctx->player->stop();
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_tick") int lswTick(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->player == nullptr) {
        return 0;
    }
    try {
        clearError(*ctx);
        const bool keep_going = ctx->player->tick();
        (void)renderCurrentFrame(*ctx);

        // Return 2 when the debug controller paused execution so the worker
        // can stop the tick loop until the user resumes.
        auto debug_ctrl = ctx->player->getDebugController();
        if (debug_ctrl != nullptr && debug_ctrl->isPaused()) {
            return 2;
        }
        return keep_going ? 1 : 0;
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return 0;
    } catch (...) {
        setError(*ctx, "Unknown tick error");
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_render_frame") int lswRenderFrame(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr) {
        return 0;
    }
    try {
        return renderCurrentFrame(*ctx);
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return 0;
    } catch (...) {
        setError(*ctx, "Unknown render error");
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_frame_pixels") const std::uint8_t* lswFramePixels(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->frame_rgba.empty()) {
        return nullptr;
    }
    return ctx->frame_rgba.data();
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_frame_byte_length") int lswFrameByteLength(int handle) {
    auto* ctx = getContext(handle);
    return ctx == nullptr ? 0 : static_cast<int>(ctx->frame_rgba.size());
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_frame_width") int lswFrameWidth(int handle) {
    auto* ctx = getContext(handle);
    return ctx == nullptr ? 0 : ctx->frame_width;
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_frame_height") int lswFrameHeight(int handle) {
    auto* ctx = getContext(handle);
    return ctx == nullptr ? 0 : ctx->frame_height;
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_frame_info_json") const char* lswFrameInfoJson(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr) {
        return "";
    }
    try {
        return scratch(*ctx, frameInfoJson(*ctx));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        std::ostringstream out;
        out << "{\"width\":" << ctx->frame_width
            << ",\"height\":" << ctx->frame_height
            << ",\"tempo\":" << (ctx->player != nullptr ? std::max(1, ctx->player->tempo()) : 15)
            << ",\"error\":";
        appendJsonString(out, ctx->last_error);
        out << ",\"cursor\":{\"code\":-1,\"css\":\"default\"}}";
        return scratch(*ctx, out.str());
    } catch (...) {
        setError(*ctx, "Unknown frame info error");
        std::ostringstream out;
        out << "{\"width\":" << ctx->frame_width
            << ",\"height\":" << ctx->frame_height
            << ",\"tempo\":" << (ctx->player != nullptr ? std::max(1, ctx->player->tempo()) : 15)
            << ",\"error\":";
        appendJsonString(out, ctx->last_error);
        out << ",\"cursor\":{\"code\":-1,\"css\":\"default\"}}";
        return scratch(*ctx, out.str());
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_frame_sprites_json") const char* lswFrameSpritesJson(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr) {
        return "{\"sprites\":[]}";
    }
    try {
        return scratch(*ctx, frameSpritesJson(*ctx));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "{\"sprites\":[]}";
    } catch (...) {
        setError(*ctx, "Unknown frame sprites error");
        return "{\"sprites\":[]}";
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_image_operation_trace_json") const char* lswImageOperationTraceJson(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr) {
        return "{\"events\":[]}";
    }
    try {
        return scratch(*ctx, libreshockwave::lingo::vm::imageOperationTraceJson());
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "{\"events\":[]}";
    } catch (...) {
        setError(*ctx, "Unknown image operation trace error");
        return "{\"events\":[]}";
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_clear_image_operation_trace") void lswClearImageOperationTrace(int handle) {
    if (getContext(handle) != nullptr) {
        libreshockwave::lingo::vm::clearImageOperationTrace();
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_fire_test_error") int lswFireTestError(int handle, const char* message) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->player == nullptr) {
        return 0;
    }
    return guardedResult(*ctx, "fire test error", 0, [&]() {
        const std::string text = message != nullptr && *message != '\0'
            ? std::string(message)
            : std::string("LibreShockwave test error");
        return ctx->player->fireTestError(text) ? 1 : 0;
    });
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_last_error") const char* lswLastError(int handle) {
    auto* ctx = getContext(handle);
    return ctx == nullptr ? "" : ctx->last_error.c_str();
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_poll_debug_messages") const char* lswPollDebugMessages(int handle) {
    auto* ctx = getContext(handle);
    return ctx == nullptr ? "[]" : scratch(*ctx, debugMessagesJson(*ctx));
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_drain_debug_messages") void lswDrainDebugMessages(int handle) {
    if (auto* ctx = getContext(handle)) {
        ctx->debug_messages.clear();
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_poll_fetch_requests") const char* lswPollFetchRequests(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->net_provider == nullptr) {
        return "[]";
    }
    try {
        return scratch(*ctx, fetchRequestsJson(ctx->net_provider->pendingRequests()));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "[]";
    } catch (...) {
        setError(*ctx, "Unknown fetch request poll error");
        return "[]";
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_drain_fetch_requests") void lswDrainFetchRequests(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->net_provider != nullptr) {
        guardedVoid(*ctx, "drain fetch requests", [&]() {
            ctx->net_provider->drainPendingRequests();
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_poll_jpeg_decode_requests") const char* lswPollJpegDecodeRequests(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->jpeg_decoder == nullptr) {
        return "[]";
    }
    try {
        return scratch(*ctx, jpegDecodeRequestsJson(*ctx->jpeg_decoder));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "[]";
    } catch (...) {
        setError(*ctx, "Unknown JPEG decode request poll error");
        return "[]";
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_jpeg_decode_complete") void lswJpegDecodeComplete(int handle,
                                                    int id,
                                                    int width,
                                                    int height,
                                                    const std::uint8_t* rgba,
                                                    int byte_count) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->jpeg_decoder == nullptr || byte_count < 0 ||
        (byte_count > 0 && rgba == nullptr)) {
        return;
    }
    guardedVoid(*ctx, "complete JPEG decode", [&]() {
        std::vector<std::uint8_t> data;
        if (rgba != nullptr && byte_count > 0) {
            data.assign(rgba, rgba + byte_count);
        }
        ctx->jpeg_decoder->deliverDecoded(id, width, height, data);
        if (ctx->player != nullptr) {
            (void)renderCurrentFrame(*ctx);
        }
    });
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_poll_movie_navigation_requests") const char* lswPollMovieNavigationRequests(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->net_provider == nullptr) {
        return "[]";
    }
    try {
        return scratch(*ctx, fetchRequestsJson(ctx->net_provider->pendingMovieNavigationRequests()));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "[]";
    } catch (...) {
        setError(*ctx, "Unknown movie navigation poll error");
        return "[]";
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_movie_navigation_complete") void lswMovieNavigationComplete(int handle, int task_id) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->net_provider != nullptr) {
        guardedVoid(*ctx, "complete movie navigation", [&]() {
            ctx->net_provider->onMovieNavigationComplete(task_id);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_fetch_complete") void lswFetchComplete(int handle,
                                             int task_id,
                                             const std::uint8_t* bytes,
                                             int byte_count) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->net_provider == nullptr || bytes == nullptr || byte_count < 0) {
        return;
    }
    guardedVoid(*ctx, "complete fetch", [&]() {
        WasmScriptDeadlineGuard script_deadline(ctx->player.get());
        ctx->net_provider->onFetchComplete(task_id, std::vector<std::uint8_t>(bytes, bytes + byte_count));
    });
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_fetch_error") void lswFetchError(int handle, int task_id, int status) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->net_provider != nullptr) {
        guardedVoid(*ctx, "fail fetch", [&]() {
            ctx->net_provider->onFetchError(task_id, status);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_poll_multiuser_requests") const char* lswPollMultiuserRequests(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->multiuser_bridge == nullptr) {
        return "[]";
    }
    try {
        return scratch(*ctx, multiuserRequestsJson(ctx->multiuser_bridge->pendingRequests(), ctx->external_params));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "[]";
    } catch (...) {
        setError(*ctx, "Unknown multiuser poll error");
        return "[]";
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_drain_multiuser_requests") void lswDrainMultiuserRequests(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->multiuser_bridge != nullptr) {
        guardedVoid(*ctx, "drain multiuser requests", [&]() {
            ctx->multiuser_bridge->drainPendingRequests();
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_poll_audio_commands") const char* lswPollAudioCommands(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->audio_backend == nullptr) {
        return "[]";
    }
    try {
        return scratch(*ctx, audioCommandsJson(ctx->audio_backend->pendingCommands()));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "[]";
    } catch (...) {
        setError(*ctx, "Unknown audio command poll error");
        return "[]";
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_drain_audio_commands") void lswDrainAudioCommands(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->audio_backend != nullptr) {
        guardedVoid(*ctx, "drain audio commands", [&]() {
            ctx->audio_backend->drainPending();
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_audio_stopped") void lswAudioStopped(int handle, int channel_num) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->audio_backend != nullptr) {
        guardedVoid(*ctx, "audio stopped", [&]() {
            ctx->audio_backend->notifyStopped(channel_num);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_multiuser_connected") void lswMultiuserConnected(int handle, int instance_id) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->multiuser_bridge != nullptr) {
        guardedVoid(*ctx, "multiuser connected", [&]() {
            ctx->multiuser_bridge->notifyConnected(instance_id);
            processHostXtraCallbacks(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_multiuser_disconnected") void lswMultiuserDisconnected(int handle, int instance_id) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->multiuser_bridge != nullptr) {
        guardedVoid(*ctx, "multiuser disconnected", [&]() {
            ctx->multiuser_bridge->notifyDisconnected(instance_id);
            processHostXtraCallbacks(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_multiuser_error") void lswMultiuserError(int handle, int instance_id, int error_code) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->multiuser_bridge != nullptr) {
        guardedVoid(*ctx, "multiuser error", [&]() {
            ctx->multiuser_bridge->notifyError(instance_id, error_code);
            processHostXtraCallbacks(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_multiuser_message_bytes") void lswMultiuserMessageBytes(int handle,
                                                      int instance_id,
                                                      const std::uint8_t* bytes,
                                                      int byte_count) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->multiuser_bridge == nullptr || bytes == nullptr || byte_count < 0) {
        return;
    }
    guardedVoid(*ctx, "multiuser message", [&]() {
        const auto deliver_started = std::chrono::steady_clock::now();
        ctx->multiuser_bridge->deliverMessageBytes(
            instance_id,
            std::vector<std::uint8_t>(bytes, bytes + byte_count));
        queueSlowOperationMessage(*ctx, "multiuser message delivery", deliver_started);
        processHostXtraCallbacks(*ctx);
    });
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_director_key_from_browser") int lswDirectorKeyFromBrowser(int browser_key_code) {
    return DirectorKeyCodes::fromBrowserKeyCode(browser_key_code);
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_mouse_move") int lswMouseMove(int handle, int stage_x, int stage_y) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        int result_flags = 0;
        guardedVoid(*ctx, "mouse move", [&]() {
            const int sprite_revision_before = ctx->player->stageRenderer().spriteRegistry().revision();
            const int rollover_before = ctx->player->inputState().rolloverSprite();
            const bool was_mouse_down = ctx->player->inputState().isMouseDown();
            ctx->player->inputHandler().onMouseMove(stage_x, stage_y);
            processQueuedInput(*ctx);
            const int sprite_revision_after = ctx->player->stageRenderer().spriteRegistry().revision();
            if (was_mouse_down || sprite_revision_after != sprite_revision_before) {
                if (renderCurrentFrame(*ctx)) {
                    result_flags |= 1;
                }
            }
            if (ctx->player->inputState().rolloverSprite() != rollover_before) {
                result_flags |= 2;
            }
        });
        return result_flags;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_mouse_down") void lswMouseDown(int handle, int stage_x, int stage_y, int right_button) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "mouse down", [&]() {
            ctx->player->inputHandler().onMouseDown(stage_x, stage_y, right_button != 0);
            processQueuedInput(*ctx);
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_mouse_up") void lswMouseUp(int handle, int stage_x, int stage_y, int right_button) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "mouse up", [&]() {
            ctx->player->inputHandler().onMouseUp(stage_x, stage_y, right_button != 0);
            processQueuedInput(*ctx);
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_blur") void lswBlur(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "blur", [&]() {
            ctx->player->inputHandler().onBlur();
            processQueuedInput(*ctx);
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_key_down") void lswKeyDown(int handle,
                                       int browser_key_code,
                                       const char* key_text,
                                       int shift,
                                       int ctrl,
                                       int alt) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "key down", [&]() {
            ctx->player->inputHandler().onKeyDown(DirectorKeyCodes::fromBrowserKeyCode(browser_key_code),
                                                  key_text != nullptr ? std::string(key_text) : std::string(),
                                                  shift != 0,
                                                  ctrl != 0,
                                                  alt != 0);
            processQueuedInput(*ctx);
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_key_up") void lswKeyUp(int handle,
                                     int browser_key_code,
                                     const char* key_text,
                                     int shift,
                                     int ctrl,
                                     int alt) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "key up", [&]() {
            ctx->player->inputHandler().onKeyUp(DirectorKeyCodes::fromBrowserKeyCode(browser_key_code),
                                                key_text != nullptr ? std::string(key_text) : std::string(),
                                                shift != 0,
                                                ctrl != 0,
                                                alt != 0);
            processQueuedInput(*ctx);
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_paste_text") void lswPasteText(int handle, const char* text) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr && text != nullptr) {
        guardedVoid(*ctx, "paste text", [&]() {
            ctx->player->inputHandler().onPasteText(text);
            processQueuedInput(*ctx);
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_selected_text") const char* lswSelectedText(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->player == nullptr) {
        return "";
    }
    return guardedResult<const char*>(*ctx, "selected text", "", [&]() {
        const auto text = ctx->player->inputHandler().getSelectedText();
        return scratch(*ctx, text.value_or(std::string()));
    });
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_select_all") void lswSelectAll(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "select all", [&]() {
            ctx->player->inputHandler().selectAll();
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_cut_selected_text") const char* lswCutSelectedText(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->player == nullptr) {
        return "";
    }
    return guardedResult<const char*>(*ctx, "cut selected text", "", [&]() {
        const auto text = ctx->player->inputHandler().cutSelectedText();
        (void)renderCurrentFrame(*ctx);
        return scratch(*ctx, text.value_or(std::string()));
    });
}

} // extern "C"
