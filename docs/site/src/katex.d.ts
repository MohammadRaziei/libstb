declare module 'katex' {
  const katex: { renderToString(tex: string, options?: { throwOnError?: boolean; displayMode?: boolean }): string };
  export default katex;
}
