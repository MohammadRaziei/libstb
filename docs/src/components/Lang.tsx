import React from 'react';
import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';
import CodeBlock from '@theme/CodeBlock';
import snippets from '@site/src/generated/snippets.json';
import languages from '@site/languages.json';

type Props = {
  /** File name without extension under snippets/<language>/. */
  name: string;
};

const all = snippets as Record<string, Record<string, string>>;

/**
 * One tab per language, filled from the real snippet files. All Lang blocks share the groupId
 * "lang", so picking a language switches every block on every page (kept in localStorage);
 * `?lang=python` in the URL preselects a tab.
 */
export default function Lang({ name }: Props): React.ReactElement {
  const entry = all[name];
  if (!entry) {
    throw new Error(`Lang: no snippet named "${name}". Run: python3 scripts/prepare.py snippets`);
  }
  const items = languages.languages
    .filter((l) => entry[l.id] !== undefined)
    .map((l) => (
      <TabItem key={l.id} value={l.id} label={l.label}>
        <CodeBlock language={l.prism} title={`${name}.${l.ext}`}>
          {entry[l.id]}
        </CodeBlock>
      </TabItem>
    ));
  return (
    <Tabs groupId="lang" queryString="lang">
      {items}
    </Tabs>
  );
}
