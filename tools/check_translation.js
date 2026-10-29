// Checks a translation folder (mcm_<lang>.tsv + strings.json) against the English originals:
// the same keys, and the same {placeholders} in every text.
// usage: node tools/check_translation.js <skyrimnet_MiniGame folder> <translation folder>
const fs = require("fs");
const path = require("path");

const [base, dir] = process.argv.slice(2);
const problems = [];

// strings.json against the view's built-in English
const html = fs.readFileSync(path.join(base, "view", "index.html"), "utf8");
const english = new Function(`return ${html.match(/const STRINGS = (\{[\s\S]*?\n        \});/)[1]};`)();
const json = fs.readFileSync(path.join(dir, "strings.json"), "utf8");
const translated = JSON.parse(json);
// JSON.parse keeps only the last of a key written twice
const jsonKeys = [...json.matchAll(/^\s*"(\w+)"\s*:/gm)].map((m) => m[1]);
for (const key of new Set(jsonKeys.filter((k, i) => jsonKeys.indexOf(k) !== i))) {
    problems.push(`strings.json ${key}: written more than once`);
}
const ours = translated.view || {};
const placeholders = (text) => (text.match(/\{\w+\}/g) || []).sort().join(" ");
for (const key of Object.keys(english)) {
    if (!(key in ours)) {
        problems.push(`strings.json view.${key}: missing`);
    } else if (placeholders(english[key]) !== placeholders(ours[key])) {
        problems.push(`strings.json view.${key}: placeholders ${placeholders(ours[key])} instead of ${placeholders(english[key])}`);
    }
}
for (const key of Object.keys(ours)) {
    if (!(key in english)) {
        problems.push(`strings.json view.${key}: not in the English original`);
    }
}
for (const section of Object.keys(translated).filter((s) => s !== "view")) {
    problems.push(`strings.json ${section}: not in the English original`);
}

// the MCM list against translations/mcm_en.tsv
const keys = (file) => fs.readFileSync(file, "utf8").split(/\r?\n/).filter(Boolean).map((l) => l.split("\t")[0]);
const mcmEn = keys(path.join(base, "translations", "mcm_en.tsv"));
const mcmFile = fs.readdirSync(dir).find((f) => /^mcm_\w+\.tsv$/.test(f));
const mcm = keys(path.join(dir, mcmFile));
for (const key of mcmEn.filter((k) => !mcm.includes(k))) problems.push(`${mcmFile} ${key}: missing`);
for (const key of mcm.filter((k) => !mcmEn.includes(k))) problems.push(`${mcmFile} ${key}: not in mcm_en.tsv`);

if (problems.length) {
    console.error(problems.join("\n"));
    process.exit(1);
}
console.log(`translation in ${dir} matches the English originals`);
