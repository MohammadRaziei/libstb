import React, {useState} from 'react';
import Layout from '@theme/Layout';
import Link from '@docusaurus/Link';
import {Highlight, themes} from 'prism-react-renderer';
import languages from '@site/languages.json';
import snippets from '@site/src/generated/snippets.json';
import {useLang} from '@site/src/components/LangContext';
import styles from './index.module.css';

type Entry = {code: string; path: string; url: string};
const data = snippets as unknown as Record<string, {files: Record<string, Record<string, Entry>>}>;
const theme = {...themes.vsDark, plain: {...themes.vsDark.plain, backgroundColor: 'transparent', color: '#d7dde4'}};

function Install() {
  const [done, setDone] = useState(false);
  const copy = () => {
    navigator.clipboard?.writeText('pip install libstb').then(() => {
      setDone(true);
      setTimeout(() => setDone(false), 1600);
    });
  };
  return (
    <button type="button" className={styles.install} onClick={copy} aria-label="Copy pip install libstb">
      <span className={styles.prompt}>$</span> pip install libstb
      <span className={styles.copy}>{done ? 'Copied' : 'Copy'}</span>
    </button>
  );
}

function CodeWindow() {
  const {lang, setLang} = useLang();
  const entry = data.resize?.files.main?.[lang] ?? data.resize?.files.main?.[languages[0].id];
  const known = languages.find((l) => l.id === lang) ?? languages[0];
  return (
    <div className={styles.window}>
      <div className={styles.bar}>
        <div role="tablist" aria-label="Language" className={styles.tabs}>
          {languages.map((l) => (
            <button key={l.id} type="button" role="tab" aria-selected={lang === l.id} className={styles.tab} onClick={() => setLang(l.id)}>
              {l.label}
            </button>
          ))}
        </div>
        <span className={styles.file}>{entry?.path.split('/').pop()}</span>
      </div>
      <Highlight theme={theme} code={(entry?.code ?? '').trimEnd().replace(/\n{3,}/g, '\n\n')} language={known.prism ?? 'python'}>
        {({tokens, getLineProps, getTokenProps}) => (
          <pre className={styles.code}>
            {tokens.map((line, i) => (
              <div key={i} {...getLineProps({line})}>
                {line.map((token, k) => (
                  <span key={k} {...getTokenProps({token})} />
                ))}
              </div>
            ))}
          </pre>
        )}
      </Highlight>
      <div className={styles.reply}>
        <span className={styles.replyIn}>$ ls out</span>
        <span className={styles.replyOut}>tiny.png  nearest.png  linear.png  cubic.png  bspline.png  mitchell.png</span>
      </div>
    </div>
  );
}

const facts = [
  {title: 'No dependencies', text: 'No system libraries and no Python dependencies. The Linux x86-64 wheel is under 1 MB, and the C++ library builds from source with CMake.', to: '/docs/install', cta: 'Install'},
  {title: 'Python and C++, one library', text: 'The same operations in libstb.Image and stb::image. Decode PNG, JPEG, BMP, TGA and GIF, edit, resize, draw and encode.', to: '/docs/image/resize', cta: 'Resize an image'},
  {title: 'Text that shapes properly', text: 'TrueType and CFF fonts with GSUB shaping, bidi reordering and kerning, so Persian and Arabic come out right.', to: 'https://github.com/mohammadraziei/libstb/blob/master/docs/guide.md', cta: 'Read the guide'},
  {title: 'Fast where it pays', text: 'SIMD kernels picked at run time with identical results, the GIL released in Python calls, and big resizes split over threads.', to: '/benchmarks', cta: 'See the numbers'},
];

export default function Home() {
  return (
    <Layout title="Images, fonts and zlib for Python and C++" description="Decode, encode, resize, edit and draw images, render text and compress with zlib, from Python and C++. No dependencies.">
      <header className={styles.hero}>
        <div className={styles.heroText}>
          <h1>Bitmaps and typography, in one small library.</h1>
          <p>libstb decodes, encodes, resizes, edits and draws images, renders text and compresses with zlib, from Python and C++. Nothing has to be installed on the system first.</p>
          <div className={styles.actions}>
            <Link className={styles.primary} to="/docs/install">Get started</Link>
            <Link className={styles.secondary} to="/docs/">Read the docs</Link>
          </div>
          <Install />
        </div>
        <CodeWindow />
      </header>

      <main className={styles.facts}>
        {facts.map((f) => (
          <section key={f.title} className={styles.fact}>
            <h2>{f.title}</h2>
            <p>{f.text}</p>
            <Link to={f.to}>{f.cta}</Link>
          </section>
        ))}
      </main>

      <footer className={styles.close}>
        <p>MIT licensed, built in the open.</p>
        <Link to="https://github.com/mohammadraziei/libstb">Star it or open an issue on GitHub</Link>
      </footer>
    </Layout>
  );
}
