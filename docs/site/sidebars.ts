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
  ],
};

export default sidebars;
