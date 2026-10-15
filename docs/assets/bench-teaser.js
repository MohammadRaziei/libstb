/* The landing page's benchmark teaser. It draws nothing of its own: every number comes from
 * benchmarks/benchmark.json (written by benchmarks/report/gather.py), fetched from next to the
 * published report. If the file is missing the claims keep their static text. */
(function () {
  var LABELS = { libstb: 'libstb', libstb_srgb: 'libstb sRGB', pillow: 'Pillow', opencv: 'OpenCV', imageio: 'imageio', skimage: 'scikit-image', numpy: 'numpy' };
  var ORDER = ['libstb', 'libstb_srgb', 'pillow', 'opencv', 'imageio', 'skimage', 'numpy'];
  function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }
  function sorted(keys) { return keys.slice().sort(function (a, b) { return (ORDER.indexOf(a) + 100) % 100 - (ORDER.indexOf(b) + 100) % 100; }); }

  function bars(rows, aria) {
    rows = rows.slice().sort(function (a, b) { return a.v - b.v; });   // shortest first, same as the report
    var top = Math.max.apply(null, rows.map(function (r) { return r.v; })) || 1;
    var items = rows.map(function (r, i) {
      var pct = Math.max(r.v / top * 100, 1.5).toFixed(1);
      return '<li class="bar-row' + (r.key === 'libstb' ? ' is-self' : '') + '"><span class="bar-label">' + esc(r.label) + '</span>' +
        '<span class="bar-track"><span class="bar s-' + esc(r.key) + '" style="--w:' + pct + '%;--i:' + i + '"></span></span>' +
        '<span class="bar-value">' + esc(r.text) + '</span></li>';
    }).join('');
    return '<ul class="bars anim" role="img" aria-label="' + esc(aria) + '">' + items + '</ul>';
  }

  function speed(host, data) {
    var rows = (data.headline && data.headline.rows) || [];
    if (!rows.length) return;
    var html = rows.map(function (row) {
      var keys = sorted(Object.keys(row.ratios));
      var list = keys.map(function (k) { return { key: k, label: LABELS[k] || k, v: row.ratios[k], text: row.ratios[k].toFixed(row.ratios[k] < 10 ? 2 : 1) + '\u00d7' }; });
      return '<div style="margin-top:1.1rem"><div class="cap" style="margin:0 0 .4rem;font-weight:700;color:var(--fg)">' + esc(row.label) + '</div>' +
        bars(list, row.label + ': time relative to libstb') + '</div>';
    }).join('');
    host.insertAdjacentHTML('afterbegin', html);
  }

  function install(host, data) {
    var rows = (data.headline && data.headline.install) || [];
    rows = rows.filter(function (r) { return r.kb > 0; });
    if (rows.length < 2) return;
    var list = sorted(rows.map(function (r) { return r.lib; })).map(function (k) {
      var r = rows.filter(function (x) { return x.lib === k; })[0];
      return { key: k, label: r.label, v: r.kb, text: r.total };
    });
    host.innerHTML = '<div class="cap" style="margin:1.2rem 0 .4rem;font-weight:700;color:var(--fg)">What pip downloads (wheel plus every dependency)</div>' +
      bars(list, 'Total download size of each package');
  }

  function verify(host, data) {
    var v = data.verify;
    if (!v || typeof v.passed !== 'number') return;
    host.insertAdjacentHTML('afterbegin', '<div class="big">' + v.passed + '<small>output checks passed before timing</small></div>');
  }

  function animate() {
    var all = document.querySelectorAll('.bars.anim');
    if (!('IntersectionObserver' in window)) { all.forEach(function (b) { b.classList.add('in'); }); return; }
    var io = new IntersectionObserver(function (es) { es.forEach(function (e) { if (e.isIntersecting) { e.target.classList.add('in'); io.unobserve(e.target); } }); }, { threshold: .2 });
    all.forEach(function (b) { io.observe(b); });
  }

  fetch('benchmarks/benchmark.json').then(function (r) { if (!r.ok) throw new Error(r.status); return r.json(); }).then(function (data) {
    var a = document.getElementById('proof-speed'), b = document.getElementById('proof-install'), c = document.getElementById('proof-verify');
    if (a) speed(a, data);
    if (b) install(b, data);
    if (c) verify(c, data);
    animate();
  }).catch(function () { /* no report published next to this page: keep the static text */ });
})();
