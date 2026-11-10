import React from 'react';
import data from '@site/src/generated/benchmark.json';

type Op = {
  key: string;
  title: string;
  note?: string;
  libs: string[];
  reference: string;
  n_compared: number;
  time_ratio: Record<string, number>;
};
type Section = { id: string; title: string; ops: Op[] };
type Data = {
  generated?: string;
  repeats?: number;
  n_entries?: number;
  libraries: Record<string, string>;
  system?: { cpu_model?: string; logical_cores?: number };
  sections: Section[];
};

const bench = data as unknown as Data;

type Props = {
  /** Section id in benchmark.json, for example "resizing". */
  section: string;
  /** Operation keys to show, for example ["resize_down_cubic"]. All of the section when omitted. */
  ops?: string[];
};

const plain = (html: string) => html.replace(/<[^>]+>/g, '');
const fmt = (r: number) => (r < 10 ? r.toFixed(2) : r.toFixed(1));

function Block({ op }: { op: Op }): React.ReactElement {
  // time_ratio holds target time / reference time; the reference itself is 1 by definition.
  const rows = op.libs
    .map((id) => ({
      id,
      label: bench.libraries[id] ?? id,
      ratio: id === op.reference ? 1 : op.time_ratio[id],
    }))
    .filter((r) => typeof r.ratio === 'number')
    .sort((a, b) => a.ratio - b.ratio);
  const max = Math.max(10, ...rows.map((r) => r.ratio));
  // Log scale: ratios range from 1 to well over 100. The 0.3 offset keeps the reference bar visible.
  const width = (r: number) => ((Math.log10(r) + 0.3) / (Math.log10(max) + 0.3)) * 100;
  return (
    <div className="stb-bench__op">
      <h4>{op.title}</h4>
      <ul className="stb-bench__rows">
        {rows.map((r) => (
          <li key={r.id} className={r.id.startsWith('libstb') ? 'stb-bench__row stb-bench__row--own' : 'stb-bench__row'}>
            <span className="stb-bench__name">{r.label}</span>
            <span className="stb-bench__bar" aria-hidden="true">
              <span style={{ width: `${width(r.ratio)}%` }} />
            </span>
            <span className="stb-bench__value">
              {fmt(r.ratio)}×{r.id === op.reference ? ' (reference)' : ''}
            </span>
          </li>
        ))}
      </ul>
      {op.note ? <p className="stb-bench__note">{plain(op.note)}</p> : null}
    </div>
  );
}

/**
 * Reads the committed benchmark.json (the site build never runs benchmarks). For each operation
 * the reference library is the one that was fastest on most images, so it differs per operation.
 */
export default function Bench({ section, ops }: Props): React.ReactElement {
  const sec = bench.sections.find((s) => s.id === section);
  const shown = sec ? sec.ops.filter((o) => !ops || ops.includes(o.key)) : [];
  if (shown.length === 0) {
    return (
      <p className="stb-bench__note">
        No benchmark data for “{section}”. Run the benchmarks and then <code>python3 scripts/prepare.py bench</code>.
      </p>
    );
  }
  const n = shown[0].n_compared;
  return (
    <div className="stb-bench">
      {shown.map((op) => (
        <Block key={op.key} op={op} />
      ))}
      <p className="stb-bench__foot">
        Time of each library divided by the time of the reference (lower is better), averaged over the
        pictures of the corpus ({n} compared per operation). Log scale. Measured {bench.generated ?? 'unknown date'},{' '}
        {bench.repeats ?? '?'} repeats, {bench.system?.cpu_model ?? 'unknown CPU'}, {bench.system?.logical_cores ?? '?'} logical
        core(s). Source: <code>benchmarks/outputs/benchmark.json</code>.
      </p>
    </div>
  );
}
