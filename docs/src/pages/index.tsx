import React from 'react';
import Layout from '@theme/Layout';
import Home from '../components/home/Home';

export default function Index(): React.ReactElement {
  return (
    <Layout
      title="Images, fonts and zlib with no dependencies"
      description="libstb wraps Sean Barrett's stb libraries in a typed C++17 API and a Python module: decode, resize, edit and encode PNG, JPEG, BMP and TGA, render TrueType text, and compress zlib streams."
    >
      <Home />
    </Layout>
  );
}
