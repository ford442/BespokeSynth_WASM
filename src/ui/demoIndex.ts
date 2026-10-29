// Demo gallery index parsing; no imports, so tests/unit can load it under node.

/** One entry of resource-pack/demos/index.json (written by scripts/gen_demos.py). */
export interface DemoEntry {
  id: string;
  file: string;
  title: string;
  description: string;
  modules: string[];
}

function isDemoEntry(value: unknown): value is DemoEntry {
  if (!value || typeof value !== 'object') return false;
  const entry = value as Record<string, unknown>;
  return (
    typeof entry.id === 'string' &&
    typeof entry.file === 'string' &&
    typeof entry.title === 'string' &&
    typeof entry.description === 'string' &&
    Array.isArray(entry.modules)
  );
}

/** Parses the demo index; malformed entries are skipped, a malformed file yields none. */
export function parseDemoIndex(text: string): DemoEntry[] {
  if (!text) return [];
  try {
    const data: unknown = JSON.parse(text);
    const demos = data && typeof data === 'object' ? (data as { demos?: unknown }).demos : undefined;
    return Array.isArray(demos) ? demos.filter(isDemoEntry) : [];
  } catch {
    return [];
  }
}
