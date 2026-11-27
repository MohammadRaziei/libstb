import React from 'react';
import katex from 'katex';
import 'katex/dist/katex.min.css';

/** Inline LaTeX, rendered by KaTeX when the page is built (so it also works without script). */
export default function Tex({ children }: { children: string }): React.ReactElement {
  return <span dangerouslySetInnerHTML={{ __html: katex.renderToString(children, { throwOnError: false }) }} />;
}

/** E[quantity_L / quantity_ref]: the mean of each entry's own ratio. */
export const eVsRef = (symbol: 't' | 'm' | 's', refLabel: string): string =>
  String.raw`\mathbb{E}\!\left[\dfrac{${symbol}_{L}}{${symbol}_{\text{${refLabel}}}}\right]`;
