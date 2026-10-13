# Configuration file for Sphinx documentation (Python API).

from pathlib import Path
import re
import sys

DOCS_DIR     = Path(__file__).resolve().parent
PROJECT_ROOT = DOCS_DIR.parent.parent.parent
IMAGES_DIR   = PROJECT_ROOT / 'docs' / 'images'

# ── Version ───────────────────────────────────────────────────────────────────
LIBSTB_H = PROJECT_ROOT / 'include' / 'stb.h'
version = '0.0.0'

if LIBSTB_H.exists():
    text   = LIBSTB_H.read_text()
    _major = re.search(r'#define\s+LIBSTB_VERSION_MAJOR\s+(\d+)', text)
    _minor = re.search(r'#define\s+LIBSTB_VERSION_MINOR\s+(\d+)', text)
    _patch = re.search(r'#define\s+LIBSTB_VERSION_PATCH\s+(\d+)', text)
    major  = int(_major.group(1)) if _major else 0
    minor  = int(_minor.group(1)) if _minor else 0
    patch  = int(_patch.group(1)) if _patch else 0
    version = f'{major}.{minor}.{patch}'

# ── Import libstb from the build's python/install dir (the working directory) ──
sys.path.insert(0, '.')

import libstb  # noqa: E402

# ── Project info ──────────────────────────────────────────────────────────────
project   = 'libstb Python'
copyright = '2025, Mohammad Raziei'
author    = 'Mohammad Raziei'
release   = version

# ── Extensions ────────────────────────────────────────────────────────────────
extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.napoleon',
    'sphinx.ext.viewcode',
    'sphinx.ext.autosummary',
]

autodoc_member_order   = 'groupwise'
autodoc_typehints      = 'description'
autosummary_generate   = True

# ── Hide internal libstb_py module - rewrite to libstb ─────────────────────────
add_module_names       = False
modindex_common_prefix = ['libstb.']

def _fix_libstb_py(app, what, name, obj, options, lines):
    """Rewrite libstb_py → libstb in docstring lines."""
    for i, line in enumerate(lines):
        lines[i] = line.replace('libstb_py.', 'libstb.').replace('libstb_py::', 'libstb::')

def _fix_libstb_py_sig(app, what, name, obj, options, signature, return_annotation):
    """Rewrite libstb_py → libstb in signatures."""
    if signature:
        signature = signature.replace('libstb_py.', 'libstb.')
    if return_annotation:
        return_annotation = return_annotation.replace('libstb_py.', 'libstb.')
    return signature, return_annotation

def setup(app):
    app.connect('autodoc-process-docstring', _fix_libstb_py)
    app.connect('autodoc-process-signature', _fix_libstb_py_sig)
    # Patch __module__ on all exported symbols so sphinx shows 'libstb' not 'libstb.libstb_py'
    import libstb as _libstb
    import libstb.libstb_py as _libstb_py
    for _name in dir(_libstb_py):
        _obj = getattr(_libstb_py, _name, None)
        if _obj is not None and hasattr(_obj, '__module__') and _obj.__module__ == 'libstb.libstb_py':
            try:
                _obj.__module__ = 'libstb'
            except (AttributeError, TypeError):
                pass

napoleon_google_docstring = True
napoleon_numpy_docstring  = True

# ── HTML output ───────────────────────────────────────────────────────────────
language         = 'en'
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']
html_theme       = 'libstb'
html_theme_path  = [str(DOCS_DIR / '_themes')]
html_css_files   = []
pygments_style   = 'monokai'

_logo = IMAGES_DIR / 'libstb-sq.svg'
if _logo.exists():
    html_logo = str(_logo)

html_last_updated_fmt = '%Y-%m-%d %H:%M'
