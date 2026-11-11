import type { ChartConfiguration } from 'chart.js';
import type { Theme } from './Chart';
import { DASH, POINT_STYLES, label, type Series } from './data';

/** One line per library, with markers: log-log scatter charts (throughput, time) and the memory chart. */
export function xyConfig(
  theme: Theme,
  series: Series[],
  xTitle: string,
  yTitle: string,
  yLog: boolean,
): ChartConfiguration {
  return {
    type: 'scatter',
    data: {
      datasets: series.map((s) => {
        const color = theme.series(s.lib);
        return {
          label: label(s.lib),
          data: s.points,
          borderColor: color,
          backgroundColor: color,
          pointStyle: POINT_STYLES[s.lib] ?? 'circle',
          borderDash: DASH[s.lib] ?? [],
          showLine: true,
          tension: 0.2,
          pointRadius: 5,
          pointHoverRadius: 8,
        };
      }),
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      scales: {
        x: { type: 'logarithmic', title: { display: true, text: xTitle } },
        y: { type: yLog ? 'logarithmic' : 'linear', title: { display: true, text: yTitle } },
      },
      plugins: { legend: { position: 'bottom' } },
    },
  };
}

/** Horizontal bars, one colour per library: the install footprint. */
export function barConfig(labels: string[], values: number[], colors: string[], unit: string): ChartConfiguration {
  return {
    type: 'bar',
    data: { labels, datasets: [{ data: values, backgroundColor: colors }] },
    options: {
      indexAxis: 'y',
      responsive: true,
      maintainAspectRatio: false,
      scales: { x: { title: { display: true, text: unit } }, y: { grid: { display: false } } },
      plugins: { legend: { display: false } },
    },
  };
}

/** Two bars per case on a log axis: libstb against Pillow, in microseconds. */
export function groupedBarConfig(
  theme: Theme,
  labels: string[],
  a: { lib: string; values: number[] },
  b: { lib: string; values: number[] },
  yTitle: string,
): ChartConfiguration {
  return {
    type: 'bar',
    data: {
      labels,
      datasets: [
        { label: label(a.lib), data: a.values, backgroundColor: theme.series(a.lib) },
        { label: label(b.lib), data: b.values, backgroundColor: theme.series(b.lib) },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      scales: { y: { type: 'logarithmic', title: { display: true, text: yTitle } } },
      plugins: { legend: { position: 'bottom' } },
    },
  };
}
