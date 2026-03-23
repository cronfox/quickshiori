import { isRunning,readFMO, readFMORaw } from "ukafmo.dll";

// ── isRunning ────────────────────────────────────────────────────────
print("=== isRunning ===");
const running = isRunning();
print("baseware running:", running);

// ── readFMORaw ───────────────────────────────────────────────────────
print("\n=== readFMORaw ===");
const raw = readFMORaw();
if (raw === null) {
    print("FMO not found (baseware not running?)");
} else {
    print("raw FMO length:", raw.length, "chars");
    print("raw FMO (first 500 chars):");
    print(raw.substring(0, 500));
}

// ── readFMO ──────────────────────────────────────────────────────────
print("\n=== readFMO ===");
const ghosts = readFMO();
print("ghost count:", ghosts.length);
for (let i = 0; i < ghosts.length; i++) {
    const g = ghosts[i];
    print("\n-- ghost [" + i + "] --");
    print("  id:", g.id);
    const keys = Object.keys(g).filter(k => k !== "id");
    for (const k of keys) {
        print("  " + k + ":", g[k]);
    }
}
