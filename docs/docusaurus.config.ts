import { themes as prismThemes } from 'prism-react-renderer';
import type { Config } from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

// The site is the whole front of https://mohammadraziei.github.io/libstb/ : home page, lessons under /docs/,
// the benchmarks page, and (copied in beside it) the Doxygen and Sphinx references at cpp/html and python/html.
// CI sets DOCS_URL and DOCS_BASE_URL; the defaults serve a local build from the root.
const url = process.env.DOCS_URL ?? 'http://localhost';
const baseUrl = process.env.DOCS_BASE_URL ?? '/';

const config: Config = {
  title: 'libstb',
  tagline: 'Images, fonts and zlib, with no dependencies',
  url,
  baseUrl,
  favicon: 'img/favicon.svg',
  onBrokenLinks: 'warn',
  // .md files (the synced guide, ADRs and changelog) are CommonMark and shown as they are; only .mdx files use MDX.
  markdown: { format: 'detect', hooks: { onBrokenMarkdownLinks: 'warn' } },

  headTags: [
    { tagName: 'link', attributes: { rel: 'preconnect', href: 'https://fonts.googleapis.com' } },
    { tagName: 'link', attributes: { rel: 'preconnect', href: 'https://fonts.gstatic.com', crossorigin: 'anonymous' } },
    {
      tagName: 'link',
      attributes: {
        rel: 'stylesheet',
        href: 'https://fonts.googleapis.com/css2?family=Archivo:wdth,wght@62..125,100..900&family=JetBrains+Mono:wght@400;600&display=swap',
      },
    },
  ],

  presets: [
    [
      'classic',
      {
        docs: {
          routeBasePath: 'docs',
          sidebarPath: './sidebars.ts',
          editUrl: 'https://github.com/MohammadRaziei/libstb/edit/master/docs/',
        },
        blog: false,
        theme: {
          // tokens.css is generated from docs/assets/libstb-docs.css by scripts/prepare.py, so the colours
          // stay the ones the Doxygen and Sphinx pages use.
          customCss: ['./src/generated/tokens.css', './src/css/custom.css', './src/css/home.css', './src/css/benchmarks.css'],
        },
      } satisfies Preset.Options,
    ],
  ],

  themeConfig: {
    colorMode: { respectPrefersColorScheme: true },
    navbar: {
      title: 'libstb',
      logo: { alt: 'libstb', src: 'assets/libstb-sq.svg' },
      items: [
        { type: 'docSidebar', sidebarId: 'guide', position: 'left', label: 'Guide' },
        { to: '/benchmarks', label: 'Benchmarks', position: 'left' },
        // The references are static pages next to the site, not routes of it.
        { href: `${baseUrl}python/html/index.html`, label: 'Python', position: 'left', target: '_self' },
        { href: `${baseUrl}cpp/html/index.html`, label: 'C++', position: 'left', target: '_self' },
        { href: 'https://github.com/MohammadRaziei/libstb', label: 'GitHub', position: 'right' },
      ],
    },
    footer: {
      style: 'dark',
      links: [
        {
          title: 'Reference',
          items: [
            { label: 'Python reference', href: `${baseUrl}python/html/index.html` },
            { label: 'C++ reference', href: `${baseUrl}cpp/html/index.html` },
            { label: 'Benchmarks', to: '/benchmarks' },
          ],
        },
        {
          title: 'Project',
          items: [
            { label: 'GitHub', href: 'https://github.com/MohammadRaziei/libstb' },
            { label: 'PyPI', href: 'https://pypi.org/project/libstb/' },
          ],
        },
      ],
      copyright:
        'Images, fonts and zlib on top of the stb single-header libraries, with a binding for Python. MIT license.<br />' +
        'Demo photograph: fruits.jpg from OpenCV\'s sample data (Apache-2.0), resized by libstb.',
    },
    prism: { theme: prismThemes.github, darkTheme: prismThemes.dracula },
  } satisfies Preset.ThemeConfig,
};

export default config;
