// Minify web assets; preserve global names used by HTML event handlers.
const fs = require('node:fs');
const CleanCSS = require('clean-css');
const {minify} = require('terser');
(async () => {
  let html = fs.readFileSync(0, 'utf8');
  if (process.argv[2] === 'js') {
    process.stdout.write((await minify(html, {compress:{passes:2},mangle:true})).code); return;
  }
  if (process.argv[2] === 'css') {
    const result = new CleanCSS({level:2}).minify(html);
    if (result.errors.length) throw new Error(result.errors.join('\n'));
    process.stdout.write(result.styles); return;
  }
  const scripts = [...html.matchAll(/(<script\b[^>]*>)([\s\S]*?)(<\/script>)/g)];
  for (const match of scripts) {
    const result = await minify(match[2], {
      compress: {passes: 2},
      mangle: true,
      format: {comments: false, inline_script: true},
    });
    html = html.replace(match[0], match[1] + result.code + match[3]);
  }
  // Merge style blocks in source order; retain all selectors and cascade semantics.
  const styles = [...html.matchAll(/<style>([\s\S]*?)<\/style>/g)];
  const css = new CleanCSS({level: 2}).minify(styles.map(m => m[1]).join('\n'));
  if (css.errors.length) throw new Error(css.errors.join('\n'));
  let first = true;
  html = html.replace(/<style>[\s\S]*?<\/style>/g, () => {
    if (!first) return ''; first = false; return '<style>' + css.styles + '</style>';
  });
  process.stdout.write(html);
})().catch(error => { console.error(error); process.exitCode = 1; });
