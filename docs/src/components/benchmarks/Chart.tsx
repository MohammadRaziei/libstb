import React, { useEffect, useRef } from 'react';
import { useColorMode } from '@docusaurus/theme-common';
import type { ChartConfiguration } from 'chart.js';

export type Theme = { series: (lib: string) => string; fg2: string; border: string };

function readTheme(): Theme {
  const cs = getComputedStyle(document.documentElement);
  const v = (name: string, fallback: string) => cs.getPropertyValue(name).trim() || fallback;
  return {
    series: (lib) => v(`--s-${lib}`, '#858c96'),
    fg2: v('--fg2', '#3f454d'),
    border: v('--border', '#d3d6db'),
  };
}

type Props = {
  /** Spoken description of the chart. */
  label: string;
  /** Builds the Chart.js configuration from the theme's colours. Called again when the theme changes. */
  build: (theme: Theme) => ChartConfiguration;
  tall?: boolean;
};

/**
 * One Chart.js chart on a canvas. Chart.js is loaded in the browser only (dynamic import), and the chart is
 * drawn again with the other theme's series colours (--s-<lib> in tokens.css) when the colour mode changes.
 */
export default function Chart({ label, build, tall }: Props): React.ReactElement {
  const canvas = useRef<HTMLCanvasElement>(null);
  const { colorMode } = useColorMode();

  useEffect(() => {
    let chart: { destroy(): void } | undefined;
    let cancelled = false;
    let frame = 0;
    import('chart.js/auto').then(({ default: ChartJS }) => {
      // Docusaurus sets data-theme after this effect has run, so the colours are read one frame later.
      frame = requestAnimationFrame(() => {
        if (cancelled || !canvas.current) return;
        const theme = readTheme();
        ChartJS.defaults.color = theme.fg2;
        ChartJS.defaults.borderColor = theme.border;
        chart = new ChartJS(canvas.current, build(theme));
      });
    });
    return () => {
      cancelled = true;
      cancelAnimationFrame(frame);
      chart?.destroy();
    };
    // build is rebuilt on every render of the page; the chart only needs to be drawn again for a new theme
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [colorMode]);

  return (
    <div className={tall ? 'chart-box tall' : 'chart-box'}>
      <canvas ref={canvas} role="img" aria-label={label} />
    </div>
  );
}
