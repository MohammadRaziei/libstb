import type {SidebarsConfig} from '@docusaurus/plugin-content-docs';

const sidebars: SidebarsConfig = {
  main: [
    'intro',
    'install',
    {
      type: 'category',
      label: 'Image',
      items: ['image/resize'],
    },
    {
      type: 'category',
      label: 'Benchmarks',
      items: [
        'benchmarks/overview',
        'benchmarks/method',
        'benchmarks/decoding',
        'benchmarks/encoding',
        'benchmarks/resizing',
        'benchmarks/editing',
        'benchmarks/scaling',
        'benchmarks/fonts',
        'benchmarks/install-size',
      ],
    },
  ],
};

export default sidebars;
