/**
 * WinRTMCPlugin – entry.js
 *
 * このファイルは qjsc によってビルド時にバイトコード（C 配列）にコンパイルされ、
 * winrtmc_plugin.dll に静的に埋め込まれます。
 *
 * 利用可能なモジュール
 *   "quickshiori"  – ログ / process.version などのユーティリティ
 *   "winrtmc"      – Windows メディアセッション制御 (MediaSession クラス)
 *
 * グローバルに公開すべき SHIORI フック関数
 *   __shiori_load(dir)       : SHIORI load / loadu に対応
 *   __shiori_request(raw)    : PLUGIN/2.0 request に対応
 *   __shiori_unload()        : SHIORI unload に対応
 */

import { info, warn, error } from "quickshiori";
import { MediaSession } from "winrtmc";

// ---------------------------------------------------------------------------
// PLUGIN/2.0 minimal parser  (精简自 KashiwazakiParser)
// ---------------------------------------------------------------------------

const PLUGIN_STATUS = { 200: "OK", 204: "No Content", 400: "Bad Request", 500: "Internal Server Error" };

/**
 * PLUGIN/2.0 リクエスト文字列をパースする。
 * @param {string} raw
 * @returns {{ method: string, version: string, headers: Object<string,string>, reference: string[] }}
 */
function parseRequest(raw) {
    const lines = raw.split("\r\n").filter(l => l !== "");
    const firstLine = lines[0];
    const method = firstLine.startsWith("GET") ? "GET"
        : firstLine.startsWith("NOTIFY") ? "NOTIFY"
            : (() => { throw new Error("Unknown PLUGIN method: " + firstLine) })();
    const version = firstLine.split("/")[1];

    const headers = {};
    const reference = [];
    for (let i = 1; i < lines.length; i++) {
        const sep = lines[i].indexOf(": ");
        if (sep === -1) continue;
        const k = lines[i].slice(0, sep);
        const v = lines[i].slice(sep + 2);
        if (k.startsWith("Reference")) {
            reference[parseInt(k.slice(9))] = v;
        } else {
            headers[k] = v;
        }
    }
    return { method, version, headers, reference };
}

/**
 * PLUGIN/2.0 レスポンスオブジェクトを文字列に変換する。
 * @param {{ statusCode: number, headers?: Object<string,string>, value?: string[]  }} res
 * @returns {string}
 */
function buildResponse({ statusCode, headers = {}, reference = [] }) {
    const statusText = PLUGIN_STATUS[statusCode];
    if (!statusText) throw new Error("Unknown status code: " + statusCode);
    let s = `PLUGIN/2.0 ${statusCode} ${statusText}\r\n`;
    for (const [k, v] of Object.entries(headers)) s += `${k}: ${v}\r\n`;
    for (let i = 0; i < reference.length; i++) s += `Reference${i}: ${reference[i]}\r\n`;
    return s + "\r\n";
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

/** @type {MediaSession | null} */
let media = null;

// ---------------------------------------------------------------------------
// EventDispatcher
// ---------------------------------------------------------------------------
class EventDispatcher {
    constructor() {
        this.listeners = {
            "GET": {},
            "NOTIFY": {},
        };
    }
    dispatch(raw) {
        let req;
        try {
            req = parseRequest(raw);
        } catch (e) {
            warn("Failed to parse request: " + e);
            return buildResponse({ statusCode: 400, headers: { Charset: "UTF-8" } });
        }
        const handlers = this.listeners[req.method] || {};
        const handler = handlers[req.headers["ID"] ?? ""] || (() => buildResponse({ statusCode: 204, headers: { Charset: "UTF-8" } }));
        return handler(req);
    }
    /**
     * Register an event handler
     * @param {string} method 
     * @param {string} id 
     * @param {Function} handler 
     */
    on(method, id, handler) {
        if (!this.listeners[method]) {
            throw new Error("Unsupported method: " + method);
        }
        this.listeners[method][id] = handler;
    }
}

// ---------------------------------------------------------------------------
// SHIORI hooks  (called by winrtmc_plugin.dll)
// ---------------------------------------------------------------------------

const dispatcher = new EventDispatcher();
let lastInfo = ""
dispatcher.on("GET", "OnSecondChange", req => {
    if (!media) {
        return buildResponse({ statusCode: 500, headers: { Charset: "UTF-8" } });
    }
    media.poll();
    let metadata = media.getMetadata();
    let info = getInfoString(metadata);
    if (info !== lastInfo) {
        lastInfo = info
        return buildResponse({
            statusCode: 200, headers: {
                Charset: "UTF-8",
                Event: "WinRTMC.MusicChanged",
                EventOption: "notify",
                Target: "__SYSTEM_ALL_GHOST__"
            }, reference: [info]
        });
    }
    return buildResponse({ statusCode: 204, headers: { Charset: "UTF-8" } });
})
dispatcher.on("GET", "OnMenuExec", req => {
    media.poll();
    let metadata = media.getMetadata();
    let info = getInfoString(metadata);
    return buildResponse({
        statusCode: 200, headers: {
            Charset: "UTF-8",
            Event: "WinRTMC.MusicChanged",
            EventOption: "notify",
            Target: "__SYSTEM_ALL_GHOST__"
        }, reference: [info]
    });
})
function getInfoString(metadata) {
    return [metadata.title, metadata.artist, metadata.albumTitle, metadata.albumArtist, metadata.genres.join("|"), metadata.trackNumber, metadata.albumTrackCount].join("\x01")
}



globalThis.__shiori_load = function (dir) {
    info("WinRTMCPlugin loaded. dir=" + dir);
    try {
        media = new MediaSession();
        info("MediaSession created successfully.");
    } catch (e) {
        error("Failed to create MediaSession: " + e);
    }
};

globalThis.__shiori_request = function (rawRequest) {
    return dispatcher.dispatch(rawRequest);
};

globalThis.__shiori_unload = function () {
    info("WinRTMCPlugin unloaded.");
    media = null;
};

