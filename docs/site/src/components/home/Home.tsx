import React, { useEffect, useRef, useState } from 'react';
import useBaseUrl from '@docusaurus/useBaseUrl';
import CodeBlock from '@theme/CodeBlock';
import languages from '@site/languages.json';
import meta from '@site/src/generated/meta.json';
import { COPY_CODE, FONTS_CODE, REFERENCES, START, TOUR, ZLIB_CODE, type Snippet } from './content';

const SIZES = [8, 12, 16, 24, 32, 48, 64, 96, 128];
const FILTERS = ['box', 'linear', 'cubic'] as const;
const TAB_KEY = 'docusaurus.tab.lang'; // the key Docusaurus' own Tabs use for groupId="lang", so the choice is shared with the lessons

const code = (s: Snippet) => (
  <CodeBlock language={s.language} title={s.title}>
    {s.code}
  </CodeBlock>
);

/** The entrance of each block as it scrolls in: only where script and motion are on, so content never waits for either. */
function useEntrance(root: React.RefObject<HTMLElement | null>): void {
  useEffect(() => {
    const el = root.current;
    if (!el) return;
    el.classList.add('js');
    const blocks = el.querySelectorAll('.reveal');
    if (!('IntersectionObserver' in window)) {
      blocks.forEach((b) => b.classList.add('in'));
      return;
    }
    const io = new IntersectionObserver(
      (entries) =>
        entries.forEach((e) => {
          if (e.isIntersecting) {
            e.target.classList.add('in');
            io.unobserve(e.target);
          }
        }),
      { threshold: 0.12 },
    );
    blocks.forEach((b) => io.observe(b));
    return () => io.disconnect();
  }, [root]);
}

function Demo(): React.ReactElement | null {
  const [filter, setFilter] = useState<(typeof FILTERS)[number]>('cubic');
  const [index, setIndex] = useState(3);
  const [missing, setMissing] = useState(false);
  const n = SIZES[index];
  const src = useBaseUrl(`showcase/${filter}-${n}.png`);
  if (missing) return null; // the frames are made at build time (stb_docs_figures); without them the demo is left out
  return (
    <figure className="demo" id="demo" aria-labelledby="demo-title">
      <div className="demo-frame">
        <img
          src={src}
          width={128}
          height={128}
          alt={`A photograph of fruit resized to ${n} by ${n} pixels with the ${filter} filter, shown enlarged`}
          onError={() => setMissing(true)}
        />
      </div>
      <figcaption className="demo-cap" id="demo-title" aria-live="polite">
        <code>
          img.resize({n}, {n}, "{filter}")
        </code>{' '}
        writes {(n * n).toLocaleString('en-US')} pixels. This is the file libstb wrote, shown enlarged.
      </figcaption>
      <div className="demo-controls">
        <div className="demo-row">
          <span>Filter</span>
          <span className="seg" role="group" aria-label="Resize filter">
            {FILTERS.map((f) => (
              <button key={f} type="button" aria-pressed={f === filter} onClick={() => setFilter(f)}>
                {f}
              </button>
            ))}
          </span>
        </div>
        <div className="demo-row">
          <label htmlFor="demo-size">Size</label>
          <input id="demo-size" type="range" min={0} max={SIZES.length - 1} step={1} value={index} onChange={(e) => setIndex(+e.target.value)} />
          <output htmlFor="demo-size">
            {n} &times; {n}
          </output>
        </div>
      </div>
    </figure>
  );
}

function Start(): React.ReactElement {
  const tabs = languages.languages.filter((l) => START[l.id]);
  const [lang, setLang] = useState(tabs[0]?.id);
  useEffect(() => {
    try {
      const saved = localStorage.getItem(TAB_KEY);
      if (saved && tabs.some((t) => t.id === saved)) setLang(saved);
    } catch {
      /* storage can be blocked */
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  const choose = (id: string) => {
    setLang(id);
    try {
      localStorage.setItem(TAB_KEY, id);
    } catch {
      /* storage can be blocked */
    }
  };
  const start = lang ? START[lang] : undefined;
  return (
    <section className="section" id="start">
      <div className="wrap">
        <div className="section-head reveal">
          <h2>Install and use</h2>
          <div className="tab-bar" role="tablist" aria-label="Language">
            {tabs.map((t) => (
              <button
                key={t.id}
                type="button"
                role="tab"
                aria-selected={t.id === lang}
                className={t.id === lang ? 'tab-btn active' : 'tab-btn'}
                onClick={() => choose(t.id)}
              >
                {t.label}
              </button>
            ))}
          </div>
        </div>
        {start ? (
          <>
            <div className="step">
              <h3>Install</h3>
              <div className="step-body">
                {start.install.map((s, i) => (
                  <React.Fragment key={i}>{code(s)}</React.Fragment>
                ))}
                <p className="install-note">{start.installNote}</p>
              </div>
            </div>
            <div className="step">
              <h3>Use</h3>
              <div className="step-body">{code(start.example)}</div>
            </div>
          </>
        ) : null}
      </div>
    </section>
  );
}

function TourImage({ name, size, alt }: { name: string; size: number; alt: string }): React.ReactElement {
  return <img src={useBaseUrl(`showcase/tour-${name}.png`)} width={size} height={size} alt={alt} />;
}

function Jobs(): React.ReactElement {
  const [tourMissing, setTourMissing] = useState(false);
  return (
    <section className="section" id="what">
      <div className="wrap">
        <h2 className="reveal">One library, three jobs</h2>
        <div className="jobs">
          <article className="job job-images reveal">
            <div>
              <h3>Images</h3>
              <p>
                Decode and encode PNG, JPEG, BMP and TGA. Resize with selectable filters, and edit with <code>crop</code>, <code>flip</code>,{' '}
                <code>rotate90</code>, <code>pad</code>, <code>convert</code> and <code>composite</code>, with EXIF orientation applied on request. Every
                operation returns a new image.
              </p>
            </div>
            {tourMissing ? null : (
              <ul className="tour" aria-label="One photograph and four edits of it, each written by libstb">
                {TOUR.map((t) => (
                  <li key={t.name} style={{ '--s': t.scale } as React.CSSProperties} onErrorCapture={() => setTourMissing(true)}>
                    <TourImage name={t.name} size={t.size} alt={t.alt} />
                    <span>
                      <code>{t.code}</code>
                    </span>
                  </li>
                ))}
              </ul>
            )}
          </article>

          <article className="job job-fonts reveal">
            <h3>Fonts</h3>
            <p>
              Render UTF-8 text from a TrueType font to a bitmap, or build a glyph atlas. To lay text out before you have a font file, estimate its size
              from built-in metrics.
            </p>
            <div style={{ marginTop: '1.4rem' }}>{code(FONTS_CODE)}</div>
          </article>

          <article className="job job-zlib reveal">
            <h3>zlib</h3>
            <p>
              Compress and decompress zlib streams. Decompression takes an output limit, so a decompression bomb ends in an error and not in an exhausted
              machine.
            </p>
            <div style={{ marginTop: '1.4rem' }}>{code(ZLIB_CODE)}</div>
          </article>
        </div>
      </div>
    </section>
  );
}

// ---------------------------------------------------------------------------------------------- proof

type Digest = {
  headline?: {
    rows?: { key: string; label: string; ratios: Record<string, number> }[];
    install?: { lib: string; label: string; total: string; kb: number }[];
  };
  verify?: { passed?: number } | null;
};

const SHORT: Record<string, string> = { libstb: 'libstb', libstb_srgb: 'libstb sRGB', pillow: 'Pillow', opencv: 'OpenCV', imageio: 'imageio', skimage: 'scikit-image', numpy: 'numpy' };
const ORDER = ['libstb', 'libstb_srgb', 'pillow', 'opencv', 'imageio', 'skimage', 'numpy'];
const byOrder = (a: string, b: string) => ((ORDER.indexOf(a) + 100) % 100) - ((ORDER.indexOf(b) + 100) % 100);

type Bar = { key: string; label: string; v: number; text: string };

function Bars({ rows, aria }: { rows: Bar[]; aria: string }): React.ReactElement {
  const ref = useRef<HTMLUListElement>(null);
  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    if (!('IntersectionObserver' in window)) {
      el.classList.add('in');
      return;
    }
    const io = new IntersectionObserver((es) => es.forEach((e) => e.isIntersecting && (e.target.classList.add('in'), io.unobserve(e.target))), { threshold: 0.2 });
    io.observe(el);
    return () => io.disconnect();
  }, []);
  const sorted = [...rows].sort((a, b) => a.v - b.v); // shortest first
  const top = Math.max(...sorted.map((r) => r.v)) || 1;
  return (
    <ul className="bars anim" role="img" aria-label={aria} ref={ref}>
      {sorted.map((r, i) => (
        <li key={r.key} className={r.key === 'libstb' ? 'bar-row is-self' : 'bar-row'}>
          <span className="bar-label">{r.label}</span>
          <span className="bar-track">
            <span className={`bar s-${r.key}`} style={{ '--w': `${Math.max((r.v / top) * 100, 1.5).toFixed(1)}%`, '--i': i } as React.CSSProperties} />
          </span>
          <span className="bar-value">{r.text}</span>
        </li>
      ))}
    </ul>
  );
}

/** Every number of the proof tiles comes from benchmarks/benchmark.json; without it the tiles keep their text. */
function useDigest(): Digest | null {
  const url = useBaseUrl('benchmarks/benchmark.json');
  const [data, setData] = useState<Digest | null>(null);
  useEffect(() => {
    let live = true;
    fetch(url)
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error(String(r.status)))))
      .then((d) => live && setData(d))
      .catch(() => undefined);
    return () => {
      live = false;
    };
  }, [url]);
  return data;
}

const capStyle: React.CSSProperties = { margin: '0 0 .4rem', fontWeight: 700, color: 'var(--fg)' };

function Proof(): React.ReactElement {
  const d = useDigest();
  const rows = d?.headline?.rows ?? [];
  const install = (d?.headline?.install ?? []).filter((r) => r.kb > 0);
  const passed = d?.verify?.passed;
  const href = useBaseUrl('benchmarks');
  return (
    <section className="section" id="why">
      <div className="wrap">
        <h2 className="reveal">What backs it up</h2>
        <div className="proofs">
          <article className="tile tile-speed reveal">
            <h3>Quick, with the losses shown.</h3>
            <p>
              The pixel kernels pick a SIMD backend at run time (AVX2, SSE2, NEON or scalar) and every backend gives identical output. The bars include the
              operations where Pillow or OpenCV is faster, and what each library weighs when you install it.
            </p>
            <div className="proof" id="proof-speed">
              {rows.map((row) => (
                <div key={row.key} style={{ marginTop: '1.1rem' }}>
                  <div className="cap" style={capStyle}>
                    {row.label}
                  </div>
                  <Bars
                    aria={`${row.label}: time relative to libstb`}
                    rows={Object.keys(row.ratios)
                      .sort(byOrder)
                      .map((k) => ({ key: k, label: SHORT[k] ?? k, v: row.ratios[k], text: `${row.ratios[k].toFixed(row.ratios[k] < 10 ? 2 : 1)}\u00d7` }))}
                  />
                </div>
              ))}
              <div className="cap">Time per operation, averaged over every corpus entry, as a multiple of libstb (1.0&times; is libstb; larger is slower).</div>
              {install.length >= 2 ? (
                <div>
                  <div className="cap" style={{ ...capStyle, margin: '1.2rem 0 .4rem' }}>
                    What pip downloads (wheel plus every dependency)
                  </div>
                  <Bars
                    aria="Total download size of each package"
                    rows={install.map((r) => ({ key: r.lib, label: r.label, v: r.kb, text: r.total }))}
                  />
                </div>
              ) : null}
              <p className="proof-note">
                <a href={href}>Open the full benchmark report</a>.
              </p>
            </div>
          </article>

          <div className="proof-col">
            <article className="tile tile-deps reveal">
              <h3>Nothing else to install.</h3>
              <p>
                No system libraries and no Python packages. The stb headers are private; the public API is <code>stb::image</code>, <code>stb::resizer</code>,{' '}
                <code>stb::font</code> and <code>stb::zlib_compress</code>.
              </p>
              <div className="proof">
                <div className="big">
                  0<small>dependencies</small>
                </div>
              </div>
            </article>

            <article className="tile tile-verify reveal">
              <h3>Checked before it is timed.</h3>
              <p>Every operation and library is run on small non-square images and its output compared before a single speed number is recorded.</p>
              <div className="proof" id="proof-verify">
                {typeof passed === 'number' ? (
                  <div className="big">
                    {passed}
                    <small>output checks passed before timing</small>
                  </div>
                ) : null}
                <div className="cap">Lossless formats must decode to the original pixels exactly; resize output is compared with Pillow's for the same filter.</div>
              </div>
            </article>

            <article className="tile tile-copy reveal">
              <h3>Your arrays, not copies.</h3>
              <p>
                The Python <code>Image</code> is the C++ image itself, not a wrapper. numpy and DLPack consumers such as torch get a view of the same memory,
                and Pillow converts with <code>to_pil()</code> and <code>from_pil()</code>.
              </p>
              <div style={{ marginTop: '1.4rem' }}>{code(COPY_CODE)}</div>
            </article>
          </div>
        </div>
      </div>
    </section>
  );
}

function References(): React.ReactElement {
  return (
    <section className="section" id="reference">
      <div className="wrap">
        <h2 className="reveal">API reference</h2>
        <div className="refs">
          {REFERENCES.map((r) => (
            <RefCard key={r.id} r={r} />
          ))}
        </div>
      </div>
    </section>
  );
}

function RefCard({ r }: { r: (typeof REFERENCES)[number] }): React.ReactElement {
  // A plain anchor on purpose: the references are static pages next to the site, not routes of it.
  return (
    <article className="ref reveal">
      <h3>{r.title}</h3>
      <p className="ref-badge">{r.badge}</p>
      <p className="ref-desc">{r.description}</p>
      <p className="ref-meta">{r.meta}</p>
      <a className="btn" href={useBaseUrl(r.href)}>
        {r.link}
      </a>
    </article>
  );
}

export default function Home(): React.ReactElement {
  const root = useRef<HTMLDivElement>(null);
  useEntrance(root);
  const version = (meta as { version?: string }).version;
  return (
    <div className="stb-home" ref={root}>
      <section className="hero">
        <div className="hero-grid">
          <div className="hero-copy">
            <h1>
              <span>Images, fonts and zlib,</span> <span>with no dependencies.</span>
            </h1>
            <p className="lede">
              A C++17 library and a Python module on top of Sean Barrett's <a href="https://github.com/nothings/stb">stb</a>. Decode, resize, edit and
              encode PNG, JPEG, BMP and TGA, render TrueType text, and compress zlib streams.
            </p>
            <div className="hero-actions">
              <a className="btn" href="#start">
                Install libstb
              </a>
              <a className="hero-link" href={useBaseUrl('cpp/html/index.html')}>
                C++ reference
              </a>
              <a className="hero-link" href={useBaseUrl('python/html/index.html')}>
                Python reference
              </a>
            </div>
            <p className="hero-note">
              C++17 core, Python 3.9+ module. Pre-built wheels for Linux, macOS and Windows. MIT license.{version ? ` v${version}.` : ''}
            </p>
          </div>
          <Demo />
        </div>
      </section>
      <Start />
      <Jobs />
      <Proof />
      <References />
    </div>
  );
}
