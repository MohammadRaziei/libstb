import React from 'react';
import useBaseUrl from '@docusaurus/useBaseUrl';
import symbols from '@site/src/generated/symbols.json';
import names from '@site/api-map.json';

type Props = {
  /** Key in api-map.json, for example "image.resize". */
  name: string;
};

type Names = Record<string, { cpp: string; python: string }>;
const map = names as unknown as Names;
const found = symbols as Record<string, { cpp?: string; python?: string }>;

function Ref({ to, children }: { to?: string; children: string }): React.ReactElement {
  const href = useBaseUrl(to ?? '/');
  // A plain anchor on purpose: the reference pages are static files, not routes of this site.
  return to ? (
    <a href={href}>
      <code>{children}</code>
    </a>
  ) : (
    <code>{children}</code>
  );
}

/**
 * Links the same function in the C++ and the Python reference. The target addresses come from
 * the Doxygen tag file and Sphinx objects.inv (scripts/prepare.py symbols); a symbol that did
 * not resolve is shown as plain text, never as a dead link.
 */
export default function ApiLink({ name }: Props): React.ReactElement {
  const n = map[name];
  if (!n) {
    throw new Error(`ApiLink: "${name}" is not in api-map.json`);
  }
  const f = found[name] ?? {};
  return (
    <span className="stb-api">
      C++ <Ref to={f.cpp}>{n.cpp}</Ref> · Python <Ref to={f.python}>{n.python}</Ref>
    </span>
  );
}
