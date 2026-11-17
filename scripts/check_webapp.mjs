// Checks of web/app.html without a browser or dependencies (run by CI):
//   node scripts/check_webapp.mjs [web/app.html]
// - the app script parses (a syntax error leaves the page blank);
// - every text key exists in both languages (I18N.en / I18N.nl), so no label
//   falls back to its key name;
// - every t("key") / data-i / data-help used in the page has a text.
import { readFileSync } from "node:fs";
import vm from "node:vm";

const file = process.argv[2] || "web/app.html";
const html = readFileSync(file, "utf8");
const errors = [];

// 1. syntax of the app script (the last inline <script>)
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
if (!scripts.length) errors.push("no inline <script> found");
const code = scripts[scripts.length - 1] || "";
try { new vm.Script(code, { filename: file }); }
catch (e) { errors.push(`script syntax: ${e.message}`); }

// 2. the I18N object literal: cut it out by matching braces (skipping strings)
function objectLiteralAfter(src, marker) {
  const start = src.indexOf("{", src.indexOf(marker));
  if (src.indexOf(marker) < 0 || start < 0) return null;
  let depth = 0, q = null;
  for (let i = start; i < src.length; i++) {
    const c = src[i];
    if (q) { if (c === "\\") i++; else if (c === q) q = null; continue; }
    if (c === '"' || c === "'" || c === "`") q = c;
    else if (c === "{") depth++;
    else if (c === "}" && --depth === 0) return src.slice(start, i + 1);
  }
  return null;
}
let I18N = null;
const lit = objectLiteralAfter(code, "const I18N =");
if (!lit) errors.push("const I18N = {...} not found");
else {
  try { I18N = vm.runInNewContext("(" + lit + ")"); }
  catch (e) { errors.push(`I18N does not evaluate: ${e.message}`); }
}

if (I18N) {
  const langs = Object.keys(I18N);
  const all = new Set(langs.flatMap(l => Object.keys(I18N[l])));
  for (const l of langs)
    for (const k of all)
      if (!(k in I18N[l])) errors.push(`text "${k}" missing in ${l}`);

  // 3. keys used by the page
  const used = new Set();
  for (const m of html.matchAll(/data-i="([\w-]+)"/g)) used.add(m[1]);
  for (const m of html.matchAll(/data-help="([\w-]+)"/g)) used.add("h_" + m[1]);
  for (const m of code.matchAll(/\bt\(\s*"([\w-]+)"\s*[,)]/g)) used.add(m[1]);
  for (const k of used)
    if (!(k in I18N[langs[0]])) errors.push(`text "${k}" is used but not defined`);
  console.log(`web app: ${langs.join("/")} × ${all.size} texts, ${used.size} used keys checked`);
}

if (errors.length) {
  for (const e of errors) console.error("ERROR " + e);
  process.exit(1);
}
console.log("web app OK");
