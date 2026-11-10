import { themes as prismThemes } from 'prism-react-renderer';
import type { Config } from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

// The deploy address is not known to the skeleton: set DOCS_URL / DOCS_BASE_URL when publishing.
const url = process.env.DOCS_URL ?? 'http://localhost';
const baseUrl = process.env.DOCS_BASE_URL ?? '/';

const config: Config = {
  title: 'libstb',
  tagline: 'Image I/O, resizing, drawing and text for C++ and Python',
  url,
  baseUrl,
  favicon: 'img/favicon.svg',
  onBrokenLinks: 'warn',
  markdown: { hooks: { onBrokenMarkdownLinks: 'warn' } },

  headTags: [
    { tagName: 'link', attributes: { rel: 'preconnect', href: 'https://fonts.googleapis.com' } },
    { tagName: 'link', attributes: { rel: 'preconnect', href: 'https://fonts.gstatic.com', crossorigin: 'anonymous' } },
    {
      tagName: 'link',
      attributes: {
        rel: 'stylesheet',
        href: 'https://fonts.googleapis.com/css2?family=Archivo:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500&display=swap',
      },
    },
  ],

  presets: [
    [
      'classic',
      {
        docs: { routeBasePath: '/', sidebarPath: './sidebars.ts' },
        blog: false,
        theme: { customCss: './src/css/custom.css' },
      } satisfies Preset.Options,
    ],
  ],

  themeConfig: {
    colorMode: { respectPrefersColorScheme: true },
    navbar: {
      title: 'libstb',
      items: [
        { type: 'docSidebar', sidebarId: 'guide', position: 'left', label: 'Guide' },
        // Static output of Doxygen and Sphinx, copied into static/api by scripts/prepare.py.
        { href: `${baseUrl}api/cpp/html/index.html`, label: 'C++ reference', position: 'left', target: '_self' },
        { href: `${baseUrl}api/python/html/index.html`, label: 'Python reference', position: 'left', target: '_self' },
        { href: 'https://github.com/MohammadRaziei/libstb', label: 'GitHub', position: 'right' },
      ],
    },
    prism: { theme: prismThemes.github, darkTheme: prismThemes.dracula },
  } satisfies Preset.ThemeConfig,
};

export default config;
