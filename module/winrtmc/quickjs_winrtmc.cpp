/*
 * Copyright (c) 2026 Cronfox
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <chrono>
#include <cstdint>
#include <vector>

extern "C" {
#include "quickjs.h"
}

using namespace winrt;
using namespace Windows::Media;
using namespace Windows::Media::Control;

// --- 类定义 ---
static JSClassID js_media_session_class_id;

struct MediaSessionData {
    GlobalSystemMediaTransportControlsSession session{ nullptr };
    GlobalSystemMediaTransportControlsSessionManager manager{ nullptr };
    std::mutex session_mutex;

    event_token token_prop_changed;
    event_token token_playback_changed;
    event_token token_timeline_changed;
    event_token token_session_changed;
    
    // 缓存的元数据
    std::mutex cache_mutex;
    std::string cached_title;
    std::string cached_subtitle;
    std::string cached_artist;
    std::string cached_album;
    std::string cached_album_artist;
    int32_t cached_track_number{ 0 };
    int32_t cached_album_track_count{ 0 };
    bool cached_has_thumbnail{ false };
    std::vector<std::string> cached_genres;
    std::string cached_source_app_id;
    std::string cached_playback_status = "Unknown";
    std::string cached_playback_type = "Unknown";

    bool ctrl_play{ false };
    bool ctrl_pause{ false };
    bool ctrl_stop{ false };
    bool ctrl_record{ false };
    bool ctrl_next{ false };
    bool ctrl_prev{ false };
    bool ctrl_fast_forward{ false };
    bool ctrl_rewind{ false };
    bool ctrl_channel_up{ false };
    bool ctrl_channel_down{ false };

    double timeline_start_sec{ 0.0 };
    double timeline_end_sec{ 0.0 };
    double timeline_min_seek_sec{ 0.0 };
    double timeline_max_seek_sec{ 0.0 };
    double timeline_position_sec{ 0.0 };
    int64_t timeline_last_updated_ticks{ 0 };

    std::atomic<bool> cache_valid{ false };
    std::atomic<bool> playback_valid{ false };
    std::atomic<bool> timeline_valid{ false };
    
    // 同步标志
    std::atomic<bool> session_ready{ false };
    std::atomic<bool> destroyed{ false };
    
    // 变更通知
    std::atomic<bool> metadata_changed{ false };
    JSValue on_changed_callback = JS_UNDEFINED;
    
    ~MediaSessionData() {
        GlobalSystemMediaTransportControlsSession local_session{ nullptr };
        GlobalSystemMediaTransportControlsSessionManager local_manager{ nullptr };
        {
            std::lock_guard<std::mutex> session_lock(session_mutex);
            local_session = session;
            local_manager = manager;
        }

        if (local_session && token_prop_changed.value) {
            local_session.MediaPropertiesChanged(token_prop_changed);
        }
        if (local_session && token_playback_changed.value) {
            local_session.PlaybackInfoChanged(token_playback_changed);
        }
        if (local_session && token_timeline_changed.value) {
            local_session.TimelinePropertiesChanged(token_timeline_changed);
        }
        if (local_manager && token_session_changed.value) {
            local_manager.CurrentSessionChanged(token_session_changed);
        }
        // on_changed_callback 会在 finalizer 中释放
    }
};

static void RunOnWinRTThread(std::function<void()> fn) {
    std::thread([fn = std::move(fn)]() mutable {
        try {
            winrt::init_apartment(apartment_type::multi_threaded);
        } catch (...) {
        }
        try {
            fn();
        } catch (...) {
        }
    }).detach();
}

static GlobalSystemMediaTransportControlsSession GetSessionSnapshot(MediaSessionData* data) {
    std::lock_guard<std::mutex> lock(data->session_mutex);
    return data->session;
}

static GlobalSystemMediaTransportControlsSessionManager GetManagerSnapshot(MediaSessionData* data) {
    std::lock_guard<std::mutex> lock(data->session_mutex);
    return data->manager;
}

static void SetSession(MediaSessionData* data, GlobalSystemMediaTransportControlsSession const& session) {
    std::lock_guard<std::mutex> lock(data->session_mutex);
    data->session = session;
    if (session) {
        data->cached_source_app_id = winrt::to_string(session.SourceAppUserModelId());
    } else {
        data->cached_source_app_id.clear();
    }
}

static std::string PlaybackStatusToString(GlobalSystemMediaTransportControlsSessionPlaybackStatus status) {
    switch (status) {
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed:
        return "Closed";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Opened:
        return "Opened";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing:
        return "Changing";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
        return "Stopped";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
        return "Playing";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
        return "Paused";
    default:
        return "Unknown";
    }
}

static std::string PlaybackTypeToString(MediaPlaybackType type) {
    switch (type) {
    case MediaPlaybackType::Music:
        return "Music";
    case MediaPlaybackType::Video:
        return "Video";
    case MediaPlaybackType::Image:
        return "Image";
    default:
        return "Unknown";
    }
}

static std::string RepeatModeToString(MediaPlaybackAutoRepeatMode mode) {
    switch (mode) {
    case MediaPlaybackAutoRepeatMode::None:
        return "None";
    case MediaPlaybackAutoRepeatMode::Track:
        return "Track";
    case MediaPlaybackAutoRepeatMode::List:
        return "List";
    default:
        return "None";
    }
}

static bool ParseRepeatMode(std::string const& text, MediaPlaybackAutoRepeatMode& mode) {
    if (text == "None" || text == "none") {
        mode = MediaPlaybackAutoRepeatMode::None;
        return true;
    }
    if (text == "Track" || text == "track") {
        mode = MediaPlaybackAutoRepeatMode::Track;
        return true;
    }
    if (text == "List" || text == "list") {
        mode = MediaPlaybackAutoRepeatMode::List;
        return true;
    }
    return false;
}

static Windows::Foundation::TimeSpan SecondsToTimeSpan(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    return Windows::Foundation::TimeSpan{ static_cast<int64_t>(seconds * 10000000.0) };
}

static double TimeSpanToSeconds(Windows::Foundation::TimeSpan const& ts) {
    return static_cast<double>(ts.count()) / 10000000.0;
}

static JSValue BuildMetadataObjectFromProperties(JSContext* ctx, GlobalSystemMediaTransportControlsSessionMediaProperties const& props) {
    JSValue obj = JS_NewObject(ctx);
    auto title = winrt::to_string(props.Title());
    auto subtitle = winrt::to_string(props.Subtitle());
    auto artist = winrt::to_string(props.Artist());
    auto album = winrt::to_string(props.AlbumTitle());
    auto album_artist = winrt::to_string(props.AlbumArtist());

    JS_SetPropertyStr(ctx, obj, "title", JS_NewString(ctx, title.c_str()));
    JS_SetPropertyStr(ctx, obj, "subtitle", JS_NewString(ctx, subtitle.c_str()));
    JS_SetPropertyStr(ctx, obj, "artist", JS_NewString(ctx, artist.c_str()));
    JS_SetPropertyStr(ctx, obj, "album", JS_NewString(ctx, album.c_str()));
    JS_SetPropertyStr(ctx, obj, "albumTitle", JS_NewString(ctx, album.c_str()));
    JS_SetPropertyStr(ctx, obj, "albumArtist", JS_NewString(ctx, album_artist.c_str()));
    JS_SetPropertyStr(ctx, obj, "trackNumber", JS_NewInt32(ctx, props.TrackNumber()));
    JS_SetPropertyStr(ctx, obj, "hasThumbnail", JS_NewBool(ctx, !!props.Thumbnail()));

    JSValue genres = JS_NewArray(ctx);
    uint32_t index = 0;
    for (auto const& genre : props.Genres()) {
        auto s = winrt::to_string(genre);
        JS_SetPropertyUint32(ctx, genres, index++, JS_NewString(ctx, s.c_str()));
    }
    JS_SetPropertyStr(ctx, obj, "genres", genres);

    return obj;
}

static JSValue BuildSessionDetailObject(JSContext* ctx, GlobalSystemMediaTransportControlsSession const& session, uint32_t session_id, bool is_current) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "sessionId", JS_NewUint32(ctx, session_id));
    JS_SetPropertyStr(ctx, obj, "isCurrent", JS_NewBool(ctx, is_current));

    auto source = winrt::to_string(session.SourceAppUserModelId());
    JS_SetPropertyStr(ctx, obj, "sourceAppUserModelId", JS_NewString(ctx, source.c_str()));

    try {
        auto props = session.TryGetMediaPropertiesAsync().get();
        if (props) {
            JS_SetPropertyStr(ctx, obj, "metadata", BuildMetadataObjectFromProperties(ctx, props));
        } else {
            JS_SetPropertyStr(ctx, obj, "metadata", JS_NULL);
        }
    } catch (...) {
        JS_SetPropertyStr(ctx, obj, "metadata", JS_NULL);
    }

    try {
        auto info = session.GetPlaybackInfo();
        JSValue playback = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, playback, "status", JS_NewString(ctx, PlaybackStatusToString(info.PlaybackStatus()).c_str()));
        auto playback_type = info.PlaybackType();
        JS_SetPropertyStr(ctx, playback, "type", JS_NewString(ctx, playback_type ? PlaybackTypeToString(playback_type.Value()).c_str() : "Unknown"));

        auto rate = info.PlaybackRate();
        JS_SetPropertyStr(ctx, playback, "playbackRate", rate ? JS_NewFloat64(ctx, rate.Value()) : JS_NULL);
        auto shuffle = info.IsShuffleActive();
        JS_SetPropertyStr(ctx, playback, "shuffle", shuffle ? JS_NewBool(ctx, shuffle.Value()) : JS_NULL);
        auto repeat = info.AutoRepeatMode();
        JS_SetPropertyStr(ctx, playback, "repeatMode", repeat ? JS_NewString(ctx, RepeatModeToString(repeat.Value()).c_str()) : JS_NULL);

        auto controls = info.Controls();
        JSValue controls_obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, controls_obj, "play", JS_NewBool(ctx, controls.IsPlayEnabled()));
        JS_SetPropertyStr(ctx, controls_obj, "pause", JS_NewBool(ctx, controls.IsPauseEnabled()));
        JS_SetPropertyStr(ctx, controls_obj, "stop", JS_NewBool(ctx, controls.IsStopEnabled()));
        JS_SetPropertyStr(ctx, controls_obj, "record", JS_NewBool(ctx, controls.IsRecordEnabled()));
        JS_SetPropertyStr(ctx, controls_obj, "next", JS_NewBool(ctx, controls.IsNextEnabled()));
        JS_SetPropertyStr(ctx, controls_obj, "prev", JS_NewBool(ctx, controls.IsPreviousEnabled()));
        JS_SetPropertyStr(ctx, controls_obj, "fastForward", JS_NewBool(ctx, controls.IsFastForwardEnabled()));
        JS_SetPropertyStr(ctx, controls_obj, "rewind", JS_NewBool(ctx, controls.IsRewindEnabled()));
        JS_SetPropertyStr(ctx, controls_obj, "channelUp", JS_NewBool(ctx, controls.IsChannelUpEnabled()));
        JS_SetPropertyStr(ctx, controls_obj, "channelDown", JS_NewBool(ctx, controls.IsChannelDownEnabled()));
        JS_SetPropertyStr(ctx, playback, "controls", controls_obj);

        JS_SetPropertyStr(ctx, obj, "playback", playback);
    } catch (...) {
        JS_SetPropertyStr(ctx, obj, "playback", JS_NULL);
    }

    try {
        auto timeline = session.GetTimelineProperties();
        JSValue timeline_obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, timeline_obj, "start", JS_NewFloat64(ctx, TimeSpanToSeconds(timeline.StartTime())));
        JS_SetPropertyStr(ctx, timeline_obj, "end", JS_NewFloat64(ctx, TimeSpanToSeconds(timeline.EndTime())));
        JS_SetPropertyStr(ctx, timeline_obj, "minSeek", JS_NewFloat64(ctx, TimeSpanToSeconds(timeline.MinSeekTime())));
        JS_SetPropertyStr(ctx, timeline_obj, "maxSeek", JS_NewFloat64(ctx, TimeSpanToSeconds(timeline.MaxSeekTime())));
        JS_SetPropertyStr(ctx, timeline_obj, "position", JS_NewFloat64(ctx, TimeSpanToSeconds(timeline.Position())));
        auto last_updated = timeline.LastUpdatedTime();
        JS_SetPropertyStr(ctx, timeline_obj, "lastUpdatedTicks", JS_NewFloat64(ctx, static_cast<double>(last_updated.time_since_epoch().count())));
        JS_SetPropertyStr(ctx, obj, "timeline", timeline_obj);
    } catch (...) {
        JS_SetPropertyStr(ctx, obj, "timeline", JS_NULL);
    }

    return obj;
}

static bool ResolveTargetSession(JSContext* ctx, MediaSessionData* data, JSValueConst arg, GlobalSystemMediaTransportControlsSession& out_session) {
    auto manager = GetManagerSnapshot(data);
    if (!manager) {
        return false;
    }

    try {
        auto sessions = manager.GetSessions();
        if (JS_IsNumber(arg)) {
            uint32_t session_id = 0;
            if (JS_ToUint32(ctx, &session_id, arg) != 0) {
                return false;
            }
            if (session_id >= sessions.Size()) {
                return false;
            }
            out_session = sessions.GetAt(session_id);
            return !!out_session;
        }

        const char* source_cstr = JS_ToCString(ctx, arg);
        if (!source_cstr) {
            return false;
        }
        std::string source(source_cstr);
        JS_FreeCString(ctx, source_cstr);

        for (auto const& session : sessions) {
            auto id = winrt::to_string(session.SourceAppUserModelId());
            if (id == source) {
                out_session = session;
                return true;
            }
        }
    } catch (...) {
    }
    return false;
}

static JSValue js_media_session_control_target_common(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv,
    std::function<void(GlobalSystemMediaTransportControlsSession const&)> call) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "sessionId(number) or sourceAppUserModelId(string) is required");
    }

    GlobalSystemMediaTransportControlsSession target{ nullptr };
    if (!ResolveTargetSession(ctx, data, argv[0], target) || !target) {
        return JS_FALSE;
    }

    RunOnWinRTThread([target, call = std::move(call)]() mutable {
        call(target);
    });
    return JS_TRUE;
}

// --- 更新元数据缓存（在后台线程中调用） ---
static void UpdateMetadataCache(MediaSessionData* data, GlobalSystemMediaTransportControlsSession const& session) {
    try {
        if (data->destroyed.load(std::memory_order_acquire)) return;
        if (!session) {
            data->cache_valid.store(false, std::memory_order_release);
            return;
        }
        
        auto props = session.TryGetMediaPropertiesAsync().get();
        if (data->destroyed.load(std::memory_order_acquire)) return;
        if (props) {
            std::lock_guard<std::mutex> lock(data->cache_mutex);
            data->cached_title = winrt::to_string(props.Title());
            data->cached_subtitle = winrt::to_string(props.Subtitle());
            data->cached_artist = winrt::to_string(props.Artist());
            data->cached_album = winrt::to_string(props.AlbumTitle());
            data->cached_album_artist = winrt::to_string(props.AlbumArtist());
            data->cached_track_number = props.TrackNumber();
            data->cached_album_track_count = props.AlbumTrackCount();
            data->cached_has_thumbnail = !!props.Thumbnail();
            data->cached_genres.clear();
            for (auto const& genre : props.Genres()) {
                data->cached_genres.emplace_back(winrt::to_string(genre));
            }
            data->cache_valid.store(true, std::memory_order_release);
        }
    } catch (...) {
        if (!data->destroyed.load(std::memory_order_acquire))
            data->cache_valid.store(false, std::memory_order_release);
    }
}

static void UpdatePlaybackCache(MediaSessionData* data, GlobalSystemMediaTransportControlsSession const& session) {
    try {
        if (data->destroyed.load(std::memory_order_acquire)) return;
        if (!session) {
            data->playback_valid.store(false, std::memory_order_release);
            return;
        }

        auto info = session.GetPlaybackInfo();
        auto controls = info.Controls();
        std::lock_guard<std::mutex> lock(data->cache_mutex);
        data->cached_playback_status = PlaybackStatusToString(info.PlaybackStatus());
        auto playback_type = info.PlaybackType();
        data->cached_playback_type = playback_type ? PlaybackTypeToString(playback_type.Value()) : "Unknown";
        data->ctrl_play = controls.IsPlayEnabled();
        data->ctrl_pause = controls.IsPauseEnabled();
        data->ctrl_stop = controls.IsStopEnabled();
        data->ctrl_record = controls.IsRecordEnabled();
        data->ctrl_next = controls.IsNextEnabled();
        data->ctrl_prev = controls.IsPreviousEnabled();
        data->ctrl_fast_forward = controls.IsFastForwardEnabled();
        data->ctrl_rewind = controls.IsRewindEnabled();
        data->ctrl_channel_up = controls.IsChannelUpEnabled();
        data->ctrl_channel_down = controls.IsChannelDownEnabled();
        data->playback_valid.store(true, std::memory_order_release);
    } catch (...) {
        if (!data->destroyed.load(std::memory_order_acquire)) {
            data->playback_valid.store(false, std::memory_order_release);
        }
    }
}

static void UpdateTimelineCache(MediaSessionData* data, GlobalSystemMediaTransportControlsSession const& session) {
    try {
        if (data->destroyed.load(std::memory_order_acquire)) return;
        if (!session) {
            data->timeline_valid.store(false, std::memory_order_release);
            return;
        }

        auto timeline = session.GetTimelineProperties();
        std::lock_guard<std::mutex> lock(data->cache_mutex);
        data->timeline_start_sec = TimeSpanToSeconds(timeline.StartTime());
        data->timeline_end_sec = TimeSpanToSeconds(timeline.EndTime());
        data->timeline_min_seek_sec = TimeSpanToSeconds(timeline.MinSeekTime());
        data->timeline_max_seek_sec = TimeSpanToSeconds(timeline.MaxSeekTime());
        data->timeline_position_sec = TimeSpanToSeconds(timeline.Position());
        auto last_updated = timeline.LastUpdatedTime();
        data->timeline_last_updated_ticks = last_updated.time_since_epoch().count();
        data->timeline_valid.store(true, std::memory_order_release);
    } catch (...) {
        if (!data->destroyed.load(std::memory_order_acquire)) {
            data->timeline_valid.store(false, std::memory_order_release);
        }
    }
}

static void RefreshAllCaches(MediaSessionData* data) {
    if (data->destroyed.load(std::memory_order_acquire)) return;
    auto session = GetSessionSnapshot(data);
    UpdateMetadataCache(data, session);
    UpdatePlaybackCache(data, session);
    UpdateTimelineCache(data, session);
}

static JSValue BuildPlaybackInfoObject(JSContext* ctx, MediaSessionData* data) {
    if (!data->playback_valid.load(std::memory_order_acquire)) {
        return JS_NULL;
    }

    std::lock_guard<std::mutex> lock(data->cache_mutex);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "status", JS_NewString(ctx, data->cached_playback_status.c_str()));
    JS_SetPropertyStr(ctx, obj, "type", JS_NewString(ctx, data->cached_playback_type.c_str()));

    JSValue controls = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, controls, "play", JS_NewBool(ctx, data->ctrl_play));
    JS_SetPropertyStr(ctx, controls, "pause", JS_NewBool(ctx, data->ctrl_pause));
    JS_SetPropertyStr(ctx, controls, "stop", JS_NewBool(ctx, data->ctrl_stop));
    JS_SetPropertyStr(ctx, controls, "record", JS_NewBool(ctx, data->ctrl_record));
    JS_SetPropertyStr(ctx, controls, "next", JS_NewBool(ctx, data->ctrl_next));
    JS_SetPropertyStr(ctx, controls, "prev", JS_NewBool(ctx, data->ctrl_prev));
    JS_SetPropertyStr(ctx, controls, "fastForward", JS_NewBool(ctx, data->ctrl_fast_forward));
    JS_SetPropertyStr(ctx, controls, "rewind", JS_NewBool(ctx, data->ctrl_rewind));
    JS_SetPropertyStr(ctx, controls, "channelUp", JS_NewBool(ctx, data->ctrl_channel_up));
    JS_SetPropertyStr(ctx, controls, "channelDown", JS_NewBool(ctx, data->ctrl_channel_down));

    JS_SetPropertyStr(ctx, obj, "controls", controls);
    return obj;
}

static JSValue BuildTimelineInfoObject(JSContext* ctx, MediaSessionData* data) {
    if (!data->timeline_valid.load(std::memory_order_acquire)) {
        return JS_NULL;
    }

    std::lock_guard<std::mutex> lock(data->cache_mutex);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "start", JS_NewFloat64(ctx, data->timeline_start_sec));
    JS_SetPropertyStr(ctx, obj, "end", JS_NewFloat64(ctx, data->timeline_end_sec));
    JS_SetPropertyStr(ctx, obj, "minSeek", JS_NewFloat64(ctx, data->timeline_min_seek_sec));
    JS_SetPropertyStr(ctx, obj, "maxSeek", JS_NewFloat64(ctx, data->timeline_max_seek_sec));
    JS_SetPropertyStr(ctx, obj, "position", JS_NewFloat64(ctx, data->timeline_position_sec));
    JS_SetPropertyStr(ctx, obj, "lastUpdatedTicks", JS_NewFloat64(ctx, static_cast<double>(data->timeline_last_updated_ticks)));
    return obj;
}

// --- 注册媒体属性变更事件 ---
static void RegisterMediaEvents(MediaSessionData* data) {
    auto session = GetSessionSnapshot(data);
    if (!session) return;
    
    // 注销旧的事件
    if (data->token_prop_changed.value) {
        session.MediaPropertiesChanged(data->token_prop_changed);
        data->token_prop_changed = {};
    }
    if (data->token_playback_changed.value) {
        session.PlaybackInfoChanged(data->token_playback_changed);
        data->token_playback_changed = {};
    }
    if (data->token_timeline_changed.value) {
        session.TimelinePropertiesChanged(data->token_timeline_changed);
        data->token_timeline_changed = {};
    }
    
    // 注册新的 MediaPropertiesChanged 事件
    data->token_prop_changed = session.MediaPropertiesChanged(
        [data](auto const&, auto const&) {
            if (data->destroyed.load(std::memory_order_acquire)) return;
            data->metadata_changed.store(true, std::memory_order_release);
            RunOnWinRTThread([data]() {
                RefreshAllCaches(data);
            });
        }
    );

    data->token_playback_changed = session.PlaybackInfoChanged(
        [data](auto const&, auto const&) {
            if (data->destroyed.load(std::memory_order_acquire)) return;
            data->metadata_changed.store(true, std::memory_order_release);
            RunOnWinRTThread([data]() {
                RefreshAllCaches(data);
            });
        }
    );

    data->token_timeline_changed = session.TimelinePropertiesChanged(
        [data](auto const&, auto const&) {
            if (data->destroyed.load(std::memory_order_acquire)) return;
            data->metadata_changed.store(true, std::memory_order_release);
            RunOnWinRTThread([data]() {
                RefreshAllCaches(data);
            });
        }
    );
}

// --- GC 标记（让 GC 知道 on_changed_callback 的引用） ---
static void js_media_session_gc_mark(JSRuntime* rt, JSValueConst val, JS_MarkFunc* mark_func) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque(val, js_media_session_class_id));
    if (data) {
        if (!JS_IsUndefined(data->on_changed_callback)) {
            JS_MarkValue(rt, data->on_changed_callback, mark_func);
        }
    }
}

// --- Finalizer（自动清理） ---
static void js_media_session_finalizer(JSRuntime* rt, JSValue val) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque(val, js_media_session_class_id));
    if (data) {
        // 标记已销毁，阻止后台线程继续访问
        data->destroyed.store(true, std::memory_order_release);
        // 释放回调（在 finalizer 中安全释放）
        if (!JS_IsUndefined(data->on_changed_callback)) {
            JS_FreeValueRT(rt, data->on_changed_callback);
            data->on_changed_callback = JS_UNDEFINED;
        }
        delete data;
    }
}

// --- 构造函数 ---
static JSValue js_media_session_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_media_session_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj)) return obj;

    auto* data = new MediaSessionData();
    JS_SetOpaque(obj, data);
    
    // 在后台线程初始化 WinRT（避免 STA 线程问题）
    RunOnWinRTThread([data]() {
        try {
            if (data->destroyed.load(std::memory_order_acquire)) return;
            auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            if (data->destroyed.load(std::memory_order_acquire)) return;
            {
                std::lock_guard<std::mutex> lock(data->session_mutex);
                data->manager = manager;
            }
            auto session = manager.GetCurrentSession();
            SetSession(data, session);
            
            if (session) {
                // 立即获取初始缓存
                RefreshAllCaches(data);
                // 注册属性变更事件，自动更新缓存
                RegisterMediaEvents(data);
            }
            
            if (data->destroyed.load(std::memory_order_acquire)) return;
            
            // 监听活动会话切换（如从 Spotify 切换到其他播放器）
            data->token_session_changed = manager.CurrentSessionChanged(
                [data](auto const&, auto const&) {
                    if (data->destroyed.load(std::memory_order_acquire)) return;
                    try {
                        auto manager_snapshot = GetManagerSnapshot(data);
                        auto newSession = manager_snapshot ? manager_snapshot.GetCurrentSession() : GlobalSystemMediaTransportControlsSession{ nullptr };
                        // 注销旧事件
                        auto old_session = GetSessionSnapshot(data);
                        if (old_session && data->token_prop_changed.value) {
                            old_session.MediaPropertiesChanged(data->token_prop_changed);
                            data->token_prop_changed = {};
                        }
                        if (old_session && data->token_playback_changed.value) {
                            old_session.PlaybackInfoChanged(data->token_playback_changed);
                            data->token_playback_changed = {};
                        }
                        if (old_session && data->token_timeline_changed.value) {
                            old_session.TimelinePropertiesChanged(data->token_timeline_changed);
                            data->token_timeline_changed = {};
                        }

                        SetSession(data, newSession);
                        if (newSession) {
                            RefreshAllCaches(data);
                            RegisterMediaEvents(data);
                        } else {
                            data->cache_valid.store(false, std::memory_order_release);
                            data->playback_valid.store(false, std::memory_order_release);
                            data->timeline_valid.store(false, std::memory_order_release);
                        }
                        data->metadata_changed.store(true, std::memory_order_release);
                    } catch (...) {}
                }
            );
            
            data->session_ready.store(true, std::memory_order_release);
        } catch (...) {
            // 初始化失败
        }
    });
    
    return obj;
}

// --- 获取元数据（独立 getter 函数） ---
static JSValue js_media_session_get_title(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data || !data->cache_valid.load(std::memory_order_acquire)) {
        return JS_NewString(ctx, "");
    }
    std::lock_guard<std::mutex> lock(data->cache_mutex);
    return JS_NewString(ctx, data->cached_title.c_str());
}

static JSValue js_media_session_get_artist(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data || !data->cache_valid.load(std::memory_order_acquire)) {
        return JS_NewString(ctx, "");
    }
    std::lock_guard<std::mutex> lock(data->cache_mutex);
    return JS_NewString(ctx, data->cached_artist.c_str());
}

static JSValue js_media_session_get_album(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data || !data->cache_valid.load(std::memory_order_acquire)) {
        return JS_NewString(ctx, "");
    }
    std::lock_guard<std::mutex> lock(data->cache_mutex);
    return JS_NewString(ctx, data->cached_album.c_str());
}

// --- 获取完整元数据 ---
static JSValue js_media_session_get_metadata(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data || !data->cache_valid.load(std::memory_order_acquire)) {
        return JS_NULL;
    }
    
    std::lock_guard<std::mutex> lock(data->cache_mutex);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "title", JS_NewString(ctx, data->cached_title.c_str()));
    JS_SetPropertyStr(ctx, obj, "subtitle", JS_NewString(ctx, data->cached_subtitle.c_str()));
    JS_SetPropertyStr(ctx, obj, "artist", JS_NewString(ctx, data->cached_artist.c_str()));
    JS_SetPropertyStr(ctx, obj, "album", JS_NewString(ctx, data->cached_album.c_str()));
    JS_SetPropertyStr(ctx, obj, "albumTitle", JS_NewString(ctx, data->cached_album.c_str()));
    JS_SetPropertyStr(ctx, obj, "albumArtist", JS_NewString(ctx, data->cached_album_artist.c_str()));
    JS_SetPropertyStr(ctx, obj, "trackNumber", JS_NewInt32(ctx, data->cached_track_number));
    JS_SetPropertyStr(ctx, obj, "albumTrackCount", JS_NewInt32(ctx, data->cached_album_track_count));

    JS_SetPropertyStr(ctx, obj, "hasThumbnail", JS_NewBool(ctx, data->cached_has_thumbnail));

    JSValue genres = JS_NewArray(ctx);
    uint32_t index = 0;
    for (auto const& genre : data->cached_genres) {
        JS_SetPropertyUint32(ctx, genres, index++, JS_NewString(ctx, genre.c_str()));
    }
    JS_SetPropertyStr(ctx, obj, "genres", genres);
    return obj;
}

static JSValue js_media_session_get_playback_info(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;
    return BuildPlaybackInfoObject(ctx, data);
}

static JSValue js_media_session_get_timeline_info(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;
    return BuildTimelineInfoObject(ctx, data);
}

static JSValue js_media_session_get_session_info(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;

    auto session = GetSessionSnapshot(data);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "ready", JS_NewBool(ctx, data->session_ready.load(std::memory_order_acquire)));
    JS_SetPropertyStr(ctx, obj, "hasSession", JS_NewBool(ctx, !!session));
    std::string source;
    {
        std::lock_guard<std::mutex> lock(data->cache_mutex);
        source = data->cached_source_app_id;
    }
    JS_SetPropertyStr(ctx, obj, "sourceAppUserModelId", JS_NewString(ctx, source.c_str()));
    return obj;
}

static JSValue js_media_session_get_all_sessions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;

    auto manager = GetManagerSnapshot(data);
    if (!manager) return JS_NULL;

    JSValue arr = JS_NewArray(ctx);
    try {
        auto list = manager.GetSessions();
        auto current = manager.GetCurrentSession();
        uint32_t index = 0;
        for (auto const& session : list) {
            JSValue item = JS_NewObject(ctx);
            auto source = winrt::to_string(session.SourceAppUserModelId());
            JS_SetPropertyStr(ctx, item, "sourceAppUserModelId", JS_NewString(ctx, source.c_str()));
            JS_SetPropertyStr(ctx, item, "isCurrent", JS_NewBool(ctx, current && (session == current)));
            JS_SetPropertyUint32(ctx, arr, index++, item);
        }
    } catch (...) {
    }
    return arr;
}

static JSValue js_media_session_get_all_sessions_detailed(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;

    auto manager = GetManagerSnapshot(data);
    if (!manager) return JS_NULL;

    JSValue arr = JS_NewArray(ctx);
    try {
        auto list = manager.GetSessions();
        auto current = manager.GetCurrentSession();
        uint32_t index = 0;
        for (auto const& session : list) {
            auto item = BuildSessionDetailObject(ctx, session, index, current && (session == current));
            JS_SetPropertyUint32(ctx, arr, index, item);
            ++index;
        }
    } catch (...) {
    }

    return arr;
}

static JSValue js_media_session_get_session_detail(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "sessionId(number) or sourceAppUserModelId(string) is required");
    }

    auto manager = GetManagerSnapshot(data);
    if (!manager) return JS_NULL;

    GlobalSystemMediaTransportControlsSession target{ nullptr };
    if (!ResolveTargetSession(ctx, data, argv[0], target) || !target) {
        return JS_NULL;
    }

    uint32_t session_id = 0;
    try {
        auto list = manager.GetSessions();
        auto size = list.Size();
        for (uint32_t i = 0; i < size; ++i) {
            if (list.GetAt(i) == target) {
                session_id = i;
                break;
            }
        }
        auto current = manager.GetCurrentSession();
        return BuildSessionDetailObject(ctx, target, session_id, current && (target == current));
    } catch (...) {
        return JS_NULL;
    }
}

static JSValue js_media_session_get_source_app(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;

    std::lock_guard<std::mutex> lock(data->cache_mutex);
    return JS_NewString(ctx, data->cached_source_app_id.c_str());
}

static JSValue js_media_session_get_ready(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;
    return JS_NewBool(ctx, data->session_ready.load(std::memory_order_acquire));
}

static JSValue js_media_session_get_has_session(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;
    return JS_NewBool(ctx, !!GetSessionSnapshot(data));
}

static JSValue js_media_session_control_common(JSContext* ctx, JSValueConst this_val, std::function<void(GlobalSystemMediaTransportControlsSession const&)> call) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;
    auto session = GetSessionSnapshot(data);
    if (!session) return JS_FALSE;

    RunOnWinRTThread([session, call = std::move(call)]() mutable {
        call(session);
    });
    return JS_TRUE;
}

// --- 媒体控制 ---
static JSValue js_media_session_play(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_common(ctx, this_val, [](auto const& session) {
        try { session.TryPlayAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_pause(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_common(ctx, this_val, [](auto const& session) {
        try { session.TryPauseAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_common(ctx, this_val, [](auto const& session) {
        try { session.TryStopAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_common(ctx, this_val, [](auto const& session) {
        try { session.TrySkipNextAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_prev(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_common(ctx, this_val, [](auto const& session) {
        try { session.TrySkipPreviousAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_toggle_play_pause(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_common(ctx, this_val, [](auto const& session) {
        try { session.TryTogglePlayPauseAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_fast_forward(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_common(ctx, this_val, [](auto const& session) {
        try { session.TryFastForwardAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_rewind(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_common(ctx, this_val, [](auto const& session) {
        try { session.TryRewindAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_channel_up(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_common(ctx, this_val, [](auto const& session) {
        try { session.TryChangeChannelUpAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_channel_down(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_common(ctx, this_val, [](auto const& session) {
        try { session.TryChangeChannelDownAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_set_shuffle(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "shuffle enabled(bool) is required");
    }

    int enabled = JS_ToBool(ctx, argv[0]);
    return js_media_session_control_common(ctx, this_val, [enabled](auto const& session) {
        try { session.TryChangeShuffleActiveAsync(enabled != 0).get(); } catch (...) {}
    });
}

static JSValue js_media_session_set_repeat_mode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "repeat mode is required (None|Track|List)");
    }

    const char* mode_cstr = JS_ToCString(ctx, argv[0]);
    if (!mode_cstr) return JS_EXCEPTION;
    std::string mode_text(mode_cstr);
    JS_FreeCString(ctx, mode_cstr);

    MediaPlaybackAutoRepeatMode mode;
    if (!ParseRepeatMode(mode_text, mode)) {
        return JS_ThrowRangeError(ctx, "Invalid repeat mode, expected None|Track|List");
    }

    return js_media_session_control_common(ctx, this_val, [mode](auto const& session) {
        try { session.TryChangeAutoRepeatModeAsync(mode).get(); } catch (...) {}
    });
}

static JSValue js_media_session_set_playback_rate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "playback rate(number) is required");
    }

    double rate = 0.0;
    if (JS_ToFloat64(ctx, &rate, argv[0]) != 0) {
        return JS_EXCEPTION;
    }
    if (rate <= 0.0) {
        return JS_ThrowRangeError(ctx, "playback rate must be greater than 0");
    }

    return js_media_session_control_common(ctx, this_val, [rate](auto const& session) {
        try { session.TryChangePlaybackRateAsync(rate).get(); } catch (...) {}
    });
}

static JSValue js_media_session_seek(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "positionSeconds(number) is required");
    }

    double seconds = 0.0;
    if (JS_ToFloat64(ctx, &seconds, argv[0]) != 0) {
        return JS_EXCEPTION;
    }

    auto target = SecondsToTimeSpan(seconds);
    return js_media_session_control_common(ctx, this_val, [target](auto const& session) {
        try { session.TryChangePlaybackPositionAsync(target.count()).get(); } catch (...) {}
    });
}

static JSValue js_media_session_play_session(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_target_common(ctx, this_val, argc, argv, [](auto const& session) {
        try { session.TryPlayAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_pause_session(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_target_common(ctx, this_val, argc, argv, [](auto const& session) {
        try { session.TryPauseAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_stop_session(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_target_common(ctx, this_val, argc, argv, [](auto const& session) {
        try { session.TryStopAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_next_session(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_target_common(ctx, this_val, argc, argv, [](auto const& session) {
        try { session.TrySkipNextAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_prev_session(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_target_common(ctx, this_val, argc, argv, [](auto const& session) {
        try { session.TrySkipPreviousAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_toggle_play_pause_session(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_media_session_control_target_common(ctx, this_val, argc, argv, [](auto const& session) {
        try { session.TryTogglePlayPauseAsync().get(); } catch (...) {}
    });
}

static JSValue js_media_session_seek_session(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "seekSession requires (sessionIdOrSource, positionSeconds)");
    }

    double seconds = 0.0;
    if (JS_ToFloat64(ctx, &seconds, argv[1]) != 0) {
        return JS_EXCEPTION;
    }
    auto target = SecondsToTimeSpan(seconds);

    return js_media_session_control_target_common(ctx, this_val, argc, argv, [target](auto const& session) {
        try { session.TryChangePlaybackPositionAsync(target.count()).get(); } catch (...) {}
    });
}

static JSValue js_media_session_set_playback_rate_session(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "setPlaybackRateSession requires (sessionIdOrSource, rate)");
    }

    double rate = 0.0;
    if (JS_ToFloat64(ctx, &rate, argv[1]) != 0) {
        return JS_EXCEPTION;
    }
    if (rate <= 0.0) {
        return JS_ThrowRangeError(ctx, "playback rate must be greater than 0");
    }

    return js_media_session_control_target_common(ctx, this_val, argc, argv, [rate](auto const& session) {
        try { session.TryChangePlaybackRateAsync(rate).get(); } catch (...) {}
    });
}

static JSValue js_media_session_set_shuffle_session(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "setShuffleSession requires (sessionIdOrSource, enabled)");
    }
    int enabled = JS_ToBool(ctx, argv[1]);
    return js_media_session_control_target_common(ctx, this_val, argc, argv, [enabled](auto const& session) {
        try { session.TryChangeShuffleActiveAsync(enabled != 0).get(); } catch (...) {}
    });
}

static JSValue js_media_session_set_repeat_mode_session(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "setRepeatModeSession requires (sessionIdOrSource, mode)");
    }

    const char* mode_cstr = JS_ToCString(ctx, argv[1]);
    if (!mode_cstr) return JS_EXCEPTION;
    std::string mode_text(mode_cstr);
    JS_FreeCString(ctx, mode_cstr);

    MediaPlaybackAutoRepeatMode mode;
    if (!ParseRepeatMode(mode_text, mode)) {
        return JS_ThrowRangeError(ctx, "Invalid repeat mode, expected None|Track|List");
    }

    return js_media_session_control_target_common(ctx, this_val, argc, argv, [mode](auto const& session) {
        try { session.TryChangeAutoRepeatModeAsync(mode).get(); } catch (...) {}
    });
}

// --- 事件监听（仅设置 JS 回调，事件已在构造函数中注册） ---
static JSValue js_media_session_on_changed(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;
    
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Callback must be a function");
    }
    
    // 释放旧回调
    if (!JS_IsUndefined(data->on_changed_callback)) {
        JS_FreeValue(ctx, data->on_changed_callback);
    }
    data->on_changed_callback = JS_DupValue(ctx, argv[0]);
    
    return JS_UNDEFINED;
}

// --- 手动刷新元数据 ---
static JSValue js_media_session_refresh(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;
    
    if (data->session_ready.load(std::memory_order_acquire) && GetSessionSnapshot(data)) {
        RunOnWinRTThread([data]() {
            RefreshAllCaches(data);
        });
        return JS_TRUE;
    }
    return JS_FALSE;
}

// --- 轮询事件 ---
static JSValue js_media_session_poll(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* data = static_cast<MediaSessionData*>(JS_GetOpaque2(ctx, this_val, js_media_session_class_id));
    if (!data) return JS_EXCEPTION;
    
    if (data->metadata_changed.exchange(false, std::memory_order_acq_rel)) {
        if (!JS_IsUndefined(data->on_changed_callback)) {
            JSValue ret = JS_Call(ctx, data->on_changed_callback, this_val, 0, nullptr);
            JS_FreeValue(ctx, ret);
        }
    }
    return JS_UNDEFINED;
}

// --- 模块初始化 ---
static int js_media_module_init(JSContext* ctx, JSModuleDef* m) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    
    // 注册类
    JS_NewClassID(rt, &js_media_session_class_id);
    JSClassDef class_def = { "MediaSession", js_media_session_finalizer, js_media_session_gc_mark };
    JS_NewClass(rt, js_media_session_class_id, &class_def);

    // 创建原型对象
    JSValue proto = JS_NewObject(ctx);
    
    // 手动添加属性和方法
    JSAtom atom_title = JS_NewAtom(ctx, "title");
    JS_DefinePropertyGetSet(ctx, proto, atom_title,
        JS_NewCFunction(ctx, js_media_session_get_title, "get_title", 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom_title);
    
    JSAtom atom_artist = JS_NewAtom(ctx, "artist");
    JS_DefinePropertyGetSet(ctx, proto, atom_artist,
        JS_NewCFunction(ctx, js_media_session_get_artist, "get_artist", 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom_artist);
    
    JSAtom atom_album = JS_NewAtom(ctx, "album");
    JS_DefinePropertyGetSet(ctx, proto, atom_album,
        JS_NewCFunction(ctx, js_media_session_get_album, "get_album", 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom_album);

    JSAtom atom_source_app = JS_NewAtom(ctx, "sourceAppUserModelId");
    JS_DefinePropertyGetSet(ctx, proto, atom_source_app,
        JS_NewCFunction(ctx, js_media_session_get_source_app, "get_sourceAppUserModelId", 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom_source_app);

    JSAtom atom_ready = JS_NewAtom(ctx, "ready");
    JS_DefinePropertyGetSet(ctx, proto, atom_ready,
        JS_NewCFunction(ctx, js_media_session_get_ready, "get_ready", 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom_ready);

    JSAtom atom_has_session = JS_NewAtom(ctx, "hasSession");
    JS_DefinePropertyGetSet(ctx, proto, atom_has_session,
        JS_NewCFunction(ctx, js_media_session_get_has_session, "get_hasSession", 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom_has_session);
    
    JS_SetPropertyStr(ctx, proto, "getMetadata", JS_NewCFunction(ctx, js_media_session_get_metadata, "getMetadata", 0));
    JS_SetPropertyStr(ctx, proto, "getPlaybackInfo", JS_NewCFunction(ctx, js_media_session_get_playback_info, "getPlaybackInfo", 0));
    JS_SetPropertyStr(ctx, proto, "getTimelineInfo", JS_NewCFunction(ctx, js_media_session_get_timeline_info, "getTimelineInfo", 0));
    JS_SetPropertyStr(ctx, proto, "getSessionInfo", JS_NewCFunction(ctx, js_media_session_get_session_info, "getSessionInfo", 0));
    JS_SetPropertyStr(ctx, proto, "getAllSessions", JS_NewCFunction(ctx, js_media_session_get_all_sessions, "getAllSessions", 0));
    JS_SetPropertyStr(ctx, proto, "getAllSessionsDetailed", JS_NewCFunction(ctx, js_media_session_get_all_sessions_detailed, "getAllSessionsDetailed", 0));
    JS_SetPropertyStr(ctx, proto, "getSessionDetail", JS_NewCFunction(ctx, js_media_session_get_session_detail, "getSessionDetail", 1));

    JS_SetPropertyStr(ctx, proto, "play", JS_NewCFunction(ctx, js_media_session_play, "play", 0));
    JS_SetPropertyStr(ctx, proto, "pause", JS_NewCFunction(ctx, js_media_session_pause, "pause", 0));
    JS_SetPropertyStr(ctx, proto, "stop", JS_NewCFunction(ctx, js_media_session_stop, "stop", 0));
    JS_SetPropertyStr(ctx, proto, "next", JS_NewCFunction(ctx, js_media_session_next, "next", 0));
    JS_SetPropertyStr(ctx, proto, "prev", JS_NewCFunction(ctx, js_media_session_prev, "prev", 0));
    JS_SetPropertyStr(ctx, proto, "togglePlayPause", JS_NewCFunction(ctx, js_media_session_toggle_play_pause, "togglePlayPause", 0));
    JS_SetPropertyStr(ctx, proto, "fastForward", JS_NewCFunction(ctx, js_media_session_fast_forward, "fastForward", 0));
    JS_SetPropertyStr(ctx, proto, "rewind", JS_NewCFunction(ctx, js_media_session_rewind, "rewind", 0));
    JS_SetPropertyStr(ctx, proto, "channelUp", JS_NewCFunction(ctx, js_media_session_channel_up, "channelUp", 0));
    JS_SetPropertyStr(ctx, proto, "channelDown", JS_NewCFunction(ctx, js_media_session_channel_down, "channelDown", 0));
    JS_SetPropertyStr(ctx, proto, "seek", JS_NewCFunction(ctx, js_media_session_seek, "seek", 1));
    JS_SetPropertyStr(ctx, proto, "setPlaybackRate", JS_NewCFunction(ctx, js_media_session_set_playback_rate, "setPlaybackRate", 1));
    JS_SetPropertyStr(ctx, proto, "setShuffle", JS_NewCFunction(ctx, js_media_session_set_shuffle, "setShuffle", 1));
    JS_SetPropertyStr(ctx, proto, "setRepeatMode", JS_NewCFunction(ctx, js_media_session_set_repeat_mode, "setRepeatMode", 1));

    JS_SetPropertyStr(ctx, proto, "playSession", JS_NewCFunction(ctx, js_media_session_play_session, "playSession", 1));
    JS_SetPropertyStr(ctx, proto, "pauseSession", JS_NewCFunction(ctx, js_media_session_pause_session, "pauseSession", 1));
    JS_SetPropertyStr(ctx, proto, "stopSession", JS_NewCFunction(ctx, js_media_session_stop_session, "stopSession", 1));
    JS_SetPropertyStr(ctx, proto, "nextSession", JS_NewCFunction(ctx, js_media_session_next_session, "nextSession", 1));
    JS_SetPropertyStr(ctx, proto, "prevSession", JS_NewCFunction(ctx, js_media_session_prev_session, "prevSession", 1));
    JS_SetPropertyStr(ctx, proto, "togglePlayPauseSession", JS_NewCFunction(ctx, js_media_session_toggle_play_pause_session, "togglePlayPauseSession", 1));
    JS_SetPropertyStr(ctx, proto, "seekSession", JS_NewCFunction(ctx, js_media_session_seek_session, "seekSession", 2));
    JS_SetPropertyStr(ctx, proto, "setPlaybackRateSession", JS_NewCFunction(ctx, js_media_session_set_playback_rate_session, "setPlaybackRateSession", 2));
    JS_SetPropertyStr(ctx, proto, "setShuffleSession", JS_NewCFunction(ctx, js_media_session_set_shuffle_session, "setShuffleSession", 2));
    JS_SetPropertyStr(ctx, proto, "setRepeatModeSession", JS_NewCFunction(ctx, js_media_session_set_repeat_mode_session, "setRepeatModeSession", 2));

    JS_SetPropertyStr(ctx, proto, "onChanged", JS_NewCFunction(ctx, js_media_session_on_changed, "onChanged", 1));
    JS_SetPropertyStr(ctx, proto, "refresh", JS_NewCFunction(ctx, js_media_session_refresh, "refresh", 0));
    JS_SetPropertyStr(ctx, proto, "poll", JS_NewCFunction(ctx, js_media_session_poll, "poll", 0));
    
    // 设置类的原型
    JS_SetClassProto(ctx, js_media_session_class_id, proto);
    
    // 创建构造函数并设置原型关系
    JSValue ctor = JS_NewCFunction2(ctx, js_media_session_ctor, "MediaSession", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, proto);
    
    // 导出类
    JS_SetModuleExport(ctx, m, "MediaSession", ctor);
    return 0;
}

extern "C" __declspec(dllexport) JSModuleDef* js_init_module(JSContext* ctx, const char* module_name) {
    JSModuleDef* m = JS_NewCModule(ctx, module_name, js_media_module_init);
    if (!m) return NULL;
    JS_AddModuleExport(ctx, m, "MediaSession");
    return m;
}

// 供静态链接（WinRTMCPlugin 等内嵌场景）使用：
// 不带 dllexport，模块名固定为 "winrtmc"。
extern "C" JSModuleDef* js_init_module_winrtmc(JSContext* ctx) {
    return js_init_module(ctx, "winrtmc");
}
