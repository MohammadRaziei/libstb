import {themes as prismThemes} from 'prism-react-renderer';
import type {Config} from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'libstb',
  tagline: 'Images, fonts and zlib for Python and C++, with no dependencies',
  favicon: 'img/favicon.svg',
  // GitHub Pages of the repository (deployed by the orchestrator, job deploy-pages).
  // CI sets DOCS_URL and DOCS_BASE_URL from the repository; the defaults are the published address.
  url: process.env.DOCS_URL ?? 'https://mohammadraziei.github.io',
  baseUrl: process.env.DOCS_BASE_URL ?? '/libstb/',
  organizationName: 'mohammadraziei',
  projectName: 'libstb',
  onBrokenLinks: 'throw',
  onBrokenMarkdownLinks: 'warn',
  i18n: {defaultLocale: 'en', locales: ['en']},
  stylesheets: [
    'https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600&family=Public+Sans:wght@400;500;700;800&display=swap',
  ],
  presets: [
    [
      'classic',
      {
        docs: {routeBasePath: 'docs', sidebarPath: './sidebars.ts'},
        blog: false,
        theme: {customCss: ['./src/css/tokens.css', './src/css/custom.css', './src/css/benchmarks.css']},
      } satisfies Preset.Options,
    ],
  ],
  themeConfig: {
    colorMode: {respectPrefersColorScheme: true},
    navbar: {
      logo: {alt: 'libstb', src: 'img/logo.svg', srcDark: 'img/logo-dark.svg', height: 26},
      items: [
        {type: 'docSidebar', sidebarId: 'main', label: 'Docs', position: 'left'},
        {to: '/docs/benchmarks/overview', label: 'Benchmarks', position: 'left'},
        {type: 'custom-langSwitch', position: 'right'},
        {href: 'https://github.com/mohammadraziei/libstb', label: 'GitHub', position: 'right'},
      ],
    },
    prism: {theme: prismThemes.vsDark, darkTheme: prismThemes.vsDark, additionalLanguages: ['cpp', 'bash']},
  } satisfies Preset.ThemeConfig,
};

export default config;
