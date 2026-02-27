/**
 * quickshiori.d.ts
 *
 * TypeScript declarations for the QuickShiori runtime environment.
 *
 * Usage
 * -----
 * Place (or symlink) this file where your tsconfig.json can find it, then
 * add the following to your tsconfig.json:
 *
 *   {
 *     "compilerOptions": {
 *       "module": "ES2022",          // or "ESNext"
 *       "moduleResolution": "Bundler", // or "Node16" / "NodeNext"
 *       "target": "ES2022",
 *       "typeRoots": [],
 *       "paths": {
 *         "quickshiori": ["./quickshiori.d.ts"]
 *       }
 *     }
 *   }
 *
 * Alternatively, if the file lives in a types/ folder next to your source:
 *
 *   "typeRoots": ["./types"]
 *
 * and rename/place this file as  types/quickshiori/index.d.ts .
 */

// =============================================================================
// Global declarations
// Injected into globalThis by the QuickShiori runtime before index.js runs.
// =============================================================================


/**
 * Absolute path to the ghost/SHIORI root directory (UTF-8, no trailing slash).
 * Set by the runtime before `index.js` is evaluated.
 *
 * @example
 * console.log(__shiori_dir); // "C:/SSP/ghost/myGhost"
 */
declare const __shiori_dir: string;

/**
 * Called by the runtime immediately after `index.js` finishes loading.
 * Receives the ghost directory path (same as `__shiori_dir`).
 *
 * Define this function in the top-level scope of `index.js` (or assign it to
 * `globalThis`) to run initialisation logic.
 *
 * > **Note:** The return value is ignored. Async functions are not supported
 * > here — the runtime does not await the result.
 *
 * @param dir - Absolute path to the ghost's SHIORI directory.
 */
declare function __shiori_load(dir: string): void;

/**
 * Called by the runtime for every SHIORI/3.0 request from the base ware.
 *
 * The raw request string (status line + headers, CRLF-delimited) is forwarded
 * as-is.  The function must return the complete response string (status line +
 * headers + optional body).  Returning `undefined` / `null` causes the runtime
 * to return a null response to the base ware.
 *
 * > **Note:** The runtime calls this function synchronously and immediately
 * > converts the return value to a string via `.toString()`.  Do **not** return
 * > a Promise — it will be serialised as `"[object Promise]"` and sent to the
 * > base ware verbatim.
 *
 * @param request - Raw SHIORI/3.0 request (e.g. "SHIORI/3.0 GET\r\nID: OnBoot\r\n\r\n").
 * @returns       - Complete SHIORI/3.0 response string, or void/null to send nothing.
 */
declare function __shiori_request(
  request: string
): string | null | undefined;

/**
 * Called by the runtime just before the JS engine is destroyed.
 * Use this to flush data, close handles, etc.
 *
 * > **Note:** The return value is ignored. Async functions are not supported
 * > here — the runtime does not await the result.
 */
declare function __shiori_unload(): void;


// =============================================================================
// Module: "quickshiori"
// Built-in C module compiled into quickshiori.dll.
// =============================================================================

declare module "quickshiori" {

  // ---------------------------------------------------------------------------
  // Logging
  // ---------------------------------------------------------------------------

  /** Valid log level strings (case-insensitive at runtime). */
  type LogLevel = "debug" | "info" | "warn" | "error";

  /**
   * Write a log entry at an explicit level.
   *
   * Arguments after `level` are stringified with `.toString()` and joined with
   * a single space — identical behaviour to `console.log`.
   *
   * Output goes to:
   *  - the Windows debugger (via `OutputDebugStringA`) — always
   *  - `<ghost_dir>/kashiwazaki.log`                   — when file output is enabled
   *
   * @param level - Log level ("debug" | "info" | "warn" | "error").
   * @param args  - Values to log.
   *
   * @example
   * log("warn", "retry count:", 3);
   */
  export function log(level: LogLevel, ...args: unknown[]): void;

  /**
   * Write a DEBUG-level log entry.
   * @param args - Values to log (joined with spaces).
   */
  export function debug(...args: unknown[]): void;

  /**
   * Write an INFO-level log entry.
   * @param args - Values to log (joined with spaces).
   */
  export function info(...args: unknown[]): void;

  /**
   * Write a WARN-level log entry.
   * @param args - Values to log (joined with spaces).
   */
  export function warn(...args: unknown[]): void;

  /**
   * Write an ERROR-level log entry.
   * @param args - Values to log (joined with spaces).
   */
  export function error(...args: unknown[]): void;

  /**
   * Change the minimum recorded log level at runtime.
   * Messages below this level are silently dropped.
   *
   * Level order (ascending severity): debug < info < warn < error < none
   *
   * Pass `"none"` to disable all logging.
   *
   * @param level - New minimum level.
   * @returns       The previous level string.
   *
   * @example
   * const prev = setLevel("debug"); // enable verbose output
   */
  export function setLevel(level: LogLevel | "none"): LogLevel | "none";

  /**
   * Enable or disable writing log entries to `kashiwazaki.log` on disk.
   * Debug-output (`OutputDebugStringA`) is unaffected by this setting.
   *
   * @param enabled - `true` to write to file, `false` to suppress file output.
   * @returns         The previous value.
   */
  export function setFileOutput(enabled: boolean): boolean;

  // ---------------------------------------------------------------------------
  // Runtime information
  // ---------------------------------------------------------------------------

  /** Runtime process information, analogous to Node.js `process`. */
  export const process: {
    /**
     * QuickShiori version string, e.g. `"0.0.1"`.
     * Resolved at compile time from the CMake project version.
     */
    readonly version: string;

    /**
     * Version strings for each runtime component.
     *
     * @example
     * console.log(process.versions["quickjs-ng"]); // "0.12.1"
     */
    readonly versions: {
      /** QuickShiori release version. */
      readonly quickshiori: string;
      /** QuickJS-ng engine version. */
      readonly "quickjs-ng": string;
    };
  };
}
