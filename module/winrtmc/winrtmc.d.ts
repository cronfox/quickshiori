// Type definitions for winrtmc module
// Generated based on quickjs_winrtmc.cpp

declare module 'winrtmc' {

  export interface MediaMetadata {
    title: string;
    subtitle: string;
    artist: string;
    album: string;
    albumTitle: string;
    albumArtist: string;
    trackNumber: number;
    albumTrackCount: number;
    hasThumbnail: boolean;
    genres: string[];
  }

  export interface MediaControls {
    play: boolean;
    pause: boolean;
    stop: boolean;
    record: boolean;
    next: boolean;
    prev: boolean;
    fastForward: boolean;
    rewind: boolean;
    channelUp: boolean;
    channelDown: boolean;
  }

  export interface PlaybackInfo {
    status: "Closed" | "Opened" | "Changing" | "Stopped" | "Playing" | "Paused" | "Unknown";
    type: "Music" | "Video" | "Image" | "Unknown";
    playbackRate?: number | null;
    shuffle?: boolean | null;
    repeatMode?: "None" | "Track" | "List" | null;
    controls: MediaControls;
  }

  export interface TimelineInfo {
    start: number;
    end: number;
    minSeek: number;
    maxSeek: number;
    position: number;
    lastUpdatedTicks: number;
  }

  export interface SessionInfo {
    ready: boolean;
    hasSession: boolean;
    sourceAppUserModelId: string;
  }

  export interface SessionDetail {
    sessionId: number;
    isCurrent: boolean;
    sourceAppUserModelId: string;
    metadata: MediaMetadata | null;
    playback: PlaybackInfo | null;
    timeline: TimelineInfo | null;
  }

  export class MediaSession {
    constructor();

    // Properties
    readonly title: string;
    readonly artist: string;
    readonly album: string;
    readonly sourceAppUserModelId: string;
    readonly ready: boolean;
    readonly hasSession: boolean;

    // Information Retrieval
    getMetadata(): MediaMetadata | null;
    getPlaybackInfo(): PlaybackInfo | null;
    getTimelineInfo(): TimelineInfo | null;
    getSessionInfo(): SessionInfo;

    /**
     * Returns a list of available sessions.
     * Implementation detail: returns basic info about sessions.
     */
    getAllSessions(): any[]; // Keeping any[] as the C++ implementation returns an array of objects but structure wasn't fully detailed in simple getAllSessions

    getAllSessionsDetailed(): SessionDetail[];

    getSessionDetail(sessionIdOrSource: number | string): SessionDetail | null;

    // Controls for the current specialized session
    play(): void;
    pause(): void;
    stop(): void;
    next(): void;
    prev(): void;
    togglePlayPause(): void;
    fastForward(): void;
    rewind(): void;
    channelUp(): void;
    channelDown(): void;
    seek(positionSeconds: number): void;
    setPlaybackRate(rate: number): void;
    setShuffle(enabled: boolean): void;
    setRepeatMode(mode: "None" | "Track" | "List"): void;

    // Controls for specific sessions (by ID or Source App ID)
    playSession(sessionIdOrSource: number | string): boolean;
    pauseSession(sessionIdOrSource: number | string): boolean;
    stopSession(sessionIdOrSource: number | string): boolean;
    nextSession(sessionIdOrSource: number | string): boolean;
    prevSession(sessionIdOrSource: number | string): boolean;
    togglePlayPauseSession(sessionIdOrSource: number | string): boolean;
    seekSession(sessionIdOrSource: number | string, positionSeconds: number): boolean;
    setPlaybackRateSession(sessionIdOrSource: number | string, rate: number): boolean;
    setShuffleSession(sessionIdOrSource: number | string, enabled: boolean): boolean;
    setRepeatModeSession(sessionIdOrSource: number | string, mode: "None" | "Track" | "List"): boolean;

    // Events & Lifecycle
    onChanged(callback: () => void): void;
    refresh(): boolean;
    poll(): void;
  }
}

declare module 'winrtmc.dll' {
  export * from 'winrtmc';
}