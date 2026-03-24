/*
 * Copyright (c) 2026 Cronfox
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

declare module 'ukafmo' {
    /**
     * A single FMO entry with its 32-byte id and all key-value pairs.
     */
    export interface FmoEntry {
        /** 32-byte unique identifier of the ghost/Sakura instance. */
        id: string;
        /** All other FMO fields as key-value pairs. */
        [key: string]: string;
    }

    /**
     * Check whether a Ukagaka baseware (SSP, Materia, or CROW) is currently running.
     *
     * Detects baseware by probing the `ssp` or `sakura` named mutexes.
     * @returns `true` if a baseware instance is running; otherwise `false`.
     */
    export function isRunning(): boolean;

    /**
     * Read and parse the FMO (File Mapping Object) shared memory.
     *
     * Tries SakuraUnicodeFMO (UTF-8) first, then SakuraFMO (CP_ACP → UTF-8).
     * Acquires the corresponding mutex for exclusive read.
     *
     * @returns An array of {@link FmoEntry} objects. Returns an empty array
     *          if no baseware is running or FMO data cannot be read.
     */
    export function readFMO(): FmoEntry[];

    /**
     * Read the raw FMO shared memory as a UTF-8 string.
     *
     * Tries SakuraUnicodeFMO (UTF-8) first, then SakuraFMO (CP_ACP → UTF-8).
     * Acquires the corresponding mutex for exclusive read.
     *
     * @returns Raw FMO content as a UTF-8 string, or `null` if not available.
     */
    export function readFMORaw(): string | null;
}

declare module 'ukafmo.dll' {
    export * from 'ukafmo';
}
