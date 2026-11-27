import React from 'react';
import Layout from '@theme/Layout';
import BenchmarksPage from '../components/benchmarks/BenchmarksPage';

export default function Benchmarks(): React.ReactElement {
  return (
    <Layout
      title="Benchmarks"
      description="libstb measured against Pillow, OpenCV, imageio and scikit-image: decoding, encoding, resizing, editing, scaling, text and install size."
    >
      <BenchmarksPage />
    </Layout>
  );
}
