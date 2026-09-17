// Prebundle every board variant for the portable compiler, which has no Node
// dependency at runtime. C++ guards select exactly one variant in each image.
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import { build } from "esbuild";

const root = path.resolve(process.argv[2]);
const output = path.resolve(process.argv[3]);
const boards = ["esp32-s3-super-mini", "esp32-s3-zero", "esp32-s3-psram", "esp32-spk-n16r8",
  "esp32-s3-devkit-c1", "esp32-s3-cam-module", "esp32-wrover", "esp32-wroom", "esp32-mini",
  "wemos-lolin32-mini", "esp32-c3", "esp32-s2-psram", "esp32-c6"];
const app = fs.readFileSync(path.join(root, "web/app.js"), "utf8");
const html = fs.readFileSync(path.join(root, "web/index.html"), "utf8");
const policy = await import("data:text/javascript;base64," + fs.readFileSync(path.join(root, "web/modules/board-pin-policy.js")).toString("base64"));
const evaluate = expression => vm.runInNewContext(`(${expression})`, {}, {timeout: 1000});

function replaceFunction(source, name, replacement, indent = "") {
  const pattern = new RegExp(`^${indent}(?:export )?function ${name}\\([^]*?^${indent}\\}`, "m");
  if (!pattern.test(source)) throw new Error(`Missing specialization point: ${name}`);
  return source.replace(pattern, replacement);
}

for (const [index, board] of boards.entries()) {
  const chip = policy.boardChipFamily(board);
  const folder = path.join(output, "__boards", String(index + 1));
  fs.mkdirSync(folder, {recursive: true});
  let narrowedApp = app.replace(/^const PC_DESIGNER_RUNTIME = .*;$/m, "const PC_DESIGNER_RUNTIME = false;");
  let maps = 0;
  narrowedApp = narrowedApp.replace(/^const (GPIO_BOARD_\w+) = (\{[^]*?^\});/gm, (_, name, literal) => {
    const data = evaluate(literal); ++maps;
    return `const ${name} = ${JSON.stringify(board in data ? {[board]: data[board]} : {})};`;
  });
  if (maps !== 5) throw new Error(`Board map coverage changed: found ${maps}`);
  narrowedApp = narrowedApp.replace(/const WS_STATUS_LED_BOARD_PROFILES = new Set\(\[.*?\]\);/,
    `const WS_STATUS_LED_BOARD_PROFILES = new Set(${JSON.stringify(index < 2 ? [board] : [])});`);
  narrowedApp = replaceFunction(narrowedApp, "boardProfileChipFamily", `function boardProfileChipFamily() { return ${JSON.stringify(chip)}; }`);
  narrowedApp = replaceFunction(narrowedApp, "boardDefaultStatusLedPin", `function boardDefaultStatusLedPin() { return ${chip === "esp32c3" ? 8 : ["esp32-s3-super-mini", "esp32-s3-zero", "esp32-s3-devkit-c1"].includes(board) ? 48 : 22}; }`);
  narrowedApp = replaceFunction(narrowedApp, "detectGpioBoardProfile", `function detectGpioBoardProfile() { return ${JSON.stringify(board)}; }`);

  await build({
    entryPoints: [path.join(root, "web/app.js")], bundle: true, format: "esm", minify: true,
    outfile: path.join(folder, "app.js"), logLevel: "silent",
    plugins: [{name: "fixed-device-board", setup(plugin) {
      plugin.onLoad({filter: /\.js$/}, args => {
        const filename = path.basename(args.path);
        let source = fs.readFileSync(args.path, "utf8");
        if (args.path === path.join(root, "web/app.js")) source = narrowedApp;
        if (filename === "board-pin-policy.js") {
          source = replaceFunction(source, "boardChipFamily", `export function boardChipFamily() { return ${JSON.stringify(chip)}; }`);
          source = replaceFunction(source, "chipPins", `export function chipPins(chip, output = false) { return output ? ${JSON.stringify(policy.chipPins(chip, true, board))} : ${JSON.stringify(policy.chipPins(chip, false, board))}; }`);
        }
        if (filename === "configuration-gpio-tab.js") {
          const match = source.match(/^  function inferBoardAdcHint\([^]*?^  \}/m);
          if (!match) throw new Error("ADC hint specialization point missing");
          const hint = vm.runInNewContext(`${match[0]}; inferBoardAdcHint(${JSON.stringify(board)})`, {}, {timeout: 1000});
          source = replaceFunction(source, "inferBoardAdcHint", `  function inferBoardAdcHint() { return ${JSON.stringify(hint)}; }`, "  ");
          source = source.replace(/\{"esp32-c3":8[^{}]*\}/g, literal => {
            const data = evaluate(literal);
            return JSON.stringify(board in data ? {[board]: data[board]} : {});
          });
          source = source.replaceAll('"esp32-s3-super-mini"', JSON.stringify(board));
        }
        if (filename === "device-migration-tab.js") {
          source = source.replace(/  const boardOptions = (\{[^]*?^  \});/m, (_, literal) => {
            const data = evaluate(literal);
            const options = (data[chip] || []).filter(entry => entry[0] === board);
            return `  const boardOptions = ${JSON.stringify({[chip]: options})};`;
          });
        }
        if (filename === "peripheral-diagram-wiring.js") {
          source = source.replace(/^const (\w+) = (\{\s*"esp32-s3-zero"[^]*?^\});/gm, (_, name, literal) => {
            const data = evaluate(literal);
            return `const ${name} = ${JSON.stringify(board in data ? {[board]: data[board]} : {})};`;
          });
        }
        return {contents: source, loader: "js"};
      });
    }}],
  });
  const narrowedHtml = html.replace(/<option\b[^>]*value="([^"]+)"[^>]*>[^<]*<\/option>/g, (tag, value) => {
    if (!boards.includes(value)) return tag;
    return value === board ? tag.replace(/ selected\b/, "").replace(">", " selected>") : "";
  });
  fs.writeFileSync(path.join(folder, "index.html"), narrowedHtml.replace(/>\s+</g, "><").replace(/\s{2,}/g, " "));
  const javascript = fs.readFileSync(path.join(folder, "app.js"), "utf8");
  for (const other of boards.filter(value => value !== board)) {
    if (javascript.includes(JSON.stringify(other)) || narrowedHtml.includes(`value="${other}"`)) {
      throw new Error(`Unexpected board reference ${other} in ${board}`);
    }
  }
  console.log(`[web-board] ${board}: ${javascript.length} JavaScript bytes; all peripheral definitions retained`);
}
