import React from 'react';
import useBaseUrl from '@docusaurus/useBaseUrl';

type Item = {
  /** Path under static/, for example img/generated/resize/cubic.png */
  src: string;
  /** What produced this picture: shown under it and used as alt text. */
  label: string;
  /** Show hard pixel edges (for tiny images displayed larger than they are). */
  pixelated?: boolean;
};

type Props = {
  items: Item[];
  /** Displayed size in CSS pixels (square). */
  size?: number;
};

function Figure({ item, size }: { item: Item; size: number }): React.ReactElement {
  const src = useBaseUrl(item.src);
  return (
    <figure className="stb-compare__item">
      <img
        src={src}
        alt={item.label}
        width={size}
        height={size}
        loading="lazy"
        style={item.pixelated ? { imageRendering: 'pixelated' } : undefined}
      />
      <figcaption>{item.label}</figcaption>
    </figure>
  );
}

/** Pictures side by side, each labelled with what produced it. */
export default function Compare({ items, size = 192 }: Props): React.ReactElement {
  return (
    <div className="stb-compare">
      {items.map((it) => (
        <Figure key={it.src} item={it} size={size} />
      ))}
    </div>
  );
}
