import React from 'react';
import useBaseUrl from '@docusaurus/useBaseUrl';
import Chart from './Chart';
import Tex, { eVsRef } from './Tex';
import { barConfig, groupedBarConfig, xyConfig } from './charts';
import { bench, label, type FontOp, type Op } from './data';

// One component per page of the Benchmarks chapter (docs/benchmarks/*.mdx). They all draw benchmarks/outputs/benchmark.json, which
// benchmarks/report/gather.py writes and `prepare.py bench` copies to src/generated/. The text of a page is MDX; what comes from the
// data (numbers, charts, tables) is drawn here.

const ratioList = (r: Record<string, number>) =>
  Object.entries(r)
    .sort((a, b) => a[1] - b[1])
    .map(([lib, e]) => `${label(lib)} ${e.toFixed(1)}\u00d7`)
    .join(', ');

/** The notes in benchmark.json hold a little HTML: only <code> is kept, as an element. */
function Note({ html }: { html: string }): React.ReactElement {
  return (
    <>
      {html.split(/(<code>.*?<\/code>)/g).map((part, i) =>
        part.startsWith('<code>') ? <code key={i}>{part.slice(6, -7)}</code> : part.replace(/<[^>]+>/g, ''),
      )}
    </>
  );
}

function DownloadLink({ name }: { name: string }): React.ReactElement {
  const href = useBaseUrl(`benchmarks/results/${name}.json`);
  return (
    <a className="dl-btn" href={href} download>
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
        <path d="M12 3v12" />
        <path d="m7 11 5 5 5-5" />
        <path d="M5 21h14" />
      </svg>
      download {name}.json
    </a>
  );
}

const Downloads = ({ names }: { names: string[] }) => (
  <div className="dl-row">
    {names.map((n) => (
      <DownloadLink key={n} name={n} />
    ))}
  </div>
);

const MX = 'Image size (megapixels, log scale)';

const hasData = bench.sections.length > 0 || bench.install.length > 0;
const hasMemory = bench.sections.some((s) => s.ops.some((o) => o.n_mem_compared > 0 || o.trend.memory_mb.length > 0));

/** Every component below sits in this wrapper, which also says so when the build has no data. */
function Page({ children }: { children: React.ReactNode }): React.ReactElement {
  if (!hasData) {
    return (
      <p className="stb-bench-empty">
        There is no benchmark data in this build. Run the benchmarks (<code>cmake --build benchmarks/build --target stb_benchmark</code>), commit{' '}
        <code>benchmarks/outputs/</code>, and build the site again.
      </p>
    );
  }
  return <div className="stb-bench-page">{children}</div>;
}

// ---------------------------------------------------------------- overview

export function Overview(): React.ReactElement {
  const libs = Object.keys(bench.libraries).filter((l) => bench.headline.rows.some((r) => l in r.ratios));
  const sys = bench.system;
  const meta = [`${bench.n_entries} corpus entries`, `${bench.repeats} repeats per cell`];
  if (bench.generated) meta.push(`generated ${bench.generated}`);
  return (
    <Page>
      <p className="meta">{meta.join(' \u00b7 ')}</p>

      <h2 id="at-a-glance">At a glance</h2>
      <p>
        Mean time of each library as a multiple of the reference, the library that was fastest on the most corpus entries (the reference column).
        1.00&times; is the reference itself, and below 1 means less time than the reference. How the mean is taken, and why a mean of ratios
        needs care, is on the <a href={useBaseUrl('docs/benchmarks/method')}>method</a> page.
      </p>
      <table className="headline">
        <thead>
          <tr>
            <th>Operation</th>
            <th>Reference</th>
            {libs.map((l) => (
              <th key={l} className="num">
                {label(l)}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {bench.headline.rows.map((r) => (
            <tr key={r.key}>
              <td>{r.label}</td>
              <td>
                <strong>{label(r.reference)}</strong>
              </td>
              {libs.map((l) => (
                <td key={l} className={l === 'libstb' ? 'num self' : 'num'}>
                  {l in r.ratios ? `${r.ratios[l].toFixed(2)}\u00d7` : '\u2013'}
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>

      <h2 id="environment">Where these numbers came from</h2>
      {bench.verify ? (
        <p>
          Before anything was timed, every operation and library was run on small non-square images and its output checked (exact pixels for
          lossless formats, close for JPEG and resizing): {bench.verify.passed} checks passed, {bench.verify.failed} failed,{' '}
          {bench.verify.unavailable} library/operation pairs unavailable (formats a library cannot handle at all).
        </p>
      ) : null}
      {sys ? (
        <>
          <table className="env-table">
            <tbody>
              <tr><td>CPU</td><td>{sys.cpu_model}</td></tr>
              <tr><td>Logical cores</td><td>{sys.logical_cores}</td></tr>
              {sys.ram_gib ? <tr><td>RAM</td><td>{sys.ram_gib} GiB</td></tr> : null}
              <tr><td>Architecture</td><td>{sys.architecture}</td></tr>
              <tr><td>OS</td><td>{sys.os}</td></tr>
              <tr><td>Python</td><td>{sys.python_implementation} {sys.python_version}</td></tr>
            </tbody>
          </table>
          <Downloads names={['system_info', 'verify']} />
        </>
      ) : null}
    </Page>
  );
}

// ---------------------------------------------------------------- method

export function Method(): React.ReactElement {
  return (
    <Page>
      <p>
        Each corpus entry <Tex>i</Tex> is one image (a genre, a size and a format). For a library <Tex>L</Tex>, <Tex>{String.raw`t_i^{L}`}</Tex> is the best
        of {bench.repeats} timed runs of one operation on entry <Tex>i</Tex>, <Tex>{String.raw`s_i^{L}`}</Tex> is the size in bytes of the file it wrote, and{' '}
        <Tex>{String.raw`m_i^{L}`}</Tex> is its extra peak memory. Two libraries are compared only on the entries where both ran.
      </p>
      <ul className="method">
        <li>
          <strong>Reference library.</strong> <Tex>{String.raw`W_L=\bigl|\{\,i : t_i^{L}=\min_K t_i^{K}\,\}\bigr|`}</Tex> is the number of entries on which{' '}
          <Tex>L</Tex> was fastest, and <Tex>{String.raw`\mathrm{ref}=\arg\max_L W_L`}</Tex> is the library that won the most. The count shows up in the
          per-operation summaries.
        </li>
        <li>
          <strong>Headline ratio.</strong> Over the <Tex>N</Tex> entries both libraries ran,{' '}
          <Tex>{String.raw`\mathbb{E}\!\left[\dfrac{t_L}{t_{\mathrm{ref}}}\right]=\dfrac{1}{N}\sum_{i=1}^{N}\dfrac{t_i^{L}}{t_i^{\mathrm{ref}}}`}</Tex>. It is the
          mean of each entry's own ratio (ratio first, then average), so a 64x64 image counts as much as a 4000x4000 one. Above 1 means <Tex>L</Tex> took
          longer than the reference. Output size and extra memory use the same formula with <Tex>s</Tex> and <Tex>m</Tex> in place of <Tex>t</Tex>.
        </li>
        <li>
          <strong>Trend charts.</strong> For an image of <Tex>z</Tex> pixels, let <Tex>{String.raw`G_z`}</Tex> be the synthetic entries of that size (photo,
          gradient, noise, graphics). Each library's point is <Tex>{String.raw`\bar t_L(z)=\Bigl(\prod_{i\in G_z} t_i^{L}\Bigr)^{1/|G_z|}`}</Tex> for time,
          throughput <Tex>{String.raw`\dfrac{z/10^{6}}{\bar t_L(z)}`}</Tex> in MP/s, and{' '}
          <Tex>{String.raw`\bar m_L(z)=\dfrac{1}{|G_z|}\sum_{i\in G_z} m_i^{L}`}</Tex> for memory. Real photographs each have an odd size of their own, so they
          are left out of these charts (they still count in every ratio above).
        </li>
        <li>
          <strong>Reading a ratio.</strong> Averaging ratios is not symmetric: a library 10 times faster on one entry and 10 times slower on another scores{' '}
          <Tex>{String.raw`\mathbb{E}=(0.1+10)/2=5.05`}</Tex>, not 1. Read it together with the win count <Tex>{String.raw`W_L`}</Tex>.
        </li>
      </ul>
    </Page>
  );
}

// ---------------------------------------------------------------- decoding, encoding, resizing, editing

function OpBody({ op }: { op: Op }): React.ReactElement {
  const timeRest = ratioList(op.time_ratio);
  const memRest = ratioList(op.mem_ratio);
  const sizeRows = Object.entries(op.size_ratio);
  const { throughput, time_ms, memory_mb } = op.trend;
  return (
    <>
      {op.n_compared > 0 && op.reference ? (
        <p className="op-winner">
          <strong>{label(op.reference)}</strong> is the reference (fastest on the most corpus entries). On average (
          <Tex>{eVsRef('t', label(op.reference))}</Tex>, the mean of each entry's own ratio, not a ratio of medians)
          {timeRest ? `: ${timeRest} as long.` : '.'}
        </p>
      ) : null}
      {op.n_mem_compared > 0 && op.mem_reference ? (
        <p className="op-winner">
          <strong>{label(op.mem_reference)}</strong> needs the least extra memory on the most entries (extra peak RSS on top of what the process held before
          the operation started; images under 0.065 MP are not measured)
          {memRest ? (
            <>
              . On average, <Tex>{eVsRef('m', label(op.mem_reference))}</Tex>: {memRest} as much.
            </>
          ) : (
            '.'
          )}
        </p>
      ) : null}

      {sizeRows.length > 0 && op.size_reference ? (
        <table className="size-rows">
          <thead>
            <tr>
              <th>Output size</th>
              <th className="num">
                <Tex>{eVsRef('s', label(op.size_reference))}</Tex>
              </th>
              <th className="num">entries</th>
            </tr>
          </thead>
          <tbody>
            {sizeRows.map(([lib, r]) => (
              <tr key={lib}>
                <td>{label(lib)}</td>
                <td className="num">{r.ratio.toFixed(2)}&times;</td>
                <td className="num">{r.n}</td>
              </tr>
            ))}
          </tbody>
        </table>
      ) : null}

      {throughput.length > 0 ? (
        <div className="panel">
          <h3>
            Throughput vs. image size <span className="dir">(higher is better)</span>
          </h3>
          <Chart label={`${op.title}: throughput against image size`} build={(t) => xyConfig(t, throughput, MX, 'Throughput (MP/s, log scale)', true)} />
        </div>
      ) : null}

      {time_ms.length > 0 ? (
        <div className="panel">
          <h3>
            Time vs. image size <span className="dir">(lower is better)</span>
          </h3>
          <Chart label={`${op.title}: time against image size`} build={(t) => xyConfig(t, time_ms, MX, 'Time (ms, log scale)', true)} />
        </div>
      ) : null}
      {memory_mb.length > 0 ? (
        <div className="panel">
          <h3>
            Extra peak memory vs. image size <span className="dir">(lower is better)</span>
          </h3>
          <Chart label={`${op.title}: extra peak memory against image size`} build={(t) => xyConfig(t, memory_mb, MX, 'Extra peak memory (MB)', false)} />
        </div>
      ) : null}
    </>
  );
}

/** One section of the digest: "decoding", "encoding", "resizing" or "editing". */
export function Ops({ id }: { id: string }): React.ReactElement {
  const sec = bench.sections.find((s) => s.id === id);
  return (
    <Page>
      {sec ? (
        <>
          <p>
            {sec.lede} Speed is the best of {bench.repeats} runs; memory{' '}
            {hasMemory ? 'is the extra peak RSS of one isolated process per measurement' : 'was not measured'}.
          </p>
          {sec.ops.map((op) => (
            <div className="op" key={op.key}>
              <h2 id={op.key}>{op.title}</h2>
              <p className="op-note">
                <Note html={op.note} />
              </p>
              <OpBody op={op} />
            </div>
          ))}
          <Downloads names={['throughput', 'throughput_memory']} />
        </>
      ) : (
        <p>There is no “{id}” section in this build's benchmark data.</p>
      )}
    </Page>
  );
}

// ---------------------------------------------------------------- scaling

export function Scaling(): React.ReactElement {
  const { sides, panels } = bench.scaling;
  return (
    <Page>
      {panels.length > 0 ? (
        <>
          <p>
            One photo-like image at each side length from {sides[0]} to {sides[sides.length - 1]} pixels, one representative operation of each kind, libstb
            against Pillow and OpenCV. A flat line means the cost per pixel stays constant; a falling line means it grows with the image.
          </p>
          {panels.map((panel) => (
            <div className="panel" key={panel.key}>
              <h3>
                {panel.title} <span className="dir">(throughput, higher is better)</span>
              </h3>
              <Chart label={`${panel.title}: throughput against image size`} build={(t) => xyConfig(t, panel.series, MX, 'Throughput (MP/s, log scale)', true)} />
            </div>
          ))}
          <Downloads names={['scaling']} />
        </>
      ) : (
        <p>There is no scaling data in this build.</p>
      )}
    </Page>
  );
}

// ---------------------------------------------------------------- fonts

function FontBlock({ op }: { op: FontOp }): React.ReactElement {
  const labels = op.rows.map((r) => r.case);
  return (
    <div className="op">
      <h2 id={op.key}>{op.title}</h2>
      {op.mean_ratio ? (
        <p className="op-winner">
          On average (<Tex>{String.raw`\mathbb{E}\!\left[\dfrac{t_{\text{libstb}}}{t_{\text{Pillow}}}\right]`}</Tex>, the mean of each case's own ratio):{' '}
          <strong>{op.mean_ratio.toFixed(2)}&times;</strong>; below 1 means libstb took less time.
        </p>
      ) : null}
      <div className="panel">
        <h3>
          Time per call <span className="dir">(lower is better)</span>
        </h3>
        <Chart
          label={`${op.title}: time per call, libstb against Pillow`}
          build={(t) =>
            groupedBarConfig(
              t,
              labels,
              { lib: 'libstb', values: op.rows.map((r) => Math.round(r.libstb_us * 1000) / 1000) },
              { lib: 'pillow', values: op.rows.map((r) => Math.round(r.pillow_us * 1000) / 1000) },
              '\u00b5s (log scale)',
            )
          }
        />
      </div>
      <details className="mini-details">
        <summary>See the numbers</summary>
        <table>
          <thead>
            <tr>
              <th>Case</th>
              <th className="num">libstb</th>
              <th className="num">Pillow</th>
              <th className="num">libstb / Pillow</th>
            </tr>
          </thead>
          <tbody>
            {op.rows.map((r) => (
              <tr key={r.case}>
                <td>{r.case}</td>
                <td className="num">{r.libstb}</td>
                <td className="num">{r.pillow}</td>
                <td className="num">{r.ratio.toFixed(2)}&times;</td>
              </tr>
            ))}
          </tbody>
        </table>
      </details>
    </div>
  );
}

export function Fonts(): React.ReactElement {
  return (
    <Page>
      {bench.fonts.ops.length > 0 ? (
        <>
          <p>
            {bench.fonts.font}, libstb's <code>Font</code> against Pillow's FreeType binding on its basic layout engine (so Pillow is not charged for
            shaping, which stb_truetype does not do). The two rasterize differently, so this compares speed only. stb_truetype has no security guarantee on
            untrusted fonts; FreeType is hardened. No speed number captures that.
          </p>
          {bench.fonts.ops.map((op) => (
            <FontBlock key={op.key} op={op} />
          ))}
          <Downloads names={['fonts']} />
        </>
      ) : (
        <p>There is no font data in this build.</p>
      )}
    </Page>
  );
}

// ---------------------------------------------------------------- install size

export function InstallSize(): React.ReactElement {
  const install = [...bench.install].sort((a, b) => a.kb - b.kb);
  return (
    <Page>
      {install.length > 0 ? (
        <>
          <p>
            Download size of the wheel for this platform plus every dependency, resolved from PyPI by{' '}
            <a href="https://github.com/MohammadRaziei/pip-size">pip-size</a>. Capability is not the same thing as size: a bigger package is usually doing far
            more, which is why the last column is here.
          </p>
          <div className="panel">
            <h3>
              Total download size <span className="dir">(lower is better)</span>
            </h3>
            <Chart
              label="Total download size of each package, with its dependencies"
              build={(t) =>
                barConfig(
                  install.map((r) => `${r.label} (${r.n_deps} dep${r.n_deps !== 1 ? 's' : ''})`),
                  install.map((r) => Math.round((r.kb / 1024) * 100) / 100),
                  install.map((r) => t.series(r.lib)),
                  'MB (wheel + all dependencies)',
                )
              }
            />
          </div>
          <table className="size-table">
            <thead>
              <tr>
                <th>Package</th>
                <th className="num">Version</th>
                <th className="num">Total</th>
                <th className="num">Deps</th>
                <th>What it is</th>
              </tr>
            </thead>
            <tbody>
              {install.map((r) => (
                <tr key={r.lib} className={r.lib === 'libstb' ? 'self-row' : undefined}>
                  <td>
                    {r.label}
                    {r.lib === 'libstb' ? ' \u2605' : ''}
                  </td>
                  <td className="num">{r.version}</td>
                  <td className="num">{r.total}</td>
                  <td className="num">{r.n_deps}</td>
                  <td className="cap">{r.capability}</td>
                </tr>
              ))}
            </tbody>
          </table>
          <Downloads names={['sizes']} />
        </>
      ) : (
        <p>There is no install-size data in this build.</p>
      )}
    </Page>
  );
}
