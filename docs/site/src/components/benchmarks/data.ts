import type { PointStyle } from 'chart.js';
import raw from '@site/src/generated/benchmark.json';

// The shape of benchmarks/outputs/benchmark.json, written by benchmarks/report/gather.py.
export type Point = { x: number; y: number };
export type Series = { lib: string; points: Point[] };
export type Op = {
  key: string;
  title: string;
  note: string;
  libs: string[];
  reference: string | null;
  wins: Record<string, number>;
  n_compared: number;
  time_ratio: Record<string, number>;
  mem_reference: string | null;
  mem_ratio: Record<string, number>;
  n_mem_compared: number;
  size_reference: string | null;
  size_ratio: Record<string, { ratio: number; n: number }>;
  trend: { throughput: Series[]; time_ms: Series[]; memory_mb: Series[] };
};
export type Section = { id: string; title: string; lede: string; ops: Op[] };
export type FontRow = { case: string; libstb_us: number; pillow_us: number; libstb: string; pillow: string; ratio: number };
export type FontOp = { key: string; title: string; rows: FontRow[]; mean_ratio: number | null };
export type InstallRow = { lib: string; label: string; version: string; own: string; total: string; kb: number; n_deps: number; capability: string };
export type Bench = {
  generated: string | null;
  repeats: string | number | null;
  n_entries: string | number;
  libraries: Record<string, string>;
  headline: { rows: { key: string; label: string; reference: string; ratios: Record<string, number> }[]; install: InstallRow[] };
  sections: Section[];
  scaling: { sides: number[]; panels: { key: string; title: string; series: Series[] }[] };
  fonts: { font: string; ops: FontOp[] };
  install: InstallRow[];
  verify: { passed: number; unavailable: number; failed: number; failures: unknown[] } | null;
  system: Record<string, string | number> | null;
};

export const bench = raw as unknown as Bench;

export const LABELS: Record<string, string> = {
  libstb: 'libstb',
  libstb_srgb: 'libstb (sRGB-correct)',
  pillow: 'Pillow',
  opencv: 'OpenCV',
  imageio: 'imageio',
  skimage: 'scikit-image',
  numpy: 'numpy',
};
export const label = (lib: string): string => bench.libraries?.[lib] ?? LABELS[lib] ?? lib;

// Colour is never the only way to tell two series apart: every multi-series chart also varies the point
// shape and the line dash per library.
export const POINT_STYLES: Record<string, PointStyle> = {
  libstb: 'circle',
  libstb_srgb: 'rectRot',
  pillow: 'triangle',
  opencv: 'rect',
  imageio: 'star',
  skimage: 'crossRot',
};
export const DASH: Record<string, number[]> = {
  libstb: [],
  libstb_srgb: [4, 2],
  pillow: [6, 3],
  opencv: [2, 2],
  imageio: [8, 3, 2, 3],
  skimage: [1, 3],
};

export const KICKERS: Record<string, string> = { decoding: 'Decoding', encoding: 'Encoding', resizing: 'Resizing', editing: 'Editing' };
